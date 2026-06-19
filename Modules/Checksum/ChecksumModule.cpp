//==============================================================================
// ChecksumModule.cpp - Checksum calculation module implementation
//==============================================================================

#include "ChecksumModule.h"

#include "../../Core/ConfigManager.h"
#include "../../Core/Logger.h"
#include "../../Services/ChecksumService.h"
#include "../../Services/ClipboardService.h"
#include "../../Services/TranslationService.h"

#include <filesystem>
#include <shlobj.h>

namespace fs = std::filesystem;

namespace {
    constexpr int kActionGetChecksum = 1;
    constexpr int kMenuCalcChecksum = 100;
}

//==============================================================================
// Constructor / Destructor
//==============================================================================

ChecksumModule::ChecksumModule()
    : m_hInstance(nullptr), m_host(nullptr) {
    InitializeDefinitions();
    InitializeDefaults();
}

ChecksumModule::~ChecksumModule() {
}

//==============================================================================
// Config definitions
//==============================================================================

const std::vector<ConfigFieldDefinition>& ChecksumModule::GetConfigDefinitions() const {
    return m_definitions;
}

//==============================================================================
// Lifecycle
//==============================================================================

void ChecksumModule::Initialize(HINSTANCE hInst, IHostContext* host) {
    m_hInstance = hInst;
    m_host = host;
}

void ChecksumModule::LoadConfig() {
    m_values = ConfigManager::LoadModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        m_definitions
    );
}

void ChecksumModule::SaveConfig() {
    ConfigManager::SaveModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        m_values
    );
}

void ChecksumModule::ApplyConfig() {
}

void ChecksumModule::Shutdown() {
    SaveConfig();
}

//==============================================================================
// Value access
//==============================================================================

std::wstring ChecksumModule::GetValue(const std::wstring& key) const {
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        return it->second;
    }
    return L"";
}

void ChecksumModule::SetValue(const std::wstring& key, const std::wstring& value) {
    m_values[key] = value;
}

//==============================================================================
// IDropActionProvider - drag-and-drop handling
//==============================================================================

bool ChecksumModule::CanHandleDrop(const DropContext& ctx) const {
    if (ctx.filePaths.size() != 1) {
        return false;
    }

    if (!GetBoolValue(L"Enabled", true)) {
        return false;
    }

    fs::path path(ctx.filePaths[0]);
    if (!fs::is_regular_file(path)) {
        return false;
    }

    if (!ChecksumService::IsSupportedFile(path)) {
        return false;
    }

    std::wstring ext = path.extension().wstring();
    for (auto& c : ext) {
        c = static_cast<wchar_t>(towlower(c));
    }

    if (ext == L".bin" && !GetBoolValue(L"AllowBin", true)) {
        return false;
    }

    if (ext == L".rom" && !GetBoolValue(L"AllowRom", true)) {
        return false;
    }

    return true;
}

std::vector<DropActionDefinition> ChecksumModule::GetDropActions(const DropContext& ctx) const {
    std::vector<DropActionDefinition> actions;
    if (CanHandleDrop(ctx)) {
        actions.push_back({ kActionGetChecksum, TranslationService::Get()->Tr(L"Checksum", L"Get Checksum") });
    }
    return actions;
}

void ChecksumModule::ExecuteDropAction(int actionId, const DropContext& ctx) {
    if (actionId != kActionGetChecksum) {
        return;
    }

    if (ctx.filePaths.empty()) {
        return;
    }

    ProcessFile(ctx.filePaths[0]);
}

//==============================================================================
// Config field initialization
//==============================================================================

void ChecksumModule::InitializeDefinitions() {
    m_definitions = {
        { L"Enabled", L"Enable Module", ConfigValueType::Bool, L"1", 0, 1 },
        { L"AutoCopyToClipboard", L"Auto Copy to Clipboard", ConfigValueType::Bool, L"1", 0, 1 },
        { L"AllowBin", L"Allow .bin files", ConfigValueType::Bool, L"1", 0, 1 },
        { L"AllowRom", L"Allow .rom files", ConfigValueType::Bool, L"1", 0, 1 }
    };
}

void ChecksumModule::InitializeDefaults() {
    for (const auto& def : m_definitions) {
        m_values[def.key] = def.defaultValue;
    }
}

//==============================================================================
// Helper: typed value access
//==============================================================================

bool ChecksumModule::GetBoolValue(const std::wstring& key, bool defaultValue) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) {
        return defaultValue;
    }

    return it->second == L"1" || it->second == L"true" || it->second == L"TRUE";
}

int ChecksumModule::GetIntValue(const std::wstring& key, int defaultValue) const {
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

//==============================================================================
// Context menu (IFeatureModule override) - right-click menu
//------------------------------------------------------------------------------
// Shows "Calculate Checksum..." which opens a file dialog for .bin/.rom files.
//==============================================================================

std::vector<ContextMenuItem> ChecksumModule::GetContextMenuItems() const {
    if (!GetBoolValue(L"Enabled", true)) {
        return {};
    }
    return { { kMenuCalcChecksum, TranslationService::Get()->Tr(L"Checksum", L"Calculate Checksum...") } };
}

void ChecksumModule::ExecuteContextMenuItem(int itemId) {
    if (itemId != kMenuCalcChecksum || !m_host) {
        return;
    }

    OPENFILENAMEW ofn = {};
    wchar_t path[MAX_PATH] = {};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_host->GetMainWindow();
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"All Supported Files (*.bin;*.rom)\0*.bin;*.rom\0Binary Files (*.bin)\0*.bin\0ROM Files (*.rom)\0*.rom\0All Files (*.*)\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&ofn)) {
        ProcessFile(path);
    }
}

//==============================================================================
// ProcessFile - calculates checksum and shows the result
//==============================================================================

void ChecksumModule::ProcessFile(const std::wstring& filePath) {
    std::wstring checksum;
    if (!ChecksumService::CalculateFileChecksum(fs::path(filePath), checksum)) {
        Logger::WriteModuleLog(GetLogSourceName(), L"ERROR: Failed to calculate checksum: " + filePath);
        MessageBoxW(
            m_host ? m_host->GetMainWindow() : nullptr,
            TranslationService::Get()->Tr(L"Checksum", L"Failed to read file or calculate checksum.").c_str(),
            TranslationService::Get()->Tr(L"Checksum", L"Checksum").c_str(),
            MB_OK | MB_ICONERROR
        );
        return;
    }

    bool copied = false;
    if (GetBoolValue(L"AutoCopyToClipboard", true)) {
        copied = ClipboardService::CopyText(checksum);
    }

    Logger::WriteModuleLog(GetLogSourceName(), L"File     : " + filePath);
    Logger::WriteModuleLog(GetLogSourceName(), L"Checksum : " + checksum);
    Logger::WriteModuleLog(GetLogSourceName(), std::wstring(L"Copied   : ") + (copied ? L"Yes" : L"No"));

    auto Tr = [](const wchar_t* text) {
        return TranslationService::Get()->Tr(L"Checksum", text);
    };

    std::wstring msg;
    if (copied) {
        msg = Tr(L"Checksum calculation completed, and the result has been copied to the clipboard.") + L"\n\n" + checksum;
    } else {
        msg = Tr(L"Checksum calculation completed, but failed to copy to the clipboard.") + L"\n\n" + checksum;
    }

    MessageBoxW(
        m_host ? m_host->GetMainWindow() : nullptr,
        msg.c_str(),
        Tr(L"Checksum").c_str(),
        MB_OK | MB_ICONINFORMATION
    );
}
