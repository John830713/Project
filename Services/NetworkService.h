#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <vector>

class NetworkService {
public:
    static bool Initialize();
    static void Cleanup();

    static bool Send(SOCKET s, const void* d, int n);
    static bool Recv(SOCKET s, void* d, int n);
    static bool RecvMsg(SOCKET s, std::vector<uint8_t>& out);

    static constexpr uint32_t kMaxMsgSize = 10 * 1024 * 1024;
};
