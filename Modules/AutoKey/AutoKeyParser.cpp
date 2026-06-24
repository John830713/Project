#include "AutoKeyParser.h"
#include <cwctype>

namespace {

struct ModEntry {
    const wchar_t* name;
    WORD vk;
    WORD regFlag;
};

const ModEntry kMods[] = {
    { L"Ctrl",  VK_CONTROL, MOD_CONTROL },
    { L"Shift", VK_SHIFT,   MOD_SHIFT },
    { L"Alt",   VK_MENU,    MOD_ALT },
    { L"Win",   VK_LWIN,    MOD_WIN },
};

} // anonymous namespace

WORD AutoKeyParser::ModNameToVk(const std::wstring& name) {
    for (const auto& m : kMods) {
        if (_wcsicmp(name.c_str(), m.name) == 0) return m.vk;
    }
    return 0;
}

WORD AutoKeyParser::ModNameToRegFlag(const std::wstring& name) {
    for (const auto& m : kMods) {
        if (_wcsicmp(name.c_str(), m.name) == 0) return m.regFlag;
    }
    return 0;
}

WORD AutoKeyParser::KeyNameToVk(const std::wstring& name) {
    if (name.empty()) return VK_F13;

    std::wstring upper;
    upper.reserve(name.size());
    for (auto c : name) upper.push_back(towupper(c));

    if (upper.size() >= 2 && upper[0] == L'F') {
        try {
            int n = std::stoi(upper.substr(1));
            if (n >= 1 && n <= 24) return static_cast<WORD>(VK_F1 + n - 1);
        } catch (...) {}
    }

    if (upper.size() == 1 && iswalpha(upper[0]))
        return static_cast<WORD>(upper[0]);

    if (upper == L"SCROLL_LOCK" || upper == L"SCROLLLOCK") return VK_SCROLL;
    if (upper == L"CAPS_LOCK"   || upper == L"CAPSLOCK")   return VK_CAPITAL;
    if (upper == L"NUM_LOCK"    || upper == L"NUMLOCK")     return VK_NUMLOCK;
    if (upper == L"SHIFT")  return VK_SHIFT;
    if (upper == L"CTRL")   return VK_CONTROL;
    if (upper == L"ALT")    return VK_MENU;
    if (upper == L"WIN")    return VK_LWIN;
    if (upper == L"DEL" || upper == L"DELETE") return VK_DELETE;
    if (upper == L"RETURN" || upper == L"ENTER") return VK_RETURN;
    if (upper == L"SPACE")  return VK_SPACE;
    if (upper == L"TAB")    return VK_TAB;
    if (upper == L"ESC" || upper == L"ESCAPE") return VK_ESCAPE;
    if (upper == L"HOME")   return VK_HOME;
    if (upper == L"END")    return VK_END;
    if (upper == L"INSERT" || upper == L"INS") return VK_INSERT;
    if (upper == L"PAGEUP" || upper == L"PGUP")   return VK_PRIOR;
    if (upper == L"PAGEDOWN" || upper == L"PGDN") return VK_NEXT;
    if (upper == L"UP")     return VK_UP;
    if (upper == L"DOWN")   return VK_DOWN;
    if (upper == L"LEFT")   return VK_LEFT;
    if (upper == L"RIGHT")  return VK_RIGHT;

    return VK_F13;
}

std::vector<ParsedChord> AutoKeyParser::ParseSequence(const std::wstring& keys) {
    std::vector<ParsedChord> result;
    if (keys.empty()) return result;

    size_t pos = 0;
    while (pos < keys.size()) {
        while (pos < keys.size() && (keys[pos] == L' ' || keys[pos] == L',')) ++pos;
        if (pos >= keys.size()) break;

        size_t end = pos;
        while (end < keys.size() && keys[end] != L',') ++end;

        std::wstring chordStr = keys.substr(pos, end - pos);
        pos = end;

        while (!chordStr.empty() && chordStr.back() == L' ') chordStr.pop_back();
        while (!chordStr.empty() && chordStr.front() == L' ') chordStr.erase(0, 1);

        ParsedChord chord;
        size_t plus = chordStr.find(L'+');
        if (plus == std::wstring::npos) {
            chord.key = KeyNameToVk(chordStr);
        } else {
            size_t modStart = 0;
            while (modStart < chordStr.size()) {
                size_t plusEnd = chordStr.find(L'+', modStart);
                if (plusEnd == std::wstring::npos) {
                    std::wstring k = chordStr.substr(modStart);
                    chord.key = KeyNameToVk(k);
                    break;
                }
                std::wstring modName = chordStr.substr(modStart, plusEnd - modStart);
                WORD vk = ModNameToVk(modName);
                if (vk) chord.modifiers.push_back(vk);
                modStart = plusEnd + 1;
            }
        }

        result.push_back(std::move(chord));
    }

    return result;
}
