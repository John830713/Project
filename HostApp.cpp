#include "HostApp.h"

#include "Pet/MainWindow.h"
#include "Core/Logger.h"
#include "Core/ConfigManager.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

HostApp::HostApp() : m_hInstance(nullptr), m_mainWindow(nullptr), m_window(nullptr) {}
HostApp::~HostApp() { delete m_window; }

bool HostApp::Initialize(HINSTANCE hInstance, int nCmdShow) {
    m_hInstance = hInstance;

    ConfigManager::Initialize(hInstance);
    Logger::Initialize(hInstance);

    Logger::Write(L"Main", L"Host application initialization started.");

    std::wstring projectTitle = LoadProjectTitle();
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
            L"",
            appVersion)) {
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

std::wstring HostApp::LoadAppVersion() const {
    std::wstring text = ReadTextFileIfExists(L"Version.txt");
    if (!text.empty()) return text;
    return L"1.0.0";
}
