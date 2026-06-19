//==============================================================================
// IDropActionProvider.h - Interface for modules that handle file drops
// Purpose:   Modules implement this to declare what file types they accept
//            and what actions to offer when files are dropped.
//==============================================================================

#pragma once

#include "DropTypes.h"
#include <vector>

class IDropActionProvider {
public:
    virtual ~IDropActionProvider() {}

    virtual bool CanHandleDrop(const DropContext& ctx) const = 0;
    virtual std::vector<DropActionDefinition> GetDropActions(const DropContext& ctx) const = 0;
    virtual void ExecuteDropAction(int actionId, const DropContext& ctx) = 0;
};
