#include "TranslationService.h"
#include <windows.h>

std::wstring TranslationService::FindTranslationDir() const {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring dir = buf;
    auto pos = dir.rfind(L'\\');
    if (pos != std::wstring::npos) dir = dir.substr(0, pos);
    return dir + L"\\Translation";
}

void TranslationService::LoadFile(const std::wstring& path) {
    wchar_t sections[4096] = {};
    GetPrivateProfileStringW(nullptr, nullptr, L"", sections, 4096, path.c_str());
    for (const wchar_t* s = sections; *s; s += wcslen(s) + 1) {
        std::wstring section(s);
        wchar_t keys[32768] = {};
        GetPrivateProfileStringW(section.c_str(), nullptr, L"", keys, 32768, path.c_str());
        auto& sectionMap = m_strings[section];
        for (const wchar_t* k = keys; *k; k += wcslen(k) + 1) {
            std::wstring key(k);
            wchar_t value[4096] = {};
            GetPrivateProfileStringW(section.c_str(), key.c_str(), L"", value, 4096, path.c_str());
            sectionMap[key] = value;
        }
    }
}

TranslationService* TranslationService::Get() {
    static TranslationService instance;
    return &instance;
}

bool TranslationService::Load(const std::wstring& langCode) {
    m_strings.clear();
    m_currentLang = langCode;
    if (langCode == L"en") return true;

    std::wstring path = FindTranslationDir() + L"\\" + langCode + L".ini";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        return false;

    LoadFile(path);
    return true;
}

bool TranslationService::SetLanguage(const std::wstring& langCode) {
    return Load(langCode);
}

std::wstring TranslationService::Tr(const std::wstring& section, const std::wstring& text) const {
    if (m_currentLang == L"en") return text;
    auto secIt = m_strings.find(section);
    if (secIt == m_strings.end()) return text;
    auto keyIt = secIt->second.find(text);
    if (keyIt == secIt->second.end()) return text;
    return keyIt->second;
}

std::vector<std::wstring> TranslationService::GetAvailableLanguages() const {
    std::vector<std::wstring> langs;
    langs.push_back(L"en");

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW((FindTranslationDir() + L"\\*.ini").c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return langs;

    do {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring name(ffd.cFileName);
            auto dot = name.rfind(L'.');
            if (dot != std::wstring::npos) {
                std::wstring stem = name.substr(0, dot);
                if (stem != L"en")
                    langs.push_back(stem);
            }
        }
    } while (FindNextFileW(hFind, &ffd));
    FindClose(hFind);
    return langs;
}
