# design

Desktop pet system architecture and reusable UI patterns.

## Documents

| File | Description |
|------|-------------|
| [spec.md](spec.md) | Architecture — module system, context menu, config, build |
| [usage.md](usage.md) | Patterns — new module, context menu item, dialog |

## Principles

- **Modules are plugins**: each module is a self-contained DLL-like unit under `Modules/`
- **Build.py is the source of truth**: it scans `Modules/*/*.module.ini` to generate registry + build files
- **Win32 native**: no MFC/ATL, no dialog resources, all UI created programmatically
- **INI persistence**: all config uses `GetPrivateProfileStringW`/`WritePrivateProfileStringW`
- **Translation optional**: `Tr(section, key)` falls back to English key if no locale file

## Referenced by

- [reference/INDEX.md](../INDEX.md)
