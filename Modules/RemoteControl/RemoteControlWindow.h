//==============================================================================
// RemoteControlWindow.h - Remote Control status and drop window
//==============================================================================

#pragma once
#include <windows.h>
#include <string>

class RemoteControlWindow {
public:
    RemoteControlWindow(HINSTANCE hInst);
    ~RemoteControlWindow();

    bool Create(HWND parent);
    void Show(int cmdShow);
    void Destroy();

    void SetConnected(bool connected);
    void SetHookActive(bool active);
    void SetSavePath(const std::wstring& path);
    std::wstring GetSavePath() const;

    using DropCallback = void(*)(const std::wstring& path, void* ud);
    void SetDropCallback(DropCallback cb, void* ud);

    HWND GetHwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l);
    void RegisterWindowClass();

    HINSTANCE m_hInst;
    HWND m_hwnd = nullptr;
    HWND m_saveEdit = nullptr;
    HWND m_browseBtn = nullptr;
    HWND m_helpBtn = nullptr;

    bool m_connected = false;
    bool m_hookActive = false;
    std::wstring m_savePath;

    DropCallback m_dropCb = nullptr;
    void* m_dropUd = nullptr;

    static void ShowHelpPopup(HWND parent);

    static constexpr int kWidth = 380;
    static constexpr int kHeight = 240;
};
