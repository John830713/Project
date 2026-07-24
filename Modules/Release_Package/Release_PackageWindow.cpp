//==============================================================================
// Release_PackageWindow.cpp - Release_Package UI implementation
//==============================================================================

#include "Release_PackageWindow.h"
#include "Release_PackageModule.h"
#include "Release_PackageLogic.h"
#include "../../Services/TranslationService.h"
#include "../../Core/DebugConsole.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <regex>
#include <filesystem>

namespace fs = std::filesystem;

//==============================================================================
// String helpers
//==============================================================================

static std::wstring BrowseFolder(HWND hParent, const wchar_t* title) {
    BROWSEINFOW bi = {};
    bi.hwndOwner = hParent;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return L"";
    wchar_t path[MAX_PATH];
    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return path;
}

static std::wstring BrowseFile(HWND hParent, const wchar_t* title, const wchar_t* filter) {
    OPENFILENAMEW ofn = {};
    wchar_t file[MAX_PATH] = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hParent;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&ofn) ? file : L"";
}

//==============================================================================
// Globals
//==============================================================================

static HWND g_hWndMain = nullptr;
static HWND g_hListParams = nullptr;
static HWND g_hListRules = nullptr;
static HWND g_hEdt7z, g_hEdtArchive, g_hEdtDefaultSrc, g_hEdtOutDir;
static HWND g_hStatus = nullptr;
static Release_PackageModule* g_module = nullptr;
static std::vector<std::pair<std::wstring, int>> g_archiveFiles;

enum {
    IDM_ADD_PARAM = 2001, IDM_EDIT_PARAM, IDM_DEL_PARAM,
    IDM_ADD_RULE = 2011, IDM_EDIT_RULE, IDM_DEL_RULE, IDM_RULE_UP, IDM_RULE_DOWN,
    IDM_PREVIEW = 2020, IDM_EXECUTE,
    IDM_LOAD_RULES = 2022, IDM_SAVE_RULES,
    IDM_BROWSE_7Z = 2030, IDM_BROWSE_ARC, IDM_BROWSE_OUT,
};

//==============================================================================
// Refresh lists
//==============================================================================

static std::wstring GetParamPreview(const RPParam& p) {
    if (!p.value.empty()) return p.value;
    if (!p.regex.empty()) {
        if (!p.example.empty()) {
            try {
                std::wsmatch m;
                if (std::regex_search(p.example, m, std::wregex(p.regex)))
                    return (m.size() > 1) ? m[1].str() : m[0].str();
                return L"(no match)";
            } catch (...) { return p.regex; }
        }
        return p.regex;
    }
    return L"";
}

static void RefreshParamList() {
    ListView_DeleteAllItems(g_hListParams);
    for (int i = 0; i < (int)g_module->GetParams().size(); i++) {
        auto& p = g_module->GetParams()[i];
        std::wstring wtoken = L"{" + p.name + L"}";
        std::wstring wpreview = GetParamPreview(p);
        LVITEMW li = {};
        li.mask = LVIF_TEXT;
        li.iItem = i;
        li.pszText = (wchar_t*)wtoken.c_str();
        int idx = ListView_InsertItem(g_hListParams, &li);
        ListView_SetItemText(g_hListParams, idx, 1, (wchar_t*)wpreview.c_str());
    }
}

static void RefreshRuleList() {
    ListView_DeleteAllItems(g_hListRules);
    for (int i = 0; i < (int)g_module->GetRules().size(); i++) {
        auto& r = g_module->GetRules()[i];
        std::wstring wen = r.enabled ? L"\u2714" : L"\u2716";
        std::wstring wsrc = r.useDefaultSrc ? L"[Default]" : r.sourceDir;
        std::wstring wpat = r.pattern;
        std::wstring wdest = r.useDefaultDest ? L"[Default]" : r.destDir;
        std::wstring wren = r.rename;
        LVITEMW li = {};
        li.mask = LVIF_TEXT;
        li.iItem = i;
        li.pszText = (wchar_t*)wen.c_str();
        int idx = ListView_InsertItem(g_hListRules, &li);
        ListView_SetItemText(g_hListRules, idx, 1, (wchar_t*)wsrc.c_str());
        ListView_SetItemText(g_hListRules, idx, 2, (wchar_t*)wpat.c_str());
        ListView_SetItemText(g_hListRules, idx, 3, (wchar_t*)wdest.c_str());
        ListView_SetItemText(g_hListRules, idx, 4, (wchar_t*)wren.c_str());
    }
}

//==============================================================================
// Read inputs
//==============================================================================

static void ReadInputs() {
    auto GetText = [](HWND h) -> std::wstring {
        int len = GetWindowTextLengthW(h);
        if (len <= 0) return L"";
        std::wstring w(len + 1, 0);
        GetWindowTextW(h, &w[0], len + 1);
        w.resize(len);
        return w;
    };
    g_module->SetValue(L"7zPath", GetText(g_hEdt7z));
    g_module->SetValue(L"DefaultSourceDir", GetText(g_hEdtDefaultSrc));
    g_module->SetValue(L"OutputDir", GetText(g_hEdtOutDir));
}

static void LoadInputsToUI() {
    SetWindowTextW(g_hEdt7z, g_module->Get7zPath().c_str());
    SetWindowTextW(g_hEdtDefaultSrc, g_module->GetDefaultSourceDir().c_str());
    SetWindowTextW(g_hEdtOutDir, g_module->GetOutputDir().c_str());
}

//==============================================================================
// Param Dialog
//==============================================================================

static HWND g_hParamDlg = nullptr;
static HWND g_pEdName, g_pEdValue, g_pEdRegex, g_pEdExample, g_pStPreview;
static int g_paramEditIdx = -1;

static LRESULT CALLBACK ParamDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE:
        EnableWindow(g_hWndMain, TRUE);
        DestroyWindow(hWnd);
        g_hParamDlg = nullptr;
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == 310) {
            auto GetText = [](HWND h) -> std::wstring {
                int len = GetWindowTextLengthW(h);
                if (len <= 0) return L"";
                std::wstring w(len + 1, 0);
                GetWindowTextW(h, &w[0], len + 1);
                w.resize(len);
                return w;
            };
            std::wstring name = GetText(g_pEdName);
            std::wstring val = GetText(g_pEdValue);
            std::wstring regex = GetText(g_pEdRegex);
            std::wstring example = GetText(g_pEdExample);
            std::wstring preview;
            if (!val.empty()) {
                preview = L"{name} = \"" + val + L"\"";
            } else if (!regex.empty() && !example.empty()) {
                try {
                    std::wsmatch m;
                    if (std::regex_search(example, m, std::wregex(regex)))
                        preview = L"{" + name + L"} = \"" + ((m.size() > 1) ? m[1].str() : m[0].str()) + L"\"";
                    else preview = L"(no match)";
                } catch (...) { preview = L"Invalid regex!"; }
            } else preview = L"Enter value or regex+example.";
            SetWindowTextW(g_pStPreview, preview.c_str());
            return 0;
        }
        if (LOWORD(wParam) == IDOK) {
            auto GetText = [](HWND h) -> std::wstring {
                int len = GetWindowTextLengthW(h);
                if (len <= 0) return L"";
                std::wstring w(len + 1, 0);
                GetWindowTextW(h, &w[0], len + 1);
                w.resize(len);
                return w;
            };
            std::wstring name = GetText(g_pEdName);
            std::wstring val = GetText(g_pEdValue);
            std::wstring regex = GetText(g_pEdRegex);
            std::wstring example = GetText(g_pEdExample);
            if (name.empty()) { MessageBoxW(hWnd, L"Token name cannot be empty.", L"Warning", MB_ICONWARNING); return 0; }
            if (val.empty() && regex.empty()) { MessageBoxW(hWnd, L"Enter a Fixed Value or Regex.", L"Warning", MB_ICONWARNING); return 0; }
            RPParam result{name, val, regex, example};
            if (g_paramEditIdx >= 0) g_module->GetParams()[g_paramEditIdx] = result;
            else g_module->GetParams().push_back(result);
            EnableWindow(g_hWndMain, TRUE);
            DestroyWindow(hWnd);
            g_hParamDlg = nullptr;
            RefreshParamList();
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EnableWindow(g_hWndMain, TRUE);
            DestroyWindow(hWnd);
            g_hParamDlg = nullptr;
            return 0;
        }
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void ShowParamDialog(int editIdx) {
    if (g_hParamDlg) { SetForegroundWindow(g_hParamDlg); return; }
    g_paramEditIdx = editIdx;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ParamDlgProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"RPParamDlg";
    RegisterClassExW(&wc);

    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    int dw = 430, dh = 280;
    RECT rc = {0, 0, dw, dh};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);

    g_hParamDlg = CreateWindowExW(0, L"RPParamDlg",
        editIdx >= 0 ? L"Edit Token" : L"Add Token",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        g_hWndMain, nullptr, GetModuleHandleW(nullptr), nullptr);

    int y = 14;
    auto mkLabel = [&](const wchar_t* t) {
        HWND h = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE, 12, y + 3, 90, 20, g_hParamDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
    };
    auto mkEdit = [&](int id) {
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 108, y, 300, 24, g_hParamDlg, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
        SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        return h;
    };

    mkLabel(L"Token Name:"); y += 4; g_pEdName = mkEdit(301); y += 30;
    mkLabel(L"Fixed Value:"); y += 4; g_pEdValue = mkEdit(302); y += 30;
    mkLabel(L"Regex:");       y += 4; g_pEdRegex = mkEdit(303); y += 30;
    mkLabel(L"Example:");     y += 4; g_pEdExample = mkEdit(304); y += 30;

    g_pStPreview = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 12, y, 396, 20, g_hParamDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessage(g_pStPreview, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 26;

    HWND hb;
    hb = CreateWindowExW(0, L"BUTTON", L"Test String", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 12, y, 90, 28, g_hParamDlg, (HMENU)310, GetModuleHandleW(nullptr), nullptr);
    SendMessage(hb, WM_SETFONT, (WPARAM)hFont, TRUE);
    hb = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 234, y, 80, 28, g_hParamDlg, (HMENU)IDOK, GetModuleHandleW(nullptr), nullptr);
    SendMessage(hb, WM_SETFONT, (WPARAM)hFont, TRUE);
    hb = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 320, y, 80, 28, g_hParamDlg, (HMENU)IDCANCEL, GetModuleHandleW(nullptr), nullptr);
    SendMessage(hb, WM_SETFONT, (WPARAM)hFont, TRUE);

    if (editIdx >= 0) {
        auto& p = g_module->GetParams()[editIdx];
        SetWindowTextW(g_pEdName, p.name.c_str());
        SetWindowTextW(g_pEdValue, p.value.c_str());
        SetWindowTextW(g_pEdRegex, p.regex.c_str());
        SetWindowTextW(g_pEdExample, p.example.c_str());
    }

    EnableWindow(g_hWndMain, FALSE);
}

//==============================================================================
// Rule Dialog
//==============================================================================

static HWND g_hRuleDlg = nullptr;
static HWND g_rChkEnabled, g_rChkDefSrc, g_rEdSrcDir, g_rEdPattern;
static HWND g_rChkDefDest, g_rEdDestDir, g_rBtnBrowse, g_rEdRename;
static int g_ruleEditIdx = -1;

static void RuleToggleSrc() {
    BOOL checked = (SendMessage(g_rChkDefSrc, BM_GETCHECK, 0, 0) == BST_CHECKED);
    EnableWindow(g_rEdSrcDir, !checked);
    if (checked) SetWindowTextW(g_rEdSrcDir, g_module->GetDefaultSourceDir().c_str());
}

static void RuleToggleDest() {
    BOOL checked = (SendMessage(g_rChkDefDest, BM_GETCHECK, 0, 0) == BST_CHECKED);
    EnableWindow(g_rEdDestDir, !checked);
    EnableWindow(g_rBtnBrowse, !checked);
    if (checked) SetWindowTextW(g_rEdDestDir, g_module->GetOutputDir().c_str());
}

static LRESULT CALLBACK RuleDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE:
        EnableWindow(g_hWndMain, TRUE);
        DestroyWindow(hWnd);
        g_hRuleDlg = nullptr;
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == 403) { RuleToggleSrc(); return 0; }
        if (LOWORD(wParam) == 406) { RuleToggleDest(); return 0; }
        if (LOWORD(wParam) == 409) {
            std::wstring d = BrowseFolder(hWnd, L"Select destination");
            if (!d.empty()) SetWindowTextW(g_rEdDestDir, d.c_str());
            return 0;
        }
        if (LOWORD(wParam) == IDOK) {
            auto GetText = [](HWND h) -> std::wstring {
                int len = GetWindowTextLengthW(h);
                if (len <= 0) return L"";
                std::wstring w(len + 1, 0);
                GetWindowTextW(h, &w[0], len + 1);
                w.resize(len);
                return w;
            };
            RPRule result;
            result.enabled = (SendMessage(g_rChkEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
            result.useDefaultSrc = (SendMessage(g_rChkDefSrc, BM_GETCHECK, 0, 0) == BST_CHECKED);
            result.sourceDir = GetText(g_rEdSrcDir);
            result.pattern = GetText(g_rEdPattern);
            if (result.pattern.empty()) result.pattern = L"*";
            result.useDefaultDest = (SendMessage(g_rChkDefDest, BM_GETCHECK, 0, 0) == BST_CHECKED);
            result.destDir = GetText(g_rEdDestDir);
            result.rename = GetText(g_rEdRename);
            if (result.rename.empty()) result.rename = L"{filename}{ext}";
            if (g_ruleEditIdx >= 0) g_module->GetRules()[g_ruleEditIdx] = result;
            else g_module->GetRules().push_back(result);
            EnableWindow(g_hWndMain, TRUE);
            DestroyWindow(hWnd);
            g_hRuleDlg = nullptr;
            RefreshRuleList();
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EnableWindow(g_hWndMain, TRUE);
            DestroyWindow(hWnd);
            g_hRuleDlg = nullptr;
            return 0;
        }
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void ShowRuleDialog(int editIdx) {
    if (g_hRuleDlg) { SetForegroundWindow(g_hRuleDlg); return; }
    g_ruleEditIdx = editIdx;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = RuleDlgProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"RPRuleDlg";
    RegisterClassExW(&wc);

    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    int dw = 450, dh = 340;
    RECT rc = {0, 0, dw, dh};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);

    g_hRuleDlg = CreateWindowExW(0, L"RPRuleDlg",
        editIdx >= 0 ? L"Edit Rule" : L"Add Rule",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        g_hWndMain, nullptr, GetModuleHandleW(nullptr), nullptr);

    int y = 14;
    auto mkLabel = [&](const wchar_t* t) {
        HWND h = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE, 12, y + 3, 120, 20, g_hRuleDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
    };
    auto mkCheck = [&](int id, BOOL checked) {
        HWND h = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            140, y, 280, 24, g_hRuleDlg, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
        SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(h, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        return h;
    };
    auto mkEdit = [&](int id, const wchar_t* val) {
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", val, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            140, y, 230, 24, g_hRuleDlg, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
        SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        return h;
    };

    mkLabel(L"Enabled:");           y += 4; g_rChkEnabled = mkCheck(401, TRUE); y += 28;
    mkLabel(L"Use Default Source:"); y += 4; g_rChkDefSrc = mkCheck(403, TRUE); y += 24;
    mkLabel(L"Source Dir:");         y += 4; g_rEdSrcDir = mkEdit(404, L""); y += 30;
    mkLabel(L"Filename Pattern:");   y += 4; g_rEdPattern = mkEdit(405, L"*"); y += 30;
    mkLabel(L"Use Default Output:"); y += 4; g_rChkDefDest = mkCheck(406, TRUE); y += 24;
    mkLabel(L"Destination Dir:");    y += 4;
    g_rEdDestDir = mkEdit(407, L"");
    g_rBtnBrowse = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        374, y, 30, 24, g_hRuleDlg, (HMENU)409, GetModuleHandleW(nullptr), nullptr);
    SendMessage(g_rBtnBrowse, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 30;
    mkLabel(L"Rename Pattern:");     y += 4; g_rEdRename = mkEdit(408, L"{filename}{ext}"); y += 36;

    HWND hb;
    hb = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 228, y, 80, 28, g_hRuleDlg, (HMENU)IDOK, GetModuleHandleW(nullptr), nullptr);
    SendMessage(hb, WM_SETFONT, (WPARAM)hFont, TRUE);
    hb = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 314, y, 80, 28, g_hRuleDlg, (HMENU)IDCANCEL, GetModuleHandleW(nullptr), nullptr);
    SendMessage(hb, WM_SETFONT, (WPARAM)hFont, TRUE);

    if (editIdx >= 0) {
        auto& r = g_module->GetRules()[editIdx];
        SendMessage(g_rChkEnabled, BM_SETCHECK, r.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessage(g_rChkDefSrc, BM_SETCHECK, r.useDefaultSrc ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextW(g_rEdSrcDir, r.sourceDir.c_str());
        SetWindowTextW(g_rEdPattern, r.pattern.c_str());
        SendMessage(g_rChkDefDest, BM_SETCHECK, r.useDefaultDest ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextW(g_rEdDestDir, r.destDir.c_str());
        SetWindowTextW(g_rEdRename, r.rename.c_str());
    }

    RuleToggleSrc();
    RuleToggleDest();
    EnableWindow(g_hWndMain, FALSE);
}

//==============================================================================
// Preview / Execute
//==============================================================================

static void DoPreview() {
    ReadInputs();
    std::wstring archivePath;
    {
        int len = GetWindowTextLengthW(g_hEdtArchive);
        if (len > 0) {
            std::wstring w(len + 1, 0);
            GetWindowTextW(g_hEdtArchive, &w[0], len + 1);
            w.resize(len);
            archivePath = w;
        }
    }
    if (archivePath.empty() || !fs::exists(archivePath)) {
        MessageBoxW(g_hWndMain, L"Please select a valid .7z archive.", L"Warning", MB_ICONWARNING);
        return;
    }
    if (g_archiveFiles.empty()) {
        SetWindowTextW(g_hStatus, L"Listing archive..."); UpdateWindow(g_hWndMain);
        g_archiveFiles = RPLogic::ListArchive(archivePath, g_module->Get7zPath());
    }
    std::wstring outDir = g_module->GetOutputDir().empty() ?
        (RPLogic::BaseName(archivePath) + L"_output") : g_module->GetOutputDir();
    for (auto& r : g_module->GetRules()) {
        if (r.useDefaultSrc) r.sourceDir = g_module->GetDefaultSourceDir();
        if (r.useDefaultDest) r.destDir = outDir;
    }
    auto tokens = RPLogic::ExtractTokens(archivePath, g_module->GetParams());
    auto matches = RPLogic::MatchFiles(g_archiveFiles, tokens, g_module->GetRules());
    if (matches.empty()) {
        MessageBoxW(g_hWndMain, L"No files matched.", L"Preview", MB_ICONINFORMATION);
        SetWindowTextW(g_hStatus, L"No matches");
        return;
    }

    std::wstring text = L"Matched " + std::to_wstring(matches.size()) + L" file(s):\n\n";
    for (auto& m : matches) {
        text += L"  " + m.originalPath + L"\n    -> " + m.destPath + L"\n\n";
    }

    HWND hWin = CreateWindowExW(0, L"EDIT", L"Preview",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_WANTRETURN,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 480,
        g_hWndMain, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessage(hWin, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), TRUE);
    SetWindowTextW(hWin, text.c_str());
}

static void DoExecute() {
    ReadInputs();
    std::wstring archivePath;
    {
        int len = GetWindowTextLengthW(g_hEdtArchive);
        if (len > 0) {
            std::wstring w(len + 1, 0);
            GetWindowTextW(g_hEdtArchive, &w[0], len + 1);
            w.resize(len);
            archivePath = w;
        }
    }
    if (archivePath.empty() || !fs::exists(archivePath)) {
        MessageBoxW(g_hWndMain, L"Please select a valid .7z archive.", L"Warning", MB_ICONWARNING);
        return;
    }
    if (g_archiveFiles.empty()) {
        SetWindowTextW(g_hStatus, L"Listing archive..."); UpdateWindow(g_hWndMain);
        g_archiveFiles = RPLogic::ListArchive(archivePath, g_module->Get7zPath());
    }
    std::wstring outDir = g_module->GetOutputDir().empty() ?
        (RPLogic::BaseName(archivePath) + L"_output") : g_module->GetOutputDir();
    for (auto& r : g_module->GetRules()) {
        if (r.useDefaultSrc) r.sourceDir = g_module->GetDefaultSourceDir();
        if (r.useDefaultDest) r.destDir = outDir;
    }
    auto tokens = RPLogic::ExtractTokens(archivePath, g_module->GetParams());
    auto matches = RPLogic::MatchFiles(g_archiveFiles, tokens, g_module->GetRules());
    if (matches.empty()) {
        MessageBoxW(g_hWndMain, L"No files matched.", L"Info", MB_ICONINFORMATION);
        return;
    }

    std::wstring msg = L"Extract " + std::to_wstring(matches.size()) + L" file(s) to:\n" + outDir + L"\n\nProceed?";
    if (MessageBoxW(g_hWndMain, msg.c_str(), L"Confirm", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    SetWindowTextW(g_hStatus, L"Extracting..."); UpdateWindow(g_hWndMain);
    bool ok = RPLogic::ExtractFiles(archivePath, g_module->Get7zPath(), matches, outDir);

    std::wstring result = ok ? L"Extraction complete." : L"Extraction failed.";
    MessageBoxW(g_hWndMain, result.c_str(), L"Complete", MB_ICONINFORMATION);
    SetWindowTextW(g_hStatus, result.c_str());
}

//==============================================================================
// Main Window
//==============================================================================

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        int y = 8;
        auto mkGrp = [&](const wchar_t* t, int h) {
            HWND g = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 8, y, 848, h, hWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessage(g, WM_SETFONT, (WPARAM)hFont, TRUE);
        };
        auto mkLbl = [&](const wchar_t* t, int cx, int cy) {
            HWND h = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE, cx, cy + 3, 90, 20, hWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        };
        auto mkEdt = [&](int id, int cx, int cy, int cw) {
            HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, cx, cy, cw, 24, hWnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
            SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
            return h;
        };
        auto mkBtn = [&](const wchar_t* t, int id, int cx, int cy, int cw) {
            HWND h = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP, cx, cy, cw, 24, hWnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
            SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
            return h;
        };

        mkGrp(L"Paths (config.ini)", 120);
        mkLbl(L"7z Path:", 16, y + 20); g_hEdt7z = mkEdt(1001, 100, y + 18, 600); mkBtn(L"...", IDM_BROWSE_7Z, 704, y + 18, 30);
        mkLbl(L"Source .7z:", 16, y + 48); g_hEdtArchive = mkEdt(1002, 100, y + 46, 600); mkBtn(L"...", IDM_BROWSE_ARC, 704, y + 46, 30);
        mkLbl(L"Default Src:", 16, y + 76); g_hEdtDefaultSrc = mkEdt(1003, 100, y + 74, 660);
        mkLbl(L"Output Dir:", 16, y + 104); g_hEdtOutDir = mkEdt(1004, 100, y + 102, 600); mkBtn(L"...", IDM_BROWSE_OUT, 704, y + 102, 30);
        y += 128;

        mkGrp(L"Custom Tokens (config.json > params)", 110);
        g_hListParams = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
            16, y + 18, 760, 82, hWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessage(g_hListParams, WM_SETFONT, (WPARAM)hFont, TRUE);
        ListView_SetExtendedListViewStyle(g_hListParams, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        LVCOLUMNW lvc = {};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (wchar_t*)L"Token"; lvc.cx = 140;
        ListView_InsertColumn(g_hListParams, 0, &lvc);
        lvc.pszText = (wchar_t*)L"Value"; lvc.cx = 600;
        ListView_InsertColumn(g_hListParams, 1, &lvc);
        mkBtn(L"Add", IDM_ADD_PARAM, 784, y + 18, 64);
        mkBtn(L"Edit", IDM_EDIT_PARAM, 784, y + 48, 64);
        mkBtn(L"Del", IDM_DEL_PARAM, 784, y + 78, 64);
        y += 118;

        mkGrp(L"Extraction Rules (config.json > rules)", 250);
        g_hListRules = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
            16, y + 18, 824, 196, hWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessage(g_hListRules, WM_SETFONT, (WPARAM)hFont, TRUE);
        ListView_SetExtendedListViewStyle(g_hListRules, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        lvc.pszText = (wchar_t*)L"On"; lvc.cx = 36;
        ListView_InsertColumn(g_hListRules, 0, &lvc);
        lvc.pszText = (wchar_t*)L"Source Dir"; lvc.cx = 130;
        ListView_InsertColumn(g_hListRules, 1, &lvc);
        lvc.pszText = (wchar_t*)L"Pattern"; lvc.cx = 120;
        ListView_InsertColumn(g_hListRules, 2, &lvc);
        lvc.pszText = (wchar_t*)L"Destination"; lvc.cx = 200;
        ListView_InsertColumn(g_hListRules, 3, &lvc);
        lvc.pszText = (wchar_t*)L"Rename"; lvc.cx = 180;
        ListView_InsertColumn(g_hListRules, 4, &lvc);
        y += 275;

        mkBtn(L"Add", IDM_ADD_RULE, 16, y, 64);
        mkBtn(L"Edit", IDM_EDIT_RULE, 86, y, 64);
        mkBtn(L"Delete", IDM_DEL_RULE, 156, y, 64);
        mkBtn(L"Up", IDM_RULE_UP, 226, y, 40);
        mkBtn(L"Down", IDM_RULE_DOWN, 272, y, 48);
        mkBtn(L"Load Rules", IDM_LOAD_RULES, 340, y, 80);
        mkBtn(L"Save Rules", IDM_SAVE_RULES, 426, y, 80);
        mkBtn(L"Preview", IDM_PREVIEW, 580, y, 100);
        mkBtn(L"Execute", IDM_EXECUTE, 686, y, 100);

        g_hStatus = CreateWindowExW(0, L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_LEFT, 16, y + 34, 820, 20, hWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessage(g_hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

        LoadInputsToUI();
        RefreshParamList();
        RefreshRuleList();
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        switch (id) {
        case IDM_BROWSE_7Z: {
            std::wstring f = BrowseFile(hWnd, L"Select 7z.exe", L"7-Zip (7z.exe)\07z.exe\0All\0*.*\0");
            if (!f.empty()) SetWindowTextW(g_hEdt7z, f.c_str());
            break;
        }
        case IDM_BROWSE_ARC: {
            std::wstring f = BrowseFile(hWnd, L"Select archive", L"7z archives (*.7z)\0*.7z\0All\0*.*\0");
            if (!f.empty()) {
                SetWindowTextW(g_hEdtArchive, f.c_str());
                ReadInputs();
                SetWindowTextW(g_hStatus, L"Listing archive..."); UpdateWindow(hWnd);
                g_archiveFiles = RPLogic::ListArchive(f, g_module->Get7zPath());
                SetWindowTextW(g_hStatus, (L"Loaded " + std::to_wstring(g_archiveFiles.size()) + L" file(s)").c_str());
            }
            break;
        }
        case IDM_BROWSE_OUT: {
            std::wstring d = BrowseFolder(hWnd, L"Select output directory");
            if (!d.empty()) SetWindowTextW(g_hEdtOutDir, d.c_str());
            break;
        }
        case IDM_ADD_PARAM: ReadInputs(); ShowParamDialog(-1); break;
        case IDM_EDIT_PARAM: {
            int sel = ListView_GetNextItem(g_hListParams, -1, LVNI_SELECTED);
            if (sel >= 0) { ReadInputs(); ShowParamDialog(sel); }
            break;
        }
        case IDM_DEL_PARAM: {
            int sel = ListView_GetNextItem(g_hListParams, -1, LVNI_SELECTED);
            if (sel >= 0) { g_module->GetParams().erase(g_module->GetParams().begin() + sel); RefreshParamList(); }
            break;
        }
        case IDM_ADD_RULE: ReadInputs(); ShowRuleDialog(-1); break;
        case IDM_EDIT_RULE: {
            int sel = ListView_GetNextItem(g_hListRules, -1, LVNI_SELECTED);
            if (sel >= 0) { ReadInputs(); ShowRuleDialog(sel); }
            break;
        }
        case IDM_DEL_RULE: {
            int sel = ListView_GetNextItem(g_hListRules, -1, LVNI_SELECTED);
            if (sel >= 0) { g_module->GetRules().erase(g_module->GetRules().begin() + sel); RefreshRuleList(); }
            break;
        }
        case IDM_RULE_UP: {
            int sel = ListView_GetNextItem(g_hListRules, -1, LVNI_SELECTED);
            if (sel > 0) { std::swap(g_module->GetRules()[sel], g_module->GetRules()[sel - 1]); RefreshRuleList(); ListView_SetItemState(g_hListRules, sel - 1, LVIS_SELECTED, LVIS_SELECTED); }
            break;
        }
        case IDM_RULE_DOWN: {
            int sel = ListView_GetNextItem(g_hListRules, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < (int)g_module->GetRules().size() - 1) { std::swap(g_module->GetRules()[sel], g_module->GetRules()[sel + 1]); RefreshRuleList(); ListView_SetItemState(g_hListRules, sel + 1, LVIS_SELECTED, LVIS_SELECTED); }
            break;
        }
        case IDM_LOAD_RULES: {
            std::wstring f = BrowseFile(hWnd, L"Load rules & tokens", L"JSON (*.json)\0*.json\0All\0*.*\0");
            if (!f.empty()) {
                g_module->LoadRulesFromJson(f);
                RefreshParamList();
                RefreshRuleList();
                SetWindowTextW(g_hStatus, (L"Loaded: " + fs::path(f).filename().wstring()).c_str());
            }
            break;
        }
        case IDM_SAVE_RULES: {
            std::wstring f;
            {
                OPENFILENAMEW ofn = {};
                wchar_t file[MAX_PATH] = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hWnd;
                ofn.lpstrFilter = L"JSON (*.json)\0*.json\0All\0*.*\0";
                ofn.lpstrFile = file;
                ofn.nMaxFile = MAX_PATH;
                ofn.lpstrTitle = L"Save rules & tokens";
                ofn.lpstrDefExt = L"json";
                ofn.Flags = OFN_OVERWRITEPROMPT;
                if (GetSaveFileNameW(&ofn)) f = file;
            }
            if (!f.empty()) {
                ReadInputs();
                g_module->SaveRulesToJson(f);
                SetWindowTextW(g_hStatus, (L"Saved: " + fs::path(f).filename().wstring()).c_str());
            }
            break;
        }
        case IDM_PREVIEW: DoPreview(); break;
        case IDM_EXECUTE: DoExecute(); break;
        }
        return 0;
    }

    case WM_CLOSE:
        ReadInputs();
        g_module->SaveConfig();
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        g_hWndMain = nullptr;
        g_module = nullptr;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

//==============================================================================
// ShowReleasePackageWindow
//==============================================================================

void ShowReleasePackageWindow(HWND parent, Release_PackageModule* mod) {
    if (g_hWndMain) { SetForegroundWindow(g_hWndMain); return; }
    g_module = mod;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"Release_PackageWindow";
    RegisterClassExW(&wc);

    g_hWndMain = CreateWindowExW(0, L"Release_PackageWindow", L"7z Extractor",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 888, 680,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);

    ShowWindow(g_hWndMain, SW_SHOW);
    UpdateWindow(g_hWndMain);
}