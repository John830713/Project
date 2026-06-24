#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <shobjidl.h>
#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <string>
#include <mutex>

namespace FileTransferService {

constexpr uint32_t kChunkSize = 65536;

struct ReceiveState {
    HANDLE        hFile    = INVALID_HANDLE_VALUE;
    std::string   name;
    uint64_t      total    = 0;
    uint64_t      received = 0;
    std::wstring  saveBase;
};

using ProgressCallback = void(*)(const char* name,
                                  uint64_t done,
                                  uint64_t total,
                                  int finished,
                                  void* ud);

void SetProgressCB(ProgressCallback cb, void* ud);
void SetProgressHwnd(HWND hwnd);

bool StartSvSender(SOCKET* pSock, std::mutex* sockMx);
bool StartClSender(SOCKET* pSock, std::mutex* sockMx);
bool QueueSend(const wchar_t* path);
bool QueueSendAll(const wchar_t* path);

bool ProcessMsg(const uint8_t* data, uint32_t len,
                ReceiveState& rx, const wchar_t* saveDir);

void ResetRecv(ReceiveState& rx);
void Progress(const char* prefix, uint64_t done, uint64_t total);

}
