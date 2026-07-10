//==============================================================================
// IDebugStateProvider.h — Optional interface for debug state dumps.
// Only compiled when ENABLE_DEBUG_STATE is defined (test/debug builds).
//==============================================================================

#pragma once

#include <string>

class IDebugStateProvider {
public:
    virtual ~IDebugStateProvider() {}
    virtual std::wstring DebugGetState() const = 0;
};
