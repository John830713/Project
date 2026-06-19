//==============================================================================
// TemplateModule.h - Template feature module
// Purpose:   Implements IFeatureModule and IDropActionProvider for the
//            Template module. Manages lifecycle, config, and drop actions.
//==============================================================================

#pragma once

#include "../../Core/IFeatureModule.h"
#include "../../Core/IDropActionProvider.h"
#include "../../Core/ConfigTypes.h"
#include "TemplateTypes.h"

#include <map>
#include <string>
#include <vector>

class TemplateModule : public IFeatureModule, public IDropActionProvider {
public:
    TemplateModule();
    ~TemplateModule() override;

    //==============================================================================
    // Lifecycle
    //==============================================================================
    void Initialize(HINSTANCE hInst, IHostContext* host) override;
    void LoadConfig() override;
    void SaveConfig() override;
    void ApplyConfig() override;
    void Shutdown() override;

    //==============================================================================
    // Metadata
    //==============================================================================
    const wchar_t* GetModuleName() const override { return L"Template"; }
    const wchar_t* GetDisplayName() const override { return L"Template Module"; }
    const wchar_t* GetVersion() const override { return L"0.0.0"; }
    const wchar_t* GetPurpose() const override { return L"Skeleton module demonstrating the standard module pattern."; }
    const wchar_t* GetConfigFileName() const override { return L"Config_Template.ini"; }
    const wchar_t* GetConfigSectionName() const override { return L"Template"; }
    const wchar_t* GetLogSourceName() const override { return L"Template"; }

    //==============================================================================
    // Configuration
    //==============================================================================
    const std::vector<ConfigFieldDefinition>& GetConfigDefinitions() const override;
    std::wstring GetValue(const std::wstring& key) const override;
    void SetValue(const std::wstring& key, const std::wstring& value) override;

    //==============================================================================
    // Drop Actions
    //==============================================================================
    bool CanHandleDrop(const DropContext& ctx) const override;
    std::vector<DropActionDefinition> GetDropActions(const DropContext& ctx) const override;
    void ExecuteDropAction(int actionId, const DropContext& ctx) override;

    //==============================================================================
    // Context menu (IFeatureModule override)
    //==============================================================================
    std::vector<ContextMenuItem> GetContextMenuItems() const override;
    void ExecuteContextMenuItem(int itemId) override;

    //==============================================================================
    // Private Helpers
    //==============================================================================
private:
    void InitializeDefinitions();
    void InitializeDefaults();
    bool GetBoolValue(const std::wstring& key, bool defaultValue) const;

    //==============================================================================
    // Members
    //==============================================================================
private:
    IHostContext* m_host = nullptr;
    std::vector<ConfigFieldDefinition> m_definitions;
    std::map<std::wstring, std::wstring> m_values;
};