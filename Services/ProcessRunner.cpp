#include "ProcessRunner.h"

#include <shellapi.h>

#include <string>
#include <vector>

namespace {

// A child console tool writes in the OEM code page when its stdout is a pipe,
// while PowerShell 7 / Python emit UTF-8. Try UTF-8 first and fall back to OEM.
std::wstring DecodeOutput(const std::string& bytes) {
    if (bytes.empty()) return L"";

    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                bytes.data(), static_cast<int>(bytes.size()),
                                nullptr, 0);
    if (n > 0) {
        std::wstring out(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()),
                            &out[0], n);
        return out;
    }

    n = MultiByteToWideChar(GetOEMCP(), 0, bytes.data(),
                            static_cast<int>(bytes.size()), nullptr, 0);
    if (n <= 0) return L"";

    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(GetOEMCP(), 0, bytes.data(), static_cast<int>(bytes.size()),
                        &out[0], n);
    return out;
}

std::wstring System32File(const wchar_t* file) {
    wchar_t dir[MAX_PATH] = {};
    if (GetSystemDirectoryW(dir, MAX_PATH) == 0) return file;
    return std::wstring(dir) + L"\\" + file;
}

void TrimTrailingNewlines(std::wstring& text) {
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r')) {
        text.pop_back();
    }
}

}  // namespace

bool ProcessRunner::IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;

    TOKEN_ELEVATION elevation = {};
    DWORD size = sizeof(elevation);
    BOOL ok = GetTokenInformation(token, TokenElevation, &elevation,
                                  sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

std::wstring ProcessRunner::GetProductName() {
    wchar_t buffer[256] = {};
    DWORD size = sizeof(buffer);
    LONG rc = RegGetValueW(HKEY_LOCAL_MACHINE,
                           L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                           L"ProductName", RRF_RT_REG_SZ, nullptr,
                           buffer, &size);
    if (rc != ERROR_SUCCESS) return L"";
    return buffer;
}

bool ProcessRunner::IsWindowsHome() {
    std::wstring product = GetProductName();
    return product.find(L"Home") != std::wstring::npos;
}

bool ProcessRunner::RestartElevated(const std::wstring& extraArgs) {
    wchar_t exe[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return false;

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = exe;
    info.lpParameters = extraArgs.empty() ? nullptr : extraArgs.c_str();
    info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&info)) return false;  // ERROR_CANCELLED on UAC "No"
    if (info.hProcess) CloseHandle(info.hProcess);
    return true;
}

ProcessResult ProcessRunner::RunCmd(const std::wstring& args, DWORD timeoutMs) {
    std::wstring shell = System32File(L"cmd.exe");
    std::wstring commandLine = L"\"" + shell + L"\" /c " + args;
    return Run(shell, commandLine, timeoutMs);
}

ProcessResult ProcessRunner::Run(const std::wstring& application,
                                 const std::wstring& commandLine,
                                 DWORD timeoutMs) {
    ProcessResult result;

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return result;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nulInput = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                  OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = nulInput;

    std::vector<wchar_t> cmdLine(commandLine.begin(), commandLine.end());
    cmdLine.push_back(L'\0');

    PROCESS_INFORMATION pi = {};
    BOOL created = CreateProcessW(application.c_str(), cmdLine.data(),
                                  nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                  nullptr, nullptr, &si, &pi);

    CloseHandle(writePipe);
    if (nulInput != INVALID_HANDLE_VALUE) CloseHandle(nulInput);
    if (!created) {
        CloseHandle(readPipe);
        return result;
    }
    result.started = true;

    std::string bytes;
    char buffer[4096];
    ULONGLONG start = GetTickCount64();

    for (;;) {
        DWORD available = 0;
        if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) &&
            available > 0) {
            DWORD toRead = available < sizeof(buffer) ? available
                                                      : static_cast<DWORD>(sizeof(buffer));
            DWORD read = 0;
            if (!ReadFile(readPipe, buffer, toRead, &read, nullptr) || read == 0) {
                break;
            }
            bytes.append(buffer, read);
            continue;
        }

        if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
            // Drain whatever the child wrote before exiting.
            DWORD read = 0;
            while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
                bytes.append(buffer, read);
            }
            break;
        }

        if (GetTickCount64() - start > timeoutMs) {
            TerminateProcess(pi.hProcess, static_cast<UINT>(-1));
            WaitForSingleObject(pi.hProcess, 5000);
            break;
        }
    }

    DWORD exitCode = static_cast<DWORD>(-1);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    result.exitCode = exitCode;
    result.output = DecodeOutput(bytes);
    TrimTrailingNewlines(result.output);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(readPipe);
    return result;
}
