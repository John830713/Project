//==============================================================================
// IHostContext.h - Generic host context interface
// Purpose:   Abstracts the host application so modules don't depend on a
//            specific HostApp class. Provides instance handle and main window.
//==============================================================================

#pragma once

#include <windows.h>

class TranslationService;

class IHostContext {
public:
    virtual ~IHostContext() {}
    virtual HINSTANCE GetInstance() const = 0;
    virtual HWND GetMainWindow() const = 0;
    virtual TranslationService* GetTranslationService() const = 0;

    // Host chrome that shows translated text but is not a child window (the
    // tray tooltip) has to be told to re-read it after the language changes.
    // Default no-op so minimal hosts and test doubles need no change.
    virtual void RefreshTrayTooltip() {}
};
