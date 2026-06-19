//==============================================================================
// TemplateWindow.h - Template module window
// Purpose:   Manages the Template module's UI window, including creation,
//            event handling, and display of template data.
//==============================================================================

#pragma once
#include <windows.h>
#include "TemplateLogic.h"
#include "../../Core/IHostContext.h"

class TemplateWindow {
public:
    //==============================================================================
    // Public API
    //==============================================================================
    TemplateWindow(HINSTANCE hInstance);
    ~TemplateWindow();

    bool Create(HWND hParent);
    void Show(int nCmdShow);
    //------------------------------------------------------------------------------
    // Data Input
    //------------------------------------------------------------------------------
    void Open(const TemplateOpenRequest& req, IHostContext* host);
    void HandleDrop(HDROP hDrop);

    //==============================================================================
    // Private
    //==============================================================================
private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    
    void BuildUI();
    void RefreshInfoPanels();
    void UpdateOutputControls();
    void OnGenerateClicked();

    //==============================================================================
    // Members
    //==============================================================================
    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;

    // State management using template-specific structures
    TemplateOpenRequest m_request;
    TemplateInfo m_info1; 
    TemplateInfo m_info2;
    
    HWND hOutputBtn = nullptr;
    HWND hOutputPath = nullptr;
};