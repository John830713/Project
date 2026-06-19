//==============================================================================
// RomSeparatorWindow.h - ROM Separator dialog window
// Purpose:   Provides the UI for splitting ROM files, displaying rom info,
//            split options, and output paths.
//==============================================================================

#pragma once

#include "RomSeparatorTypes.h"

#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class RomSeparatorModule;

//==============================================================================
// RomSeparatorWindow
//==============================================================================
class RomSeparatorWindow {
public:
    //--- Constructor / Destructor ---
    RomSeparatorWindow();
    ~RomSeparatorWindow();

    //--- Public API ---
    bool Create(HINSTANCE hInstance, HWND owner, RomSeparatorModule* module);
    void Show();
    void BringToFront();

    void OpenRom(const RomSeparatorOpenRequest& request);

    HWND GetHwnd() const;

//==============================================================================
    // Private Methods
    //==============================================================================
private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void BuildMainWindow(HWND hwnd);
    void RefreshAll();
    void RefreshRomInfo();
    void RefreshSplitInfo();
    void RefreshOutputInfo();

    void OnSplitChanged();
    void OnToggleOutputMode();
    void OnGenerate();

    bool HasLoadedRom() const;
    fs::path GetCurrentOutputDir() const;
    SplitOption GetSelectedSplit() const;

//==============================================================================
    // Private Members
    //==============================================================================
private:
    HINSTANCE m_hInstance;
    HWND m_hwnd;
    HWND m_owner;
    RomSeparatorModule* m_module;

    HWND m_hRomName;
    HWND m_hRomPath;
    HWND m_hRomSize;
    HWND m_hRom1Label;
    HWND m_hRom2Label;
    HWND m_hSplitCombo;
    HWND m_hOutputBtn;
    HWND m_hOutputPath;
    HWND m_hGenerateBtn;

    fs::path m_romPath;
    RomSeparatorOutputMode m_outputMode;
    bool m_confirmOverwrite;
    bool m_showSuccessMessage;
    bool m_verboseLog;

    std::vector<SplitOption> m_splits;
};