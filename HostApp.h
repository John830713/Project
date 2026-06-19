#pragma once

#include <windows.h>

class HostApp {
public:
    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    int Run();
    HWND GetMainWindow() const { return m_mainWindow; }

private:
    HINSTANCE m_hInstance = nullptr;
    HWND m_mainWindow = nullptr;
};
