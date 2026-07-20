# RomCombiner

ROM file combiner. Merges multiple ROM files into one.

## Files

| File | Role |
|------|------|
| `RomCombinerModule.h/cpp` | IFeatureModule + IDropActionProvider impl |
| `RomCombinerLogic.h/cpp` | Static merge logic |
| `RomCombinerWindow.h/cpp` | Tool window UI |
| `RomCombinerTypes.h` | Standalone struct/enum definitions |

## Interfaces

- `IFeatureModule` — yes
- `IDropActionProvider` — yes

## Pattern

Standard three-tier architecture. Same structure as RomSeparator (mirror operation).

## Core/Services used

ConfigManager, Logger, TranslationService, FileService

## Referenced by

- [Modules/INDEX.md](../INDEX.md)
