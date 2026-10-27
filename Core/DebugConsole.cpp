#include "DebugConsole.h"

#if DEBUG_CONSOLE
#include "ModuleManager.h"
#include "IFeatureModule.h"
#include "../HostApp.h"
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <cwchar>

std::wstring& DbgCaptureBuffer() {
    static std::wstring buf;
    return buf;
}

bool& DbgCaptureActive() {
    static bool active = false;
    return active;
}

void DebugConsoleBeginCapture() {
    DbgCaptureBuffer().clear();
    DbgCaptureActive() = true;
}

std::wstring DebugConsoleEndCapture() {
    DbgCaptureActive() = false;
    return DbgCaptureBuffer();
}

void DebugConsoleCaptureAppend(const wchar_t* text) {
    if (!text || !*text || !DbgCaptureActive()) return;
    DbgCaptureBuffer() += text;
}

static DWORD s_mainThreadId = 0;
static HostApp* s_appHost = nullptr;

// Set by RunConsoleCommand("quit") while a pipe command is being served; the
// pipe thread posts WM_QUIT only after the reply has been flushed, otherwise
// the app could exit before the caller reads its answer.
static bool s_servingPipeRequest = false;
static bool s_quitAfterServe = false;

void DebugConsoleSetHost(HostApp* host) {
    s_appHost = host;
}

static void SetMainThreadId(DWORD id) {
    s_mainThreadId = id;
}

static DWORD WINAPI ConsoleReaderThread(LPVOID) {
    wchar_t buffer[4096];
    while (fgetws(buffer, 4096, stdin)) {
        size_t len = wcslen(buffer);
        while (len > 0 && (buffer[len - 1] == L'\n' || buffer[len - 1] == L'\r'))
            buffer[--len] = L'\0';
        if (len == 0) continue;

        if (wcscmp(buffer, L"quit") == 0 || wcscmp(buffer, L"exit") == 0) {
            PostThreadMessageW(s_mainThreadId, WM_QUIT, 0, 0);
            break;
        }

        wchar_t* copy = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
        wcscpy_s(copy, len + 1, buffer);
        PostThreadMessageW(s_mainThreadId, WM_APP_CONSOLE_CMD, 0, (LPARAM)copy);
    }
    return 0;
}

HANDLE StartConsoleReader() {
    SetMainThreadId(GetCurrentThreadId());
    HANDLE hThread = CreateThread(nullptr, 0, ConsoleReaderThread, nullptr, 0, nullptr);
    if (hThread) {
        DBG(L"Console reader started. Type 'help' for commands.");
    }
    return hThread;
}

void StopConsoleReader(HANDLE hThread) {
    if (hThread && hThread != INVALID_HANDLE_VALUE) {
        if (WaitForSingleObject(hThread, 500) == WAIT_TIMEOUT) {
            TerminateThread(hThread, 0);
        }
        CloseHandle(hThread);
    }
}

static IFeatureModule* FindModuleByName(ModuleManager* modMgr, const std::wstring& name) {
    for (auto* mod : modMgr->GetAllModules()) {
        if (_wcsicmp(mod->GetModuleName(), name.c_str()) == 0)
            return mod;
    }
    return nullptr;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

// Log files are written as UTF-8 (Logger::AppendLine) and end with "\n", so read
// bytes and convert. std::wifstream would widen each byte and garble CJK text.
static std::vector<std::wstring> ReadLogLastLines(const std::wstring& filePath, int n) {
    std::vector<std::wstring> lines;
    std::ifstream file(std::filesystem::path(filePath), std::ios::binary);
    if (!file.is_open()) return lines;
    std::string line;
    bool firstLine = true;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (firstLine) {
            firstLine = false;
            // Tolerate a BOM in case a tool rewrote the file.
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }
        }
        lines.push_back(Utf8ToWide(line));
    }
    if (static_cast<int>(lines.size()) > n)
        lines.erase(lines.begin(), lines.begin() + (lines.size() - n));
    return lines;
}

// Log lines are printed without the "[DBG] " prefix; mirror them into the capture
// buffer so pipe callers see the same text as a human at the console.
static void PrintLogLine(const std::wstring& line) {
    wprintf(L"%s\n", line.c_str());
    std::wstring withNewline = line + L'\n';
    DebugConsoleCaptureAppend(withNewline.c_str());
}

// Module logs live in Log/Log_<name>.txt; core logs in Log/<category>.txt.
static std::wstring ResolveLogPath(const std::wstring& category) {
    std::error_code ec;
    std::wstring modulePath = L"Log/Log_" + category + L".txt";
    if (std::filesystem::exists(std::filesystem::path(modulePath), ec)) return modulePath;
    return L"Log/" + category + L".txt";
}

void RunConsoleCommand(const wchar_t* cmd, ModuleManager* modMgr, HWND hMainWnd, HINSTANCE) {
    std::wstring fullCmd(cmd);

    size_t spacePos = fullCmd.find(L' ');
    std::wstring command = (spacePos == std::wstring::npos) ? fullCmd : fullCmd.substr(0, spacePos);
    std::wstring args = (spacePos == std::wstring::npos) ? L"" : fullCmd.substr(spacePos + 1);

    auto trim = [](std::wstring& s) {
        size_t start = s.find_first_not_of(L' ');
        size_t end = s.find_last_not_of(L' ');
        if (start == std::wstring::npos) { s.clear(); return; }
        s = s.substr(start, end - start + 1);
    };
    trim(args);

    if (command == L"help") {
        DBG(L"=== Debug Console Commands ===");
        DBG(L"  help                        - Show this help");
        DBG(L"  modules (lsmod)             - List registered modules");
        DBG(L"  mod <name>                  - Show module info");
        DBG(L"  mod <name> cmd <id>         - Execute module context menu command");
        DBG(L"  mod <name> config           - Show module config values");
        DBG(L"  mod <name> log [n]          - Show last n lines of module log (default 20)");
        DBG(L"  log <category> [n]          - Show last n lines of category log (default 20)");
        DBG(L"  tray <status|show|hide|reset>- Tray icon / pet-window control");
        DBG(L"  clear                       - Clear console");
        DBG(L"  status                      - Show application state");
        DBG(L"  quit / exit                 - Graceful shutdown");

    } else if (command == L"modules" || command == L"lsmod") {
        if (!modMgr) { DBG(L"ModuleManager not available."); return; }
        const auto& modules = modMgr->GetAllModules();
        DBG(L"Modules registered: %zu", modules.size());
        for (size_t i = 0; i < modules.size(); ++i) {
            auto* mod = modules[i];
            DBG(L"  [%zu] %s - %s%s", i, mod->GetModuleName(), mod->GetDisplayName(),
                mod->IsAvailable() ? L"" : L" [unavailable: hidden from menus/settings]");
        }

    } else if (command == L"mod") {
        if (!modMgr) { DBG(L"ModuleManager not available."); return; }
        if (args.empty()) { DBG(L"Usage: mod <name> [cmd|config|log]"); return; }

        size_t as = args.find(L' ');
        std::wstring modName = (as == std::wstring::npos) ? args : args.substr(0, as);
        std::wstring sub = (as == std::wstring::npos) ? L"" : args.substr(as + 1);
        trim(sub);

        auto* mod = FindModuleByName(modMgr, modName);
        if (!mod) { DBG(L"Module '%s' not found.", modName.c_str()); return; }

        if (sub.empty()) {
            DBG(L"Module: %s", mod->GetModuleName());
            DBG(L"  Display: %s", mod->GetDisplayName());
            DBG(L"  Version: %s", mod->GetVersion());
            const wchar_t* purpose = mod->GetPurpose();
            if (purpose && *purpose) DBG(L"  Purpose: %s", purpose);
            DBG(L"  Config: %s", mod->GetConfigFileName());
            DBG(L"  Log source: %s", mod->GetLogSourceName());

            auto items = mod->GetContextMenuItems();
            if (!items.empty()) {
                DBG(L"  Context menu items:");
                for (const auto& item : items) {
                    DBG(L"    [%d] %s", item.itemId, item.label.c_str());
                }
            }

        } else if (sub == L"config") {
            DBG(L"--- Config for %s ---", mod->GetModuleName());
            const auto& defs = mod->GetConfigDefinitions();
            for (const auto& def : defs) {
                std::wstring val = mod->GetValue(def.key);
                DBG(L"  %s = %s", def.key.c_str(), val.c_str());
            }

        } else {
            // sub = "log [n]" or "cmd <id>"
            size_t ss = sub.find(L' ');
            std::wstring action = (ss == std::wstring::npos) ? sub : sub.substr(0, ss);
            std::wstring param = (ss == std::wstring::npos) ? L"" : sub.substr(ss + 1);
            trim(param);

            if (action == L"log") {
                int n = param.empty() ? 20 : _wtoi(param.c_str());
                if (n <= 0) n = 20;
                std::wstring logPath = L"Log/Log_" + std::wstring(mod->GetLogSourceName()) + L".txt";
                auto lines = ReadLogLastLines(logPath, n);
                if (lines.empty()) {
                    DBG(L"No log entries for module '%s'.", mod->GetModuleName());
                } else {
                    DBG(L"--- Last %zu lines of %s ---", lines.size(), logPath.c_str());
                    for (const auto& line : lines) {
                        PrintLogLine(line);
                    }
                    fflush(stdout);
                }
            } else if (action == L"cmd") {
                int id = _wtoi(param.c_str());
                DBG(L"Executing context menu item %d on %s...", id, mod->GetModuleName());
                mod->ExecuteContextMenuItem(id);
            } else {
                DBG(L"Unknown sub-command '%s'. Use: cmd, config, or log.", action.c_str());
            }
        }

    } else if (command == L"log") {
        size_t ls = args.find(L' ');
        std::wstring category = (ls == std::wstring::npos) ? args : args.substr(0, ls);
        std::wstring nstr = (ls == std::wstring::npos) ? L"" : args.substr(ls + 1);
        trim(nstr);
        if (category.empty()) { DBG(L"Usage: log <category> [n]"); return; }

        int n = nstr.empty() ? 20 : _wtoi(nstr.c_str());
        if (n <= 0) n = 20;

        std::wstring logPath = ResolveLogPath(category);
        auto lines = ReadLogLastLines(logPath, n);
        if (lines.empty()) {
            DBG(L"No log entries for '%s'.", category.c_str());
        } else {
            DBG(L"--- Last %zu lines of %s ---", lines.size(), logPath.c_str());
            for (const auto& line : lines) {
                PrintLogLine(line);
            }
            fflush(stdout);
        }

    } else if (command == L"tray") {
        if (!s_appHost) { DBG(L"Host not attached; tray commands unavailable."); return; }
        if (args == L"status" || args.empty()) {
            DBG(L"=== Tray Status ===");
            DBG(L"  Icon added      : %s", s_appHost->IsTrayIconAdded() ? L"Yes" : L"No");
            DBG(L"  Main window     : %s",
                s_appHost->IsMainWindowAlive() ? L"alive" : L"MISSING");
            DBG(L"  Main window vis : %s", s_appHost->IsMainWindowVisible() ? L"visible" : L"hidden");
            DBG(L"  Watchdog ticks  : %lu", s_appHost->GetTrayWatchdogTicks());
        } else if (args == L"show") {
            s_appHost->SetMainWindowVisible(true);
            DBG(L"  Tray: pet window shown.");
        } else if (args == L"hide") {
            s_appHost->SetMainWindowVisible(false);
            DBG(L"  Tray: pet window hidden.");
        } else if (args == L"reset") {
            s_appHost->RecreateMainWindow();
            DBG(L"  Tray: pet window recreated (alive=%s).",
                s_appHost->IsMainWindowAlive() ? L"Yes" : L"No");
        } else if (args == L"killwindow") {
            s_appHost->DebugDestroyMainWindow();
            DBG(L"  Tray: pet window destroyed (alive=%s); watchdog should rebuild it.",
                s_appHost->IsMainWindowAlive() ? L"Yes" : L"No");
        } else {
            DBG(L"Usage: tray <status|show|hide|reset>");
        }

    } else if (command == L"clear") {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hConsole, &csbi);
        COORD topLeft = {0, 0};
        DWORD written;
        FillConsoleOutputCharacterW(hConsole, L' ',
            csbi.dwSize.X * csbi.dwSize.Y, topLeft, &written);
        FillConsoleOutputAttribute(hConsole, csbi.wAttributes,
            csbi.dwSize.X * csbi.dwSize.Y, topLeft, &written);
        SetConsoleCursorPosition(hConsole, topLeft);

    } else if (command == L"quit" || command == L"exit") {
        // Over the pipe the app must stay alive long enough to answer; the
        // pipe thread posts WM_QUIT once the response is flushed.
        DBG(L"Shutting down...");
        if (s_servingPipeRequest) {
            s_quitAfterServe = true;
        } else {
            PostQuitMessage(0);
        }

    } else if (command == L"status") {
        DBG(L"=== Application Status ===");
        if (hMainWnd && IsWindow(hMainWnd)) {
            RECT rc;
            GetWindowRect(hMainWnd, &rc);
            DBG(L"  Main window: (%d,%d) %dx%d",
                rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
            LONG style = GetWindowLongW(hMainWnd, GWL_EXSTYLE);
            DBG(L"  Topmost: %s", (style & WS_EX_TOPMOST) ? L"Yes" : L"No");
        }
        if (modMgr) {
            DBG(L"  Modules loaded: %zu", modMgr->GetAllModules().size());
        }

    } else {
        DBG(L"Unknown command: '%s'. Type 'help'.", command.c_str());
    }
}

//==============================================================================
// AI-drivable named-pipe command server
//
// Protocol: client connects to \\.\pipe\ProjectDebug, writes one UTF-16LE
// command line (terminated by '\n' or ';'), the server runs it on the main
// thread and writes back the captured output, then closes the pipe. One
// command per connection keeps it trivial to drive from PowerShell/Python.
//
// The pipe thread only reads/writes bytes and hands the command to the main
// thread; the command itself runs on the main thread (RunConsoleCommand is not
// thread-safe, and tray commands touch windows owned by the main thread).
//==============================================================================

namespace {

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\ProjectDebug";

struct PipeRequest {
    std::wstring command;
    std::wstring response;
    bool quitAfterResponse;
    HANDLE doneEvent;
};

// Bounded wait for the main thread to finish serving a pipe request. If the
// main thread is wedged we still answer the client rather than hang forever.
constexpr DWORD kPipeServeTimeoutMs = 15000;

}  // namespace

static std::wstring RunConsoleCommandCaptured(const std::wstring& cmd,
                                             ModuleManager* modMgr,
                                             HWND hMainWnd,
                                             HINSTANCE hInst) {
    DebugConsoleBeginCapture();
    s_quitAfterServe = false;
    // RunConsoleCommand takes a mutable buffer; pass a copy.
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    RunConsoleCommand(buf.data(), modMgr, hMainWnd, hInst);
    return DebugConsoleEndCapture();
}

// Main-thread handler for a pipe command. Runs the command (with capture) and
// signals the waiting pipe thread. Returns the response via req->response.
void DebugConsoleServePipeRequest(void* requestPtr, ModuleManager* modMgr,
                                  HWND hMainWnd, HINSTANCE hInst) {
    auto* req = static_cast<PipeRequest*>(requestPtr);
    if (!req) return;
    s_servingPipeRequest = true;
    req->response = RunConsoleCommandCaptured(req->command, modMgr, hMainWnd, hInst);
    s_servingPipeRequest = false;
    req->quitAfterResponse = s_quitAfterServe;
    s_quitAfterServe = false;
    SetEvent(req->doneEvent);
}

static HANDLE CreatePipeInstance() {
    return CreateNamedPipeW(
        kPipeName,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        8192, 8192, 0, nullptr);
}

static DWORD WINAPI PipeServerThread(LPVOID) {
    // Canonical multi-instance pattern: always keep a fresh instance listening
    // so a new client can connect the instant the previous one is served
    // (otherwise back-to-back clients race the gap while we recreate).
    HANDLE hListen = CreatePipeInstance();
    if (hListen == INVALID_HANDLE_VALUE) {
        return 0;
    }

    while (true) {
        BOOL connected = ConnectNamedPipe(hListen, nullptr)
                         ? TRUE
                         : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected) {
            CloseHandle(hListen);
            hListen = CreatePipeInstance();
            if (hListen == INVALID_HANDLE_VALUE) return 0;
            continue;
        }

        // The next instance is already listening while we serve this client.
        HANDLE hPipe = hListen;
        hListen = CreatePipeInstance();
        if (hListen == INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            return 0;
        }

        // Read a UTF-16 command (up to 8 KiB). Peek first so a client that
        // connects then aborts (or sends nothing) cannot wedge this thread in
        // a blocking ReadFile forever.
        wchar_t raw[4099] = {};
        DWORD bytesRead = 0;
        BOOL ok = FALSE;
        for (int waited = 0; waited < 5000; waited += 50) {  // 5s budget
            DWORD avail = 0;
            if (!PeekNamedPipe(hPipe, nullptr, 0, nullptr, &avail, nullptr)) {
                break;  // broken pipe / client gone
            }
            if (avail > 0) {
                ok = ReadFile(hPipe, raw, sizeof(raw) - sizeof(wchar_t),
                              &bytesRead, nullptr);
                break;
            }
            Sleep(50);
        }
        std::wstring cmd;
        if (ok && bytesRead > 0) {
            cmd.assign(raw, bytesRead / sizeof(wchar_t));
            while (!cmd.empty() &&
                   (cmd.back() == L'\n' || cmd.back() == L'\r' ||
                    cmd.back() == L';'  || cmd.back() == L'\0')) {
                cmd.pop_back();
            }
        }

        std::wstring response = L"[pipe] empty command\n";
        bool quitNow = false;
        if (!cmd.empty()) {
            PipeRequest req;
            req.command = cmd;
            req.quitAfterResponse = false;
            req.doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

            if (!PostThreadMessageW(s_mainThreadId, WM_APP_CONSOLE_PIPE_CMD, 0,
                                    reinterpret_cast<LPARAM>(&req))) {
                response = L"[pipe] main thread not reachable\n";
            } else {
                DWORD wr = WaitForSingleObject(req.doneEvent, kPipeServeTimeoutMs);
                response = (wr == WAIT_TIMEOUT)
                           ? L"[pipe] command timed out\n"
                           : req.response;
                quitNow = (wr != WAIT_TIMEOUT) && req.quitAfterResponse;
            }
            CloseHandle(req.doneEvent);
        }

        DWORD written = 0;
        WriteFile(hPipe, response.c_str(),
                  static_cast<DWORD>(response.size() * sizeof(wchar_t)),
                  &written, nullptr);
        FlushFileBuffers(hPipe);
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);

        if (quitNow) {
            // Reply is on the wire; now tear the app down.
            PostThreadMessageW(s_mainThreadId, WM_QUIT, 0, 0);
            return 0;
        }
    }
    return 0;
}

static HANDLE s_pipeThread = nullptr;

void StartPipeServer() {
    if (s_pipeThread) return;
    s_pipeThread = CreateThread(nullptr, 0, PipeServerThread, nullptr, 0, nullptr);
    if (s_pipeThread) {
        DBG(L"Pipe command server started at %s", kPipeName);
    }
}

void StopPipeServer() {
    if (s_pipeThread) {
        // The thread blocks in ConnectNamedPipe; terminating it is acceptable
        // during shutdown because it holds no locks the exit path needs.
        TerminateThread(s_pipeThread, 0);
        CloseHandle(s_pipeThread);
        s_pipeThread = nullptr;
    }
}

#endif
