//==============================================================================
// ClipboardService.cpp - Clipboard text copy implementation
//==============================================================================

#include "ClipboardService.h"

#include <windows.h>
#include <cstring>

//------------------------------------------------------------------------------
// CopyText - copies wide string to clipboard as CF_UNICODETEXT
// Returns true on success
//------------------------------------------------------------------------------

bool ClipboardService::CopyText(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) {
        return false;
    }

    if (!EmptyClipboard()) {
        CloseClipboard();
        return false;
    }

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) {
        CloseClipboard();
        return false;
    }

    void* ptr = GlobalLock(hMem);
    if (!ptr) {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }

    memcpy(ptr, text.c_str(), bytes);
    GlobalUnlock(hMem);

    if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}
