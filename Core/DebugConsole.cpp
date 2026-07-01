#include "DebugConsole.h"

#if DEBUG_CONSOLE
#include "ModuleManager.h"
#include "IFeatureModule.h"
#include <string>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <cwchar>

static DWORD s_mainThreadId = 0;

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

static std::vector<std::wstring> ReadLogLastLines(const std::wstring& filePath, int n) {
    std::vector<std::wstring> lines;
    std::wifstream file(filePath.c_str());
    if (!file.is_open()) return lines;
    std::wstring line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    if (static_cast<int>(lines.size()) > n)
        lines.erase(lines.begin(), lines.begin() + (lines.size() - n));
    return lines;
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
        DBG(L"  clear                       - Clear console");
        DBG(L"  status                      - Show application state");
        DBG(L"  quit / exit                 - Graceful shutdown");

    } else if (command == L"modules" || command == L"lsmod") {
        if (!modMgr) { DBG(L"ModuleManager not available."); return; }
        const auto& modules = modMgr->GetAllModules();
        DBG(L"Modules registered: %zu", modules.size());
        for (size_t i = 0; i < modules.size(); ++i) {
            auto* mod = modules[i];
            DBG(L"  [%zu] %s - %s", i, mod->GetModuleName(), mod->GetDisplayName());
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
                        wprintf(L"%s\n", line.c_str());
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

        std::wstring logPath = L"Log/" + category + L".txt";
        auto lines = ReadLogLastLines(logPath, n);
        if (lines.empty()) {
            DBG(L"No log entries for '%s'.", category.c_str());
        } else {
            DBG(L"--- Last %zu lines of %s ---", lines.size(), logPath.c_str());
            for (const auto& line : lines) {
                wprintf(L"%s\n", line.c_str());
            }
            fflush(stdout);
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

#endif
