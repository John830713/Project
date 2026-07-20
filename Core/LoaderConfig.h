#pragma once

#include <string>
#include <vector>
#include <set>

class LoaderConfig {
public:
    enum class Mode { Pet, Clean };

    struct Data {
        Mode mode = Mode::Pet;
        std::set<std::wstring> enabledModules;
    };

    static Data Load();
    static void Save(const Data& data);
    static std::vector<std::wstring> ScanAvailableModules();

private:
    static std::wstring GetConfigPath();
    static std::wstring GetExeDir();
};
