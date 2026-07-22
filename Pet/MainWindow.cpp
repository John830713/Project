#include "MainWindow.h"
#include "PetConfig.h"
#include "../UI/SettingsDialog.h"
#include "../UI/MenuOrderDialog.h"
#include "../Core/ModuleManager.h"
#include "../Core/DropTypes.h"
#include <cstdlib>
#include "../Core/InputManager.h"
#include "../Core/DebugConsole.h"
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
        LOGFONTW lf = ncm.lfMessageFont;
        wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Microsoft JhengHei");
        HFONT hFont = CreateFontIndirectW(&lf);

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
    : m_hwnd(nullptr), m_hSliderPopup(nullptr), m_hMenuTooltip(nullptr),
      m_hContextPopup(nullptr), m_hSubPopup(nullptr), m_hMovePopup(nullptr), m_hOpacityPopup(nullptr), m_hScalePopup(nullptr),
      m_hStepPopup(nullptr), m_hSpeedPopup(nullptr)
#if DEBUG_CONSOLE
      , m_moveTestPetPopup(nullptr)
#endif
      ,
      m_contextOpacity(255), m_contextScale(100),
      m_contextMoveStep(3), m_contextMoveSpeed(200),
      m_host(nullptr), m_moduleManager(nullptr), m_inputManager(nullptr),
      m_gdiplusToken(0), m_petImage(nullptr), m_originalImage(nullptr),
      m_imageWidth(0), m_imageHeight(0),
      m_moveLocked(false), m_scalePercent(100), m_currentOpacity(255),
      m_moveEnabled(false), m_moveStep(3), m_moveSpeed(200), m_moveShuttle(false),
      m_moveDx(1), m_moveDy(1), m_movePauseMs(0),
      m_edgeSnapped(false), m_snapEdge(SNAP_NONE), m_snapIndicator(nullptr) {
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
    if (!m_hwnd || !m_petImage) { DBG(L"ApplyWindowImage SKIP  hwnd=%d petImage=%p", !!m_hwnd, m_petImage); return; }

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
    m_scalePercent = petCfg.scalePercent;
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

#if DEBUG_CONSOLE
    SetTimer(m_hwnd, 99, 3000, nullptr);
#endif

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
    DBG(L"SetOpacity(%d)  prev=%d  petImage=%p origImage=%p", alpha, m_currentOpacity, m_petImage, m_originalImage);
    m_currentOpacity = alpha;
    ApplyWindowImage();
    PetConfig::Data cfg = PetConfig::Load();
    cfg.opacity = alpha;
    PetConfig::Save(cfg);
}

void MainWindow::SetScale(int percent) {
    if (percent < 25) percent = 25;
    if (percent > 250) percent = 250;
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
#if DEBUG_CONSOLE
        if (wParam >= 99 && wParam <= 200) {
            static int step = 0;
            int timerId = (int)wParam;
            KillTimer(m_hwnd, timerId);
            DBG(L"[AUTOTEST] step=%d timerId=%d", step, timerId);

            switch (step) {
            case 0: {
                // Open context menu at pet corner
                RECT rc;
                GetWindowRect(m_hwnd, &rc);
                POINT pt = { rc.left + 4, rc.bottom - 4 };
                if (pt.x < 0) pt.x = 0; if (pt.y < 0) pt.y = 0;
                ShowContextPopup(m_hwnd, pt);
                step++;
                SetTimer(m_hwnd, 100, 600, nullptr);
                DBG(L"[AUTOTEST] ShowContextPopup at (%d,%d)", pt.x, pt.y);
                return 0;
            }
            case 1: {
                // Click "Pet" item in context menu (i=0, y range: 4..4+26)
                if (m_hContextPopup) {
                    LPARAM lp = MAKELPARAM(6, 6);
                    PostMessageW(m_hContextPopup, WM_LBUTTONDOWN, 0, lp);
                    DBG(L"[AUTOTEST] sent LM to ctxpop for Pet item");
                }
                step++;
                SetTimer(m_hwnd, 101, 800, nullptr);
                return 0;
            }
            case 2: {
                m_moveTestPetPopup = m_hSubPopup;
                DBG(L"[AUTOTEST] saved PetPopup %p, clicking Move item", m_moveTestPetPopup);
                if (m_moveTestPetPopup) {
                    // "Move ▸" item at y=30..56, click at y=40
                    PostMessageW(m_moveTestPetPopup, WM_LBUTTONDOWN, 0, MAKELPARAM(40, 42));
                    DBG(L"[AUTOTEST] sent WM_LBUTTONDOWN to PetPopup at (40,42)");
                }
                step++;
                SetTimer(m_hwnd, 102, 800, nullptr);
                return 0;
            }
            case 3: {
                HWND hMoveSub = m_hMovePopup;
                RECT rcMove = {};
                if (hMoveSub && IsWindow(hMoveSub)) {
                    GetWindowRect(hMoveSub, &rcMove);
                    DBG(L"[AUTOTEST] Move submenu %p pos=(%d,%d,%d,%d)",
                        hMoveSub, rcMove.left, rcMove.top, rcMove.right, rcMove.bottom);
                } else {
                    DBG(L"[AUTOTEST] Move submenu NOT created!");
                }
                RECT rcPet = {};
                if (m_moveTestPetPopup && IsWindow(m_moveTestPetPopup)) {
                    GetWindowRect(m_moveTestPetPopup, &rcPet);
                    DBG(L"[AUTOTEST] PetPopup %p pos=(%d,%d,%d,%d)",
                        m_moveTestPetPopup, rcPet.left, rcPet.top, rcPet.right, rcPet.bottom);
                }
                if (rcMove.left && rcPet.left) {
                    int dx = rcMove.left - rcPet.right;
                    DBG(L"[AUTOTEST] Move x offset from PetPopup right edge: %d", dx);
                }
                step++;
                SetTimer(m_hwnd, 103, 500, nullptr);
                return 0;
            }
            case 4: {
                m_moveTestPetPopup = m_hSubPopup;
                // Directly create Step slider popup (bypassing hover/TrackMouseEvent
                // which fails in headless testing because real mouse is not over window)
                m_contextMoveStep = m_moveStep;
                m_moveStep = 5; // starting value
                m_contextMoveStep = m_moveStep;
                POINT scr = {0, 0};
                if (m_hMovePopup && IsWindow(m_hMovePopup)) {
                    RECT rc;
                    GetWindowRect(m_hMovePopup, &rc);
                    scr = { rc.right + 2, rc.top + 60 };
                }
                CloseStepPopup();
                CloseSpeedPopup();
                OpenSliderSubPopup(m_hwnd, this, scr, 200,
                    &m_contextMoveStep, 1, 50,
                    &m_hStepPopup, SliderKind::Step);
                step++;
                SetTimer(m_hwnd, 104, 800, nullptr);
                return 0;
            }
            case 5: {
                HWND hStep = m_hStepPopup;
                DBG(L"[AUTOTEST] m_hStepPopup=%p valid=%d  m_moveStep=%d  m_contextMoveStep=%d",
                    hStep, hStep ? IsWindow(hStep) : 0, m_moveStep, m_contextMoveStep);
                if (hStep && IsWindow(hStep)) {
                    HWND hTrackbar = FindWindowExW(hStep, nullptr, TRACKBAR_CLASSW, nullptr);
                    DBG(L"[AUTOTEST] trackbar=%p", hTrackbar);
                    if (hTrackbar) {
                        constexpr int TEST_VALUE = 42;
                        SendMessageW(hTrackbar, TBM_SETPOS, TRUE, TEST_VALUE);
                        SendMessageW(hStep, WM_HSCROLL, 0, (LPARAM)hTrackbar);
                        DBG(L"[AUTOTEST] trackbar → %d, m_contextMoveStep=%d",
                            TEST_VALUE, m_contextMoveStep);
                    }
                }
                step++;
                SetTimer(m_hwnd, 105, 300, nullptr);
                return 0;
            }
            case 6: {
                HWND hPet = m_moveTestPetPopup;
                DBG(L"[AUTOTEST] sending WA_INACTIVE to PetPopup=%p  (StepCtx=%d  Step=%d)",
                    hPet, m_contextMoveStep, m_moveStep);
                if (hPet && IsWindow(hPet)) {
                    // Use non-null lParam so it doesn't match null popup handles
                    SendMessageW(hPet, WM_ACTIVATE, WA_INACTIVE, (LPARAM)(ULONG_PTR)1);
                }
                step++;
                SetTimer(m_hwnd, 106, 500, nullptr);
                return 0;
            }
            case 7: {
                bool passed = (m_moveStep == 42);
                DBG(L"[AUTOTEST] m_moveStep=%d  EXPECTED=42  %s",
                    m_moveStep, passed ? L"PASS" : L"FAIL");
                DBG(L"[AUTOTEST] final: ctx=%p pet=%p move=%p step=%p",
                    m_hContextPopup, m_hSubPopup, m_hMovePopup, m_hStepPopup);
                DBG(L"[AUTOTEST] done, exiting");
                PostQuitMessage(0);
                return 0;
            }
            }
            return 0;
        }
#endif
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

    case WM_APP_CONTEXT_ACTION:
        DBG(L"WM_APP_CONTEXT_ACTION received");
        CloseContextPopup();
        return 0;

    case WM_MENUSELECT: {
        UINT cmd = LOWORD(wParam);
        auto it = m_menuTooltips.find(cmd);
        if (it != m_menuTooltips.end() && !it->second.empty()) {
            if (!m_hMenuTooltip) {
                m_hMenuTooltip = CreateWindowExW(0, TOOLTIPS_CLASS, nullptr,
                    WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                    hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
                if (m_hMenuTooltip) {
                    SendMessageW(m_hMenuTooltip, TTM_SETMAXTIPWIDTH, 0, 2000);
                    TOOLINFOW ti = {};
                    ti.cbSize = sizeof(TOOLINFOW) - sizeof(void*);
                    ti.uFlags = TTF_TRACK;
                    ti.hwnd = hwnd;
                    ti.uId = (UINT_PTR)hwnd;
                    ti.lpszText = const_cast<wchar_t*>(it->second.c_str());
                    SendMessageW(m_hMenuTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
                }
            }
            if (m_hMenuTooltip) {
                POINT pt;
                GetCursorPos(&pt);
                SendMessageW(m_hMenuTooltip, TTM_TRACKPOSITION, 0,
                             MAKELPARAM(pt.x, pt.y + 24));
                TOOLINFOW ti = {};
                ti.cbSize = sizeof(TOOLINFOW) - sizeof(void*);
                ti.hwnd = hwnd;
                ti.uId = (UINT_PTR)hwnd;
                ti.lpszText = const_cast<wchar_t*>(it->second.c_str());
                SendMessageW(m_hMenuTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
                SendMessageW(m_hMenuTooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
            }
        } else {
            if (m_hMenuTooltip) {
                TOOLINFOW ti = {};
                ti.cbSize = sizeof(TOOLINFOW) - sizeof(void*);
                ti.hwnd = hwnd;
                ti.uId = (UINT_PTR)hwnd;
                SendMessageW(m_hMenuTooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    case WM_COMMAND: {
        UINT cmd = LOWORD(wParam);
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
            { PetConfig::Data cfg = PetConfig::Load(); cfg.moveEnabled = false; PetConfig::Save(cfg); }
        } else if (cmd == ID_PET_MOVE_MOVING) {
            m_moveEnabled = true;
            SyncMoveState();
            { PetConfig::Data cfg = PetConfig::Load(); cfg.moveEnabled = true; PetConfig::Save(cfg); }
        } else if (cmd == ID_PET_MOVE_STEP) {
            ShowSliderPopup(hwnd, TranslationService::Get()->Tr(L"Pet", L"Step (px)").c_str(), m_moveStep, 1, 50, ID_PET_MOVE_STEP);
        } else if (cmd == ID_PET_MOVE_SPEED) {
            ShowSliderPopup(hwnd, TranslationService::Get()->Tr(L"Pet", L"Speed (ms)").c_str(), m_moveSpeed, 10, 1000, ID_PET_MOVE_SPEED);
        } else if (cmd == ID_PET_MOVE_SHUTTLE) {
            m_moveShuttle = !m_moveShuttle;
            { PetConfig::Data cfg = PetConfig::Load(); cfg.moveShuttle = m_moveShuttle; PetConfig::Save(cfg); }
        } else if (cmd == ID_PET_EDIT_ORDER) {
            OpenMenuOrderDialog(hwnd);
        } else if (cmd >= ID_MENU_BASE && m_moduleManager) {
            m_moduleManager->ExecuteContextMenuItem(cmd - ID_MENU_BASE);
        }
        return 0;
    }

    case WM_EXITMENULOOP:
        if (m_hMenuTooltip) { DestroyWindow(m_hMenuTooltip); m_hMenuTooltip = nullptr; }
        m_menuTooltips.clear();
        break;
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

//==============================================================================
// Custom popup context menu with inline sliders
//==============================================================================

static constexpr int POPUP_ITEM_H = 26;
static constexpr int POPUP_SEP_H = 6;
static constexpr int POPUP_PAD = 4;
static constexpr int POPUP_W = 220;
static constexpr int TRACKBAR_H = 22;

enum PopupHit { HIT_NONE = -1 };
enum PopupItemKind { PIK_ACTION, PIK_SEP, PIK_SUBMENU, PIK_SLIDER };
// SliderKind defined in MainWindow.h

static constexpr int FILE_ID_TOPMOST = 102;
static constexpr int FILE_ID_SETTINGS = 101;
static constexpr int FILE_ID_ABOUT = 9999;
static constexpr int FILE_ID_EXIT = 100;
static constexpr int FILE_ID_MOVE_STILL = 103;
static constexpr int FILE_ID_MOVE_MOVING = 104;
static constexpr int FILE_ID_MOVE_STEP = 105;
static constexpr int FILE_ID_MOVE_SPEED = 106;
static constexpr int FILE_ID_MOVE_SHUTTLE = 107;
static constexpr int FILE_ID_EDIT_ORDER = 108;

//--- Pet submenu descriptors (Always on Top, Move ▸, ---, Opacity ▸, Scale ▸, ---, Edit Order) ---
static PopupItemKind g_petKind[] = {
    PIK_ACTION,   // 0:  Always on Top
    PIK_SUBMENU,  // 1:  Move ▸
    PIK_SEP,      // 2:
    PIK_SUBMENU,  // 3:  Opacity ▸
    PIK_SUBMENU,  // 4:  Scale ▸
    PIK_SEP,      // 5:
    PIK_ACTION,   // 6:  Edit Order
};
static constexpr int PET_N = sizeof(g_petKind) / sizeof(g_petKind[0]);
static constexpr int PET_SLIDERS = 0;
static int g_petCmd[] = {
    FILE_ID_TOPMOST, 0, 0, 0, 0, 0, FILE_ID_EDIT_ORDER
};

//--- Main popup descriptors ---
static PopupItemKind g_popupKind[] = {
    PIK_SUBMENU,  // 0:  Pet ▸
    PIK_SUBMENU,  // 1:  Function ▸
    PIK_SEP,      // 2:
    PIK_ACTION,   // 3:  Settings...
    PIK_ACTION,   // 4:  About...
    PIK_SEP,      // 5:
    PIK_ACTION,   // 6:  Exit
};
static constexpr int POPUP_N = sizeof(g_popupKind) / sizeof(g_popupKind[0]);

static int g_popupCmd[] = {
    0, 0, 0,
    FILE_ID_SETTINGS, FILE_ID_ABOUT, 0, FILE_ID_EXIT
};

struct ContextSliderData {
    MainWindow* mainWindow;
    SliderKind kind;
    int* pContextValue;
    HWND hTrackbar;
    int minVal;
    int maxVal;
    HWND* phSelf;
    ULONGLONG creationTime;
};

static int PopupTotalH(int /*itemCount*/, int /*sliderCount*/) {
    int h = POPUP_PAD * 2;
    for (int i = 0; i < PET_N; i++) {
        if (g_petKind[i] == PIK_SEP)
            h += POPUP_SEP_H;
        else if (g_petKind[i] == PIK_SLIDER)
            h += TRACKBAR_H + 2;
        else
            h += POPUP_ITEM_H;
    }
    return h;
}

//--- Move submenu descriptors ---
struct PopupSub {
    int*    ids;
    const wchar_t** labels;
    int     n;
    int     checkedId; // -1 = none
};
static const wchar_t* g_moveLabels[] = { L"Still", L"Move", nullptr, L"Step...", L"Speed...", nullptr, L"Shuttle" };
static int g_moveIds[] = { FILE_ID_MOVE_STILL, FILE_ID_MOVE_MOVING, 0, FILE_ID_MOVE_STEP, FILE_ID_MOVE_SPEED, 0, FILE_ID_MOVE_SHUTTLE };
static constexpr int MOVE_N = 7;

//==============================================================================
// ContextPopupProc
//==============================================================================

struct ContextPopupData {
    MainWindow* mainWindow;
    int hoveredItem;
};

LRESULT CALLBACK MainWindow::ContextPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* data = reinterpret_cast<ContextPopupData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    auto* self = data ? data->mainWindow : nullptr;

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* d = new ContextPopupData{reinterpret_cast<MainWindow*>(cs->lpCreateParams), -1};
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));

        HDC dc = GetDC(hwnd);
        NONCLIENTMETRICSW nm = { sizeof(nm) };
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nm), &nm, 0);
        LOGFONTW lf = nm.lfMenuFont;
        wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Microsoft JhengHei");
        HFONT hf = CreateFontIndirectW(&lf);
        ReleaseDC(hwnd, dc);
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)hf, TRUE);
        break;
    }

    case WM_NCDESTROY: {
        delete data;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }

    case WM_ACTIVATE:
        DBG(L"[ContextPopup] WM_ACTIVATE w=%d newActive=%p sub=%p ctx=%p",
            LOWORD(wParam), (HWND)lParam,
            self ? self->m_hSubPopup : 0, self ? self->m_hContextPopup : 0);
        if (LOWORD(wParam) == WA_INACTIVE) {
            HWND hwndNewActive = (HWND)lParam;
            bool match = self && hwndNewActive && (hwndNewActive == self->m_hSubPopup ||
                hwndNewActive == self->m_hContextPopup ||
                hwndNewActive == self->m_hOpacityPopup ||
                hwndNewActive == self->m_hScalePopup ||
                hwndNewActive == self->m_hMovePopup ||
                hwndNewActive == self->m_hStepPopup ||
                hwndNewActive == self->m_hSpeedPopup);
            DBG(L"[ContextPopup] WA_INACTIVE match=%d newActive=%p sub=%p",
                match, hwndNewActive, self ? self->m_hSubPopup : 0);
            if (match) return 0;
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int y = POPUP_PAD;
        int newHover = -1;
        for (int i = 0; i < POPUP_N; i++) {
            int ih = (g_popupKind[i] == PIK_SEP) ? POPUP_SEP_H : POPUP_ITEM_H;
            if (pt.y >= y && pt.y < y + ih && g_popupKind[i] != PIK_SEP) {
                newHover = i;
                break;
            }
            y += ih;
        }
        if (newHover != data->hoveredItem) {
            if (data->hoveredItem >= 0)
                KillTimer(hwnd, 1);
            data->hoveredItem = newHover;
            InvalidateRect(hwnd, nullptr, TRUE);
            if (newHover >= 0)
                SetTimer(hwnd, 1, 300, nullptr);
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (data->hoveredItem >= 0)
            KillTimer(hwnd, 1);
        data->hoveredItem = -1;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_TIMER: {
        if (wParam != 1) break;
        KillTimer(hwnd, 1);
        int idx = data->hoveredItem;
        if (idx < 0 || !self) break;
        if (g_popupKind[idx] != PIK_SUBMENU) break;
        self->CloseSubPopup();
        int cw = POPUP_W - POPUP_PAD * 2;
        RECT ir = { POPUP_PAD, 0, POPUP_PAD + cw, 0 };
        {
            int y = POPUP_PAD;
            for (int i = 0; i <= idx; i++) {
                int ih = (g_popupKind[i] == PIK_SEP) ? POPUP_SEP_H : POPUP_ITEM_H;
                if (i == idx) { ir.top = y; ir.bottom = y + ih; break; }
                y += ih;
            }
        }
        POINT scr = { ir.left, ir.top };
        ClientToScreen(hwnd, &scr);
        scr.x += cw + 2;
        int subH = (idx == 0) ? PopupTotalH(PET_N, PET_SLIDERS) + 6 : 0;
        if (scr.y + subH > GetSystemMetrics(SM_CYSCREEN))
            scr.y = GetSystemMetrics(SM_CYSCREEN) - subH - 4;
        if (scr.y < 0) scr.y = 4;

        if (idx == 0) {
            const wchar_t kPetClass[] = L"PetSubPopupExClass";
            WNDCLASSW wc = {};
            wc.lpfnWndProc = MainWindow::PetPopupProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = kPetClass;
            wc.hbrBackground = GetSysColorBrush(COLOR_MENU);
            RegisterClassW(&wc);
            int pw = POPUP_W;
            if (scr.x + pw > GetSystemMetrics(SM_CXSCREEN))
                scr.x -= POPUP_W + pw + 4;
            if (scr.x < 0) scr.x = 4;
            DWORD exStyle = WS_EX_TOOLWINDOW;
            if ((GetWindowLongPtrW(self->m_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST))
                exStyle |= WS_EX_TOPMOST;
            HWND hPet = CreateWindowExW(exStyle, kPetClass, L"",
                WS_POPUP | WS_BORDER, scr.x, scr.y, pw, subH,
                nullptr, nullptr, GetModuleHandleW(nullptr), self);
            self->m_hSubPopup = hPet;
            ShowWindow(hPet, SW_SHOWNA);
        } else if (idx == 1 && self->m_moduleManager) {
            std::vector<PopupItem> items;
            auto groups = self->m_moduleManager->GetMenuGroups();
            for (const auto& g : groups) {
                if (!items.empty())
                    items.push_back({ PopupItem::Separator });
                for (const auto& mi : g.items) {
                    items.push_back({ PopupItem::Action, ID_MENU_BASE + mi.itemId, mi.label });
                    if (!mi.tooltip.empty())
                        self->m_menuTooltips[ID_MENU_BASE + mi.itemId] = mi.tooltip;
                }
            }
            int h2;
            self->m_hSubPopup = self->CreateSubPopup(hwnd, scr, items, 0, h2);
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, GetSysColorBrush(COLOR_MENU));

        HFONT hf = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        if (!hf) hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        auto oldF = (HFONT)SelectObject(dc, hf);
        SetBkMode(dc, TRANSPARENT);

        int x = POPUP_PAD;
        int w = rc.right - POPUP_PAD * 2;
        int y = POPUP_PAD;
        int hover = data ? data->hoveredItem : -1;

        auto Tr = [](const wchar_t* sec, const wchar_t* key) {
            return TranslationService::Get()->Tr(sec, key);
        };

        for (int i = 0; i < POPUP_N; i++) {
            switch (g_popupKind[i]) {
            case PIK_SEP:
                y += POPUP_SEP_H / 2;
                PatBlt(dc, x, y, w, 1, PATCOPY);
                y += POPUP_SEP_H / 2;
                break;

            case PIK_ACTION:
            case PIK_SUBMENU: {
                int ih = POPUP_ITEM_H;
                RECT ir = { x, y, x + w, y + ih };
                bool hl = (i == hover);
                if (hl) {
                    FillRect(dc, &ir, GetSysColorBrush(COLOR_HIGHLIGHT));
                    SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
                } else {
                    SetTextColor(dc, GetSysColor(COLOR_MENUTEXT));
                }
                std::wstring label;
                if (g_popupKind[i] == PIK_ACTION) {
                    switch (g_popupCmd[i]) {
                    case FILE_ID_SETTINGS: label = Tr(L"Pet", L"Settings..."); break;
                    case FILE_ID_ABOUT:    label = Tr(L"Pet", L"About Project..."); break;
                    case FILE_ID_EXIT:     label = Tr(L"Pet", L"Exit"); break;
                    }
                } else {
                    if (i == 0)      label = Tr(L"Pet", L"Pet");
                    else if (i == 1) label = Tr(L"Pet", L"Function");
                    RECT arr = { x + w - 16, y, x + w, y + POPUP_ITEM_H };
                    DrawTextW(dc, L"\u25B8", -1, &arr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                }
                if (!label.empty())
                    DrawTextW(dc, label.c_str(), -1, &ir, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                y += ih;
                break;
            }
            }
        }
        SelectObject(dc, oldF);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int y = POPUP_PAD;
        int cw = POPUP_W - POPUP_PAD * 2;
        for (int i = 0; i < POPUP_N; i++) {
            int ih = (g_popupKind[i] == PIK_SEP) ? POPUP_SEP_H : POPUP_ITEM_H;
            RECT ir = { POPUP_PAD, y, POPUP_PAD + cw, y + ih };
            if (pt.y >= ir.top && pt.y < ir.bottom) {
                if (g_popupKind[i] == PIK_ACTION) {
                    if (!self) break;
                    self->HandleMessage(self->m_hwnd, WM_COMMAND, g_popupCmd[i], 0);
                } else if (g_popupKind[i] == PIK_SUBMENU && self) {
                    self->CloseSubPopup();
                    POINT scr = { ir.left, ir.top };
                    ClientToScreen(hwnd, &scr);
                    scr.x += cw + 2;
                    int subH = (i == 0) ? PopupTotalH(PET_N, PET_SLIDERS) + 6 : 0;
                    if (scr.y + subH > GetSystemMetrics(SM_CYSCREEN))
                        scr.y = GetSystemMetrics(SM_CYSCREEN) - subH - 4;
                    if (scr.y < 0) scr.y = 4;

                    if (i == 0) {
                        const wchar_t kPetClass[] = L"PetSubPopupExClass";
                        WNDCLASSW wc = {};
                        wc.lpfnWndProc = MainWindow::PetPopupProc;
                        wc.hInstance = GetModuleHandleW(nullptr);
                        wc.lpszClassName = kPetClass;
                        wc.hbrBackground = GetSysColorBrush(COLOR_MENU);
                        RegisterClassW(&wc);
                        int pw = POPUP_W;
                        if (scr.x + pw > GetSystemMetrics(SM_CXSCREEN))
                            scr.x -= POPUP_W + pw + 4;
                        if (scr.x < 0) scr.x = 4;
                        DWORD exStyle = WS_EX_TOOLWINDOW;
                        if ((GetWindowLongPtrW(self->m_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST))
                            exStyle |= WS_EX_TOPMOST;
                        HWND hPet = CreateWindowExW(exStyle, kPetClass, L"",
                            WS_POPUP | WS_BORDER, scr.x, scr.y, pw, subH,
                            nullptr, nullptr, GetModuleHandleW(nullptr), self);
                        self->m_hSubPopup = hPet;
                        ShowWindow(hPet, SW_SHOWNA);
                    } else if (i == 1 && self->m_moduleManager) {
                        std::vector<PopupItem> items;
                        auto groups = self->m_moduleManager->GetMenuGroups();
                        for (const auto& g : groups) {
                            if (!items.empty())
                                items.push_back({ PopupItem::Separator });
                            for (const auto& mi : g.items) {
                                items.push_back({ PopupItem::Action,
                                    ID_MENU_BASE + mi.itemId, mi.label });
                                if (!mi.tooltip.empty())
                                    self->m_menuTooltips[ID_MENU_BASE + mi.itemId] = mi.tooltip;
                            }
                        }
                        int h2;
                        self->m_hSubPopup = self->CreateSubPopup(hwnd, scr, items, 0, h2);
                    }
                }
                break;
            }
            y += ih;
        }
        return 0;
    }

    case WM_DESTROY: {
        DBG(L"[ContextPopup] WM_DESTROY  m_hSubPopup=%p", self ? self->m_hSubPopup : 0);
        KillTimer(hwnd, 1);
        if (self) {
            self->CloseSubPopup();
            self->m_hContextPopup = nullptr;
        }
        break;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

struct PetPopupData {
    MainWindow* mainWindow;
    int hoveredItem;
};

void MainWindow::OpenSliderSubPopup(HWND hwnd, MainWindow* self,
    POINT scr, int popupWidth,
    int* pContextValue, int minVal, int maxVal,
    HWND* phSelf, SliderKind kind)
{
    DBG(L"[OpenSliderSubPopup] scr=(%d,%d) kind=%d", scr.x, scr.y, (int)kind);

    if (scr.x + popupWidth > GetSystemMetrics(SM_CXSCREEN))
        scr.x -= POPUP_W + popupWidth + 4;
    if (scr.x < 0) scr.x = 4;

    if (*phSelf && IsWindow(*phSelf))
        DestroyWindow(*phSelf);
    *phSelf = nullptr;

    const wchar_t kClass[] = L"PetSliderSubPopupClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWindow::SliderSubPopupProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hbrBackground = GetSysColorBrush(COLOR_MENU);
    RegisterClassW(&wc);

    auto* d = new ContextSliderData{
        self, kind, pContextValue,
        nullptr, minVal, maxVal, phSelf, GetTickCount64()
    };

    DWORD exStyle = WS_EX_TOOLWINDOW;
    if ((GetWindowLongPtrW(self->m_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST))
        exStyle |= WS_EX_TOPMOST;

    HWND hPop = CreateWindowExW(exStyle, kClass, L"",
        WS_POPUP | WS_BORDER, scr.x, scr.y, popupWidth, 52,
        hwnd, nullptr, GetModuleHandleW(nullptr), d);
    DBG(L"[OpenSliderSubPopup] created hPop=%p  *phSelf was %p  now = %p",
        hPop, *phSelf, hPop);
    *phSelf = hPop;
    ShowWindow(hPop, SW_SHOWNA);
    DBG(L"[OpenSliderSubPopup] done");
}

//==============================================================================
// PetPopupProc — submenu with hover highlight + auto-expand for submenus
//==============================================================================

LRESULT CALLBACK MainWindow::PetPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* data = reinterpret_cast<PetPopupData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    auto* self = data ? data->mainWindow : nullptr;

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* d = new PetPopupData{reinterpret_cast<MainWindow*>(cs->lpCreateParams), -1};
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));

        HDC dc = GetDC(hwnd);
        NONCLIENTMETRICSW nm = { sizeof(nm) };
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nm), &nm, 0);
        LOGFONTW lf = nm.lfMenuFont;
        wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Microsoft JhengHei");
        HFONT hf = CreateFontIndirectW(&lf);
        ReleaseDC(hwnd, dc);
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)hf, TRUE);
        break;
    }

    case WM_NCDESTROY: {
        delete data;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }

    case WM_ACTIVATE:
        DBG(L"[PetPopup] WM_ACTIVATE w=%d newActive=%p  hOp=%p hSc=%p",
            LOWORD(wParam), (HWND)lParam,
            self ? self->m_hOpacityPopup : 0, self ? self->m_hScalePopup : 0);
        if (LOWORD(wParam) == WA_INACTIVE) {
            HWND hwndNewActive = (HWND)lParam;
            bool match = self && hwndNewActive && (hwndNewActive == self->m_hOpacityPopup || hwndNewActive == self->m_hScalePopup ||
                hwndNewActive == self->m_hSubPopup || hwndNewActive == self->m_hMovePopup ||
                hwndNewActive == self->m_hStepPopup || hwndNewActive == self->m_hSpeedPopup);
            DBG(L"[PetPopup] WA_INACTIVE match=%d  newActive=%p hOp=%p hSc=%p",
                match, hwndNewActive, self ? self->m_hOpacityPopup : 0, self ? self->m_hScalePopup : 0);
            if (match) return 0;
            KillTimer(hwnd, 1);
            if (self) {
                if (self->m_contextOpacity != self->m_currentOpacity)
                    self->SetOpacity(self->m_contextOpacity);
                if (self->m_contextScale != self->m_scalePercent)
                    self->SetScale(self->m_contextScale);
                if (self->m_contextMoveStep != self->m_moveStep)
                    PostMessageW(self->m_hwnd, MainWindow::WM_APP_SLIDER_CHANGE, ID_PET_MOVE_STEP, self->m_contextMoveStep);
                if (self->m_contextMoveSpeed != self->m_moveSpeed)
                    PostMessageW(self->m_hwnd, MainWindow::WM_APP_SLIDER_CHANGE, ID_PET_MOVE_SPEED, self->m_contextMoveSpeed);
                self->CloseMovePopup();
                self->CloseStepPopup();
                self->CloseSpeedPopup();
                self->CloseOpacityPopup();
                self->CloseScalePopup();
                PostMessageW(self->m_hwnd, WM_APP_CONTEXT_ACTION, 0, 0);
                self->m_hSubPopup = nullptr;
            }
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int y = POPUP_PAD;
        int newHover = -1;

        for (int i = 0; i < PET_N; i++) {
            int ih = (g_petKind[i] == PIK_SEP) ? POPUP_SEP_H : POPUP_ITEM_H;
            if (pt.y >= y && pt.y < y + ih && g_petKind[i] != PIK_SEP) {
                newHover = i;
                break;
            }
            y += ih;
        }
        if (newHover != data->hoveredItem) {
            if (data->hoveredItem >= 0)
                KillTimer(hwnd, 1);
            data->hoveredItem = newHover;
            InvalidateRect(hwnd, nullptr, TRUE);
            if (newHover >= 0)
                SetTimer(hwnd, 1, 300, nullptr);
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (data->hoveredItem >= 0)
            KillTimer(hwnd, 1);
        data->hoveredItem = -1;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_TIMER: {
        if (wParam != 1) break;
        DBG(L"[PetPopup] WM_TIMER hoveredItem=%d", data->hoveredItem);
        KillTimer(hwnd, 1);
        int idx = data->hoveredItem;
        if (idx < 0 || !self) break;
        if (g_petKind[idx] != PIK_SUBMENU) break;
        int cw = POPUP_W - POPUP_PAD * 2;
        if (idx == 1) {
            // Move submenu — keep PetPopup alive (parallel like opacity slider)
            int y = POPUP_PAD;
            for (int i = 0; i < idx; i++)
                y += (g_petKind[i] == PIK_SEP) ? POPUP_SEP_H : POPUP_ITEM_H;
            RECT ir = { POPUP_PAD, y, POPUP_PAD + cw, y + POPUP_ITEM_H };
            POINT scr = { ir.left, ir.top };
            ClientToScreen(hwnd, &scr);
            scr.x += cw + 2;
            self->CloseMovePopup();
            self->CloseOpacityPopup();
            self->CloseScalePopup();
            std::vector<PopupItem> items;
            auto Tr = [](const wchar_t* sec, const wchar_t* key) {
                return TranslationService::Get()->Tr(sec, key);
            };
            for (int j = 0; j < MOVE_N; j++) {
                if (g_moveIds[j] == 0)
                    items.push_back({ PopupItem::Separator, 0, L"" });
                else if (g_moveIds[j] == FILE_ID_MOVE_STEP || g_moveIds[j] == FILE_ID_MOVE_SPEED)
                    items.push_back({ PopupItem::Submenu, g_moveIds[j], Tr(L"Pet", g_moveLabels[j]) });
                else
                    items.push_back({ PopupItem::Action, g_moveIds[j], Tr(L"Pet", g_moveLabels[j]) });
            }
            int h2;
            self->CreateSubPopup(hwnd, scr, items, 0, h2, &self->m_hMovePopup);
        } else if (idx == 3 || idx == 4) {
            self->CloseMovePopup();
            POINT ptSl = { POPUP_PAD, POPUP_PAD };
            for (int i = 0; i < idx; i++)
                ptSl.y += (g_petKind[i] == PIK_SEP) ? POPUP_SEP_H : POPUP_ITEM_H;
            ClientToScreen(hwnd, &ptSl);
            ptSl.x += cw + 2;
            if (idx == 3) {
                self->m_contextOpacity = self->m_currentOpacity;
                self->CloseScalePopup();
                OpenSliderSubPopup(hwnd, self, ptSl, 200,
                    &self->m_contextOpacity, 1, 255,
                    &self->m_hOpacityPopup, SliderKind::Opacity);
            } else {
                self->m_contextScale = self->m_scalePercent;
                self->CloseOpacityPopup();
                OpenSliderSubPopup(hwnd, self, ptSl, 200,
                    &self->m_contextScale, 25, 250,
                    &self->m_hScalePopup, SliderKind::Scale);
            }
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, GetSysColorBrush(COLOR_MENU));

        HFONT hf = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        if (!hf) hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        auto oldF = (HFONT)SelectObject(dc, hf);
        SetBkMode(dc, TRANSPARENT);

        int x = POPUP_PAD;
        int w = rc.right - POPUP_PAD * 2;
        int y = POPUP_PAD;
        int hover = data ? data->hoveredItem : -1;

        auto Tr = [](const wchar_t* sec, const wchar_t* key) {
            return TranslationService::Get()->Tr(sec, key);
        };

        for (int i = 0; i < PET_N; i++) {
            switch (g_petKind[i]) {
            case PIK_SEP:
                y += POPUP_SEP_H / 2;
                PatBlt(dc, x, y, w, 1, PATCOPY);
                y += POPUP_SEP_H / 2;
                break;

            case PIK_ACTION: {
                int ih = POPUP_ITEM_H;
                RECT ir = { x, y, x + w, y + ih };
                bool hl = (i == hover);
                if (hl) {
                    FillRect(dc, &ir, GetSysColorBrush(COLOR_HIGHLIGHT));
                    SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
                } else {
                    SetTextColor(dc, GetSysColor(COLOR_MENUTEXT));
                }
                bool topmost = (g_petCmd[i] == FILE_ID_TOPMOST &&
                    (GetWindowLongPtrW(self ? self->m_hwnd : nullptr, GWL_EXSTYLE) & WS_EX_TOPMOST));
                std::wstring label;
                switch (g_petCmd[i]) {
                case FILE_ID_TOPMOST:   label = Tr(L"Pet", L"Always on Top"); break;
                case FILE_ID_EDIT_ORDER: label = Tr(L"Pet", L"Edit Order"); break;
                }
                if (topmost)
                    DrawTextW(dc, L"\u2713 ", -1, &ir, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                ir.left += 16;
                if (!label.empty())
                    DrawTextW(dc, label.c_str(), -1, &ir, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                y += ih;
                break;
            }

            case PIK_SUBMENU: {
                int ih = POPUP_ITEM_H;
                RECT ir = { x, y, x + w, y + ih };
                bool hl = (i == hover);
                if (hl) {
                    FillRect(dc, &ir, GetSysColorBrush(COLOR_HIGHLIGHT));
                    SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
                } else {
                    SetTextColor(dc, GetSysColor(COLOR_MENUTEXT));
                }
                std::wstring label;
                if (i == 1)      label = Tr(L"Pet", L"Move");
                else if (i == 3) label = Tr(L"Pet", L"Opacity");
                else if (i == 4) label = Tr(L"Pet", L"Scale");
                if (!label.empty())
                    DrawTextW(dc, label.c_str(), -1, &ir, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                RECT arr = { x + w - 16, y, x + w, y + POPUP_ITEM_H };
                DrawTextW(dc, L"\u25B8", -1, &arr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                y += ih;
                break;
            }

            default:
                y += POPUP_ITEM_H;
                break;
            }
        }
        SelectObject(dc, oldF);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int y = POPUP_PAD;
        int cw = POPUP_W - POPUP_PAD * 2;
        for (int i = 0; i < PET_N; i++) {
            int ih = (g_petKind[i] == PIK_SEP) ? POPUP_SEP_H : POPUP_ITEM_H;
            RECT ir = { POPUP_PAD, y, POPUP_PAD + cw, y + ih };
            if (pt.y >= ir.top && pt.y < ir.bottom) {
                if (g_petKind[i] == PIK_ACTION) {
                    if (!self) break;
                    self->HandleMessage(self->m_hwnd, WM_COMMAND, g_petCmd[i], 0);
                } else if (g_petKind[i] == PIK_SUBMENU && self) {
                    if (i == 1) {
                        POINT scr = { ir.left, ir.top };
                        ClientToScreen(hwnd, &scr);
                        scr.x += cw + 2;
                        self->CloseMovePopup();
                        self->CloseOpacityPopup();
                        self->CloseScalePopup();
                        std::vector<PopupItem> items;
                        auto Tr = [](const wchar_t* sec, const wchar_t* key) {
                            return TranslationService::Get()->Tr(sec, key);
                        };
                        for (int j = 0; j < MOVE_N; j++) {
                            if (g_moveIds[j] == 0)
                                items.push_back({ PopupItem::Separator, 0, L"" });
                            else if (g_moveIds[j] == FILE_ID_MOVE_STEP || g_moveIds[j] == FILE_ID_MOVE_SPEED)
                                items.push_back({ PopupItem::Submenu, g_moveIds[j], Tr(L"Pet", g_moveLabels[j]) });
                            else
                                items.push_back({ PopupItem::Action, g_moveIds[j], Tr(L"Pet", g_moveLabels[j]) });
                        }
                        int h2;
                        self->CreateSubPopup(hwnd, scr, items, 0, h2, &self->m_hMovePopup);
                    } else if (i == 3 || i == 4) {
                        self->CloseMovePopup();
                        POINT ptSl = { ir.left, ir.top };
                        ClientToScreen(hwnd, &ptSl);
                        ptSl.x += cw + 2;
                        if (i == 3) {
                            self->m_contextOpacity = self->m_currentOpacity;
                            self->CloseScalePopup();
                            OpenSliderSubPopup(hwnd, self, ptSl, 200,
                                &self->m_contextOpacity, 1, 255,
                                &self->m_hOpacityPopup, SliderKind::Opacity);
                        } else {
                            self->m_contextScale = self->m_scalePercent;
                            self->CloseOpacityPopup();
                            OpenSliderSubPopup(hwnd, self, ptSl, 200,
                                &self->m_contextScale, 25, 250,
                                &self->m_hScalePopup, SliderKind::Scale);
                        }
                    }
                }
                break;
            }
            y += ih;
        }
        return 0;
    }

    case WM_DESTROY: {
        DBG(L"[PetPopup] WM_DESTROY");
        KillTimer(hwnd, 1);
        if (self) {
            self->CloseMovePopup();
            self->CloseStepPopup();
            self->CloseSpeedPopup();
            self->CloseOpacityPopup();
            self->CloseScalePopup();
        }
        break;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

//==============================================================================
// SubPopupProc
//==============================================================================

LRESULT CALLBACK MainWindow::SubPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* data = reinterpret_cast<SubPopupData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    auto* container = data ? data->container : nullptr;

    switch (msg) {
    case WM_CREATE: {
        HDC dc = GetDC(hwnd);
        NONCLIENTMETRICSW nm = { sizeof(nm) };
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nm), &nm, 0);
        LOGFONTW lf = nm.lfMenuFont;
        wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Microsoft JhengHei");
        HFONT hf = CreateFontIndirectW(&lf);
        ReleaseDC(hwnd, dc);
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)hf, TRUE);
        break;
    }

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            DBG(L"[SubPopup] WA_INACTIVE  mainWindow=%p", data ? data->mainWindow : 0);
            if (data && data->mainWindow) {
                MainWindow* self = data->mainWindow;
                if (self->m_contextMoveStep != self->m_moveStep)
                    PostMessageW(self->m_hwnd, MainWindow::WM_APP_SLIDER_CHANGE, ID_PET_MOVE_STEP, self->m_contextMoveStep);
                if (self->m_contextMoveSpeed != self->m_moveSpeed)
                    PostMessageW(self->m_hwnd, MainWindow::WM_APP_SLIDER_CHANGE, ID_PET_MOVE_SPEED, self->m_contextMoveSpeed);
                PostMessageW(self->m_hwnd, WM_APP_CONTEXT_ACTION, 0, 0);
                if (data->clearPtr) *(data->clearPtr) = nullptr;
                if (GetActiveWindow() != self->m_hStepPopup &&
                    GetActiveWindow() != self->m_hSpeedPopup) {
                    self->CloseStepPopup();
                    self->CloseSpeedPopup();
                }
            }
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, GetSysColorBrush(COLOR_MENU));
        HFONT hf = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        if (!hf) hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        auto oldF = (HFONT)SelectObject(dc, hf);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_MENUTEXT));

        int y = POPUP_PAD;
        int x = POPUP_PAD;
        int w = rc.right - POPUP_PAD * 2;
        for (const auto& item : *container) {
            if (item.type == PopupItem::Separator) {
                y += POPUP_SEP_H / 2;
                PatBlt(dc, x, y, w, 1, PATCOPY);
                y += POPUP_SEP_H / 2;
            } else {
                RECT ir = { x, y, x + w, y + POPUP_ITEM_H };
                DrawTextW(dc, item.text.c_str(), -1, &ir, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                if (item.type == PopupItem::Submenu) {
                    RECT arr = { x + w - 16, y, x + w, y + POPUP_ITEM_H };
                    DrawTextW(dc, L"\u25B8", -1, &arr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                }
                y += POPUP_ITEM_H;
            }
        }
        SelectObject(dc, oldF);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!data || !data->mainWindow) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int y2 = POPUP_PAD;
        int x2 = POPUP_PAD;
        int w2 = data->popupWidth - POPUP_PAD * 2;
        int hit = -1;
        for (size_t i = 0; i < container->size(); i++) {
            const auto& item = (*container)[i];
            int ih = (item.type == PopupItem::Separator) ? POPUP_SEP_H : POPUP_ITEM_H;
            RECT ir = { x2, y2, x2 + w2, y2 + ih };
            if (item.type == PopupItem::Submenu && pt.y >= ir.top && pt.y < ir.bottom) {
                hit = (int)i;
                break;
            }
            y2 += ih;
        }
        if (hit != data->hoveredItem) {
            data->hoveredItem = hit;
            if (hit >= 0) {
                SetTimer(hwnd, 2, 300, nullptr);
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            } else {
                KillTimer(hwnd, 2);
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (data) data->hoveredItem = -1;
        KillTimer(hwnd, 2);
        return 0;
    }

    case WM_TIMER: {
        if (wParam != 2) break;
        if (!data || !data->mainWindow) break;
        KillTimer(hwnd, 2);
        MainWindow* self = data->mainWindow;

        int idx = data->hoveredItem;
        if (idx < 0 || (size_t)idx >= container->size()) break;
        auto& item = (*container)[idx];
        if (item.type != PopupItem::Submenu) break;

        if (item.id == FILE_ID_MOVE_STEP || item.id == FILE_ID_MOVE_SPEED) {
            int y2 = POPUP_PAD;
            int x2 = POPUP_PAD;
            int w2 = data->popupWidth - POPUP_PAD * 2;
            for (int j = 0; j < idx; j++) {
                const auto& sj = (*container)[j];
                y2 += (sj.type == PopupItem::Separator) ? POPUP_SEP_H : POPUP_ITEM_H;
            }

            POINT scr = { x2 + w2 + 2, y2 };
            ClientToScreen(hwnd, &scr);
            RECT rc; GetClientRect(hwnd, &rc);
            MapWindowPoints(hwnd, nullptr, (POINT*)&rc, 2);
            if (scr.x > rc.right) scr.x = rc.right - 2;

            if (item.id == FILE_ID_MOVE_STEP) {
                self->m_contextMoveStep = self->m_moveStep;
                self->CloseSpeedPopup();
                self->CloseStepPopup();
                OpenSliderSubPopup(self->m_hwnd, self, scr, 200,
                    &self->m_contextMoveStep, 1, 50,
                    &self->m_hStepPopup, SliderKind::Step);
            } else {
                self->m_contextMoveSpeed = self->m_moveSpeed;
                self->CloseStepPopup();
                self->CloseSpeedPopup();
                OpenSliderSubPopup(self->m_hwnd, self, scr, 200,
                    &self->m_contextMoveSpeed, 10, 1000,
                    &self->m_hSpeedPopup, SliderKind::Speed);
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        MainWindow* self = data ? data->mainWindow : nullptr;

        int y = POPUP_PAD;
        int x = POPUP_PAD;
        int w = data ? data->popupWidth : POPUP_W;
        w -= POPUP_PAD * 2;
        for (const auto& item : *container) {
            int ih = (item.type == PopupItem::Separator) ? POPUP_SEP_H : POPUP_ITEM_H;
            RECT ir = { x, y, x + w, y + ih };
            if (pt.y >= ir.top && pt.y < ir.bottom) {
                if (item.type == PopupItem::Action) {
                    if (self) {
                        HWND mainHwnd = self->m_hwnd;
                        self->HandleMessage(mainHwnd, WM_COMMAND, item.id, 0);
                    }
                    if (IsWindow(hwnd))
                        DestroyWindow(hwnd);
                }
                return 0;
            }
            y += ih;
        }
        return 0;
    }

    case WM_DESTROY: {
        DBG(L"[SubPopup] WM_DESTROY  mainWindow=%p", data ? data->mainWindow : 0);
        if (data && data->mainWindow) {
            data->mainWindow->CloseStepPopup();
            data->mainWindow->CloseSpeedPopup();
        }
        if (data) {
            delete data->container;
            delete data;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

//==============================================================================
// SliderSubPopupProc — generic nested popup with trackbar
//==============================================================================

LRESULT CALLBACK MainWindow::SliderSubPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* data = reinterpret_cast<ContextSliderData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!data && msg != WM_CREATE)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* d = reinterpret_cast<ContextSliderData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));

        RECT rc; GetClientRect(hwnd, &rc);

        d->hTrackbar = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | TBS_FIXEDLENGTH,
            POPUP_PAD, POPUP_PAD + 18, rc.right - POPUP_PAD * 2, TRACKBAR_H,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        // Subclass trackbar: prevent activation changes when clicked
        auto oldTrackProc = (WNDPROC)SetWindowLongPtrW(d->hTrackbar, GWLP_WNDPROC,
            (LONG_PTR)+[](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
                if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
                if (m == WM_SETFOCUS) return 0;
                auto* old = (WNDPROC)GetWindowLongPtrW(h, GWLP_USERDATA);
                return old ? CallWindowProcW(old, h, m, w, l) : DefWindowProcW(h, m, w, l);
            });
        SetWindowLongPtrW(d->hTrackbar, GWLP_USERDATA, (LONG_PTR)oldTrackProc);

        SendMessageW(d->hTrackbar, TBM_SETRANGE, TRUE, MAKELPARAM(d->minVal, d->maxVal));
        SendMessageW(d->hTrackbar, TBM_SETPOS, TRUE, *d->pContextValue);

        HDC dc = GetDC(hwnd);
        NONCLIENTMETRICSW nm = { sizeof(nm) };
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nm), &nm, 0);
        LOGFONTW lf = nm.lfMenuFont;
        wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Microsoft JhengHei");
        HFONT hf = CreateFontIndirectW(&lf);
        ReleaseDC(hwnd, dc);
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)hf, TRUE);
        break;
    }

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ACTIVATE:
        DBG(L"[SliderSubPopup] WM_ACTIVATE w=%d kind=%d newActive=%p  hOp=%p hSc=%p sub=%p",
            LOWORD(wParam), (int)data->kind, (HWND)lParam,
            data->mainWindow ? data->mainWindow->m_hOpacityPopup : 0,
            data->mainWindow ? data->mainWindow->m_hScalePopup : 0,
            data->mainWindow ? data->mainWindow->m_hSubPopup : 0);
        if (LOWORD(wParam) == WA_INACTIVE) {
            if (!data->creationTime || GetTickCount64() - data->creationTime < 600)
                break;
            HWND hwndNewActive = (HWND)lParam;
            auto* self = data->mainWindow;
            bool match = self && hwndNewActive && (hwndNewActive == self->m_hSubPopup ||
                hwndNewActive == self->m_hOpacityPopup ||
                hwndNewActive == self->m_hScalePopup ||
                hwndNewActive == self->m_hMovePopup ||
                hwndNewActive == self->m_hStepPopup ||
                hwndNewActive == self->m_hSpeedPopup);
            DBG(L"[SliderSubPopup] WA_INACTIVE check match=%d sub=%p hOp=%p hSc=%p",
                match, self ? self->m_hSubPopup : 0,
                self ? self->m_hOpacityPopup : 0, self ? self->m_hScalePopup : 0);
            if (match) return 0;
            if (self) {
                if (data->kind == SliderKind::Opacity && *data->pContextValue != self->m_currentOpacity)
                    self->SetOpacity(*data->pContextValue);
                else if (data->kind == SliderKind::Scale && *data->pContextValue != self->m_scalePercent)
                    self->SetScale(*data->pContextValue);
                else if (data->kind == SliderKind::Step)
                    PostMessageW(self->m_hwnd, WM_APP_SLIDER_CHANGE, ID_PET_MOVE_STEP, *data->pContextValue);
                else if (data->kind == SliderKind::Speed)
                    PostMessageW(self->m_hwnd, WM_APP_SLIDER_CHANGE, ID_PET_MOVE_SPEED, *data->pContextValue);
                PostMessageW(self->m_hwnd, WM_APP_CONTEXT_ACTION, 0, 0);
            }
            *data->phSelf = nullptr;
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_HSCROLL: {
        if (!data) break;
        HWND hBar = (HWND)lParam;
        int pos = (int)SendMessageW(hBar, TBM_GETPOS, 0, 0);
        *data->pContextValue = pos;
        auto* self = data->mainWindow;
        if (self) {
            if (data->kind == SliderKind::Opacity)
                self->SetOpacity(pos);
            else if (data->kind == SliderKind::Scale)
                self->SetScale(pos);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);

        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH bg = GetSysColorBrush(COLOR_MENU);
        FillRect(dc, &rc, bg);

        HFONT hf = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        if (!hf) hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        auto oldF = (HFONT)SelectObject(dc, hf);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_MENUTEXT));

        auto Tr = [](const wchar_t* sec, const wchar_t* key) {
            return TranslationService::Get()->Tr(sec, key);
        };
        const wchar_t* labelKey = L"";
        if (data->kind == SliderKind::Opacity) labelKey = L"Opacity";
        else if (data->kind == SliderKind::Scale) labelKey = L"Scale";
        else if (data->kind == SliderKind::Step) labelKey = L"Step (px)";
        else if (data->kind == SliderKind::Speed) labelKey = L"Speed (ms)";
        int val = data->pContextValue ? *data->pContextValue : 0;
        wchar_t buf[32];
        swprintf(buf, 32, L"%ls: %d", Tr(L"Pet", labelKey).c_str(), val);
        RECT tr = { POPUP_PAD, 2, rc.right - POPUP_PAD, POPUP_PAD + 18 };
        DrawTextW(dc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(dc, oldF);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_NCDESTROY: {
        DBG(L"[SliderSubPopup] WM_NCDESTROY kind=%d", data ? (int)data->kind : -1);
        delete data;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void MainWindow::ShowContextPopup(HWND hwnd, POINT pt) {
    const wchar_t kClass[] = L"PetContextPopupClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = ContextPopupProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hbrBackground = GetSysColorBrush(COLOR_MENU);
    RegisterClassW(&wc);

    int totalH = POPUP_PAD * 2;
    for (int i = 0; i < POPUP_N; i++)
        totalH += (g_popupKind[i] == PIK_SEP) ? POPUP_SEP_H : POPUP_ITEM_H;
    int x = pt.x, y = pt.y - totalH - 8;
    if (y < 0) y = pt.y + 8;
    if (x + POPUP_W > GetSystemMetrics(SM_CXSCREEN))
        x = GetSystemMetrics(SM_CXSCREEN) - POPUP_W - 4;
    if (x < 0) x = 4;

    DBG(L"ShowContextPopup: totalH=%d pos=(%d,%d) size=(%d,%d)", totalH, x, y, POPUP_W, totalH + 6);

    DWORD exStyle = WS_EX_TOOLWINDOW;
    if ((GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST))
        exStyle |= WS_EX_TOPMOST;

    m_hContextPopup = CreateWindowExW(exStyle, kClass, L"",
        WS_POPUP | WS_VISIBLE | WS_BORDER,
        x, y, POPUP_W, totalH + 6,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);

    DBG(L"ShowContextPopup: m_hContextPopup=%p", m_hContextPopup);

    if (m_hContextPopup)
        SetForegroundWindow(m_hContextPopup);
}

void MainWindow::CloseContextPopup() {
    DBG(L"CloseContextPopup  m_hContextPopup=%p  m_hSubPopup=%p  m_hOpacityPopup=%p m_hScalePopup=%p m_hMovePopup=%p",
        m_hContextPopup, m_hSubPopup, m_hOpacityPopup, m_hScalePopup, m_hMovePopup);
    CloseMovePopup();
    CloseSubPopup();
    CloseOpacityPopup();
    CloseScalePopup();
    CloseStepPopup();
    CloseSpeedPopup();
    if (m_hContextPopup && IsWindow(m_hContextPopup)) {
        DBG(L"CloseContextPopup  destroying main menu %p", m_hContextPopup);
        DestroyWindow(m_hContextPopup);
    }
    m_hContextPopup = nullptr;
}

void MainWindow::CloseSubPopup() {
    if (m_hSubPopup && IsWindow(m_hSubPopup)) {
        DestroyWindow(m_hSubPopup);
    }
    m_hSubPopup = nullptr;
}

void MainWindow::CloseMovePopup() {
    if (m_hMovePopup && IsWindow(m_hMovePopup)) {
        DestroyWindow(m_hMovePopup);
    }
    m_hMovePopup = nullptr;
}

void MainWindow::CloseOpacityPopup() {
    if (m_hOpacityPopup && IsWindow(m_hOpacityPopup)) {
        DestroyWindow(m_hOpacityPopup);
    }
    m_hOpacityPopup = nullptr;
}

void MainWindow::CloseScalePopup() {
    if (m_hScalePopup && IsWindow(m_hScalePopup)) {
        DestroyWindow(m_hScalePopup);
    }
    m_hScalePopup = nullptr;
}

void MainWindow::CloseStepPopup() {
    if (m_hStepPopup && IsWindow(m_hStepPopup)) {
        DestroyWindow(m_hStepPopup);
    }
    m_hStepPopup = nullptr;
}

void MainWindow::CloseSpeedPopup() {
    if (m_hSpeedPopup && IsWindow(m_hSpeedPopup)) {
        DestroyWindow(m_hSpeedPopup);
    }
    m_hSpeedPopup = nullptr;
}

HWND MainWindow::CreateSubPopup(HWND parent, POINT pt, const std::vector<PopupItem>& items, int /*baseId*/, int& outHeight, HWND* storePtr) {
    if (!storePtr) storePtr = &m_hSubPopup;
    const wchar_t kClass[] = L"PetSubPopupClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = SubPopupProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hbrBackground = GetSysColorBrush(COLOR_MENU);
    RegisterClassW(&wc);

    int h = POPUP_PAD * 2;
    int maxLabel = 0;
    for (const auto& item : items) {
        if (item.type == PopupItem::Separator)
            h += POPUP_SEP_H;
        else {
            h += POPUP_ITEM_H;
            int tw = item.text.size() * 8 + 16;
            if (tw > maxLabel) maxLabel = tw;
        }
    }

    int w = (maxLabel < 120) ? 160 : maxLabel + 20;
    outHeight = h;

    if (pt.x + w > GetSystemMetrics(SM_CXSCREEN))
        pt.x -= POPUP_W + w + 4;
    if (pt.y + h > GetSystemMetrics(SM_CYSCREEN))
        pt.y = GetSystemMetrics(SM_CYSCREEN) - h - 4;
    if (pt.y < 0) pt.y = 4;

    auto* container = new std::vector<PopupItem>(items);
    auto* userData = new SubPopupData{this, container, storePtr, w};

    DWORD exStyle = WS_EX_TOOLWINDOW;
    if ((GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST))
        exStyle |= WS_EX_TOPMOST;

    HWND hSub = CreateWindowExW(exStyle, kClass, L"",
        WS_POPUP | WS_BORDER,
        pt.x, pt.y, w, h,
        nullptr, nullptr, GetModuleHandleW(nullptr), userData);

    if (hSub) {
        *storePtr = hSub;
        SetWindowLongPtrW(hSub, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(userData));
        ShowWindow(hSub, SW_SHOWNA);
    } else {
        delete container;
        delete userData;
    }
    return hSub;
}

void MainWindow::OnContextMenu(HWND hwnd, POINT pt) {
    DBG(L"OnContextMenu called, pt=(%d,%d)  curOp=%d curSc=%d  ctxPopup=%p subPopup=%p opPop=%p scPop=%p",
        pt.x, pt.y, m_currentOpacity, m_scalePercent, m_hContextPopup, m_hSubPopup, m_hOpacityPopup, m_hScalePopup);
    CloseSliderPopup();
    CloseContextPopup();
    m_contextOpacity = m_currentOpacity;
    m_contextScale = m_scalePercent;
    m_contextMoveStep = m_moveStep;
    m_contextMoveSpeed = m_moveSpeed;
    ShowContextPopup(hwnd, pt);
}

void MainWindow::OpenSettings(HWND hwnd) {
    CloseSliderPopup();
    StopMoveTimer();
    SettingsDialog dlg(hwnd, m_moduleManager);
    dlg.Show();
    SyncMoveState();
}

void MainWindow::OpenMenuOrderDialog(HWND hwnd) {
    CloseSliderPopup();
    MenuOrderDialog::Show(hwnd, m_moduleManager);
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
