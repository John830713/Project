//==============================================================================
// RomCombinerModule.cpp - Rom Combiner module implementation
//==============================================================================

#include "RomCombinerModule.h"
#include "RomCombinerWindow.h"
#include "../../Core/ConfigManager.h"
#include "../../Services/TranslationService.h"
#include <windows.h>

//==============================================================================
// Constructor / Config
//==============================================================================

RomCombinerModule::RomCombinerModule() {
    InitializeDefinitions();
    InitializeDefaults();
}

//------------------------------------------------------------------------------
// InitializeDefinitions - populates the config field list
//------------------------------------------------------------------------------

void RomCombinerModule::InitializeDefinitions() {
    m_definitions = {
        { L"Enabled", L"Enable Module", ConfigValueType::Bool, L"1", 0, 1 },
        { L"VerboseLog", L"Enable Verbose Logging", ConfigValueType::Bool, L"1", 0, 1 }
    };
}

//------------------------------------------------------------------------------
// InitializeDefaults - sets all config values to their defaults
//------------------------------------------------------------------------------

void RomCombinerModule::InitializeDefaults() {
    for (const auto& def : m_definitions) {
        m_values[def.key] = def.defaultValue;
    }
}

//==============================================================================
// Lifecycle
//==============================================================================

void RomCombinerModule::Initialize(HINSTANCE hInst, IHostContext* host) {
    m_hInst = hInst;
    m_host = host;
}

//------------------------------------------------------------------------------
// Config Persistence
//------------------------------------------------------------------------------

void RomCombinerModule::LoadConfig() {
    m_values = ConfigManager::LoadModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        m_definitions
    );
}

void RomCombinerModule::SaveConfig() {
    ConfigManager::SaveModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        m_values
    );
}

void RomCombinerModule::ApplyConfig() {}
void RomCombinerModule::Shutdown() { SaveConfig(); }

//------------------------------------------------------------------------------
// Config Accessors
//------------------------------------------------------------------------------

std::wstring RomCombinerModule::GetValue(const std::wstring& key) const {
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        return it->second;
    }
    return L"";
}

void RomCombinerModule::SetValue(const std::wstring& key, const std::wstring& value) {
    m_values[key] = value;
}

const std::vector<ConfigFieldDefinition>& RomCombinerModule::GetConfigDefinitions() const {
    return m_definitions;
}

bool RomCombinerModule::GetBoolValue(const std::wstring& key, bool defaultValue) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) {
        return defaultValue;
    }
    return it->second == L"1" || it->second == L"true" || it->second == L"TRUE";
}

//==============================================================================
// Drop Actions
//==============================================================================

bool RomCombinerModule::CanHandleDrop(const DropContext& ctx) const {
    if (!GetBoolValue(L"Enabled", true)) {
        return false;
    }
    return !ctx.filePaths.empty();
}

std::vector<DropActionDefinition> RomCombinerModule::GetDropActions(const DropContext& ctx) const {
    return { { kActionOpenCombiner, TranslationService::Get()->Tr(L"RomCombiner", L"Combine ROMs") } };
}

void RomCombinerModule::ExecuteDropAction(int actionId, const DropContext& ctx) {
    if (actionId != kActionOpenCombiner) return;

    for (const auto& path : ctx.filePaths) {
        if (m_pendingFiles.size() < 2) m_pendingFiles.push_back(path);
    }

    if (m_pendingFiles.size() < 2) {
        MessageBoxW(m_host->GetMainWindow(),
            TranslationService::Get()->Tr(L"RomCombiner", L"Files received. Please drag and drop the next file.").c_str(),
            TranslationService::Get()->Tr(L"RomCombiner", L"Notification").c_str(), MB_OK);
        return;
    }

    RomCombinerOpenRequest req;
    req.rom1Path = m_pendingFiles[0];
    req.rom2Path = m_pendingFiles[1];
    
    RomCombinerWindow* window = new RomCombinerWindow(m_hInst);
    if (window->Create(m_host->GetMainWindow())) {
        window->Open(req, m_host);
        window->Show(SW_SHOW);
    }
    m_pendingFiles.clear();
}

//==============================================================================
// Context menu (IFeatureModule override)
//==============================================================================

std::vector<ContextMenuItem> RomCombinerModule::GetContextMenuItems() const {
    if (!GetBoolValue(L"Enabled", true)) {
        return {};
    }
    return { { kMenuOpenCombiner, TranslationService::Get()->Tr(L"RomCombiner", L"Combine ROMs (Select files...)") } };
}

void RomCombinerModule::ExecuteContextMenuItem(int itemId) {
    if (itemId != kMenuOpenCombiner || !m_host) {
        return;
    }

    wchar_t path[MAX_PATH] = {};

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_host->GetMainWindow();
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"ROM files (*.rom)\0*.rom\0All Files (*.*)\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring rom1 = path;
    path[0] = L'\0';

    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring rom2 = path;

    RomCombinerOpenRequest req;
    req.rom1Path = rom1;
    req.rom2Path = rom2;

    RomCombinerWindow* window = new RomCombinerWindow(m_hInst);
    if (window->Create(m_host->GetMainWindow())) {
        window->Open(req, m_host);
        window->Show(SW_SHOW);
    }
}

//==============================================================================
// Config Accessors