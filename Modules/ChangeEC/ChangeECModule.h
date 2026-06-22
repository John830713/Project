//==============================================================================
// ChangeECModule.h - ChangeEC module interface
// Purpose:   Provides IFeatureModule and IDropActionProvider for
//            the ChangeEC (EC Modifier) module.
//==============================================================================

#pragma once
#include "../../Core/IFeatureModule.h"
#include "../../Core/IDropActionProvider.h"
#include "../../Core/ConfigTypes.h"
#include <map>
#include <vector>
#include <string>

//==============================================================================
// IFeatureModule identity
//==============================================================================

class ChangeECModule : public IFeatureModule, public IDropActionProvider {
public:
    ChangeECModule();
    ~ChangeECModule() override = default;

    const wchar_t* GetModuleName() const override { return L"ChangeEC"; }
    const wchar_t* GetDisplayName() const override { return L"EC Modifier"; }
    const wchar_t* GetVersion() const override { return L"1.0.0"; }
    const wchar_t* GetPurpose() const override { return L"Patches an EC binary into a BIOS ROM at a configurable offset."; }
    const wchar_t* GetConfigFileName() const override { return L"Config_ChangeEC.ini"; }
    const wchar_t* GetConfigSectionName() const override { return L"ChangeEC"; }
    const wchar_t* GetLogSourceName() const override { return L"ChangeEC"; }
    
    const std::vector<ConfigFieldDefinition>& GetConfigDefinitions() const override;

    //==============================================================================
    // Lifecycle
    //==============================================================================
    
    void Initialize(HINSTANCE hInst, IHostContext* host) override;
    void LoadConfig() override;
    void SaveConfig() override;
    void ApplyConfig() override;
    void Shutdown() override;
    std::wstring GetValue(const std::wstring& key) const override;
    void SetValue(const std::wstring& key, const std::wstring& value) override;

    //==============================================================================
    // Drop Actions
    //==============================================================================

    bool CanHandleDrop(const DropContext& ctx) const override;
    std::vector<DropActionDefinition> GetDropActions(const DropContext& ctx) const override;
    void ExecuteDropAction(int actionId, const DropContext& ctx) override;

    //--- Context menu (IFeatureModule override) ---
    std::vector<ContextMenuItem> GetContextMenuItems() const override;
    void ExecuteContextMenuItem(int itemId) override;

private:
    //==============================================================================
    // Private helpers
    //==============================================================================

    void InitializeDefinitions();
    void InitializeDefaults();
    bool GetBoolValue(const std::wstring& key, bool defaultValue) const;
    std::wstring GetStringValue(const std::wstring& key, const std::wstring& defaultVal) const;

private:
    HINSTANCE m_hInst = nullptr;
    IHostContext* m_host = nullptr;
    std::vector<ConfigFieldDefinition> m_definitions;
    std::map<std::wstring, std::wstring> m_values;
    std::vector<std::wstring> m_pendingFiles;
};
