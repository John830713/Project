//==============================================================================
// main.cpp - Application entry point
//==============================================================================

#include "HostApp.h"
#include "UI/ModuleLauncherWindow.h"
#include "Core/ConfigManager.h"
#include "Core/Logger.h"
#include "Core/LoaderConfig.h"
#include "Core/DebugConsole.h"
#include "Core/UpdateManager.h"
#include "Services/TranslationService.h"
#include "Pet/PetConfig.h"

#include <exception>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cwchar>
#include <memory>
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

//------------------------------------------------------------------------------
// Command line helpers
//
// wWinMain receives the tail of the command line, not an argv vector, and the
// only flags we support are simple "--name value" pairs, so a tiny parser keeps
// this dependency-free. BuildUI.py launches flavours with
//   Project.exe --flavor <name> [--carrier pet|tray|window]
//------------------------------------------------------------------------------

std::wstring GetArgValue(const wchar_t* cmdLine, const wchar_t* flag) {
    if (!cmdLine || !flag) {
        return {};
    }
    const size_t flagLen = wcslen(flag);
    const wchar_t* p = cmdLine;
    while ((p = wcsstr(p, flag)) != nullptr) {
        // Only accept the flag when it starts a token, so "foo--carrier"
        // inside another value cannot match.
        const bool atTokenStart = (p == cmdLine) || (p[-1] == L' ' || p[-1] == L'\t');
        const wchar_t* after = p + flagLen;
        if (atTokenStart && (*after == L' ' || *after == L'\t' || *after == L'\0')) {
            while (*after == L' ' || *after == L'\t') {
                ++after;
            }
            if (*after == L'"') {
                ++after;
                const wchar_t* end = wcschr(after, L'"');
                return end ? std::wstring(after, end) : std::wstring(after);
            }
            const wchar_t* end = after;
            while (*end && *end != L' ' && *end != L'\t') {
                ++end;
            }
            return std::wstring(after, end);
        }
        p += flagLen;
    }
    return {};
}

} // namespace

//==============================================================================
// wWinMain - Windows Unicode entry point
//------------------------------------------------------------------------------
// 1. Install crash handlers
// 2. Initialize core services to read loader config
// 3. Load module selection from Config_Loader.json (or a --flavor file)
// 4. Route to the selected carrier: pet window, tray only, or module launcher
//==============================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    DBG_OPEN();
    DBG(L"Application started");

    SetUnhandledExceptionFilter(CrashFilter);
    std::set_terminate(TerminateHandler);

    // A relaunch spawned by an update passes --reload-wait so BeginSession
    // waits for the old instance to release the single-instance mutex.
    if (pCmdLine && wcsstr(pCmdLine, L"--reload-wait")) {
        UpdateManager::SetReloadWaitRequested(true);
    }

    // BuildUI.py flavours: --flavor selects Config_Flavor_<name>.json and
    // --carrier overrides the carrier stored in it (handy when testing the
    // same module selection in a different shell).
    const std::wstring flavor = GetArgValue(pCmdLine, L"--flavor");
    const std::wstring carrier = GetArgValue(pCmdLine, L"--carrier");

    ConfigManager::Initialize(hInstance);
    Logger::Initialize(hInstance);

    // Update/single-instance notices must speak the UI language; MainWindow
    // only loads it later during its own initialization.
    TranslationService::Get()->Load(PetConfig::Load().language);

    // Single instance + in-place update. Returns false when another copy is
    // already running: a newer build has been swapped into the registered
    // path (running instance untouched) or a notice was shown. Either way a
    // second copy must not start.
    if (!UpdateManager::BeginSession([](const std::wstring& key) {
            return TranslationService::Get()->Tr(L"Update", key);
        }, UpdateManager::IsReloadWaitRequested())) {
        return 0;
    }

    LoaderConfig::Data cfg = LoaderConfig::Load(flavor);
    if (!carrier.empty()) {
        cfg.mode = LoaderConfig::ModeFromKey(carrier);
    }

    const size_t moduleCount = cfg.enabledModules.empty()
        ? LoaderConfig::ScanAvailableModules().size()
        : cfg.enabledModules.size();
    DBG(L"Starting in %s mode with %zu module(s) selected", LoaderConfig::ModeToKey(cfg.mode),
        moduleCount);
    Logger::Write(L"Main", std::wstring(L"Starting in ") + LoaderConfig::ModeToKey(cfg.mode) +
                               L" mode with " + std::to_wstring(moduleCount) + L" module(s) selected.");

    try {
        HostApp app;
        // Tray and window flavours still create the pet window (the tray icon,
        // module lifecycle and hotkeys hang off it) but never show it, so the
        // user's saved pet visibility is left alone.
        const bool startHidden = cfg.mode != LoaderConfig::Mode::Pet;
        if (!app.Initialize(hInstance, nCmdShow, cfg, startHidden)) {
            return 1;
        }

        std::unique_ptr<ModuleLauncherWindow> launcher;
        if (cfg.mode == LoaderConfig::Mode::Window) {
            launcher = std::make_unique<ModuleLauncherWindow>(hInstance, &app.GetModuleManager());
            if (launcher->Create()) {
                launcher->Show(SW_SHOW);
            } else {
                Logger::Write(L"Error", L"Failed to create the module launcher window.");
                launcher.reset();
            }
        }

        const int exitCode = app.Run();
        launcher.reset();
        return exitCode;
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
