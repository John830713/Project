//==============================================================================
// ModuleManager.h - Module lifecycle and action dispatch
// Purpose:   Manages registration, initialization, config loading, and
//            dispatch of drop actions and context menu items.
//==============================================================================

#pragma once

#include "IFeatureModule.h"
#include "IDropActionProvider.h"
#include "DropTypes.h"
#include "IHostContext.h"

#include <windows.h>
#include <vector>

struct ResolvedDropAction {
    IDropActionProvider* provider;
    DropActionDefinition action;
};

class ModuleManager {
public:
    ModuleManager();
    ~ModuleManager();

    //--- Module lifecycle ---
    void RegisterModule(IFeatureModule* module);
    void InitializeModules(HINSTANCE hInst, IHostContext* host);
    void LoadAllConfigs();
    void SaveAllConfigs();
    void ApplyAllConfigs();
    void ShutdownAllModules();

    //--- Drop action dispatch ---
    std::vector<ResolvedDropAction> GetDropActions(const DropContext& ctx);
    bool ExecuteDropAction(const ResolvedDropAction& resolvedAction, const DropContext& ctx);

    //--- Context menu dispatch ---
    std::vector<ContextMenuItem> GetContextMenuItems() const;
    bool ExecuteContextMenuItem(int uniqueId);

    //--- Module enumeration ---
    const std::vector<IFeatureModule*>& GetAllModules() const { return m_modules; }

private:
    struct MenuRoute {
        IFeatureModule* module;
        int originalItemId;
    };
    mutable std::vector<MenuRoute> m_menuRoutes;
    std::vector<IFeatureModule*> m_modules;
};
