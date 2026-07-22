#pragma once

#include <windows.h>
#include <string>
#include <vector>

class ModuleManager;

class MenuOrderDialog {
public:
    static void Show(HWND parent, ModuleManager* moduleManager);

private:
    MenuOrderDialog(HWND parent, ModuleManager* moduleManager);
    ~MenuOrderDialog() = default;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void OnCreate(HWND hwnd);
    void RefreshList();
    void OnUp();
    void OnDown();
    void OnSave();
    void LoadOrder();
    void SaveOrder();

    ModuleManager* m_moduleManager;
    HWND m_hwnd = nullptr;
    HWND m_hList = nullptr;
    HWND m_hUpBtn = nullptr;
    HWND m_hDownBtn = nullptr;
    HWND m_hSaveBtn = nullptr;
    HWND m_hCancelBtn = nullptr;
    std::vector<std::wstring> m_moduleOrder;
    int m_selectedIdx = -1;

    static constexpr int LIST_ID = 100;
    static constexpr int UP_BTN_ID = 101;
    static constexpr int DOWN_BTN_ID = 102;
    static constexpr int SAVE_BTN_ID = 103;
    static constexpr int CANCEL_BTN_ID = 104;
};
