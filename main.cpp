//==============================================================================
// main.cpp - Application entry point
//==============================================================================

#include "HostApp.h"
#include "Core/ConfigManager.h"
#include "Core/Logger.h"
#include "Core/LoaderConfig.h"
#include "Core/DebugConsole.h"

#include <exception>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>

//==============================================================================
// Crash reporting
//
// The app is linked -mwindows: no console, no stderr. Without these handlers an
// access violation or an uncaught C++ exception ends the process with zero
// evidence, so a crash is indistinguishable from a normal exit. Everything here
// writes to Log/Crash.txt.
//==============================================================================

namespace {

//------------------------------------------------------------------------------
// AppendCrashLine - appends UTF-8 text to Log/Crash.txt.
//
// Uses raw Win32 I/O rather than std::ofstream or Logger: a crash handler runs
// with the C++ runtime in an unknown state, so it must not allocate or throw.
//------------------------------------------------------------------------------

void AppendCrashLine(const char* utf8Text) {
    if (!utf8Text) return;

    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return;

    std::wstring logPath(exePath);
    size_t slash = logPath.find_last_of(L'\\');
    if (slash == std::wstring::npos) return;
    logPath.resize(slash);
    logPath += L"\\Log\\Crash.txt";

    HANDLE h = CreateFileW(logPath.c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    WriteFile(h, utf8Text, (DWORD)strlen(utf8Text), &written, nullptr);
    CloseHandle(h);
}

//------------------------------------------------------------------------------
// AppendCrashReport - writes a timestamped section then the detail block.
//------------------------------------------------------------------------------

void AppendCrashReport(const char* kind, const std::string& body) {
    SYSTEMTIME st;
    GetLocalTime(&st);

    char header[160];
    snprintf(header, sizeof(header), "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, kind);

    AppendCrashLine(header);
    AppendCrashLine(body.c_str());
    AppendCrashLine("\n");
}

std::string HexAddress(const void* p) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(
                 reinterpret_cast<uintptr_t>(p)));
    return buf;
}

//------------------------------------------------------------------------------
// CrashFilter - catches SEH exceptions (access violations, stack overflow, ...)
//------------------------------------------------------------------------------

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
    std::string body;

    if (info && info->ExceptionRecord) {
        const EXCEPTION_RECORD* rec = info->ExceptionRecord;
        char detail[512];
        snprintf(detail, sizeof(detail),
                 "  exception code : 0x%08lX\n"
                 "  exception addr : %s\n"
                 "  info[0]        : %s\n"
                 "  thread id      : %lu\n",
                 rec->ExceptionCode,
                 HexAddress(rec->ExceptionAddress).c_str(),
                 HexAddress(reinterpret_cast<const void*>(
                     rec->ExceptionInformation[0])).c_str(),
                 GetCurrentThreadId());
        body = detail;
    } else {
        body = "  (no exception record)\n";
    }

    AppendCrashReport("SEH exception", body);
    return EXCEPTION_CONTINUE_SEARCH;
}

//------------------------------------------------------------------------------
// TerminateHandler - last-resort net for std::terminate
//------------------------------------------------------------------------------

void TerminateHandler() {
    AppendCrashReport("std::terminate",
                      "  unhandled C++ exception reached std::terminate\n");
    std::abort();
}

} // namespace

//==============================================================================
// wWinMain - Windows Unicode entry point
//------------------------------------------------------------------------------
// 1. Install crash handlers
// 2. Initialize core services to read loader config
// 3. Load module selection from Config/Config_Loader.json
// 4. Route to Pet mode (full app) or Clean mode (standalone runner, future)
//==============================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    DBG_OPEN();
    DBG(L"Application started");

    SetUnhandledExceptionFilter(CrashFilter);
    std::set_terminate(TerminateHandler);

    ConfigManager::Initialize(hInstance);
    Logger::Initialize(hInstance);
    LoaderConfig::Data cfg = LoaderConfig::Load();

    if (cfg.mode == LoaderConfig::Mode::Clean) {
        DBG(L"Clean mode selected — not yet implemented, falling through to Pet");
    }

    const size_t moduleCount = cfg.enabledModules.empty()
        ? LoaderConfig::ScanAvailableModules().size()
        : cfg.enabledModules.size();
    DBG(L"Starting in Pet mode with %zu module(s) selected", moduleCount);
    Logger::Write(L"Main", L"Starting in Pet mode with " + std::to_wstring(moduleCount) +
                           L" module(s) selected.");

    try {
        HostApp app;
        if (!app.Initialize(hInstance, nCmdShow, cfg)) {
            return 1;
        }

        return app.Run();
    } catch (const std::exception& e) {
        AppendCrashReport("C++ exception",
                          std::string("  type    : std::exception\n  what    : ") + e.what() + "\n");
        Logger::Write(L"Error", L"Unhandled std::exception reached wWinMain.");
        return 1;
    } catch (...) {
        AppendCrashReport("C++ exception", "  type    : unknown\n");
        Logger::Write(L"Error", L"Unhandled unknown exception reached wWinMain.");
        return 1;
    }
}
