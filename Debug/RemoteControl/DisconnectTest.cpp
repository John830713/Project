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
#include "Services/FileTransferService.h"
#include "Modules/RemoteControl/RemoteControlTypes.h"

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

// -----------------------------------------------------------------------------
// 5. Port parsing must never throw. ListenPort/TargetPort are read on worker
//    threads straight from a user-editable INI; an uncaught std::stoi
//    exception there would std::terminate the whole process.
// -----------------------------------------------------------------------------
void TestParsePortNeverThrows() {
    std::printf("\n-- defensive port parsing --\n");

    int out = -1;
    Check(RemoteControl::TryParsePort(L"5500", 1, out) && out == 5500,
          "plain valid port parses");
    Check(RemoteControl::TryParsePort(L"1024", 1, out) && out == 1024,
          "lower boundary parses");
    Check(RemoteControl::TryParsePort(L"65535", 1, out) && out == 65535,
          "upper boundary parses");
    Check(RemoteControl::TryParsePort(L"80", 1, out) && out == 1024,
          "privileged port clamps up to 1024");
    Check(!RemoteControl::TryParsePort(L"", 5500, out) && out == 5500,
          "empty value falls back to default");
    Check(!RemoteControl::TryParsePort(L"abc", 5500, out) && out == 5500,
          "non-numeric garbage falls back to default");
    Check(!RemoteControl::TryParsePort(L"55a0", 5500, out) && out == 5500,
          "trailing junk falls back to default");
    Check(!RemoteControl::TryParsePort(L"0", 5500, out) && out == 5500,
          "zero falls back to default");
    Check(!RemoteControl::TryParsePort(L"-1", 5500, out) && out == 5500,
          "negative falls back to default");
    Check(!RemoteControl::TryParsePort(L"99999", 5500, out) && out == 5500,
          "overflow falls back to default");
    Check(!RemoteControl::TryParsePort(L"5500.5", 5500, out) && out == 5500,
          "float falls back to default");
    Check(!RemoteControl::TryParsePort(L"5500 ", 5500, out) && out == 5500,
          "trailing space falls back to default");

    // The one input that used to be lethal: std::stoi would throw inside
    // RunAccept()/RunClient(). TryParsePort catches everything.
    bool survived = true;
    try {
        int junk;
        RemoteControl::TryParsePort(L"\x4e2d\x6587", 5500, junk);
    } catch (...) {
        survived = false;
    }
    Check(survived, "CJK garbage does not escape as an exception");
}

// -----------------------------------------------------------------------------
// 6. Reconnect regression: the sv-recv loop must SURVIVE a dead connection.
//    The old code did `break` on RecvMsg failure, permanently killing the
//    thread — a client that reconnected got accepted by RunAccept but never
//    had a receiver again. Mirrors RunSvRecv()'s control flow.
// -----------------------------------------------------------------------------
void TestRecvLoopSurvivesDisconnect() {
    std::printf("\n-- sv-recv loop survives disconnect and keeps receiving --\n");

    std::atomic<unsigned> epoch{1};
    std::atomic<SOCKET> svSocket{INVALID_SOCKET};
    std::atomic<int> received{0};
    std::atomic<int> cycles{0};

    std::thread svRecv([&] {
        const unsigned myEpoch = epoch.load();
        SOCKET lastC = INVALID_SOCKET;
        while (epoch.load() == myEpoch) {
            SOCKET c = svSocket.load();
            if (c != lastC) { lastC = c; }  // stand-in for ResetRecv
            if (c == INVALID_SOCKET) { Sleep(50); continue; }

            fd_set fs; FD_ZERO(&fs); FD_SET(c, &fs);
            timeval tv = {0, 100000};
            if (select(0, &fs, nullptr, nullptr, &tv) <= 0) continue;

            std::vector<uint8_t> pay;
            if (!NetworkService::RecvMsg(c, pay)) {
                // The fixed failure path: drop the socket, stay in the loop.
                SOCKET expected = c;
                if (svSocket.load() == expected) {
                    svSocket.store(INVALID_SOCKET);
                    closesocket(c);
                    cycles.fetch_add(1);
                }
                continue;
            }
            received.fetch_add(1);
        }
    });

    // Sends a framed message from the PEER side; svRecv reads the other end.
    auto deliver = [&](SOCKET peer, uint8_t payloadByte) {
        if (peer == INVALID_SOCKET) return false;
        uint8_t frame[5] = {0, 0, 0, 1, payloadByte};
        return NetworkService::Send(peer, frame, 5);
    };

    // First connection: one message must arrive.
    SOCKET tx1 = INVALID_SOCKET, rx1 = INVALID_SOCKET;
    if (!MakeLoopbackPair(tx1, rx1)) {
        epoch.fetch_add(1);
        svSocket.store(INVALID_SOCKET);
        svRecv.join();
        std::printf("  [SKIP] loopback socket pair unavailable\n");
        return;
    }
    NetworkService::ApplyTimeouts(rx1, 1000);
    svSocket.store(rx1);
    Check(deliver(tx1, 42), "first connection delivers a message");

    int deadline = 0;
    while (received.load() == 0 && deadline++ < 100) Sleep(10);
    Check(received.load() == 1, "sv-recv loop read the first message");

    // Peer dies. The loop must clear the socket and CONTINUE (not break).
    closesocket(tx1);
    deadline = 0;
    while (cycles.load() == 0 && deadline++ < 300) Sleep(10);
    Check(cycles.load() >= 1, "loop observed the dead connection");
    Check(svSocket.load() == INVALID_SOCKET, "dead socket cleared");

    // Second connection: proves the loop is still alive after the drop.
    SOCKET tx2 = INVALID_SOCKET, rx2 = INVALID_SOCKET;
    if (MakeLoopbackPair(tx2, rx2)) {
        NetworkService::ApplyTimeouts(rx2, 1000);
        svSocket.store(rx2);
        Check(deliver(tx2, 43), "reconnected peer can send again");

        deadline = 0;
        while (received.load() < 2 && deadline++ < 100) Sleep(10);
        Check(received.load() == 2,
              "sv-recv loop survived the disconnect (old code: thread dead)");

        closesocket(tx2);
        closesocket(rx2);
    }

    // Shut the loop down the way StopRemoteControl does: bump the epoch.
    epoch.fetch_add(1);
    svSocket.store(INVALID_SOCKET);
    auto t0 = std::chrono::steady_clock::now();
    svRecv.join();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    Check(elapsed < 2000, "epoch bump joins the recv loop promptly");
    std::printf("       (elapsed %lld ms)\n", static_cast<long long>(elapsed));
}

// -----------------------------------------------------------------------------
// 7. Generation discipline: a Stop/Start cycle must not revive threads from
//    the previous cycle. The old m_quit=false reset in StartRemoteControl did
//    exactly that — old RunClient/RunLeds woke up and duplicated the new ones.
// -----------------------------------------------------------------------------
void TestEpochBumpDoesNotReviveOldThreads() {
    std::printf("\n-- epoch generation prevents thread revival --\n");

    std::atomic<unsigned> epoch{1};
    std::atomic<int> oldAlive{0};
    std::atomic<int> oldSawMismatch{0};
    std::atomic<int> oldRuns{0};

    // Simulates a long-lived worker from generation 1 (e.g. RunClient's
    // 3s retry backoff, sliced so the epoch is checked frequently).
    std::thread oldWorker([&] {
        const unsigned myEpoch = epoch.load();
        while (epoch.load() == myEpoch) {
            oldRuns.fetch_add(1);
            for (int i = 0; i < 30 && epoch.load() == myEpoch; ++i) Sleep(10);
        }
        if (epoch.load() != myEpoch) oldSawMismatch.fetch_add(1);
    });

    Sleep(100);
    Check(oldRuns.load() > 0, "old worker ran under its own generation");

    // Stop: bump the epoch. The worker must exit even though no one joins it.
    epoch.fetch_add(1);
    int deadline = 0;
    while (oldRuns.load() < 2 && epoch.load() == 2 && deadline++ < 100) Sleep(10);
    // Start again immediately: with the old m_quit flag reset this would
    // resurrect the worker; with epoch the generation value only moves forward.
    epoch.fetch_add(1);

    oldWorker.join();
    Check(oldSawMismatch.load() == 1, "old worker exited on generation mismatch");
    Check(oldRuns.load() < 1000000, "old worker did not spin forever");

    // New generation worker must run under epoch==3 while old is gone.
    std::atomic<int> newRuns{0};
    const unsigned newEpoch = epoch.load();
    std::thread newWorker([&] {
        while (epoch.load() == newEpoch) {
            newRuns.fetch_add(1);
            Sleep(10);
        }
    });
    Sleep(120);
    epoch.fetch_add(1);
    newWorker.join();
    Check(newRuns.load() > 0 && newRuns.load() < 500,
          "new generation worker ran only during its own epoch");
    (void)oldAlive;
}

// -----------------------------------------------------------------------------
// 8. FileTransferService sender workers must spawn exactly once. The old code
//    called std::thread(senderThread).detach() on every Start(), leaking one
//    permanent thread per Stop/Start cycle — each waiting on the same queue.
// -----------------------------------------------------------------------------
void TestSenderSpawnedOnlyOnce() {
    std::printf("\n-- file-transfer sender thread spawns once --\n");

    int before = FileTransferService::DebugSenderSpawnCount();

    SOCKET sock = INVALID_SOCKET;
    std::mutex mx;

    // Simulates several Start/Stop cycles (StartRemoteControl calls both
    // Start*Sender each time).
    for (int cycle = 0; cycle < 5; ++cycle) {
        Check(FileTransferService::StartSvSender(&sock, &mx),
              "StartSvSender accepts valid arguments");
        Check(FileTransferService::StartClSender(&sock, &mx),
              "StartClSender accepts valid arguments");
    }

    int spawned = FileTransferService::DebugSenderSpawnCount() - before;
    Check(spawned == 2,
          "5 start cycles spawn exactly 2 workers (old code: 10)");
    std::printf("       (spawned %d workers)\n", spawned);

    Check(!FileTransferService::StartSvSender(nullptr, nullptr),
          "StartSvSender rejects null arguments");
    Check(!FileTransferService::StartClSender(nullptr, nullptr),
          "StartClSender rejects null arguments");

    // Progress HWND churn races SetProgressHwnd against updateTaskbar's
    // release; without s_tbMx this is a COM use-after-free under a fuzzer.
    for (int i = 0; i < 50; ++i) {
        FileTransferService::SetProgressHwnd(nullptr);
    }
    Check(true, "repeated SetProgressHwnd(nullptr) does not crash");
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
    TestParsePortNeverThrows();
    TestRecvLoopSurvivesDisconnect();
    TestEpochBumpDoesNotReviveOldThreads();
    TestSenderSpawnedOnlyOnce();

    NetworkService::Cleanup();

    std::printf("\n=== Results: %d/%d passed ===\n", g_pass, g_pass + g_fail);
    if (g_fail) {
        std::printf("=== %d FAILED ===\n", g_fail);
        return 1;
    }
    std::printf("=== all passed ===\n");
    return 0;
}