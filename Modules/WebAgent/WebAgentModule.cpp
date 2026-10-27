//==============================================================================
// WebAgentModule.cpp - Web Agent feature module implementation
//==============================================================================

#include "WebAgentModule.h"

#include "../../Core/ConfigManager.h"
#include "../../Core/Logger.h"
#include "../../Services/TranslationService.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>

WebAgentModule::WebAgentModule() {
    InitializeDefinitions();
    InitializeDefaults();
    Logger::WriteModuleLog(GetLogSourceName(), L"WebAgentModule constructed.");
}

WebAgentModule::~WebAgentModule() {
    StopServer();
    Logger::WriteModuleLog(GetLogSourceName(), L"WebAgentModule destructed.");
}

void WebAgentModule::InitializeDefinitions() {
    m_definitions = {
        { L"Enabled",      L"Enable Web Agent",        ConfigValueType::Bool,   L"1",              0, 1 },
        { L"AutoStart",    L"Start server on launch",  ConfigValueType::Bool,   L"1",              0, 1 },
        { L"Port",         L"Listen port",             ConfigValueType::Int,    L"8921",           1, 65535 },
        { L"BindHost",     L"Bind host",               ConfigValueType::String, L"0.0.0.0",        0, 0 },
        { L"ShowConsole",  L"Show console window",     ConfigValueType::Bool,   L"0",              0, 1 },
        { L"ResourceRoot", L"JS resource root",        ConfigValueType::Directory, L"D:\\Tampermonkey", 0, 0 },
    };
}

void WebAgentModule::InitializeDefaults() {
    for (const auto& def : m_definitions) {
        m_values[def.key] = def.defaultValue;
    }
}

void WebAgentModule::Initialize(HINSTANCE hInst, IHostContext* host) {
    (void)hInst;
    m_host = host;
    Logger::WriteModuleLog(GetLogSourceName(), L"WebAgentModule Initialized.");
}

void WebAgentModule::LoadConfig() {
    m_values = ConfigManager::LoadModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        m_definitions
    );
}

void WebAgentModule::SaveConfig() {
    ConfigManager::SaveModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        m_values
    );
}

void WebAgentModule::ApplyConfig() {
    // ApplyConfig runs once at startup and again after the settings dialog.
    // Restart the server so a changed port/host/root takes effect. Keep a
    // manually-started server alive even if AutoStart is off.
    bool wasRunning = ServerRunning();
    StopServer();
    if (!GetBoolValue(L"Enabled", true)) {
        return;
    }
    if (wasRunning || GetBoolValue(L"AutoStart", true)) {
        StartServer();
    }
}

//==============================================================================
// Configuration accessors
//==============================================================================

const std::vector<ConfigFieldDefinition>& WebAgentModule::GetConfigDefinitions() const {
    return m_definitions;
}

std::wstring WebAgentModule::GetValue(const std::wstring& key) const {
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        return it->second;
    }
    return L"";
}

void WebAgentModule::SetValue(const std::wstring& key, const std::wstring& value) {
    m_values[key] = value;
}

bool WebAgentModule::GetBoolValue(const std::wstring& key, bool defaultValue) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) {
        return defaultValue;
    }
    return it->second == L"1" || it->second == L"true" || it->second == L"TRUE";
}

int WebAgentModule::GetIntValue(const std::wstring& key, int defaultValue) const {
    auto it = m_values.find(key);
    if (it == m_values.end() || it->second.empty()) {
        return defaultValue;
    }
    try {
        return std::stoi(it->second);
    } catch (...) {
        return defaultValue;
    }
}

//==============================================================================
// Server control
//==============================================================================

bool WebAgentModule::ResourceRootAvailable() const {
    std::error_code ec;
    const std::filesystem::path root(GetValue(L"ResourceRoot"));
    return !root.empty() && std::filesystem::is_directory(root, ec);
}

bool WebAgentModule::StartServer() {
    if (!GetBoolValue(L"Enabled", true)) {
        Logger::WriteModuleLog(GetLogSourceName(), L"StartServer skipped: module disabled.");
        return false;
    }
    if (!ResourceRootAvailable()) {
        Logger::WriteModuleLog(GetLogSourceName(),
            L"StartServer skipped: resource root not found (" + GetValue(L"ResourceRoot") + L").");
        return false;
    }
    if (ServerRunning()) {
        return true;
    }

    m_api = std::make_unique<webagent::WebAgentApi>();

    // Convert wide config values to narrow for the networking layer.
    std::wstring hostW = GetValue(L"BindHost");
    std::string host(hostW.begin(), hostW.end());
    std::wstring rootW = GetValue(L"ResourceRoot");
    std::string root(rootW.begin(), rootW.end());
    int port = GetIntValue(L"Port", webagent::WebAgentApi::kDefaultPort);

    m_api->SetResourceRoot(root);
    m_api->SetAgentDir(root + "/.agent");
    m_api->SetShowConsole(GetBoolValue(L"ShowConsole", false));

    m_server = std::make_unique<webagent::HttpServer>();
    m_api->RegisterRoutes(*m_server);

    if (!m_server->Start(host, port)) {
        Logger::WriteModuleLog(GetLogSourceName(),
            L"StartServer failed to bind " + hostW + L":" + std::to_wstring(port) + L".");
        m_server.reset();
        m_api.reset();
        return false;
    }

    Logger::WriteModuleLog(GetLogSourceName(),
        L"Web Agent server started on " + hostW + L":" + std::to_wstring(port) +
        L" (root=" + rootW + L").");
    return true;
}

void WebAgentModule::StopServer() {
    if (m_server) {
        m_server->Stop();
        m_server.reset();
    }
    m_api.reset();
}

//==============================================================================
// Context menu
//==============================================================================

std::vector<ContextMenuItem> WebAgentModule::GetContextMenuItems() const {
    // No resource root means the browser-side scripts were never installed on
    // this machine: hide the whole group instead of offering a dead switch.
    if (!GetBoolValue(L"Enabled", true) || !ResourceRootAvailable()) {
        return {};
    }
    std::vector<ContextMenuItem> items;
    ContextMenuItem toggle;
    toggle.itemId = kWebAgentToggleServer;
    toggle.label = ServerRunning()
        ? TranslationService::Get()->Tr(L"WebAgent", L"Stop Web Agent")
        : TranslationService::Get()->Tr(L"WebAgent", L"Start Web Agent");
    items.push_back(toggle);

    ContextMenuItem restart;
    restart.itemId = kWebAgentRestartServer;
    restart.label = TranslationService::Get()->Tr(L"WebAgent", L"Restart Web Agent");
    if (!ServerRunning()) {
        restart.flags |= kContextMenuItemDisabled;
    }
    items.push_back(restart);

    ContextMenuItem dash;
    dash.itemId = kWebAgentOpenDashboard;
    dash.label = TranslationService::Get()->Tr(L"WebAgent", L"Open Dashboard");
    if (!ServerRunning()) {
        dash.flags |= kContextMenuItemDisabled;
    }
    items.push_back(dash);
    return items;
}

void WebAgentModule::ExecuteContextMenuItem(int itemId) {
    if (itemId == kWebAgentToggleServer) {
        if (ServerRunning()) {
            StopServer();
            Logger::WriteModuleLog(GetLogSourceName(), L"Web Agent stopped via menu.");
        } else {
            StartServer();
        }
        return;
    }

    if (itemId == kWebAgentRestartServer) {
        StopServer();
        if (StartServer()) {
            Logger::WriteModuleLog(GetLogSourceName(), L"Web Agent restarted via menu.");
        }
        return;
    }

    if (itemId == kWebAgentOpenDashboard && ServerRunning() && m_server) {
        std::wstring url = L"http://localhost:" + std::to_wstring(m_server->GetPort()) + L"/dashboard";
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

//==============================================================================
// Shutdown
//==============================================================================

void WebAgentModule::Shutdown() {
    StopServer();
    SaveConfig();
}
