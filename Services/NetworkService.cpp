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
