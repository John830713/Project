//==============================================================================
// RomCombinerWindow.h - Rom Combiner dialog window
// Purpose:   Manages the ROM Combiner UI window including display of ROM
//            info panels, output controls, and generation trigger.
//==============================================================================

#pragma once
#include <windows.h>
#include "RomCombinerLogic.h"
#include "../../Core/IHostContext.h"

class RomCombinerWindow {
public:
    //==============================================================================
    // Public Interface
    //==============================================================================
    RomCombinerWindow(HINSTANCE hInstance);
    ~RomCombinerWindow();

    bool Create(HWND hParent);
    void Show(int nCmdShow);
    void Open(const RomCombinerOpenRequest& req, IHostContext* host);
    void HandleDrop(HDROP hDrop);

private:
    //==============================================================================
    // Private Implementation
    //==============================================================================
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    
    void BuildUI();
    void RefreshInfoPanels();
    void UpdateOutputControls();
    void OnGenerateClicked();

    //--- Window handles
    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;

    //--- ROM data
    RomCombinerOpenRequest m_request;
    RomInfo m_info1; 
    RomInfo m_info2;
    
    //--- Output controls
    HWND hOutputBtn = nullptr;
    HWND hOutputPath = nullptr;
};