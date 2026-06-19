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
};
