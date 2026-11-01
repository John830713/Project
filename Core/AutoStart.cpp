//==============================================================================
// AutoStart.cpp - per-user Run entry management
//==============================================================================

#include "AutoStart.h"

#include <windows.h>

#include <cstring>
#include <vector>

namespace {

const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring ModulePath() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path,
                                   static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
    if (len == 0 || len >= sizeof(path) / sizeof(path[0])) return L"";
    return std::wstring(path, len);
}

// Splits a command line the way CreateProcess does (quotes group a token).
std::vector<std::wstring> SplitArgs(const wchar_t* cmdLine) {
    std::vector<std::wstring> args;
    const wchar_t* p = cmdLine;
    while (p && *p) {
        while (*p == L' ' || *p == L'\t') ++p;
        if (!*p) break;

        std::wstring arg;
        bool quoted = false;
        while (*p && (quoted || (*p != L' ' && *p != L'\t'))) {
            if (*p == L'"') {
                quoted = !quoted;
                ++p;
                continue;
            }
            arg += *p++;
        }
        args.push_back(arg);
    }
    return args;
}

std::wstring QuoteIfNeeded(const std::wstring& value) {
    if (value.find(L' ') == std::wstring::npos) return value;
    return L"\"" + value + L"\"";
}

// Keep only the switches that change how the app starts. --reload-wait is an
// update handshake, never something to boot with.
std::wstring PreservedArguments() {
    const std::vector<std::wstring> args = SplitArgs(GetCommandLineW());
    std::wstring out;
    for (size_t i = 1; i + 1 < args.size(); ++i) {
        if (args[i] != L"--flavor" && args[i] != L"--carrier") continue;
        if (!out.empty()) out += L" ";
        out += args[i] + L" " + QuoteIfNeeded(args[i + 1]);
        ++i;
    }
    return out;
}

// Executable path out of a Run value: quoted, or everything before the first
// space.
std::wstring CommandPath(const std::wstring& command) {
    if (command.empty()) return L"";
    if (command[0] == L'"') {
        const size_t end = command.find(L'"', 1);
        if (end == std::wstring::npos) return command.substr(1);
        return command.substr(1, end - 1);
    }
    const size_t space = command.find(L' ');
    return (space == std::wstring::npos) ? command : command.substr(0, space);
}

std::wstring ReadValue(HKEY key, const std::wstring& name) {
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &bytes) !=
        ERROR_SUCCESS) {
        return L"";
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) return L"";

    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(key, name.c_str(), nullptr, &type,
                         reinterpret_cast<BYTE*>(buffer.data()), &bytes) !=
        ERROR_SUCCESS) {
        return L"";
    }
    return std::wstring(buffer.data());
}

} // namespace

std::wstring AutoStart::ValueName() {
    const std::wstring path = ModulePath();
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) name.erase(dot);
    return name.empty() ? L"Project" : name;
}

std::wstring AutoStart::Command() {
    std::wstring command = L"\"" + ModulePath() + L"\"";
    const std::wstring args = PreservedArguments();
    if (!args.empty()) command += L" " + args;
    return command;
}

bool AutoStart::IsEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    const std::wstring stored = ReadValue(key, ValueName());
    RegCloseKey(key);

    if (stored.empty()) return false;
    const std::wstring self = ModulePath();
    if (self.empty()) return false;
    return _wcsicmp(CommandPath(stored).c_str(), self.c_str()) == 0;
}

bool AutoStart::SetEnabled(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const std::wstring name = ValueName();
    LONG rc = ERROR_SUCCESS;
    if (enabled) {
        const std::wstring command = Command();
        // Rewrite only when the entry is missing or stale (exe moved, switches
        // changed); the writers call this on every PetConfig::Save.
        if (ReadValue(key, name) != command) {
            rc = RegSetValueExW(
                key, name.c_str(), 0, REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }
    } else {
        rc = RegDeleteValueW(key, name.c_str());
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }

    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}
