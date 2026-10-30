#pragma once

#include <string>
#include <vector>
#include <set>

class LoaderConfig {
public:
    //--- Carrier (which shell hosts the selected modules) ---
    // Pet    : the pet window + tray (the original full app).
    // Tray   : tray icon only, pet window stays hidden.
    // Window : a plain module launcher window, pet window stays hidden.
    enum class Mode { Pet, Tray, Window };

    struct Data {
        Mode mode = Mode::Pet;
        std::set<std::wstring> enabledModules;
    };

    // A flavour is one saved selection: Config_Flavor_<name>.json. The empty
    // name means the default Config_Loader.json, so existing builds keep
    // working and only --flavor users get a second file.
    static Data Load(const std::wstring& flavor = L"");
    static void Save(const Data& data, const std::wstring& flavor = L"");
    static std::vector<std::wstring> ScanAvailableModules();

    static const wchar_t* ModeToKey(Mode mode);
    static Mode ModeFromKey(const std::wstring& key);

private:
    static std::wstring GetConfigPath(const std::wstring& flavor = L"");
    static std::wstring GetExeDir();
};
