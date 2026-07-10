#include "SettingsDialog.h"
#include "../Core/ModuleManager.h"
#include "../Core/IFeatureModule.h"
#include "../Core/Logger.h"
#include "../Core/DebugConsole.h"
#include "../Services/TranslationService.h"

#include <shobjidl.h>
#include <windowsx.h>
#include <algorithm>

namespace {
    constexpr int W = 520;
    constexpr int H = 450;
    constexpr int MARGIN = 12;
    constexpr int TAB_H = 26;
    constexpr int FIELD_START_Y = 52;
    constexpr int FIELD_H = 24;
    constexpr int FIELD_SPACING = 30;
    constexpr int LABEL_W = 160;
    constexpr int CONTROL_MARGIN = 6;
    constexpr int BTN_H = 26;
    constexpr int BTN_W = 80;
    constexpr int BTN_BOTTOM = 10;
}

SettingsDialog::SettingsDialog(HWND parent, ModuleManager* moduleManager)
    : m_hwnd(nullptr), m_parent(parent), m_moduleManager(moduleManager),
      m_hTab(nullptr), m_currentTab(-1), m_scrollPos(0), m_hFont(nullptr) {
    m_modules = m_moduleManager->GetAllModules();
    m_editedValues.resize(m_modules.size());
    for (size_t i = 0; i < m_modules.size(); ++i) {
        auto* mod = m_modules[i];
        for (const auto& def : mod->GetConfigDefinitions()) {
            m_editedValues[i][def.key] = mod->GetValue(def.key);
        }
    }

    auto petData = PetConfig::Load();
    m_petEditedValues[L"posX"] = std::to_wstring(petData.posX);
    m_petEditedValues[L"posY"] = std::to_wstring(petData.posY);
    m_petEditedValues[L"opacity"] = std::to_wstring(petData.opacity);
    m_petEditedValues[L"scalePercent"] = std::to_wstring(petData.scalePercent);
    m_petEditedValues[L"alwaysOnTop"] = petData.alwaysOnTop ? L"1" : L"0";
    m_petEditedValues[L"moveEnabled"] = petData.moveEnabled ? L"1" : L"0";
    m_petEditedValues[L"moveStep"] = std::to_wstring(petData.moveStep);
    m_petEditedValues[L"moveSpeed"] = std::to_wstring(petData.moveSpeed);
    m_petEditedValues[L"moveShuttle"] = petData.moveShuttle ? L"1" : L"0";
    m_petEditedValues[L"language"] = petData.language;
    m_origOpacity = petData.opacity;
    m_origScale = petData.scalePercent;
}

SettingsDialog::~SettingsDialog() {
    if (m_hFont) DeleteObject(m_hFont);
}

INT_PTR SettingsDialog::Show() {
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_TAB_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icex);

    const wchar_t kClass[] = L"SettingsDialogClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = SettingsDialog::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
    RegisterClassW(&wc);

    int cx = GetSystemMetrics(SM_CXSCREEN);
    int cy = GetSystemMetrics(SM_CYSCREEN);
    int x = (cx - W) / 2;
    int y = (cy - H) / 2;

    EnableWindow(m_parent, FALSE);

    m_hwnd = CreateWindowExW(0, kClass,
        TranslationService::Get()->Tr(L"Settings", L"Settings").c_str(),
        WS_CAPTION | WS_POPUPWINDOW | WS_VSCROLL | WS_VISIBLE,
        x, y, W, H, m_parent, nullptr, GetModuleHandleW(nullptr), this);

    if (!m_hwnd) {
        EnableWindow(m_parent, TRUE);
        SetForegroundWindow(m_parent);
        return -1;
    }

    SetForegroundWindow(m_hwnd);
    SetFocus(m_hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsWindow(m_hwnd)) break;
        if (msg.message == WM_KEYDOWN && msg.wParam == 'A' &&
            (GetKeyState(VK_CONTROL) & 0x8000)) {
            HWND hFocus = GetFocus();
            if (hFocus) {
                wchar_t cls[16] = {};
                GetClassNameW(hFocus, cls, 16);
                if (wcscmp(cls, L"Edit") == 0) {
                    SendMessageW(hFocus, EM_SETSEL, 0, -1);
                    continue;
                }
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (IsWindow(m_parent)) {
        EnableWindow(m_parent, TRUE);
        SetForegroundWindow(m_parent);
    }
    return 0;
}

LRESULT CALLBACK SettingsDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsDialog* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<SettingsDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsDialog::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        DBG(L"SettingsDialog WM_CREATE");
        m_hwnd = hwnd;
        NONCLIENTMETRICSW ncm = { sizeof(ncm) };
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        m_hFont = CreateFontIndirectW(&ncm.lfMessageFont);

        SetupTabs(hwnd);

        PopulateFields(hwnd);

        CreateWindowW(L"BUTTON",
            TranslationService::Get()->Tr(L"Settings", L"Save").c_str(),
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            W - BTN_W * 2 - MARGIN * 2, H - BTN_BOTTOM - BTN_H - 40,
            BTN_W, BTN_H,
            hwnd, (HMENU)1, GetModuleHandleW(nullptr), nullptr);

        CreateWindowW(L"BUTTON",
            TranslationService::Get()->Tr(L"Settings", L"Cancel").c_str(),
            WS_CHILD | WS_VISIBLE,
            W - BTN_W - MARGIN, H - BTN_BOTTOM - BTN_H - 40,
            BTN_W, BTN_H,
            hwnd, (HMENU)2, GetModuleHandleW(nullptr), nullptr);
        return 0;
    }

    case WM_SIZE: {
        UpdateScrollBar();
        RepositionFields();
        return 0;
    }

    case WM_HSCROLL: {
        HWND hBar = (HWND)lParam;
        auto it = m_sliderValueLabels.find(hBar);
        if (it != m_sliderValueLabels.end()) {
            int pos = (int)SendMessageW(hBar, TBM_GETPOS, 0, 0);
            wchar_t buf[8];
            swprintf(buf, 8, L"%d", pos);
            SetWindowTextW(it->second, buf);
            auto kit = m_controlKeys.find(hBar);
            DBG(L"Settings WM_HSCROLL: bar=%p pos=%d key=%s",
                (void*)hBar, pos,
                (kit != m_controlKeys.end()) ? kit->second.c_str() : L"?");
            if (kit != m_controlKeys.end() && m_currentTab == 0) {
                m_petEditedValues[kit->second] = buf;
                if (kit->second == L"opacity") {
                    PostMessageW(m_parent, WM_APP + 2, 110, pos);
                } else if (kit->second == L"scalePercent") {
                    PostMessageW(m_parent, WM_APP + 2, 111, pos);
                }
            }
        }
        return 0;
    }

    case WM_VSCROLL: {
        int maxScroll = 0;
        SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
        if (GetScrollInfo(hwnd, SB_VERT, &si)) {
            maxScroll = si.nMax - static_cast<int>(si.nPage);
        }
        if (maxScroll <= 0) return 0;

        switch (LOWORD(wParam)) {
        case SB_LINEUP:    m_scrollPos = std::max(0, m_scrollPos - FIELD_SPACING); break;
        case SB_LINEDOWN:  m_scrollPos = std::min(maxScroll, m_scrollPos + FIELD_SPACING); break;
        case SB_PAGEUP:    m_scrollPos = std::max(0, m_scrollPos - (maxScroll / 4)); break;
        case SB_PAGEDOWN:  m_scrollPos = std::min(maxScroll, m_scrollPos + (maxScroll / 4)); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: m_scrollPos = HIWORD(wParam); break;
        case SB_TOP:       m_scrollPos = 0; break;
        case SB_BOTTOM:    m_scrollPos = maxScroll; break;
        }
        m_scrollPos = std::max(0, std::min(maxScroll, m_scrollPos));
        UpdateScrollBar();
        RepositionFields();
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int scrollLines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &scrollLines, 0);
        int steps = (delta / WHEEL_DELTA) * scrollLines;
        m_scrollPos = std::max(0, m_scrollPos - steps * (FIELD_SPACING / 2));
        UpdateScrollBar();
        RepositionFields();
        return 0;
    }

    case WM_NOTIFY: {
        auto* nmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (nmhdr->hwndFrom == m_hTab && nmhdr->code == TCN_SELCHANGE) {
            OnTabChanged(hwnd);
            return 0;
        }
        if (m_hTooltip && nmhdr->hwndFrom == m_hTooltip && nmhdr->code == TTN_GETDISPINFO) {
            auto* dispInfo = reinterpret_cast<NMTTDISPINFOW*>(lParam);
            HWND hEdit = (HWND)dispInfo->hdr.idFrom;
            auto kit = m_controlKeys.find(hEdit);
            if (kit != m_controlKeys.end()) {
                int len = GetWindowTextLengthW(hEdit);
                if (len > 0) {
                    m_tooltipBuf.resize(len);
                    GetWindowTextW(hEdit, &m_tooltipBuf[0], len + 1);
                    dispInfo->lpszText = &m_tooltipBuf[0];
                }
            }
            return 0;
        }
        break;
    }

    case WM_COMMAND: {
        if (HIWORD(wParam) == EN_KILLFOCUS) {
            HWND hEdit = (HWND)lParam;
            auto eit = m_editToSlider.find(hEdit);
            if (eit != m_editToSlider.end()) {
                HWND hBar = eit->second;
                int len = GetWindowTextLengthW(hEdit);
                std::wstring txt;
                if (len > 0) { txt.resize(len); GetWindowTextW(hEdit, &txt[0], len + 1); }
                int val = (len > 0) ? _wtoi(txt.c_str()) : 1;
                auto kit = m_controlKeys.find(hBar);
                if (kit != m_controlKeys.end()) {
                    if (kit->second == L"opacity") {
                        if (val < 1) val = 1;
                        if (val > 255) val = 255;
                    } else if (kit->second == L"scalePercent") {
                        if (val < 25) val = 25;
                        if (val > 250) val = 250;
                    }
                }
                wchar_t buf[8];
                swprintf(buf, 8, L"%d", val);
                SetWindowTextW(hEdit, buf);
                SendMessageW(hBar, TBM_SETPOS, TRUE, val);
                DBG(L"Settings EN_KILLFOCUS: edit=%p slider=%p key=%s val=%d",
                    (void*)hEdit, (void*)hBar,
                    (kit != m_controlKeys.end()) ? kit->second.c_str() : L"?", val);
                if (kit != m_controlKeys.end() && m_currentTab == 0) {
                    m_petEditedValues[kit->second] = buf;
                    if (kit->second == L"opacity")
                        PostMessageW(m_parent, WM_APP + 2, 110, val);
                    else if (kit->second == L"scalePercent")
                        PostMessageW(m_parent, WM_APP + 2, 111, val);
                }
            }
        }
        UINT id = LOWORD(wParam);
        if (id == 1) { OnSave(hwnd); return 0; }
        if (id == 2) { OnCancel(hwnd); return 0; }
        if (id == 1000) {
            DBG(L"SettingsDialog WM_COMMAND 1000 (Manage)");
            ReadFieldsFromControls();
            if (m_currentTab > 0) {
                DBG(L"  currentTab=%d module=%s",
                    m_currentTab, m_modules[m_currentTab - 1]->GetModuleName());
                m_modules[m_currentTab - 1]->OpenCustomSettings(hwnd);
                DBG(L"  OpenCustomSettings returned, fieldControls=%zu", m_fieldControls.size());
                for (size_t _i = 0; _i < m_fieldControls.size(); ++_i) {
                    DBG(L"    ctrl[%zu] hwnd=%p visible=%d",
                        _i, (void*)m_fieldControls[_i],
                        IsWindowVisible(m_fieldControls[_i]) ? 1 : 0);
                }
            }
            return 0;
        }
        if (id >= BROWSE_BTN_BASE) {
            size_t fieldIdx = id - BROWSE_BTN_BASE;
            std::vector<ConfigFieldDefinition> defs;
            if (m_currentTab == 0)
                defs = PetConfig::GetDefinitions();
            else
                defs = m_modules[m_currentTab - 1]->GetConfigDefinitions();
            if (fieldIdx >= defs.size()) return 0;
            if (defs[fieldIdx].type != ConfigValueType::Directory) return 0;
            wchar_t currentPath[MAX_PATH] = {};
            GetWindowTextW(m_fieldControls[fieldIdx], currentPath, MAX_PATH);
            IFileOpenDialog* pDlg = nullptr;
            HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg));
            if (SUCCEEDED(hr) && pDlg) {
                DWORD flags;
                pDlg->GetOptions(&flags);
                pDlg->SetOptions(flags | FOS_PICKFOLDERS);
                if (currentPath[0]) {
                    IShellItem* pFolder = nullptr;
                    hr = SHCreateItemFromParsingName(currentPath, nullptr,
                                                      IID_PPV_ARGS(&pFolder));
                    if (SUCCEEDED(hr) && pFolder) {
                        pDlg->SetFolder(pFolder);
                        pFolder->Release();
                    }
                }
                hr = pDlg->Show(hwnd);
                if (SUCCEEDED(hr)) {
                    IShellItem* pItem = nullptr;
                    hr = pDlg->GetResult(&pItem);
                    if (SUCCEEDED(hr) && pItem) {
                        PWSTR pszPath = nullptr;
                        hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
                        if (SUCCEEDED(hr) && pszPath) {
                            SetWindowTextW(m_fieldControls[fieldIdx], pszPath);
                            if (m_hTooltip) {
                                HWND hEdit = m_fieldControls[fieldIdx];
                                m_tooltipTexts[hEdit] = pszPath;
                                TOOLINFOW ti = {};
                                ti.cbSize = sizeof(TOOLINFOW) - sizeof(void*);
                                ti.hwnd = hEdit;
                                ti.uId = (UINT_PTR)hEdit;
                                ti.lpszText = const_cast<wchar_t*>(
                                    m_tooltipTexts[hEdit].c_str());
                                SendMessageW(m_hTooltip, TTM_UPDATETIPTEXT,
                                             0, (LPARAM)&ti);
                            }
                            CoTaskMemFree(pszPath);
                        }
                        pItem->Release();
                    }
                }
                pDlg->Release();
            }
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        OnCancel(hwnd);
        return 0;

    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK SettingsDialog::EditTooltipProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsDialog* self = reinterpret_cast<SettingsDialog*>(
        GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hWnd, msg, wParam, lParam);

    auto it = self->m_origEditProcs.find(hWnd);
    WNDPROC origProc = (it != self->m_origEditProcs.end()) ? it->second : nullptr;

    DBG(L"EditTP: msg=0x%x hWnd=%p", msg, (void*)hWnd);
    switch (msg) {
    case WM_MOUSEMOVE: {
        if (!self->m_hTooltip) { DBG(L"EditTP: no tooltip"); break; }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hWnd, &pt);
        TOOLINFOW ti = {};
        ti.cbSize = sizeof(TOOLINFOW) - sizeof(void*);
        ti.hwnd = hWnd;
        ti.uId = (UINT_PTR)hWnd;
        SendMessageW(self->m_hTooltip, TTM_TRACKPOSITION, 0,
                     MAKELPARAM(pt.x, pt.y + 20));
        BOOL act = (BOOL)SendMessageW(self->m_hTooltip, TTM_TRACKACTIVATE,
                                       TRUE, (LPARAM)&ti);
        DBG(L"EditTP: MOUSEMOVE pt=%d,%d act=%d", pt.x, pt.y, act);
        break;
    }
    case WM_MOUSELEAVE: {
        if (!self->m_hTooltip) break;
        POINT pt;
        GetCursorPos(&pt);
        RECT rc;
        GetWindowRect(hWnd, &rc);
        DBG(L"EditTP: MOUSELEAVE pt=%d,%d rc=%d,%d-%d,%d",
            pt.x, pt.y, rc.left, rc.top, rc.right, rc.bottom);
        if (PtInRect(&rc, pt)) { DBG(L"EditTP:  still inside"); break; }
        TOOLINFOW ti = {};
        ti.cbSize = sizeof(TOOLINFOW) - sizeof(void*);
        ti.hwnd = hWnd;
        ti.uId = (UINT_PTR)hWnd;
        SendMessageW(self->m_hTooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
        DBG(L"EditTP:  deactivated");
        break;
    }
    }

    return origProc ? CallWindowProcW(origProc, hWnd, msg, wParam, lParam)
                    : DefWindowProcW(hWnd, msg, wParam, lParam);
}

void SettingsDialog::SetupTabs(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    m_hTab = CreateWindowW(WC_TABCONTROL, L"",
        WS_CHILD | WS_VISIBLE | TCS_FIXEDWIDTH,
        MARGIN, MARGIN, rc.right - MARGIN * 2, TAB_H,
        hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!m_hTab) return;

    if (m_hFont)
        SendMessageW(m_hTab, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    TCITEMW tie = {};
    tie.mask = TCIF_TEXT;

    std::wstring petLabel = TranslationService::Get()->Tr(L"Settings", L"Pet");
    tie.pszText = const_cast<wchar_t*>(petLabel.c_str());
    SendMessageW(m_hTab, TCM_INSERTITEM, 0, (LPARAM)&tie);

    for (size_t i = 0; i < m_modules.size(); ++i) {
        std::wstring modLabel = TranslationService::Get()->Tr(
            L"TabLabels", m_modules[i]->GetDisplayName());
        tie.pszText = const_cast<wchar_t*>(modLabel.c_str());
        SendMessageW(m_hTab, TCM_INSERTITEM, (WPARAM)(i + 1), (LPARAM)&tie);
    }

    m_currentTab = 0;
    TabCtrl_SetCurSel(m_hTab, 0);
}

void SettingsDialog::OnTabChanged(HWND hwnd) {
    DBG(L"OnTabChanged: old tab=%d", m_currentTab);
    ReadFieldsFromControls();

    for (auto h : m_fieldLabels) if (IsWindow(h)) DestroyWindow(h);
    for (auto h : m_fieldControls) if (IsWindow(h)) DestroyWindow(h);
    for (auto h : m_browseBtns) if (IsWindow(h)) DestroyWindow(h);
    for (auto& kv : m_sliderValueLabels) if (IsWindow(kv.second)) DestroyWindow(kv.second);
    if (m_hTooltip) { DestroyWindow(m_hTooltip); m_hTooltip = nullptr; }
    m_fieldLabels.clear();
    m_fieldControls.clear();
    m_browseBtns.clear();
    m_browseBtnEditIdx.clear();
    m_controlKeys.clear();
    m_controlTypes.clear();
    m_origEditProcs.clear();
    m_tooltipTexts.clear();
    m_sliderValueLabels.clear();
    m_editToSlider.clear();
    m_scrollPos = 0;

    int sel = TabCtrl_GetCurSel(m_hTab);
    int tabCount = 1 + (int)m_modules.size();
    DBG(L"  new sel=%d tabCount=%d", sel, tabCount);
    if (sel >= 0 && sel < tabCount) {
        m_currentTab = sel;
        PopulateFields(hwnd);
    }
}

void SettingsDialog::PopulateFields(HWND hwnd) {
    int tabCount = 1 + (int)m_modules.size();
    DBG(L"PopulateFields: currentTab=%d tabCount=%d", m_currentTab, tabCount);
    if (m_currentTab < 0 || m_currentTab >= tabCount) {
        DBG(L"  EARLY RETURN (invalid tab)");
        return;
    }

    std::vector<ConfigFieldDefinition> defs;
    std::map<std::wstring, std::wstring>* values = nullptr;

    if (m_currentTab == 0) {
        defs = PetConfig::GetDefinitions();
        values = &m_petEditedValues;
    } else {
        auto* mod = m_modules[m_currentTab - 1];
        defs = mod->GetConfigDefinitions();
        values = &m_editedValues[m_currentTab - 1];
    }

    RECT rc;
    GetClientRect(hwnd, &rc);

    for (size_t i = 0; i < defs.size(); ++i) {
        const auto& def = defs[i];

        std::wstring valText;
        auto it = values->find(def.key);
        if (it != values->end()) valText = it->second;
        else valText = def.defaultValue;

        auto Tr = [](const wchar_t* section, const wchar_t* text) {
            return TranslationService::Get()->Tr(section, text);
        };

        int baseY = FIELD_START_Y;

        // For Bool fields, hide the label; for others, show it
        bool isBool = (def.type == ConfigValueType::Bool);
        std::wstring fieldLabel = Tr(L"Common", def.label.c_str());
        HWND hLabel = CreateWindowW(L"STATIC", isBool ? L"" : fieldLabel.c_str(),
            WS_CHILD | WS_VISIBLE,
            MARGIN, baseY + (int)i * FIELD_SPACING, LABEL_W, FIELD_H,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (m_hFont) SendMessageW(hLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        if (isBool) ShowWindow(hLabel, SW_HIDE);
        m_fieldLabels.push_back(hLabel);

        HWND hCtrl = nullptr;
        switch (def.type) {
        case ConfigValueType::Bool: {
            hCtrl = CreateWindowW(L"BUTTON", fieldLabel.c_str(),
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                MARGIN, baseY + (int)i * FIELD_SPACING, rc.right - MARGIN * 2, FIELD_H,
                hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hCtrl, BM_SETCHECK, (valText == L"1") ? BST_CHECKED : BST_UNCHECKED, 0);
            if (m_hFont) SendMessageW(hCtrl, WM_SETFONT, (WPARAM)m_hFont, TRUE);
            break;
        }
        case ConfigValueType::Directory: {
            int btnW = 64;
            int ctrlX = MARGIN + LABEL_W + CONTROL_MARGIN;
            int ctrlW = rc.right - ctrlX - MARGIN - btnW - 4 - 30; // 30px breathing room after button
            if (ctrlW < 50) ctrlW = 50;

            hCtrl = CreateWindowW(L"EDIT", valText.c_str(),
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                ctrlX, baseY + (int)i * FIELD_SPACING, ctrlW, FIELD_H,
                hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (m_hFont) SendMessageW(hCtrl, WM_SETFONT, (WPARAM)m_hFont, TRUE);

            HWND hBtn = CreateWindowW(L"BUTTON", L"Browse...",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                ctrlX + ctrlW + 4, baseY + (int)i * FIELD_SPACING, btnW, FIELD_H,
                hwnd, (HMENU)(BROWSE_BTN_BASE + i), GetModuleHandleW(nullptr), nullptr);
            if (m_hFont) SendMessageW(hBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);
            m_browseBtns.push_back(hBtn);
            m_browseBtnEditIdx.push_back(i);

            if (!m_hTooltip) {
                m_hTooltip = CreateWindowExW(0, TOOLTIPS_CLASS, nullptr,
                    WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                    hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
                DBG(L"Settings: tooltip hwnd=%p error=%d",
                    (void*)m_hTooltip, (int)GetLastError());
                if (m_hTooltip) {
                    SendMessageW(m_hTooltip, TTM_SETMAXTIPWIDTH, 0, 2000);
                }
            }
            if (m_hTooltip && hCtrl) {
                m_tooltipTexts[hCtrl] = valText;
                TOOLINFOW ti = {};
                ti.cbSize = sizeof(TOOLINFOW) - sizeof(void*);
                ti.uFlags = TTF_TRACK;
                ti.hwnd = hCtrl;
                ti.uId = (UINT_PTR)hCtrl;
                ti.lpszText = const_cast<wchar_t*>(m_tooltipTexts[hCtrl].c_str());
                BOOL ok = (BOOL)SendMessageW(m_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
                DBG(L"Settings: TTM_ADDTOOL edit=%p hwnd=%p ok=%d text='%s'",
                    (void*)hCtrl, (void*)hwnd, ok, ti.lpszText);

                m_origEditProcs[hCtrl] = (WNDPROC)SetWindowLongPtrW(hCtrl,
                    GWLP_WNDPROC, (LONG_PTR)SettingsDialog::EditTooltipProc);
                SetWindowLongPtrW(hCtrl, GWLP_USERDATA, (LONG_PTR)this);
            }
            break;
        }
        case ConfigValueType::Int:
        case ConfigValueType::String:
        default: {
            int ctrlX = MARGIN + LABEL_W + CONTROL_MARGIN;
            int ctrlW = rc.right - ctrlX - MARGIN;
            if (ctrlW < 50) ctrlW = 50;

            if (m_currentTab == 0 && def.key == L"language") {
                hCtrl = CreateWindowW(WC_COMBOBOX, L"",
                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                    ctrlX, baseY + (int)i * FIELD_SPACING, ctrlW, 200,
                    hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
                if (m_hFont) SendMessageW(hCtrl, WM_SETFONT, (WPARAM)m_hFont, TRUE);
                auto langs = TranslationService::Get()->GetAvailableLanguages();
                int selIdx = 0;
                for (size_t li = 0; li < langs.size(); ++li) {
                    SendMessageW(hCtrl, CB_ADDSTRING, 0, (LPARAM)langs[li].c_str());
                    if (langs[li] == valText) selIdx = (int)li;
                }
                SendMessageW(hCtrl, CB_SETCURSEL, selIdx, 0);
            } else if (m_currentTab == 0 && (def.key == L"opacity" || def.key == L"scalePercent")) {
                int val = std::stoi(valText);
                int trbW = ctrlW - 50;
                int minVal = (def.key == L"scalePercent") ? 25 : 1;
                int maxVal = (def.key == L"scalePercent") ? 250 : 255;
                hCtrl = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
                    WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | TBS_FIXEDLENGTH,
                    ctrlX, baseY + (int)i * FIELD_SPACING + 2, trbW, 22,
                    hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
                SendMessageW(hCtrl, TBM_SETRANGE, TRUE, MAKELPARAM(minVal, maxVal));
                SendMessageW(hCtrl, TBM_SETPOS, TRUE, val);
                wchar_t valBuf[8];
                swprintf(valBuf, 8, L"%d", val);
                DWORD valStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER;
                HWND hVal = CreateWindowW(L"EDIT", valBuf, valStyle,
                    ctrlX + trbW + 4, baseY + (int)i * FIELD_SPACING + 3, 40, 20,
                    hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
                if (m_hFont) SendMessageW(hVal, WM_SETFONT, (WPARAM)m_hFont, TRUE);
                m_sliderValueLabels[hCtrl] = hVal;
                m_editToSlider[hVal] = hCtrl;
                DBG(L"Settings: %s trackbar=%p valLabel=%p(EDIT) x=%d y=%d trbW=%d",
                    def.key.c_str(), (void*)hCtrl, (void*)hVal,
                    ctrlX + trbW + 4, baseY + (int)i * FIELD_SPACING + 3, trbW);
            } else {
                hCtrl = CreateWindowW(L"EDIT", valText.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                    ctrlX, baseY + (int)i * FIELD_SPACING, ctrlW, FIELD_H,
                    hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
                if (m_hFont) SendMessageW(hCtrl, WM_SETFONT, (WPARAM)m_hFont, TRUE);
            }
            break;
        }
        }

        if (hCtrl) {
            m_fieldControls.push_back(hCtrl);
            m_controlKeys[hCtrl] = def.key;
            m_controlTypes[hCtrl] = def.type;
        }
    }

    // Custom controls for modules with custom settings UI
    if (m_currentTab > 0) {
        auto* mod = m_modules[m_currentTab - 1];
        if (mod->HasCustomSettings()) {
            int btnY = FIELD_START_Y + static_cast<int>(defs.size()) * FIELD_SPACING + 10;
            HWND hBtn = CreateWindowW(L"BUTTON",
                TranslationService::Get()->Tr(L"Settings", L"Manage Actions...").c_str(),
                WS_CHILD | WS_VISIBLE,
                MARGIN, btnY, 160, 28,
                hwnd, (HMENU)1000, GetModuleHandleW(nullptr), nullptr);
            DBG(L"PopulateFields: created Manage btn hwnd=%p at y=%d", (void*)hBtn, btnY);
            if (m_hFont && hBtn) SendMessageW(hBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);
            if (hBtn) {
                m_fieldControls.push_back(hBtn);
                m_controlKeys[hBtn] = L"__MANAGE_ACTIONS__";
                m_controlTypes[hBtn] = ConfigValueType::Bool;
            } else {
                DBG(L"  FAILED to create button!");
            }
        }
    }

    m_scrollPos = 0;
    UpdateScrollBar();
    RepositionFields();
}

void SettingsDialog::ReadFieldsFromControls() {
    int tabCount = 1 + (int)m_modules.size();
    if (m_currentTab < 0 || m_currentTab >= tabCount) return;

    std::map<std::wstring, std::wstring>* target = nullptr;
    if (m_currentTab == 0) {
        target = &m_petEditedValues;
    } else {
        target = &m_editedValues[m_currentTab - 1];
    }

    for (auto hCtrl : m_fieldControls) {
        if (!IsWindow(hCtrl)) continue;

        auto kit = m_controlKeys.find(hCtrl);
        if (kit == m_controlKeys.end()) continue;

        auto tit = m_controlTypes.find(hCtrl);
        ConfigValueType type = (tit != m_controlTypes.end()) ? tit->second : ConfigValueType::String;

        std::wstring val;
        if (type == ConfigValueType::Bool) {
            val = (SendMessageW(hCtrl, BM_GETCHECK, 0, 0) == BST_CHECKED) ? L"1" : L"0";
        } else {
            wchar_t cls[16] = {};
            GetClassNameW(hCtrl, cls, 16);
            if (wcscmp(cls, L"ComboBox") == 0) {
                int sel = (int)SendMessageW(hCtrl, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    int len = (int)SendMessageW(hCtrl, CB_GETLBTEXTLEN, sel, 0);
                    val.resize((size_t)len);
                    SendMessageW(hCtrl, CB_GETLBTEXT, sel, (LPARAM)&val[0]);
                }
            } else {
                wchar_t cls[32] = {};
                GetClassNameW(hCtrl, cls, 32);
                if (wcscmp(cls, L"msctls_trackbar32") == 0) {
                    int pos = (int)SendMessageW(hCtrl, TBM_GETPOS, 0, 0);
                    val = std::to_wstring(pos);
                } else {
                    int len = GetWindowTextLengthW(hCtrl);
                    val.resize((size_t)len);
                    GetWindowTextW(hCtrl, &val[0], len + 1);
                }
            }
        }

        (*target)[kit->second] = val;
    }
}

void SettingsDialog::RepositionFields() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);

    size_t total = m_fieldLabels.size();
    if (m_fieldControls.size() > total) total = m_fieldControls.size();

    int baseY = FIELD_START_Y - m_scrollPos;
    DBG(L"RepositionFields: fieldCtrls=%zu fieldLabels=%zu scroll=%d baseY=%d",
        m_fieldControls.size(), m_fieldLabels.size(), m_scrollPos, baseY);

    for (size_t i = 0; i < m_fieldLabels.size(); ++i) {
        HWND h = m_fieldLabels[i];
        if (IsWindow(h)) {
            SetWindowPos(h, nullptr, MARGIN, baseY + (int)i * FIELD_SPACING,
                LABEL_W, FIELD_H, SWP_NOZORDER | SWP_NOSIZE);
        }
    }

    for (size_t i = 0; i < m_fieldControls.size(); ++i) {
        HWND h = m_fieldControls[i];
        if (!IsWindow(h)) continue;

        auto tit = m_controlTypes.find(h);
        ConfigValueType type = (tit != m_controlTypes.end()) ? tit->second : ConfigValueType::String;

        wchar_t cls[32] = {};
        GetClassNameW(h, cls, 32);
        bool isCombo = (wcscmp(cls, L"ComboBox") == 0);
        bool isTrackbar = (wcscmp(cls, L"msctls_trackbar32") == 0);

        if (type == ConfigValueType::Bool) {
            SetWindowPos(h, nullptr, MARGIN, baseY + (int)i * FIELD_SPACING,
                rc.right - MARGIN * 2, FIELD_H, SWP_NOZORDER);
        } else if (isCombo) {
            int x = MARGIN + LABEL_W + CONTROL_MARGIN;
            int cw = rc.right - x - MARGIN;
            if (cw < 50) cw = 50;
            SetWindowPos(h, nullptr, x, baseY + (int)i * FIELD_SPACING,
                cw, 0, SWP_NOZORDER | SWP_NOSIZE);
        } else if (isTrackbar) {
            int x = MARGIN + LABEL_W + CONTROL_MARGIN;
            int cw = rc.right - x - MARGIN - 50;
            if (cw < 50) cw = 50;
            SetWindowPos(h, nullptr, x, baseY + (int)i * FIELD_SPACING + 2,
                cw, 22, SWP_NOZORDER);
            auto lit = m_sliderValueLabels.find(h);
            if (lit != m_sliderValueLabels.end() && IsWindow(lit->second)) {
                SetWindowPos(lit->second, nullptr, x + cw + 4,
                    baseY + (int)i * FIELD_SPACING + 3, 40, 20, SWP_NOZORDER);
                wchar_t cls[32] = {};
                GetClassNameW(lit->second, cls, 32);
                DBG(L"Reposition: slider=%p valLabel=%p cls=%s x=%d y=%d w=%d",
                    (void*)h, (void*)lit->second, cls, x + cw + 4,
                    baseY + (int)i * FIELD_SPACING + 3, cw);
            }
        } else {
            int x = MARGIN + LABEL_W + CONTROL_MARGIN;
            int cw;
            if (type == ConfigValueType::Directory) {
                cw = rc.right - x - MARGIN - 64 - 4 - 30;
            } else {
                cw = rc.right - x - MARGIN;
            }
            if (cw < 50) cw = 50;
            SetWindowPos(h, nullptr, x, baseY + (int)i * FIELD_SPACING,
                cw, FIELD_H, SWP_NOZORDER);
        }
    }

    for (size_t j = 0; j < m_browseBtns.size(); ++j) {
        HWND hBtn = m_browseBtns[j];
        if (!IsWindow(hBtn)) continue;
        size_t editIdx = m_browseBtnEditIdx[j];
        if (editIdx >= m_fieldControls.size()) continue;
        HWND hEdit = m_fieldControls[editIdx];
        if (!IsWindow(hEdit)) continue;
        RECT rcEdit;
        GetWindowRect(hEdit, &rcEdit);
        POINT pt = { rcEdit.left, rcEdit.top };
        ScreenToClient(m_hwnd, &pt);
        SetWindowPos(hBtn, nullptr, pt.x + (rcEdit.right - rcEdit.left) + 4, pt.y,
            64, FIELD_H, SWP_NOZORDER);
    }

    UpdateScrollBar();
}

void SettingsDialog::UpdateScrollBar() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);

    size_t fieldCount = m_fieldControls.size();
    if (m_fieldLabels.size() > fieldCount) fieldCount = m_fieldLabels.size();

    int totalFieldHeight = (int)fieldCount * FIELD_SPACING;
    int availableHeight = rc.bottom - FIELD_START_Y - BTN_H - 20;

    SCROLLINFO si = { sizeof(si), SIF_ALL };
    si.nMin = 0;
    si.nMax = std::max(0, totalFieldHeight);
    si.nPage = (UINT)std::max(0, availableHeight);
    si.nPos = m_scrollPos;

    if (totalFieldHeight <= availableHeight) {
        si.nMax = 0;
        si.nPage = 1;
        si.nPos = 0;
        m_scrollPos = 0;
        ShowScrollBar(m_hwnd, SB_VERT, FALSE);
    } else {
        ShowScrollBar(m_hwnd, SB_VERT, TRUE);
    }

    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
}

void SettingsDialog::OnSave(HWND hwnd) {
    ReadFieldsFromControls();

    PetConfig::Data petData;
    auto getInt = [&](const wchar_t* key, int def) -> int {
        auto it = m_petEditedValues.find(key);
        return (it != m_petEditedValues.end()) ? _wtoi(it->second.c_str()) : def;
    };
    auto getBool = [&](const wchar_t* key, bool def) -> bool {
        auto it = m_petEditedValues.find(key);
        return (it != m_petEditedValues.end()) ? (it->second == L"1") : def;
    };
    petData.posX = getInt(L"posX", -1);
    petData.posY = getInt(L"posY", -1);
    petData.opacity = getInt(L"opacity", 255);
    petData.scalePercent = getInt(L"scalePercent", 100);
    petData.alwaysOnTop = getBool(L"alwaysOnTop", true);
    petData.moveEnabled = getBool(L"moveEnabled", false);
    petData.moveStep = getInt(L"moveStep", 3);
    petData.moveSpeed = getInt(L"moveSpeed", 200);
    petData.moveShuttle = getBool(L"moveShuttle", false);
    {
        auto it = m_petEditedValues.find(L"language");
        petData.language = (it != m_petEditedValues.end()) ? it->second : L"en";
    }
    PetConfig::Save(petData);
    SendMessageW(m_parent, WM_APP + 3, 0, 0);

    for (size_t i = 0; i < m_modules.size(); ++i) {
        auto* mod = m_modules[i];
        for (const auto& def : mod->GetConfigDefinitions()) {
            auto it = m_editedValues[i].find(def.key);
            if (it != m_editedValues[i].end()) {
                mod->SetValue(def.key, it->second);
            }
        }
    }

    m_moduleManager->SaveAllConfigs();
    m_moduleManager->ApplyAllConfigs();

    Logger::Write(L"Main", L"Settings saved.");
    DestroyWindow(hwnd);
}

void SettingsDialog::OnCancel(HWND hwnd) {
    DBG(L"Settings OnCancel: restoring origOpacity=%d origScale=%d", m_origOpacity, m_origScale);
    PetConfig::Data cfg = PetConfig::Load();
    cfg.opacity = m_origOpacity;
    cfg.scalePercent = m_origScale;
    PetConfig::Save(cfg);
    SendMessageW(m_parent, WM_APP + 2, 110, m_origOpacity);
    SendMessageW(m_parent, WM_APP + 2, 111, m_origScale);
    DestroyWindow(hwnd);
}
