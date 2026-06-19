//==============================================================================
// InputManager.h - Centralized input dispatch
// Purpose:   Collects input handlers, manages input mode, and dispatches
//            window events to registered handlers by priority order.
//==============================================================================

#pragma once

#include "IInputHandler.h"
#include <vector>

class InputManager {
public:
    void RegisterHandler(IInputHandler* handler);
    void UnregisterHandler(IInputHandler* handler);
    void SetMode(InputMode mode);
    InputMode GetMode() const;
    bool Dispatch(const InputEvent& event);

private:
    InputMode m_mode = InputMode::Normal;
    std::vector<IInputHandler*> m_handlers;
};
