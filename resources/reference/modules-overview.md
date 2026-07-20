# Module System Overview

Shared reference for the module plugin architecture.

## Interfaces

| Interface | Header | Purpose |
|-----------|--------|---------|
| `IFeatureModule` | `Core/IFeatureModule.h` | Lifecycle, config, context menu, value access |
| `IDropActionProvider` | `Core/IDropActionProvider.h` | File drop handling (optional) |
| `IDebugStateProvider` | `Core/IDebugStateProvider.h` | Debug state dump (optional, compile-time gated) |
| `IHostContext` | `Core/IHostContext.h` | Abstract host access (HINSTANCE, HWND, TranslationService) |

## Standard Three-Tier Architecture

Most modules follow this internal structure:

```
<Name>Module    — IFeatureModule impl, config, lifecycle
<Name>Logic     — Static utility methods (pure business logic, no state)
<Name>Window    — Win32 tool window (UI)
<Name>Types     — Standalone struct/enum definitions (no core deps)
```

Exceptions:
- **AutoKey**: Uses `AutoKeyParser` + `AutoKeyDialog` instead of Logic+Window
- **Checksum**: Minimal — just Module + trivial Window (MessageBox wrapper)
- **RemoteControl**: Uses Module + Hook + Window + Types (hook is a specialized subsystem)

## Dependency Matrix

| Core/Service | AutoKey | ChangeEC | Checksum | RemoteControl | RomCombiner | RomSeparator | Template |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| `IFeatureModule.h` | H | H | H | H | H | H | H |
| `IDropActionProvider.h` | -- | H | H | H | H | H | H |
| `ConfigTypes.h` | H | H | -- | H | H | --* | H |
| `IHostContext.h` | -- | H** | -- | -- | H** | -- | H** |
| `IDebugStateProvider.h` | H† | -- | -- | -- | -- | -- | -- |
| `ConfigManager.h` | cpp | cpp | cpp | cpp | cpp | cpp | cpp |
| `Logger.h` | cpp | cpp | cpp | cpp | cpp | cpp | cpp |
| `DebugConsole.h` | cpp | -- | -- | -- | -- | -- | -- |
| `TranslationService.h` | cpp | cpp | cpp | cpp | cpp | cpp | cpp |
| `FileService.h` | -- | cpp | -- | -- | cpp | cpp | -- |
| `ChecksumService.h` | -- | -- | cpp | -- | -- | -- | -- |
| `ClipboardService.h` | -- | -- | cpp | -- | -- | -- | -- |
| `NetworkService.h` | -- | -- | -- | cpp | -- | -- | -- |
| `FileTransferService.h` | -- | -- | -- | H+cpp | -- | -- | -- |

**Legend:** H = `.h` include, cpp = `.cpp` only, † = conditional, \* = transitive, \*\* = in Window.h

## Universal Services

Used by all modules:
- **TranslationService** — `.h` level (every module)
- **ConfigManager** — `.cpp` level (every module)
- **Logger** — `.cpp` level (every module)

## Module Interface Implementations

| Module | IFeatureModule | IDropActionProvider | IDebugStateProvider | Custom Settings |
|---|:---:|:---:|:---:|:---:|
| AutoKey | Yes | **No** | Conditional | Yes (dialog) |
| ChangeEC | Yes | Yes | No | No |
| Checksum | Yes | Yes | No | No |
| RemoteControl | Yes | Yes | No | No |
| RomCombiner | Yes | Yes | No | No |
| RomSeparator | Yes | Yes | No | No |
| Template | Yes | Yes | No | No |

## Cross-Module Dependencies

**None.** Zero direct `#include` between modules. All inter-module communication is mediated by the host through `IHostContext`.

## Types Headers are Standalone

Every `*Types.h` includes only standard library headers. Never references Core or Services. Clean separation — types can be included without pulling in the module system.

## Build Integration

- `Build.py` scans `Modules/*/*.module.ini`
- `GeneratedModuleRegistry.cpp` auto-registers all enabled modules
- `HostApp.cpp:70` calls `RegisterGeneratedModules()`
- `Enabled=0` in `.module.ini` excludes a module from build

## Creating a New Module

1. Copy `Modules/Template/` directory
2. Rename files and classes
3. Update `*.module.ini` fields (`Name`, `Class`, `Sources`, `Headers`)
4. Implement `IFeatureModule` + optionally `IDropActionProvider`
5. Run `py Build.py` to regenerate registry

## Implementation Rules

1. Every module must fully implement `IFeatureModule`.
2. For drag-drop support, additionally implement `IDropActionProvider`.
3. For right-click menu items, override `GetContextMenuItems()` / `ExecuteContextMenuItem()`. Labels must be translated inside the module before returning — `ModuleManager` does not translate them:
   ```cpp
   item.label = Tr(L"ModuleName", L"Toggle Enable");
   ```
4. For custom input, additionally implement `IInputHandler`.
5. For About dialog, override `GetVersion()` / `GetPurpose()` (defaults: `"1.0.0"` / `""`). Return raw strings — the About dialog handles `Tr()` wrapping.
6. Modules communicate with host via `IHostContext`, NOT a concrete `HostApp` class.
7. Settings accessed via `GetValue`/`SetValue`, persisted by `ConfigManager`.
8. Config fields (`GetConfigDefinitions`) are automatically exposed in the Settings dialog; each module gets a tab. `Bool` → checkbox, `Int`/`String` → edit box.
9. Build script auto-scans `*.module.ini`; no manual Makefile editing needed.

## Translation Conventions

- Each module has its own lang file: `Modules/<Name>/lang/zh-TW.ini`
- `Build.py` merges all modules' lang files into `Translation/zh-TW.ini` on each build
- Sections: `[ModuleName]` for general UI + context menu labels, `[TabLabels]` for settings tab names, `[About]` for version/purpose strings
- **Prefix+name pattern**: never put trailing space in translation key or value (`Build.py` strips both). Add space in C++: `Tr(L"AutoKey", L"▶ Start") + L" " + actionName`
- Context menu parent prefix: if a module's context menu items need a parent prefix (like `"AutoKey ▶"`), the prefix is translated by `Tr()` and the space is added in C++

## Config File Conventions

- Module config files live in `Config/Config_<ModuleName>.ini`
- Standard fields (`GetConfigDefinitions`) use `[<ModuleName>]` section
- Custom fields or multi-record data may use additional sections like `[Action_N]` (see AutoKey for `GetPrivateProfileStringW`-based access)
