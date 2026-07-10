#pragma once

#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <string>
#include <map>
#include <vector>
#include <functional>

class IHostContext;
class ModuleManager;
class InputManager;
struct ResolvedDropAction;
struct ContextMenuItem;

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool Create(
        HINSTANCE hInstance,
        int nCmdShow,
        IHostContext* host,
        ModuleManager* moduleManager,
        InputManager* inputManager,
        HWND& outHwnd,
        const std::wstring& windowTitle,
        const std::wstring& hintText,
        const std::wstring& appVersion = L"1.0.0",
        const std::wstring& appPurpose = L"Modular desktop pet.");

    HWND GetHWND() const { return m_hwnd; }

    static constexpr UINT WM_APP_SLIDER_CHANGE = WM_APP + 2;
    static constexpr UINT WM_APP_RELOAD_PETCONFIG = WM_APP + 3;
    static constexpr UINT WM_APP_CONTEXT_ACTION = WM_APP + 4;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool LoadPetImage();
    void ApplyWindowImage();
    void OnDropFiles(HWND hwnd, HDROP hDrop);
    void OnContextMenu(HWND hwnd, POINT pt);
    void ShowAboutDialog(HWND hwnd);
    void OpenSettings(HWND hwnd);
    int ShowDropActionMenu(HWND hwnd, const std::vector<ResolvedDropAction>& actions);
    void SavePosition();
    void ToggleTopmost();
    void ReloadPetConfig();
    void SyncMoveState();
    void StartMoveTimer();
    void StopMoveTimer();
    void OnMoveTimer();
    void SetOpacity(int alpha);
    void SetScale(int percent);

    void ShowSliderPopup(HWND parent, const wchar_t* title, int current,
                         int minVal, int maxVal, WPARAM changeId);
    void CloseSliderPopup();

    //--- Custom popup context menu ---
    struct PopupItem {
        enum Type { Action, Separator, Submenu };
        Type type;
        int id;
        std::wstring text;
        std::vector<PopupItem> subItems;
    };

    struct SubPopupData {
        MainWindow* mainWindow;
        std::vector<PopupItem>* container;
        HWND* clearPtr;
        SubPopupData(MainWindow* mw, std::vector<PopupItem>* c, HWND* cp = nullptr)
            : mainWindow(mw), container(c), clearPtr(cp) {}
    };

    void ShowContextPopup(HWND hwnd, POINT pt);
    void CloseContextPopup();
    void CloseSubPopup();
    void CloseMovePopup();
    void CloseOpacityPopup();
    void CloseScalePopup();
    HWND CreateSubPopup(HWND parent, POINT pt, const std::vector<PopupItem>& items, int baseId, int& outHeight, HWND* storePtr = nullptr);

    static LRESULT CALLBACK ContextPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK PetPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK SubPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK SliderSubPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void OpenSliderSubPopup(HWND hwnd, MainWindow* self, int itemIdx, int cw);

    HWND m_hwnd;
    HWND m_hSliderPopup;
    HWND m_hMenuTooltip;
    HWND m_hContextPopup;
    HWND m_hSubPopup;
    HWND m_hMovePopup;
    HWND m_hOpacityPopup;
    HWND m_hScalePopup;
#if DEBUG_CONSOLE
    HWND m_moveTestPetPopup;
#endif
    int m_contextOpacity;
    int m_contextScale;
    std::map<UINT, std::wstring> m_menuTooltips;
    IHostContext* m_host;
    ModuleManager* m_moduleManager;
    InputManager* m_inputManager;
    std::wstring m_windowTitle;
    std::wstring m_hintText;
    std::wstring m_appVersion;
    std::wstring m_appPurpose;

    ULONG_PTR m_gdiplusToken;
    Gdiplus::Bitmap* m_petImage;
    Gdiplus::Bitmap* m_originalImage;
    int m_imageWidth;
    int m_imageHeight;
    bool m_moveLocked;
    int m_scalePercent;
    int m_currentOpacity;

    bool m_moveEnabled;
    int m_moveStep;
    int m_moveSpeed;
    bool m_moveShuttle;
    int m_moveDx;
    int m_moveDy;
    int m_movePauseMs;

    bool m_edgeSnapped;
    int m_snapEdge;
    Gdiplus::Bitmap* m_snapIndicator;

    enum { SNAP_NONE = 0, SNAP_TOP, SNAP_BOTTOM, SNAP_LEFT, SNAP_RIGHT };

    void CheckEdgeSnap();
    void CreateSnapIndicator();
    void ApplySnapIndicator();

    static constexpr int SNAP_INDICATOR_SIZE = 24;

    enum {
        ID_EXIT = 100, ID_SETTINGS = 101,
        ID_PET_TOPMOST = 102,
        ID_PET_MOVE_STILL = 103, ID_PET_MOVE_MOVING = 104,
        ID_PET_MOVE_STEP = 105, ID_PET_MOVE_SPEED = 106,
        ID_PET_MOVE_SHUTTLE = 107,
        ID_PET_OPACITY = 110, ID_PET_SCALE = 111,
        ID_MENU_BASE = 2000, ID_ABOUT = 9999,
        ID_POPUP_BASE = 6000
    };
};
