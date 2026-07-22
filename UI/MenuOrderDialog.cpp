#include "MenuOrderDialog.h"
#include "../Core/ModuleManager.h"
#include "../Core/IFeatureModule.h"
#include "../Services/TranslationService.h"
#include "../Core/DebugConsole.h"

#include <algorithm>
#include <filesystem>

//==============================================================================
// Show (static)
//==============================================================================

void MenuOrderDialog::Show(HWND parent, ModuleManager* moduleManager) {
    MenuOrderDialog dlg(parent, moduleManager);
    if (parent) EnableWindow(parent, FALSE);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsWindow(dlg.m_hwnd)) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (parent && IsWindow(parent)) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }
}

MenuOrderDialog::MenuOrderDialog(HWND parent, ModuleManager* moduleManager)
    : m_moduleManager(moduleManager)
{
    const wchar_t kClass[] = L"MenuOrderDialogClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(0, kClass,
        TranslationService::Get()->Tr(L"Pet", L"Edit Menu Order").c_str(),
        WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 360, 400,
        parent, nullptr, GetModuleHandleW(nullptr), this);

    if (m_hwnd && parent) {
        RECT pr, cr;
        GetWindowRect(parent, &pr);
        GetWindowRect(m_hwnd, &cr);
        int w = cr.right - cr.left;
        int h = cr.bottom - cr.top;
        int x = pr.left + (pr.right - pr.left - w) / 2;
        int y = pr.top + (pr.bottom - pr.top - h) / 2;
        SetWindowPos(m_hwnd, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
        SetForegroundWindow(m_hwnd);
    }
}

LRESULT CALLBACK MenuOrderDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MenuOrderDialog* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<MenuOrderDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MenuOrderDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MenuOrderDialog::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        m_hwnd = hwnd;
        OnCreate(hwnd);
        return 0;

    case WM_COMMAND: {
        UINT id = LOWORD(wParam);
        UINT code = HIWORD(wParam);
        if (id == UP_BTN_ID) { OnUp(); return 0; }
        if (id == DOWN_BTN_ID) { OnDown(); return 0; }
        if (id == SAVE_BTN_ID) { OnSave(); return 0; }
        if (id == CANCEL_BTN_ID) { DestroyWindow(hwnd); return 0; }
        if (id == LIST_ID && code == LBN_SELCHANGE) {
            m_selectedIdx = (int)SendMessageW(m_hList, LB_GETCURSEL, 0, 0);
            EnableWindow(m_hUpBtn, m_selectedIdx > 0 ? TRUE : FALSE);
            EnableWindow(m_hDownBtn,
                m_selectedIdx < (int)m_moduleOrder.size() - 1 ? TRUE : FALSE);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void MenuOrderDialog::OnCreate(HWND hwnd) {
    auto Tr = [](const wchar_t* text) {
        return TranslationService::Get()->Tr(L"Pet", text);
    };

    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    const int MARGIN = 10;
    int listW = w - MARGIN * 2 - 50;
    int listH = h - 80;

    m_hList = CreateWindowW(L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        MARGIN, MARGIN, listW, listH,
        hwnd, (HMENU)LIST_ID, GetModuleHandleW(nullptr), nullptr);
    if (hFont) SendMessageW(m_hList, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Up/Down buttons on the right
    int btnX = MARGIN + listW + 6;
    int btnW = 40;
    int btnH = 26;

    m_hUpBtn = CreateWindowW(L"BUTTON", L"\u25B2",
        WS_CHILD | WS_VISIBLE | WS_DISABLED,
        btnX, MARGIN, btnW, btnH,
        hwnd, (HMENU)UP_BTN_ID, GetModuleHandleW(nullptr), nullptr);
    if (hFont) SendMessageW(m_hUpBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    m_hDownBtn = CreateWindowW(L"BUTTON", L"\u25BC",
        WS_CHILD | WS_VISIBLE | WS_DISABLED,
        btnX, MARGIN + btnH + 4, btnW, btnH,
        hwnd, (HMENU)DOWN_BTN_ID, GetModuleHandleW(nullptr), nullptr);
    if (hFont) SendMessageW(m_hDownBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Save/Cancel buttons at the bottom
    int btnBottomY = h - 50;
    int btnW2 = 80;

    m_hSaveBtn = CreateWindowW(L"BUTTON", Tr(L"Save").c_str(),
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        w - MARGIN - btnW2, btnBottomY, btnW2, btnH,
        hwnd, (HMENU)SAVE_BTN_ID, GetModuleHandleW(nullptr), nullptr);
    if (hFont) SendMessageW(m_hSaveBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    m_hCancelBtn = CreateWindowW(L"BUTTON", Tr(L"Cancel").c_str(),
        WS_CHILD | WS_VISIBLE,
        w - MARGIN - btnW2 * 2 - 6, btnBottomY, btnW2, btnH,
        hwnd, (HMENU)CANCEL_BTN_ID, GetModuleHandleW(nullptr), nullptr);
    if (hFont) SendMessageW(m_hCancelBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    DeleteObject(hFont);

    LoadOrder();
    RefreshList();
}

void MenuOrderDialog::RefreshList() {
    SendMessageW(m_hList, LB_RESETCONTENT, 0, 0);
    for (const auto& name : m_moduleOrder) {
        SendMessageW(m_hList, LB_ADDSTRING, 0, (LPARAM)name.c_str());
    }
    m_selectedIdx = -1;
    EnableWindow(m_hUpBtn, FALSE);
    EnableWindow(m_hDownBtn, FALSE);
}

void MenuOrderDialog::LoadOrder() {
    m_moduleOrder.clear();

    auto allModules = m_moduleManager->GetAllModules();

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path iniPath = std::filesystem::path(exePath).parent_path() / L"Config" / L"Config_Pet.ini";

    struct ModPriority {
        std::wstring name;
        int priority;
    };
    std::vector<ModPriority> entries;
    for (auto* mod : allModules) {
        wchar_t buf[16] = {};
        GetPrivateProfileStringW(L"MenuOrder", mod->GetModuleName(), L"999",
            buf, static_cast<DWORD>(std::size(buf)), iniPath.c_str());
        entries.push_back({ mod->GetModuleName(), _wtoi(buf) });
    }

    std::stable_sort(entries.begin(), entries.end(),
        [](const ModPriority& a, const ModPriority& b) {
            return a.priority < b.priority;
        });

    for (const auto& e : entries) {
        m_moduleOrder.push_back(e.name);
    }
}

void MenuOrderDialog::SaveOrder() {
    if (m_moduleOrder.empty()) return;

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path iniPath = std::filesystem::path(exePath).parent_path() / L"Config" / L"Config_Pet.ini";

    WritePrivateProfileStringW(L"MenuOrder", nullptr, nullptr, iniPath.c_str());

    for (int i = 0; i < (int)m_moduleOrder.size(); ++i) {
        wchar_t buf[16];
        swprintf(buf, 16, L"%d", (i + 1) * 100);
        WritePrivateProfileStringW(L"MenuOrder", m_moduleOrder[i].c_str(),
            buf, iniPath.c_str());
    }
}

void MenuOrderDialog::OnUp() {
    if (m_selectedIdx <= 0) return;
    if (m_selectedIdx >= (int)m_moduleOrder.size()) return;

    std::swap(m_moduleOrder[m_selectedIdx], m_moduleOrder[m_selectedIdx - 1]);
    m_selectedIdx--;
    RefreshList();
    SendMessageW(m_hList, LB_SETCURSEL, m_selectedIdx, 0);
    EnableWindow(m_hUpBtn, m_selectedIdx > 0 ? TRUE : FALSE);
    EnableWindow(m_hDownBtn, m_selectedIdx < (int)m_moduleOrder.size() - 1 ? TRUE : FALSE);
}

void MenuOrderDialog::OnDown() {
    if (m_selectedIdx < 0) return;
    if (m_selectedIdx >= (int)m_moduleOrder.size() - 1) return;

    std::swap(m_moduleOrder[m_selectedIdx], m_moduleOrder[m_selectedIdx + 1]);
    m_selectedIdx++;
    RefreshList();
    SendMessageW(m_hList, LB_SETCURSEL, m_selectedIdx, 0);
    EnableWindow(m_hUpBtn, m_selectedIdx > 0 ? TRUE : FALSE);
    EnableWindow(m_hDownBtn, m_selectedIdx < (int)m_moduleOrder.size() - 1 ? TRUE : FALSE);
}

void MenuOrderDialog::OnSave() {
    SaveOrder();
    DestroyWindow(m_hwnd);
}
