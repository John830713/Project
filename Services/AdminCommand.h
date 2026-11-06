#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Runs admin-requiring work in a separate elevated copy of this executable.
//
// The normal app keeps running as a standard user. When an operation needs
// administrator rights, it launches:
//
//   Project.exe --admin <op> [op flags] --result <temp file>
//
// through ShellExecuteEx("runas"), waits for it, then reads the result file.
// That deliberately avoids restarting the whole pet elevated: UIPI would then
// block drag-and-drop from Explorer and every launch would raise a UAC prompt.
//
// The helper is the same binary, so it always matches the running version and
// needs no extra deployment step.
class AdminCommand {
public:
    struct Result {
        bool started = false;  // UAC accepted and the helper process ran
        bool allOk = false;    // helper reported success
        std::vector<std::wstring> lines;
    };

    // True when the command line starts with the --admin token.
    static bool IsInvocation(const wchar_t* cmdLine);

    // Elevated-side entry point. Performs one command and exits.
    static int Run(HINSTANCE hInstance, const wchar_t* cmdLine);

    // Standard-user side: launches this exe with `args` through UAC.
    static Result RunElevated(const std::wstring& args, DWORD timeoutMs = 120000);

    // High-level admin operations used by RemoteInitService.
    static Result ApplyRemoteInit(const std::vector<std::wstring>& ids,
                                  const std::wstring& userName);
    static Result RestoreRemoteInit(const std::wstring& backupPath);

    //--- Exposed for the headless test --------------------------------------
    static std::wstring QuoteArgument(const std::wstring& arg);
    static std::wstring BuildApplyArgs(const std::vector<std::wstring>& ids,
                                       const std::wstring& userName);
    static std::wstring BuildRestoreArgs(const std::wstring& backupPath);
    // Parses "RESULT=OK" + log lines. Returns true when the helper succeeded.
    static bool ParseResultText(const std::wstring& text,
                                std::vector<std::wstring>& lines);
    static void WriteResultFile(const std::wstring& path, bool allOk,
                                const std::vector<std::wstring>& lines);
    static std::wstring ReadTextFile(const std::wstring& path);
    static std::wstring MakeTempResultPath();
};
