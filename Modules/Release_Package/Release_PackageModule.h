//==============================================================================
// Release_PackageModule.h - Release_Package module interface
// Purpose:   Provides IFeatureModule for the Release_Package (7z Extractor) module.
//==============================================================================

#pragma once

#include "../../Core/IFeatureModule.h"
#include "../../Core/ConfigTypes.h"
#include "Release_PackageTypes.h"

#include <map>
#include <vector>
#include <string>

class Release_PackageModule : public IFeatureModule {
public:
    Release_PackageModule();
    ~Release_PackageModule() override = default;

    //==============================================================================
    // Metadata
    //==============================================================================
    const wchar_t* GetModuleName() const override { return L"Release_Package"; }
    const wchar_t* GetDisplayName() const override { return L"Release Package"; }
    const wchar_t* GetVersion() const override { return L"1.0.0"; }
    const wchar_t* GetPurpose() const override { return L"7z archive extractor with token-based rule engine."; }
    const wchar_t* GetConfigFileName() const override { return L"Config_Release_Package.ini"; }
    const wchar_t* GetConfigSectionName() const override { return L"Release_Package"; }
    const wchar_t* GetLogSourceName() const override { return L"Release_Package"; }

    //==============================================================================
    // Configuration
    //==============================================================================
    const std::vector<ConfigFieldDefinition>& GetConfigDefinitions() const override;
    std::wstring GetValue(const std::wstring& key) const override;
    void SetValue(const std::wstring& key, const std::wstring& value) override;

    //==============================================================================
    // Lifecycle
    //==============================================================================
    void Initialize(HINSTANCE hInst, IHostContext* host) override;
    void LoadConfig() override;
    void SaveConfig() override;
    void ApplyConfig() override;
    void Shutdown() override;

    //==============================================================================
    // Context menu
    //==============================================================================
    std::vector<ContextMenuItem> GetContextMenuItems() const override;
    void ExecuteContextMenuItem(int itemId) override;

    //==============================================================================
    // Public accessors for Window
    //==============================================================================
    std::wstring Get7zPath() const;
    std::wstring GetDefaultSourceDir() const;
    std::wstring GetOutputDir() const;

    void LoadRulesFromJson(const std::wstring& path);
    void SaveRulesToJson(const std::wstring& path);

    std::vector<RPParam>& GetParams() { return m_params; }
    std::vector<RPRule>& GetRules() { return m_rules; }
    const std::vector<RPParam>& GetParams() const { return m_params; }
    const std::vector<RPRule>& GetRules() const { return m_rules; }

private:
    void InitializeDefinitions();
    void InitializeDefaults();

    IHostContext* m_host = nullptr;
    std::vector<ConfigFieldDefinition> m_definitions;
    std::map<std::wstring, std::wstring> m_values;
    std::vector<RPParam> m_params;
    std::vector<RPRule> m_rules;
};