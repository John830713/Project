# AutoKey

Keyboard automation module. Records and replays key sequences with hotkey triggers.

## Files

| File | Role |
|------|------|
| `AutoKeyModule.h/cpp` | IFeatureModule impl, lifecycle, hotkey dispatch |
| `AutoKeyParser.h/cpp` | Pure logic: key sequence parsing (standalone, no core deps) |
| `AutoKeyDialog.h/cpp` | Custom settings dialog (modal, lambda WndProc) |

## Interfaces

- `IFeatureModule` — yes
- `IDropActionProvider` — **no** (only module without)
- `IDebugStateProvider` — conditional (`#ifdef ENABLE_DEBUG_STATE`)

## Notes

- Manages its own hidden `WS_POPUP` HWND for `RegisterHotKey` / `WM_HOTKEY` dispatch
- Each action runs in its own `std::thread` with `InterlockedExchange` for safe teardown
- `AutoKeyParser.h` is missing from `module.ini` `Headers=` (compiles via include chain)
- Detailed analysis: [resources/reference/modules-autokey.md](../../resources/reference/modules-autokey.md)

## Core/Services used

ConfigManager, Logger, DebugConsole, TranslationService

## Referenced by

- [Modules/INDEX.md](../INDEX.md)
