#pragma once
#include <windows.h>

class NetworkInfoDialog {
public:
    NetworkInfoDialog(HWND parent);
    INT_PTR Show();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Init(HWND hwnd);
    void CopySelectedIp();

    HWND m_hwnd;
    HWND m_parent;
    HWND m_hListView;
    HWND m_hBtnCopy;
    HWND m_hBtnClose;
};
