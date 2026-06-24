//==============================================================================
// main.cpp - Application entry point
//==============================================================================

#include "HostApp.h"
#include "Core/DebugConsole.h"

//==============================================================================
// wWinMain - Windows Unicode entry point
//------------------------------------------------------------------------------
// Creates the HostApp, initializes it, and runs the message loop.
//==============================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    DBG_OPEN();
    DBG(L"Application started");
    HostApp app;
    if (!app.Initialize(hInstance, nCmdShow)) {
        return 1;
    }

    return app.Run();
}
