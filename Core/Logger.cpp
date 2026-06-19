#include "Logger.h"

#include <fstream>
#include <filesystem>
#include <windows.h>

namespace fs = std::filesystem;

HINSTANCE Logger::s_hInstance = nullptr;

//==============================================================================
// Initialize - sets up log directory
//==============================================================================

void Logger::Initialize(HINSTANCE hInstance) {
    s_hInstance = hInstance;
    fs::create_directories(GetLogDir());
}

fs::path Logger::GetBaseDir() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path();
}

fs::path Logger::GetLogDir() {
    return GetBaseDir() / L"Log";
}

namespace {

//------------------------------------------------------------------------------
// GetTimestampString - returns formatted local time: YYYY-MM-DD HH:MM:SS
//------------------------------------------------------------------------------

    std::wstring GetTimestampString() {
        WCHAR buffer[32];
        SYSTEMTIME st;

        GetLocalTime(&st);

        swprintf(buffer, 32, L"%04d-%02d-%02d %02d:%02d:%02d",
                    st.wYear, st.wMonth, st.wDay,
                    st.wHour, st.wMinute, st.wSecond);

        return std::wstring(buffer);
    }
}

//==============================================================================
// Write - writes a timestamped log entry to Log/<category>.txt
//------------------------------------------------------------------------------
// Format: [YYYY-MM-DD HH:MM:SS] [Category] Message
//==============================================================================

void Logger::Write(const std::wstring& category, const std::wstring& message) {

    fs::create_directories(GetLogDir());

    std::wstring timestamp = GetTimestampString();

    std::wstring logEntry = L"[" + timestamp + L"] [" + category + L"] " + message + L"\n";

    fs::path filePath = GetLogDir() / (category + L".txt");

    std::wofstream file(filePath, std::ios::app);
    if (file.is_open()) {
        file << logEntry;
        file.close();
    }
}

//==============================================================================
// WriteModuleLog - writes to Log/Log_<moduleName>.txt
//------------------------------------------------------------------------------
// Format: [YYYY-MM-DD HH:MM:SS] [ModuleName] Message
//==============================================================================

void Logger::WriteModuleLog(const std::wstring& moduleName, const std::wstring& message) {

    fs::create_directories(GetLogDir());

    std::wstring timestamp = GetTimestampString();

    std::wstring logEntry = L"[" + timestamp + L"] [" + moduleName + L"] " + message + L"\n";

    fs::path filePath = GetLogDir() / (L"Log_" + moduleName + L".txt");

    std::wofstream file(filePath, std::ios::app);
    if (file.is_open()) {
        file << logEntry;
        file.close();
    }
}
