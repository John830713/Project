#pragma once

#include <windows.h>

#ifndef DEBUG_CONSOLE
#define DEBUG_CONSOLE 0
#endif

#if DEBUG_CONSOLE
#include <cstdio>

inline FILE*& DbgFileRef() {
    static FILE* f = nullptr;
    return f;
}

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
        fflush(stdout); \
    } while(0)
#define DBG_OPEN() DebugConsoleOpen()
#define DBG_CLOSE() DebugConsoleClose()

#define WM_APP_CONSOLE_CMD (WM_APP + 10)

class ModuleManager;

HANDLE   StartConsoleReader();
void     StopConsoleReader(HANDLE hThread);
void     RunConsoleCommand(const wchar_t* cmd, ModuleManager* modMgr, HWND hMainWnd, HINSTANCE hInst);

#else
#define DBG(fmt, ...) ((void)0)
#define DBG_OPEN() ((void)0)
#define DBG_CLOSE() ((void)0)

#define WM_APP_CONSOLE_CMD (WM_APP + 10)

class ModuleManager;

inline HANDLE StartConsoleReader() { return nullptr; }
inline void   StopConsoleReader(HANDLE) {}
inline void   RunConsoleCommand(const wchar_t*, ModuleManager*, HWND, HINSTANCE) {}

#endif
