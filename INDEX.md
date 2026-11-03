# Project — Modular Desktop Pet & ROM Utility

Windows desktop pet with ROM plugin system. Win32 API + GDI+ + C++17, MinGW/g++.

## Forward links

| Path | Description |
|------|-------------|
| [Core/](Core/INDEX.md) | Shared infrastructure (interfaces, managers, config) |
| [Services/](Services/INDEX.md) | Stateless singleton services (i18n, file, network) |
| [Pet/](Pet/INDEX.md) | Desktop pet transparent GDI+ window |
| [Tray/](Tray/INDEX.md) | System-tray shell (icon, menu, watchdog, pet visibility) |
| [UI/](UI/INDEX.md) | Settings dialog (Pet + per-module tabs) |
| [Debug/](Debug/INDEX.md) | Module-level debug content (unit tests, test utils, E2E GUI test) |
| [Translation/](Translation/INDEX.md) | Localization sources |
| [resources/](resources/INDEX.md) | Tool docs, skills, reference |
| [Modules/](Modules/INDEX.md) | Feature plugin modules |

## Architecture

```
main.cpp → HostApp → ModuleManager → IFeatureModule (each module)
                   → InputManager
                   → MainWindow (Pet/)
                   → TrayHost (Tray/)
                   → SettingsDialog (UI/)
```

See [resources/reference/modules-overview.md](resources/reference/modules-overview.md) for detailed module system reference.

## Root-level files

| File | Role |
|------|------|
| `main.cpp` | Application entry point (`wWinMain`) |
| `HostApp.h/cpp` | Application host, IHostContext impl, module lifecycle |
| `Makefile` | Build rules (MinGW-g++), auto-runs `py BuildUI.py --generate` if generated files missing |
| `BuildUI.vbs` | Recommended launcher: opens the build tool with no console window |
| `BuildUI.bat` | Same launcher for cmd users (brief console flash) |
| `BuildUI.py` | The single build tool: codegen (registry, icon, version, translations), clean/build/tests, and the module/carrier checkbox UI. Replaces the old `Build.bat`/`Build.py`/`Clear.bat` |
| `Config/BuildUI.json` | Machine-local build config (mode `release`/`debug`, carrier, flavour, selection) — gitignored |
| `ProjectPackager.py` | Packaging report / export-clean |
| `ProjectSnapshot.py` | Project context snapshot for agents |
| `TranslationRes.rc` | Embeds `Translation/zh-TW.ini` as RCDATA |
| `Pet.png` | Desktop pet image (loaded by MainWindow) |
| `Test.png` | Icon source (used by BuildUI.py if no .ico file) |
| `AGENTS.md` | Agent startup guide (build commands, shell environment) |

## Referenced by

- *(Root node — entry point for this project's INDEX chain)*
