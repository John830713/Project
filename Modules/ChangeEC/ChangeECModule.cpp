//==============================================================================
// ChangeECModule.cpp - ChangeEC module implementation
//==============================================================================

#include "ChangeECModule.h"
#include "ChangeECWindow.h"
#include "ChangeECLogic.h"
#include "../../Core/ConfigManager.h"
#include "../../Services/TranslationService.h"
#include <windows.h>

enum { kActionOpenChangeEC = 1, kMenuOpenChangeEC = 200 };

//==============================================================================
// Constructor / Configuration
//==============================================================================

ChangeECModule::ChangeECModule() {
    InitializeDefinitions();
    InitializeDefaults();
}

void ChangeECModule::InitializeDefinitions() {
    m_definitions = {
        { L"Enabled", L"Enable Module", ConfigValueType::Bool, L"1", 0, 1 },
        { L"Offset", L"Offset", ConfigValueType::String, L"0x6000", 0, 0 },
        { L"VerboseLog", L"Enable Verbose Logging", ConfigValueType::Bool, L"1", 0, 1 }
    };
}

void ChangeECModule::InitializeDefaults() {
    for (const auto& def : m_definitions) {
        m_values[def.key] = def.defaultValue;
    }
}

const std::vector<ConfigFieldDefinition>& ChangeECModule::GetConfigDefinitions() const {
    return m_definitions;
}

//==============================================================================
// Lifecycle
//==============================================================================

void ChangeECModule::Initialize(HINSTANCE hInst, IHostContext* host) {
    m_hInst = hInst;
    m_host = host;
}

void ChangeECModule::LoadConfig() {
    m_values = ConfigManager::LoadModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        m_definitions
    );
}

void ChangeECModule::SaveConfig() {
    ConfigManager::SaveModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        m_values
    );
}

void ChangeECModule::ApplyConfig() {}
void ChangeECModule::Shutdown() { SaveConfig(); }

std::wstring ChangeECModule::GetValue(const std::wstring& key) const {
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        return it->second;
    }
    return L"";
}

void ChangeECModule::SetValue(const std::wstring& key, const std::wstring& value) {
    m_values[key] = value;
}

//==============================================================================
// Private Helpers
//==============================================================================

bool ChangeECModule::GetBoolValue(const std::wstring& key, bool defaultValue) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) {
        return defaultValue;
    }
    return it->second == L"1" || it->second == L"true" || it->second == L"TRUE";
}

std::wstring ChangeECModule::GetStringValue(const std::wstring& key, const std::wstring& defaultVal) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) {
        return defaultVal;
    }
    return it->second;
}

//==============================================================================
// Drop Actions
//==============================================================================

bool ChangeECModule::CanHandleDrop(const DropContext& ctx) const {
    if (!GetBoolValue(L"Enabled", true)) {
        return false;
    }
    return !ctx.filePaths.empty();
}

std::vector<DropActionDefinition> ChangeECModule::GetDropActions(const DropContext& ctx) const {
    std::vector<DropActionDefinition> actions;
    if (ctx.filePaths.size() + m_pendingFiles.size() >= 2) {
        actions.push_back({ kActionOpenChangeEC, TranslationService::Get()->Tr(L"ChangeEC", L"Change EC Mode") });
    } else {
        actions.push_back({ kActionOpenChangeEC, TranslationService::Get()->Tr(L"ChangeEC", L"Add to Change EC Pending List") });
    }
    return actions;
}

void ChangeECModule::ExecuteDropAction(int actionId, const DropContext& ctx) {
    if (actionId != kActionOpenChangeEC) return;

    for (const auto& path : ctx.filePaths) {
        if (m_pendingFiles.size() < 2) m_pendingFiles.push_back(path);
    }

    if (m_pendingFiles.size() < 2) {
        MessageBoxW(m_host->GetMainWindow(),
            TranslationService::Get()->Tr(L"ChangeEC", L"Files received. Please drag and drop the next file (BIOS ROM or EC BIN).").c_str(),
            TranslationService::Get()->Tr(L"ChangeEC", L"Notification").c_str(), MB_OK);
        return;
    }

    ChangeECWindow* window = new ChangeECWindow(m_hInst);
    if (window->Create(m_host->GetMainWindow())) {
        ChangeECOpenRequest req;
        std::filesystem::path p1 = m_pendingFiles[0];
        std::filesystem::path p2 = m_pendingFiles[1];
        std::filesystem::path bios, ec;
        
        ChangeECLogic::DetectFiles(p1, p2, bios, ec);
        req.biosPath = bios.wstring();
        req.ecPath = ec.wstring();

        window->Open(req, m_host);
        window->Show(SW_SHOW);
    }
    m_pendingFiles.clear();
}

//==============================================================================
// Context menu (IFeatureModule override)
//==============================================================================

std::vector<ContextMenuItem> ChangeECModule::GetContextMenuItems() const {
    if (!GetBoolValue(L"Enabled", true)) {
        return {};
    }
    return { { kMenuOpenChangeEC, TranslationService::Get()->Tr(L"ChangeEC", L"Patch EC (Select files...)") } };
}

void ChangeECModule::ExecuteContextMenuItem(int itemId) {
    if (itemId != kMenuOpenChangeEC || !m_host) {
        return;
    }

    wchar_t path[MAX_PATH] = {};

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_host->GetMainWindow();
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"BIOS ROM (*.rom)\0*.rom\0All Files (*.*)\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring firstFile = path;

    path[0] = L'\0';
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"EC Binary (*.bin)\0*.bin\0All Files (*.*)\0*.*\0";

    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring secondFile = path;

    std::filesystem::path p1(firstFile);
    std::filesystem::path p2(secondFile);
    std::filesystem::path bios, ec;

    ChangeECLogic::DetectFiles(p1, p2, bios, ec);

    ChangeECWindow* window = new ChangeECWindow(m_hInst);
    if (window->Create(m_host->GetMainWindow())) {
        ChangeECOpenRequest req;
        req.biosPath = bios.wstring();
        req.ecPath = ec.wstring();
        window->Open(req, m_host);
        window->Show(SW_SHOW);
    }
}