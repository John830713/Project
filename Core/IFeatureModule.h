//==============================================================================
// IFeatureModule.h - Feature module interface
// Purpose:   All modules must implement this interface.
//            Provides identity, lifecycle, config, value access,
//            and optional context menu support.
//==============================================================================

#pragma once

#include "ConfigTypes.h"
#include "IHostContext.h"

#include <windows.h>
#include <string>
#include <vector>

struct ContextMenuItem {
    int itemId;
    std::wstring label;
    UINT flags = MF_STRING;
    std::wstring tooltip;
};

// Flags shared by every host that renders ContextMenuItem into a menu.
// Keeping them here (next to the struct) means the pet window and the tray
// host agree on the bit values without one including the other.
constexpr UINT kContextMenuItemDisabled = 0x80000000u;
constexpr UINT kContextMenuItemChecked = 0x40000000u;

class IFeatureModule {
public:
    virtual ~IFeatureModule() {}

    //--- Identity ---
    virtual const wchar_t* GetModuleName() const = 0;
    virtual const wchar_t* GetDisplayName() const = 0;

    //--- Config identity ---
    virtual const wchar_t* GetConfigFileName() const = 0;
    virtual const wchar_t* GetConfigSectionName() const = 0;

    //--- Log identity ---
    virtual const wchar_t* GetLogSourceName() const = 0;

    //--- Config definitions for dynamic settings UI ---
    virtual const std::vector<ConfigFieldDefinition>& GetConfigDefinitions() const = 0;

    //--- Lifecycle ---
    virtual void Initialize(HINSTANCE hInst, IHostContext* host) = 0;
    virtual void LoadConfig() = 0;
    virtual void SaveConfig() = 0;
    virtual void ApplyConfig() = 0;
    virtual void Shutdown() = 0;

    //--- Value access ---
    virtual std::wstring GetValue(const std::wstring& key) const = 0;
    virtual void SetValue(const std::wstring& key, const std::wstring& value) = 0;

    //--- Version / purpose (optional, override to provide About info) ---
    virtual const wchar_t* GetVersion() const { return L"1.0.0"; }
    virtual const wchar_t* GetPurpose() const { return L""; }

    //--- Availability ---
    // Override when the module has nothing to offer on this machine (missing
    // external resource tree, unsupported hardware, ...). The host then hides
    // every entry point that would operate it — context menu group and settings
    // tab — so the machine looks like the module was never shipped.
    // This is NOT the same as `Enabled`: a disabled module still shows its
    // settings so the user can switch it back on; an unavailable one has no
    // working setting to show.
    virtual bool IsAvailable() const { return true; }

    //--- Context menu (optional, override to provide right-click items) ---
    virtual std::vector<ContextMenuItem> GetContextMenuItems() const { return {}; }
    virtual void ExecuteContextMenuItem(int /*itemId*/) {}

    //--- Custom settings UI (optional) ---
    virtual bool HasCustomSettings() const { return false; }
    virtual void OpenCustomSettings(HWND /*parent*/) {}
};
