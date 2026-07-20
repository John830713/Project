# Core

Project-wide shared infrastructure. All upper layers depend on this.

## Files

| File | Role |
|------|------|
| `IFeatureModule.h` | Base interface all modules must implement |
| `IDropActionProvider.h` | Optional file-drop interface for modules |
| `IDebugStateProvider.h` | Optional debug-state dump interface (compile-time gated) |
| `IHostContext.h` | Abstract host context (HINSTANCE, HWND, TranslationService) |
| `IInputHandler.h` | Optional input handling interface |
| `ConfigManager.h/cpp` | INI config path management, read/write, auto-fill |
| `ConfigTypes.h` | `ConfigFieldDefinition` and `ConfigValueType` definitions |
| `DropTypes.h` | `DropContext` / `DropActionDefinition` shared types |
| `InputManager.h/cpp` | Centralized input dispatch |
| `Logger.h/cpp` | Categorized logs, per-module log writing |
| `ModuleManager.h/cpp` | Module registration, lifecycle, drop/context-menu dispatch |
| `DebugConsole.h/cpp` | Compile-time debug console (`-DDEBUG_CONSOLE=1`) |

## Debug Console

- `DEBUG_CONSOLE` macro enabled via `-DDEBUG_CONSOLE=1` in CXXFLAGS
- When enabled: `AllocConsole()` at startup, `DBG(L"msg")` macro for output
- When disabled (default): `DBG()` compiles to `((void)0)`
- Quick enable: `mingw32-make debug` (clean rebuild) or `Build.bat -debug`
- `DBG_OPEN()` called in `main.cpp::wWinMain`

## Context Menu Dispatch (ModuleManager)

- `GetContextMenuItems()` replaces each module's raw `itemId` with unique IDs
- Mapping stored in `m_menuRoutes` (`vector<pair<module*, originalItemId>>`)
- `ExecuteContextMenuItem(int uniqueId)` indexes `m_menuRoutes` in O(1)
- Modules must `Tr()` their context menu labels before returning items

## Custom Settings UI

- Modules override `HasCustomSettings()` / `OpenCustomSettings(HWND)` for custom dialogs
- **CRITICAL**: `OpenCustomSettings` must NOT destroy+repopulate parent controls (the Manage button ID 1000 belongs to SettingsDialog's control set and would vanish)

## Hidden Window Pattern

- Some modules (AutoKey, RemoteControl) create hidden `WS_POPUP` windows for message dispatch (`RegisterHotKey` / `WM_HOTKEY`, etc.)
- Store `this` pointer in `GWLP_USERDATA` for static WndProc dispatch

## Rules

1. Core must NOT depend on any feature module
2. All interfaces must stay project-neutral and reusable across projects
3. Init order: Logger → ConfigManager → ModuleManager → InputManager

## Referenced by

- [INDEX.md](../INDEX.md)
