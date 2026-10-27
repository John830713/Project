#include "HostApp.h"

#include "Pet/MainWindow.h"
#include "Core/Logger.h"
#include "Core/ConfigManager.h"
#include "Core/LoaderConfig.h"
#include "Core/DebugConsole.h"
#include "Core/UpdateManager.h"
#include "GeneratedModuleRegistry.h"

#include <objbase.h>
#include <mmsystem.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

// Host-level pet visibility flag. Lives in the Tray module config so it sits
// next to the tray settings that own the toggle, and so it survives restarts.
constexpr wchar_t kTrayConfigFile[] = L"Config_Tray.ini";
constexpr wchar_t kTrayConfigSection[] = L"Tray";

const std::vector<ConfigFieldDefinition> kTrayConfigDefinitions = {
    { L"petVisible", L"Pet Visible", ConfigValueType::Bool, L"1", 0, 0 },
};

}  // namespace

HostApp::HostApp()
    : m_hInstance(nullptr), m_mainWindow(nullptr), m_window(nullptr), m_trayHost(this),
      m_comInitialized(false) {}
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

bool HostApp::Initialize(HINSTANCE hInstance, int nCmdShow, const LoaderConfig::Data& config) {
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

    bool petVisible = LoadMainWindowVisibility();
    int windowCmdShow = petVisible ? nCmdShow : SW_HIDE;
    if (!CreateMainWindow(windowCmdShow)) {
        Logger::Write(L"Error", L"Failed to create main window.");
        return false;
    }

    RegisterGeneratedModules(m_moduleManager, config.enabledModules);

    m_moduleManager.InitializeModules(hInstance, this);
    m_moduleManager.LoadAllConfigs();
    m_moduleManager.ApplyAllConfigs();

    if (!m_trayHost.Initialize()) {
        Logger::Write(L"Warning", L"Tray host failed to initialize.");
    }

    Logger::Write(L"Main", L"Host application initialization completed.");
    return true;
}

bool HostApp::CreateMainWindow(int nCmdShow) {
    m_window = new MainWindow();
    if (!m_window->Create(
            m_hInstance,
            nCmdShow,
            this,
            &m_moduleManager,
            &m_inputManager,
            m_mainWindow,
            LoadProjectTitle(),
            LoadHostHint(),
            LoadAppVersion())) {
        delete m_window;
        m_window = nullptr;
        m_mainWindow = nullptr;
        return false;
    }
    return true;
}

int HostApp::Run() {
#if DEBUG_CONSOLE
    DebugConsoleSetHost(this);
    HANDLE hConsoleReader = StartConsoleReader();
    StartPipeServer();
#endif

    MSG msg;
    BOOL fetched;
    while ((fetched = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (fetched == -1) {
            Logger::Write(L"Error", L"GetMessageW failed; terminating message loop.");
            break;
        }
#if DEBUG_CONSOLE
        if (msg.hwnd == nullptr && msg.message == WM_APP_CONSOLE_CMD) {
            const wchar_t* cmd = reinterpret_cast<const wchar_t*>(msg.lParam);
            RunConsoleCommand(cmd, &m_moduleManager, m_mainWindow, m_hInstance);
            free(const_cast<wchar_t*>(cmd));
            continue;
        }
        if (msg.hwnd == nullptr && msg.message == WM_APP_CONSOLE_PIPE_CMD) {
            // Runs on the main thread because RunConsoleCommand/tray commands
            // touch main-thread-owned windows. Signals the pipe thread.
            DebugConsoleServePipeRequest(reinterpret_cast<void*>(msg.lParam),
                                         &m_moduleManager, m_mainWindow, m_hInstance);
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
    // Shutdown markers: if the app "silently" stops responding on exit, the last
    // marker printed here names the step that hung.
    DBG(L"[shutdown] message loop exited");
    StopPipeServer();
    DBG(L"[shutdown] pipe server stopped");
    StopConsoleReader(hConsoleReader);
    DBG(L"[shutdown] console reader stopped");
#endif

    m_trayHost.Shutdown();
    DBG(L"[shutdown] tray stopped");
    m_moduleManager.ShutdownAllModules();
    DBG(L"[shutdown] modules stopped");
    timeEndPeriod(1);
    Logger::Write(L"Main", L"Host application shutdown completed.");
    return (fetched == 0) ? static_cast<int>(msg.wParam) : 1;
}

HINSTANCE HostApp::GetInstance() const { return m_hInstance; }
HWND HostApp::GetMainWindow() const { return m_mainWindow; }
TranslationService* HostApp::GetTranslationService() const {
    return TranslationService::Get();
}
ModuleManager& HostApp::GetModuleManager() { return m_moduleManager; }
InputManager& HostApp::GetInputManager() { return m_inputManager; }

bool HostApp::IsMainWindowAlive() const {
    return m_mainWindow != nullptr && IsWindow(m_mainWindow) != FALSE;
}

bool HostApp::IsMainWindowVisible() const {
    return IsMainWindowAlive() && IsWindowVisible(m_mainWindow) != FALSE;
}

void HostApp::SetMainWindowVisible(bool visible) {
    if (!IsMainWindowAlive()) {
        return;
    }
    ShowWindow(m_mainWindow, visible ? SW_SHOW : SW_HIDE);
    SaveMainWindowVisibility(visible);
}

void HostApp::RecreateMainWindow() {
    bool wasVisible = IsMainWindowVisible();

    if (m_window) {
        // Suppress the unconditional PostQuitMessage in WM_DESTROY so the
        // message loop survives the rebuild.
        m_window->SetRecreating(true);
    }
    if (IsMainWindowAlive()) {
        DestroyWindow(m_mainWindow);
    }
    m_mainWindow = nullptr;

    delete m_window;
    m_window = nullptr;

    if (!CreateMainWindow(wasVisible ? SW_SHOW : SW_HIDE)) {
        Logger::Write(L"Error", L"Failed to recreate main window.");
        return;
    }

    m_trayHost.OnPetWindowRecreated(m_mainWindow);
    Logger::Write(L"Main", L"Main window recreated.");
}

void HostApp::OpenMainWindowSettings() {
    if (m_window) {
        m_window->OpenSettings(m_mainWindow);
    }
}

void HostApp::DebugDestroyMainWindow() {
    if (!IsMainWindowAlive()) {
        return;
    }
    if (m_window) {
        m_window->SetRecreating(true);
    }
    DestroyWindow(m_mainWindow);
    // Keep m_mainWindow/m_window dangling so IsMainWindowAlive() reports false
    // and the tray watchdog rebuilds the window.
}

void HostApp::PollForReload() {
    std::wstring target;
    if (!UpdateManager::PollReloadRequest(&target)) {
        return;
    }

    Logger::Write(L"Main", L"Reload requested; relaunching " + target);
    if (UpdateManager::SpawnReloadedInstance(target)) {
        PostQuitMessage(0);
    } else {
        Logger::Write(L"Error", L"Reload spawn failed; staying on the old build.");
    }
}

bool HostApp::LoadMainWindowVisibility() const {
    auto values = ConfigManager::LoadModuleConfig(
        kTrayConfigFile, kTrayConfigSection, kTrayConfigDefinitions);
    return values[L"petVisible"] == L"1";
}

void HostApp::SaveMainWindowVisibility(bool visible) const {
    std::map<std::wstring, std::wstring> values;
    values[L"petVisible"] = visible ? L"1" : L"0";
    ConfigManager::SaveModuleConfig(kTrayConfigFile, kTrayConfigSection, values);
}

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
