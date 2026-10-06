//==============================================================================
// RemoteControlDiagnostics.h - Runtime snapshot for the DBG_REQ/DBG_RESP channel
//
// The module fills SnapshotData with Win32 reads (sockets, thread liveness,
// UI probe, log tail) and BuildSnapshotText renders the UTF-8 report that the
// requester appends to Log/RemoteDiagnostics.txt. Formatting is pure and
// header-only so DiagnosticsTest can verify it without linking the module.
//==============================================================================

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace RemoteControl {
namespace Diag {

struct SnapshotData {
    // Identity / session
    std::string role;          // "server" (listens) or "client" (dials out)
    uint32_t pid = 0;
    uint64_t uptimeSec = 0;
    std::string exePath;       // UTF-8
    std::string version;       // "a.b.c.d" (0.0.0.0 = no VERSIONINFO)

    // Runtime state
    bool running = false;
    unsigned epoch = 0;
    size_t queueDepth = 0;
    int hookLocal = 0;         // this machine's F9 hook active
    int hookRemote = 0;        // peer's hook state mirrored for the LED

    // Sockets ("-"/"0" = invalid)
    std::string listenSock;
    std::string serverSock;
    std::string clientSock;

    // Worker threads alive (accept/send/svRecv/client/led)
    bool thrAccept = false;
    bool thrSend = false;
    bool thrSvRecv = false;
    bool thrClient = false;
    bool thrLed = false;

    // Wire counters
    uint64_t txPkt = 0;
    uint64_t txBytes = 0;
    uint64_t rxPkt = 0;
    uint64_t rxBytes = 0;
    uint64_t lastTxAgoMs = 0;  // age of last successful send (0 = never)
    uint64_t lastRxAgoMs = 0;

    // UI responsiveness: SendMessageTimeout(WM_NULL, SMTO_ABORTIFHUNG).
    // "responsive (N ms)" / "HUNG" / "no window" — detects the hidden-idle
    // freeze even when the worker threads are still healthy.
    std::string uiState;
    bool uiVisible = false;

    // Last lines of this side's module log, oldest first (UTF-8).
    std::vector<std::string> logTail;
};

inline std::string Bool1(bool b) { return b ? "1" : "0"; }

inline std::string BuildSnapshotText(const SnapshotData& d) {
    std::string out;
    char num[64];

    out += "[session] role=" + d.role;
    std::snprintf(num, sizeof(num), " pid=%u uptime=%llus",
                  static_cast<unsigned>(d.pid),
                  static_cast<unsigned long long>(d.uptimeSec));
    out += num;
    out += " version=" + d.version + " exe=\"" + d.exePath + "\"\n";

    out += "[runtime] running=" + Bool1(d.running);
    std::snprintf(num, sizeof(num), " epoch=%u queue=%llu hookLocal=%d hookRemote=%d",
                  d.epoch, static_cast<unsigned long long>(d.queueDepth),
                  d.hookLocal, d.hookRemote);
    out += num;
    out += "\n";

    out += "[sockets] listen=" + d.listenSock + " server=" + d.serverSock +
           " client=" + d.clientSock + "\n";

    out += "[threads] accept=" + Bool1(d.thrAccept) + " send=" + Bool1(d.thrSend) +
           " svRecv=" + Bool1(d.thrSvRecv) + " client=" + Bool1(d.thrClient) +
           " led=" + Bool1(d.thrLed) + "\n";

    out += "[counters]";
    std::snprintf(num, sizeof(num),
                  " txPkt=%llu txBytes=%llu rxPkt=%llu rxBytes=%llu",
                  static_cast<unsigned long long>(d.txPkt),
                  static_cast<unsigned long long>(d.txBytes),
                  static_cast<unsigned long long>(d.rxPkt),
                  static_cast<unsigned long long>(d.rxBytes));
    out += num;
    std::snprintf(num, sizeof(num), " lastTxAgo=%llums lastRxAgo=%llums",
                  static_cast<unsigned long long>(d.lastTxAgoMs),
                  static_cast<unsigned long long>(d.lastRxAgoMs));
    out += num;
    out += "\n";

    out += "[ui] " + d.uiState + " visible=" + Bool1(d.uiVisible) + "\n";

    out += "[log] tail (" + std::to_string(d.logTail.size()) + " lines)\n";
    for (const auto& line : d.logTail) {
        out += "  " + line + "\n";
    }
    return out;
}

}  // namespace Diag
}  // namespace RemoteControl
