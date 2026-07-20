#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include "NetworkInfoDialog.h"
#include <iphlpapi.h>
#include <commctrl.h>
#include <vector>
#include <string>

namespace {
    struct AdapterIpEntry {
        std::wstring adapterName;
        std::wstring ipAddress;
    };

    std::vector<AdapterIpEntry> EnumerateAdapters() {
        std::vector<AdapterIpEntry> entries;

        ULONG bufLen = 0;
        GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &bufLen);
        if (bufLen == 0) return entries;

        std::vector<BYTE> buf(bufLen);
        auto* adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());

        ULONG ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapters, &bufLen);
        if (ret != NO_ERROR) return entries;

        wchar_t ipStr[64];
        for (auto* a = adapters; a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue;

            std::wstring desc = a->Description ? a->Description : L"";

            for (auto* ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
                DWORD len = 64;
                ipStr[0] = 0;
                if (WSAAddressToStringW(ua->Address.lpSockaddr,
                    ua->Address.iSockaddrLength, nullptr, ipStr, &len) == 0) {
                    wchar_t* pct = wcschr(ipStr, L'%');
                    if (pct) *pct = 0;
                    entries.push_back({ desc, ipStr });
                }
            }
        }
        return entries;
    }

    constexpr int BTN_W = 80;
    constexpr int BTN_H = 26;
    constexpr int MARGIN = 8;
}

NetworkInfoDialog::NetworkInfoDialog(HWND parent)
    : m_hwnd(nullptr), m_parent(parent),
      m_hListView(nullptr), m_hBtnCopy(nullptr), m_hBtnClose(nullptr) {}

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
    int winW = 520, winH = 350;
    int x = pr.left + (pr.right - pr.left - winW) / 2;
    int y = pr.top + (pr.bottom - pr.top - winH) / 2;

    m_hwnd = CreateWindowExW(0, cls, L"Network Information",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, winW, winH,
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
        RECT rc;
        GetClientRect(hwnd, &rc);
        int lvH = rc.bottom - BTN_H - MARGIN * 2;
        SetWindowPos(m_hListView, nullptr, MARGIN, MARGIN,
            rc.right - MARGIN * 2, lvH, SWP_NOZORDER);
        int btnY = rc.bottom - BTN_H - MARGIN;
        SetWindowPos(m_hBtnCopy, nullptr, MARGIN, btnY, BTN_W, BTN_H, SWP_NOZORDER);
        SetWindowPos(m_hBtnClose, nullptr, rc.right - BTN_W - MARGIN,
            btnY, BTN_W, BTN_H, SWP_NOZORDER);
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == 1001) {
            CopySelectedIp();
        } else if (LOWORD(wParam) == 1002) {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        m_hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void NetworkInfoDialog::Init(HWND hwnd) {
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icex);

    RECT rc;
    GetClientRect(hwnd, &rc);

    m_hListView = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        MARGIN, MARGIN, rc.right - MARGIN * 2, rc.bottom - BTN_H - MARGIN * 2,
        hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

    ListView_SetExtendedListViewStyleEx(m_hListView,
        LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);

    wchar_t colIf[] = L"Network Interface";
    wchar_t colIp[] = L"IP Address";
    LVCOLUMNW lvc;
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvc.fmt = LVCFMT_LEFT;
    lvc.cx = 300;
    lvc.pszText = colIf;
    ListView_InsertColumn(m_hListView, 0, &lvc);

    lvc.cx = 160;
    lvc.pszText = colIp;
    ListView_InsertColumn(m_hListView, 1, &lvc);

    m_hBtnCopy = CreateWindowW(L"BUTTON", L"Copy",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        MARGIN, rc.bottom - BTN_H - MARGIN, BTN_W, BTN_H,
        hwnd, reinterpret_cast<HMENU>(1001), GetModuleHandleW(nullptr), nullptr);

    m_hBtnClose = CreateWindowW(L"BUTTON", L"Close",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        rc.right - BTN_W - MARGIN, rc.bottom - BTN_H - MARGIN, BTN_W, BTN_H,
        hwnd, reinterpret_cast<HMENU>(1002), GetModuleHandleW(nullptr), nullptr);

    auto entries = EnumerateAdapters();
    for (const auto& e : entries) {
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.pszText = const_cast<wchar_t*>(e.adapterName.c_str());
        item.iItem = ListView_GetItemCount(m_hListView);
        ListView_InsertItem(m_hListView, &item);
        ListView_SetItemText(m_hListView, item.iItem, 1,
            const_cast<wchar_t*>(e.ipAddress.c_str()));
    }
}

void NetworkInfoDialog::CopySelectedIp() {
    int sel = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);
    if (sel < 0) return;

    wchar_t ip[128];
    ListView_GetItemText(m_hListView, sel, 1, ip, 128);

    if (!OpenClipboard(m_hwnd)) return;
    EmptyClipboard();

    size_t len = wcslen(ip);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
    if (h) {
        wcscpy(static_cast<wchar_t*>(GlobalLock(h)), ip);
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}
