//==============================================================================
// RomSeparatorWindow.cpp - ROM Separator window implementation
//==============================================================================

#include "RomSeparatorWindow.h"

#include "RomSeparatorModule.h"
#include "RomSeparatorLogic.h"

#include "../../Core/Logger.h"
#include "../../Services/TranslationService.h"

#include <shellapi.h>
#include <string>
#include <stdexcept>
#include <filesystem>

namespace fs = std::filesystem;

//==============================================================================
// Anonymous Helpers
//==============================================================================

namespace {
    constexpr int IDC_ROM_NAME    = 101;
    constexpr int IDC_ROM_PATH    = 102;
    constexpr int IDC_ROM_SIZE    = 103;
    constexpr int IDC_ROM1_LABEL  = 104;
    constexpr int IDC_ROM2_LABEL  = 105;
    constexpr int IDC_SPLIT_COMBO = 106;
    constexpr int IDC_OUTPUT_BTN  = 107;
    constexpr int IDC_OUTPUT_PATH = 108;
    constexpr int IDC_GENERATE    = 109;

    void CreateLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id = 0) {
        CreateWindowW(
            L"STATIC",
            text,
            WS_VISIBLE | WS_CHILD,
            x, y, w, h,
            parent,
            (HMENU)(INT_PTR)id,
            nullptr,
            nullptr
        );
    }

    HWND CreateValueLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id) {
        return CreateWindowW(
            L"STATIC",
            text,
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            x, y, w, h,
            parent,
            (HMENU)(INT_PTR)id,
            nullptr,
            nullptr
        );
    }

    std::wstring ToWStringULongLong(unsigned long long value) {
        return std::to_wstring(value);
    }
}

//==============================================================================
// Constructor / Destructor
//==============================================================================

RomSeparatorWindow::RomSeparatorWindow()
    : m_hInstance(nullptr),
      m_hwnd(nullptr),
      m_owner(nullptr),
      m_module(nullptr),
      m_hRomName(nullptr),
      m_hRomPath(nullptr),
      m_hRomSize(nullptr),
      m_hRom1Label(nullptr),
      m_hRom2Label(nullptr),
      m_hSplitCombo(nullptr),
      m_hOutputBtn(nullptr),
      m_hOutputPath(nullptr),
      m_hGenerateBtn(nullptr),
      m_outputMode(RomSeparatorOutputMode::HostFolder),
      m_confirmOverwrite(true),
      m_showSuccessMessage(true),
      m_verboseLog(true) {
}

RomSeparatorWindow::~RomSeparatorWindow() {
}

//==============================================================================
// Create
//==============================================================================

bool RomSeparatorWindow::Create(HINSTANCE hInstance, HWND owner, RomSeparatorModule* module) {
    m_hInstance = hInstance;
    m_owner = owner;
    m_module = module;

    const wchar_t kClassName[] = L"RomSeparatorWindowClass";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = RomSeparatorWindow::WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(
        0,
        kClassName,
        TranslationService::Get()->Tr(L"RomSeparator", L"ROM Separator").c_str(),
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        700, 480,
        owner,
        nullptr,
        hInstance,
        this
    );

    if (!m_hwnd) {
        return false;
    }

    BuildMainWindow(m_hwnd);
    return true;
}

//==============================================================================
// Window Lifecycle
//==============================================================================

void RomSeparatorWindow::Show() {
    if (!m_hwnd) {
        return;
    }

    ShowWindow(m_hwnd, SW_SHOWNORMAL);
    UpdateWindow(m_hwnd);
}

void RomSeparatorWindow::BringToFront() {
    if (!m_hwnd) {
        return;
    }

    ShowWindow(m_hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(m_hwnd);
}

void RomSeparatorWindow::OpenRom(const RomSeparatorOpenRequest& request) {
    m_romPath = request.romPath;
    m_outputMode = request.outputMode;
    m_confirmOverwrite = request.confirmOverwrite;
    m_showSuccessMessage = request.showSuccessMessage;
    m_verboseLog = request.verboseLog;

    m_splits.clear();

    try {
        if (fs::exists(m_romPath) && fs::is_regular_file(m_romPath)) {
            m_splits = RomSeparatorLogic::GetPossibleSplits(fs::file_size(m_romPath));
        }
    } catch (...) {
        m_splits.clear();
    }

    if (m_hSplitCombo) {
        SendMessageW(m_hSplitCombo, CB_RESETCONTENT, 0, 0);

        for (const auto& split : m_splits) {
            std::wstring text =
                std::to_wstring(split.leftSize / (1024 * 1024)) + L"+" +
                std::to_wstring(split.rightSize / (1024 * 1024));
            SendMessageW(m_hSplitCombo, CB_ADDSTRING, 0, (LPARAM)text.c_str());
        }

        if (!m_splits.empty()) {
            SendMessageW(m_hSplitCombo, CB_SETCURSEL, 0, 0);
        }
    }

    RefreshAll();
}

HWND RomSeparatorWindow::GetHwnd() const {
    return m_hwnd;
}

//==============================================================================
// WndProc
//==============================================================================

LRESULT CALLBACK RomSeparatorWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    RomSeparatorWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<RomSeparatorWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<RomSeparatorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT RomSeparatorWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_OUTPUT_BTN:
            OnToggleOutputMode();
            return 0;

        case IDC_SPLIT_COMBO:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                OnSplitChanged();
            }
            return 0;

        case IDC_GENERATE:
            OnGenerate();
            return 0;
        }
        break;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        if (m_hwnd == hwnd) {
            m_hwnd = nullptr;
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

//==============================================================================
// Build UI
//==============================================================================

void RomSeparatorWindow::BuildMainWindow(HWND hwnd) {
    auto Tr = [](const wchar_t* text) {
        return TranslationService::Get()->Tr(L"RomSeparator", text);
    };
    CreateLabel(hwnd, Tr(L"Input ROM").c_str(), 20, 20, 300, 20);
    CreateLabel(hwnd, Tr(L"Name:").c_str(), 20, 50, 80, 20);
    CreateLabel(hwnd, Tr(L"Path:").c_str(), 20, 90, 80, 20);
    CreateLabel(hwnd, Tr(L"Original Size:").c_str(), 20, 170, 100, 20);

    m_hRomName = CreateValueLabel(hwnd, L"", 120, 50, 520, 20, IDC_ROM_NAME);

    m_hRomPath = CreateWindowW(
        L"EDIT",
        L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        120, 90, 520, 60,
        hwnd,
        (HMENU)(INT_PTR)IDC_ROM_PATH,
        nullptr,
        nullptr
    );

    m_hRomSize = CreateValueLabel(hwnd, L"", 120, 170, 200, 20, IDC_ROM_SIZE);

    CreateLabel(hwnd, Tr(L"Split Layout:").c_str(), 20, 230, 100, 20);

    m_hRom1Label = CreateWindowW(
        L"STATIC",
        Tr(L"Rom1: ?").c_str(),
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        120, 230, 120, 20,
        hwnd,
        (HMENU)(INT_PTR)IDC_ROM1_LABEL,
        nullptr,
        nullptr
    );

    m_hSplitCombo = CreateWindowW(
        L"COMBOBOX",
        L"",
        CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
        250, 226, 120, 300,
        hwnd,
        (HMENU)(INT_PTR)IDC_SPLIT_COMBO,
        nullptr,
        nullptr
    );

    m_hRom2Label = CreateWindowW(
        L"STATIC",
        Tr(L"Rom2: ?").c_str(),
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        390, 230, 120, 20,
        hwnd,
        (HMENU)(INT_PTR)IDC_ROM2_LABEL,
        nullptr,
        nullptr
    );

    CreateLabel(hwnd, Tr(L"Output Location:").c_str(), 20, 290, 120, 20);

    m_hOutputBtn = CreateWindowW(
        L"BUTTON",
        Tr(L"Host").c_str(),
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        150, 286, 70, 28,
        hwnd,
        (HMENU)(INT_PTR)IDC_OUTPUT_BTN,
        nullptr,
        nullptr
    );

    m_hOutputPath = CreateWindowW(
        L"EDIT",
        L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        230, 285, 410, 60,
        hwnd,
        (HMENU)(INT_PTR)IDC_OUTPUT_PATH,
        nullptr,
        nullptr
    );

    m_hGenerateBtn = CreateWindowW(
        L"BUTTON",
        Tr(L"Generate").c_str(),
        WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        270, 380, 120, 36,
        hwnd,
        (HMENU)(INT_PTR)IDC_GENERATE,
        nullptr,
        nullptr
    );

    RefreshAll();
}

//==============================================================================
// Refresh
//==============================================================================

void RomSeparatorWindow::RefreshAll() {
    RefreshRomInfo();
    RefreshSplitInfo();
    RefreshOutputInfo();
}

void RomSeparatorWindow::RefreshRomInfo() {
    if (!m_hwnd) {
        return;
    }

    std::wstring romName;
    std::wstring romPath;
    std::wstring romSize;

    try {
        if (HasLoadedRom()) {
            romName = m_romPath.filename().wstring();
            romPath = m_romPath.wstring();
            romSize = ToWStringULongLong(fs::file_size(m_romPath));
        }
    } catch (...) {
    }

    SetWindowTextW(m_hRomName, romName.c_str());
    SetWindowTextW(m_hRomPath, romPath.c_str());
    SetWindowTextW(m_hRomSize, romSize.c_str());
}

void RomSeparatorWindow::RefreshSplitInfo() {
    auto Tr = [](const wchar_t* text) {
        return TranslationService::Get()->Tr(L"RomSeparator", text);
    };
    std::wstring leftText = Tr(L"Rom1: ?");
    std::wstring rightText = Tr(L"Rom2: ?");

    if (!m_splits.empty()) {
        try {
            SplitOption split = GetSelectedSplit();
            leftText = Tr(L"Rom1: ") + RomSeparatorLogic::SizeToMBString(split.leftSize);
            rightText = Tr(L"Rom2: ") + RomSeparatorLogic::SizeToMBString(split.rightSize);
        } catch (...) {
        }
    }

    SetWindowTextW(m_hRom1Label, leftText.c_str());
    SetWindowTextW(m_hRom2Label, rightText.c_str());
}

void RomSeparatorWindow::RefreshOutputInfo() {
    if (!m_hOutputBtn || !m_hOutputPath) {
        return;
    }

    SetWindowTextW(
        m_hOutputBtn,
        TranslationService::Get()->Tr(L"RomSeparator",
            (m_outputMode == RomSeparatorOutputMode::HostFolder) ? L"Host" : L"Source").c_str()
    );

    std::wstring text;
    try {
        if (HasLoadedRom() && !m_splits.empty()) {
            auto outputPaths = RomSeparatorLogic::BuildOutputPaths(
                GetCurrentOutputDir(),
                GetSelectedSplit()
            );

            text = outputPaths.first.wstring() + L"\r\n" + outputPaths.second.wstring();
        }
    } catch (...) {
    }

    SetWindowTextW(m_hOutputPath, text.c_str());
}

//==============================================================================
// Split
//==============================================================================

void RomSeparatorWindow::OnSplitChanged() {
    RefreshSplitInfo();
    RefreshOutputInfo();
}

void RomSeparatorWindow::OnToggleOutputMode() {
    m_outputMode = (m_outputMode == RomSeparatorOutputMode::HostFolder)
        ? RomSeparatorOutputMode::SourceFolder
        : RomSeparatorOutputMode::HostFolder;

    if (m_module) {
        m_module->SetDefaultOutputMode(m_outputMode);
        m_module->SaveConfig();
    }

    RefreshOutputInfo();
}

//==============================================================================
// Generate
//==============================================================================

void RomSeparatorWindow::OnGenerate() {
    auto Tr = [](const wchar_t* text) {
        return TranslationService::Get()->Tr(L"RomSeparator", text);
    };

    if (!HasLoadedRom()) {
        MessageBoxW(
            m_hwnd,
            Tr(L"No ROM file loaded.").c_str(),
            Tr(L"ROM Separator").c_str(),
            MB_OK | MB_ICONWARNING
        );
        return;
    }

    if (m_splits.empty()) {
        MessageBoxW(
            m_hwnd,
            Tr(L"This ROM size does not support splitting.").c_str(),
            Tr(L"ROM Separator").c_str(),
            MB_OK | MB_ICONWARNING
        );
        return;
    }

    SplitOption split;
    try {
        split = GetSelectedSplit();
    } catch (...) {
        MessageBoxW(
            m_hwnd,
            Tr(L"Please select a split option.").c_str(),
            Tr(L"ROM Separator").c_str(),
            MB_OK | MB_ICONWARNING
        );
        return;
    }

    fs::path hostBaseDir;
    try {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        hostBaseDir = fs::path(path).parent_path();
    } catch (...) {
        hostBaseDir = fs::current_path();
    }

    auto outputPaths = RomSeparatorLogic::BuildOutputPaths(GetCurrentOutputDir(), split);

    if (m_confirmOverwrite) {
        std::vector<fs::path> existingFiles;
        if (fs::exists(outputPaths.first)) {
            existingFiles.push_back(outputPaths.first);
        }
        if (fs::exists(outputPaths.second)) {
            existingFiles.push_back(outputPaths.second);
        }

        if (!existingFiles.empty()) {
            std::wstring msg = Tr(L"The following files already exist:\n");
            for (const auto& p : existingFiles) {
                msg += p.wstring() + L"\n";
            }
            msg += Tr(L"\nOverwrite?");

            int ret = MessageBoxW(
                m_hwnd,
                msg.c_str(),
                Tr(L"ROM Separator").c_str(),
                MB_ICONQUESTION | MB_YESNO
            );

            if (ret != IDYES) {
                if (m_verboseLog && m_module) {
                    Logger::WriteModuleLog(m_module->GetLogSourceName(), L"Generate cancelled by user due to existing output files.");
                }
                return;
            }
        }
    }

    try {
        RomSeparatorGenerateRequest request;
        request.romPath = m_romPath;
        request.split = split;
        request.outputMode = m_outputMode;
        request.confirmOverwrite = m_confirmOverwrite;

        RomSeparatorGenerateResult result = RomSeparatorLogic::Generate(request, hostBaseDir);

        if (m_verboseLog && m_module) {
            Logger::WriteModuleLog(m_module->GetLogSourceName(), L"Input ROM: " + m_romPath.wstring());
            Logger::WriteModuleLog(m_module->GetLogSourceName(), L"Selected split: "
                + RomSeparatorLogic::SizeToMBString(split.leftSize)
                + L"+"
                + RomSeparatorLogic::SizeToMBString(split.rightSize));
            Logger::WriteModuleLog(m_module->GetLogSourceName(), L"Created: " + result.outputPath1.wstring());
            Logger::WriteModuleLog(m_module->GetLogSourceName(), L"Created: " + result.outputPath2.wstring());
        }

        RefreshOutputInfo();

        if (m_showSuccessMessage) {
            std::wstring msg =
                Tr(L"ROM split complete:\n") +
                result.outputPath1.wstring() +
                L"\n" +
                result.outputPath2.wstring();

            MessageBoxW(
                m_hwnd,
                msg.c_str(),
                Tr(L"ROM Separator").c_str(),
                MB_OK | MB_ICONINFORMATION
            );
        }
    }
    catch (const std::exception& ex) {
        std::wstring reason(ex.what(), ex.what() + strlen(ex.what()));
        std::wstring msg = Tr(L"Failed to split ROM file.\n\nReason: ") + reason;

        if (m_module) {
            Logger::WriteModuleLog(m_module->GetLogSourceName(), L"ERROR: " + msg);
        }

        MessageBoxW(
            m_hwnd,
            msg.c_str(),
            Tr(L"ROM Separator").c_str(),
            MB_OK | MB_ICONERROR
        );
    }
    catch (...) {
        if (m_module) {
            Logger::WriteModuleLog(m_module->GetLogSourceName(), L"ERROR: Unknown exception in RomSeparatorWindow::OnGenerate.");
        }

        MessageBoxW(
            m_hwnd,
            Tr(L"Failed to split ROM file.").c_str(),
            Tr(L"ROM Separator").c_str(),
            MB_OK | MB_ICONERROR
        );
    }
}

bool RomSeparatorWindow::HasLoadedRom() const {
    return !m_romPath.empty();
}

fs::path RomSeparatorWindow::GetCurrentOutputDir() const {
    if (m_outputMode == RomSeparatorOutputMode::HostFolder) {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return fs::path(path).parent_path();
    }

    return m_romPath.parent_path();
}

SplitOption RomSeparatorWindow::GetSelectedSplit() const {
    int index = static_cast<int>(SendMessageW(m_hSplitCombo, CB_GETCURSEL, 0, 0));
    if (index < 0 || index >= static_cast<int>(m_splits.size())) {
        throw std::runtime_error("No split selected.");
    }

    return m_splits[static_cast<size_t>(index)];
}