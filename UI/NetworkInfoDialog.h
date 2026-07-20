#pragma once
#include <windows.h>
#include <string>
#include <vector>

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

    HWND m_hwnd;
    HWND m_parent;
    HWND m_hBtnClose;
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
};
