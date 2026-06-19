//==============================================================================
// ClipboardService.h - Clipboard text copy service
// Purpose:   Provides static methods for copying text to the Windows clipboard.
//==============================================================================

#pragma once

#include <string>

class ClipboardService {
public:
    static bool CopyText(const std::wstring& text);
};
