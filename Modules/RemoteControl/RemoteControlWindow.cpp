//==============================================================================
// RemoteControlWindow.cpp - Remote Control status and drop window
//==============================================================================

#include <shobjidl.h>
#include "RemoteControlWindow.h"
#include <shellapi.h>
#include <cstdio>

RemoteControlWindow::RemoteControlWindow(HINSTANCE hInst)
    : m_hInst(hInst) {}

RemoteControlWindow::~RemoteControlWindow() {
    Destroy();
}

void RemoteControlWindow::RegisterWindowClass() {
    static bool registered = false;
    if (registered) return;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = m_hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
    wc.lpszClassName = L"RemoteControlDropWnd";
    RegisterClassExW(&wc);
    registered = true;
}

bool RemoteControlWindow::Create(HWND parent) {
    if (m_hwnd) return true;

    RegisterWindowClass();

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_ACCEPTFILES,
        L"RemoteControlDropWnd", L"Remote Control",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, kWidth, kHeight,
        parent, nullptr, m_hInst, this);

    if (!m_hwnd) return false;

    DragAcceptFiles(m_hwnd, TRUE);

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    m_saveEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", m_savePath.c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        76, 8, 200, 24, m_hwnd,
        nullptr, m_hInst, nullptr);

    m_browseBtn = CreateWindowW(L"BUTTON", L"Browse",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        282, 8, 70, 24, m_hwnd,
        reinterpret_cast<HMENU>(1001), m_hInst, nullptr);

    CreateWindowW(L"STATIC", L"Save Path:",
        WS_CHILD | WS_VISIBLE,
        8, 10, 68, 20, m_hwnd,
        nullptr, m_hInst, nullptr);

    m_helpBtn = CreateWindowW(L"BUTTON", L"?",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        kWidth - 30, kHeight - 30, 24, 24, m_hwnd,
        reinterpret_cast<HMENU>(1002), m_hInst, nullptr);

    return true;
}

void RemoteControlWindow::Show(int cmdShow) {
    if (m_hwnd) ShowWindow(m_hwnd, cmdShow);
}

void RemoteControlWindow::Destroy() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void RemoteControlWindow::SetConnected(bool connected) {
    m_connected = connected;
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
        UpdateWindow(m_hwnd);
    }
}

void RemoteControlWindow::SetHookActive(bool active) {
    m_hookActive = active;
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
        UpdateWindow(m_hwnd);
    }
}

void RemoteControlWindow::SetSavePath(const std::wstring& path) {
    m_savePath = path;
    if (m_saveEdit) SetWindowTextW(m_saveEdit, path.c_str());
}

std::wstring RemoteControlWindow::GetSavePath() const {
    if (m_saveEdit) {
        wchar_t buf[MAX_PATH] = {};
        GetWindowTextW(m_saveEdit, buf, MAX_PATH);
        return buf;
    }
    return m_savePath;
}

void RemoteControlWindow::SetDropCallback(DropCallback cb, void* ud) {
    m_dropCb = cb;
    m_dropUd = ud;
}

void RemoteControlWindow::ShowHelpPopup(HWND parent) {
    MessageBoxW(parent,
        L"Remote Control Operation Guide\n\n"
        L"[F9] Toggle control mode\n"
        L"  Press F9 to start/stop capturing keyboard and mouse input.\n"
        L"  When active, input is sent to the remote machine.\n\n"
        L"[Connection]\n"
        L"  The module runs both Server and Client simultaneously.\n"
        L"  Server: listens for incoming connections (Target role).\n"
        L"  Client: connects to the remote machine (Controller role).\n\n"
        L"[File Transfer]\n"
        L"  Drag and drop files onto this window to send them\n"
        L"  to the remote machine's configured save path.\n\n"
        L"[Status LEDs]\n"
        L"  Connect: lights up when a remote connection is established.\n"
        L"  Control: lights up when F9 control is active.\n\n"
        L"[Configuration]\n"
        L"  Open Settings -> Remote Control tab to configure\n"
        L"  IP, ports, passwords, and save path.\n",
        L"Remote Control Help",
        MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK RemoteControlWindow::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<RemoteControlWindow*>(
        GetWindowLongPtrW(h, GWLP_USERDATA));

    switch (m) {

    case WM_COMMAND: {
        if (!self) break;
        if (LOWORD(w) == 1001) {
            IFileDialog* pfd = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                           CLSCTX_ALL, IID_PPV_ARGS(&pfd)))) {
                DWORD opts = 0;
                pfd->GetOptions(&opts);
                pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                if (SUCCEEDED(pfd->Show(h))) {
                    IShellItem* psi = nullptr;
                    if (SUCCEEDED(pfd->GetResult(&psi))) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                            self->SetSavePath(path);
                            CoTaskMemFree(path);
                        }
                        psi->Release();
                    }
                }
                pfd->Release();
            }
            return 0;
        }
        if (LOWORD(w) == 1002) {
            ShowHelpPopup(h);
            return 0;
        }
        break;
    }

    case WM_DROPFILES: {
        if (!self) break;
        HDROP hd = reinterpret_cast<HDROP>(w);
        UINT cnt = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < cnt; i++) {
            wchar_t path[MAX_PATH];
            if (DragQueryFileW(hd, i, path, MAX_PATH)) {
                if (self->m_dropCb)
                    self->m_dropCb(path, self->m_dropUd);
            }
        }
        DragFinish(hd);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r; GetClientRect(h, &r);

        // "Drop Files Here" banner
        RECT textRect = r;
        textRect.top = 40; textRect.bottom = r.bottom - 50;
        HFONT hf = CreateFontW(24, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                               DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Segoe UI");
        HFONT old = static_cast<HFONT>(SelectObject(dc, hf));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(80, 80, 80));
        DrawTextW(dc, L"\x2193 Drop Files Here \x2193", -1, &textRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, old);
        DeleteObject(hf);

        // Use default font for labels
        HFONT hfLabel = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                     DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH, L"Segoe UI");
        SelectObject(dc, hfLabel);

        // Connect LED + label (bottom-left)
        {
            int ledX = 40, ledY = r.bottom - 40, radius = 8;
            COLORREF ledC = self && self->m_connected ? RGB(0, 200, 0) : RGB(160, 160, 160);
            HBRUSH hb = CreateSolidBrush(ledC);
            HPEN hp = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
            SelectObject(dc, hb); SelectObject(dc, hp);
            Ellipse(dc, ledX - radius, ledY - radius, ledX + radius, ledY + radius);
            SelectObject(dc, GetStockObject(DC_BRUSH));
            SelectObject(dc, GetStockObject(DC_PEN));
            DeleteObject(hb); DeleteObject(hp);

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(60, 60, 60));
            TextOutW(dc, ledX + 14, ledY - 8, L"Connect", 7);
        }

        // Control LED + label (bottom-center-left)
        {
            int ledX = 160, ledY = r.bottom - 40, radius = 8;
            COLORREF ledC = self && self->m_hookActive ? RGB(0, 210, 0) : RGB(160, 160, 160);
            HBRUSH hb = CreateSolidBrush(ledC);
            HPEN hp = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
            SelectObject(dc, hb); SelectObject(dc, hp);
            Ellipse(dc, ledX - radius, ledY - radius, ledX + radius, ledY + radius);
            SelectObject(dc, GetStockObject(DC_BRUSH));
            SelectObject(dc, GetStockObject(DC_PEN));
            DeleteObject(hb); DeleteObject(hp);

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(60, 60, 60));
            TextOutW(dc, ledX + 14, ledY - 8, L"Control", 7);
        }

        SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        DeleteObject(hfLabel);

        EndPaint(h, &ps);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        if (self) self->m_hwnd = nullptr;
        return 0;
    }

    return DefWindowProcW(h, m, w, l);
}
