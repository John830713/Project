#include "FileTransferService.h"
#include "NetworkService.h"
#include "../Modules/RemoteControl/RemoteControlTypes.h"

#include <cstdio>
#include <queue>
#include <thread>
#include <condition_variable>
#include <string>
#include <vector>

namespace FileTransferService {

static ProgressCallback s_progressCB = nullptr;
static void* s_progressUD = nullptr;

void SetProgressCB(ProgressCallback cb, void* ud) {
    s_progressCB = cb;
    s_progressUD = ud;
}

static HWND s_progHwnd = nullptr;
static ITaskbarList3* s_taskbar = nullptr;

void SetProgressHwnd(HWND hwnd) {
    s_progHwnd = hwnd;
    if (s_taskbar) { s_taskbar->Release(); s_taskbar = nullptr; }
    if (!hwnd) return;
    CoCreateInstance(CLSID_TaskbarList, nullptr,
                     CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s_taskbar));
    if (s_taskbar) s_taskbar->HrInit();
}

static void updateTaskbar(uint64_t done, uint64_t total) {
    if (!s_taskbar || !s_progHwnd) return;
    if (total == 0 || done >= total) {
        s_taskbar->SetProgressState(s_progHwnd, TBPF_NOPROGRESS);
    } else {
        s_taskbar->SetProgressState(s_progHwnd, TBPF_NORMAL);
        s_taskbar->SetProgressValue(s_progHwnd, done, total);
    }
}

static void notifyProgress(const char* name, uint64_t done,
                           uint64_t total, bool finished) {
    if (s_progressCB)
        s_progressCB(name, done, total, finished ? 1 : 0, s_progressUD);
    updateTaskbar(done, total);
}

static std::string toUtf8(const wchar_t* s) {
    if (!s) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string r(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, &r[0], n, nullptr, nullptr);
    return r;
}

static std::wstring toWide(const char* s, int len) {
    if (!s || len < 0) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, len, nullptr, 0);
    if (n <= 0) return {};
    std::wstring r(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, len, &r[0], n);
    return r;
}

static const wchar_t* baseName(const wchar_t* p) {
    if (!p) return L"";
    const wchar_t* a = wcsrchr(p, L'\\');
    const wchar_t* b = wcsrchr(p, L'/');
    if (b && (!a || b > a)) a = b;
    return a ? a + 1 : p;
}

static void ensureDir(const wchar_t* dir) {
    if (!dir || !*dir) return;
    wchar_t tmp[MAX_PATH];
    wcsncpy(tmp, dir, MAX_PATH - 1);
    tmp[MAX_PATH - 1] = 0;
    for (wchar_t* p = tmp + 1; *p; p++) {
        if (*p == L'\\' || *p == L'/') {
            wchar_t c = *p; *p = 0;
            CreateDirectoryW(tmp, nullptr);
            *p = c;
        }
    }
    CreateDirectoryW(tmp, nullptr);
}

void Progress(const char* prefix, uint64_t done, uint64_t total) {
    if (total == 0) return;
    int pct  = static_cast<int>(done * 100 / total);
    int bars = pct * 40 / 100;
    double dMB = static_cast<double>(done)  / (1024.0 * 1024.0);
    double tMB = static_cast<double>(total) / (1024.0 * 1024.0);
    std::printf("\r  %s [", prefix ? prefix : "");
    for (int i = 0; i < 40; i++) std::putchar(i < bars ? '=' : ' ');
    std::printf("] %3d%% %6.1f / %.1f MB  ", pct, dMB, tMB);
    std::fflush(stdout);
}

static SOCKET* s_pSock = nullptr;
static std::mutex* s_pMx = nullptr;

static bool lockedSend(const void* d, int n) {
    if (!s_pSock || !s_pMx) return false;
    std::lock_guard<std::mutex> lk(*s_pMx);
    if (*s_pSock == INVALID_SOCKET) return false;
    return NetworkService::Send(*s_pSock, d, n);
}

static bool sendOneFile(const wchar_t* absPath, const std::string& relName) {
    HANDLE hf = CreateFileW(absPath, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) {
        notifyProgress(relName.c_str(), 0, 0, true);
        return false;
    }

    LARGE_INTEGER li{};
    if (!GetFileSizeEx(hf, &li)) { CloseHandle(hf); return false; }

    uint64_t size = static_cast<uint64_t>(li.QuadPart);
    notifyProgress(relName.c_str(), 0, size, false);

    auto fb = RemoteControl::Proto::fileBegin(relName.c_str(), static_cast<int>(relName.size()), size);
    if (!lockedSend(fb.data(), static_cast<int>(fb.size()))) {
        CloseHandle(hf);
        notifyProgress(relName.c_str(), 0, size, true);
        return false;
    }

    uint8_t buf[kChunkSize];
    uint64_t sent = 0;

    while (sent < size) {
        DWORD nr = 0;
        if (!ReadFile(hf, buf, kChunkSize, &nr, nullptr) || nr == 0) break;

        auto fc = RemoteControl::Proto::fileChunk(buf, static_cast<int>(nr));
        if (!lockedSend(fc.data(), static_cast<int>(fc.size()))) {
            CloseHandle(hf);
            notifyProgress(relName.c_str(), sent, size, true);
            return false;
        }

        sent += nr;
        notifyProgress(relName.c_str(), sent, size, false);
        Sleep(0);
    }

    CloseHandle(hf);
    auto fe = RemoteControl::Proto::simple(RemoteControl::Proto::FT_END);
    lockedSend(fe.data(), static_cast<int>(fe.size()));
    notifyProgress(relName.c_str(), size, size, true);
    return sent == size;
}

static void sendDir(const wchar_t* absBase, const wchar_t* absDir,
                    const std::string& relBase) {
    std::string relPath = relBase.empty()
        ? toUtf8(baseName(absDir))
        : relBase;

    auto dp = RemoteControl::Proto::dirBegin(relPath.c_str(), static_cast<int>(relPath.size()));
    lockedSend(dp.data(), static_cast<int>(dp.size()));

    std::wstring pattern = std::wstring(absDir) + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 ||
            wcscmp(fd.cFileName, L"..") == 0) continue;

        std::wstring childAbs = std::wstring(absDir) + L"\\" + fd.cFileName;
        std::string  childRel = relPath + "/" + toUtf8(fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            sendDir(absBase, childAbs.c_str(), childRel);
        } else {
            sendOneFile(childAbs.c_str(), childRel);
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

static std::queue<std::wstring> s_sendQ;
static std::mutex s_sendMx;
static std::condition_variable s_sendCv;

static void senderThread() {
    while (true) {
        std::wstring path;
        {
            std::unique_lock<std::mutex> lk(s_sendMx);
            s_sendCv.wait(lk, [] { return !s_sendQ.empty(); });
            path = s_sendQ.front();
            s_sendQ.pop();
        }

        DWORD attr = GetFileAttributesW(path.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) continue;

        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            sendDir(path.c_str(), path.c_str(), "");
        } else {
            std::string relName = toUtf8(baseName(path.c_str()));
            sendOneFile(path.c_str(), relName);
        }
    }
}

bool StartSvSender(SOCKET* ps, std::mutex* mx) {
    if (!ps || !mx) return false;
    s_pSock = ps;
    s_pMx   = mx;
    std::thread(senderThread).detach();
    return true;
}

/* ── Client-direction sender (m_clSocket) ── */
static SOCKET*     s_pSockCl = nullptr;
static std::mutex* s_pMxCl   = nullptr;

static bool lockedSendCl(const void* d, int n) {
    if (!s_pSockCl || !s_pMxCl) return false;
    std::lock_guard<std::mutex> lk(*s_pMxCl);
    if (*s_pSockCl == INVALID_SOCKET) return false;
    return NetworkService::Send(*s_pSockCl, d, n);
}

static bool sendOneFileCl(const wchar_t* absPath, const std::string& relName) {
    HANDLE hf = CreateFileW(absPath, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) {
        notifyProgress(relName.c_str(), 0, 0, true);
        return false;
    }
    LARGE_INTEGER li{};
    if (!GetFileSizeEx(hf, &li)) { CloseHandle(hf); return false; }
    uint64_t size = static_cast<uint64_t>(li.QuadPart);
    notifyProgress(relName.c_str(), 0, size, false);
    auto fb = RemoteControl::Proto::fileBegin(relName.c_str(), static_cast<int>(relName.size()), size);
    if (!lockedSendCl(fb.data(), static_cast<int>(fb.size()))) {
        CloseHandle(hf);
        notifyProgress(relName.c_str(), 0, size, true);
        return false;
    }
    uint8_t buf[kChunkSize];
    uint64_t sent = 0;
    while (sent < size) {
        DWORD nr = 0;
        if (!ReadFile(hf, buf, kChunkSize, &nr, nullptr) || nr == 0) break;
        auto fc = RemoteControl::Proto::fileChunk(buf, static_cast<int>(nr));
        if (!lockedSendCl(fc.data(), static_cast<int>(fc.size()))) {
            CloseHandle(hf);
            notifyProgress(relName.c_str(), sent, size, true);
            return false;
        }
        sent += nr;
        notifyProgress(relName.c_str(), sent, size, false);
        Sleep(0);
    }
    CloseHandle(hf);
    auto fe = RemoteControl::Proto::simple(RemoteControl::Proto::FT_END);
    lockedSendCl(fe.data(), static_cast<int>(fe.size()));
    notifyProgress(relName.c_str(), size, size, true);
    return sent == size;
}

static void sendDirCl(const wchar_t* absBase, const wchar_t* absDir,
                      const std::string& relBase) {
    std::string relPath = relBase.empty()
        ? toUtf8(baseName(absDir))
        : relBase;
    auto dp = RemoteControl::Proto::dirBegin(relPath.c_str(), static_cast<int>(relPath.size()));
    lockedSendCl(dp.data(), static_cast<int>(dp.size()));
    std::wstring pattern = std::wstring(absDir) + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring childAbs = std::wstring(absDir) + L"\\" + fd.cFileName;
        std::string  childRel = relPath + "/" + toUtf8(fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            sendDirCl(absBase, childAbs.c_str(), childRel);
        else
            sendOneFileCl(childAbs.c_str(), childRel);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

static std::queue<std::wstring> s_sendQCl;
static std::mutex s_sendMxCl;
static std::condition_variable s_sendCvCl;

static void senderThreadCl() {
    while (true) {
        std::wstring path;
        {
            std::unique_lock<std::mutex> lk(s_sendMxCl);
            s_sendCvCl.wait(lk, [] { return !s_sendQCl.empty(); });
            path = s_sendQCl.front();
            s_sendQCl.pop();
        }
        DWORD attr = GetFileAttributesW(path.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) continue;
        if (attr & FILE_ATTRIBUTE_DIRECTORY)
            sendDirCl(path.c_str(), path.c_str(), "");
        else {
            std::string relName = toUtf8(baseName(path.c_str()));
            sendOneFileCl(path.c_str(), relName);
        }
    }
}

bool StartClSender(SOCKET* ps, std::mutex* mx) {
    if (!ps || !mx) return false;
    s_pSockCl = ps;
    s_pMxCl   = mx;
    std::thread(senderThreadCl).detach();
    return true;
}

bool QueueSend(const wchar_t* path) {
    if (!path || !*path) return false;
    {
        std::lock_guard<std::mutex> lk(s_sendMx);
        s_sendQ.push(path);
    }
    s_sendCv.notify_one();
    return true;
}

bool QueueSendAll(const wchar_t* path) {
    if (!path || !*path) return false;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lk(s_sendMx);
        s_sendQ.push(path);
        ok = true;
    }
    s_sendCv.notify_one();
    {
        std::lock_guard<std::mutex> lk(s_sendMxCl);
        s_sendQCl.push(path);
    }
    s_sendCvCl.notify_one();
    return ok;
}

bool ProcessMsg(const uint8_t* p, uint32_t n,
                ReceiveState& rx, const wchar_t* saveDir) {
    if (!p || n < 1) return false;

    switch (p[0]) {

    case RemoteControl::Proto::FT_DIR_BEGIN: {
        if (n < 3) return true;
        uint16_t nlen = RemoteControl::Proto::r16(p + 1);
        if (static_cast<uint32_t>(3 + nlen) > n) return true;

        std::string relPath(reinterpret_cast<const char*>(p + 3), nlen);
        std::wstring wrel = toWide(relPath.c_str(), static_cast<int>(relPath.size()));
        for (auto& c : wrel) if (c == L'/') c = L'\\';

        std::wstring fullDir = std::wstring(saveDir ? saveDir : L"") + L"\\" + wrel;
        ensureDir(fullDir.c_str());
        rx.saveBase = std::wstring(saveDir ? saveDir : L"");

        notifyProgress(relPath.c_str(), 0, 0, true);
        return true;
    }

    case RemoteControl::Proto::FT_BEGIN: {
        if (n < 11) return true;
        uint16_t nlen = RemoteControl::Proto::r16(p + 1);
        if (static_cast<uint32_t>(3 + nlen + 8) > n) return true;

        ResetRecv(rx);
        rx.name.assign(reinterpret_cast<const char*>(p + 3), nlen);
        rx.total    = RemoteControl::Proto::r64(p + 3 + nlen);
        rx.received = 0;

        std::wstring wrel = toWide(rx.name.c_str(), static_cast<int>(rx.name.size()));
        for (auto& c : wrel) if (c == L'/') c = L'\\';

        std::wstring outPath = std::wstring(saveDir ? saveDir : L"") + L"\\" + wrel;
        std::wstring parentDir = outPath.substr(0, outPath.find_last_of(L'\\'));
        ensureDir(parentDir.c_str());

        rx.hFile = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (rx.hFile == INVALID_HANDLE_VALUE) return true;

        notifyProgress(rx.name.c_str(), 0, rx.total, false);
        return true;
    }

    case RemoteControl::Proto::FT_CHUNK_MSG: {
        if (rx.hFile == INVALID_HANDLE_VALUE || n < 2) return true;
        DWORD nw = 0;
        if (!WriteFile(rx.hFile, p + 1, static_cast<DWORD>(n - 1), &nw, nullptr)) {
            ResetRecv(rx);
            return true;
        }
        rx.received += nw;
        notifyProgress(rx.name.c_str(), rx.received, rx.total, false);
        return true;
    }

    case RemoteControl::Proto::FT_END: {
        if (rx.hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(rx.hFile);
            rx.hFile = INVALID_HANDLE_VALUE;
        }
        notifyProgress(rx.name.c_str(), rx.total, rx.total, true);
        rx = {};
        return true;
    }

    default:
        return false;
    }
}

void ResetRecv(ReceiveState& rx) {
    if (rx.hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(rx.hFile);
        rx.hFile = INVALID_HANDLE_VALUE;
    }
    rx = {};
}

} // namespace FileTransferService
