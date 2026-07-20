# AGENTS.md — Project

Windows desktop pet with ROM plugin system. Win32 API + GDI+ + C++17, MinGW/g++.

## Build commands

| Command | What it does |
|---|---|
| `mingw32-make` | Release build |
| `mingw32-make debug` | Clean rebuild with `DEBUG_CONSOLE=1` (AllocConsole, full DBG traces) |
| `mingw32-make test` | Build + run all test targets (AutoKeyTest, TooltipTest, DebugStateTest) |
| `mingw32-make rebuild` | `clean` then `all` |
| `mingw32-make distclean` | `clean` + delete `GeneratedBuild.mk`, `GeneratedIcon.*`, `Translation\zh-TW.ini` (triggers full regeneration; does NOT delete `GeneratedModuleRegistry.*`) |
| `mingw32-make CXXFLAGS_EXTRA=-DDEBUG_CONSOLE=1` | **CAUTION**: incremental only — `main.o` / `DebugConsole.o` may not recompile. Prefer `Build.bat -debug` instead. |
| `py Build.py` | Regenerate `GeneratedBuild.mk`, `GeneratedModuleRegistry.*`, merged translations |
| `Build.bat` | Release build via MakePath.txt or MSYS2 (`clean` + `py Build.py` + `make`) |
| `Build.bat -debug` | Debug build (same but `CXXFLAGS_EXTRA=-DDEBUG_CONSOLE=1` — full rebuild guaranteed) |
| `Build.bat trim` | Release build + trim intermediate files |
| `py ProjectPackager.py --mode report` | Generate packaging report |
| `py ProjectPackager.py --mode export-clean` | Export a clean copy of the project |
| `py ProjectSnapshot.py [--summary\|--full]` | Generate a project context snapshot |

- Makefile auto-runs `py Build.py` if `GeneratedBuild.mk` or `Translation/zh-TW.ini` is missing.
- `Build.py` scans `Modules/*/*.module.ini`. No manual Makefile edits needed to add a module.
- `Tests/GuiTest.cpp` exists but has no Makefile target — compile manually if needed.

### Shell environment

`mingw32-make` (MinGW-w64) uses `cmd.exe` as the shell **regardless** of the `SHELL` variable. All recipes must be cmd.exe-compatible:
- **No** `rm -f` → use `del /f`
- **No** POSIX `for f in ...; do ...; done` → use cmd.exe `for %f in (...) do @...`
- Paths need `$(subst /,\,...)` for `del` to work
- Clean target pattern: `-@del /f /q $(subst /,\,$(ALL_OBJS)) 2>nul`

## Architecture

```
main.cpp → HostApp → ModuleManager → IFeatureModule (each module)
                   → InputManager
                   → MainWindow (Pet/, transparent GDI+ window)
                   → SettingsDialog (UI/, tabs: Pet + per-module)
```

- **Core/** — ModuleManager, ConfigManager, Logger, interfaces
- **Services/** — stateless singletons (TranslationService, FileService, ChecksumService, ClipboardService, NetworkService, FileTransferService)
- **Pet/** — desktop pet host window, right-click menu, movement system
- **UI/** — settings dialog
- **Modules/** — feature plugins, each with `<Name>.module.ini`

## Module system

- Implement `IFeatureModule` (`Core/IFeatureModule.h`) + optionally `IDropActionProvider` for file drop handling.
- `*.module.ini` `[Module]` fields: `Name`, `Class`, `Sources`, `Headers`, `Resources`, `Libraries`, `Enabled=1`, `ConfigFile`, `LogFile`.
- Setting `Enabled=0` excludes the module from build at generation time.
- Auto-registered via `GeneratedModuleRegistry.cpp` (`HostApp.cpp:70` calls `RegisterGeneratedModules`).
- Module configs stored in `Config/Config_<Name>.ini` (whole `Config/` dir is gitignored, created at runtime).
- Use `Modules/Template/` as skeleton reference when creating a new module.

## Translation system

- English text IS the key. `Tr(section, key)` falls back to key text if untranslated.
- Sources: `Translation/_src/{lang}.ini` + per-module `Modules/*/lang/{lang}.ini`. `Translation/en.ini` is the English source (not auto-generated).
- `Build.py` merges → `Translation/{lang}.ini` (UTF-16 LE with BOM).
- `TranslationRes.rc` embeds `Translation/zh-TW.ini` as RCDATA.
- **Beware**: `Build.py` strips whitespace from keys and values — never rely on trailing spaces. Use C++ concatenation: `Tr(...) + L" " + name`.

## Gotchas

- `Build.py` calls `.strip()` on INI keys/values — leading/trailing whitespace silently removed.
- Custom settings "Manage" button has ID 1000. `OpenCustomSettings()` **must not** destroy + repopulate parent controls (button would vanish).
- Context menu labels must call `Tr()` inside the module before returning (ModuleManager does not translate them).
- Hidden `WS_POPUP` windows for hotkey dispatch (AutoKey, RemoteControl) store `this` in `GWLP_USERDATA`.
- Generated files (`GeneratedBuild.mk`, `GeneratedIcon.*`, `GeneratedModuleRegistry.*`) are gitignored — deleting them triggers regeneration.
- `DEBUG_CONSOLE` macro controls `DBG()` / `AllocConsole()` via `Core/DebugConsole.h`.
- Project metadata is read from `ProjectName.txt`, `TargetName.txt`, `HostHint.txt`, `Version.txt` (all gitignored, optional).
