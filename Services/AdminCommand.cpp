#include "AdminCommand.h"

#include "../Core/CommandLine.h"
#include "../Core/Logger.h"
#include "ProcessRunner.h"
#include "RemoteInitService.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                    static_cast<int>(text.size()),
                                    nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                        static_cast<int>(text.size()), out.data(), bytes,
                        nullptr, nullptr);
    return out;
}

std::wstring FromUtf8(const std::string& text) {
    if (text.empty()) return {};
    int chars = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                    static_cast<int>(text.size()),
                                    nullptr, 0);
    if (chars <= 0) return {};
    std::wstring out(static_cast<size_t>(chars), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), chars);
    return out;
}

std::wstring CurrentExePath() {
    wchar_t path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return path;
}

std::vector<std::wstring> SplitIds(const std::wstring& csv) {
    std::vector<std::wstring> out;
    size_t start = 0;
    while (start <= csv.size()) {
        size_t comma = csv.find(L',', start);
        std::wstring id = csv.substr(start, comma == std::wstring::npos
                                                ? std::wstring::npos
                                                : comma - start);
        if (!id.empty()) out.push_back(id);
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    return out;
}

std::wstring JoinIds(const std::vector<std::wstring>& ids) {
    std::wstring out;
    for (const auto& id : ids) {
        if (!out.empty()) out += L",";
        out += id;
    }
    return out;
}

}  // namespace

bool AdminCommand::IsInvocation(const wchar_t* cmdLine) {
    if (!cmdLine) return false;
    const wchar_t* p = cmdLine;
    while (*p == L' ' || *p == L'\t') ++p;
    const wchar_t kFlag[] = L"--admin";
    const size_t len = sizeof(kFlag) / sizeof(kFlag[0]) - 1;
    if (wcsncmp(p, kFlag, len) != 0) return false;
    const wchar_t after = p[len];
    return after == L'\0' || after == L' ' || after == L'\t';
}

int AdminCommand::Run(HINSTANCE hInstance, const wchar_t* cmdLine) {
    const std::wstring op = CommandLine::GetValue(cmdLine, L"--admin");
    const std::wstring resultPath = CommandLine::GetValue(cmdLine, L"--result");
    if (resultPath.empty()) return 2;

    Logger::Initialize(hInstance);

    std::vector<std::wstring> lines;
    bool ok = false;

    if (!ProcessRunner::IsElevated()) {
        lines.push_back(L"[FAIL] 管理員指令未取得提升權限");
    } else if (op == L"remote-init-apply") {
        std::vector<RemoteInitItem> items;
        for (const auto& id : SplitIds(CommandLine::GetValue(cmdLine, L"--ids"))) {
            RemoteInitItem item;
            item.id = id;
            item.checked = true;
            item.selectable = true;
            items.push_back(item);
        }
        RemoteInitResult res = RemoteInitService::Get()->ApplySelected(
            items, CommandLine::GetValue(cmdLine, L"--user"));
        ok = res.allOk;
        lines = res.logs;
    } else if (op == L"remote-init-restore") {
        const std::wstring backup = CommandLine::GetValue(cmdLine, L"--backup");
        ok = RemoteInitService::Get()->RestoreBackup(backup);
        lines.push_back(ok ? L"[OK] 還原完成" : L"[FAIL] 還原失敗");
    } else {
        lines.push_back(L"[FAIL] 未知的管理員指令: " + op);
    }

    WriteResultFile(resultPath, ok, lines);
    return ok ? 0 : 1;
}

AdminCommand::Result AdminCommand::RunElevated(const std::wstring& args,
                                               DWORD timeoutMs) {
    Result result;

    const std::wstring exe = CurrentExePath();
    if (exe.empty()) {
        result.lines.push_back(L"[FAIL] 無法取得程式路徑");
        return result;
    }

    const std::wstring resultPath = MakeTempResultPath();
    if (resultPath.empty()) {
        result.lines.push_back(L"[FAIL] 無法建立暫存結果檔");
        return result;
    }

    const std::wstring params =
        args + L" --result " + QuoteArgument(resultPath);

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = exe.c_str();
    info.lpParameters = params.c_str();
    info.nShow = SW_HIDE;

    if (!ShellExecuteExW(&info)) {
        const DWORD err = GetLastError();
        if (err == ERROR_CANCELLED) {
            result.lines.push_back(L"[FAIL] 已取消管理員授權");
        } else {
            result.lines.push_back(L"[FAIL] 無法啟動管理員指令（錯誤 " +
                                   std::to_wstring(err) + L"）");
        }
        DeleteFileW(resultPath.c_str());
        return result;
    }

    result.started = true;

    if (info.hProcess) {
        const DWORD wait = WaitForSingleObject(info.hProcess, timeoutMs);
        DWORD exitCode = 1;
        GetExitCodeProcess(info.hProcess, &exitCode);
        CloseHandle(info.hProcess);
        if (wait != WAIT_OBJECT_0) {
            result.lines.push_back(L"[FAIL] 管理員指令逾時未完成");
        }
    }

    const std::wstring text = ReadTextFile(resultPath);
    DeleteFileW(resultPath.c_str());

    if (!text.empty()) {
        result.allOk = ParseResultText(text, result.lines);
    } else if (result.lines.empty()) {
        result.lines.push_back(L"[FAIL] 管理員指令沒有回傳結果");
    }
    return result;
}

AdminCommand::Result AdminCommand::ApplyRemoteInit(
    const std::vector<std::wstring>& ids, const std::wstring& userName) {
    return RunElevated(BuildApplyArgs(ids, userName));
}

AdminCommand::Result AdminCommand::RestoreRemoteInit(
    const std::wstring& backupPath) {
    return RunElevated(BuildRestoreArgs(backupPath));
}

std::wstring AdminCommand::QuoteArgument(const std::wstring& arg) {
    if (arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;

    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
            continue;
        }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(c);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::wstring AdminCommand::BuildApplyArgs(const std::vector<std::wstring>& ids,
                                          const std::wstring& userName) {
    std::wstring args = L"--admin remote-init-apply --ids ";
    args += QuoteArgument(JoinIds(ids));
    if (!userName.empty()) {
        args += L" --user " + QuoteArgument(userName);
    }
    return args;
}

std::wstring AdminCommand::BuildRestoreArgs(const std::wstring& backupPath) {
    return L"--admin remote-init-restore --backup " + QuoteArgument(backupPath);
}

bool AdminCommand::ParseResultText(const std::wstring& text,
                                   std::vector<std::wstring>& lines) {
    bool ok = false;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        std::wstring line = text.substr(
            start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        if (line.rfind(L"RESULT=", 0) == 0) {
            ok = (line == L"RESULT=OK");
        } else if (!line.empty()) {
            lines.push_back(line);
        }

        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return ok;
}

void AdminCommand::WriteResultFile(const std::wstring& path, bool allOk,
                                   const std::vector<std::wstring>& lines) {
    std::wstring text = allOk ? L"RESULT=OK\n" : L"RESULT=FAIL\n";
    for (const auto& line : lines) {
        text += line;
        text += L"\n";
    }

    const std::string utf8 = ToUtf8(text);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(h);
}

std::wstring AdminCommand::ReadTextFile(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 ||
        size.QuadPart > 1024 * 1024) {
        CloseHandle(h);
        return {};
    }

    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()),
                             &read, nullptr);
    CloseHandle(h);
    if (!ok) return {};
    bytes.resize(read);
    return FromUtf8(bytes);
}

std::wstring AdminCommand::MakeTempResultPath() {
    wchar_t dir[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, dir) == 0) return {};

    wchar_t file[MAX_PATH] = {};
    if (GetTempFileNameW(dir, L"prj", 0, file) == 0) return {};
    return file;
}
