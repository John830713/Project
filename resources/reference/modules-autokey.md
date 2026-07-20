# AutoKey Module — Detailed Reference

Keyboard automation module. Records and replays key sequences with hotkey triggers.

## Hotkey System Chain

```
RegisterHotKey(m_hHotkeyWnd, action.id, modifiers, vk)
    ↓
WM_HOTKEY received by HotkeyWndProc (hidden WS_POPUP HWND)
    ↓
OnHotkey(action.id) — looks up action by id
    ↓
std::thread::RunAction(action)
    ↓
ExecuteSequence() — iterates parsed key sequence
    ↓
SendInput() — OS-level key simulation
```

## Key Parser

`AutoKeyParser` is a namespace with pure static functions (no class state):

- `ParseSequence(std::wstring)` → `std::vector<ParsedChord>` — parses "ctrl+a shift+b" syntax
- `KeyNameToVk(std::wstring)` → virtual key code
- `ModNameToVk(std::wstring)` → modifier flag (MOD_ALT, MOD_CONTROL, etc.)
- `ModNameToRegFlag(std::wstring)` → modifier for `RegisterHotKey`

## Thread Safety

- `AutoKeyAction::running` uses `std::atomic<bool>`
- `AutoKeyAction::thread` uses `InterlockedExchange` for safe move-assignment
- Each action spawns its own thread; no shared mutable state between actions

## Dialog Implementation

`AutoKeyDialog` uses an inline child window pattern:
- `Show(parent, module)` — creates modal loop
- `EditAction()` — creates a child HWND with a lambda `WndProc` (not a separate class)
- `RefreshList()` — sends `LB_RESETCONTENT` to rebuild the listbox

## Known Issues

- `AutoKeyParser.h` is missing from `AutoKey.module.ini` `Headers=` field (compiles via include chain)
- `DestroyWindow` timing caused empty list after editing (historical bug, fixed)
- `WS_GROUP` missing caused RadioButton grouping anomalies (historical bug, fixed)

## Compile-Time Gating

`IDebugStateProvider` implementation is conditional on `ENABLE_DEBUG_STATE`:
```cpp
#ifdef ENABLE_DEBUG_STATE
    // debug state dump implementation
#endif
```
Enabled in test builds via `TEST_CXXFLAGS = -DENABLE_DEBUG_STATE=1`.
