//==============================================================================
// ILedIndicator.h - Optional module surface used by the pet window status dot
// Purpose:   Lets the pet window drive the indicator lamp of whichever module
//            owns one, without including that module's header. A hard
//            #include would make Pet/ depend on Modules/RemoteControl and
//            break subset builds (BuildUI.py --modules), where the module is
//            not compiled at all.
//==============================================================================

#pragma once
#include <windows.h>

class ILedIndicator {
public:
    virtual ~ILedIndicator() {}
    virtual bool IsRunning() const = 0;
    virtual COLORREF GetLedColor() const = 0;
    virtual void ShowLed(bool visible) = 0;
};
