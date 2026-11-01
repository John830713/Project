#include "PetConfig.h"
#include "../Core/AutoStart.h"
#include "../Core/ConfigManager.h"
#include <windows.h>

PetConfig::Data PetConfig::Load() {
    Data data;
    auto values = ConfigManager::LoadModuleConfig(L"Config_Pet.ini", L"Pet", GetDefinitions());

    data.posX = std::stoi(values[L"posX"]);
    data.posY = std::stoi(values[L"posY"]);
    int op = std::stoi(values[L"opacity"]);
    if (op < 0) op = 0;
    if (op > 255) op = 255;
    data.opacity = op;
    int sc = std::stoi(values[L"scalePercent"]);
    if (sc < 25) sc = 25;
    if (sc > 250) sc = 250;
    data.scalePercent = sc;
    data.alwaysOnTop = values[L"alwaysOnTop"] == L"1";
    data.moveEnabled = values[L"moveEnabled"] == L"1";
    data.moveStep = std::stoi(values[L"moveStep"]);
    data.moveSpeed = std::stoi(values[L"moveSpeed"]);
    data.moveShuttle = values[L"moveShuttle"] == L"1";
    data.language = values[L"language"];

    // snapEdge is window state written by the snap gesture, not something the
    // user picks in the settings dialog, so it stays out of GetDefinitions()
    // (which drives that dialog) and is read straight from the file.
    data.snapEdge = GetPrivateProfileIntW(
        L"Pet", L"snapEdge", 0,
        ConfigManager::GetModuleConfigPath(L"Config_Pet.ini").c_str());
    if (data.snapEdge < 0) data.snapEdge = 0;
    if (data.snapEdge > 4) data.snapEdge = 4;

    // Windows starts the app from the Run entry, so that entry - not the INI
    // copy - decides whether the checkbox is ticked. Deleting it from Task
    // Manager therefore turns the setting off instead of lying about it.
    data.autoStart = AutoStart::IsEnabled();

    return data;
}

void PetConfig::Save(const Data& data) {
    std::map<std::wstring, std::wstring> values;
    values[L"posX"] = std::to_wstring(data.posX);
    values[L"posY"] = std::to_wstring(data.posY);
    values[L"opacity"] = std::to_wstring(data.opacity);
    values[L"scalePercent"] = std::to_wstring(data.scalePercent);
    values[L"alwaysOnTop"] = data.alwaysOnTop ? L"1" : L"0";
    values[L"moveEnabled"] = data.moveEnabled ? L"1" : L"0";
    values[L"moveStep"] = std::to_wstring(data.moveStep);
    values[L"moveSpeed"] = std::to_wstring(data.moveSpeed);
    values[L"moveShuttle"] = data.moveShuttle ? L"1" : L"0";
    values[L"snapEdge"] = std::to_wstring(data.snapEdge);
    values[L"autoStart"] = data.autoStart ? L"1" : L"0";
    values[L"language"] = data.language;
    ConfigManager::SaveModuleConfig(L"Config_Pet.ini", L"Pet", values);

    // Mirror the flag into the registry, which is what actually makes Windows
    // launch the app. No-op when the entry already matches.
    AutoStart::SetEnabled(data.autoStart);
}

std::vector<ConfigFieldDefinition> PetConfig::GetDefinitions() {
    return {
        { L"posX", L"Position X", ConfigValueType::Int, L"-1", -1, 9999 },
        { L"posY", L"Position Y", ConfigValueType::Int, L"-1", -1, 9999 },
        { L"opacity", L"Opacity", ConfigValueType::Int, L"255", 0, 255 },
        { L"scalePercent", L"Scale", ConfigValueType::Int, L"100", 25, 250 },
        { L"alwaysOnTop", L"Always On Top", ConfigValueType::Bool, L"1", 0, 0 },
        { L"moveEnabled", L"Move Enabled", ConfigValueType::Bool, L"0", 0, 0 },
        { L"moveStep", L"Move Step (px)", ConfigValueType::Int, L"3", 1, 50 },
        { L"moveSpeed", L"Move Speed (ms)", ConfigValueType::Int, L"200", 10, 1000 },
        { L"moveShuttle", L"Move Shuttle", ConfigValueType::Bool, L"0", 0, 0 },
        { L"language", L"Language", ConfigValueType::String, L"en", 0, 0 },
        { L"autoStart", L"Start with Windows", ConfigValueType::Bool, L"0", 0, 0 },
    };
}
