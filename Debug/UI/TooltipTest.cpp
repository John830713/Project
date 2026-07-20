#define _WIN32_IE 0x0501
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <cstdio>

static int g_tests = 0;
static int g_fails = 0;

#define CHECK(cond, msg) do { \
    g_tests++; \
    if (!(cond)) { \
        printf("FAIL [%d] %s\n", __LINE__, msg); \
        g_fails++; \
    } else { \
        printf("OK   [%d] %s\n", __LINE__, msg); \
    } \
} while(0)

#define CHECK_MSG(cond, fmt, ...) do { \
    g_tests++; \
    if (!(cond)) { \
        printf("FAIL [%d] " fmt "\n", __LINE__, ##__VA_ARGS__); \
        g_fails++; \
    } else { \
        printf("OK   [%d] " fmt "\n", __LINE__, ##__VA_ARGS__); \
    } \
} while(0)

static int cbSizeCorrect() {
    TOOLINFOW ti = {};
    ti.cbSize = sizeof(TOOLINFOW) - sizeof(void*);
    return (int)ti.cbSize;
}

static HWND CreateEdit(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowW(L"EDIT", text,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        x, y, w, h, parent, nullptr, nullptr, nullptr);
}

static HWND CreateTooltip(HWND parent) {
    HWND tt = CreateWindowExW(0, L"Tooltips_class32", nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        parent, nullptr, nullptr, nullptr);
    if (tt) SendMessageW(tt, TTM_SETMAXTIPWIDTH, 0, 2000);
    return tt;
}

static BOOL AddTrackTool(HWND tooltip, HWND hwnd, UINT_PTR uId, LPCWSTR text) {
    TOOLINFOW ti = {};
    ti.cbSize = sizeof(TOOLINFOW) - sizeof(void*);
    ti.uFlags = TTF_TRACK;
    ti.hwnd = hwnd;
    ti.uId = uId;
    ti.lpszText = const_cast<wchar_t*>(text);
    return (BOOL)SendMessageW(tooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
}

static BOOL AddCallbackTool(HWND tooltip, HWND hwnd, UINT_PTR uId) {
    TOOLINFOW ti = {};
    ti.cbSize = sizeof(TOOLINFOW) - sizeof(void*);
    ti.uFlags = TTF_TRACK;
    ti.hwnd = hwnd;
    ti.uId = uId;
    ti.lpszText = (LPWSTR)LPSTR_TEXTCALLBACK;
    return (BOOL)SendMessageW(tooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
}

static BOOL ActivateTrack(HWND tooltip, HWND hwnd, UINT_PTR uId, BOOL active) {
    TOOLINFOW ti = {};
    ti.cbSize = sizeof(TOOLINFOW) - sizeof(void*);
    ti.hwnd = hwnd;
    ti.uId = uId;
    return (BOOL)SendMessageW(tooltip, TTM_TRACKACTIVATE, active, (LPARAM)&ti);
}

static void SetTrackPos(HWND tooltip, int x, int y) {
    SendMessageW(tooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(x, y));
}

static void PumpMessages() {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

void TestCbSize() {
    printf("\n=== cbSize ===\n");
    CHECK(cbSizeCorrect() <= 64, "cbSize <= 64");
    CHECK(cbSizeCorrect() > 0, "cbSize > 0");
}

void TestTrackWithStaticText() {
    printf("\n=== TTF_TRACK + static text ===\n");
    HWND parent = CreateWindowW(L"STATIC", L"", WS_POPUP, 0, 0, 0, 0,
                                nullptr, nullptr, nullptr, nullptr);
    HWND edit = CreateEdit(parent, L"C:\\Test\\Long\\Path", 10, 10, 200, 24);
    HWND tt = CreateTooltip(parent);

    CHECK_MSG(tt != nullptr, "tooltip created");

    BOOL ok = AddTrackTool(tt, edit, (UINT_PTR)edit, L"C:\\Test\\Long\\Path");
    CHECK_MSG(ok != 0, "TTM_ADDTOOL");
    if (ok) {
        SetTrackPos(tt, 100, 100);
        ok = ActivateTrack(tt, edit, (UINT_PTR)edit, TRUE);
        CHECK_MSG(ok != 0, "TTM_TRACKACTIVATE");
        PumpMessages();
        CHECK_MSG(IsWindowVisible(tt) != 0, "tooltip visible with static text");
        ActivateTrack(tt, edit, (UINT_PTR)edit, FALSE);
    }

    DestroyWindow(tt);
    DestroyWindow(edit);
    DestroyWindow(parent);
}

void TestTrackWithCallback() {
    printf("\n=== TTF_TRACK + LPSTR_TEXTCALLBACK ===\n");
    HWND parent = CreateWindowW(L"STATIC", L"", WS_POPUP, 0, 0, 0, 0,
                                nullptr, nullptr, nullptr, nullptr);
    HWND edit = CreateEdit(parent, L"C:\\Test\\Long\\Path", 10, 10, 200, 24);
    HWND tt = CreateTooltip(parent);

    CHECK_MSG(tt != nullptr, "tooltip created");

    BOOL ok = AddCallbackTool(tt, edit, (UINT_PTR)edit);
    CHECK_MSG(ok != 0, "TTM_ADDTOOL with LPSTR_TEXTCALLBACK");
    if (ok) {
        SetTrackPos(tt, 100, 100);
        ok = ActivateTrack(tt, edit, (UINT_PTR)edit, TRUE);
        CHECK_MSG(ok != 0, "TTM_TRACKACTIVATE");
        PumpMessages();
        CHECK_MSG(IsWindowVisible(tt) == 0,
                  "tooltip NOT visible with LPSTR_TEXTCALLBACK");
        ActivateTrack(tt, edit, (UINT_PTR)edit, FALSE);
    }

    DestroyWindow(tt);
    DestroyWindow(edit);
    DestroyWindow(parent);
}

void TestTrackActivate() {
    printf("\n=== TTF_TRACK activate after add ===\n");
    HWND parent = CreateWindowW(L"STATIC", L"", WS_POPUP, 0, 0, 0, 0,
                                nullptr, nullptr, nullptr, nullptr);
    HWND edit = CreateEdit(parent, L"Initial", 10, 10, 200, 24);
    HWND tt = CreateTooltip(parent);

    CHECK_MSG(tt != nullptr, "tooltip created");

    BOOL ok = AddTrackTool(tt, edit, (UINT_PTR)edit, L"Initial");
    CHECK_MSG(ok != 0, "TTM_ADDTOOL with 'Initial'");
    if (ok) {
        SetTrackPos(tt, 100, 100);
        ActivateTrack(tt, edit, (UINT_PTR)edit, TRUE);
        PumpMessages();
        CHECK_MSG(IsWindowVisible(tt) != 0, "tooltip visible after track activate");
        ActivateTrack(tt, edit, (UINT_PTR)edit, FALSE);
    }

    DestroyWindow(tt);
    DestroyWindow(edit);
    DestroyWindow(parent);
}

//--- Simulate WM_MOUSEMOVE → subclass → tooltip visible ---
static LRESULT CALLBACK TestEditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    HWND hTooltip = reinterpret_cast<HWND>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (!hTooltip) return DefWindowProcW(hWnd, msg, wParam, lParam);

    if (msg == WM_MOUSEMOVE) {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hWnd, &pt);
        TOOLINFOW ti = {};
        ti.cbSize = sizeof(TOOLINFOW) - sizeof(void*);
        ti.hwnd = hWnd;
        ti.uId = (UINT_PTR)hWnd;
        SendMessageW(hTooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(pt.x, pt.y + 20));
        SendMessageW(hTooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
        return 0;
    }
    if (msg == WM_MOUSELEAVE) {
        TOOLINFOW ti = {};
        ti.cbSize = sizeof(TOOLINFOW) - sizeof(void*);
        ti.hwnd = hWnd;
        ti.uId = (UINT_PTR)hWnd;
        SendMessageW(hTooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void TestMouseMoveTriggersTooltip() {
    printf("\n=== WM_MOUSEMOVE → subclass → tooltip visible ===\n");
    HWND parent = CreateWindowW(L"STATIC", L"", WS_POPUP, 0, 0, 800, 600,
                                nullptr, nullptr, nullptr, nullptr);
    HWND edit = CreateEdit(parent, L"Long path text", 10, 10, 200, 24);
    HWND tt = CreateTooltip(parent);

    CHECK_MSG(tt != nullptr, "tooltip created");
    BOOL ok = AddTrackTool(tt, edit, (UINT_PTR)edit, L"Full tooltip text");
    CHECK_MSG(ok != 0, "TTM_ADDTOOL");
    if (ok) {
        SetWindowPos(parent, nullptr, 100, 100, 800, 600, SWP_NOZORDER);
        SetWindowPos(edit, nullptr, 10, 10, 200, 24, SWP_NOZORDER);

        SetWindowLongPtrW(edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tt));
        WNDPROC oldProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(edit, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(TestEditSubclassProc)));

        SendMessageW(edit, WM_MOUSEMOVE, 0, MAKELPARAM(20, 15));
        PumpMessages();
        Sleep(50);
        PumpMessages();

        CHECK_MSG(IsWindowVisible(tt) != 0, "tooltip visible after WM_MOUSEMOVE");

        SendMessageW(edit, WM_MOUSELEAVE, 0, 0);
        PumpMessages();
        CHECK_MSG(IsWindowVisible(tt) == 0, "tooltip hidden after WM_MOUSELEAVE");

        SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(oldProc));
    }

    DestroyWindow(tt);
    DestroyWindow(edit);
    DestroyWindow(parent);
}

int main() {
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_TAB_CLASSES };
    InitCommonControlsEx(&icex);

    TestCbSize();
    TestTrackWithStaticText();
    TestTrackWithCallback();
    TestTrackActivate();
    TestMouseMoveTriggersTooltip();

    printf("\n=== Results: %d tests, %d failures ===\n", g_tests, g_fails);
    return g_fails > 0 ? 1 : 0;
}
