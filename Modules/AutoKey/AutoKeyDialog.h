#pragma once

#include <windows.h>

class AutoKeyModule;

class AutoKeyDialog {
public:
    static void Show(HWND parent, AutoKeyModule* module);

private:
    AutoKeyDialog(HWND parent, AutoKeyModule* module);
    ~AutoKeyDialog() = default;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void OnCreate(HWND hwnd);
    void RefreshList();
    void OnAdd();
    void OnEdit();
    void OnDelete();
    bool EditAction(HWND parentWnd, int actionId);

    AutoKeyModule* m_module;
    HWND m_hwnd = nullptr;
    HWND m_hList = nullptr;
};
