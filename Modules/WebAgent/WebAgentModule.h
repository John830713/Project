//==============================================================================
// WebAgentModule.h - Web Agent feature module (Tampermonkey bridge)
// Purpose:   Implements IFeatureModule. Owns the HttpServer + WebAgentApi and
//            exposes the browser-side Web Agent protocol (command queue,
//            sessions, static JS serving) as a first-class module. Design goal:
//            100% protocol compatibility with the original Python Flask server.
//==============================================================================

#pragma once

// Pull in the networking header first so winsock2.h precedes windows.h (which
// IFeatureModule.h drags in). Skipping this emits the winsock2 ordering warning.
#include "HttpServer.h"
#include "WebAgentApi.h"

#include "../../Core/IFeatureModule.h"
#include "../../Core/ConfigTypes.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class WebAgentModule : public IFeatureModule {
public:
    WebAgentModule();
    ~WebAgentModule() override;

    //--- Lifecycle ---
    void Initialize(HINSTANCE hInst, IHostContext* host) override;
    void LoadConfig() override;
    void SaveConfig() override;
    void ApplyConfig() override;
    void Shutdown() override;

    //--- Metadata ---
    const wchar_t* GetModuleName() const override { return L"WebAgent"; }
    const wchar_t* GetDisplayName() const override { return L"Tampermonkey Web Agent"; }
    const wchar_t* GetVersion() const override { return L"0.1.0"; }
    const wchar_t* GetPurpose() const override {
        return L"Serves the browser Web Agent protocol and JS modules to Tampermonkey.";
    }
    const wchar_t* GetConfigFileName() const override { return L"Config_WebAgent.ini"; }
    const wchar_t* GetConfigSectionName() const override { return L"WebAgent"; }
    const wchar_t* GetLogSourceName() const override { return L"WebAgent"; }

    //--- Configuration ---
    const std::vector<ConfigFieldDefinition>& GetConfigDefinitions() const override;
    std::wstring GetValue(const std::wstring& key) const override;
    void SetValue(const std::wstring& key, const std::wstring& value) override;

    //--- Availability ---
    // No ResourceRoot means the JS tree was never deployed here: hide the
    // module from menus and settings entirely instead of offering controls
    // that cannot work. Machines that do have the tree are unaffected.
    bool IsAvailable() const override { return ResourceRootAvailable(); }

    //--- Context menu (toggle server on/off) ---
    std::vector<ContextMenuItem> GetContextMenuItems() const override;
    void ExecuteContextMenuItem(int itemId) override;

private:
    void InitializeDefinitions();
    void InitializeDefaults();
    bool GetBoolValue(const std::wstring& key, bool defaultValue) const;
    int GetIntValue(const std::wstring& key, int defaultValue) const;

    bool StartServer();
    void StopServer();
    bool ServerRunning() const { return m_server && m_server->IsRunning(); }

    // Machines that never installed the browser-side scripts have no resource
    // root. They get no Web Agent menu entry and no listener.
    bool ResourceRootAvailable() const;

    IHostContext* m_host = nullptr;
    std::vector<ConfigFieldDefinition> m_definitions;
    std::map<std::wstring, std::wstring> m_values;

    std::unique_ptr<webagent::HttpServer> m_server;
    std::unique_ptr<webagent::WebAgentApi> m_api;
};

// Context menu command IDs (local to this module).
constexpr int kWebAgentToggleServer = 1;
constexpr int kWebAgentOpenDashboard = 2;
constexpr int kWebAgentRestartServer = 3;
