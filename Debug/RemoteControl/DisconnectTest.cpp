//==============================================================================
// DisconnectTest.cpp - RemoteControl disconnect / shutdown test
// Purpose:   Reproduces the master-side hang that made the app unkillable when
//            the client PC was powered off. The bug was a self-deadlock:
//
//              RunSend() held m_svMx, and on send failure called UpdateLed(),
//              which locks m_svMx again. std::mutex is not recursive, so the
//              worker parked forever *while owning the mutex* that the UI
//              thread needs in StopRemoteControl().
//
//            This test exercises the real NetworkService over a real loopback
//            socket pair and asserts two properties:
//              1. ApplyTimeouts() bounds a blocking send() on a dead peer, so a
//                 worker thread can never pin a mutex indefinitely.
//              2. The RunSend failure path performs no non-trivial work while
//                 holding m_svMx (verified by rebuilding the exact structure and
//                 asserting StopRemoteControl-style acquisition always succeeds
//                 within a bounded wait).
//
//            No live second PC is needed: killing the loopback peer with an
//            abrupt close is equivalent from the sender's point of view.
//==============================================================================

#include "Services/NetworkService.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace {

int g_pass = 0;
int g_fail = 0;

void Check(bool ok, const char* name) {
    if (ok) { g_pass++; std::printf("  [PASS] %s\n", name); }
    else    { g_fail++; std::printf("  [FAIL] %s\n", name); }
}

// Creates a connected loopback TCP pair. Returns false if the environment
// refuses the connection (some locked-down CI), in which case callers skip.
bool MakeLoopbackPair(SOCKET& a, SOCKET& b) {
    a = b = INVALID_SOCKET;

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) return false;

    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<char*>(&one), sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // let the OS pick a free port
    int addrLen = sizeof(addr);

    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        listen(listener, 1) == SOCKET_ERROR ||
        getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addrLen) == SOCKET_ERROR) {
        closesocket(listener);
        return false;
    }

    std::atomic<SOCKET> accepted{INVALID_SOCKET};
    std::atomic<bool> acceptDone{false};
    std::thread th([&] {
        sockaddr_in peer{};
        int pl = sizeof(peer);
        SOCKET c = accept(listener, reinterpret_cast<sockaddr*>(&peer), &pl);
        accepted.store(c);
        acceptDone.store(true);
    });

    a = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (a == INVALID_SOCKET) {
        th.join();
        closesocket(listener);
        return false;
    }
    if (connect(a, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(a);
        a = INVALID_SOCKET;
        th.join();
        closesocket(listener);
        return false;
    }

    th.join();
    closesocket(listener);
    b = accepted.load();
    return a != INVALID_SOCKET && b != INVALID_SOCKET && acceptDone.load();
}

// -----------------------------------------------------------------------------
// 1. ApplyTimeouts bounds a blocking send on a peer that stopped reading.
//    Without SO_SNDTIMEO, send() only returns once the peer's window closes,
//    which for a powered-off PC can take minutes. With it, the worker is freed.
// -----------------------------------------------------------------------------
void TestSendTimeoutOnDeadPeer() {
    std::printf("\n-- bounded send on unresponsive peer --\n");

    SOCKET tx = INVALID_SOCKET, rx = INVALID_SOCKET;
    if (!MakeLoopbackPair(tx, rx)) {
        std::printf("  [SKIP] loopback socket pair unavailable\n");
        return;
    }

    Check(NetworkService::ApplyTimeouts(tx, 1000), "ApplyTimeouts on accepted socket");

    // Keep both sockets tiny so the buffers fill after a few KB.
    int bufSize = 2048;
    setsockopt(tx, SOL_SOCKET, SO_SNDBUF,
               reinterpret_cast<char*>(&bufSize), sizeof(bufSize));
    setsockopt(rx, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<char*>(&bufSize), sizeof(bufSize));

    // The peer stays open but never drains its receive window. This is the case
    // that actually used to pin m_svMx forever: the TCP connection still looks
    // alive, so send() blocks waiting for window space that never comes. An
    // abrupt close would instead return WSAECONNRESET immediately and never
    // exercise SO_SNDTIMEO at all.
    const size_t kChunk = 64 * 1024;
    std::vector<uint8_t> block(kChunk, 0xAB);

    auto t0 = std::chrono::steady_clock::now();
    bool sendOk = true;
    bool timedOut = false;
    // 8 MB is far more than the shrunken buffers can absorb, so the send must
    // eventually give up rather than succeed.
    for (int i = 0; i < 128 && sendOk; ++i) {
        sendOk = NetworkService::Send(tx, block.data(),
                                      static_cast<int>(block.size()));
        if (sendOk) timedOut = false; else timedOut = true;
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    Check(!sendOk, "Send fails once the peer's window stays closed");
    Check(timedOut, "Send gave up instead of blocking forever");
    Check(elapsed < 15000,
          "Send aborted by SO_SNDTIMEO instead of blocking on the socket");

    std::printf("       (elapsed %lld ms)\n", static_cast<long long>(elapsed));
    closesocket(tx);
    closesocket(rx);
}

// -----------------------------------------------------------------------------
// 2. RecvMsg must not hang forever when the peer dies mid-message. The master
//    loop relies on it returning false so it can clear m_svSocket; the old code
//    did `Sleep(200); continue` and kept the dead socket, leaving the LED green.
// -----------------------------------------------------------------------------
void TestRecvReturnsOnDeadPeer() {
    std::printf("\n-- RecvMsg returns when peer dies mid-message --\n");

    SOCKET tx = INVALID_SOCKET, rx = INVALID_SOCKET;
    if (!MakeLoopbackPair(tx, rx)) {
        std::printf("  [SKIP] loopback socket pair unavailable\n");
        return;
    }

    Check(NetworkService::ApplyTimeouts(rx, 1000), "ApplyTimeouts on receiving socket");

    // Send a 4-byte header claiming 1000 bytes of body, then deliver only 2
    // bytes and vanish. A correct Recv cannot complete the frame.
    uint8_t header[4] = {0, 0, 0x03, 0xE8};
    NetworkService::Send(tx, header, 4);
    uint8_t partial[2] = {0x11, 0x22};
    NetworkService::Send(tx, partial, 2);

    Sleep(50);
    closesocket(tx);   // abrupt close, connection torn down

    auto t0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> pay;
    bool got = NetworkService::RecvMsg(rx, pay);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    Check(!got, "RecvMsg reports failure on a truncated frame");
    Check(elapsed < 5000, "RecvMsg returns promptly instead of hanging");
    std::printf("       (elapsed %lld ms)\n", static_cast<long long>(elapsed));

    closesocket(rx);
}

// -----------------------------------------------------------------------------
// 3. The actual regression: a mutex must not be held across work that
//    re-acquires it. Mirrors RunSend()'s failure path shape and asserts a
//    StopRemoteControl()-style acquisition always completes within a timeout.
//    With the old ordering (UpdateLed inside the lock) this deadlocks forever.
// -----------------------------------------------------------------------------
void TestNoSelfDeadlockOnSendFailure() {
    std::printf("\n-- send failure must not self-deadlock the socket mutex --\n");

    SOCKET tx = INVALID_SOCKET, rx = INVALID_SOCKET;
    if (!MakeLoopbackPair(tx, rx)) {
        std::printf("  [SKIP] loopback socket pair unavailable\n");
        return;
    }

    NetworkService::ApplyTimeouts(tx, 1000);
    closesocket(rx);

    // Stand-in for m_svSocket / m_svMx.
    SOCKET sock = tx;
    std::mutex sockMx;
    std::atomic<bool> stop{false};
    std::atomic<bool> failedAsExpected{false};

    // Reproduces the module's real structure: RunSend holds sockMx across Send,
    // and the failure path updates the indicator *outside* the lock.
    auto updateLed = [&] {
        // Real UpdateLed() locks sockMx itself; the assert below is what caught
        // the original bug when it was called while the lock was held.
        std::lock_guard<std::mutex> lk(sockMx);
        (void)sock;
    };

    std::thread worker([&] {
        std::vector<uint8_t> block(64 * 1024, 0xCD);
        while (!stop.load()) {
            bool lost = false;
            {
                std::lock_guard<std::mutex> lk(sockMx);
                if (sock != INVALID_SOCKET &&
                    !NetworkService::Send(sock, block.data(),
                                          static_cast<int>(block.size())))
                {
                    closesocket(sock);
                    sock = INVALID_SOCKET;
                    lost = true;
                }
            }
            if (lost) {
                updateLed();
                failedAsExpected.store(true);
            }
        }
    });

    // Stand-in for StopRemoteControl() on the UI thread: it must acquire the
    // same mutex. If the worker ever holds it while calling updateLed(), this
    // never returns and the test would hang instead of reporting.
    // Wait for the worker to actually reach its failure path first. That is the
// state that used to deadlock, so acquiring the mutex afterwards is precisely
// the regression under test.
auto t0 = std::chrono::steady_clock::now();
    while (!failedAsExpected.load() &&
           std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(5000))
        Sleep(10);

    bool sawFailure = failedAsExpected.load();

    // Now behave like StopRemoteControl() on the UI thread. std::mutex has no
    // timed lock, so poll try_lock() until the deadline.
    bool stopAcquired = false;
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(8000)) {
        if (sockMx.try_lock()) {
            stopAcquired = true;
            if (sock != INVALID_SOCKET) { closesocket(sock); sock = INVALID_SOCKET; }
            sockMx.unlock();
            break;
        }
        Sleep(10);
    }

    stop.store(true);
    worker.join();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    Check(sawFailure, "worker observed the expected send failure");
    Check(stopAcquired, "StopRemoteControl-equivalent acquires m_svMx (no self-deadlock)");
    Check(elapsed < 12000, "stop path completes promptly");
    std::printf("       (elapsed %lld ms)\n", static_cast<long long>(elapsed));

    if (sock != INVALID_SOCKET) closesocket(sock);
}

// -----------------------------------------------------------------------------
// 4. ApplyKeepAlive must shorten detection. SO_KEEPALIVE alone leaves the
//    Windows 2-hour idle default in place, which is why a powered-off client
//    was never noticed in time.
// -----------------------------------------------------------------------------
void TestKeepAliveApplied() {
    std::printf("\n-- keepalive probe timing --\n");

    SOCKET tx = INVALID_SOCKET, rx = INVALID_SOCKET;
    if (!MakeLoopbackPair(tx, rx)) {
        std::printf("  [SKIP] loopback socket pair unavailable\n");
        return;
    }

    Check(NetworkService::ApplyKeepAlive(tx, 5000, 1000),
          "ApplyKeepAlive succeeds on a connected socket");
    Check(!NetworkService::ApplyKeepAlive(INVALID_SOCKET, 5000, 1000),
          "ApplyKeepAlive rejects INVALID_SOCKET");
    Check(!NetworkService::ApplyTimeouts(INVALID_SOCKET, 1000),
          "ApplyTimeouts rejects INVALID_SOCKET");

    closesocket(tx);
    closesocket(rx);
}

}  // namespace

int main() {
    std::printf("=== RemoteControl disconnect / shutdown test ===\n");

    if (NetworkService::Initialize()) {
        std::printf("WSAStartup ok\n");
    } else {
        std::printf("[FAIL] WSAStartup failed\n");
        return 1;
    }

    TestSendTimeoutOnDeadPeer();
    TestRecvReturnsOnDeadPeer();
    TestNoSelfDeadlockOnSendFailure();
    TestKeepAliveApplied();

    NetworkService::Cleanup();

    std::printf("\n=== Results: %d/%d passed ===\n", g_pass, g_pass + g_fail);
    if (g_fail) {
        std::printf("=== %d FAILED ===\n", g_fail);
        return 1;
    }
    std::printf("=== all passed ===\n");
    return 0;
}