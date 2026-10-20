# Tray

System-tray shell for the host application. Owns a message-only window
(`HWND_MESSAGE`) plus the notification-area icon, so the application stays
reachable when the pet body itself is hidden.

## Files

| File | Role |
|------|------|
| `TrayHost.h/cpp` | Message window, `Shell_NotifyIcon` add/remove, tray menu, watchdog |

## Scope (v1)

Host-level shell only. The tray menu exposes the host actions and mirrors the
module context-menu groups; it does **not** manage services, poll health, or
report progress — that is Phase 2.

## Tray Menu

```
Show Pet / Hide Pet     — toggle the pet window (double-click the icon too)
Settings                — open the pet settings dialog
Reset Pet Window        — destroy and rebuild the pet window in-process
---
<module groups>         — GetMenuGroups(); each module is a submenu
---
Exit
```

## Command IDs

The pet window owns `100–111` / `2000+` / `6000+` / `9999`; the tray keeps a
separate high band so the two never collide:

| ID | Action |
|----|--------|
| `12000` | Toggle pet visibility |
| `12001` | Settings |
| `12003` | Reset (recreate) pet window |
| `12100` | Base for module menu groups (offset by module-item index) |
| `12999` | Exit |

## Visibility persistence

`Config/Config_Tray.ini` `[Tray] petVisible` (`1`/`0`) records whether the pet
window should be shown at startup. Double-clicking the tray icon toggles and
saves it, so a hidden pet stays hidden across restarts.

## Watchdog

A `SetTimer(1, 3000)` tick calls `HostApp::IsMainWindowAlive()`; when the pet
window is gone (`!IsWindow(m_mainWindow)`) the host calls `RecreateMainWindow()`
under a `m_recreating` flag so the unconditional `PostQuitMessage` in
`MainWindow::WM_DESTROY` is skipped and the same message loop keeps running.

**Architectural limit.** The pet window, this message window and the message
loop all live on the main thread. A hard hang of the pet window freezes the
tray as well; the in-process watchdog can only detect a *destroyed* window, not
a wedged thread. Recovering a genuine hang needs an external watchdog (Phase 2).

The tray callback message is `WM_APP + 12` (`kTrayCallbackMessage`), chosen to
avoid `WM_APP + 10` (`WM_APP_CONSOLE_CMD`).

## Debug channel

A `DEBUG_CONSOLE` build exposes `tray status|show|hide|reset|killwindow` through
the debug console and the `\\.\pipe\ProjectDebug` server (see `spec.md` §11/§12),
so an agent can drive and verify this subsystem. `tray killwindow` destroys the
pet window without recreating it to exercise watchdog recovery; the host pointer
for these commands is `HostApp::DebugConsoleSetHost` + `StartPipeServer` in
`HostApp::Run`.

## Verification

`.agent/tray-smoke.ps1` launches the debug build and asserts the state machine
(15 checks: icon added, hide/show, reset while hidden/visible, watchdog ticking,
killwindow + rebuild, process survival).

## See also

- [INDEX.md](../INDEX.md)
- [Pet/INDEX.md](../Pet/INDEX.md) — `SetRecreating()` and public `OpenSettings()`
