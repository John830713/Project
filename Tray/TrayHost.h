//==============================================================================
// TrayHost.h - System tray host for the application shell
// Purpose:   Owns a hidden message-only window and the notification-area icon.
//            Host-level tray menu items (show/hide pet, settings, reset pet
//            window, exit) plus the module context menu groups are presented
//            here so the tray stays usable even when the pet window is hidden.
//
//            v1 scope: with the pet window hidden the tray icon is the only
//            way back. Service start/stop, health polling and progress
//            feedback are Phase 2 concerns and intentionally absent.
//
//            Finding (2026-10-20): the pet window, this message window and
//            the message loop all live on the main thread, so a hard hang of
//            the pet window freezes the tray too. The watchdog below can only
//            detect a *destroyed* pet window and rebuild it; it cannot rescue
//            a wedged thread. A genuine hang needs an external watchdog, which
//            lands in Phase 2.
//==============================================================================

#pragma once

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

class HostApp;

class TrayHost {
public:
    explicit TrayHost(HostApp* host);
    ~TrayHost();

    bool Initialize();
    void Shutdown();

    // Refresh the icon tooltip and notify the shell after the pet window was
    // recreated, so the icon keeps pointing at a live window.
    void OnPetWindowRecreated(HWND newWindow);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool AddIcon();
    void RemoveIcon();
    void ShowMenu(HWND hwnd, POINT pt);
    void OnCommand(UINT cmd);
    void OnWatchdogTick();
    void UpdateTooltip();

    static std::wstring Tr(const wchar_t* text);

    HostApp* m_host;
    HWND m_hwnd;
    HICON m_icon;
    bool m_iconAdded;
    bool m_shuttingDown;
};
