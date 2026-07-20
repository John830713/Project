#include "NetworkInfoModule.h"

#include "../../Core/ConfigManager.h"
#include "../../Services/TranslationService.h"
#include "../../UI/NetworkInfoDialog.h"

namespace {
    constexpr int kMenuNetInfo = 100;
}

NetworkInfoModule::NetworkInfoModule()
    : m_hInstance(nullptr), m_host(nullptr) {
    m_definitions = {
        { L"Enabled", L"Enable Module", ConfigValueType::Bool, L"1", 0, 1 }
    };
    for (const auto& def : m_definitions)
        m_values[def.key] = def.defaultValue;
}

NetworkInfoModule::~NetworkInfoModule() {}

const std::vector<ConfigFieldDefinition>& NetworkInfoModule::GetConfigDefinitions() const {
    return m_definitions;
}

void NetworkInfoModule::Initialize(HINSTANCE hInst, IHostContext* host) {
    m_hInstance = hInst;
    m_host = host;
}

void NetworkInfoModule::LoadConfig() {
    m_values = ConfigManager::LoadModuleConfig(
        GetConfigFileName(), GetConfigSectionName(), m_definitions);
}

void NetworkInfoModule::SaveConfig() {
    ConfigManager::SaveModuleConfig(
        GetConfigFileName(), GetConfigSectionName(), m_values);
}

void NetworkInfoModule::ApplyConfig() {}

void NetworkInfoModule::Shutdown() {
    SaveConfig();
}

std::wstring NetworkInfoModule::GetValue(const std::wstring& key) const {
    auto it = m_values.find(key);
    return it != m_values.end() ? it->second : L"";
}

void NetworkInfoModule::SetValue(const std::wstring& key, const std::wstring& value) {
    m_values[key] = value;
}

std::vector<ContextMenuItem> NetworkInfoModule::GetContextMenuItems() const {
    if (GetValue(L"Enabled") != L"1") return {};
    auto Tr = TranslationService::Get();
    return { { kMenuNetInfo, Tr ? Tr->Tr(L"NetworkInfo", L"Network Info...") : L"Network Info..." } };
}

void NetworkInfoModule::ExecuteContextMenuItem(int itemId) {
    if (itemId != kMenuNetInfo || !m_host) return;
    NetworkInfoDialog dlg(m_host->GetMainWindow());
    dlg.Show();
}
