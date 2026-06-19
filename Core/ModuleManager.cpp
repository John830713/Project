//==============================================================================
// ModuleManager.cpp - Module lifecycle and action dispatch implementation
//==============================================================================

#include "ModuleManager.h"

ModuleManager::ModuleManager() {
}

ModuleManager::~ModuleManager() {
    for (auto* module : m_modules) {
        delete module;
    }
    m_modules.clear();
}

//==============================================================================
// Module lifecycle
//==============================================================================

void ModuleManager::RegisterModule(IFeatureModule* module) {
    m_modules.push_back(module);
}

void ModuleManager::InitializeModules(HINSTANCE hInst, IHostContext* host) {
    for (auto* module : m_modules) {
        module->Initialize(hInst, host);
    }
}

void ModuleManager::LoadAllConfigs() {
    for (auto* module : m_modules) {
        module->LoadConfig();
    }
}

void ModuleManager::SaveAllConfigs() {
    for (auto* module : m_modules) {
        module->SaveConfig();
    }
}

void ModuleManager::ApplyAllConfigs() {
    for (auto* module : m_modules) {
        module->ApplyConfig();
    }
}

void ModuleManager::ShutdownAllModules() {
    for (auto* module : m_modules) {
        module->Shutdown();
    }
}

//==============================================================================
// Drop action dispatch - queries modules implementing IDropActionProvider
//==============================================================================

std::vector<ResolvedDropAction> ModuleManager::GetDropActions(const DropContext& ctx) {
    std::vector<ResolvedDropAction> result;

    for (auto* module : m_modules) {
        auto* provider = dynamic_cast<IDropActionProvider*>(module);
        if (!provider) {
            continue;
        }

        if (!provider->CanHandleDrop(ctx)) {
            continue;
        }

        std::vector<DropActionDefinition> actions = provider->GetDropActions(ctx);
        for (const auto& action : actions) {
            result.push_back({ provider, action });
        }
    }

    return result;
}

bool ModuleManager::ExecuteDropAction(const ResolvedDropAction& resolvedAction, const DropContext& ctx) {
    if (!resolvedAction.provider) {
        return false;
    }

    resolvedAction.provider->ExecuteDropAction(resolvedAction.action.actionId, ctx);
    return true;
}

//==============================================================================
// Context menu dispatch - queries all modules via IFeatureModule
//==============================================================================

std::vector<ContextMenuItem> ModuleManager::GetContextMenuItems() const {
    std::vector<ContextMenuItem> result;

    for (auto* module : m_modules) {
        std::vector<ContextMenuItem> items = module->GetContextMenuItems();
        for (const auto& item : items) {
            result.push_back(item);
        }
    }

    return result;
}

bool ModuleManager::ExecuteContextMenuItem(int itemId) {
    for (auto* module : m_modules) {
        std::vector<ContextMenuItem> items = module->GetContextMenuItems();
        for (const auto& item : items) {
            if (item.itemId == itemId) {
                module->ExecuteContextMenuItem(itemId);
                return true;
            }
        }
    }

    return false;
}
