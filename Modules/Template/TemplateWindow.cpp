//==============================================================================
// TemplateWindow.cpp - Template module window implementation
//==============================================================================

#include "TemplateWindow.h"
#include "TemplateLogic.h"
#include "../../Core/Logger.h"
#include "../../Services/TranslationService.h"

#include <windows.h>
#include <string>
#include <algorithm>

#define IDC_STATUS_TEXT 101
#define IDC_PROCESS_BTN 201

//==============================================================================
// Constructor
//==============================================================================

TemplateWindow::TemplateWindow(HINSTANCE hInstance) : m_hwnd(nullptr), m_hInstance(hInstance) {}
TemplateWindow::~TemplateWindow() {}

//==============================================================================
// Create
//==============================================================================

bool TemplateWindow::Create(HWND hParent) {
    const wchar_t kClassName[] = L"TemplateWindowClass";
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = TemplateWindow::WndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!GetClassInfoW(m_hInstance, kClassName, &wc)) RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(0, kClassName,
        TranslationService::Get()->Tr(L"Template", L"Template Module").c_str(), WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 200, hParent, nullptr, m_hInstance, this);
    
    if (m_hwnd) BuildUI();
    return (m_hwnd != nullptr);
}

//==============================================================================
// BuildUI
//==============================================================================

void TemplateWindow::BuildUI() {
    CreateWindowW(L"STATIC",
        TranslationService::Get()->Tr(L"Template", L"Template Module Ready").c_str(),
        WS_VISIBLE | WS_CHILD, 20, 20, 300, 20, m_hwnd, (HMENU)IDC_STATUS_TEXT, m_hInstance, nullptr);
    CreateWindowW(L"BUTTON",
        TranslationService::Get()->Tr(L"Template", L"Process").c_str(),
        WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 130, 100, 120, 36, m_hwnd, (HMENU)IDC_PROCESS_BTN, m_hInstance, nullptr);
}

void TemplateWindow::Show(int nCmdShow) { if (m_hwnd) ShowWindow(m_hwnd, nCmdShow); }

//==============================================================================
// Open
//==============================================================================

void TemplateWindow::Open(const TemplateOpenRequest& req, IHostContext* host) {
    m_request = req;
    m_info1 = TemplateLogic::NormalizeData(req.source1Path.wstring());
    m_info2 = TemplateLogic::NormalizeData(req.source2Path.wstring());
    RefreshInfoPanels();
}

void TemplateWindow::RefreshInfoPanels() {
    SetDlgItemTextW(m_hwnd, IDC_STATUS_TEXT,
        TranslationService::Get()->Tr(L"Template", L"Files Loaded. Ready to process.").c_str());
}

void TemplateWindow::UpdateOutputControls() {}

//==============================================================================
// WndProc
//==============================================================================

LRESULT CALLBACK TemplateWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* pCs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        TemplateWindow* pWnd = static_cast<TemplateWindow*>(pCs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWnd));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    TemplateWindow* self = reinterpret_cast<TemplateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    
    if (self) {
        switch (msg) {
            case WM_COMMAND:
                if (LOWORD(wParam) == IDC_PROCESS_BTN) {
                    self->OnGenerateClicked();
                    return 0;
                }
                break;
            case WM_DESTROY:
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

//==============================================================================
// Generate
//==============================================================================

void TemplateWindow::OnGenerateClicked() {
    Logger::WriteModuleLog(L"Template", L"Generate button clicked.");

    std::filesystem::path outputDir = TemplateLogic::GetOutputPath(m_request, m_info1, std::filesystem::current_path());
    TemplateGenerateResult result = TemplateLogic::Generate(m_request, m_info1, m_info2, outputDir);

    if (result.success) {
        MessageBoxW(m_hwnd,
            TranslationService::Get()->Tr(L"Template", L"Operation successful!").c_str(),
            TranslationService::Get()->Tr(L"Template", L"Success").c_str(), MB_ICONINFORMATION);
    } else {
        Logger::WriteModuleLog(L"Template", L"Operation failed.");
        MessageBoxW(m_hwnd,
            TranslationService::Get()->Tr(L"Template", L"Operation failed.").c_str(),
            TranslationService::Get()->Tr(L"Template", L"Error").c_str(), MB_ICONERROR);
    }
}

void TemplateWindow::HandleDrop(HDROP) {}