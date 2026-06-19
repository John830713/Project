//==============================================================================
// ConfigManager.h - Configuration file management
// Purpose:   Manages INI-based config files under Config/ directory.
//            Provides read/write with safe defaults (missing keys are added,
//            existing keys are never overwritten).
//==============================================================================

#pragma once

#include "ConfigTypes.h"

#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <map>

class ConfigManager {
public:
    static void Initialize(HINSTANCE hInstance);

    static std::filesystem::path GetConfigDirectory();
    static std::filesystem::path GetModuleConfigPath(const std::wstring& fileName);

    static void EnsureModuleConfig(
        const std::wstring& fileName,
        const std::wstring& sectionName,
        const std::vector<ConfigFieldDefinition>& definitions);

    static std::map<std::wstring, std::wstring> LoadModuleConfig(
        const std::wstring& fileName,
        const std::wstring& sectionName,
        const std::vector<ConfigFieldDefinition>& definitions);

    static void SaveModuleConfig(
        const std::wstring& fileName,
        const std::wstring& sectionName,
        const std::map<std::wstring, std::wstring>& values);

private:
    static std::filesystem::path GetBaseDir();

private:
    static HINSTANCE s_hInstance;
};
