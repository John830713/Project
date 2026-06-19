//==============================================================================
// RomSeparatorModule.cpp - ROM Separator feature module implementation
//==============================================================================

#include "RomSeparatorModule.h"

#include "../../Core/ConfigManager.h"
#include "../../Core/Logger.h"
#include "../../Services/TranslationService.h"
#include "RomSeparatorLogic.h"
#include "RomSeparatorWindow.h"

#include <filesystem>

namespace fs = std::filesystem;

//==============================================================================
// Constructor / Config
//==============================================================================

namespace {
    constexpr int kActionOpenRomSeparator = 1;
    constexpr int kMenuOpenRomSeparator = 200;
}

RomSeparatorModule::RomSeparatorModule()
    : m_hInstance(nullptr), m_host(nullptr), m_window(nullptr) {
    InitializeDefinitions();
    InitializeDefaults();
}

RomSeparatorModule::~RomSeparatorModule() {
    delete m_window;
    m_window = nullptr;
}

//==============================================================================
// Metadata
//==============================================================================

const wchar_t* RomSeparatorModule::GetModuleName() const {
    return L"RomSeparator";
}

const wchar_t* RomSeparatorModule::GetDisplayName() const {
    return L"ROM Separator";
}

const wchar_t* RomSeparatorModule::GetVersion() const {
    return L"1.0.0";
}

const wchar_t* RomSeparatorModule::GetPurpose() const {
    return L"Splits combined ROM files into two separate ROMs.";
}

const wchar_t* RomSeparatorModule::GetConfigFileName() const {
    return L"Config_RomSeparator.ini";
}

const wchar_t* RomSeparatorModule::GetConfigSectionName() const {
    return L"RomSeparator";
}

const wchar_t* RomSeparatorModule::GetLogSourceName() const {
    return L"RomSeparator";
}

const std::vector<ConfigFieldDefinition>& RomSeparatorModule::GetConfigDefinitions() const {
    return m_definitions;
}

//==============================================================================
// Lifecycle
//==============================================================================

void RomSeparatorModule::Initialize(HINSTANCE hInst, IHostContext* host) {
    m_hInstance = hInst;
    m_host = host;
}

void RomSeparatorModule::LoadConfig() {
    m_values = ConfigManager::LoadModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        m_definitions
    );
}

void RomSeparatorModule::SaveConfig() {
    ConfigManager::SaveModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        m_values
    );
}

void RomSeparatorModule::ApplyConfig() {
}

void RomSeparatorModule::Shutdown() {
    SaveConfig();
}

std::wstring RomSeparatorModule::GetValue(const std::wstring& key) const {
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        return it->second;
    }
    return L"";
}

void RomSeparatorModule::SetValue(const std::wstring& key, const std::wstring& value) {
    m_values[key] = value;
}

//==============================================================================
// Drop Actions
//==============================================================================

bool RomSeparatorModule::CanHandleDrop(const DropContext& ctx) const {
    if (ctx.filePaths.size() != 1) {
        return false;
    }

    if (!GetBoolValue(L"Enabled", true)) {
        return false;
    }

    fs::path path(ctx.filePaths[0]);
    if (!fs::exists(path) || !fs::is_regular_file(path)) {
        return false;
    }

    try {
        const auto size = fs::file_size(path);
        if (!RomSeparatorLogic::IsSupportedRomSize(size)) {
            return false;
        }
    } catch (...) {
        return false;
    }

    return true;
}

std::vector<DropActionDefinition> RomSeparatorModule::GetDropActions(const DropContext& ctx) const {
    std::vector<DropActionDefinition> actions;
    if (CanHandleDrop(ctx)) {
        actions.push_back({ kActionOpenRomSeparator, TranslationService::Get()->Tr(L"RomSeparator", L"Open ROM Separator") });
    }
    return actions;
}

void RomSeparatorModule::ExecuteDropAction(int actionId, const DropContext& ctx) {
    if (actionId != kActionOpenRomSeparator) {
        return;
    }

    if (ctx.filePaths.empty()) {
        return;
    }

    OpenWindowForFile(ctx.filePaths[0]);
}

//==============================================================================
// Config Accessors
//==============================================================================

bool RomSeparatorModule::GetConfirmOverwrite() const {
    return GetBoolValue(L"ConfirmOverwrite", true);
}

bool RomSeparatorModule::GetShowSuccessMessage() const {
    return GetBoolValue(L"ShowSuccessMessage", true);
}

bool RomSeparatorModule::GetVerboseLog() const {
    return GetBoolValue(L"VerboseLog", true);
}

RomSeparatorOutputMode RomSeparatorModule::GetDefaultOutputMode() const {
    return GetBoolValue(L"OutputToHostFolderByDefault", true)
        ? RomSeparatorOutputMode::HostFolder
        : RomSeparatorOutputMode::SourceFolder;
}

void RomSeparatorModule::SetDefaultOutputMode(RomSeparatorOutputMode mode) {
    m_values[L"OutputToHostFolderByDefault"] =
        (mode == RomSeparatorOutputMode::HostFolder) ? L"1" : L"0";
}

IHostContext* RomSeparatorModule::GetHost() const {
    return m_host;
}

HINSTANCE RomSeparatorModule::GetInstance() const {
    return m_hInstance;
}

//==============================================================================
// Config Definition Helpers
//==============================================================================

void RomSeparatorModule::InitializeDefinitions() {
    m_definitions = {
        { L"Enabled", L"Enable Module", ConfigValueType::Bool, L"1", 0, 1 },
        { L"OutputToHostFolderByDefault", L"Default Output to Host Folder", ConfigValueType::Bool, L"1", 0, 1 },
        { L"ConfirmOverwrite", L"Confirm Before Overwrite", ConfigValueType::Bool, L"1", 0, 1 },
        { L"ShowSuccessMessage", L"Show Success Message After Completion", ConfigValueType::Bool, L"1", 0, 1 },
        { L"VerboseLog", L"Enable Verbose Logging", ConfigValueType::Bool, L"1", 0, 1 }
    };
}

//------------------------------------------------------------------------------
// InitializeDefaults
//------------------------------------------------------------------------------

void RomSeparatorModule::InitializeDefaults() {
    for (const auto& def : m_definitions) {
        m_values[def.key] = def.defaultValue;
    }
}

//------------------------------------------------------------------------------
// GetBoolValue / GetIntValue
//------------------------------------------------------------------------------

bool RomSeparatorModule::GetBoolValue(const std::wstring& key, bool defaultValue) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) {
        return defaultValue;
    }

    return it->second == L"1" || it->second == L"true" || it->second == L"TRUE";
}

int RomSeparatorModule::GetIntValue(const std::wstring& key, int defaultValue) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) {
        return defaultValue;
    }

    try {
        return std::stoi(it->second);
    } catch (...) {
        return defaultValue;
    }
}

//------------------------------------------------------------------------------
// OpenWindowForFile
//------------------------------------------------------------------------------

void RomSeparatorModule::OpenWindowForFile(const std::wstring& filePath) {
    if (!m_window) {
        m_window = new RomSeparatorWindow();
        if (!m_window->Create(
                m_hInstance,
                m_host ? m_host->GetMainWindow() : nullptr,
                this)) {
            Logger::WriteModuleLog(GetLogSourceName(), L"ERROR: Failed to create RomSeparatorWindow.");
            delete m_window;
            m_window = nullptr;

            MessageBoxW(
                m_host ? m_host->GetMainWindow() : nullptr,
                TranslationService::Get()->Tr(L"RomSeparator", L"Failed to create ROM Separator window.").c_str(),
                GetDisplayName(),
                MB_OK | MB_ICONERROR
            );
            return;
        }
    }

    RomSeparatorOpenRequest request;
    request.romPath = fs::path(filePath);
    request.outputMode = GetDefaultOutputMode();
    request.confirmOverwrite = GetConfirmOverwrite();
    request.showSuccessMessage = GetShowSuccessMessage();
    request.verboseLog = GetVerboseLog();

    m_window->OpenRom(request);
    m_window->Show();
    m_window->BringToFront();

    Logger::WriteModuleLog(GetLogSourceName(), L"Opened ROM Separator for file: " + request.romPath.wstring());
}

//==============================================================================
// Context menu (IFeatureModule override)
//==============================================================================

std::vector<ContextMenuItem> RomSeparatorModule::GetContextMenuItems() const {
    if (!GetBoolValue(L"Enabled", true)) {
        return {};
    }
    return { { kMenuOpenRomSeparator, TranslationService::Get()->Tr(L"RomSeparator", L"Split ROM (Select file...)") } };
}

void RomSeparatorModule::ExecuteContextMenuItem(int itemId) {
    if (itemId != kMenuOpenRomSeparator || !m_host) {
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

    OpenWindowForFile(path);
}
