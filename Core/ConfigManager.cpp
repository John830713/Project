//==============================================================================
// ConfigManager.cpp - Configuration file management implementation
//==============================================================================

#include "ConfigManager.h"

#include <windows.h>

namespace fs = std::filesystem;

HINSTANCE ConfigManager::s_hInstance = nullptr;

//==============================================================================
// Initialize - ensures Config/ directory exists
//==============================================================================

void ConfigManager::Initialize(HINSTANCE hInstance) {
    s_hInstance = hInstance;
    fs::create_directories(GetConfigDirectory());
}

//------------------------------------------------------------------------------
// GetBaseDir - returns the directory containing the executable
//------------------------------------------------------------------------------

fs::path ConfigManager::GetBaseDir() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path();
}

fs::path ConfigManager::GetConfigDirectory() {
    return GetBaseDir() / L"Config";
}

fs::path ConfigManager::GetModuleConfigPath(const std::wstring& fileName) {
    return GetConfigDirectory() / fileName;
}

//==============================================================================
// EnsureModuleConfig - writes only missing keys with their default values
//------------------------------------------------------------------------------
// Existing keys in the INI file are never overwritten.
//==============================================================================

void ConfigManager::EnsureModuleConfig(
    const std::wstring& fileName,
    const std::wstring& sectionName,
    const std::vector<ConfigFieldDefinition>& definitions) {

    fs::create_directories(GetConfigDirectory());
    fs::path path = GetModuleConfigPath(fileName);

    for (const auto& def : definitions) {
        wchar_t buffer[512] = {};
        GetPrivateProfileStringW(
            sectionName.c_str(),
            def.key.c_str(),
            L"",
            buffer,
            static_cast<DWORD>(std::size(buffer)),
            path.c_str());

        if (wcslen(buffer) == 0) {
            WritePrivateProfileStringW(
                sectionName.c_str(),
                def.key.c_str(),
                def.defaultValue.c_str(),
                path.c_str());
        }
    }
}

//==============================================================================
// LoadModuleConfig - reads all config values, filling missing ones with defaults
//==============================================================================

std::map<std::wstring, std::wstring> ConfigManager::LoadModuleConfig(
    const std::wstring& fileName,
    const std::wstring& sectionName,
    const std::vector<ConfigFieldDefinition>& definitions) {

    EnsureModuleConfig(fileName, sectionName, definitions);

    std::map<std::wstring, std::wstring> values;
    fs::path path = GetModuleConfigPath(fileName);

    for (const auto& def : definitions) {
        wchar_t buffer[512] = {};
        GetPrivateProfileStringW(
            sectionName.c_str(),
            def.key.c_str(),
            def.defaultValue.c_str(),
            buffer,
            static_cast<DWORD>(std::size(buffer)),
            path.c_str());

        values[def.key] = buffer;
    }

    return values;
}

//==============================================================================
// SaveModuleConfig - writes all key-value pairs to the INI file
//==============================================================================

void ConfigManager::SaveModuleConfig(
    const std::wstring& fileName,
    const std::wstring& sectionName,
    const std::map<std::wstring, std::wstring>& values) {

    fs::create_directories(GetConfigDirectory());
    fs::path path = GetModuleConfigPath(fileName);

    for (const auto& pair : values) {
        WritePrivateProfileStringW(
            sectionName.c_str(),
            pair.first.c_str(),
            pair.second.c_str(),
            path.c_str());
    }
}
