#include "CommandLine.h"

#include <cwchar>

namespace CommandLine {

std::wstring GetValue(const wchar_t* cmdLine, const wchar_t* flag) {
    if (!cmdLine || !flag) {
        return {};
    }
    const size_t flagLen = wcslen(flag);
    const wchar_t* p = cmdLine;
    while ((p = wcsstr(p, flag)) != nullptr) {
        // Only accept the flag when it starts a token, so "foo--carrier"
        // inside another value cannot match.
        const bool atTokenStart = (p == cmdLine) || (p[-1] == L' ' || p[-1] == L'\t');
        const wchar_t* after = p + flagLen;
        if (atTokenStart && (*after == L' ' || *after == L'\t' || *after == L'\0')) {
            while (*after == L' ' || *after == L'\t') {
                ++after;
            }
            if (*after == L'"') {
                ++after;
                const wchar_t* end = wcschr(after, L'"');
                return end ? std::wstring(after, end) : std::wstring(after);
            }
            const wchar_t* end = after;
            while (*end && *end != L' ' && *end != L'\t') {
                ++end;
            }
            return std::wstring(after, end);
        }
        p += flagLen;
    }
    return {};
}

}  // namespace CommandLine
