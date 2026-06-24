#pragma once

#include "AutoKeyParser.h"
#include "../../Core/IFeatureModule.h"
#include "../../Core/ConfigTypes.h"

#include <map>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

struct AutoKeyAction {
    int id = 0;
    std::wstring name;
    std::wstring keys;           // "F13" or "Ctrl+A,Ctrl+C"
    int intervalSeconds = 1200;
    int repeatCount = 0;         // 0 = infinite
    int actionMode = 1;          // 0 = press_once, 1 = repeat
    std::wstring triggerHotkey;  // "F8" or "" (none)
    int triggerMode = 0;         // 0 = press (exec once), 1 = toggle
    bool autoStart = false;

    volatile LONG running = 0;
    std::thread thread;

    AutoKeyAction() = default;
    AutoKeyAction(AutoKeyAction&& other) noexcept
        : id(other.id)
        , name(std::move(other.name))
        , keys(std::move(other.keys))
        , intervalSeconds(other.intervalSeconds)
        , repeatCount(other.repeatCount)
        , actionMode(other.actionMode)
        , triggerHotkey(std::move(other.triggerHotkey))
        , triggerMode(other.triggerMode)
        , autoStart(other.autoStart)
        , running(static_cast<LONG>(InterlockedExchange(&other.running, 0)))
        , thread(std::move(other.thread))
    {}
    AutoKeyAction& operator=(AutoKeyAction&& other) noexcept {
        id = other.id;
        name = std::move(other.name);
        keys = std::move(other.keys);
        intervalSeconds = other.intervalSeconds;
        repeatCount = other.repeatCount;
        actionMode = other.actionMode;
        triggerHotkey = std::move(other.triggerHotkey);
        triggerMode = other.triggerMode;
        autoStart = other.autoStart;
        running = static_cast<LONG>(InterlockedExchange(&other.running, 0));
        thread = std::move(other.thread);
        return *this;
    }
};

class AutoKeyModule : public IFeatureModule {
public:
    AutoKeyModule();
    ~AutoKeyModule() override;

    const wchar_t* GetModuleName() const override { return L"AutoKey"; }
    const wchar_t* GetDisplayName() const override { return L"AutoKey"; }
    const wchar_t* GetVersion() const override { return L"1.0.0"; }
    const wchar_t* GetPurpose() const override { return L"Configurable key press automation."; }
    const wchar_t* GetConfigFileName() const override { return L"Config_AutoKey.ini"; }
    const wchar_t* GetConfigSectionName() const override { return L"AutoKey"; }
    const wchar_t* GetLogSourceName() const override { return L"AutoKey"; }

    const std::vector<ConfigFieldDefinition>& GetConfigDefinitions() const override;

    void Initialize(HINSTANCE hInst, IHostContext* host) override;
    void LoadConfig() override;
    void SaveConfig() override;
    void ApplyConfig() override;
    void Shutdown() override;

    std::wstring GetValue(const std::wstring& key) const override;
    void SetValue(const std::wstring& key, const std::wstring& value) override;

    std::vector<ContextMenuItem> GetContextMenuItems() const override;
    void ExecuteContextMenuItem(int itemId) override;

    // Custom settings
    bool HasCustomSettings() const override { return true; }
    void OpenCustomSettings(HWND parent) override { ShowManageDialog(parent); }

    // Action management
    void StartAction(int actionId);
    void StopAction(int actionId);
    void StopAll();
    bool IsActionRunning(int actionId) const;
    std::vector<AutoKeyAction>& GetActions() { return m_actions; }
    void SaveActions();

    // Dialog
    void ShowManageDialog(HWND parent);

    // Hotkey handling
    void OnHotkey(int actionId);

private:
    void InitializeDefinitions();
    void InitializeDefaults();
    bool GetBoolValue(const std::wstring& key, bool defaultValue) const;

    void LoadActions();
    void RegisterHotkeys();
    void UnregisterHotkeys();
    void DestroyHotkeyWindow();

    void ExecuteSequence(const std::vector<ParsedChord>& sequence);

    static LRESULT CALLBACK HotkeyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void RunAction(int actionId);

    enum {
        kMenuStopAll = 2000,
        kMenuStartBase = 0,
        kMenuStopBase = 1000,
    };

    std::vector<ConfigFieldDefinition> m_definitions;
    std::map<std::wstring, std::wstring> m_values;
    std::vector<AutoKeyAction> m_actions;
    IHostContext* m_host = nullptr;

    HINSTANCE m_hInst = nullptr;
    HWND m_hHotkeyWnd = nullptr;
};
