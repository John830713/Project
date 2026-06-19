#include "PetConfig.h"
#include <windows.h>

static std::wstring GetFilePath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir = exePath;
    auto pos = dir.rfind(L'\\');
    if (pos != std::wstring::npos) dir = dir.substr(0, pos);
    return dir + L"\\Config\\Config_Pet.ini";
}

PetConfig::Data PetConfig::Load() {
    Data data;
    auto path = GetFilePath();
    data.posX = GetPrivateProfileIntW(L"Pet", L"posX", -1, path.c_str());
    data.posY = GetPrivateProfileIntW(L"Pet", L"posY", -1, path.c_str());
    data.opacity = static_cast<int>(GetPrivateProfileIntW(L"Pet", L"opacity", 255, path.c_str()));
    data.alwaysOnTop = GetPrivateProfileIntW(L"Pet", L"alwaysOnTop", 1, path.c_str()) != 0;
    data.moveEnabled = GetPrivateProfileIntW(L"Pet", L"moveEnabled", 0, path.c_str()) != 0;
    data.moveStep = static_cast<int>(GetPrivateProfileIntW(L"Pet", L"moveStep", 3, path.c_str()));
    data.moveSpeed = static_cast<int>(GetPrivateProfileIntW(L"Pet", L"moveSpeed", 200, path.c_str()));
    data.moveShuttle = GetPrivateProfileIntW(L"Pet", L"moveShuttle", 0, path.c_str()) != 0;
    wchar_t langBuf[32] = {};
    GetPrivateProfileStringW(L"Pet", L"language", L"en", langBuf, 32, path.c_str());
    data.language = langBuf;
    return data;
}

void PetConfig::Save(const Data& data) {
    auto path = GetFilePath();
    wchar_t buf[32];
    swprintf(buf, 32, L"%d", data.posX);
    WritePrivateProfileStringW(L"Pet", L"posX", buf, path.c_str());
    swprintf(buf, 32, L"%d", data.posY);
    WritePrivateProfileStringW(L"Pet", L"posY", buf, path.c_str());
    swprintf(buf, 32, L"%d", data.opacity);
    WritePrivateProfileStringW(L"Pet", L"opacity", buf, path.c_str());
    WritePrivateProfileStringW(L"Pet", L"alwaysOnTop", data.alwaysOnTop ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Pet", L"moveEnabled", data.moveEnabled ? L"1" : L"0", path.c_str());
    swprintf(buf, 32, L"%d", data.moveStep);
    WritePrivateProfileStringW(L"Pet", L"moveStep", buf, path.c_str());
    swprintf(buf, 32, L"%d", data.moveSpeed);
    WritePrivateProfileStringW(L"Pet", L"moveSpeed", buf, path.c_str());
    WritePrivateProfileStringW(L"Pet", L"moveShuttle", data.moveShuttle ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Pet", L"language", data.language.c_str(), path.c_str());
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
