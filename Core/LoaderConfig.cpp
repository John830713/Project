#include "LoaderConfig.h"
#include "ConfigManager.h"
#include "DebugConsole.h"

#include <windows.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace {

void SkipWhitespace(const std::string& s, size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
        pos++;
}

std::string ReadQuotedString(const std::string& s, size_t& pos) {
    SkipWhitespace(s, pos);
    if (pos >= s.size() || s[pos] != '"') return {};
    pos++;
    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\') { pos++; if (pos < s.size()) { result += s[pos]; pos++; } }
        else { result += s[pos]; pos++; }
    }
    if (pos < s.size()) pos++;
    return result;
}

// Walk the document one token at a time looking for "key": at object level.
// The previous version tried to skip values in place and could stall on the
// opening brace (pos never advanced), which hung the app on any real config
// file. Every branch here consumes at least one character.
bool FindKey(const std::string& json, const std::string& key, size_t& outPos) {
    size_t pos = 0;
    while (pos < json.size()) {
        if (json[pos] != '"') {
            pos++;
            continue;
        }

        std::string name = ReadQuotedString(json, pos);
        SkipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == ':') {
            pos++;
            SkipWhitespace(json, pos);
            if (name == key) {
                outPos = pos;
                return true;
            }
            continue;
        }
        // A string inside an array (no ':' after it): nothing to do.
    }
    return false;
}

}

std::wstring LoaderConfig::GetExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring dir(buf);
    auto pos = dir.rfind(L'\\');
    if (pos != std::wstring::npos) dir = dir.substr(0, pos);
    return dir;
}

std::wstring LoaderConfig::GetConfigPath(const std::wstring& flavor) {
    std::wstring fileName = flavor.empty()
        ? L"Config_Loader.json"
        : L"Config_Flavor_" + flavor + L".json";
    return ConfigManager::GetConfigDirectory().wstring() + L"\\" + fileName;
}

const wchar_t* LoaderConfig::ModeToKey(Mode mode) {
    switch (mode) {
        case Mode::Tray:   return L"tray";
        case Mode::Window: return L"window";
        case Mode::Pet:
        default:           return L"pet";
    }
}

LoaderConfig::Mode LoaderConfig::ModeFromKey(const std::wstring& key) {
    if (key == L"tray") return Mode::Tray;
    if (key == L"window") return Mode::Window;
    // "clean" was the old placeholder name for a non-pet shell; keep old
    // configs working by mapping it onto the launcher window.
    if (key == L"clean") return Mode::Window;
    return Mode::Pet;
}

std::vector<std::wstring> LoaderConfig::ScanAvailableModules() {
    std::vector<std::wstring> modules;
    std::wstring modulesDir = GetExeDir() + L"\\Modules";
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW((modulesDir + L"\\*").c_str(), &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                std::wstring dirName(ffd.cFileName);
                if (dirName != L"." && dirName != L"..") {
                    std::wstring searchPath = modulesDir + L"\\" + dirName + L"\\*.module.ini";
                    WIN32_FIND_DATAW iniFfd;
                    HANDLE hIni = FindFirstFileW(searchPath.c_str(), &iniFfd);
                    if (hIni != INVALID_HANDLE_VALUE) {
                        modules.push_back(dirName);
                        FindClose(hIni);
                    }
                }
            }
        } while (FindNextFileW(hFind, &ffd));
        FindClose(hFind);
    }
    std::sort(modules.begin(), modules.end());
    return modules;
}

LoaderConfig::Data LoaderConfig::Load(const std::wstring& flavor) {
    Data data;
    std::wstring path = GetConfigPath(flavor);
    std::ifstream file(path.c_str());
    if (!file.is_open()) {
        DBG(L"LoaderConfig: %s not found, using defaults", path.c_str());
        return data;
    }
    std::stringstream buf;
    buf << file.rdbuf();
    std::string json = buf.str();

    size_t valuePos;

    if (FindKey(json, "mode", valuePos)) {
        std::string modeStr = ReadQuotedString(json, valuePos);
        data.mode = ModeFromKey(std::wstring(modeStr.begin(), modeStr.end()));
    }

    if (FindKey(json, "modules", valuePos) && valuePos < json.size() && json[valuePos] == '[') {
        valuePos++;
        while (valuePos < json.size() && json[valuePos] != ']') {
            std::string modName = ReadQuotedString(json, valuePos);
            if (modName.empty()) {
                // Commas and whitespace between entries: consume exactly one
                // character so a malformed file can never spin here.
                valuePos++;
                continue;
            }
            data.enabledModules.insert(std::wstring(modName.begin(), modName.end()));
            SkipWhitespace(json, valuePos);
            if (valuePos < json.size() && json[valuePos] == ',') valuePos++;
        }
    }

    DBG(L"LoaderConfig: mode=%s modules=%zu",
        ModeToKey(data.mode),
        data.enabledModules.size());

    return data;
}

void LoaderConfig::Save(const Data& data, const std::wstring& flavor) {
    std::string content;
    content += "{\n";
    content += "  \"mode\": \"";
    for (const wchar_t* p = ModeToKey(data.mode); *p; ++p) {
        content += static_cast<char>(*p);
    }
    content += "\",\n";
    content += "  \"modules\": [\n";

    auto available = ScanAvailableModules();
    bool first = true;
    for (const auto& mod : available) {
        if (data.enabledModules.empty() || data.enabledModules.count(mod) > 0) {
            if (!first) content += ",\n";
            std::string modName(mod.begin(), mod.end());
            content += "    \"" + modName + "\"";
            first = false;
        }
    }

    content += "\n  ]\n";
    content += "}\n";

    std::wstring path = GetConfigPath(flavor);
    std::ofstream file(path.c_str());
    if (file.is_open()) {
        file << content;
        file.close();
        DBG(L"LoaderConfig: saved to %s", path.c_str());
    } else {
        DBG(L"LoaderConfig: FAILED to save to %s", path.c_str());
    }
}
