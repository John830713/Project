//==============================================================================
// UpdateManager.cpp - Single-instance guard + in-place executable update
//
// Detection uses a process scan (Toolhelp32) keyed on the executable name:
// it works for builds that predate this feature (they hold no mutex) and it
// yields the peer's image path, which is the registered path to swap. The
// PEB image path is fixed at process creation, so even after an earlier
// rename-swap the running instance still reports the original path — always
// the right target.
//==============================================================================

#include "UpdateManager.h"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cwchar>
#include <functional>
#include <string>
#include <vector>

namespace UpdateManager {

namespace {

constexpr UINT kNoticeTimeoutMs = 2000;
const wchar_t kNoticeClass[] = L"UpdateManagerNoticeWnd";

// Kept alive for the lifetime of the primary instance so later launches see
// ERROR_ALREADY_EXISTS. Released by the OS at process exit.
HANDLE g_hMutex = nullptr;

//------------------------------------------------------------------------------
// Path helpers
//------------------------------------------------------------------------------

std::wstring ModulePath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

std::wstring DirPart(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"" : path.substr(0, pos);
}

std::wstring FileNamePart(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? path : path.substr(pos + 1);
}

std::wstring BaseNameNoExt(const std::wstring& fileName) {
    size_t pos = fileName.find_last_of(L'.');
    return (pos == std::wstring::npos) ? fileName : fileName.substr(0, pos);
}

std::wstring LastErrorText(DWORD err) {
    wchar_t buf[256] = {};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, buf, 255, nullptr);
    std::wstring text = buf;
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' ||
                             text.back() == L' ')) {
        text.pop_back();
    }
    if (text.empty()) text = L"error";
    return text + L" (" + std::to_wstring(err) + L")";
}

std::wstring FillTemplate(const std::wstring& tmpl, const std::wstring& value) {
    wchar_t buf[512] = {};
    std::swprintf(buf, 512, tmpl.c_str(), value.c_str());
    return buf;
}

//------------------------------------------------------------------------------
// FindPeerProcess - locate another running copy of this executable.
// Returns its PID and image path (the registered path to update).
//------------------------------------------------------------------------------

bool FindPeerProcess(const std::wstring& selfPath, DWORD* pidOut,
                     std::wstring* pathOut) {
    const std::wstring selfName = FileNamePart(selfPath);
    const DWORD selfPid = GetCurrentProcessId();

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    bool found = false;
    if (Process32FirstW(snap, &entry)) {
        do {
            if (entry.th32ProcessID == selfPid) continue;
            if (_wcsicmp(entry.szExeFile, selfName.c_str()) != 0) continue;

            HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                      entry.th32ProcessID);
            if (!proc) continue;

            wchar_t buf[MAX_PATH] = {};
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(proc, 0, buf, &size)) {
                *pidOut = entry.th32ProcessID;
                *pathOut = buf;
                found = true;
            }
            CloseHandle(proc);
            if (found) break;
        } while (Process32NextW(snap, &entry));
    }

    CloseHandle(snap);
    return found;
}

//------------------------------------------------------------------------------
// 2-second notice popup (MessageBox has no built-in timeout).
// The popup owns its own message loop; WM_TIMER closes it.
//------------------------------------------------------------------------------

LRESULT CALLBACK NoticeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        case WM_TIMER:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW));

            auto* text = reinterpret_cast<std::wstring*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (text) {
                SetBkMode(hdc, TRANSPARENT);
                HGDIOBJ oldFont =
                    SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
                RECT textRc = rc;
                InflateRect(&textRc, -16, -12);
                DrawTextW(hdc, text->c_str(), -1, &textRc,
                          DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
                SelectObject(hdc, oldFont);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void ShowNotice(const std::wstring& text, const std::wstring& caption) {
    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = NoticeWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);  // resolves to the ANSI/W version in use
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        wc.lpszClassName = kNoticeClass;
        RegisterClassExW(&wc);  // already-registered on later calls is fine
        s_registered = true;
    }

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int width = 460;
    const int height = 170;
    const int x = workArea.left + ((workArea.right - workArea.left) - width) / 2;
    const int y = workArea.top + ((workArea.bottom - workArea.top) - height) / 2;

    // const_cast is safe: ShowNotice blocks in the message loop below, so
    // `text` outlives the window.
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kNoticeClass, caption.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, x, y, width, height,
        nullptr, nullptr, GetModuleHandleW(nullptr),
        const_cast<std::wstring*>(&text));

    if (!hwnd) return;
    SetTimer(hwnd, 1, kNoticeTimeoutMs, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

}  // namespace

//==============================================================================
// Public helpers (unit-tested)
//==============================================================================

std::uint64_t ReadFileVersion(const std::wstring& path) {
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) return 0;

    std::vector<BYTE> buffer(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, buffer.data())) return 0;

    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&info),
                        &len) ||
        !info || info->dwSignature != 0xFEEF04BD) {
        return 0;
    }

    return (static_cast<std::uint64_t>(info->dwFileVersionMS) << 32) |
           static_cast<std::uint64_t>(info->dwFileVersionLS);
}

std::wstring VersionToString(std::uint64_t version) {
    wchar_t buf[64] = {};
    std::swprintf(buf, 64, L"%u.%u.%u.%u",
                  static_cast<unsigned>((version >> 48) & 0xFFFF),
                  static_cast<unsigned>((version >> 32) & 0xFFFF),
                  static_cast<unsigned>((version >> 16) & 0xFFFF),
                  static_cast<unsigned>(version & 0xFFFF));
    return buf;
}

bool IsNewer(std::uint64_t candidate, std::uint64_t current) {
    return candidate > current;
}

std::wstring MakeBackupName(const std::wstring& target) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t stamp[32] = {};
    std::swprintf(stamp, 32, L"%04u%02u%02u%02u%02u%02u", st.wYear, st.wMonth,
                  st.wDay, st.wHour, st.wMinute, st.wSecond);
    return target + L"." + stamp + L".old";
}

bool SwapExecutable(const std::wstring& source, const std::wstring& target,
                    const std::wstring& backup, std::wstring* errorOut) {
    auto fail = [errorOut](const std::wstring& message) {
        if (errorOut) *errorOut = message;
        return false;
    };

    if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // Nothing installed at the registered path — just place the new build.
        if (!CopyFileW(source.c_str(), target.c_str(), FALSE)) {
            return fail(L"Cannot write the new build: " +
                        LastErrorText(GetLastError()));
        }
        return true;
    }

    // Renaming a loaded image is allowed; writing/deleting it is not.
    if (!MoveFileW(target.c_str(), backup.c_str())) {
        return fail(L"Cannot rename the running executable: " +
                    LastErrorText(GetLastError()));
    }

    if (!CopyFileW(source.c_str(), target.c_str(), FALSE)) {
        const DWORD err = GetLastError();
        const bool rolledBack = MoveFileW(backup.c_str(), target.c_str()) != 0;
        return fail(L"Cannot copy the new build: " + LastErrorText(err) +
                    (rolledBack ? L"; the original was restored"
                                : L"; rollback failed, restore " + backup));
    }

    return true;
}

int RemoveStaleBackups(const std::wstring& modulePath) {
    const std::wstring dir = DirPart(modulePath);
    const std::wstring pattern =
        dir + L"\\" + FileNamePart(modulePath) + L".*.old";

    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return 0;

    int removed = 0;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring full = dir + L"\\" + data.cFileName;
        if (DeleteFileW(full.c_str())) ++removed;
    } while (FindNextFileW(find, &data));

    FindClose(find);
    return removed;
}

//==============================================================================
// BeginSession
//==============================================================================

bool BeginSession(const NoticeTranslator& tr) {
    const std::wstring self = ModulePath();
    const std::wstring base = BaseNameNoExt(FileNamePart(self));
    const std::wstring mutexName = L"Local\\" + base + L"_Instance";

    SetLastError(ERROR_SUCCESS);
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    const bool mutexAlreadyExists =
        (hMutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS);

    DWORD peerPid = 0;
    std::wstring peerPath;
    const bool peerFound = FindPeerProcess(self, &peerPid, &peerPath);

    if (!mutexAlreadyExists && !peerFound) {
        // Primary instance: advertise via the mutex and clean up any stale
        // in-place-update backups. A backup still locked by a running old
        // image stays and is retried on the next launch.
        g_hMutex = hMutex;
        RemoveStaleBackups(self);
        return true;
    }

    // Update / duplicate-launch mode. Never starts a second copy and never
    // disturbs the running instance.
    if (hMutex) CloseHandle(hMutex);

    const std::uint64_t ourVersion = ReadFileVersion(self);

    if (!peerFound) {
        // Mutex says running but the peer's path could not be queried.
        ShowNotice(tr(L"The program is already running."), tr(L"Notice"));
        return false;
    }

    const std::uint64_t diskVersion = ReadFileVersion(peerPath);

    if (!IsNewer(ourVersion, diskVersion)) {
        std::wstring text = tr(L"The program is already running.");
        if (ourVersion != 0 || diskVersion != 0) {
            text += L"\n" + FillTemplate(tr(L"Clicked version: %s"),
                                         VersionToString(ourVersion));
            text += L"\n" + FillTemplate(tr(L"On-disk version: %s"),
                                         VersionToString(diskVersion));
        }
        ShowNotice(text, tr(L"Notice"));
        return false;
    }

    // Newer build: rename-and-swap in place. The running instance keeps
    // using the renamed image — zero downtime, RemoteControl untouched.
    const std::wstring backup = MakeBackupName(peerPath);
    std::wstring error;
    if (!SwapExecutable(self, peerPath, backup, &error)) {
        ShowNotice(FillTemplate(tr(L"Update failed: %s"), error),
                   tr(L"Update"));
        return false;
    }

    ShowNotice(tr(L"Update complete. The new version takes effect on the next launch."),
               tr(L"Update"));
    return false;
}

}  // namespace UpdateManager
