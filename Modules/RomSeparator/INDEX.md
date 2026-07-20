# RomSeparator

ROM file separator. Splits a combined ROM into individual files.

## Files

| File | Role |
|------|------|
| `RomSeparatorModule.h/cpp` | IFeatureModule + IDropActionProvider impl |
| `RomSeparatorLogic.h/cpp` | Static split logic |
| `RomSeparatorWindow.h/cpp` | Tool window UI |
| `RomSeparatorTypes.h` | Standalone struct/enum definitions |

## Interfaces

- `IFeatureModule` — yes
- `IDropActionProvider` — yes

## Pattern

Standard three-tier architecture. Mirror operation of RomCombiner.

## Core/Services used

ConfigManager, Logger, TranslationService, FileService

## Referenced by

- [Modules/INDEX.md](../INDEX.md)
