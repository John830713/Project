//==============================================================================
// DiagnosticsTest.cpp - Remote diagnostics / deploy wire format test
// Purpose:   Covers the DBG_REQ/DBG_RESP + EXE_SWAP/EXE_RESULT backdoor
//            channel added for remote debugging (spec section 10):
//              - Proto builders produce correctly framed payloads
//              - seq echo round-trip over a real loopback socket pair
//              - unknown message types survive framing (old peers ignore
//                them, new peers must never choke on them)
//              - BuildSnapshotText renders every snapshot section
//            The snapshot *collection* (Win32 reads) lives in the module and
//            is exercised manually; this test covers the pure formatting.
//==============================================================================

#include "Modules/RemoteControl/RemoteControlTypes.h"
#include "Modules/RemoteControl/RemoteControlDiagnostics.h"
#include "Services/NetworkService.h"

#include <winsock2.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_pass = 0;
int g_fail = 0;

void Check(bool ok, const char* name) {
    if (ok) { g_pass++; std::printf("  [PASS] %s\n", name); }
    else    { g_fail++; std::printf("  [FAIL] %s\n", name); }
}

// Connected loopback pair (same pattern as DisconnectTest).
bool MakeLoopbackPair(SOCKET& a, SOCKET& b) {
    a = b = INVALID_SOCKET;

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int addrLen = sizeof(addr);

    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
            SOCKET_ERROR ||
        listen(listener, 1) == SOCKET_ERROR ||
        getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addrLen) ==
            SOCKET_ERROR) {
        closesocket(listener);
        return false;
    }

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) {
        closesocket(listener);
        return false;
    }

    if (connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
        SOCKET_ERROR) {
        closesocket(client);
        closesocket(listener);
        return false;
    }

    a = accept(listener, nullptr, nullptr);
    closesocket(listener);
    if (a == INVALID_SOCKET) {
        closesocket(client);
        return false;
    }
    b = client;
    return true;
}

//------------------------------------------------------------------------------
// Framing
//------------------------------------------------------------------------------

void TestFraming() {
    std::printf("\n-- Proto framing --\n");

    auto req = RemoteControl::Proto::dbgReq(0x1234);
    Check(req.size() == 7, "dbgReq frame = 4 len + 3 payload");
    Check(RemoteControl::Proto::r32(req.data()) == 3, "dbgReq length prefix");
    Check(req[4] == RemoteControl::Proto::DBG_REQ, "dbgReq type byte");
    Check(RemoteControl::Proto::r16(req.data() + 5) == 0x1234,
          "dbgReq carries the seq");

    const char text[] = "line1\nline2\n";
    auto resp = RemoteControl::Proto::dbgResp(0x1234, text, sizeof(text) - 1);
    Check(RemoteControl::Proto::r32(resp.data()) == 3 + sizeof(text) - 1,
          "dbgResp length prefix covers seq + text");
    Check(resp[4] == RemoteControl::Proto::DBG_RESP, "dbgResp type byte");
    Check(RemoteControl::Proto::r16(resp.data() + 5) == 0x1234,
          "dbgResp echoes the seq");
    Check(std::memcmp(resp.data() + 7, text, sizeof(text) - 1) == 0,
          "dbgResp payload intact");

    auto swap = RemoteControl::Proto::exeSwap(5699912ULL, "Project.exe", 11);
    Check(swap.size() == 4 + 9 + 11, "exeSwap frame size");
    Check(swap[4] == RemoteControl::Proto::EXE_SWAP, "exeSwap type byte");
    Check(RemoteControl::Proto::r64(swap.data() + 5) == 5699912ULL,
          "exeSwap carries the expected file size");
    Check(std::memcmp(swap.data() + 13, "Project.exe", 11) == 0,
          "exeSwap carries the filename");

    auto res = RemoteControl::Proto::exeResult(false, "not newer", 9);
    Check(res.size() == 4 + 2 + 9, "exeResult frame size");
    Check(res[4] == RemoteControl::Proto::EXE_RESULT, "exeResult type byte");
    Check(res[5] == 0, "exeResult failure flag");
    Check(std::memcmp(res.data() + 6, "not newer", 9) == 0,
          "exeResult message intact");
}

//------------------------------------------------------------------------------
// Loopback round-trip
//------------------------------------------------------------------------------

void TestLoopbackRoundtrip() {
    std::printf("\n-- Loopback round-trip --\n");

    SOCKET a = INVALID_SOCKET, b = INVALID_SOCKET;
    if (!MakeLoopbackPair(a, b)) {
        std::printf("  [SKIP] loopback unavailable\n");
        return;
    }

    // A -> B: diagnostics request
    auto req = RemoteControl::Proto::dbgReq(77);
    Check(NetworkService::Send(a, req.data(), static_cast<int>(req.size())),
          "send dbgReq");

    std::vector<uint8_t> got;
    Check(NetworkService::RecvMsg(b, got),
          "peer receives dbgReq frame");
    Check(got.size() == 3 && got[0] == RemoteControl::Proto::DBG_REQ,
          "received payload is DBG_REQ");
    uint16_t seq = RemoteControl::Proto::r16(got.data() + 1);
    Check(seq == 77, "seq survives the wire");

    // B -> A: echoed response
    auto resp = RemoteControl::Proto::dbgResp(seq, "ok", 2);
    Check(NetworkService::Send(b, resp.data(), static_cast<int>(resp.size())),
          "send dbgResp");
    std::vector<uint8_t> got2;
    Check(NetworkService::RecvMsg(a, got2), "requester receives dbgResp frame");
    Check(got2.size() == 5 && got2[0] == RemoteControl::Proto::DBG_RESP &&
              RemoteControl::Proto::r16(got2.data() + 1) == 77,
          "DBG_RESP echoes seq 77");

    // Unknown type still frames/parses (old-peer tolerance at the wire level).
    uint8_t payload[2] = { 200, 0xAB };
    auto unk = RemoteControl::Proto::frame(payload, 2);
    Check(NetworkService::Send(a, unk.data(), static_cast<int>(unk.size())),
          "send unknown type");
    std::vector<uint8_t> got3;
    Check(NetworkService::RecvMsg(b, got3) && got3.size() == 2 &&
              got3[0] == 200 && got3[1] == 0xAB,
          "unknown type passes through framing untouched");

    closesocket(a);
    closesocket(b);
}

//------------------------------------------------------------------------------
// Snapshot formatting
//------------------------------------------------------------------------------

void TestSnapshotText() {
    std::printf("\n-- Snapshot formatting --\n");

    RemoteControl::Diag::SnapshotData d;
    d.role = "server";
    d.pid = 1234;
    d.uptimeSec = 345;
    d.exePath = "C:\\Apps\\Project.exe";
    d.version = "2026.1006.1900.1200";
    d.running = true;
    d.epoch = 4;
    d.queueDepth = 2;
    d.hookLocal = 1;
    d.listenSock = "0x1a4";
    d.serverSock = "0x2b8";
    d.clientSock = "-";
    d.thrAccept = d.thrSend = d.thrSvRecv = d.thrLed = true;
    d.txPkt = 100; d.txBytes = 2048; d.rxPkt = 90; d.rxBytes = 1024;
    d.lastTxAgoMs = 12; d.lastRxAgoMs = 5;
    d.uiState = "responsive (3 ms)";
    d.uiVisible = true;
    d.logTail = { "[19:00:00] [+] Client authenticated",
                  "[19:00:01] [*] Clipboard synced" };

    std::string t = RemoteControl::Diag::BuildSnapshotText(d);

    Check(t.find("[session] role=server pid=1234 uptime=345s "
                 "version=2026.1006.1900.1200 exe=\"C:\\Apps\\Project.exe\"") !=
              std::string::npos,
          "session line complete");
    Check(t.find("[runtime] running=1 epoch=4 queue=2 hookLocal=1 hookRemote=0") !=
              std::string::npos,
          "runtime line");
    Check(t.find("[sockets] listen=0x1a4 server=0x2b8 client=-") != std::string::npos,
          "sockets line");
    Check(t.find("[threads] accept=1 send=1 svRecv=1 client=0 led=1") !=
              std::string::npos,
          "thread liveness line");
    Check(t.find("txPkt=100 txBytes=2048 rxPkt=90 rxBytes=1024") != std::string::npos,
          "wire counters");
    Check(t.find("lastTxAgo=12ms lastRxAgo=5ms") != std::string::npos,
          "counter ages");
    Check(t.find("[ui] responsive (3 ms) visible=1") != std::string::npos,
          "ui probe line");
    Check(t.find("tail (2 lines)") != std::string::npos, "log tail header");
    Check(t.find("  [19:00:01] [*] Clipboard synced") != std::string::npos,
          "log tail lines are indented");

    // Default/hung rendering must not be empty anywhere the caller relies on.
    RemoteControl::Diag::SnapshotData empty;
    std::string t2 = RemoteControl::Diag::BuildSnapshotText(empty);
    Check(t2.find("running=0") != std::string::npos &&
              t2.find("tail (0 lines)") != std::string::npos,
          "default snapshot renders without gaps");
}

}  // namespace

//==============================================================================

int main() {
    NetworkService::Initialize();

    TestFraming();
    TestLoopbackRoundtrip();
    TestSnapshotText();

    NetworkService::Cleanup();

    std::printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
