#pragma once

#include <string>
#include <vector>
#include "../Core/ConfigTypes.h"

class PetConfig {
public:
    struct Data {
        int posX = -1;
        int posY = -1;
        int opacity = 255;
        int scalePercent = 100;
        bool alwaysOnTop = true;
        bool moveEnabled = false;
        int moveStep = 3;
        int moveSpeed = 200;
        bool moveShuttle = false;
        std::wstring language = L"en";
    };

    static Data Load();
    static void Save(const Data& data);
    static std::vector<ConfigFieldDefinition> GetDefinitions();
};
