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

//------------------------------------------------------------------------------
// ToUtf8 - converts a wide string to UTF-8.
//
// Log files are written as UTF-8 so non-ASCII text (e.g. paths containing CJK
// characters) survives the round-trip. std::wofstream cannot be used for this:
// under a narrow stream locale it throws std::ios_failure on characters that are
// not representable in the external encoding, which would abort the caller.
//------------------------------------------------------------------------------

    std::string ToUtf8(const std::wstring& w) {
        if (w.empty()) return {};

        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                    nullptr, 0, nullptr, nullptr);
        if (n <= 0) return {};

        std::string s(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                            &s[0], n, nullptr, nullptr);
        return s;
    }

//------------------------------------------------------------------------------
// AppendLine - appends a UTF-8 line to a log file. Never throws.
//------------------------------------------------------------------------------

    void AppendLine(const fs::path& filePath, const std::string& utf8Line) {
        try {
            fs::create_directories(filePath.parent_path());

            std::ofstream file(filePath, std::ios::binary | std::ios::app);
            if (!file.is_open()) return;

            file << utf8Line << "\n";
            file.close();
        } catch (...) {
            // Logging must never take down the caller.
        }
    }

}

//==============================================================================
// Write - writes a timestamped log entry to Log/<category>.txt
//------------------------------------------------------------------------------
// Format: [YYYY-MM-DD HH:MM:SS] [Category] Message
//==============================================================================

void Logger::Write(const std::wstring& category, const std::wstring& message) {

    std::wstring logEntry = L"[" + GetTimestampString() + L"] [" + category + L"] " + message;

    AppendLine(GetLogDir() / (category + L".txt"), ToUtf8(logEntry));
}

//==============================================================================
// WriteModuleLog - writes to Log/Log_<moduleName>.txt
//------------------------------------------------------------------------------
// Format: [YYYY-MM-DD HH:MM:SS] [ModuleName] Message
//==============================================================================

void Logger::WriteModuleLog(const std::wstring& moduleName, const std::wstring& message) {

    std::wstring logEntry = L"[" + GetTimestampString() + L"] [" + moduleName + L"] " + message;

    AppendLine(GetLogDir() / (L"Log_" + moduleName + L".txt"), ToUtf8(logEntry));
}
