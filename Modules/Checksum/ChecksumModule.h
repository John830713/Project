//==============================================================================
// ChecksumModule.h - Checksum calculation feature module
// Purpose:   Calculates a simple additive checksum for .bin and .rom files.
//            Supports drag-and-drop (IDropActionProvider) and right-click
//            context menu via IFeatureModule overrides.
//==============================================================================

#pragma once

#include "../../Core/IFeatureModule.h"
#include "../../Core/IDropActionProvider.h"

#include <map>

class ChecksumModule : public IFeatureModule, public IDropActionProvider {
public:
    ChecksumModule();
    ~ChecksumModule() override;

    //--- IFeatureModule identity ---
    const wchar_t* GetModuleName() const override { return L"Checksum"; }
    const wchar_t* GetDisplayName() const override { return L"Checksum"; }
    const wchar_t* GetVersion() const override { return L"1.0.0"; }
    const wchar_t* GetPurpose() const override { return L"Calculates an additive checksum for .bin and .rom files."; }
    const wchar_t* GetConfigFileName() const override { return L"Config_Checksum.ini"; }
    const wchar_t* GetConfigSectionName() const override { return L"Checksum"; }
    const wchar_t* GetLogSourceName() const override { return L"Checksum"; };

    const std::vector<ConfigFieldDefinition>& GetConfigDefinitions() const override;

    //--- IFeatureModule lifecycle ---
    void Initialize(HINSTANCE hInst, IHostContext* host) override;
    void LoadConfig() override;
    void SaveConfig() override;
    void ApplyConfig() override;
    void Shutdown() override;

    std::wstring GetValue(const std::wstring& key) const override;
    void SetValue(const std::wstring& key, const std::wstring& value) override;

    //--- IDropActionProvider ---
    bool CanHandleDrop(const DropContext& ctx) const override;
    std::vector<DropActionDefinition> GetDropActions(const DropContext& ctx) const override;
    void ExecuteDropAction(int actionId, const DropContext& ctx) override;

    //--- Context menu (IFeatureModule override) ---
    std::vector<ContextMenuItem> GetContextMenuItems() const override;
    void ExecuteContextMenuItem(int itemId) override;

private:
    void InitializeDefinitions();
    void InitializeDefaults();

    bool GetBoolValue(const std::wstring& key, bool defaultValue) const;
    int GetIntValue(const std::wstring& key, int defaultValue) const;
    void ProcessFile(const std::wstring& filePath);

private:
    HINSTANCE m_hInstance;
    IHostContext* m_host;
    std::vector<ConfigFieldDefinition> m_definitions;
    std::map<std::wstring, std::wstring> m_values;
};
