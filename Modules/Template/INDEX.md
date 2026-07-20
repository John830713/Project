# Template

Skeleton reference for creating new modules. Copy this directory as a starting point.

## Files

| File | Role |
|------|------|
| `TemplateModule.h/cpp` | IFeatureModule + IDropActionProvider impl (boilerplate) |
| `TemplateLogic.h/cpp` | Static logic placeholder (no core/services deps) |
| `TemplateWindow.h/cpp` | Tool window placeholder |
| `TemplateTypes.h` | Standalone type definitions |

## Interfaces

- `IFeatureModule` — yes
- `IDropActionProvider` — yes

## Pattern

Standard three-tier architecture. Use as reference when creating a new module.

## Core/Services used

ConfigManager, Logger, TranslationService

## Referenced by

- [Modules/INDEX.md](../INDEX.md)
