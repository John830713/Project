#pragma once

#include <windows.h>

#include <string>

// Result of one hidden child-process invocation.
struct ProcessResult {
    bool started = false;
    DWORD exitCode = static_cast<DWORD>(-1);
    std::wstring output;  // stdout + stderr, trailing newlines trimmed

    bool Ok() const { return started && exitCode == 0; }
};

// Runs console commands without flashing a window and captures their output.
// Remote-init operations are expressed as plain commands (reg / netsh / sc /
// winrm / net), so this is the single place that knows how to launch them.
class ProcessRunner {
public:
    // True when the current process token is elevated.
    static bool IsElevated();

    // Runs "cmd.exe /c <args>". Use for built-ins such as reg, sc, netsh, net.
    static ProcessResult RunCmd(const std::wstring& args, DWORD timeoutMs = 60000);

    // Relaunches the current executable through ShellExecuteEx("runas").
    // Returns false when the user declines the UAC prompt.
    static bool RestartElevated(const std::wstring& extraArgs);

    // Product name from the registry ("Windows 11 Pro", "Windows 10 Home", ...).
    static std::wstring GetProductName();

    // Home editions cannot host Remote Desktop, so callers disable that item.
    static bool IsWindowsHome();

private:
    static ProcessResult Run(const std::wstring& application,
                             const std::wstring& commandLine,
                             DWORD timeoutMs);
};
