#include "NetworkService.h"

bool NetworkService::Initialize() {
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0;
}

void NetworkService::Cleanup() {
    WSACleanup();
}

bool NetworkService::Send(SOCKET s, const void* d, int n) {
    if (n < 0) return false;
    if (n > 0 && d == nullptr) return false;

    auto* p = static_cast<const char*>(d);
    while (n > 0) {
        int r = send(s, p, n, 0);
        if (r == SOCKET_ERROR || r == 0) return false;
        p += r;
        n -= r;
    }
    return true;
}

bool NetworkService::Recv(SOCKET s, void* d, int n) {
    if (n < 0) return false;
    if (n > 0 && d == nullptr) return false;

    auto* p = static_cast<char*>(d);
    while (n > 0) {
        int r = recv(s, p, n, 0);
        if (r == SOCKET_ERROR || r == 0) return false;
        p += r;
        n -= r;
    }
    return true;
}

bool NetworkService::RecvMsg(SOCKET s, std::vector<uint8_t>& out) {
    uint8_t h[4];
    if (!Recv(s, h, 4)) return false;

    uint32_t n = (static_cast<uint32_t>(h[0]) << 24) |
                 (static_cast<uint32_t>(h[1]) << 16) |
                 (static_cast<uint32_t>(h[2]) << 8)  |
                  static_cast<uint32_t>(h[3]);

    if (n > kMaxMsgSize) return false;

    out.resize(n);
    if (n == 0) return true;
    return Recv(s, out.data(), static_cast<int>(n));
}

bool NetworkService::ApplyTimeouts(SOCKET s, DWORD ms) {
    if (s == INVALID_SOCKET) return false;
    return setsockopt(s, SOL_SOCKET, SO_SNDTIMEO,
                      reinterpret_cast<char*>(&ms), sizeof(ms)) == 0 &&
           setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<char*>(&ms), sizeof(ms)) == 0;
}

bool NetworkService::ApplyKeepAlive(SOCKET s, DWORD idleMs, DWORD intervalMs) {
    if (s == INVALID_SOCKET) return false;

    int on = 1;
    if (setsockopt(s, SOL_SOCKET, SO_KEEPALIVE,
                   reinterpret_cast<char*>(&on), sizeof(on)) == SOCKET_ERROR)
        return false;

    // MinGW's tcp_keepalive has no keepalivecnt field, so the probe count stays
    // at the Windows default of 10. At a 1s interval that is still ~10s.
    tcp_keepalive kv{};
    kv.onoff = 1;
    kv.keepalivetime = idleMs;
    kv.keepaliveinterval = intervalMs;

    DWORD returned = 0;
    return WSAIoctl(s, SIO_KEEPALIVE_VALS, &kv, sizeof(kv),
                    nullptr, 0, &returned, nullptr, 0) == 0;
}
