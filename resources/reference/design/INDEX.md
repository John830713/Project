# design

Desktop pet system architecture and reusable UI patterns.

## Documents

| File | Description |
|------|-------------|
| [spec.md](spec.md) | Architecture — module system, context menu, config, build, agent documentation convention |
| [usage.md](usage.md) | Patterns — new module, context menu item, dialog |

## Principles

- **Modules are plugins**: each module is a self-contained DLL-like unit under `Modules/`
- **Build.py is the source of truth**: it scans `Modules/*/*.module.ini` to generate registry + build files
- **Win32 native**: no MFC/ATL, no dialog resources, all UI created programmatically
- **INI persistence**: all config uses `GetPrivateProfileStringW`/`WritePrivateProfileStringW`
- **Translation**: every module must provide `lang/zh-TW.ini`; `Tr(section, key)` falls back to English key if locale entry missing

## Referenced by

- [reference/INDEX.md](../INDEX.md)
