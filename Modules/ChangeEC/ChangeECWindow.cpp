//==============================================================================
// ChangeECWindow.cpp - Change EC tool window implementation
//==============================================================================

#include "ChangeECWindow.h"
#include "ChangeECLogic.h"
#include "ChangeECModule.h"
#include "../../Core/Logger.h"
#include "../../Services/TranslationService.h"
#include <commctrl.h>

static const wchar_t* kClassName = L"ChangeECWindow";
enum { IDC_OFFSET = 201, IDC_OUTPUT_BTN, IDC_GENERATE, IDC_BIOS_INFO = 301, IDC_EC_INFO = 302, IDC_OUT_PATH = 303 };

//==============================================================================
// Constructor / Destructor
//==============================================================================

ChangeECWindow::ChangeECWindow(HINSTANCE hInstance) : m_hInstance(hInstance) {}
ChangeECWindow::~ChangeECWindow() { if (m_hwnd) DestroyWindow(m_hwnd); }

//==============================================================================
// Create
//==============================================================================

bool ChangeECWindow::Create(HWND hParent) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(0, kClassName,
        TranslationService::Get()->Tr(L"ChangeEC", L"Change EC Tool").c_str(), WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 680, 470, hParent, nullptr, m_hInstance, this);
    
    if (m_hwnd) BuildUI();
    return m_hwnd != nullptr;
}

//==============================================================================
// BuildUI
//==============================================================================

void ChangeECWindow::BuildUI() {
    auto Tr = [](const wchar_t* text) {
        return TranslationService::Get()->Tr(L"ChangeEC", text);
    };
    auto Lbl = [&](const wchar_t* t, int x, int y, int w) { CreateWindowW(L"STATIC", t, WS_VISIBLE | WS_CHILD, x, y, w, 20, m_hwnd, nullptr, m_hInstance, nullptr); };
    Lbl(Tr(L"BIOS ROM Info:").c_str(), 20, 20, 300);
    CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_READONLY, 20, 50, 280, 150, m_hwnd, (HMENU)IDC_BIOS_INFO, m_hInstance, nullptr);
    
    Lbl(Tr(L"EC BIN Info:").c_str(), 340, 20, 300);
    CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_READONLY, 340, 50, 280, 150, m_hwnd, (HMENU)IDC_EC_INFO, m_hInstance, nullptr);

    Lbl(Tr(L"Offset:").c_str(), 20, 240, 60);
    CreateWindowW(L"EDIT", L"0x6000", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 90, 236, 120, 24, m_hwnd, (HMENU)IDC_OFFSET, m_hInstance, nullptr);

    Lbl(Tr(L"Output:").c_str(), 20, 290, 60);
    CreateWindowW(L"BUTTON", Tr(L"Org").c_str(), WS_VISIBLE | WS_CHILD, 90, 286, 60, 28, m_hwnd, (HMENU)IDC_OUTPUT_BTN, m_hInstance, nullptr);
    CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_READONLY, 160, 286, 460, 50, m_hwnd, (HMENU)IDC_OUT_PATH, m_hInstance, nullptr);

    CreateWindowW(L"BUTTON", Tr(L"Generate").c_str(), WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 270, 360, 120, 36, m_hwnd, (HMENU)IDC_GENERATE, m_hInstance, nullptr);
}

//==============================================================================
// Open
//==============================================================================

void ChangeECWindow::Open(const ChangeECOpenRequest& req, IHostContext* host) {
    m_request = req;
    m_host = host;
    RefreshInfo();
}

//==============================================================================
// RefreshInfo
//==============================================================================

void ChangeECWindow::RefreshInfo() {
    SetDlgItemTextW(m_hwnd, IDC_BIOS_INFO, m_request.biosPath.c_str());
    SetDlgItemTextW(m_hwnd, IDC_EC_INFO, m_request.ecPath.c_str());
    std::filesystem::path outDir = m_outputToOrg ? std::filesystem::current_path() : std::filesystem::path(m_request.biosPath).parent_path();
    std::filesystem::path outPath = outDir / (std::filesystem::path(m_request.biosPath).stem().wstring() + L"_mod.rom");
    SetDlgItemTextW(m_hwnd, IDC_OUT_PATH, outPath.wstring().c_str());
}

//==============================================================================
// Generate
//==============================================================================

void ChangeECWindow::OnGenerate() {
    wchar_t buf[64];
    GetDlgItemTextW(m_hwnd, IDC_OFFSET, buf, 64);
    unsigned long offset = std::wcstoul(buf, nullptr, 0);
    
    std::filesystem::path outDir = m_outputToOrg ? std::filesystem::current_path() : std::filesystem::path(m_request.biosPath).parent_path();
    if (ChangeECLogic::Generate(m_request, offset, outDir, L"ChangeEC")) {
        MessageBoxW(m_hwnd,
            TranslationService::Get()->Tr(L"ChangeEC", L"Generate Success!").c_str(),
            TranslationService::Get()->Tr(L"ChangeEC", L"Done").c_str(), MB_OK);
    } else {
        MessageBoxW(m_hwnd,
            TranslationService::Get()->Tr(L"ChangeEC", L"Generate Failed!").c_str(),
            TranslationService::Get()->Tr(L"ChangeEC", L"Error").c_str(), MB_ICONERROR);
    }
}

void ChangeECWindow::Show(int nCmdShow) { ShowWindow(m_hwnd, nCmdShow); }

//==============================================================================
// WndProc
//==============================================================================

LRESULT CALLBACK ChangeECWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ChangeECWindow* self = (msg == WM_NCCREATE) ? (ChangeECWindow*)((CREATESTRUCTW*)lParam)->lpCreateParams : (ChangeECWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    if (self) {
        if (msg == WM_COMMAND) {
            if (LOWORD(wParam) == IDC_GENERATE) self->OnGenerate();
            if (LOWORD(wParam) == IDC_OUTPUT_BTN) {
                self->m_outputToOrg = !self->m_outputToOrg;
                SetWindowTextW((HWND)lParam,
                    TranslationService::Get()->Tr(L"ChangeEC", self->m_outputToOrg ? L"Org" : L"Rom").c_str());
                self->RefreshInfo(); 
            }
        }
        if (msg == WM_DESTROY) return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}