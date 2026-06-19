//==============================================================================
// IInputHandler.h - Input handling interface
// Purpose:   Defines input modes, event structure, and the handler interface
//            for centralized input dispatch via InputManager.
//==============================================================================

#pragma once

#include <windows.h>

enum class InputMode {
    Normal,
    DraggingHostWindow,
    SettingsDialogOpen,
    CustomToolWindowOpen,
    DropActionMenuOpen
};

struct InputEvent {
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    InputMode currentMode;
};

class IInputHandler {
public:
    virtual ~IInputHandler() {}
    virtual bool HandleInput(const InputEvent& event) = 0;
    virtual int GetPriority() const { return 0; }
};
