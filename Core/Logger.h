//==============================================================================
// Logger.h - Logging utility
// Purpose:   Provides categorized logging to files under Log/.
//            Write() for shared categories, WriteModuleLog() for per-module.
//==============================================================================

#pragma once

#include <windows.h>
#include <string>
#include <filesystem>

class Logger {
public:
    static void Initialize(HINSTANCE hInstance);
    static void Write(const std::wstring& category, const std::wstring& message);
    static void WriteModuleLog(const std::wstring& moduleName, const std::wstring& message);

private:
    static std::filesystem::path GetBaseDir();
    static std::filesystem::path GetLogDir();

private:
    static HINSTANCE s_hInstance;
};
