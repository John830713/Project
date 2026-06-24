#pragma once

#include <string>
#include <vector>
#include <windows.h>

struct ParsedChord {
    std::vector<WORD> modifiers; // VK_CONTROL, VK_SHIFT, VK_MENU
    WORD key = 0;
};

namespace AutoKeyParser {

WORD ModNameToVk(const std::wstring& name);
WORD ModNameToRegFlag(const std::wstring& name);
WORD KeyNameToVk(const std::wstring& name);

// "Ctrl+A" → ParsedChord { modifiers: [VK_CONTROL], key: 'A' }
// "F13"    → ParsedChord { modifiers: [], key: VK_F13 }
std::vector<ParsedChord> ParseSequence(const std::wstring& keys);

} // namespace AutoKeyParser
