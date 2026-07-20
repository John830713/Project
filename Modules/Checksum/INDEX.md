# Checksum

File checksum calculator. Simplest module in the project.

## Files

| File | Role |
|------|------|
| `ChecksumModule.h/cpp` | IFeatureModule + IDropActionProvider impl |
| `ChecksumWindow.h/cpp` | Thin wrapper around `MessageBoxW` |

## Interfaces

- `IFeatureModule` — yes
- `IDropActionProvider` — yes

## Notes

- No Types header, no Logic class — minimal module
- Only module using `ChecksumService` and `ClipboardService`

## Core/Services used

ConfigManager, Logger, ChecksumService, ClipboardService, TranslationService

## Referenced by

- [Modules/INDEX.md](../INDEX.md)
