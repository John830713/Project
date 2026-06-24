#include "TranslationService.h"
#include "../Core/DebugConsole.h"
#include <windows.h>
#include <algorithm>
#include <cstdint>

const wchar_t* const TranslationService::kEmbeddedLangs[] = { L"zh-TW" };
const int TranslationService::kEmbeddedLangCount = 1;

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

void TranslationService::ParseIniFromWide(const wchar_t* data, size_t charLen) {
    std::wstring currentSection;
    size_t pos = 0;
    while (pos < charLen) {
        size_t eol = pos;
        while (eol < charLen && data[eol] != L'\n') eol++;
        size_t lineLen = eol - pos;
        if (lineLen > 0 && data[pos + lineLen - 1] == L'\r') lineLen--;

        const wchar_t* line = data + pos;
        size_t len = lineLen;
        pos = eol + 1;

        if (len == 0) continue;

        if (line[0] == L'[') {
            const wchar_t* close = wmemchr(line, L']', len);
            if (close) {
                currentSection.assign(line + 1, close - line - 1);
            }
        } else {
            const wchar_t* eq = wmemchr(line, L'=', len);
            if (eq && !currentSection.empty()) {
                std::wstring key(line, eq - line);
                std::wstring val(eq + 1, len - (eq - line) - 1);
                m_strings[currentSection][key] = val;
            }
        }
    }
}

bool TranslationService::LoadFromResource(const std::wstring& langCode) {
    // Map lang code to resource name: zh-TW -> ZH_TW
    std::wstring resName;
    for (wchar_t c : langCode) {
        if (c == L'-') resName += L'_';
        else if (c >= L'a' && c <= L'z') resName += static_cast<wchar_t>(c & ~0x20);
        else resName += c;
    }

    HRSRC hRes = FindResourceW(nullptr, resName.c_str(), RT_RCDATA);
    if (!hRes) return false;

    HGLOBAL hGlob = LoadResource(nullptr, hRes);
    if (!hGlob) return false;

    const uint8_t* bytes = static_cast<const uint8_t*>(LockResource(hGlob));
    DWORD sz = SizeofResource(nullptr, hRes);
    if (!bytes || sz < 2) return false;

    // Expect UTF-16LE BOM (0xFF 0xFE)
    if (bytes[0] == 0xFF && bytes[1] == 0xFE) {
        const wchar_t* wide = reinterpret_cast<const wchar_t*>(bytes + 2);
        size_t charLen = (sz - 2) / sizeof(wchar_t);
        ParseIniFromWide(wide, charLen);
        return true;
    }

    // Fallback: treat as UTF-8
    int wideLen = MultiByteToWideChar(CP_UTF8, 0,
        reinterpret_cast<const char*>(bytes), static_cast<int>(sz),
        nullptr, 0);
    if (wideLen <= 0) return false;
    std::wstring buf(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
        reinterpret_cast<const char*>(bytes), static_cast<int>(sz),
        &buf[0], wideLen);
    ParseIniFromWide(buf.data(), buf.size());
    return true;
}

TranslationService* TranslationService::Get() {
    static TranslationService instance;
    return &instance;
}

bool TranslationService::Load(const std::wstring& langCode) {
    m_strings.clear();
    m_currentLang = langCode;
    DBG(L"TranslationService::Load lang=%s", langCode.c_str());
    if (langCode == L"en") { DBG(L"  -> en, returning early"); return true; }

    bool ok = LoadFromResource(langCode);
    DBG(L"  -> LoadFromResource returned %d, sectionCount=%zu", ok ? 1 : 0, m_strings.size());
    for (auto& kv : m_strings) {
        DBG(L"  section [%s] has %zu keys", kv.first.c_str(), kv.second.size());
    }
    return ok;
}

bool TranslationService::SetLanguage(const std::wstring& langCode) {
    return Load(langCode);
}

std::wstring TranslationService::Tr(const std::wstring& section, const std::wstring& text) const {
    DBG(L"Tr(section='%s', key='%s') currentLang='%s'", section.c_str(), text.c_str(), m_currentLang.c_str());
    if (m_currentLang == L"en") {
        DBG(L"  -> lang=en, return key unchanged");
        return text;
    }
    auto secIt = m_strings.find(section);
    if (secIt == m_strings.end()) {
        DBG(L"  -> section '%s' NOT FOUND", section.c_str());
        return text;
    }
    auto keyIt = secIt->second.find(text);
    if (keyIt == secIt->second.end()) {
        DBG(L"  -> key '%s' NOT FOUND in section '%s'", text.c_str(), section.c_str());
        return text;
    }
    DBG(L"  -> found: '%s'", keyIt->second.c_str());
    return keyIt->second;
}

std::vector<std::wstring> TranslationService::GetAvailableLanguages() const {
    std::vector<std::wstring> langs;
    langs.push_back(L"en");

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW((FindTranslationDir() + L"\\*.ini").c_str(), &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
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
    }

    for (int i = 0; i < kEmbeddedLangCount; i++) {
        if (std::find(langs.begin(), langs.end(), kEmbeddedLangs[i]) == langs.end())
            langs.push_back(kEmbeddedLangs[i]);
    }

    return langs;
}
