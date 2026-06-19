#pragma once

#include <string>
#include <windows.h>

class ChecksumWindow {
public:
    static void ShowResult(HWND parent, const std::wstring& title, const std::wstring& message, bool autoClose, UINT durationMs);
};