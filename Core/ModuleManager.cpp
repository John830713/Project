//==============================================================================
// ModuleManager.cpp - Module lifecycle and action dispatch implementation
//==============================================================================

#include "ModuleManager.h"

#include <algorithm>
#include <filesystem>
#include <map>

#ifdef ENABLE_DEBUG_STATE
#include "IDebugStateProvider.h"
#endif

ModuleManager::ModuleManager() {
}

ModuleManager::~ModuleManager() {
    for (auto* module : m_modules) {
        delete module;
    }
    m_modules.clear();
}

//==============================================================================
// MenuOrder - read priority from Config_Pet.ini [MenuOrder]
//==============================================================================

static int GetModulePriority(const std::wstring& moduleName) {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path iniPath = std::filesystem::path(exePath).parent_path() / L"Config" / L"Config_Pet.ini";

    wchar_t buf[16] = {};
    GetPrivateProfileStringW(L"MenuOrder", moduleName.c_str(), L"999",
        buf, static_cast<DWORD>(std::size(buf)), iniPath.c_str());
    return _wtoi(buf);
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

    m_menuRoutes.clear();

    std::vector<IFeatureModule*> sorted(m_modules.begin(), m_modules.end());
    std::stable_sort(sorted.begin(), sorted.end(),
        [](const IFeatureModule* a, const IFeatureModule* b) {
            return GetModulePriority(a->GetModuleName()) < GetModulePriority(b->GetModuleName());
        });

    int uniqueId = 0;

    for (auto* module : sorted) {
        std::vector<ContextMenuItem> items = module->GetContextMenuItems();
        for (const auto& item : items) {
            result.push_back({ uniqueId, item.label, item.flags });
            m_menuRoutes.push_back({ module, item.itemId });
            ++uniqueId;
        }
    }

    return result;
}

std::vector<ModuleManager::ModuleMenuGroup> ModuleManager::GetMenuGroups() const {
    std::vector<ModuleMenuGroup> groups;
    m_menuRoutes.clear();

    // Sort modules by priority from Config_Pet.ini [MenuOrder]
    std::vector<IFeatureModule*> sorted(m_modules.begin(), m_modules.end());
    std::stable_sort(sorted.begin(), sorted.end(),
        [](const IFeatureModule* a, const IFeatureModule* b) {
            return GetModulePriority(a->GetModuleName()) < GetModulePriority(b->GetModuleName());
        });

    int uniqueId = 0;

    for (auto* module : sorted) {
        auto items = module->GetContextMenuItems();
        if (!items.empty()) {
            ModuleMenuGroup group;
            group.displayName = module->GetDisplayName();
            group.items.reserve(items.size());
            for (const auto& item : items) {
                group.items.push_back({ uniqueId, item.label, item.flags, item.tooltip });
                m_menuRoutes.push_back({ module, item.itemId });
                ++uniqueId;
            }
            groups.push_back(std::move(group));
        }
    }

    return groups;
}

bool ModuleManager::ExecuteContextMenuItem(int uniqueId) {
    if (uniqueId < 0 || uniqueId >= static_cast<int>(m_menuRoutes.size())) {
        return false;
    }

    const auto& route = m_menuRoutes[uniqueId];
    route.module->ExecuteContextMenuItem(route.originalItemId);
    return true;
}

#ifdef ENABLE_DEBUG_STATE
std::wstring ModuleManager::DebugGetState() const {
    std::wstring s;
    s += L"=== ModuleManager ===\n";
    s += L"Modules: " + std::to_wstring(m_modules.size()) + L"\n";
    for (const auto* mod : m_modules) {
        s += L"\n--- " + std::wstring(mod->GetModuleName()) + L" ---\n";
        auto* debugMod = dynamic_cast<const IDebugStateProvider*>(mod);
        if (debugMod) {
            s += debugMod->DebugGetState();
        } else {
            s += L"  (no debug state)\n";
        }
    }
    return s;
}
#endif
