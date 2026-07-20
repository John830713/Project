# Project — Modular Desktop Pet & ROM Utility

Windows desktop pet with ROM plugin system. Win32 API + GDI+ + C++17, MinGW/g++.

## Forward links

| Path | Description |
|------|-------------|
| [Core/](Core/INDEX.md) | Shared infrastructure (interfaces, managers, config) |
| [Services/](Services/INDEX.md) | Stateless singleton services (i18n, file, network) |
| [Pet/](Pet/INDEX.md) | Desktop pet transparent GDI+ window |
| [UI/](UI/INDEX.md) | Settings dialog (Pet + per-module tabs) |
| [Tests/](Tests/INDEX.md) | Unit test targets |
| [Translation/](Translation/INDEX.md) | Localization sources |
| [resources/](resources/INDEX.md) | Tool docs, skills, reference |
| [Modules/](Modules/INDEX.md) | Feature plugin modules |

## Architecture

```
main.cpp → HostApp → ModuleManager → IFeatureModule (each module)
                   → InputManager
                   → MainWindow (Pet/)
                   → SettingsDialog (UI/)
```

See [resources/reference/modules-overview.md](resources/reference/modules-overview.md) for detailed module system reference.

## Referenced by

- *(Root node — entry point for this project's INDEX chain)*
