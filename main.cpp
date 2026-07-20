//==============================================================================
// main.cpp - Application entry point
//==============================================================================

#include "HostApp.h"
#include "Core/ConfigManager.h"
#include "Core/Logger.h"
#include "Core/LoaderConfig.h"
#include "Core/DebugConsole.h"

//==============================================================================
// wWinMain - Windows Unicode entry point
//------------------------------------------------------------------------------
// 1. Initialize core services to read loader config
// 2. Load module selection from Config/Config_Loader.json
// 3. Route to Pet mode (full app) or Clean mode (standalone runner, future)
//==============================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    DBG_OPEN();
    DBG(L"Application started");

    ConfigManager::Initialize(hInstance);
    Logger::Initialize(hInstance);
    LoaderConfig::Data cfg = LoaderConfig::Load();

    if (cfg.mode == LoaderConfig::Mode::Clean) {
        DBG(L"Clean mode selected — not yet implemented, falling through to Pet");
    }

    DBG(L"Starting in Pet mode with %zu module(s) selected",
        cfg.enabledModules.empty() ? 7 : cfg.enabledModules.size());

    HostApp app;
    if (!app.Initialize(hInstance, nCmdShow, cfg)) {
        return 1;
    }

    return app.Run();
}
