#include "ChecksumWindow.h"

void ChecksumWindow::ShowResult(HWND parent, const std::wstring& title, const std::wstring& message, bool autoClose, UINT durationMs) {
    (void)autoClose;
    (void)durationMs;

    MessageBoxW(parent, message.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}