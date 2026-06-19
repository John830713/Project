//==============================================================================
// RomCombinerWindow.cpp - Rom Combiner window implementation
//==============================================================================

#include "RomCombinerWindow.h"
#include "RomCombinerLogic.h"
#include "../../Services/TranslationService.h"
#include <windows.h>
#include <string>
#include <cstring>
#include <algorithm>

#define IDC_ROM1_NAME 101
#define IDC_ROM1_PATH 102
#define IDC_ROM1_SIZE 103
#define IDC_ROM1_TYPE 104
#define IDC_ROM1_TRIM 105
#define IDC_ROM1_EFFECTIVE 106
#define IDC_ROM2_NAME 111
#define IDC_ROM2_PATH 112
#define IDC_ROM2_SIZE 113
#define IDC_ROM2_TYPE 114
#define IDC_ROM2_TRIM 115
#define IDC_ROM2_EFFECTIVE 116
#define IDC_SWAP_BTN 201
#define IDC_OUTPUT_BTN 202
#define IDC_OUTPUT_PATH 203
#define IDC_GENERATE 204

//==============================================================================
// Constructor / Destructor
//==============================================================================

RomCombinerWindow::RomCombinerWindow(HINSTANCE hInstance) : m_hwnd(nullptr), m_hInstance(hInstance) {}
RomCombinerWindow::~RomCombinerWindow() {}

//==============================================================================
// Create - registers window class and creates the window
//==============================================================================

bool RomCombinerWindow::Create(HWND hParent) {
    const wchar_t kClassName[] = L"RomCombinerWindowClass";
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = RomCombinerWindow::WndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!GetClassInfoW(m_hInstance, kClassName, &wc)) RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(0, kClassName,
        TranslationService::Get()->Tr(L"RomCombiner", L"ROM Combiner").c_str(), WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 500, hParent, nullptr, m_hInstance, this);
    
    if (m_hwnd) BuildUI();
    return (m_hwnd != nullptr);
}

//==============================================================================
// BuildUI - creates all child controls for the window
//==============================================================================

void RomCombinerWindow::BuildUI() {
    auto Tr = [](const wchar_t* text) {
        return TranslationService::Get()->Tr(L"RomCombiner", text);
    };
    auto CreateLabel = [&](const wchar_t* t, int x, int y, int w, int h) { 
        CreateWindowW(L"STATIC", t, WS_VISIBLE | WS_CHILD, x, y, w, h, m_hwnd, nullptr, m_hInstance, nullptr); 
    };
    auto CreateVal = [&](int id, int x, int y, int w, int h, bool edit) {
        return CreateWindowW(edit ? L"EDIT" : L"STATIC", L"", WS_VISIBLE | WS_CHILD | (edit ? WS_BORDER | ES_READONLY : 0), x, y, w, h, m_hwnd, (HMENU)(INT_PTR)id, m_hInstance, nullptr);
    };

    CreateLabel(Tr(L"Rom1").c_str(), 20, 20, 280, 20);
    CreateLabel(Tr(L"Name:").c_str(), 20, 50, 80, 20); CreateLabel(Tr(L"Path:").c_str(), 20, 90, 80, 20);
    CreateLabel(Tr(L"Original Size:").c_str(), 20, 170, 100, 20); CreateLabel(Tr(L"Detected Type:").c_str(), 20, 200, 100, 20);
    CreateLabel(Tr(L"Trim Needed:").c_str(), 20, 230, 100, 20); CreateLabel(Tr(L"Effective Size:").c_str(), 20, 260, 100, 20);

    CreateLabel(Tr(L"Rom2").c_str(), 360, 20, 280, 20);
    CreateLabel(Tr(L"Name:").c_str(), 360, 50, 80, 20); CreateLabel(Tr(L"Path:").c_str(), 360, 90, 80, 20);
    CreateLabel(Tr(L"Original Size:").c_str(), 360, 170, 100, 20); CreateLabel(Tr(L"Detected Type:").c_str(), 360, 200, 100, 20);
    CreateLabel(Tr(L"Trim Needed:").c_str(), 360, 230, 100, 20); CreateLabel(Tr(L"Effective Size:").c_str(), 360, 260, 100, 20);

    CreateVal(IDC_ROM1_NAME, 120, 50, 200, 20, false); CreateVal(IDC_ROM1_PATH, 120, 90, 200, 60, true);
    CreateVal(IDC_ROM1_SIZE, 120, 170, 200, 20, false); CreateVal(IDC_ROM1_TYPE, 120, 200, 200, 20, false);
    CreateVal(IDC_ROM1_TRIM, 120, 230, 200, 20, false); CreateVal(IDC_ROM1_EFFECTIVE, 120, 260, 200, 20, false);

    CreateVal(IDC_ROM2_NAME, 460, 50, 200, 20, false); CreateVal(IDC_ROM2_PATH, 460, 90, 200, 60, true);
    CreateVal(IDC_ROM2_SIZE, 460, 170, 200, 20, false); CreateVal(IDC_ROM2_TYPE, 460, 200, 200, 20, false);
    CreateVal(IDC_ROM2_TRIM, 460, 230, 200, 20, false); CreateVal(IDC_ROM2_EFFECTIVE, 460, 260, 200, 20, false);

    CreateWindowW(L"BUTTON", Tr(L"Swap").c_str(), WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 310, 140, 60, 30, m_hwnd, (HMENU)IDC_SWAP_BTN, m_hInstance, nullptr);
    CreateLabel(Tr(L"Output Location:").c_str(), 20, 330, 120, 20);
    hOutputBtn = CreateWindowW(L"BUTTON", Tr(L"Org").c_str(), WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 150, 326, 70, 28, m_hwnd, (HMENU)IDC_OUTPUT_BTN, m_hInstance, nullptr);
    hOutputPath = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_READONLY, 230, 320, 430, 50, m_hwnd, (HMENU)IDC_OUTPUT_PATH, m_hInstance, nullptr);
    CreateWindowW(L"BUTTON", Tr(L"Generate").c_str(), WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 280, 390, 120, 36, m_hwnd, (HMENU)IDC_GENERATE, m_hInstance, nullptr);
}

//==============================================================================
// Refresh / Display
//==============================================================================

void RomCombinerWindow::Show(int nCmdShow) { if (m_hwnd) ShowWindow(m_hwnd, nCmdShow); }

void RomCombinerWindow::Open(const RomCombinerOpenRequest& req, IHostContext* host) {
    m_request = req;
    
    std::filesystem::path p1 = std::filesystem::absolute(req.rom1Path);
    std::filesystem::path p2 = std::filesystem::absolute(req.rom2Path);
    
    m_info1 = RomCombinerLogic::NormalizeRom(p1.wstring());
    m_info2 = RomCombinerLogic::NormalizeRom(p2.wstring());
    
    RefreshInfoPanels();
    UpdateOutputControls();
}

void RomCombinerWindow::RefreshInfoPanels() {
    auto Set = [&](int id, std::wstring val) { SetDlgItemTextW(m_hwnd, id, val.c_str()); };
    Set(IDC_ROM1_NAME, m_info1.path.filename().wstring()); Set(IDC_ROM1_PATH, m_info1.path.wstring());
    Set(IDC_ROM1_SIZE, std::to_wstring(m_info1.originalSize) + L" bytes"); Set(IDC_ROM1_TYPE, m_info1.displayType);
    Set(IDC_ROM1_TRIM, m_info1.needsTrim ? L"Yes" : L"No"); Set(IDC_ROM1_EFFECTIVE, std::to_wstring(m_info1.effectiveSize) + L" bytes");
    Set(IDC_ROM2_NAME, m_info2.path.filename().wstring()); Set(IDC_ROM2_PATH, m_info2.path.wstring());
    Set(IDC_ROM2_SIZE, std::to_wstring(m_info2.originalSize) + L" bytes"); Set(IDC_ROM2_TYPE, m_info2.displayType);
    Set(IDC_ROM2_TRIM, m_info2.needsTrim ? L"Yes" : L"No"); Set(IDC_ROM2_EFFECTIVE, std::to_wstring(m_info2.effectiveSize) + L" bytes");
}

void RomCombinerWindow::UpdateOutputControls() {
    SetWindowTextW(hOutputBtn,
        TranslationService::Get()->Tr(L"RomCombiner", m_request.outputToOrg ? L"Org" : L"Rom").c_str());

    std::filesystem::path outputPath;
    if (m_request.outputToOrg) {
        wchar_t buffer[MAX_PATH];
        GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        outputPath = std::filesystem::path(buffer).parent_path();
    } else {
        outputPath = m_info1.path.parent_path();
    }

    SetWindowTextW(hOutputPath, outputPath.wstring().c_str());
}

//==============================================================================
// WndProc - window message handler
//==============================================================================

LRESULT CALLBACK RomCombinerWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    RomCombinerWindow* self = (msg == WM_NCCREATE) ? (RomCombinerWindow*)((CREATESTRUCTW*)lParam)->lpCreateParams : (RomCombinerWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    if (self) {
        switch (msg) {
            case WM_COMMAND:
                if (LOWORD(wParam) == IDC_SWAP_BTN) { std::swap(self->m_info1, self->m_info2); self->RefreshInfoPanels(); }
                else if (LOWORD(wParam) == IDC_OUTPUT_BTN) { self->m_request.outputToOrg = !self->m_request.outputToOrg; self->UpdateOutputControls(); }
                else if (LOWORD(wParam) == IDC_GENERATE) self->OnGenerateClicked();
                break;
            case WM_DESTROY: return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

//==============================================================================
// Generate - initiates the ROM combination process
//==============================================================================

void RomCombinerWindow::OnGenerateClicked() {
    auto Tr = [](const wchar_t* text) {
        return TranslationService::Get()->Tr(L"RomCombiner", text);
    };

    if (m_info1.path.empty() || m_info2.path.empty()) {
        MessageBoxW(m_hwnd, Tr(L"Please load two valid ROM files first.").c_str(), Tr(L"Error").c_str(), MB_ICONERROR);
        return;
    }

    std::filesystem::path outputDir = RomCombinerLogic::GetOutputPath(m_request, m_info1, std::filesystem::current_path());
    RomCombinerGenerateResult result = RomCombinerLogic::Generate(m_request, m_info1, m_info2, outputDir);

    if (result.success) {
        MessageBoxW(m_hwnd, Tr(L"ROM combination successful!").c_str(), Tr(L"Success").c_str(), MB_ICONINFORMATION);
    } else {
        std::wstring errMsg = Tr(L"Generation failed: ") + result.errorMessage;
        MessageBoxW(m_hwnd, errMsg.c_str(), Tr(L"Error").c_str(), MB_ICONERROR);
    }
}
void RomCombinerWindow::HandleDrop(HDROP) {}
// (unused stub -- drag-drop is handled via RomCombinerModule)