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
