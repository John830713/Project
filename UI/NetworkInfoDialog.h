#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include "../Services/RemoteInitService.h"

class NetworkInfoDialog {
public:
    NetworkInfoDialog(HWND parent);
    INT_PTR Show();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Init(HWND hwnd);
    int Layout();
    void OnCopy(int idx);
    static void DrawCopyGlyph(HDC dc, int x, int y, int w, int h);
    void InitNetworkTab(HWND hwnd);
    void InitRemoteTab(HWND hwnd);
    void RefreshRemoteTab(HWND hwnd);
    void OnRemoteCommand(HWND hwnd, WPARAM wParam);

    HWND m_hwnd;
    HWND m_parent;
    HWND m_hTab;
    HWND m_hBtnClose;
    int m_currentTab;
    int m_scrollPos;
    int m_contentH;

    struct IpRow {
        std::wstring ip;
        HWND hText;
        HWND hBtn;
        int id;
    };
    struct AdapterGroup {
        std::wstring name;
        HWND hHeader;
        std::vector<int> rows;
    };
    std::vector<AdapterGroup> m_groups;
    std::vector<IpRow> m_rows;
    int m_nextBtnId;
    HICON m_hCopyIcon;

    RemoteInitStatus m_remoteStatus;
    std::vector<RemoteInitItem> m_remoteItems;
    std::wstring m_remoteBackupPath;
    std::vector<HWND> m_remoteCtrls;
};
