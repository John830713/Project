#include "AutoKeyDialog.h"
#include "AutoKeyModule.h"
#include "../../Services/TranslationService.h"
#include "../../Core/DebugConsole.h"

#include <string>
#include <vector>

//==============================================================================
// Show (static)
//==============================================================================

void AutoKeyDialog::Show(HWND parent, AutoKeyModule* module) {
    DBG(L"AutoKeyDialog::Show entered, parent=%p", (void*)parent);
    AutoKeyDialog dlg(parent, module);
    if (parent) EnableWindow(parent, FALSE);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsWindow(dlg.m_hwnd)) break;
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

    if (parent) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }
    DBG(L"AutoKeyDialog::Show exited");
}

AutoKeyDialog::AutoKeyDialog(HWND parent, AutoKeyModule* module)
    : m_module(module)
{
    const wchar_t kClass[] = L"AutoKeyDialogClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(0, kClass,
        TranslationService::Get()->Tr(L"AutoKey", L"Manage AutoKey Actions").c_str(),
        WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 460, 380,
        parent, nullptr, GetModuleHandleW(nullptr), this);

    if (m_hwnd) {
        if (parent) {
            RECT pr, cr;
            GetWindowRect(parent, &pr);
            GetWindowRect(m_hwnd, &cr);
            int w = cr.right - cr.left;
            int h = cr.bottom - cr.top;
            int x = pr.left + (pr.right - pr.left - w) / 2;
            int y = pr.top + (pr.bottom - pr.top - h) / 2;
            SetWindowPos(m_hwnd, nullptr, x, y, 0, 0,
                         SWP_NOZORDER | SWP_NOSIZE);
        }
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
    }
}

LRESULT CALLBACK AutoKeyDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AutoKeyDialog* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<AutoKeyDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<AutoKeyDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT AutoKeyDialog::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        m_hwnd = hwnd;
        OnCreate(hwnd);
        return 0;

    case WM_COMMAND: {
        UINT id = LOWORD(wParam);
        UINT code = HIWORD(wParam);
        if (id == 100) { OnAdd(); return 0; }
        if (id == 101) { OnEdit(); return 0; }
        if (id == 102) { OnDelete(); return 0; }
        if (id == 103) { DestroyWindow(hwnd); return 0; }
        if (id == 104 && code == LBN_DBLCLK) { OnEdit(); return 0; }
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

void AutoKeyDialog::OnCreate(HWND hwnd) {
    auto Tr = [](const wchar_t* text) {
        return TranslationService::Get()->Tr(L"AutoKey", text);
    };

    const int MARGIN = 10;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    m_hList = CreateWindowW(L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        MARGIN, MARGIN, w - MARGIN * 2, h - 80,
        hwnd, (HMENU)104, GetModuleHandleW(nullptr), nullptr);

    int btnY = h - 60;
    int btnW = 80;
    int btnH = 26;
    int gap = 10;

    CreateWindowW(L"BUTTON", Tr(L"Add").c_str(),
        WS_CHILD | WS_VISIBLE,
        MARGIN, btnY, btnW, btnH,
        hwnd, (HMENU)100, GetModuleHandleW(nullptr), nullptr);

    CreateWindowW(L"BUTTON", Tr(L"Edit").c_str(),
        WS_CHILD | WS_VISIBLE,
        MARGIN + btnW + gap, btnY, btnW, btnH,
        hwnd, (HMENU)101, GetModuleHandleW(nullptr), nullptr);

    CreateWindowW(L"BUTTON", Tr(L"Delete").c_str(),
        WS_CHILD | WS_VISIBLE,
        MARGIN + (btnW + gap) * 2, btnY, btnW, btnH,
        hwnd, (HMENU)102, GetModuleHandleW(nullptr), nullptr);

    CreateWindowW(L"BUTTON", Tr(L"Close").c_str(),
        WS_CHILD | WS_VISIBLE,
        w - MARGIN - btnW, btnY, btnW, btnH,
        hwnd, (HMENU)103, GetModuleHandleW(nullptr), nullptr);

    RefreshList();
}

void AutoKeyDialog::RefreshList() {
    auto Tr = [](const wchar_t* text) {
        return TranslationService::Get()->Tr(L"AutoKey", text);
    };

    if (!m_hList) return;
    SendMessageW(m_hList, LB_RESETCONTENT, 0, 0);

    const auto& actions = m_module->GetActions();
    for (const auto& a : actions) {
        std::wstring item = a.name;
        if (!a.keys.empty()) item += L"  [" + a.keys + L"]";
        if (!a.triggerHotkey.empty()) item += L"  \u2190 " + a.triggerHotkey;
        if (a.running != 0) item += L"  \u25B6 " + Tr(L"RUNNING");
        SendMessageW(m_hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
}

void AutoKeyDialog::OnAdd() {
    DBG(L"AutoKeyDialog::OnAdd");
    if (EditAction(m_hwnd, -1)) {
        DBG(L"  EditAction OK, refreshing list (count=%zu)", m_module->GetActions().size());
        RefreshList();
    } else {
        DBG(L"  EditAction cancelled");
    }
}

void AutoKeyDialog::OnEdit() {
    if (!m_hList) return;
    int sel = static_cast<int>(SendMessageW(m_hList, LB_GETCURSEL, 0, 0));
    if (sel == LB_ERR) return;

    if (EditAction(m_hwnd, sel)) {
        RefreshList();
    }
}

void AutoKeyDialog::OnDelete() {
    if (!m_hList) return;
    int sel = static_cast<int>(SendMessageW(m_hList, LB_GETCURSEL, 0, 0));
    if (sel == LB_ERR) return;

    auto& actions = m_module->GetActions();
    if (sel < 0 || sel >= static_cast<int>(actions.size())) return;

    m_module->StopAction(sel);
    actions.erase(actions.begin() + sel);

    for (size_t i = 0; i < actions.size(); ++i) {
        actions[i].id = static_cast<int>(i);
    }

    m_module->SaveActions();
    m_module->ApplyConfig();
    RefreshList();
}

bool AutoKeyDialog::EditAction(HWND parentWnd, int actionId) {
    auto& actions = m_module->GetActions();
    bool isNew = (actionId < 0);

    AutoKeyAction action;
    if (!isNew) {
        action.id = actions[actionId].id;
        action.name = actions[actionId].name;
        action.keys = actions[actionId].keys;
        action.intervalSeconds = actions[actionId].intervalSeconds;
        action.repeatCount = actions[actionId].repeatCount;
        action.actionMode = actions[actionId].actionMode;
        action.triggerHotkey = actions[actionId].triggerHotkey;
        action.triggerMode = actions[actionId].triggerMode;
        action.autoStart = actions[actionId].autoStart;
    } else {
        action.id = static_cast<int>(actions.size());
        action.name = TranslationService::Get()->Tr(L"AutoKey", L"New Action");
        action.keys = L"F13";
        action.intervalSeconds = 1200;
        action.repeatCount = 0;
        action.actionMode = 1;
        action.triggerMode = 0;
        action.autoStart = false;
    }

    const wchar_t kEditClass[] = L"AutoKeyEditDialogClass";
    struct { bool ok; bool done; } state = { false, false };
    WNDCLASSW wc = {};
    wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        if (m == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            SetWindowLongPtrW(h, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        auto* s = reinterpret_cast<decltype(state)*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (!s) return DefWindowProcW(h, m, w, l);

        switch (m) {
        case WM_COMMAND: {
            UINT id = LOWORD(w);
            if (id == 1) { s->ok = true; s->done = true; PostMessageW(h, WM_NULL, 0, 0); return 0; }
            if (id == 2) { s->done = true; PostMessageW(h, WM_NULL, 0, 0); return 0; }
            break;
        }
        case WM_CLOSE:
            s->done = true;
            PostMessageW(h, WM_NULL, 0, 0);
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kEditClass;
    wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
    RegisterClassW(&wc);

    HWND dlg = CreateWindowExW(0, kEditClass,
        TranslationService::Get()->Tr(L"AutoKey", isNew ? L"Add Action" : L"Edit Action").c_str(),
        WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 360,
        parentWnd, nullptr, GetModuleHandleW(nullptr), &state);

    if (!dlg) return false;

    if (parentWnd) {
        RECT pr, cr;
        GetWindowRect(parentWnd, &pr);
        GetWindowRect(dlg, &cr);
        int w2 = cr.right - cr.left;
        int h2 = cr.bottom - cr.top;
        int x = pr.left + (pr.right - pr.left - w2) / 2;
        int y = pr.top + (pr.bottom - pr.top - h2) / 2;
        SetWindowPos(dlg, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
    }

    const int M = 10;
    const int LH = 22;
    const int EH = 22;
    const int SP = 28;
    int cy = M;

    auto Tr = [](const wchar_t* text) {
        return TranslationService::Get()->Tr(L"AutoKey", text);
    };

    auto makeLabel = [&](const std::wstring& text) {
        CreateWindowW(L"STATIC", text.c_str(), WS_CHILD | WS_VISIBLE,
                      M, cy, 120, LH, dlg, nullptr,
                      GetModuleHandleW(nullptr), nullptr);
    };
    auto makeEdit = [&](const std::wstring& text, int id) {
        CreateWindowW(L"EDIT", text.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                      130, cy, 260, EH, dlg, (HMENU)(INT_PTR)id,
                      GetModuleHandleW(nullptr), nullptr);
    };
    auto makeCheck = [&](const std::wstring& text, int id, bool checked) {
        HWND h = CreateWindowW(L"BUTTON", text.c_str(),
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            130, cy, 260, LH, dlg, (HMENU)(INT_PTR)id,
            GetModuleHandleW(nullptr), nullptr);
        if (checked) SendMessageW(h, BM_SETCHECK, BST_CHECKED, 0);
    };
    auto makeRadio = [&](const std::wstring& text, int id, bool selected, bool firstInGroup) {
        DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON;
        if (firstInGroup) style |= WS_GROUP;
        HWND h = CreateWindowW(L"BUTTON", text.c_str(), style,
                               130, cy, 120, LH, dlg, (HMENU)(INT_PTR)id,
                               GetModuleHandleW(nullptr), nullptr);
        if (selected) SendMessageW(h, BM_SETCHECK, BST_CHECKED, 0);
    };

    makeLabel(Tr(L"Name:"));
    makeEdit(action.name.c_str(), 10);
    cy += SP;

    makeLabel(Tr(L"Keys:"));
    makeEdit(action.keys.c_str(), 11);
    cy += SP;

    makeLabel(Tr(L"Trigger:"));
    makeEdit(action.triggerHotkey.c_str(), 12);
    cy += SP;

    makeLabel(Tr(L"Interval (s):"));
    { wchar_t buf[16]; swprintf(buf, 16, L"%d", action.intervalSeconds); makeEdit(buf, 13); }
    cy += SP;

    makeLabel(Tr(L"Repeat:"));
    { wchar_t buf[16]; swprintf(buf, 16, L"%d", action.repeatCount); makeEdit(buf, 14); }
    cy += SP;

    makeCheck(Tr(L"Auto Start"), 15, action.autoStart);
    cy += SP;

    makeLabel(Tr(L"Action Mode:"));
    makeRadio(Tr(L"Press Once"),  20, action.actionMode == 0, true);
    makeRadio(Tr(L"Repeat"),      21, action.actionMode == 1, false);
    cy += SP;

    makeLabel(Tr(L"Trigger Mode:"));
    makeRadio(Tr(L"Press (once)"), 22, action.triggerMode == 0, true);
    makeRadio(Tr(L"Toggle"),       23, action.triggerMode == 1, false);
    cy += SP + 10;

    CreateWindowW(L"BUTTON", Tr(L"OK").c_str(),
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        220, cy, 80, 26, dlg, (HMENU)1,
        GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", Tr(L"Cancel").c_str(),
        WS_CHILD | WS_VISIBLE,
        310, cy, 80, 26, dlg, (HMENU)2,
        GetModuleHandleW(nullptr), nullptr);

    SetForegroundWindow(dlg);
    SetFocus(dlg);

    MSG msg = {};
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0)) {
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

    if (!state.ok) { DestroyWindow(dlg); return false; }

    auto readText = [&](int id) -> std::wstring {
        HWND h = GetDlgItem(dlg, id);
        if (!h) return L"";
        int len = GetWindowTextLengthW(h);
        std::wstring s;
        s.resize(len);
        GetWindowTextW(h, &s[0], len + 1);
        return s;
    };
    auto readCheck = [&](int id) -> bool {
        HWND h = GetDlgItem(dlg, id);
        if (!h) return false;
        return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
    };
    auto readRadio = [&](int id) -> bool {
        HWND h = GetDlgItem(dlg, id);
        if (!h) return false;
        return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
    };

    action.name = readText(10);
    action.keys = readText(11);
    action.triggerHotkey = readText(12);
    action.intervalSeconds = _wtoi(readText(13).c_str());
    if (action.intervalSeconds <= 0) action.intervalSeconds = 1200;
    action.repeatCount = _wtoi(readText(14).c_str());
    action.autoStart = readCheck(15);
    action.actionMode = readRadio(20) ? 0 : 1;
    action.triggerMode = readRadio(22) ? 0 : 1;

    DBG(L"EditAction %s: name='%s' keys='%s'", isNew?L"ADD":L"EDIT",
        action.name.c_str(), action.keys.c_str());
    if (isNew) {
        actions.push_back(std::move(action));
        DBG(L"  pushed, count=%zu", actions.size());
    } else {
        action.running = actions[actionId].running;
        action.thread = std::move(actions[actionId].thread);
        actions[actionId] = std::move(action);
    }

    m_module->SaveActions();
    m_module->ApplyConfig();
    DestroyWindow(dlg);
    return true;
}
