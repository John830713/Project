#include "MainWindow.h"
#include "PetConfig.h"
#include "../UI/SettingsDialog.h"
#include "../Core/ModuleManager.h"
#include "../Core/DropTypes.h"
#include "../Core/InputManager.h"
#include "../Services/TranslationService.h"
#include "../Modules/RemoteControl/RemoteControlModule.h"
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <cstdlib>

//==============================================================================
// SliderPopup - modeless popup with trackbar + edit for dual input
// Auto-closes when it loses focus (click-away).  No PostQuitMessage.
//==============================================================================

struct SliderPopupData {
    MainWindow* petWindow;
    HWND hTrackbar;
    HWND hEdit;
    HWND hStatic;
    int minVal;
    int maxVal;
    int current;
    WPARAM changeId;
    ULONGLONG creationTime;
};

static LRESULT CALLBACK SliderPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* data = reinterpret_cast<SliderPopupData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!data && msg != WM_CREATE)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* d = reinterpret_cast<SliderPopupData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));

        NONCLIENTMETRICSW ncm = { sizeof(ncm) };
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);

        RECT rc;
        GetClientRect(hwnd, &rc);

        d->hTrackbar = CreateWindowExW(0, TRACKBAR_CLASS, L"",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            12, 8, rc.right - 80, 26,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(d->hTrackbar, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(d->hTrackbar, TBM_SETRANGE, TRUE, MAKELPARAM(d->minVal, d->maxVal));
        SendMessageW(d->hTrackbar, TBM_SETPOS, TRUE, d->current);

        d->hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
            rc.right - 68, 8, 56, 24,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(d->hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

        wchar_t val[16];
        swprintf(val, 16, L"%d", d->current);
        SetWindowTextW(d->hEdit, val);

        d->hStatic = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            12, 38, rc.right - 24, 16,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(d->hStatic, WM_SETFONT, (WPARAM)hFont, TRUE);
        std::wstring rangeFmt = TranslationService::Get()->Tr(L"Pet", L"Range: %d ~ %d");
        swprintf(val, 16, rangeFmt.c_str(), d->minVal, d->maxVal);
        SetWindowTextW(d->hStatic, val);

        DeleteObject(hFont);
        return 0;
    }

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            if (data && data->petWindow) {
                // Ignore deactivation within the first 600ms (avoids spurious
                // WM_ACTIVATE during popup creation when the pet is WS_EX_TOPMOST)
                if (!data->creationTime || GetTickCount64() - data->creationTime < 600)
                    break;
                PostMessageW(data->petWindow->GetHWND(), MainWindow::WM_APP_SLIDER_CHANGE,
                             data->changeId, data->current);
            }
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_HSCROLL: {
        if ((HWND)lParam == data->hTrackbar) {
            data->current = (int)SendMessageW(data->hTrackbar, TBM_GETPOS, 0, 0);
            wchar_t val[16];
            swprintf(val, 16, L"%d", data->current);
            SetWindowTextW(data->hEdit, val);
            if (data->petWindow)
                PostMessageW(data->petWindow->GetHWND(), MainWindow::WM_APP_SLIDER_CHANGE,
                             data->changeId, data->current);
        }
        return 0;
    }

    case WM_COMMAND: {
        if (HIWORD(wParam) == EN_CHANGE && (HWND)lParam == data->hEdit) {
            wchar_t val[16];
            GetWindowTextW(data->hEdit, val, 16);
            int v = _wtoi(val);
            if (v < data->minVal) v = data->minVal;
            if (v > data->maxVal) v = data->maxVal;
            data->current = v;
            SendMessageW(data->hTrackbar, TBM_SETPOS, TRUE, v);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (data && data->petWindow)
            PostMessageW(data->petWindow->GetHWND(), MainWindow::WM_APP_SLIDER_CHANGE,
                         data->changeId, -1);
        return 0;

    case WM_NCDESTROY:
        delete data;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void MainWindow::ShowSliderPopup(HWND parent, const wchar_t* title, int current,
                                 int minVal, int maxVal, WPARAM changeId) {
    CloseSliderPopup();

    const wchar_t kClass[] = L"PetSliderPopupClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = SliderPopupProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
    RegisterClassW(&wc);

    auto* data = new SliderPopupData();
    data->petWindow = this;
    data->minVal = minVal;
    data->maxVal = maxVal;
    data->current = current;
    data->changeId = changeId;
    data->creationTime = GetTickCount64();

    POINT pt;
    GetCursorPos(&pt);
    int w = 280, h = 70;
    int x = pt.x;
    int y = pt.y - h - 10;
    if (y < 0) y = pt.y + 10;

    DWORD exStyle = WS_EX_TOOLWINDOW;
    if ((GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST))
        exStyle |= WS_EX_TOPMOST;

    m_hSliderPopup = CreateWindowExW(exStyle, kClass, title,
        WS_POPUP | WS_VISIBLE,
        x, y, w, h, nullptr, nullptr, GetModuleHandleW(nullptr), data);

    if (!m_hSliderPopup) {
        delete data;
    } else {
        SetForegroundWindow(m_hSliderPopup);
    }
}

void MainWindow::CloseSliderPopup() {
    if (m_hSliderPopup && IsWindow(m_hSliderPopup)) {
        DestroyWindow(m_hSliderPopup);
    }
    m_hSliderPopup = nullptr;
}

//==============================================================================
// MainWindow implementation
//==============================================================================

MainWindow::MainWindow()
    : m_hwnd(nullptr), m_host(nullptr), m_moduleManager(nullptr), m_inputManager(nullptr),
      m_gdiplusToken(0), m_petImage(nullptr), m_originalImage(nullptr),
      m_imageWidth(0), m_imageHeight(0),
      m_moveLocked(false), m_scalePercent(100), m_currentOpacity(255),
      m_moveEnabled(false), m_moveStep(3), m_moveSpeed(200), m_moveShuttle(false),
      m_moveDx(1), m_moveDy(1), m_movePauseMs(0),
      m_edgeSnapped(false), m_snapEdge(SNAP_NONE), m_snapIndicator(nullptr) {
    m_hSliderPopup = nullptr;
    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&m_gdiplusToken, &gsi, nullptr);
}

MainWindow::~MainWindow() {
    delete m_petImage;
    delete m_originalImage;
    delete m_snapIndicator;
    if (m_gdiplusToken)
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
}

bool MainWindow::LoadPetImage() {
    const wchar_t* candidates[] = { L"Pet.png", L"Pet\\Pet.png" };
    for (auto* path : candidates) {
        m_petImage = Gdiplus::Bitmap::FromFile(path);
        if (m_petImage && m_petImage->GetLastStatus() == Gdiplus::Ok) {
            m_imageWidth = m_petImage->GetWidth();
            m_imageHeight = m_petImage->GetHeight();
            m_originalImage = m_petImage->Clone(0, 0, m_imageWidth, m_imageHeight, m_petImage->GetPixelFormat());
            return true;
        }
        delete m_petImage;
        m_petImage = nullptr;
    }

    const int defaultSize = 64;
    m_petImage = new Gdiplus::Bitmap(defaultSize, defaultSize, PixelFormat32bppARGB);
    Gdiplus::Graphics g(m_petImage);
    Gdiplus::SolidBrush brush(Gdiplus::Color(180, 80, 200, 80));
    g.FillRectangle(&brush, 0, 0, defaultSize, defaultSize);
    m_imageWidth = defaultSize;
    m_imageHeight = defaultSize;
    m_originalImage = m_petImage->Clone(0, 0, defaultSize, defaultSize, m_petImage->GetPixelFormat());
    return true;
}

void MainWindow::ApplyWindowImage() {
    if (!m_hwnd || !m_petImage) return;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = nullptr;
    m_petImage->GetHBITMAP(Gdiplus::Color(0, 0, 0), &hBitmap);
    SelectObject(hdcMem, hBitmap);

    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = static_cast<BYTE>(m_currentOpacity);
    blend.AlphaFormat = AC_SRC_ALPHA;

    POINT ptZero = {0, 0};
    SIZE sizeWnd = {m_imageWidth, m_imageHeight};
    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    POINT ptPos = {rc.left, rc.top};

    UpdateLayeredWindow(m_hwnd, hdcScreen, &ptPos, &sizeWnd, hdcMem, &ptZero, 0, &blend, ULW_ALPHA);

    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

bool MainWindow::Create(
    HINSTANCE hInstance, int nCmdShow,
    IHostContext* host, ModuleManager* moduleManager,
    InputManager* inputManager, HWND& outHwnd,
    const std::wstring& windowTitle, const std::wstring& hintText,
    const std::wstring& appVersion, const std::wstring& appPurpose) {

    m_host = host;
    m_moduleManager = moduleManager;
    m_inputManager = inputManager;

    // Load language before any Tr() calls
    PetConfig::Data petCfg = PetConfig::Load();
    TranslationService::Get()->Load(petCfg.language);

    m_windowTitle = windowTitle.empty() ? TranslationService::Get()->Tr(L"Pet", L"Desktop Pet") : windowTitle;
    m_hintText = hintText;
    m_appVersion = appVersion.empty() ? L"1.0.0" : appVersion;
    m_appPurpose = appPurpose.empty() ? TranslationService::Get()->Tr(L"Pet", L"Modular desktop pet.") : appPurpose;

    LoadPetImage();

    m_currentOpacity = petCfg.opacity;
    m_scalePercent = 100;
    m_moveEnabled = petCfg.moveEnabled;
    m_moveStep = petCfg.moveStep;
    m_moveSpeed = petCfg.moveSpeed;
    m_moveShuttle = petCfg.moveShuttle;

    int x, y;
    if (petCfg.posX >= 0 && petCfg.posY >= 0) {
        x = petCfg.posX;
        y = petCfg.posY;
    } else {
        x = (GetSystemMetrics(SM_CXSCREEN) - m_imageWidth) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - m_imageHeight) / 2;
    }

    const wchar_t kClassName[] = L"PetMainWindowClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWindow::WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_ACCEPTFILES;
    if (petCfg.alwaysOnTop) exStyle |= WS_EX_TOPMOST;

    HWND hwnd = CreateWindowExW(
        exStyle, kClassName, m_windowTitle.c_str(),
        WS_POPUP | WS_VISIBLE,
        x, y, m_imageWidth, m_imageHeight,
        nullptr, nullptr, hInstance, this);

    if (!hwnd)
        return false;

    outHwnd = m_hwnd = hwnd;
    ApplyWindowImage();
    SyncMoveState();
    return true;
}

void MainWindow::SavePosition() {
    if (!m_hwnd) return;
    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    PetConfig::Data petCfg = PetConfig::Load();
    petCfg.posX = rc.left;
    petCfg.posY = rc.top;
    PetConfig::Save(petCfg);
}

void MainWindow::ReloadPetConfig() {
    PetConfig::Data cfg = PetConfig::Load();
    TranslationService::Get()->Load(cfg.language);
    m_currentOpacity = cfg.opacity;
    m_moveEnabled = cfg.moveEnabled;
    m_moveStep = cfg.moveStep;
    m_moveSpeed = cfg.moveSpeed;
    m_moveShuttle = cfg.moveShuttle;
    ApplyWindowImage();
    SyncMoveState();
}

void MainWindow::ToggleTopmost() {
    bool onTop = (GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    SetWindowPos(m_hwnd, onTop ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    PetConfig::Data cfg = PetConfig::Load();
    cfg.alwaysOnTop = !onTop;
    PetConfig::Save(cfg);
}

void MainWindow::SyncMoveState() {
    if (m_moveEnabled && m_hwnd) {
        if (m_moveDx == 0 && m_moveDy == 0) {
            m_moveDx = (rand() % 3) - 1;
            m_moveDy = (rand() % 3) - 1;
            if (m_moveDx == 0 && m_moveDy == 0) m_moveDx = 1;
        }
        m_movePauseMs = 0;
        StartMoveTimer();
    } else {
        StopMoveTimer();
    }
}

void MainWindow::StartMoveTimer() {
    StopMoveTimer();
    if (m_hwnd && m_moveSpeed > 0)
        SetTimer(m_hwnd, 1, m_moveSpeed, nullptr);
}

void MainWindow::StopMoveTimer() {
    if (m_hwnd)
        KillTimer(m_hwnd, 1);
}

void MainWindow::OnMoveTimer() {
    if (!m_hwnd || !m_moveEnabled) return;

    // Pause auto-move while snapped to edge
    if (m_edgeSnapped) return;

    // Handle pause state — pet occasionally rests
    if (m_movePauseMs > 0) {
        m_movePauseMs -= m_moveSpeed;
        return;
    }

    // 3% chance per tick to pause for 1–5 seconds
    if (rand() % 100 < 3) {
        m_movePauseMs = (rand() % 4000) + 1000;
        return;
    }

    // 15% chance per tick to pick a new random direction
    if (rand() % 100 < 15) {
        int ndx = (rand() % 3) - 1;
        int ndy = (rand() % 3) - 1;
        if (ndx != 0 || ndy != 0) {
            m_moveDx = ndx;
            m_moveDy = ndy;
        }
    }

    RECT rc;
    GetWindowRect(m_hwnd, &rc);

    int newX = rc.left + m_moveDx * m_moveStep;
    int newY = rc.top + m_moveDy * m_moveStep;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    if (m_moveShuttle) {
        if (newX + m_imageWidth < 0) newX = screenW;
        else if (newX > screenW) newX = -m_imageWidth;
        if (newY + m_imageHeight < 0) newY = screenH;
        else if (newY > screenH) newY = -m_imageHeight;
    } else {
        if (newX < 0) {
            newX = 0;
            m_moveDx = 1;
            m_moveDy += (rand() % 3) - 1;
            if (m_moveDy < -1) m_moveDy = -1;
            if (m_moveDy > 1) m_moveDy = 1;
        } else if (newX + m_imageWidth > screenW) {
            newX = screenW - m_imageWidth;
            m_moveDx = -1;
            m_moveDy += (rand() % 3) - 1;
            if (m_moveDy < -1) m_moveDy = -1;
            if (m_moveDy > 1) m_moveDy = 1;
        }
        if (newY < 0) {
            newY = 0;
            m_moveDy = 1;
            m_moveDx += (rand() % 3) - 1;
            if (m_moveDx < -1) m_moveDx = -1;
            if (m_moveDx > 1) m_moveDx = 1;
        } else if (newY + m_imageHeight > screenH) {
            newY = screenH - m_imageHeight;
            m_moveDy = -1;
            m_moveDx += (rand() % 3) - 1;
            if (m_moveDx < -1) m_moveDx = -1;
            if (m_moveDx > 1) m_moveDx = 1;
        }
    }

    SetWindowPos(m_hwnd, nullptr, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWindow::CreateSnapIndicator() {
    delete m_snapIndicator;
    m_snapIndicator = nullptr;

    const int s = SNAP_INDICATOR_SIZE;
    const int r = 4;

    m_snapIndicator = new Gdiplus::Bitmap(s, s, PixelFormat32bppARGB);
    Gdiplus::Graphics g(m_snapIndicator);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::GraphicsPath path;
    path.AddArc(1, 1, r * 2, r * 2, 180, 90);
    path.AddArc(s - 1 - r * 2, 1, r * 2, r * 2, 270, 90);
    path.AddArc(s - 1 - r * 2, s - 1 - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(1, s - 1 - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();

    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(200, 160, 160, 160));
    g.FillPath(&bgBrush, &path);

    Gdiplus::Pen borderPen(Gdiplus::Color(160, 120, 120, 120), 1.0f);
    g.DrawPath(&borderPen, &path);

    COLORREF ledColor = 0;
    if (m_moduleManager) {
        for (auto* mod : m_moduleManager->GetAllModules()) {
            if (wcscmp(mod->GetModuleName(), L"RemoteControl") == 0) {
                auto* rc = static_cast<RemoteControlModule*>(mod);
                if (rc->IsRunning()) ledColor = rc->GetLedColor();
                break;
            }
        }
    }
    Gdiplus::Color dotColor;
    if (ledColor)
        dotColor = Gdiplus::Color(255, GetRValue(ledColor), GetGValue(ledColor), GetBValue(ledColor));
    else
        dotColor = m_moveEnabled ?
            Gdiplus::Color(255, 100, 220, 100) :
            Gdiplus::Color(255, 180, 180, 180);
    Gdiplus::SolidBrush dotBrush(dotColor);
    int dotSize = s - 12;
    g.FillEllipse(&dotBrush, (s - dotSize) / 2, (s - dotSize) / 2, dotSize, dotSize);
}

void MainWindow::ApplySnapIndicator() {
    if (!m_hwnd || !m_snapIndicator) return;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = nullptr;
    m_snapIndicator->GetHBITMAP(Gdiplus::Color(0, 0, 0), &hBitmap);
    SelectObject(hdcMem, hBitmap);

    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    POINT ptZero = {0, 0};
    SIZE sizeWnd = {SNAP_INDICATOR_SIZE, SNAP_INDICATOR_SIZE};
    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    POINT ptPos = {rc.left, rc.top};

    UpdateLayeredWindow(m_hwnd, hdcScreen, &ptPos, &sizeWnd, hdcMem, &ptZero, 0, &blend, ULW_ALPHA);

    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

void MainWindow::CheckEdgeSnap() {
    if (!m_hwnd) return;

    auto findRcMod = [this]() -> RemoteControlModule* {
        if (!m_moduleManager) return nullptr;
        for (auto* mod : m_moduleManager->GetAllModules()) {
            if (wcscmp(mod->GetModuleName(), L"RemoteControl") == 0)
                return static_cast<RemoteControlModule*>(mod);
        }
        return nullptr;
    };

    RECT rc;
    GetWindowRect(m_hwnd, &rc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int distTop = rc.top;
    int distBottom = screenH - rc.bottom;
    int distLeft = rc.left;
    int distRight = screenW - rc.right;

    int threshold = (m_imageWidth + m_imageHeight) / 4;
    if (threshold < 40) threshold = 40;

    int d = distTop;
    int edge = SNAP_TOP;
    if (distBottom < d) { d = distBottom; edge = SNAP_BOTTOM; }
    if (distLeft < d) { d = distLeft; edge = SNAP_LEFT; }
    if (distRight < d) { d = distRight; edge = SNAP_RIGHT; }

    if (d > threshold) {
        if (m_edgeSnapped) {
            m_edgeSnapped = false;
            m_snapEdge = SNAP_NONE;
            KillTimer(m_hwnd, 2);
            if (auto* rc = findRcMod()) rc->ShowLed(true);
            SetWindowPos(m_hwnd, nullptr, rc.left, rc.top, m_imageWidth, m_imageHeight, SWP_NOZORDER | SWP_NOACTIVATE);
            ApplyWindowImage();
        }
        return;
    }

    int newX = rc.left;
    int newY = rc.top;

    switch (edge) {
    case SNAP_TOP:
        newY = 0;
        break;
    case SNAP_BOTTOM:
        newY = screenH - SNAP_INDICATOR_SIZE;
        break;
    case SNAP_LEFT:
        newX = 0;
        break;
    case SNAP_RIGHT:
        newX = screenW - SNAP_INDICATOR_SIZE;
        break;
    }

    if (auto* rc = findRcMod()) rc->ShowLed(false);

    SetWindowPos(m_hwnd, nullptr, newX, newY, SNAP_INDICATOR_SIZE, SNAP_INDICATOR_SIZE, SWP_NOZORDER | SWP_NOACTIVATE);
    m_edgeSnapped = true;
    m_snapEdge = edge;
    SetTimer(m_hwnd, 2, 250, nullptr);
    CreateSnapIndicator();
    ApplySnapIndicator();
}

void MainWindow::SetOpacity(int alpha) {
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    m_currentOpacity = alpha;
    ApplyWindowImage();
    PetConfig::Data cfg = PetConfig::Load();
    cfg.opacity = alpha;
    PetConfig::Save(cfg);
}

void MainWindow::SetScale(int percent) {
    if (percent < 10) percent = 10;
    if (percent > 500) percent = 500;
    if (!m_originalImage) return;

    delete m_petImage;
    m_petImage = nullptr;

    int origW = m_originalImage->GetWidth();
    int origH = m_originalImage->GetHeight();
    int newW = origW * percent / 100;
    int newH = origH * percent / 100;
    if (newW < 1) newW = 1;
    if (newH < 1) newH = 1;

    m_petImage = new Gdiplus::Bitmap(newW, newH, PixelFormat32bppARGB);
    Gdiplus::Graphics g(m_petImage);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(m_originalImage, 0, 0, newW, newH);

    m_imageWidth = newW;
    m_imageHeight = newH;
    m_scalePercent = percent;

    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    SetWindowPos(m_hwnd, nullptr, rc.left, rc.top, newW, newH, SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyWindowImage();
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self)
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST:
        return m_moveLocked ? HTCLIENT : HTCAPTION;

    case WM_EXITSIZEMOVE:
        CheckEdgeSnap();
        SavePosition();
        return 0;

    case WM_TIMER:
        if (wParam == 1) { OnMoveTimer(); return 0; }
        if (wParam == 2 && m_edgeSnapped) { CreateSnapIndicator(); ApplySnapIndicator(); return 0; }
        break;

    case WM_NCRBUTTONUP:
        if (wParam == HTCAPTION) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            OnContextMenu(hwnd, pt);
            return 0;
        }
        break;

    case WM_CONTEXTMENU: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        OnContextMenu(hwnd, pt);
        return 0;
    }

    case WM_DROPFILES:
        OnDropFiles(hwnd, reinterpret_cast<HDROP>(wParam));
        return 0;

    case WM_DESTROY:
        StopMoveTimer();
        KillTimer(m_hwnd, 2);
        PostQuitMessage(0);
        return 0;

    case WM_APP_SLIDER_CHANGE: {
        if (wParam == ID_PET_OPACITY) {
            if (lParam >= 0) SetOpacity((int)lParam);
        } else if (wParam == ID_PET_SCALE) {
            if (lParam >= 0) SetScale((int)lParam);
        } else if (wParam == ID_PET_MOVE_STEP) {
            if (lParam >= 0) {
                m_moveStep = (int)lParam;
                PetConfig::Data cfg = PetConfig::Load();
                cfg.moveStep = m_moveStep;
                PetConfig::Save(cfg);
            }
        } else if (wParam == ID_PET_MOVE_SPEED) {
            if (lParam >= 0) {
                m_moveSpeed = (int)lParam;
                PetConfig::Data cfg = PetConfig::Load();
                cfg.moveSpeed = m_moveSpeed;
                PetConfig::Save(cfg);
                if (m_moveEnabled) { StartMoveTimer(); }
            }
        }
        return 0;
    }

    case WM_APP_RELOAD_PETCONFIG:
        ReloadPetConfig();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void MainWindow::OnDropFiles(HWND hwnd, HDROP hDrop) {
    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    DropContext ctx;
    for (UINT i = 0; i < fileCount; ++i) {
        wchar_t path[MAX_PATH] = {};
        DragQueryFileW(hDrop, i, path, MAX_PATH);
        ctx.filePaths.push_back(path);
    }
    DragFinish(hDrop);

    if (!m_moduleManager)
        return;

    std::vector<ResolvedDropAction> actions = m_moduleManager->GetDropActions(ctx);
    if (actions.empty()) {
        MessageBoxW(hwnd,
            TranslationService::Get()->Tr(L"Pet", L"No modules available to handle the dropped files.").c_str(),
            m_windowTitle.c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    int selectedIndex = 0;
    if (actions.size() > 1) {
        selectedIndex = ShowDropActionMenu(hwnd, actions);
        if (selectedIndex < 0)
            return;
    }

    m_moduleManager->ExecuteDropAction(actions[(size_t)selectedIndex], ctx);
}

void MainWindow::OnContextMenu(HWND hwnd, POINT pt) {
    CloseSliderPopup();
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    auto Tr = [](const wchar_t* section, const wchar_t* text) {
        return TranslationService::Get()->Tr(section, text);
    };

    HMENU hPetMenu = CreatePopupMenu();
    if (hPetMenu) {
        AppendMenuW(hPetMenu, MF_STRING, ID_PET_TOPMOST, Tr(L"Pet", L"Always on Top").c_str());

        HMENU hMove = CreatePopupMenu();
        if (hMove) {
            AppendMenuW(hMove, MF_STRING, ID_PET_MOVE_STILL, Tr(L"Pet", L"Still").c_str());
            AppendMenuW(hMove, MF_STRING, ID_PET_MOVE_MOVING, Tr(L"Pet", L"Move").c_str());
            CheckMenuRadioItem(hMove, ID_PET_MOVE_STILL, ID_PET_MOVE_MOVING,
                               m_moveEnabled ? ID_PET_MOVE_MOVING : ID_PET_MOVE_STILL, MF_BYCOMMAND);
            AppendMenuW(hMove, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMove, MF_STRING, ID_PET_MOVE_STEP, Tr(L"Pet", L"Step...").c_str());
            AppendMenuW(hMove, MF_STRING, ID_PET_MOVE_SPEED, Tr(L"Pet", L"Speed...").c_str());
            AppendMenuW(hMove, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMove, MF_STRING, ID_PET_MOVE_SHUTTLE, Tr(L"Pet", L"Shuttle").c_str());
            if (m_moveShuttle) CheckMenuItem(hMove, ID_PET_MOVE_SHUTTLE, MF_CHECKED);

            AppendMenuW(hPetMenu, MF_STRING | MF_POPUP, (UINT_PTR)hMove, Tr(L"Pet", L"Move").c_str());
        }

        AppendMenuW(hPetMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hPetMenu, MF_STRING, ID_PET_OPACITY, Tr(L"Pet", L"Opacity...").c_str());
        AppendMenuW(hPetMenu, MF_STRING, ID_PET_SCALE, Tr(L"Pet", L"Scale...").c_str());

        if ((GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST))
            CheckMenuItem(hPetMenu, ID_PET_TOPMOST, MF_CHECKED);

        AppendMenuW(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hPetMenu, Tr(L"Pet", L"Pet").c_str());
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    }

    if (m_moduleManager) {
        std::vector<ContextMenuItem> items = m_moduleManager->GetContextMenuItems();
        if (!items.empty()) {
            HMENU hFunction = CreatePopupMenu();
            for (size_t i = 0; i < items.size(); ++i)
                AppendMenuW(hFunction, MF_STRING, ID_MENU_BASE + static_cast<UINT>(i), items[i].label.c_str());
            AppendMenuW(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hFunction, Tr(L"Pet", L"Function").c_str());
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        }
    }

    AppendMenuW(hMenu, MF_STRING, ID_SETTINGS, Tr(L"Pet", L"Settings...").c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_ABOUT, Tr(L"Pet", L"About Project...").c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_EXIT, Tr(L"Pet", L"Exit").c_str());

    UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd == ID_EXIT) {
        PostQuitMessage(0);
    } else if (cmd == ID_SETTINGS) {
        OpenSettings(hwnd);
    } else if (cmd == ID_ABOUT) {
        ShowAboutDialog(hwnd);
    } else if (cmd == ID_PET_TOPMOST) {
        ToggleTopmost();
    } else if (cmd == ID_PET_MOVE_STILL) {
        m_moveEnabled = false;
        SyncMoveState();
        PetConfig::Data cfg = PetConfig::Load();
        cfg.moveEnabled = false;
        PetConfig::Save(cfg);
    } else if (cmd == ID_PET_MOVE_MOVING) {
        m_moveEnabled = true;
        SyncMoveState();
        PetConfig::Data cfg = PetConfig::Load();
        cfg.moveEnabled = true;
        PetConfig::Save(cfg);
    } else if (cmd == ID_PET_MOVE_STEP) {
        ShowSliderPopup(hwnd, TranslationService::Get()->Tr(L"Pet", L"Step (px)").c_str(), m_moveStep, 1, 50, ID_PET_MOVE_STEP);
    } else if (cmd == ID_PET_MOVE_SPEED) {
        ShowSliderPopup(hwnd, TranslationService::Get()->Tr(L"Pet", L"Speed (ms)").c_str(), m_moveSpeed, 10, 1000, ID_PET_MOVE_SPEED);
    } else if (cmd == ID_PET_MOVE_SHUTTLE) {
        m_moveShuttle = !m_moveShuttle;
        PetConfig::Data cfg = PetConfig::Load();
        cfg.moveShuttle = m_moveShuttle;
        PetConfig::Save(cfg);
    } else if (cmd == ID_PET_OPACITY) {
        ShowSliderPopup(hwnd, TranslationService::Get()->Tr(L"Pet", L"Opacity (0-255)").c_str(), m_currentOpacity, 0, 255, ID_PET_OPACITY);
    } else if (cmd == ID_PET_SCALE) {
        ShowSliderPopup(hwnd, TranslationService::Get()->Tr(L"Pet", L"Scale (%)").c_str(), m_scalePercent, 10, 500, ID_PET_SCALE);
    } else if (cmd >= ID_MENU_BASE && cmd < ID_MENU_BASE + (m_moduleManager ? m_moduleManager->GetContextMenuItems().size() : 0)) {
        if (m_moduleManager) {
            auto items = m_moduleManager->GetContextMenuItems();
            m_moduleManager->ExecuteContextMenuItem(items[cmd - ID_MENU_BASE].itemId);
        }
    }
}

void MainWindow::OpenSettings(HWND hwnd) {
    CloseSliderPopup();
    StopMoveTimer();
    SettingsDialog dlg(hwnd, m_moduleManager);
    dlg.Show();
    SyncMoveState();
}

void MainWindow::ShowAboutDialog(HWND hwnd) {
    auto Tr = [](const wchar_t* section, const wchar_t* text) {
        return TranslationService::Get()->Tr(section, text);
    };

    std::wstring text;
    text += m_windowTitle + L" v" + m_appVersion + L"\n";
    text += m_appPurpose + L"\n\n";
    text += Tr(L"About", L"--- Loaded Modules ---") + L"\n";

    if (m_moduleManager) {
        for (auto* module : m_moduleManager->GetAllModules()) {
            text += std::wstring(L"\n") + module->GetDisplayName() + L" v" + module->GetVersion() + L"\n";
            const wchar_t* purpose = module->GetPurpose();
            if (purpose && *purpose)
                text += std::wstring(L"  ") + purpose + L"\n";
        }
    }

    std::wstring caption = Tr(L"About", L"About") + L" " + m_windowTitle;
    MessageBoxW(hwnd, text.c_str(), caption.c_str(), MB_OK | MB_ICONINFORMATION);
}

int MainWindow::ShowDropActionMenu(HWND hwnd, const std::vector<ResolvedDropAction>& actions) {
    if (actions.empty())
        return -1;

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu)
        return -1;

    for (size_t i = 0; i < actions.size(); ++i)
        AppendMenuW(hMenu, MF_STRING, static_cast<UINT>(1000 + i), actions[i].action.label.c_str());

    POINT pt;
    GetCursorPos(&pt);

    UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd < 1000 || cmd >= 1000 + actions.size())
        return -1;

    return static_cast<int>(cmd - 1000);
}
