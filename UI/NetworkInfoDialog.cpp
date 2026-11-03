#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include "NetworkInfoDialog.h"
#include "../Services/ProcessRunner.h"
#include "../Services/TranslationService.h"
#include <iphlpapi.h>
#include <vector>
#include <string>
#include <commctrl.h>
#include <shellapi.h>

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
    constexpr int WIN_W = 520;
    constexpr int WIN_MIN_H = 260;
    constexpr int CLOSE_BTN_W = 80;
    constexpr int CLOSE_BTN_H = 26;
    constexpr int CLOSE_AREA_H = 48;
    constexpr int SCROLL_STEP = 36;
    constexpr int TAB_H = 26;
    constexpr int ID_CLOSE = 10000;
    constexpr int ID_COPY_BASE = 20000;
    constexpr int ID_REMOTE_REFRESH = 21001;
    constexpr int ID_REMOTE_BACKUP = 21002;
    constexpr int ID_REMOTE_RESTORE = 21003;
    constexpr int ID_REMOTE_APPLY = 21004;
    constexpr int ID_REMOTE_ITEM_BASE = 21100;

    struct CopyBtnTag {
        int rowIdx;
    };
}

NetworkInfoDialog::NetworkInfoDialog(HWND parent)
    : m_hwnd(nullptr), m_parent(parent),
      m_hTab(nullptr), m_hBtnClose(nullptr), m_currentTab(0),
      m_scrollPos(0), m_contentH(0),
      m_nextBtnId(ID_COPY_BASE), m_hCopyIcon(nullptr) {
    m_remoteStatus = RemoteInitService::Get()->CheckEnvironment();
    m_remoteItems = RemoteInitService::Get()->GetDefaultItems(m_remoteStatus);
}

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

    m_hwnd = CreateWindowExW(0, cls, L"Network Information / Remote Init",
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
    case WM_NOTIFY: {
        auto* nmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (nmhdr->hwndFrom == m_hTab && nmhdr->code == TCN_SELCHANGE) {
            m_currentTab = TabCtrl_GetCurSel(m_hTab);
            // clear network views
            for (auto& g : m_groups) {
                if (IsWindow(g.hHeader)) DestroyWindow(g.hHeader);
            }
            for (auto& r : m_rows) {
                if (IsWindow(r.hText)) DestroyWindow(r.hText);
                if (IsWindow(r.hBtn)) DestroyWindow(r.hBtn);
            }
            m_groups.clear();
            m_rows.clear();
            for (auto h : m_remoteCtrls) if (IsWindow(h)) DestroyWindow(h);
            m_remoteCtrls.clear();
            if (m_hBtnClose) { DestroyWindow(m_hBtnClose); m_hBtnClose = nullptr; }
            if (m_currentTab == 0) {
                InitNetworkTab(hwnd);
            } else {
                InitRemoteTab(hwnd);
            }
            return 0;
        }
        break;
    }
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
            return 0;
        } else if (id >= ID_COPY_BASE && id < ID_COPY_BASE + 10000) {
            int idx = id - ID_COPY_BASE;
            OnCopy(idx);
            return 0;
        }
        if (m_currentTab == 1) {
            OnRemoteCommand(hwnd, wParam);
            return 0;
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
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_TAB_CLASSES };
    InitCommonControlsEx(&icex);

    RECT rc;
    GetClientRect(hwnd, &rc);

    m_hTab = CreateWindowW(WC_TABCONTROL, L"",
        WS_CHILD | WS_VISIBLE | TCS_FIXEDWIDTH,
        MARGIN, MARGIN, rc.right - MARGIN * 2, TAB_H,
        hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

    TCITEMW tie = {};
    tie.mask = TCIF_TEXT;
    tie.pszText = const_cast<wchar_t*>(L"網路資訊");
    SendMessageW(m_hTab, TCM_INSERTITEM, 0, (LPARAM)&tie);
    tie.pszText = const_cast<wchar_t*>(L"遠端控制初始化");
    SendMessageW(m_hTab, TCM_INSERTITEM, 1, (LPARAM)&tie);

    m_currentTab = 0;
    TabCtrl_SetCurSel(m_hTab, 0);
    InitNetworkTab(hwnd);

    RECT wr;
    GetWindowRect(hwnd, &wr);
    RECT pr;
    GetWindowRect(m_parent, &pr);
    int cx = pr.left + (pr.right - pr.left - (wr.right - wr.left)) / 2;
    int cy = pr.top + (pr.bottom - pr.top - (wr.bottom - wr.top)) / 2;
    SetWindowPos(hwnd, nullptr, cx, cy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void NetworkInfoDialog::InitNetworkTab(HWND hwnd) {
    auto entries = EnumerateAdaptersIpv4();

    // Create close button
    if (!m_hBtnClose || !IsWindow(m_hBtnClose)) {
        m_hBtnClose = CreateWindowW(L"BUTTON", L"關閉",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, CLOSE_BTN_W, CLOSE_BTN_H,
            hwnd, reinterpret_cast<HMENU>(ID_CLOSE), GetModuleHandleW(nullptr), nullptr);
    } else {
        ShowWindow(m_hBtnClose, SW_SHOW);
    }

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

    RECT rc2;
    GetClientRect(hwnd, &rc2);
    int winH = (std::min)(contentH + CLOSE_AREA_H, 500);
    winH = (std::max)(winH, WIN_MIN_H);
    SetWindowPos(hwnd, nullptr, 0, 0, WIN_W, winH,
        SWP_NOMOVE | SWP_NOZORDER);
}

void NetworkInfoDialog::InitRemoteTab(HWND hwnd) {
    for (auto h : m_remoteCtrls) if (IsWindow(h)) DestroyWindow(h);
    m_remoteCtrls.clear();
    if (m_hBtnClose) { DestroyWindow(m_hBtnClose); m_hBtnClose = nullptr; }

    m_remoteStatus = RemoteInitService::Get()->CheckEnvironment();
    m_remoteItems = RemoteInitService::Get()->GetDefaultItems(m_remoteStatus);

    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);

    int y = MARGIN + TAB_H + 8;
    {
        std::wstring info = L"系統：";
        if (m_remoteStatus.isElevated) info += L"已管理員權限  ";
        else info += L"非管理員權限  ";
        if (m_remoteStatus.isWindowsHome) info += L"Windows Home  ";
        info += L"產品：" + m_remoteStatus.productName + L"  ";
        info += L"網路：" + m_remoteStatus.networkCategory + L"\r\n";
        info += L"RDP：" + std::wstring(m_remoteStatus.rdpDeny == 0 ? L"已啟用" : L"未啟用") + L"  ";
        info += L"NLA：" + std::wstring(m_remoteStatus.nlaUserAuth == 1 ? L"啟用" : L"停用") + L"  ";
        info += L"空白密碼限制：" + std::wstring(m_remoteStatus.limitBlankPass == 1 ? L"限制" : L"允許") + L"  ";
        info += L"WinRM：" + std::wstring(m_remoteStatus.winrmServiceRunning ? L"執行中" : L"未執行");
        HWND hInfo = CreateWindowW(L"STATIC", info.c_str(), WS_CHILD | WS_VISIBLE,
            MARGIN, y, WIN_W - MARGIN * 2, 42, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (hFont) SendMessageW(hInfo, WM_SETFONT, (WPARAM)hFont, TRUE);
        m_remoteCtrls.push_back(hInfo);
        y += 48;
    }
    for (size_t i = 0; i < m_remoteItems.size(); ++i) {
        const auto& it = m_remoteItems[i];
        UINT flags = WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX;
        if (!it.selectable) flags |= WS_DISABLED;
        std::wstring lbl = it.label + L"  -  " + it.desc;
        HWND hChk = CreateWindowW(L"BUTTON", lbl.c_str(), flags,
            MARGIN, y, WIN_W - MARGIN * 2, 20, hwnd, (HMENU)(ID_REMOTE_ITEM_BASE + (int)i), GetModuleHandleW(nullptr), nullptr);
        if (hFont) SendMessageW(hChk, WM_SETFONT, (WPARAM)hFont, TRUE);
        if (it.checked && it.selectable) SendMessageW(hChk, BM_SETCHECK, BST_CHECKED, 0);
        m_remoteCtrls.push_back(hChk);
        y += 24;
    }
    y += 10;
    HWND hRefresh = CreateWindowW(L"BUTTON", L"重新檢查", WS_CHILD | WS_VISIBLE,
        MARGIN, y, 90, CLOSE_BTN_H, hwnd, (HMENU)ID_REMOTE_REFRESH, GetModuleHandleW(nullptr), nullptr);
    if (hFont) SendMessageW(hRefresh, WM_SETFONT, (WPARAM)hFont, TRUE);
    HWND hBackup = CreateWindowW(L"BUTTON", L"備份目前設定", WS_CHILD | WS_VISIBLE,
        MARGIN + 98, y, 110, CLOSE_BTN_H, hwnd, (HMENU)ID_REMOTE_BACKUP, GetModuleHandleW(nullptr), nullptr);
    if (hFont) SendMessageW(hBackup, WM_SETFONT, (WPARAM)hFont, TRUE);
    HWND hRestore = CreateWindowW(L"BUTTON", L"從備份還原", WS_CHILD | WS_VISIBLE,
        MARGIN + 216, y, 110, CLOSE_BTN_H, hwnd, (HMENU)ID_REMOTE_RESTORE, GetModuleHandleW(nullptr), nullptr);
    if (hFont) SendMessageW(hRestore, WM_SETFONT, (WPARAM)hFont, TRUE);
    HWND hApply = CreateWindowW(L"BUTTON", L"套用所選項目", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        WIN_W - 130 - MARGIN, y, 130, CLOSE_BTN_H, hwnd, (HMENU)ID_REMOTE_APPLY, GetModuleHandleW(nullptr), nullptr);
    if (hFont) SendMessageW(hApply, WM_SETFONT, (WPARAM)hFont, TRUE);
    m_remoteCtrls.push_back(hRefresh);
    m_remoteCtrls.push_back(hBackup);
    m_remoteCtrls.push_back(hRestore);
    m_remoteCtrls.push_back(hApply);

    int winH = y + CLOSE_BTN_H + 30;
    if (winH < WIN_MIN_H) winH = WIN_MIN_H;
    SetWindowPos(hwnd, nullptr, 0, 0, WIN_W, winH, SWP_NOMOVE | SWP_NOZORDER);
    m_contentH = winH;
}

void NetworkInfoDialog::OnRemoteCommand(HWND hwnd, WPARAM wParam) {
    int id = LOWORD(wParam);
    if (id == ID_REMOTE_REFRESH) {
        InitRemoteTab(hwnd);
        return;
    }
    if (id == ID_REMOTE_BACKUP) {
        m_remoteBackupPath = RemoteInitService::Get()->CreateBackup(m_remoteStatus);
        MessageBoxW(hwnd, (L"已備份目前設定：\r\n" + m_remoteBackupPath).c_str(), L"備份完成", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (id == ID_REMOTE_RESTORE) {
        if (m_remoteBackupPath.empty()) {
            MessageBoxW(hwnd, L"尚未建立備份", L"還原", MB_OK | MB_ICONWARNING);
            return;
        }
        if (MessageBoxW(hwnd, L"確定要從最近一次備份還原設定？", L"還原確認", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            if (RemoteInitService::Get()->RestoreBackup(m_remoteBackupPath)) {
                MessageBoxW(hwnd, L"還原完成", L"還原", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(hwnd, L"還原失敗（簡化實作僅支援部分項目）", L"還原", MB_OK | MB_ICONWARNING);
            }
            InitRemoteTab(hwnd);
        }
        return;
    }
    if (id == ID_REMOTE_APPLY) {
        if (!m_remoteStatus.isElevated) {
            if (MessageBoxW(hwnd, L"目前不是以系統管理員身分執行。\r\n建議以系統管理員身分重新啟動程式後再執行。\r\n是否要以管理員身分重新啟動？", L"需要管理員權限", MB_YESNO | MB_ICONWARNING) == IDYES) {
                ProcessRunner::RestartElevated(L"");
                PostQuitMessage(0);
            }
            return;
        }
        for (size_t i = 0; i < m_remoteItems.size(); ++i) {
            for (auto h : m_remoteCtrls) {
                if (GetWindowLongPtrW(h, GWLP_ID) == (LONG_PTR)(ID_REMOTE_ITEM_BASE + (int)i)) {
                    m_remoteItems[i].checked = (SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    break;
                }
            }
        }
        wchar_t userName[256] = {};
        DWORD sz = 256;
        GetUserNameW(userName, &sz);
        auto res = RemoteInitService::Get()->ApplySelected(m_remoteItems, userName);
        std::wstring log = L"執行結果：\r\n\r\n";
        for (auto& l : res.logs) log += l + L"\r\n";
        if (res.allOk) log += L"\r\n全部項目執行完成。";
        else log += L"\r\n部分項目執行失敗，詳見 Log。";
        MessageBoxW(hwnd, log.c_str(), L"Remote Init 結果", MB_OK | MB_ICONINFORMATION);
        InitRemoteTab(hwnd);
        return;
    }
}

int NetworkInfoDialog::Layout() {
    if (m_currentTab == 1) {
        return m_contentH;
    }
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int w = rc.right - rc.left;
    int y = MARGIN + TAB_H + 6 - m_scrollPos;
    int textW = w - MARGIN * 2 - BTN_SZ - GAP;

    for (auto& g : m_groups) {
        if (!IsWindow(g.hHeader)) continue;
        y += GAP;
        SetWindowPos(g.hHeader, nullptr, MARGIN, y, w - MARGIN * 2, HEADER_H,
            SWP_NOZORDER);
        y += HEADER_H;

        for (int ri : g.rows) {
            if (ri < 0 || ri >= (int)m_rows.size()) continue;
            auto& row = m_rows[ri];
            if (!IsWindow(row.hText) || !IsWindow(row.hBtn)) continue;
            y += GAP;
            SetWindowPos(row.hText, nullptr, MARGIN, y, textW, ROW_H,
                SWP_NOZORDER);
            SetWindowPos(row.hBtn, nullptr, w - MARGIN - BTN_SZ, y, BTN_SZ, BTN_SZ,
                SWP_NOZORDER);
            y += ROW_H;
        }
    }

    int contentEnd = y + m_scrollPos;
    if (m_hBtnClose && IsWindow(m_hBtnClose)) {
        int closeBtnY = contentEnd + CLOSE_AREA_H / 2 - CLOSE_BTN_H / 2 - m_scrollPos;
        SetWindowPos(m_hBtnClose, nullptr,
            (w - CLOSE_BTN_W) / 2, closeBtnY,
            CLOSE_BTN_W, CLOSE_BTN_H, SWP_NOZORDER);
    }

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
