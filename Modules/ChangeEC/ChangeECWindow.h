//==============================================================================
// ChangeECWindow.h - Change EC tool window interface
// Purpose:   Defines the UI window for the Change EC (EC Modifier) tool.
//==============================================================================

#pragma once
#include <windows.h>
#include "ChangeECTypes.h"
#include "../../Core/IHostContext.h"

//==============================================================================
// class ChangeECWindow - Public API
//==============================================================================

class ChangeECWindow {
public:
    ChangeECWindow(HINSTANCE hInstance);
    ~ChangeECWindow();

    bool Create(HWND hParent);
    void Show(int nCmdShow);
    void Open(const ChangeECOpenRequest& req, IHostContext* host);

//==============================================================================
// Private
//==============================================================================

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void BuildUI();
    void RefreshInfo();
    void OnGenerate();

    HINSTANCE m_hInstance;
    HWND m_hwnd = nullptr;
    IHostContext* m_host = nullptr;
    ChangeECOpenRequest m_request;
    bool m_outputToOrg = true;
};