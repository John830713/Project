//==============================================================================
// RemoteControlHook.cpp - Input hook capture and replay implementation
//==============================================================================

#include "RemoteControlHook.h"
#include <mmsystem.h>
#include <thread>
#include <future>

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

static RemoteControlHook::EventCallback  s_ecb    = nullptr;
static RemoteControlHook::ToggleCallback s_tcb    = nullptr;
static void*                             s_ud     = nullptr;
static HHOOK                             s_kh     = nullptr;
static HHOOK                             s_mh     = nullptr;
static volatile LONG                     s_active = 0;
static volatile LONG                     s_f9     = 0;
static volatile LONG                     s_replaying = 0;
static POINT                             s_saved  = {0, 0};
static ULONGLONG                         s_grace  = 0;
static POINT                             s_lastPt = {0, 0};
static int                               s_sw     = 0;
static int                               s_sh     = 0;
static int                               s_cx     = 0;
static int                               s_cy     = 0;
static BOOL                              s_rdp    = FALSE;
static std::thread                       s_hookThread;
static HANDLE                            s_stopEvent = nullptr;

static const int s_mods[] = {
    VK_SHIFT, VK_CONTROL, VK_MENU,
    VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL,
    VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN
};

static void releaseMods() {
    for (int vk : s_mods) {
        if (GetAsyncKeyState(vk) & 0x8000) {
            INPUT i{};
            i.type = INPUT_KEYBOARD;
            i.ki.wVk = static_cast<WORD>(vk);
            i.ki.dwFlags = KEYEVENTF_KEYUP;
            i.ki.dwExtraInfo = RemoteControlHook::kMagic;
            SendInput(1, &i, sizeof(i));
        }
    }
}

static LRESULT CALLBACK kbProc(int n, WPARAM w, LPARAM l) {
    if (n < 0 || s_replaying) return CallNextHookEx(nullptr, n, w, l);
    auto* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(l);
    if (k->dwExtraInfo == RemoteControlHook::kMagic)
        return CallNextHookEx(nullptr, n, w, l);

    if (k->vkCode == VK_F9) {
        if ((w == WM_KEYDOWN || w == WM_SYSKEYDOWN) &&
            !InterlockedCompareExchange(&s_f9, 1, 0))
        {
            LONG nv = s_active ? 0 : 1;
            InterlockedExchange(&s_active, nv);
            if (nv) {
                GetCursorPos(&s_saved);
                s_rdp = GetSystemMetrics(SM_REMOTESESSION);
                s_cx  = GetSystemMetrics(SM_CXSCREEN) / 2;
                s_cy  = GetSystemMetrics(SM_CYSCREEN) / 2;
                if (!s_rdp) SetCursorPos(s_cx, s_cy);
                GetCursorPos(&s_lastPt);
                s_grace = GetTickCount64() + 100;
                releaseMods();
            } else {
                SetCursorPos(s_saved.x, s_saved.y);
            }
            if (s_tcb) s_tcb(static_cast<int>(nv), s_ud);
        } else if (w == WM_KEYUP || w == WM_SYSKEYUP) {
            InterlockedExchange(&s_f9, 0);
        }
        return 1;
    }

    if (!s_active) return CallNextHookEx(nullptr, n, w, l);

    RemoteControl::Event e{};
    e.type     = (w == WM_KEYDOWN || w == WM_SYSKEYDOWN)
                 ? RemoteControl::EventType::KeyDown
                 : RemoteControl::EventType::KeyUp;
    e.key.vk   = static_cast<uint16_t>(k->vkCode);
    e.key.scan = static_cast<uint16_t>(k->scanCode);
    e.key.ext  = (k->flags & 1) ? 1 : 0;
    if (s_ecb) s_ecb(&e, s_ud);
    return 1;
}

static LRESULT CALLBACK msProc(int n, WPARAM w, LPARAM l) {
    if (n < 0) return CallNextHookEx(nullptr, n, w, l);
    auto* m = reinterpret_cast<MSLLHOOKSTRUCT*>(l);

    // s_replaying is set by RunClient for the entire batch-processing
    // window plus a short settle period.  While it is set, we filter
    // ALL mouse events but still update s_lastPt so that the first
    // capture after s_replaying is cleared sees a correct delta.
    if (s_replaying) {
        if (w == WM_MOUSEMOVE)
            s_lastPt = m->pt;
        return CallNextHookEx(nullptr, n, w, l);
    }

    if (!s_active) return CallNextHookEx(nullptr, n, w, l);
    if (m->dwExtraInfo == RemoteControlHook::kMagic)
        return CallNextHookEx(nullptr, n, w, l);

    if (w == WM_MOUSEMOVE) {
        if (GetTickCount64() < s_grace) {
            if (!s_rdp) SetCursorPos(s_cx, s_cy);
            s_lastPt = m->pt;
            return 1;
        }
        RemoteControl::Event e{};
        e.type = RemoteControl::EventType::MouseMove;
        if (s_rdp) {
            e.move.dx = m->pt.x - s_lastPt.x;
            e.move.dy = m->pt.y - s_lastPt.y;
            s_lastPt = m->pt;
        } else {
            e.move.dx = m->pt.x - s_lastPt.x;
            e.move.dy = m->pt.y - s_lastPt.y;
            SetCursorPos(s_cx, s_cy);
            s_lastPt.x = s_cx;
            s_lastPt.y = s_cy;
        }
        if (!e.move.dx && !e.move.dy) return 1;
        if (s_ecb) s_ecb(&e, s_ud);
        return 1;
    }

    RemoteControl::Event e{};
    switch (w) {
    case WM_LBUTTONDOWN: e.type = RemoteControl::EventType::MouseDown; e.btn.button = 0; break;
    case WM_LBUTTONUP:   e.type = RemoteControl::EventType::MouseUp;   e.btn.button = 0; break;
    case WM_RBUTTONDOWN: e.type = RemoteControl::EventType::MouseDown; e.btn.button = 1; break;
    case WM_RBUTTONUP:   e.type = RemoteControl::EventType::MouseUp;   e.btn.button = 1; break;
    case WM_MBUTTONDOWN: e.type = RemoteControl::EventType::MouseDown; e.btn.button = 2; break;
    case WM_MBUTTONUP:   e.type = RemoteControl::EventType::MouseUp;   e.btn.button = 2; break;
    case WM_MOUSEWHEEL:
        e.type = RemoteControl::EventType::MouseScroll;
        e.scroll.delta = static_cast<int32_t>(
                             static_cast<SHORT>(HIWORD(m->mouseData)));
        break;
    default: return CallNextHookEx(nullptr, n, w, l);
    }
    if (s_ecb) s_ecb(&e, s_ud);
    return 1;
}

bool RemoteControlHook::Start(EventCallback ecb, ToggleCallback tcb, void* ud) {
    s_ecb = ecb; s_tcb = tcb; s_ud = ud;
    s_rdp = GetSystemMetrics(SM_REMOTESESSION);
    s_cx  = GetSystemMetrics(SM_CXSCREEN) / 2;
    s_cy  = GetSystemMetrics(SM_CYSCREEN) / 2;
    s_sw  = GetSystemMetrics(SM_CXSCREEN);
    s_sh  = GetSystemMetrics(SM_CYSCREEN);
    s_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto ready = std::make_shared<std::promise<bool>>();
    auto future = ready->get_future();

    s_hookThread = std::thread([ready]() {
        HINSTANCE hm = GetModuleHandleW(nullptr);
        s_kh = SetWindowsHookExW(WH_KEYBOARD_LL, kbProc, hm, 0);
        s_mh = SetWindowsHookExW(WH_MOUSE_LL, msProc, hm, 0);
        bool ok = (s_kh && s_mh);
        ready->set_value(ok);
        if (!ok) return;

        while (true) {
            DWORD r = MsgWaitForMultipleObjects(1, &s_stopEvent, FALSE,
                                                 INFINITE, QS_ALLINPUT);
            if (r == WAIT_OBJECT_0) break;
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) break;
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        if (s_kh) { UnhookWindowsHookEx(s_kh); s_kh = nullptr; }
        if (s_mh) { UnhookWindowsHookEx(s_mh); s_mh = nullptr; }
        InterlockedExchange(&s_active, 0);
        InterlockedExchange(&s_f9, 0);
    });

    bool ok = future.get();
    if (!ok) { Stop(); return false; }
    return true;
}

void RemoteControlHook::Stop() {
    InterlockedExchange(&s_active, 0);
    InterlockedExchange(&s_f9, 0);
    if (s_stopEvent) SetEvent(s_stopEvent);
    if (s_hookThread.joinable()) s_hookThread.join();
    if (s_stopEvent) { CloseHandle(s_stopEvent); s_stopEvent = nullptr; }
    if (s_kh) { UnhookWindowsHookEx(s_kh); s_kh = nullptr; }
    if (s_mh) { UnhookWindowsHookEx(s_mh); s_mh = nullptr; }
}

bool RemoteControlHook::IsActive() {
    return static_cast<int>(s_active) != 0;
}

void RemoteControlHook::SetActive(bool on) {
    if (on && !s_active) {
        GetCursorPos(&s_saved);
        s_rdp = GetSystemMetrics(SM_REMOTESESSION);
        s_cx  = GetSystemMetrics(SM_CXSCREEN) / 2;
        s_cy  = GetSystemMetrics(SM_CYSCREEN) / 2;
        if (!s_rdp) SetCursorPos(s_cx, s_cy);
        GetCursorPos(&s_lastPt);
        s_grace = GetTickCount64() + 100;
        releaseMods();
    } else if (!on && s_active) {
        SetCursorPos(s_saved.x, s_saved.y);
    }
    InterlockedExchange(&s_active, on ? 1 : 0);
}

void RemoteControlHook::SetReplaying(bool on) {
    InterlockedExchange(&s_replaying, on ? 1 : 0);
}

void RemoteControlHook::ReplayKey(uint16_t vk, uint16_t sc, uint8_t ext, int up) {
    InterlockedExchange(&s_replaying, 1);
    INPUT i{};
    i.type = INPUT_KEYBOARD;
    i.ki.wVk = vk; i.ki.wScan = sc;
    i.ki.dwExtraInfo = kMagic;
    i.ki.dwFlags = (ext ? KEYEVENTF_EXTENDEDKEY : 0) | (up ? KEYEVENTF_KEYUP : 0);
    SendInput(1, &i, sizeof(i));
    InterlockedExchange(&s_replaying, 0);
}

void RemoteControlHook::ReplayMouseMove(int32_t dx, int32_t dy) {
    InterlockedExchange(&s_replaying, 1);
    INPUT i{};
    i.type = INPUT_MOUSE;
    i.mi.dx = dx;
    i.mi.dy = dy;
    i.mi.dwFlags = MOUSEEVENTF_MOVE;
    i.mi.dwExtraInfo = kMagic;
    SendInput(1, &i, sizeof(i));
    InterlockedExchange(&s_replaying, 0);
}

void RemoteControlHook::ReplayMouseBtn(uint8_t b, int down) {
    static const DWORD F[][2] = {
        { MOUSEEVENTF_LEFTUP,   MOUSEEVENTF_LEFTDOWN   },
        { MOUSEEVENTF_RIGHTUP,  MOUSEEVENTF_RIGHTDOWN  },
        { MOUSEEVENTF_MIDDLEUP, MOUSEEVENTF_MIDDLEDOWN }
    };
    if (b > 2) return;
    InterlockedExchange(&s_replaying, 1);
    INPUT i{};
    i.type = INPUT_MOUSE;
    i.mi.dwFlags = F[b][down ? 1 : 0];
    i.mi.dwExtraInfo = kMagic;
    SendInput(1, &i, sizeof(i));
    InterlockedExchange(&s_replaying, 0);
}

void RemoteControlHook::ReplayMouseScroll(int32_t d) {
    InterlockedExchange(&s_replaying, 1);
    INPUT i{};
    i.type = INPUT_MOUSE;
    i.mi.mouseData = static_cast<DWORD>(d);
    i.mi.dwFlags   = MOUSEEVENTF_WHEEL;
    i.mi.dwExtraInfo = kMagic;
    SendInput(1, &i, sizeof(i));
    InterlockedExchange(&s_replaying, 0);
}

void RemoteControlHook::ReplayReleaseAll() {
    for (int vk : s_mods)
        ReplayKey(static_cast<uint16_t>(vk), 0, 0, 1);
}

int RemoteControlHook::ClipRead(wchar_t* buf, int cap) {
    for (int i = 0; i < 5; i++) {
        if (OpenClipboard(nullptr)) goto ok_r;
        Sleep(30);
    }
    return 0;
ok_r:
    int r = 0;
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            auto* p = static_cast<wchar_t*>(GlobalLock(h));
            if (p) {
                r = static_cast<int>(wcslen(p));
                if (buf && cap > 0) {
                    int c = (r < cap - 1) ? r : (cap - 1);
                    wmemcpy(buf, p, c);
                    buf[c] = 0;
                }
                GlobalUnlock(h);
            }
        }
    }
    CloseClipboard();
    return r;
}

int RemoteControlHook::ClipWrite(const wchar_t* t, int len) {
    if (!t) return 0;
    if (len < 0) len = static_cast<int>(wcslen(t));
    for (int i = 0; i < 5; i++) {
        if (OpenClipboard(nullptr)) goto ok_w;
        Sleep(30);
    }
    return 0;
ok_w:
    EmptyClipboard();
    if (len > 0) {
        SIZE_T  sz = static_cast<SIZE_T>(len + 1) * sizeof(wchar_t);
        HGLOBAL h  = GlobalAlloc(GMEM_MOVEABLE, sz);
        if (!h) { CloseClipboard(); return 0; }
        auto* p = static_cast<wchar_t*>(GlobalLock(h));
        if (!p) { GlobalFree(h); CloseClipboard(); return 0; }
        wmemcpy(p, t, len); p[len] = 0;
        GlobalUnlock(h);
        if (!SetClipboardData(CF_UNICODETEXT, h)) {
            GlobalFree(h); CloseClipboard(); return 0;
        }
    }
    CloseClipboard();
    return 1;
}
