#pragma once

// Define DEBUG_CONSOLE to enable console output for interactive debugging.
// When done, undefine or set to 0 to restore normal GUI-only mode.
//   #define DEBUG_CONSOLE 1
// or add -DDEBUG_CONSOLE to CXXFLAGS in Makefile.

#ifndef DEBUG_CONSOLE
#define DEBUG_CONSOLE 0
#endif

#if DEBUG_CONSOLE
    #include <cstdio>
    #include <windows.h>

    inline void DebugConsoleOpen() {
        if (AllocConsole()) {
            FILE* dummy;
            freopen_s(&dummy, "CONOUT$", "w", stdout);
            freopen_s(&dummy, "CONOUT$", "w", stderr);
            SetConsoleTitleW(L"Debug Console");
            wprintf(L"[DebugConsole] Console opened.\n");
        }
    }

    inline void DebugConsoleClose() {
        wprintf(L"[DebugConsole] Console closing.\n");
        FreeConsole();
    }

    #define DBG(fmt, ...) \
        do { wprintf(L"[DBG] " fmt L"\n", ##__VA_ARGS__); fflush(stdout); } while(0)
    #define DBG_OPEN() DebugConsoleOpen()
    #define DBG_CLOSE() DebugConsoleClose()
#else
    #define DBG(fmt, ...) ((void)0)
    #define DBG_OPEN() ((void)0)
    #define DBG_CLOSE() ((void)0)
#endif
