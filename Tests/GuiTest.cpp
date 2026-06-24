#include <windows.h>
#include <cstdio>
#include <cwchar>

static HWND WaitForWindow(const wchar_t* cls, int ms = 5000) {
    auto start = GetTickCount64();
    while (GetTickCount64() - start < (DWORD)ms) {
        HWND h = FindWindowW(cls, nullptr);
        if (h) return h;
        Sleep(200);
    }
    return nullptr;
}

static void DumpChildren(HWND hwnd, int indent = 0) {
    wchar_t buf[256], cls[256];
    HWND child = nullptr;
    while ((child = FindWindowExW(hwnd, child, nullptr, nullptr)) != nullptr) {
        GetWindowTextW(child, buf, 256);
        GetClassNameW(child, cls, 256);
        bool vis = IsWindowVisible(child) != FALSE;
        wprintf(L"%*s%s hwnd=%p cls='%s' txt='%s'\n",
            indent, L"", vis ? L"V" : L"H",
            (void*)child, cls, buf);
        DumpChildren(child, indent + 2);
    }
}

int wmain() {
    wprintf(L"=== GUI Test ===\n");

    HWND pet = WaitForWindow(L"PetMainWindowClass");
    if (!pet) { wprintf(L"FAIL: Pet not found\n"); return 1; }
    wprintf(L"Pet: hwnd=%p\n", (void*)pet);

    // Open Settings via custom message
    wprintf(L"\nOpening Settings...\n");
    PostMessageW(pet, WM_APP + 4, 0, 0); // WM_APP_OPEN_SETTINGS
    Sleep(2000);

    HWND settings = FindWindowW(L"SettingsDialogClass", nullptr);
    if (!settings) { wprintf(L"FAIL: Settings not found\n"); return 1; }
    wprintf(L"Settings: hwnd=%p\n", (void*)settings);

    // Step 1: Verify SendMessage works cross-process (test with WM_CLOSE on a non-existent child)
    wprintf(L"\n--- Step 1: Testing cross-process SendMessage ---\n");
    // Send WM_APP_SWITCH_TAB (0x8002) via SendMessage
    wprintf(L"Sending WM_APP_SWITCH_TAB via SendMessage...\n");
    LRESULT ret = SendMessageW(settings, 0x8002, 1, 0);
    wprintf(L"  Returned: %lld\n", (long long)ret);
    Sleep(500);

    // Check if OnTabChanged was called by looking at children count
    int childCount = 0;
    HWND child = nullptr;
    while ((child = FindWindowExW(settings, child, nullptr, nullptr)) != nullptr)
        childCount++;
    wprintf(L"  Children count after tab switch: %d\n", childCount);

    // Try finding Manage button by ID 1000
    HWND btn = GetDlgItem(settings, 1000);
    wprintf(L"  GetDlgItem(1000) = %p\n", (void*)btn);
    if (btn) {
        wprintf(L"  Manage btn visible: %d\n", IsWindowVisible(btn) ? 1 : 0);
    } else {
        // Debug: enumerate all child windows
        wprintf(L"  Enumerating children:\n");
        DumpChildren(settings);
    }

    // Step 2: Open manage dialog
    if (btn) {
        wprintf(L"\n--- Step 2: Opening Manage Actions ---\n");
        PostMessageW(settings, WM_COMMAND, 1000, (LPARAM)btn);
        Sleep(1500);

        HWND akDlg = FindWindowW(L"AutoKeyDialogClass", nullptr);
        if (akDlg) {
            wprintf(L"AutoKeyDialog: hwnd=%p\n", (void*)akDlg);

            // Step 3: Add action
            HWND addBtn = GetDlgItem(akDlg, 101);
            if (addBtn) {
                wprintf(L"\n--- Step 3: Add action ---\n");
                PostMessageW(akDlg, WM_COMMAND, 101, (LPARAM)addBtn);
                Sleep(1000);

                HWND editDlg = FindWindowW(L"AutoKeyEditDialogClass", nullptr);
                if (editDlg) {
                    wprintf(L"EditDialog: hwnd=%p\n", (void*)editDlg);
                    DumpChildren(editDlg);

                    // Save
                    wprintf(L"\nSaving...\n");
                    PostMessageW(editDlg, WM_COMMAND, 1, 0);
                    Sleep(500);
                } else {
                    wprintf(L"Edit dialog not found\n");
                }
            }

            // Close AutoKey dialog
            wprintf(L"\nClosing AutoKey dialog...\n");
            PostMessageW(akDlg, WM_CLOSE, 0, 0);
            Sleep(500);

            // Verification
            wprintf(L"\n=== VERIFICATION: Manage btn exists? ===\n");
            HWND btnAfter = GetDlgItem(settings, 1000);
            if (btnAfter) {
                wprintf(L"PASS: Manage btn exists: hwnd=%p visible=%d\n",
                    (void*)btnAfter, IsWindowVisible(btnAfter) ? 1 : 0);
            } else {
                wprintf(L"FAIL: Manage btn GONE!\n");
            }
        } else {
            wprintf(L"AutoKey dialog not found\n");
        }
    }

    // Close settings
    wprintf(L"\nClosing settings...\n");
    PostMessageW(settings, WM_CLOSE, 0, 0);
    wprintf(L"\n=== DONE ===\n");
    return 0;
}
