#pragma once

#include "Core/IHostContext.h"
#include "Core/ModuleManager.h"
#include "Core/InputManager.h"
#include "Core/LoaderConfig.h"
#include "Services/TranslationService.h"

#include <windows.h>
#include <string>

class MainWindow;

class HostApp : public IHostContext {
public:
    HostApp();
    ~HostApp();

    bool Initialize(HINSTANCE hInstance, int nCmdShow, const LoaderConfig::Data& config);
    int Run();

    //--- IHostContext ---
    HINSTANCE GetInstance() const override;
    HWND GetMainWindow() const override;
    TranslationService* GetTranslationService() const override;

    //--- Manager access ---
    ModuleManager& GetModuleManager();
    InputManager& GetInputManager();

private:
    std::wstring LoadProjectTitle() const;
    std::wstring LoadHostHint() const;
    std::wstring LoadAppVersion() const;
    std::wstring ReadTextFileIfExists(const wchar_t* fileName) const;
    std::wstring GetBaseDirName() const;

private:
    HINSTANCE m_hInstance = nullptr;
    HWND m_mainWindow = nullptr;
    MainWindow* m_window = nullptr;
    ModuleManager m_moduleManager;
    InputManager m_inputManager;
    bool m_comInitialized = false;
};
