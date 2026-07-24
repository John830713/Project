//==============================================================================
// Release_PackageModule.cpp - Release_Package module implementation
//==============================================================================

#include "Release_PackageModule.h"
#include "../../Core/ConfigManager.h"
#include "../../Core/Logger.h"
#include "../../Services/TranslationService.h"
#include "../../Core/DebugConsole.h"

#include "json.hpp"
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

Release_PackageModule::Release_PackageModule() {
    InitializeDefinitions();
    InitializeDefaults();
}

void Release_PackageModule::InitializeDefinitions() {
    m_definitions = {
        { L"7zPath", L"7z.exe Path", ConfigValueType::String, L"C:\\Program Files\\7-Zip\\7z.exe", 0, 0 },
        { L"DefaultSourceDir", L"Default Source Dir", ConfigValueType::String, L"", 0, 0 },
        { L"OutputDir", L"Output Base Dir", ConfigValueType::String, L"", 0, 0 },
    };
}

void Release_PackageModule::InitializeDefaults() {
    for (auto& def : m_definitions) {
        m_values[def.key] = def.defaultValue;
    }
}

//==============================================================================
// Lifecycle
//==============================================================================

void Release_PackageModule::Initialize(HINSTANCE hInst, IHostContext* host) {
    m_host = host;
}

void Release_PackageModule::LoadConfig() {
    m_values = ConfigManager::LoadModuleConfig(
        GetConfigFileName(), GetConfigSectionName(), m_definitions);
}

void Release_PackageModule::SaveConfig() {
    ConfigManager::SaveModuleConfig(
        GetConfigFileName(), GetConfigSectionName(), m_values);
}

void Release_PackageModule::ApplyConfig() {
}

void Release_PackageModule::Shutdown() {
    SaveConfig();
}

//==============================================================================
// Configuration
//==============================================================================

const std::vector<ConfigFieldDefinition>& Release_PackageModule::GetConfigDefinitions() const {
    return m_definitions;
}

std::wstring Release_PackageModule::GetValue(const std::wstring& key) const {
    auto it = m_values.find(key);
    return (it != m_values.end()) ? it->second : L"";
}

void Release_PackageModule::SetValue(const std::wstring& key, const std::wstring& value) {
    m_values[key] = value;
}

//==============================================================================
// Public accessors
//==============================================================================

std::wstring Release_PackageModule::Get7zPath() const {
    return GetValue(L"7zPath");
}

std::wstring Release_PackageModule::GetDefaultSourceDir() const {
    return GetValue(L"DefaultSourceDir");
}

std::wstring Release_PackageModule::GetOutputDir() const {
    return GetValue(L"OutputDir");
}

//==============================================================================
// Context menu
//==============================================================================

std::vector<ContextMenuItem> Release_PackageModule::GetContextMenuItems() const {
    std::vector<ContextMenuItem> items;
    items.push_back({ 100, TranslationService::Get()->Tr(L"Release_Package", L"Open Extractor...") });
    return items;
}

void Release_PackageModule::ExecuteContextMenuItem(int itemId) {
    if (itemId == 100 && m_host) {
        extern void ShowReleasePackageWindow(HWND parent, Release_PackageModule* mod);
        ShowReleasePackageWindow(m_host->GetMainWindow(), this);
    }
}

//==============================================================================
// JSON Load/Save
//==============================================================================

static std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

void Release_PackageModule::LoadRulesFromJson(const std::wstring& path) {
    m_params.clear();
    m_rules.clear();
    std::ifstream f(path.c_str());
    if (!f.is_open()) return;
    json data;
    try { f >> data; } catch (...) { return; }
    for (auto& p : data.value("params", std::vector<json>())) {
        RPParam param;
        param.name = FromUtf8(p.value("name", ""));
        param.value = FromUtf8(p.value("value", ""));
        param.regex = FromUtf8(p.value("regex", ""));
        param.example = FromUtf8(p.value("example", ""));
        if (!param.name.empty()) m_params.push_back(param);
    }
    for (auto& r : data.value("rules", std::vector<json>())) {
        RPRule rule;
        rule.enabled = r.value("enabled", true);
        rule.sourceDir = FromUtf8(r.value("source_dir", ""));
        rule.pattern = FromUtf8(r.value("pattern", "*"));
        rule.destDir = FromUtf8(r.value("dest_dir", ""));
        rule.rename = FromUtf8(r.value("rename", "{filename}{ext}"));
        if (r.contains("use_default_src")) rule.useDefaultSrc = r["use_default_src"].get<bool>();
        else rule.useDefaultSrc = rule.sourceDir.empty();
        if (r.contains("use_default_dest")) rule.useDefaultDest = r["use_default_dest"].get<bool>();
        else rule.useDefaultDest = rule.destDir.empty();
        m_rules.push_back(rule);
    }
}

void Release_PackageModule::SaveRulesToJson(const std::wstring& path) {
    json data;
    json jp = json::array();
    for (auto& p : m_params) {
        json j;
        j["name"] = ToUtf8(p.name);
        if (!p.value.empty()) j["value"] = ToUtf8(p.value);
        if (!p.regex.empty()) j["regex"] = ToUtf8(p.regex);
        if (!p.example.empty()) j["example"] = ToUtf8(p.example);
        jp.push_back(j);
    }
    data["params"] = jp;
    json jr = json::array();
    for (auto& r : m_rules) {
        json j;
        j["enabled"] = r.enabled;
        if (!r.useDefaultSrc) { j["use_default_src"] = false; j["source_dir"] = ToUtf8(r.sourceDir); }
        j["pattern"] = ToUtf8(r.pattern);
        if (!r.useDefaultDest) { j["use_default_dest"] = false; j["dest_dir"] = ToUtf8(r.destDir); }
        j["rename"] = ToUtf8(r.rename);
        jr.push_back(j);
    }
    data["rules"] = jr;
    std::ofstream f(path.c_str());
    f << data.dump(2) << std::endl;
}