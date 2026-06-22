#include "PetConfig.h"
#include "../Core/ConfigManager.h"
#include <windows.h>

PetConfig::Data PetConfig::Load() {
    Data data;
    auto values = ConfigManager::LoadModuleConfig(L"Config_Pet.ini", L"Pet", GetDefinitions());

    data.posX = std::stoi(values[L"posX"]);
    data.posY = std::stoi(values[L"posY"]);
    data.opacity = std::stoi(values[L"opacity"]);
    data.alwaysOnTop = values[L"alwaysOnTop"] == L"1";
    data.moveEnabled = values[L"moveEnabled"] == L"1";
    data.moveStep = std::stoi(values[L"moveStep"]);
    data.moveSpeed = std::stoi(values[L"moveSpeed"]);
    data.moveShuttle = values[L"moveShuttle"] == L"1";
    data.language = values[L"language"];
    return data;
}

void PetConfig::Save(const Data& data) {
    std::map<std::wstring, std::wstring> values;
    values[L"posX"] = std::to_wstring(data.posX);
    values[L"posY"] = std::to_wstring(data.posY);
    values[L"opacity"] = std::to_wstring(data.opacity);
    values[L"alwaysOnTop"] = data.alwaysOnTop ? L"1" : L"0";
    values[L"moveEnabled"] = data.moveEnabled ? L"1" : L"0";
    values[L"moveStep"] = std::to_wstring(data.moveStep);
    values[L"moveSpeed"] = std::to_wstring(data.moveSpeed);
    values[L"moveShuttle"] = data.moveShuttle ? L"1" : L"0";
    values[L"language"] = data.language;
    ConfigManager::SaveModuleConfig(L"Config_Pet.ini", L"Pet", values);
}

std::vector<ConfigFieldDefinition> PetConfig::GetDefinitions() {
    return {
        { L"posX", L"Position X", ConfigValueType::Int, L"-1", -1, 9999 },
        { L"posY", L"Position Y", ConfigValueType::Int, L"-1", -1, 9999 },
        { L"opacity", L"Opacity", ConfigValueType::Int, L"255", 0, 255 },
        { L"alwaysOnTop", L"Always On Top", ConfigValueType::Bool, L"1", 0, 0 },
        { L"moveEnabled", L"Move Enabled", ConfigValueType::Bool, L"0", 0, 0 },
        { L"moveStep", L"Move Step (px)", ConfigValueType::Int, L"3", 1, 50 },
        { L"moveSpeed", L"Move Speed (ms)", ConfigValueType::Int, L"200", 10, 1000 },
        { L"moveShuttle", L"Move Shuttle", ConfigValueType::Bool, L"0", 0, 0 },
        { L"language", L"Language", ConfigValueType::String, L"en", 0, 0 },
    };
}
