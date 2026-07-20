#pragma once

#include "../../Core/IFeatureModule.h"

#include <map>
#include <string>
#include <vector>

class NetworkInfoModule : public IFeatureModule {
public:
    NetworkInfoModule();
    ~NetworkInfoModule() override;

    const wchar_t* GetModuleName() const override { return L"NetworkInfo"; }
    const wchar_t* GetDisplayName() const override { return L"Network Info"; }
    const wchar_t* GetVersion() const override { return L"1.0.0"; }
    const wchar_t* GetPurpose() const override { return L"Displays network adapter information and IP addresses."; }
    const wchar_t* GetConfigFileName() const override { return L"Config_NetworkInfo.ini"; }
    const wchar_t* GetConfigSectionName() const override { return L"NetworkInfo"; }
    const wchar_t* GetLogSourceName() const override { return L"NetworkInfo"; }

    const std::vector<ConfigFieldDefinition>& GetConfigDefinitions() const override;

    void Initialize(HINSTANCE hInst, IHostContext* host) override;
    void LoadConfig() override;
    void SaveConfig() override;
    void ApplyConfig() override;
    void Shutdown() override;

    std::wstring GetValue(const std::wstring& key) const override;
    void SetValue(const std::wstring& key, const std::wstring& value) override;

    std::vector<ContextMenuItem> GetContextMenuItems() const override;
    void ExecuteContextMenuItem(int itemId) override;

private:
    HINSTANCE m_hInstance;
    IHostContext* m_host;
    std::vector<ConfigFieldDefinition> m_definitions;
    std::map<std::wstring, std::wstring> m_values;
};
