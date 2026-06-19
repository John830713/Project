//==============================================================================
// RomSeparatorModule.h - ROM Separator feature module
// Purpose:   Implements IFeatureModule and IDropActionProvider for splitting
//            ROM files via drag-and-drop.
//==============================================================================

#pragma once

#include "../../Core/IFeatureModule.h"
#include "../../Core/IDropActionProvider.h"
#include "RomSeparatorTypes.h"

#include <map>
#include <string>
#include <vector>

class RomSeparatorWindow;

//==============================================================================
// RomSeparatorModule
//==============================================================================
class RomSeparatorModule : public IFeatureModule, public IDropActionProvider {
public:
    //--- Constructor / Destructor ---
    RomSeparatorModule();
    ~RomSeparatorModule() override;

    //--- IFeatureModule identity ---
    const wchar_t* GetModuleName() const override;
    const wchar_t* GetDisplayName() const override;
    const wchar_t* GetVersion() const override;
    const wchar_t* GetPurpose() const override;
    const wchar_t* GetConfigFileName() const override;
    const wchar_t* GetConfigSectionName() const override;
    const wchar_t* GetLogSourceName() const override;

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

//==============================================================================
    // Config Public Accessors
    //==============================================================================
public:
    bool GetConfirmOverwrite() const;
    bool GetShowSuccessMessage() const;
    bool GetVerboseLog() const;

    RomSeparatorOutputMode GetDefaultOutputMode() const;
    void SetDefaultOutputMode(RomSeparatorOutputMode mode);

    IHostContext* GetHost() const;
    HINSTANCE GetInstance() const;

//==============================================================================
    // Private Helpers
    //==============================================================================
private:
    void InitializeDefinitions();
    void InitializeDefaults();

    bool GetBoolValue(const std::wstring& key, bool defaultValue) const;
    int GetIntValue(const std::wstring& key, int defaultValue) const;

    void OpenWindowForFile(const std::wstring& filePath);

//==============================================================================
    // Private Members
    //==============================================================================
private:
    HINSTANCE m_hInstance;
    IHostContext* m_host;
    std::vector<ConfigFieldDefinition> m_definitions;
    std::map<std::wstring, std::wstring> m_values;
    RomSeparatorWindow* m_window;
};