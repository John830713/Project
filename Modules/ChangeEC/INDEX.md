# ChangeEC

ROM character encoding converter. Supports drag-and-drop file input.

## Files

| File | Role |
|------|------|
| `ChangeECModule.h/cpp` | IFeatureModule + IDropActionProvider impl |
| `ChangeECLogic.h/cpp` | Static conversion logic (pure business logic) |
| `ChangeECWindow.h/cpp` | Tool window UI |
| `ChangeECTypes.h` | Standalone struct/enum definitions |

## Interfaces

- `IFeatureModule` — yes
- `IDropActionProvider` — yes

## Pattern

Follows the standard three-tier architecture: Module → Logic (static methods) → Window (UI). Types factored into a separate header.

## Core/Services used

ConfigManager, Logger, TranslationService, FileService

## Referenced by

- [Modules/INDEX.md](../INDEX.md)
