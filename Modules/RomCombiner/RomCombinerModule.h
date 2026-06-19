//==============================================================================
// RomCombinerModule.h - Rom Combiner feature module
// Purpose:   Implements IFeatureModule and IDropActionProvider for the
//            Rom Combiner module. Manages config, lifecycle, and drag-drop.
//==============================================================================

#pragma once
#include "../../Core/IFeatureModule.h"
#include "../../Core/IDropActionProvider.h"
#include "../../Core/ConfigTypes.h"
#include "RomCombinerTypes.h"
#include <map>
#include <string>
#include <vector>

class RomCombinerModule : public IFeatureModule, public IDropActionProvider {
public:
    RomCombinerModule();
    virtual ~RomCombinerModule() = default;

    //==============================================================================
    // IFeatureModule Interface
    //==============================================================================
    void Initialize(HINSTANCE hInst, IHostContext* host) override;
    void LoadConfig() override;
    void SaveConfig() override;
    void ApplyConfig() override;
    void Shutdown() override;

    const wchar_t* GetModuleName() const override { return L"RomCombiner"; }
    const wchar_t* GetDisplayName() const override { return L"ROM Combiner"; }
    const wchar_t* GetVersion() const override { return L"1.0.0"; }
    const wchar_t* GetPurpose() const override { return L"Concatenates two ROM files into a single combined ROM."; }
    const wchar_t* GetConfigFileName() const override { return L"Config_RomCombiner.ini"; }
    const wchar_t* GetConfigSectionName() const override { return L"RomCombiner"; }
    const wchar_t* GetLogSourceName() const override { return L"RomCombiner"; }
    
    std::wstring GetValue(const std::wstring& key) const override;
    void SetValue(const std::wstring& key, const std::wstring& value) override;
    const std::vector<ConfigFieldDefinition>& GetConfigDefinitions() const override;

    //==============================================================================
    // IDropActionProvider Interface
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
    // Member Data
    //==============================================================================
private:
    //--- Module instance handle and host context
    HINSTANCE m_hInst = nullptr;
    IHostContext* m_host = nullptr;
    //--- Config definitions and values
    std::vector<ConfigFieldDefinition> m_definitions;
    std::map<std::wstring, std::wstring> m_values;
    //--- Action and menu IDs
    static const int kActionOpenCombiner = 1001;
    static const int kMenuOpenCombiner = 1002;
    //--- Files collected via drag-drop before opening the window
    std::vector<std::wstring> m_pendingFiles;
};