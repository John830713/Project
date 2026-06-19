//==============================================================================
// TemplateModule.cpp - Template feature module implementation
//==============================================================================

#include "TemplateModule.h"

#include "../../Core/ConfigManager.h"
#include "../../Core/Logger.h"
#include "../../Services/TranslationService.h"

//==============================================================================
// Constructor / Config Setup
//==============================================================================

TemplateModule::TemplateModule() {
    InitializeDefinitions();
    InitializeDefaults();
    Logger::WriteModuleLog(GetLogSourceName(), L"TemplateModule constructed.");
}

TemplateModule::~TemplateModule() {
    Logger::WriteModuleLog(GetLogSourceName(), L"TemplateModule destructed.");
}

void TemplateModule::InitializeDefinitions() {
    m_definitions = {
        { L"Enabled", L"Enable Module", ConfigValueType::Bool, L"1", 0, 1 },
        { L"VerboseLog", L"Enable Verbose Logging", ConfigValueType::Bool, L"1", 0, 1 }
    };
}

void TemplateModule::InitializeDefaults() {
    for (const auto& def : m_definitions) {
        m_values[def.key] = def.defaultValue;
    }
}

//==============================================================================
// Lifecycle
//==============================================================================

void TemplateModule::Initialize(HINSTANCE hInst, IHostContext* host) {
    m_host = host;
    Logger::WriteModuleLog(GetLogSourceName(), L"TemplateModule Initialized.");
}

void TemplateModule::LoadConfig() {
    m_values = ConfigManager::LoadModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        m_definitions
    );
}

void TemplateModule::SaveConfig() {
    ConfigManager::SaveModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        m_values
    );
}

void TemplateModule::ApplyConfig() {}
void TemplateModule::Shutdown() { SaveConfig(); }

//==============================================================================
// Configuration
//==============================================================================

const std::vector<ConfigFieldDefinition>& TemplateModule::GetConfigDefinitions() const {
    return m_definitions;
}

std::wstring TemplateModule::GetValue(const std::wstring& key) const {
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        return it->second;
    }
    return L"";
}

void TemplateModule::SetValue(const std::wstring& key, const std::wstring& value) {
    m_values[key] = value;
}

bool TemplateModule::GetBoolValue(const std::wstring& key, bool defaultValue) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) {
        return defaultValue;
    }
    return it->second == L"1" || it->second == L"true" || it->second == L"TRUE";
}

//==============================================================================
// Drop Actions
//==============================================================================

bool TemplateModule::CanHandleDrop(const DropContext& ctx) const {
    if (!GetBoolValue(L"Enabled", true)) {
        return false;
    }
    return !ctx.filePaths.empty();
}

std::vector<DropActionDefinition> TemplateModule::GetDropActions(const DropContext& ctx) const {
    std::vector<DropActionDefinition> actions;
    DropActionDefinition action;
    action.actionId = 1;
    action.label = TranslationService::Get()->Tr(L"Template", L"Execute Template Action"); 
    actions.push_back(action);
    return actions;
}

void TemplateModule::ExecuteDropAction(int actionId, const DropContext& ctx) {
    Logger::WriteModuleLog(GetLogSourceName(), L"File dropped successfully. Processing files...");

    if (m_host) {
        MessageBoxW(m_host->GetMainWindow(), 
                    TranslationService::Get()->Tr(L"Template", L"File dropped successfully!").c_str(), 
                    TranslationService::Get()->Tr(L"Template", L"Template Module").c_str(), 
                    MB_OK | MB_ICONINFORMATION);
    }
}

//==============================================================================
// Context menu (IFeatureModule override)
//==============================================================================

std::vector<ContextMenuItem> TemplateModule::GetContextMenuItems() const {
    if (!GetBoolValue(L"Enabled", true)) {
        return {};
    }
    return { { 100, TranslationService::Get()->Tr(L"Template", L"Execute Template (Select file...)") } };
}

void TemplateModule::ExecuteContextMenuItem(int itemId) {
    if (itemId != 100 || !m_host) {
        return;
    }

    wchar_t path[MAX_PATH] = {};

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_host->GetMainWindow();
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&ofn)) return;

    Logger::WriteModuleLog(GetLogSourceName(), L"File selected via context menu: " + std::wstring(path));
    MessageBoxW(m_host->GetMainWindow(),
                TranslationService::Get()->Tr(L"Template", L"File selected successfully!").c_str(),
                TranslationService::Get()->Tr(L"Template", L"Template Module").c_str(),
                MB_OK | MB_ICONINFORMATION);
}