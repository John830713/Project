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

    // True when a Run entry for this app exists and its executable is still on
    // disk. The check is app-level, not file-level: a dev build and the
    // installed copy share the value name, and the flag must not flip to "off"
    // just because the other copy is the registered one.
    static bool IsEnabled();

    // Adds or removes the Run entry. An existing registration that still points
    // at a real file is left untouched, so a copy running from another folder
    // cannot take over (or delete) the installed app's autostart. Returns false
    // when the registry rejects the write (policy, permissions).
    static bool SetEnabled(bool enabled);

    // Command written when a missing or stale entry has to be (re)created: the
    // quoted executable plus the --flavor/--carrier switches this process was
    // started with (--reload-wait is never persisted), so booting reproduces
    // the mode the user was in when they ticked the box.
    static std::wstring Command();
};
