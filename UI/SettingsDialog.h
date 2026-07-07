#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <map>

#include "../Core/ConfigTypes.h"
#include "../Pet/PetConfig.h"

class ModuleManager;
class IFeatureModule;

class SettingsDialog {
public:
    SettingsDialog(HWND parent, ModuleManager* moduleManager);
    ~SettingsDialog();
    INT_PTR Show();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void SetupTabs(HWND hwnd);
    void OnTabChanged(HWND hwnd);
    void PopulateFields(HWND hwnd);
    void ReadFieldsFromControls();
    void RepositionFields();
    void UpdateScrollBar();
    void OnSave(HWND hwnd);
    void OnCancel(HWND hwnd);

    HWND m_hwnd;
    HWND m_parent;
    ModuleManager* m_moduleManager;
    std::vector<IFeatureModule*> m_modules;
    HWND m_hTab;
    int m_currentTab;
    int m_scrollPos;
    std::vector<std::map<std::wstring, std::wstring>> m_editedValues;
    std::map<std::wstring, std::wstring> m_petEditedValues;
    std::vector<HWND> m_fieldLabels;
    std::vector<HWND> m_fieldControls;
    std::vector<HWND> m_browseBtns;
    std::vector<size_t> m_browseBtnEditIdx;
    std::map<HWND, std::wstring> m_controlKeys;
    std::map<HWND, ConfigValueType> m_controlTypes;
    std::map<HWND, WNDPROC> m_origEditProcs;
    std::map<HWND, std::wstring> m_tooltipTexts;
    HFONT m_hFont;
    HWND m_hTooltip = nullptr;
    std::wstring m_tooltipBuf;

    static LRESULT CALLBACK EditTooltipProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static constexpr UINT WM_APP_POPULATE = WM_APP + 1;
    static constexpr int W = 520;
    static constexpr int H = 450;
    static constexpr int MARGIN = 12;
    static constexpr int TAB_H = 26;
    static constexpr int FIELD_H = 24;
    static constexpr int FIELD_SPACING = 30;
    static constexpr int LABEL_W = 160;
    static constexpr int CONTROL_W = 310;
    static constexpr int BTN_H = 26;
    static constexpr int BTN_W = 80;
    static constexpr int BROWSE_BTN_BASE = 3000;
};
