#include "HostApp.h"

#include "Pet/MainWindow.h"
#include "Core/Logger.h"
#include "Core/ConfigManager.h"
#include "Core/DebugConsole.h"
#include "GeneratedModuleRegistry.h"

#include <objbase.h>
#include <mmsystem.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

HostApp::HostApp()
    : m_hInstance(nullptr), m_mainWindow(nullptr), m_window(nullptr), m_comInitialized(false) {}
HostApp::~HostApp() {
    if (m_comInitialized) CoUninitialize();
    delete m_window;
}

static void SetDpiAware() {
    HMODULE h = LoadLibraryW(L"shcore.dll");
    if (h) {
        typedef HRESULT(WINAPI* FN)(int);
        FN fn = reinterpret_cast<FN>(GetProcAddress(h, "SetProcessDpiAwareness"));
        if (fn) { fn(2); FreeLibrary(h); return; }
        FreeLibrary(h);
    }
    SetProcessDPIAware();
}

bool HostApp::Initialize(HINSTANCE hInstance, int nCmdShow) {
    SetDpiAware();
    timeBeginPeriod(1);
    m_hInstance = hInstance;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        Logger::Write(L"Warning", L"COM initialization failed.");
    } else {
        m_comInitialized = (hr == S_OK);
    }

    ConfigManager::Initialize(hInstance);
    Logger::Initialize(hInstance);

    Logger::Write(L"Main", L"Host application initialization started.");

    std::wstring projectTitle = LoadProjectTitle();
    std::wstring hostHint = LoadHostHint();
    std::wstring appVersion = LoadAppVersion();

    m_window = new MainWindow();
    if (!m_window->Create(
            hInstance,
            nCmdShow,
            this,
            &m_moduleManager,
            &m_inputManager,
            m_mainWindow,
            projectTitle,
            hostHint,
            appVersion)) {
        Logger::Write(L"Error", L"Failed to create main window.");
        return false;
    }

    RegisterGeneratedModules(m_moduleManager);

    m_moduleManager.InitializeModules(hInstance, this);
    m_moduleManager.LoadAllConfigs();
    m_moduleManager.ApplyAllConfigs();

    Logger::Write(L"Main", L"Host application initialization completed.");
    return true;
}

int HostApp::Run() {
#if DEBUG_CONSOLE
    HANDLE hConsoleReader = StartConsoleReader();
#endif

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
#if DEBUG_CONSOLE
        if (msg.hwnd == nullptr && msg.message == WM_APP_CONSOLE_CMD) {
            const wchar_t* cmd = reinterpret_cast<const wchar_t*>(msg.lParam);
            RunConsoleCommand(cmd, &m_moduleManager, m_mainWindow, m_hInstance);
            free(const_cast<wchar_t*>(cmd));
            continue;
        }
#endif
        if (msg.message == WM_KEYDOWN && msg.wParam == 'A' &&
            (GetKeyState(VK_CONTROL) & 0x8000)) {
            HWND hFocus = GetFocus();
            if (hFocus) {
                wchar_t cls[16] = {};
                GetClassNameW(hFocus, cls, 16);
                if (wcscmp(cls, L"Edit") == 0) {
                    SendMessageW(hFocus, EM_SETSEL, 0, -1);
                    continue;
                }
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

#if DEBUG_CONSOLE
    StopConsoleReader(hConsoleReader);
#endif

    m_moduleManager.ShutdownAllModules();
    timeEndPeriod(1);
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

std::wstring HostApp::ReadTextFileIfExists(const wchar_t* fileName) const {
    try {
        fs::path path = fs::current_path() / fileName;
        if (!fs::exists(path) || !fs::is_regular_file(path)) return L"";
        std::ifstream file(path);
        if (!file.is_open()) return L"";
        std::string content;
        std::getline(file, content);
        if (content.empty()) return L"";
        return std::wstring(content.begin(), content.end());
    } catch (...) { return L""; }
}

std::wstring HostApp::GetBaseDirName() const {
    try {
        return fs::current_path().filename().wstring();
    } catch (...) { return L"Host Application"; }
}

std::wstring HostApp::LoadProjectTitle() const {
    std::wstring text = ReadTextFileIfExists(L"ProjectName.txt");
    if (!text.empty()) return text;
    return GetBaseDirName();
}

std::wstring HostApp::LoadHostHint() const {
    std::wstring text = ReadTextFileIfExists(L"HostHint.txt");
    if (!text.empty()) return text;
    return L"Drag supported files onto this window.";
}

std::wstring HostApp::LoadAppVersion() const {
    std::wstring text = ReadTextFileIfExists(L"Version.txt");
    if (!text.empty()) return text;
    return L"1.0.0";
}
