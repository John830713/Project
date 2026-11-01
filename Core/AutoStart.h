//==============================================================================
// AutoStart.h - "Start with Windows" toggle
// Purpose:   Manages the per-user HKCU\...\CurrentVersion\Run entry. Per-user
//            means no administrator rights and a toggle that fully undoes
//            itself; no shortcut is copied into the Startup folder.
//==============================================================================

#pragma once

#include <string>

class AutoStart {
public:
    // Registry value name: the executable's base name, so a renamed build
    // replaces the old entry instead of leaving a second one behind.
    static std::wstring ValueName();

    // True when the Run entry exists AND points at this executable. The path
    // check matters because two copies of the app (a dev build and an installed
    // one) share the same file name; only one of them can own the entry.
    static bool IsEnabled();

    // Adds, updates or removes the Run entry. Returns false when the registry
    // rejects the write (policy, permissions).
    static bool SetEnabled(bool enabled);

    // Command Windows runs at sign-in: the quoted executable plus the
    // --flavor/--carrier switches this process was started with, so booting
    // reproduces what the user was looking at when they ticked the box.
    static std::wstring Command();
};
