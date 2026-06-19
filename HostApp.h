#pragma once

#include "Core/IHostContext.h"
#include "Core/ModuleManager.h"
#include "Core/InputManager.h"
#include "Services/TranslationService.h"

#include <windows.h>
#include <string>

class HostApp : public IHostContext {
public:
    HostApp();
    ~HostApp();

    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    int Run();

    //--- IHostContext ---
    HINSTANCE GetInstance() const override;
    HWND GetMainWindow() const override;
    TranslationService* GetTranslationService() const override;

    //--- Manager access ---
    ModuleManager& GetModuleManager();
    InputManager& GetInputManager();

private:
    HINSTANCE m_hInstance = nullptr;
    HWND m_mainWindow = nullptr;
    ModuleManager m_moduleManager;
    InputManager m_inputManager;
};
