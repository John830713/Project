#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include "NetworkInfoDialog.h"
#include <iphlpapi.h>
#include <vector>
#include <string>
#include <commctrl.h>

namespace {
    struct AdapterIpEntry {
        std::wstring adapterName;
        std::wstring ipAddress;
    };

    std::vector<AdapterIpEntry> EnumerateAdaptersIpv4() {
        std::vector<AdapterIpEntry> entries;

        ULONG bufLen = 0;
        GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &bufLen);
        if (bufLen == 0) return entries;

        std::vector<BYTE> buf(bufLen);
        auto* adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());

        ULONG ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapters, &bufLen);
        if (ret != NO_ERROR) return entries;

        wchar_t ipStr[64];
        for (auto* a = adapters; a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue;

            std::wstring desc = a->Description ? a->Description : L"";

            for (auto* ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
                if (ua->Address.lpSockaddr->sa_family != AF_INET) continue;
                DWORD len = 64;
                ipStr[0] = 0;
                if (WSAAddressToStringW(ua->Address.lpSockaddr,
                    ua->Address.iSockaddrLength, nullptr, ipStr, &len) == 0) {
                    entries.push_back({ desc, ipStr });
                }
            }
        }
        return entries;
    }

    constexpr int MARGIN = 8;
    constexpr int GAP = 3;
    constexpr int HEADER_H = 22;
    constexpr int ROW_H = 26;
    constexpr int BTN_SZ = 22;
    constexpr int WIN_W = 460;
    constexpr int WIN_MIN_H = 200;
    constexpr int CLOSE_BTN_W = 80;
    constexpr int CLOSE_BTN_H = 26;
    constexpr int CLOSE_AREA_H = 48;
    constexpr int SCROLL_STEP = 36;
    constexpr int ID_CLOSE = 10000;
    constexpr int ID_COPY_BASE = 20000;

    struct CopyBtnTag {
        int rowIdx;
    };
}

NetworkInfoDialog::NetworkInfoDialog(HWND parent)
    : m_hwnd(nullptr), m_parent(parent),
      m_hBtnClose(nullptr), m_scrollPos(0), m_contentH(0),
      m_nextBtnId(ID_COPY_BASE), m_hCopyIcon(nullptr) {}

INT_PTR NetworkInfoDialog::Show() {
    const wchar_t* cls = L"NetworkInfoWindow";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = cls;
    RegisterClassW(&wc);

    RECT pr;
    GetWindowRect(m_parent, &pr);
    int x = pr.left + (pr.right - pr.left - WIN_W) / 2;
    int y = pr.top + (pr.bottom - pr.top - WIN_MIN_H) / 2;

    m_hwnd = CreateWindowExW(0, cls, L"Network Information",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VSCROLL,
        x, y, WIN_W, WIN_MIN_H,
        m_parent, nullptr, GetModuleHandleW(nullptr), this);

    if (!m_hwnd) return -1;

    ShowWindow(m_hwnd, SW_SHOW);

    MSG msg;
    while (IsWindow(m_hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

LRESULT CALLBACK NetworkInfoDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = static_cast<NetworkInfoDialog*>(cs->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->Init(hwnd);
        return 0;
    }
    auto* self = reinterpret_cast<NetworkInfoDialog*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT NetworkInfoDialog::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE: {
        int newH = HIWORD(lParam);
        if (m_contentH > newH) {
            ShowScrollBar(hwnd, SB_VERT, TRUE);
        } else {
            ShowScrollBar(hwnd, SB_VERT, FALSE);
            m_scrollPos = 0;
        }
        SCROLLINFO si;
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = m_contentH;
        si.nPage = (UINT)newH;
        si.nPos = m_scrollPos;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        Layout();
        return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO si = { sizeof(si), SIF_ALL };
        GetScrollInfo(hwnd, SB_VERT, &si);
        switch (LOWORD(wParam)) {
        case SB_LINEUP: si.nPos -= SCROLL_STEP; break;
        case SB_LINEDOWN: si.nPos += SCROLL_STEP; break;
        case SB_PAGEUP: si.nPos -= si.nPage; break;
        case SB_PAGEDOWN: si.nPos += si.nPage; break;
        case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
        case SB_TOP: si.nPos = 0; break;
        case SB_BOTTOM: si.nPos = si.nMax; break;
        }
        if (si.nPos < 0) si.nPos = 0;
        int maxPos = si.nMax - (int)si.nPage;
        if (si.nPos > maxPos) si.nPos = maxPos;
        if (si.nPos < 0) si.nPos = 0;
        if (si.nPos != m_scrollPos) {
            int dy = m_scrollPos - si.nPos;
            m_scrollPos = si.nPos;
            si.fMask = SIF_POS;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            ScrollWindow(hwnd, 0, dy, nullptr, nullptr);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int steps = delta / WHEEL_DELTA;
        SCROLLINFO si = { sizeof(si), SIF_ALL };
        GetScrollInfo(hwnd, SB_VERT, &si);
        si.nPos -= steps * SCROLL_STEP;
        if (si.nPos < 0) si.nPos = 0;
        int maxPos = si.nMax - (int)si.nPage;
        if (si.nPos > maxPos) si.nPos = maxPos;
        if (si.nPos < 0) si.nPos = 0;
        if (si.nPos != m_scrollPos) {
            int dy = m_scrollPos - si.nPos;
            m_scrollPos = si.nPos;
            si.fMask = SIF_POS;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            ScrollWindow(hwnd, 0, dy, nullptr, nullptr);
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_CLOSE) {
            DestroyWindow(hwnd);
        } else if (id >= ID_COPY_BASE) {
            int idx = id - ID_COPY_BASE;
            OnCopy(idx);
        }
        return 0;
    }
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis->CtlType == ODT_BUTTON && dis->CtlID >= ID_COPY_BASE) {
            int idx = dis->CtlID - ID_COPY_BASE;
            if (idx >= 0 && idx < (int)m_rows.size() && dis->hwndItem == m_rows[idx].hBtn) {
                bool hover = (dis->itemState & ODS_HOTLIGHT);
                bool pressed = (dis->itemState & ODS_SELECTED);
                RECT rc = dis->rcItem;
                HDC dc = dis->hDC;

                if (pressed) {
                    FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNSHADOW));
                } else if (hover) {
                    FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNHIGHLIGHT));
                } else {
                    FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
                }
                FrameRect(dc, &rc, GetSysColorBrush(COLOR_BTNTEXT));

                int cx = rc.left + (rc.right - rc.left - 14) / 2;
                int cy = rc.top + (rc.bottom - rc.top - 14) / 2;
                DrawCopyGlyph(dc, cx, cy, 14, 14);
                return TRUE;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        m_hwnd = nullptr;
        if (m_hCopyIcon) { DestroyIcon(m_hCopyIcon); m_hCopyIcon = nullptr; }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void NetworkInfoDialog::Init(HWND hwnd) {
    auto entries = EnumerateAdaptersIpv4();

    if (entries.empty()) {
        m_contentH = 0;
        Layout();
        return;
    }

    // Group entries by adapter name
    m_groups.clear();
    m_rows.clear();
    std::wstring lastName;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (e.adapterName != lastName) {
            lastName = e.adapterName;
            AdapterGroup g;
            g.name = e.adapterName;
            g.hHeader = nullptr;
            m_groups.push_back(g);
        }
        IpRow row;
        row.ip = e.ipAddress;
        row.hText = nullptr;
        row.hBtn = nullptr;
        row.id = m_nextBtnId++;
        m_groups.back().rows.push_back((int)m_rows.size());
        m_rows.push_back(row);
    }

    // Create close button
    m_hBtnClose = CreateWindowW(L"BUTTON", L"Close",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, CLOSE_BTN_W, CLOSE_BTN_H,
        hwnd, reinterpret_cast<HMENU>(ID_CLOSE), GetModuleHandleW(nullptr), nullptr);

    // Create bold font for headers
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ncm.lfMenuFont.lfWeight = FW_BOLD;
    HFONT hBoldFont = CreateFontIndirectW(&ncm.lfMenuFont);

    // Create controls
    HICON hCopyIcon = (HICON)LoadImageW(GetModuleHandleW(L"shell32.dll"),
        MAKEINTRESOURCEW(261), IMAGE_ICON, 14, 14, LR_SHARED);

    for (auto& g : m_groups) {
        g.hHeader = CreateWindowW(L"STATIC", g.name.c_str(),
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, HEADER_H,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g.hHeader, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    }

    DWORD btnStyle = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
    for (auto& row : m_rows) {
        row.hText = CreateWindowW(L"STATIC", row.ip.c_str(),
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, ROW_H,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        row.hBtn = CreateWindowW(L"BUTTON", L"",
            btnStyle,
            0, 0, BTN_SZ, BTN_SZ,
            hwnd, reinterpret_cast<HMENU>(row.id), GetModuleHandleW(nullptr), nullptr);

        if (hCopyIcon) {
            LONG_PTR style = GetWindowLongPtrW(row.hBtn, GWL_STYLE);
            style |= BS_ICON;
            style &= ~BS_OWNERDRAW;
            SetWindowLongPtrW(row.hBtn, GWL_STYLE, style);
            SendMessageW(row.hBtn, BM_SETIMAGE, IMAGE_ICON, (LPARAM)hCopyIcon);
        }
    }

    if (!hCopyIcon) {
        m_hCopyIcon = nullptr;
    } else {
        m_hCopyIcon = hCopyIcon;
    }

    // Compute content height and size window
    int contentH = Layout();
    m_contentH = contentH;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int winH = (std::min)(contentH + CLOSE_AREA_H, 500);
    winH = (std::max)(winH, WIN_MIN_H);
    SetWindowPos(hwnd, nullptr, 0, 0, WIN_W, winH,
        SWP_NOMOVE | SWP_NOZORDER);

    // Center on parent
    RECT wr;
    GetWindowRect(hwnd, &wr);
    RECT pr;
    GetWindowRect(m_parent, &pr);
    int cx = pr.left + (pr.right - pr.left - (wr.right - wr.left)) / 2;
    int cy = pr.top + (pr.bottom - pr.top - (wr.bottom - wr.top)) / 2;
    SetWindowPos(hwnd, nullptr, cx, cy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

int NetworkInfoDialog::Layout() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int w = rc.right - rc.left;
    int y = MARGIN - m_scrollPos;
    int textW = w - MARGIN * 2 - BTN_SZ - GAP;

    for (auto& g : m_groups) {
        y += GAP;
        SetWindowPos(g.hHeader, nullptr, MARGIN, y, w - MARGIN * 2, HEADER_H,
            SWP_NOZORDER);
        y += HEADER_H;

        for (int ri : g.rows) {
            auto& row = m_rows[ri];
            y += GAP;
            SetWindowPos(row.hText, nullptr, MARGIN, y, textW, ROW_H,
                SWP_NOZORDER);
            SetWindowPos(row.hBtn, nullptr, w - MARGIN - BTN_SZ, y, BTN_SZ, BTN_SZ,
                SWP_NOZORDER);
            y += ROW_H;
        }
    }

    int contentEnd = y + m_scrollPos;
    int closeBtnY = contentEnd + CLOSE_AREA_H / 2 - CLOSE_BTN_H / 2 - m_scrollPos;
    SetWindowPos(m_hBtnClose, nullptr,
        (w - CLOSE_BTN_W) / 2, closeBtnY,
        CLOSE_BTN_W, CLOSE_BTN_H, SWP_NOZORDER);

    return contentEnd + CLOSE_AREA_H;
}

void NetworkInfoDialog::OnCopy(int idx) {
    if (idx < 0 || idx >= (int)m_rows.size()) return;
    const std::wstring& ip = m_rows[idx].ip;

    if (!OpenClipboard(m_hwnd)) return;
    EmptyClipboard();

    size_t len = ip.size();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
    if (h) {
        wcscpy(static_cast<wchar_t*>(GlobalLock(h)), ip.c_str());
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}

void NetworkInfoDialog::DrawCopyGlyph(HDC dc, int x, int y, int w, int h) {
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
    auto oldPen = (HPEN)SelectObject(dc, pen);
    auto oldBrush = (HBRUSH)SelectObject(dc, GetStockObject(WHITE_BRUSH));

    int p = 1;
    int s = 1;
    int ox = s;

    // Back document (offset right + down)
    RECT r2 = { x + ox + p, y + ox + p, x + ox + w - p, y + ox + h - p };
    Rectangle(dc, r2.left, r2.top, r2.right, r2.bottom);

    // Front document
    RECT r1 = { x + p, y + p, x + w - p, y + h - p };
    Rectangle(dc, r1.left, r1.top, r1.right, r1.bottom);

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
}
