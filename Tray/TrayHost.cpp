#include "TrayHost.h"

#include "../HostApp.h"
#include "../Core/ConfigManager.h"
#include "../Core/IFeatureModule.h"
#include "../Core/ModuleManager.h"
#include "../Core/ResourceIds.h"
#include "../Core/Logger.h"
#include "../Services/TranslationService.h"

#include <windowsx.h>
#include <algorithm>
#include <cstdio>
#include <map>

namespace {

constexpr int kTrayIconId = 1;
constexpr int kWatchdogIntervalMs = 3000;
constexpr UINT kTrayCallbackMessage = WM_APP + 12;
constexpr wchar_t kTrayWindowClass[] = L"TrayHostMessageWindow";

// Host-level tray command IDs. The pet window owns 100-111 and 2000+/6000+,
// modules go through ID_MENU_BASE (2000) offsets, so the tray keeps its own
// high band to stay clear of all of them.
enum {
    ID_TRAY_TOGGLE_PET = 12000,
    ID_TRAY_SETTINGS = 12001,
    ID_TRAY_RESET_PET = 12003,
    ID_TRAY_MODULE_BASE = 12100,
    ID_TRAY_EXIT = 12999
};

std::wstring Format(const wchar_t* fmt, unsigned long value) {
    wchar_t buffer[64] = {};
    swprintf_s(buffer, fmt, value);
    return buffer;
}

}  // namespace

TrayHost::TrayHost(HostApp* host)
    : m_host(host), m_hwnd(nullptr), m_icon(nullptr),
      m_iconAdded(false), m_shuttingDown(false) {}

TrayHost::~TrayHost() {
    RemoveIcon();
    if (m_icon) {
        DestroyIcon(m_icon);
        m_icon = nullptr;
    }
}

std::wstring TrayHost::Tr(const wchar_t* text) {
    return TranslationService::Get()->Tr(L"Tray", text);
}

bool TrayHost::Initialize() {
    HINSTANCE hInstance = m_host->GetInstance();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = TrayHost::WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kTrayWindowClass;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Logger::Write(L"Warning", L"Failed to register tray window class.");
        return false;
    }

    m_hwnd = CreateWindowExW(0, kTrayWindowClass, L"", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, hInstance, this);
    if (!m_hwnd) {
        Logger::Write(L"Warning", L"Failed to create tray message window.");
        return false;
    }

    if (!AddIcon()) {
        Logger::Write(L"Warning", Format(L"Tray icon unavailable (error %lu).",
                                         static_cast<unsigned long>(GetLastError())));
    }

    SetTimer(m_hwnd, 1, kWatchdogIntervalMs, nullptr);
    return true;
}

void TrayHost::Shutdown() {
    m_shuttingDown = true;
    if (m_hwnd) {
        KillTimer(m_hwnd, 1);
    }
    RemoveIcon();
}

bool TrayHost::AddIcon() {
    if (m_iconAdded) {
        return true;
    }

    HINSTANCE hInstance = m_host->GetInstance();
    m_icon = static_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    if (!m_icon) {
        m_icon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    }
    if (!m_icon) {
        m_icon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kTrayCallbackMessage;
    nid.hIcon = m_icon;

    std::wstring title = TranslationService::Get()->Tr(L"Pet", L"Desktop Pet");
    if (title.empty()) {
        title = L"Desktop Pet";
    }
    wcsncpy_s(nid.szTip, title.c_str(), _TRUNCATE);

    m_iconAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    if (m_iconAdded) {
        Logger::Write(L"Tray", L"Tray icon added.");
    }
    return m_iconAdded;
}

void TrayHost::RemoveIcon() {
    if (!m_iconAdded || !m_hwnd) {
        return;
    }
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_iconAdded = false;
}

void TrayHost::UpdateTooltip() {
    if (!m_iconAdded) {
        return;
    }
    std::wstring title = TranslationService::Get()->Tr(L"Pet", L"Desktop Pet");
    if (title.empty()) {
        title = L"Desktop Pet";
    }

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_TIP;
    wcsncpy_s(nid.szTip, title.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayHost::OnPetWindowRecreated(HWND newWindow) {
    (void)newWindow;
    UpdateTooltip();
    Logger::Write(L"Tray", L"Pet window recreated; tray tooltip refreshed.");
}

LRESULT CALLBACK TrayHost::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TrayHost* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<TrayHost*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<TrayHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT TrayHost::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case kTrayCallbackMessage: {
        switch (LOWORD(lParam)) {
        case WM_LBUTTONDBLCLK:
            OnCommand(ID_TRAY_TOGGLE_PET);
            return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            POINT pt;
            GetCursorPos(&pt);
            ShowMenu(hwnd, pt);
            return 0;
        }
        default:
            break;
        }
        return 0;
    }

    case WM_COMMAND:
        OnCommand(LOWORD(wParam));
        return 0;

    case WM_TIMER:
        if (wParam == 1) {
            OnWatchdogTick();
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        RemoveIcon();
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void TrayHost::ShowMenu(HWND hwnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) {
        return;
    }

    bool visible = m_host->IsMainWindowVisible();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_TOGGLE_PET,
                Tr(visible ? L"Hide Pet" : L"Show Pet").c_str());
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SETTINGS, Tr(L"Settings").c_str());
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESET_PET,
                Tr(L"Reset Pet Window").c_str());

    auto groups = m_host->GetModuleManager().GetMenuGroups();
    if (!groups.empty()) {
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

        // group.items[].itemId is already the global unique id assigned by
        // ModuleManager::GetMenuGroups() (the same index ExecuteContextMenuItem
        // takes), so do NOT add a per-group offset on top of it.
        for (const auto& group : groups) {
            HMENU hSub = CreatePopupMenu();
            if (!hSub) {
                continue;
            }
            for (const auto& item : group.items) {
                UINT flags = MF_STRING;
                if (item.flags & kContextMenuItemDisabled) {
                    flags |= MF_GRAYED;
                }
                if (item.flags & kContextMenuItemChecked) {
                    flags |= MF_CHECKED;
                }
                AppendMenuW(hSub, flags,
                            static_cast<UINT_PTR>(ID_TRAY_MODULE_BASE + item.itemId),
                            item.label.c_str());
            }
            AppendMenuW(hMenu, MF_POPUP,
                        reinterpret_cast<UINT_PTR>(hSub),
                        group.displayName.c_str());
        }
    }

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, Tr(L"Exit").c_str());

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

void TrayHost::OnCommand(UINT cmd) {
    if (cmd == ID_TRAY_TOGGLE_PET) {
        bool visible = m_host->IsMainWindowVisible();
        m_host->SetMainWindowVisible(!visible);
        UpdateTooltip();
    } else if (cmd == ID_TRAY_SETTINGS) {
        if (!m_host->IsMainWindowVisible()) {
            m_host->SetMainWindowVisible(true);
        }
        m_host->OpenMainWindowSettings();
    } else if (cmd == ID_TRAY_RESET_PET) {
        m_host->RecreateMainWindow();
        UpdateTooltip();
    } else if (cmd == ID_TRAY_EXIT) {
        PostQuitMessage(0);
    } else if (cmd >= ID_TRAY_MODULE_BASE && cmd < ID_TRAY_EXIT) {
        m_host->GetModuleManager().ExecuteContextMenuItem(
            static_cast<int>(cmd) - ID_TRAY_MODULE_BASE);
    }
}

void TrayHost::OnWatchdogTick() {
    ++m_watchdogTicks;
    if (m_shuttingDown) {
        return;
    }

    // Give an in-place update a chance to relaunch us with the new build.
    m_host->PollForReload();

    if (!m_host->IsMainWindowAlive()) {
        Logger::Write(L"Warning", L"Pet window missing; rebuilding it.");
        m_host->RecreateMainWindow();
        UpdateTooltip();
    }
}
