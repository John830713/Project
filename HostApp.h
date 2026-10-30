#pragma once

#include "Core/IHostContext.h"
#include "Core/ModuleManager.h"
#include "Core/InputManager.h"
#include "Core/LoaderConfig.h"
#include "Services/TranslationService.h"
#include "Tray/TrayHost.h"

#include <windows.h>
#include <string>

class MainWindow;

class HostApp : public IHostContext {
public:
    HostApp();
    ~HostApp();

    bool Initialize(HINSTANCE hInstance, int nCmdShow, const LoaderConfig::Data& config,
                    bool startHidden = false);
    int Run();

    //--- IHostContext ---
    HINSTANCE GetInstance() const override;
    HWND GetMainWindow() const override;
    TranslationService* GetTranslationService() const override;
    void RefreshTrayTooltip() override { m_trayHost.UpdateTooltip(); }

    //--- Manager access ---
    ModuleManager& GetModuleManager();
    InputManager& GetInputManager();

    //--- Main window control (tray host drives these) ---
    bool IsMainWindowAlive() const;
    bool IsMainWindowVisible() const;
    void SetMainWindowVisible(bool visible);
    void RecreateMainWindow();
    void OpenMainWindowSettings();

    // Debug-only: destroy the pet window without recreating, so the watchdog
    // recovery path can be exercised. The WM_DESTROY quit is suppressed.
    void DebugDestroyMainWindow();

    // Checks the UpdateManager relaunch handoff event; when an update has
    // asked this instance to reload, spawns the new build and quits. Safe to
    // call frequently (non-blocking).
    void PollForReload();

    //--- Tray state (debug channel) ---
    bool IsTrayIconAdded() const { return m_trayHost.IsIconAdded(); }
    unsigned long GetTrayWatchdogTicks() const { return m_trayHost.GetWatchdogTicks(); }
    const std::wstring& GetTrayTooltipText() const { return m_trayHost.GetTooltipText(); }

private:
    bool CreateMainWindow(int nCmdShow);
    bool LoadMainWindowVisibility() const;
    void SaveMainWindowVisibility(bool visible) const;

    std::wstring LoadProjectTitle() const;
    std::wstring LoadHostHint() const;
    std::wstring LoadAppVersion() const;
    std::wstring ReadTextFileIfExists(const wchar_t* fileName) const;
    std::wstring GetBaseDirName() const;

private:
    HINSTANCE m_hInstance = nullptr;
    HWND m_mainWindow = nullptr;
    MainWindow* m_window = nullptr;
    ModuleManager m_moduleManager;
    InputManager m_inputManager;
    TrayHost m_trayHost;
    bool m_comInitialized = false;};
