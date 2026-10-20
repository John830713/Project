#pragma once

#include <windows.h>

#ifndef DEBUG_CONSOLE
#define DEBUG_CONSOLE 0
#endif

#if DEBUG_CONSOLE
#include <cstdio>
#include <string>

inline FILE*& DbgFileRef() {
    static FILE* f = nullptr;
    return f;
}

// When set, DBG() mirrors its text here so the named-pipe server can return
// command output to an automated caller.
std::wstring& DbgCaptureBuffer();
bool& DbgCaptureActive();

inline void DebugConsoleOpen() {
    AllocConsole();
    FILE* dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
    freopen_s(&dummy, "CONIN$", "r", stdin);
    SetConsoleTitleW(L"Project Debug Console");
    _wfopen_s(&DbgFileRef(), L"debug_log.txt", L"w");
    wprintf(L"[DebugConsole] Console opened.  g_dbgFile=%p\n", (void*)DbgFileRef());
    if (DbgFileRef()) fwprintf(DbgFileRef(), L"[DebugConsole] Console opened.\n");
}

inline void DebugConsoleClose() {
    wprintf(L"[DebugConsole] Console closing.\n");
    if (DbgFileRef()) { fclose(DbgFileRef()); DbgFileRef() = nullptr; }
    FreeConsole();
}

#define DBG(fmt, ...) \
    do { \
        wprintf(L"[DBG] " fmt L"\n", ##__VA_ARGS__); \
        if (DbgFileRef()) { fwprintf(DbgFileRef(), L"[DBG] " fmt L"\n", ##__VA_ARGS__); fflush(DbgFileRef()); } \
        if (DbgCaptureActive()) { \
            wchar_t _dbgBuf[4096]; \
            swprintf_s(_dbgBuf, L"[DBG] " fmt L"\n", ##__VA_ARGS__); \
            DbgCaptureBuffer() += _dbgBuf; \
        } \
        fflush(stdout); \
    } while(0)
#define DBG_OPEN() DebugConsoleOpen()
#define DBG_CLOSE() DebugConsoleClose()

#define WM_APP_CONSOLE_CMD (WM_APP + 10)
#define WM_APP_CONSOLE_PIPE_CMD (WM_APP + 11)

class ModuleManager;
class HostApp;

HANDLE   StartConsoleReader();
void     StopConsoleReader(HANDLE hThread);
void     RunConsoleCommand(const wchar_t* cmd, ModuleManager* modMgr, HWND hMainWnd, HINSTANCE hInst);

//--- AI-drivable channel (DEBUG_CONSOLE only) ---------------------------------
// A named-pipe server that accepts one command line per connection, runs it on
// the main thread through the same RunConsoleCommand path, and returns the
// captured output. This is what lets an automated agent drive the app without
// a human typing into the AllocConsole window.
//
// The host pointer lets tray/service commands reach past the main window.
void DebugConsoleSetHost(HostApp* host);
void StartPipeServer();
void StopPipeServer();

// Called by the main thread when WM_APP_CONSOLE_PIPE_CMD arrives.
void DebugConsoleServePipeRequest(void* requestPtr);

// Response capture used by the pipe server; DBG() appends here while a pipe
// command is being served so the caller sees the same text the console prints.
void DebugConsoleBeginCapture();
std::wstring DebugConsoleEndCapture();

#else  // DEBUG_CONSOLE == 0
#define DBG(fmt, ...) ((void)0)
#define DBG_OPEN() ((void)0)
#define DBG_CLOSE() ((void)0)

#define WM_APP_CONSOLE_CMD (WM_APP + 10)
#define WM_APP_CONSOLE_PIPE_CMD (WM_APP + 11)

class ModuleManager;
class HostApp;

inline HANDLE StartConsoleReader() { return nullptr; }
inline void   StopConsoleReader(HANDLE) {}
inline void   RunConsoleCommand(const wchar_t*, ModuleManager*, HWND, HINSTANCE) {}

inline void DebugConsoleSetHost(HostApp*) {}
inline void StartPipeServer() {}
inline void StopPipeServer() {}
inline void DebugConsoleServePipeRequest(void*) {}

#include <string>
inline void DebugConsoleBeginCapture() {}
inline std::wstring DebugConsoleEndCapture() { return L""; }

#endif
