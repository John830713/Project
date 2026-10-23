# AGENTS.md — Project

Windows desktop pet with ROM plugin system. Win32 API + GDI+ + C++17, MinGW/g++. No CI, no package manager.

Read [INDEX.md](INDEX.md) and [resources/reference/project-layout.md](resources/reference/project-layout.md) first for the directory map and architecture (`main.cpp → HostApp → ModuleManager → IFeatureModule`).

## Build (cmd.exe, not bash)

- **`Build.bat`** — one-click: `make clean` → `py Build.py` → `mingw32-make`. Finds make via `MakePath.txt`, then PATH, then `C:\msys64\*`. **Pauses at the end**; use the manual sequence for scripts/CI.
- **`Build.bat -debug`** (or `debug`) / **`make debug`** — debug build with `-DDEBUG_CONSOLE=1` (AllocConsole + DBG traces → `debug_log.txt`).
- **`Build.bat trim`** — build + `make trim`; note the Makefile has **no `trim` target**, the failure is ignored.
- Manual equivalent: `make clean; py Build.py; make`.
- `make` auto-runs `py Build.py` **only if `GeneratedBuild.mk` or `Translation/zh-TW.ini` is missing** — after adding/removing any `.cpp`/`.rc`/module run `py Build.py` explicitly first.
- `make distclean` — removes all `Generated*` + `Translation/zh-TW.ini` (forces full regen).
- Makefile uses **`cmd.exe` syntax** (`@for %%t in ...`, `del`) — no POSIX shell constructs.
- Requires `py` (Python launcher) on PATH; converting a `.png`/`.jpg` root icon → `.ico` additionally needs Pillow (`py -m pip install pillow`) — otherwise build continues without icon.

## Module system

- Each feature lives in `Modules/<Name>/` with a `<Name>.module.ini` manifest:
  ```ini
  [Module]
  Name=AutoKey
  Class=AutoKeyModule
  Sources=AutoKeyParser.cpp,AutoKeyModule.cpp,AutoKeyDialog.cpp
  Headers=AutoKeyModule.h,AutoKeyDialog.h
  Resources=
  Libraries=
  Enabled=1
  ConfigFile=Config_AutoKey.ini
  LogFile=Log_AutoKey.txt
  ```
- List values accept `,` or `;` separators. `Enabled=0` excludes from build without deleting files.
- The generated registry includes `Modules/<Name>/<Name>Module.h`, where `<Name>` is the manifest's `Name` value — so **`Name` must equal the directory name** and the main class header must be named `<Name>Module.h`. `Class` must match the C++ class (defaults to `<Name>Module`). A wrong `Name`/`Class` only breaks when the generated registry compiles, after `py Build.py`.
- New module: copy `Modules/Template/`, rename files/class, then `py Build.py`.
- Per-module runtime config/log use the manifest's `ConfigFile`/`LogFile` names (files in `Config/`, `Log/`).

## Tray shell

- `Tray/TrayHost` (message-only window + `Shell_NotifyIcon`). Architecture, menu/ID layout, visibility persistence, and the in-process watchdog live in [resources/reference/design/spec.md](resources/reference/design/spec.md) §13 and [Tray/INDEX.md](Tray/INDEX.md).
- Main-thread limit (pet window, tray window and message loop share the main thread) is documented there too — the in-process watchdog only rebuilds a *destroyed* window, not a wedged thread.


## Tests

- `mingw32-make SHELL=cmd.exe test` — builds **and runs** 9 EXEs: `Debug/{AutoKey/AutoKeyTest,UI/TooltipTest,Core/DebugStateTest,Core/MenuOrderTest,Core/UpdateManagerTest,RemoteControl/ClipboardTest,RemoteControl/DisconnectTest,RemoteControl/DiagnosticsTest,WebAgent/WebAgentTest}.exe`. Not built by `make all`. **`SHELL=cmd.exe` is required** — the `test`/`clean` recipes use cmd.exe syntax (`for %%t in ...`, `del`), so plain `mingw32-make` runs the MSYS `sh` and dies with `syntax error near unexpected token '('` (this fails from cmd.exe too, not just PowerShell); `clean` otherwise silently no-ops. `resources/tools/local/run-tests.ps1` wraps it.
- Test flags: `-O0 -g -DENABLE_DEBUG_STATE=1`; linked `-luser32 -lkernel32`, plus per-target extras: Tooltip `-lcomctl32`, UpdateManager `-lversion -lgdi32`, Clipboard `-lgdi32`, Disconnect `-lws2_32 -lole32 -luuid`, Diagnostics `-lws2_32`, WebAgent `-lws2_32`; **no GDI+**.
- Tests compile real sources as units: `Core/ModuleManager.cpp` (MenuOrderTest/DebugStateTest), `Core/UpdateManager.cpp` (UpdateManagerTest), `RemoteControlHook.cpp` (ClipboardTest), `Services/NetworkService.cpp` (+ `FileTransferService.cpp` for DisconnectTest, NetworkService only for DiagnosticsTest/WebAgentTest), `Modules/WebAgent/*.cpp` (WebAgentTest) — keep Console/DBG behaviors out of code you want unit-testable.
- `Debug/UI/GuiTest.cpp` (E2E GUI) has **no Makefile target** — compile manually if needed.
- New test files under `Debug/` are **not auto-discovered**: each target is a hardcoded Makefile rule listing its own sources and extra `-l` libs. Copy the existing pattern (`$(CXX) $(TEST_CXXFLAGS) -I. $^ -o $@ $(TEST_LIBS) [extra libs]`) and add the exe to `TEST_TARGETS`.

## Codegen & translation

- `py Build.py` scans root + `Core/|Services/|UI/|Pet/|Tray/` + enabled module `.cpp`/`.rc` → `GeneratedBuild.mk`, `GeneratedModuleRegistry.*`, `GeneratedIcon.*`. All `Generated*` and `Translation/*.ini` (except `_src/`, `en.ini`) are gitignored.
- Icon resource: `Build.py` writes `GeneratedIcon.rc` as `#include "Core/ResourceIds.h"` + `IDI_APP_ICON ICON ...`, so `Core/ResourceIds.h` is the single definition of `IDI_APP_ICON` for both the RC (windres) and C++ (tray icon load). Edit that header, not the generated `.rc`.
- Translations: `Translation/_src/<lang>.ini` + `Pet/lang/<lang>.ini` + `Modules/*/lang/<lang>.ini` merged (later sources override) → `Translation/<lang>.ini` (**UTF-16 LE with BOM**), embedded via `TranslationRes.rc` as RCDATA (lang code → resource name, e.g. `zh-TW` → `ZH_TW`). Read at runtime with `Tr(L"Section", L"Key")`.
- **English strings in the code ARE the translation keys**: `Tr()` returns the passed text verbatim when `lang=en` or the key is missing. Add/modify UI text in English first, then zh-TW; `Translation/en.ini` is a manual English reference that is never loaded at runtime (only `_src/` is merged, only zh-TW is embedded).
- Icon gotcha: `Build.py` picks the **first alphabetically** image at project root, scanned from `.png/.ico/.jpg/.jpeg/.bmp` (case-insensitive), ignoring `Pet.png` and `GeneratedIcon.*` — dropping a stray image at root silently changes the app icon. `Test.png` is the intended icon source.

## Config files (gitignored, user-local)

- `Config/*.ini`, `MakePath.txt` (make path for `Build.bat`), `ProjectName.txt`/`TargetName.txt` (exe name for `Build.py`; `.exe` appended if missing), `HostHint.txt`, `Version.txt`, `Log/`.