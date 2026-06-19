#include "HostApp.h"
#include "Core/Logger.h"
#include "Core/ConfigManager.h"

bool HostApp::Initialize(HINSTANCE hInstance, int nCmdShow) {
    m_hInstance = hInstance;

    ConfigManager::Initialize(hInstance);
    Logger::Initialize(hInstance);

    Logger::Write(L"Main", L"Host application initialization started.");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"HostAppClass";
    RegisterClassW(&wc);

    m_mainWindow = CreateWindowExW(0, L"HostAppClass", L"", 0, 0, 0, 0, 0,
                                   HWND_MESSAGE, nullptr, hInstance, nullptr);
    if (!m_mainWindow) {
        Logger::Write(L"Error", L"Failed to create main window.");
        return false;
    }

    Logger::Write(L"Main", L"Host application initialization completed.");
    return true;
}

int HostApp::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    m_moduleManager.ShutdownAllModules();
    Logger::Write(L"Main", L"Host application shutdown completed.");
    return static_cast<int>(msg.wParam);
}

HINSTANCE HostApp::GetInstance() const { return m_hInstance; }
HWND HostApp::GetMainWindow() const { return m_mainWindow; }
TranslationService* HostApp::GetTranslationService() const {
    return TranslationService::Get();
}
ModuleManager& HostApp::GetModuleManager() { return m_moduleManager; }
InputManager& HostApp::GetInputManager() { return m_inputManager; }
