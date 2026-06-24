#pragma once

#include <string>
#include <vector>
#include <map>

class TranslationService {
public:
    static TranslationService* Get();

    bool Load(const std::wstring& langCode);
    std::wstring Tr(const std::wstring& section, const std::wstring& text) const;
    std::vector<std::wstring> GetAvailableLanguages() const;
    std::wstring GetCurrentLanguage() const { return m_currentLang; }
    bool SetLanguage(const std::wstring& langCode);

private:
    TranslationService() = default;
    std::wstring FindTranslationDir() const;
    void LoadFile(const std::wstring& path);
    bool LoadFromResource(const std::wstring& langCode);
    void ParseIniFromWide(const wchar_t* data, size_t charLen);

    static const wchar_t* const kEmbeddedLangs[];
    static const int kEmbeddedLangCount;

    std::wstring m_currentLang = L"en";
    std::map<std::wstring, std::map<std::wstring, std::wstring>> m_strings;
};
