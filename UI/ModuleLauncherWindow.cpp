//==============================================================================
// ModuleLauncherWindow.cpp - Plain window carrier for feature modules
//==============================================================================

#include "ModuleLauncherWindow.h"
#include "../Core/Logger.h"
#include "../Core/ModuleManager.h"
#include "../Core/ResourceIds.h"
#include "../Services/TranslationService.h"
#include <windows.h>

namespace {
const wchar_t* const kClassName = L"ModuleLauncherWindow";
enum {
    IDC_MODULE_LIST = 4100,
    IDC_MODULE_STATUS = 4101,
    IDC_RUN = 4001,
    IDC_REFRESH = 4002,
    IDC_EXIT = 4003,
};
}

//==============================================================================
// Constructor / Destructor
//==============================================================================

ModuleLauncherWindow::ModuleLauncherWindow(HINSTANCE hInstance, ModuleManager* modules)
    : m_hInstance(hInstance), m_modules(modules) {}

ModuleLauncherWindow::~ModuleLauncherWindow() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

//==============================================================================
// Tr - translated launcher chrome
//==============================================================================

std::wstring ModuleLauncherWindow::Tr(const wchar_t* key) {
    return TranslationService::Get()->Tr(L"Launcher", key);
}

//==============================================================================
// Create
//==============================================================================

bool ModuleLauncherWindow::Create() {
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = m_hInstance;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
        classRegistered = RegisterClassW(&wc) != 0;
        if (!classRegistered) {
            Logger::Write(L"Error", L"ModuleLauncher: failed to register window class.");
            return false;
        }
    }

    m_hwnd = CreateWindowExW(0, kClassName, Tr(L"Module Launcher").c_str(),
                             WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, 620, 520,
                             nullptr, nullptr, m_hInstance, this);
    return m_hwnd != nullptr;
}

void ModuleLauncherWindow::Show(int nCmdShow) {
    if (!m_hwnd) {
        return;
    }
    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    SetForegroundWindow(m_hwnd);
}

//==============================================================================
// BuildControls
//==============================================================================

void ModuleLauncherWindow::BuildControls() {
    m_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
                                 LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                             0, 0, 0, 0, m_hwnd, (HMENU)IDC_MODULE_LIST, m_hInstance, nullptr);

    m_status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                               0, 0, 0, 0, m_hwnd, (HMENU)IDC_MODULE_STATUS, m_hInstance, nullptr);

    m_runButton = CreateWindowExW(0, L"BUTTON", Tr(L"Run").c_str(),
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                  0, 0, 0, 0, m_hwnd, (HMENU)IDC_RUN, m_hInstance, nullptr);

    m_refreshButton = CreateWindowExW(0, L"BUTTON", Tr(L"Refresh").c_str(),
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                      0, 0, 0, 0, m_hwnd, (HMENU)IDC_REFRESH, m_hInstance, nullptr);

    m_exitButton = CreateWindowExW(0, L"BUTTON", Tr(L"Exit").c_str(),
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                   0, 0, 0, 0, m_hwnd, (HMENU)IDC_EXIT, m_hInstance, nullptr);

    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    for (HWND h : {m_list, m_status, m_runButton, m_refreshButton, m_exitButton}) {
        SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
    }

    Layout();
    RefreshList();
}

//==============================================================================
// Layout
//==============================================================================

void ModuleLauncherWindow::Layout() {
    if (!m_hwnd) {
        return;
    }
    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    const int margin = 12;
    const int buttonHeight = 30;
    const int statusHeight = 20;
    const int width = rc.right - margin * 2;
    const int bottom = rc.bottom - margin;

    int buttonY = bottom - buttonHeight;
    int listHeight = buttonY - margin - statusHeight - 8 - margin;
    if (listHeight < 60) {
        listHeight = 60;
    }

    MoveWindow(m_list, margin, margin, width, listHeight, TRUE);
    MoveWindow(m_status, margin, margin + listHeight + 8, width, statusHeight, TRUE);

    const int buttonWidth = 96;
    MoveWindow(m_runButton, margin, buttonY, buttonWidth, buttonHeight, TRUE);
    MoveWindow(m_refreshButton, margin + buttonWidth + 8, buttonY, buttonWidth, buttonHeight, TRUE);
    MoveWindow(m_exitButton, rc.right - margin - buttonWidth, buttonY, buttonWidth, buttonHeight, TRUE);
}

//==============================================================================
// RefreshList
//==============================================================================

void ModuleLauncherWindow::RefreshList() {
    if (!m_list || !m_modules) {
        return;
    }

    SendMessageW(m_list, LB_RESETCONTENT, 0, 0);
    m_rowActionIds.clear();

    const auto groups = m_modules->GetMenuGroups();
    size_t actionCount = 0;

    for (const auto& group : groups) {
        std::wstring groupLabel = TranslationService::Get()->TrModuleLabel(group.displayName);
        for (const auto& item : group.items) {
            std::wstring row = groupLabel + L"  \x25B8  " + item.label;
            SendMessageW(m_list, LB_ADDSTRING, 0, (LPARAM)row.c_str());
            // Disabled items are still listed (they explain themselves), but
            // the row resolves to -1 so RunSelected ignores it.
            m_rowActionIds.push_back((item.flags & kContextMenuItemDisabled) ? -1 : item.itemId);
            ++actionCount;
        }
    }

    std::wstring status = std::to_wstring(groups.size()) + L" " + Tr(L"module(s)") + L", " +
                          std::to_wstring(actionCount) + L" " + Tr(L"action(s)");
    SetWindowTextW(m_status, status.c_str());

    if (!m_rowActionIds.empty()) {
        SendMessageW(m_list, LB_SETCURSEL, 0, 0);
    }
}

//==============================================================================
// RunSelected
//==============================================================================

void ModuleLauncherWindow::RunSelected() {
    if (!m_list || !m_modules) {
        return;
    }
    int index = (int)SendMessageW(m_list, LB_GETCURSEL, 0, 0);
    if (index < 0 || index >= (int)m_rowActionIds.size()) {
        return;
    }
    int actionId = m_rowActionIds[(size_t)index];
    if (actionId < 0) {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    // Module actions may flip their own Enabled flag (context menu toggles),
    // so re-read the rows afterwards to keep the list honest.
    m_modules->ExecuteContextMenuItem(actionId);
    RefreshList();
}

//==============================================================================
// WndProc
//==============================================================================

LRESULT CALLBACK ModuleLauncherWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ModuleLauncherWindow* self =
        (msg == WM_NCCREATE)
            ? (ModuleLauncherWindow*)((CREATESTRUCTW*)lParam)->lpCreateParams
            : (ModuleLauncherWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    }
    if (!self) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (msg == WM_CREATE) {
        self->m_hwnd = hwnd;
    }
    return self->HandleMessage(hwnd, msg, wParam, lParam);
}

LRESULT ModuleLauncherWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            BuildControls();
            return 0;

        case WM_SIZE:
            Layout();
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            mmi->ptMinTrackSize.x = 420;
            mmi->ptMinTrackSize.y = 300;
            return 0;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            if (id == IDC_RUN || (id == IDC_MODULE_LIST && code == LBN_DBLCLK)) {
                RunSelected();
                return 0;
            }
            if (id == IDC_REFRESH) {
                RefreshList();
                return 0;
            }
            if (id == IDC_EXIT) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            Logger::Write(L"Main", L"Module launcher closed; exiting.");
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
