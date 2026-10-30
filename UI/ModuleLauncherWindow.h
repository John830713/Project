//==============================================================================
// ModuleLauncherWindow.h - Plain window carrier for feature modules
// Purpose:   Hosts the compiled modules without the pet window. It lists every
//            module context-menu item as a row and dispatches the selection
//            through ModuleManager, so a flavour can run as an ordinary
//            desktop window (LoaderConfig::Mode::Window).
//==============================================================================

#pragma once
#include <windows.h>
#include <string>
#include <vector>

class ModuleManager;

class ModuleLauncherWindow {
public:
    ModuleLauncherWindow(HINSTANCE hInstance, ModuleManager* modules);
    ~ModuleLauncherWindow();

    bool Create();
    void Show(int nCmdShow);

//==============================================================================
// Private
//==============================================================================

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void BuildControls();
    void Layout();
    void RefreshList();
    void RunSelected();

    static std::wstring Tr(const wchar_t* key);

    HINSTANCE m_hInstance;
    ModuleManager* m_modules;
    HWND m_hwnd = nullptr;
    HWND m_list = nullptr;
    HWND m_status = nullptr;
    HWND m_runButton = nullptr;
    HWND m_refreshButton = nullptr;
    HWND m_exitButton = nullptr;

    // Parallel to the list box rows: the ModuleManager unique id each row runs.
    std::vector<int> m_rowActionIds;
};
