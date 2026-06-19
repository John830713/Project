//==============================================================================
// DropTypes.h - Drag-and-drop shared types
// Purpose:   Defines the context passed to modules when files are dropped
//            and the structure for action definitions.
//==============================================================================

#pragma once

#include <string>
#include <vector>

struct DropContext {
    std::vector<std::wstring> filePaths;
};

struct DropActionDefinition {
    int actionId;
    std::wstring label;
};
