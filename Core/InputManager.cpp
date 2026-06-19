//==============================================================================
// InputManager.cpp - Centralized input dispatch implementation
//==============================================================================

#include "InputManager.h"
#include <algorithm>

//------------------------------------------------------------------------------
// RegisterHandler - adds a handler and sorts by priority (highest first)
//------------------------------------------------------------------------------

void InputManager::RegisterHandler(IInputHandler* handler) {
    m_handlers.push_back(handler);
    std::sort(m_handlers.begin(), m_handlers.end(),
        [](IInputHandler* a, IInputHandler* b) {
            return a->GetPriority() > b->GetPriority();
        });
}

void InputManager::UnregisterHandler(IInputHandler* handler) {
    m_handlers.erase(
        std::remove(m_handlers.begin(), m_handlers.end(), handler),
        m_handlers.end());
}

void InputManager::SetMode(InputMode mode) {
    m_mode = mode;
}

InputMode InputManager::GetMode() const {
    return m_mode;
}

//------------------------------------------------------------------------------
// Dispatch - sends event to handlers in priority order
// Returns true when a handler consumes the event
//------------------------------------------------------------------------------

bool InputManager::Dispatch(const InputEvent& event) {
    for (auto* handler : m_handlers) {
        if (handler->HandleInput(event)) {
            return true;
        }
    }
    return false;
}
