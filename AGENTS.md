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
- `Build.py` hardcodes the include path `Modules/<Name>/<Name>Module.h` — a module's main class header **must** be named `<Name>Module.h`.
- New module: copy `Modules/Template/`, rename files/class, then `py Build.py`.
- Per-module runtime config/log use the manifest's `ConfigFile`/`LogFile` names (files in `Config/`, `Log/`).

## Tests

- `mingw32-make test` — builds **and runs** 5 EXEs: `Debug/{AutoKey/AutoKeyTest,UI/TooltipTest,Core/DebugStateTest,Core/MenuOrderTest,RemoteControl/ClipboardTest}.exe`. Not built by `make all`.
- Test flags: `-O0 -g -DENABLE_DEBUG_STATE=1`; linked `-luser32 -lkernel32` (TooltipTest adds `-lcomctl32`, ClipboardTest adds `-lgdi32`), **no GDI+**.
- Tests compile the real `Core/ModuleManager.cpp` as a unit (MenuOrderTest/DebugStateTest) and the real `RemoteControlHook.cpp` (ClipboardTest) — keep Console/DBG behaviors out of code you want unit-testable.
- `Debug/UI/GuiTest.cpp` (E2E GUI) has **no Makefile target** — compile manually if needed.

## Codegen & translation

- `py Build.py` scans root + `Core/|Services/|UI/|Pet/` + enabled module `.cpp`/`.rc` → `GeneratedBuild.mk`, `GeneratedModuleRegistry.*`, `GeneratedIcon.*`. All `Generated*` and `Translation/*.ini` (except `_src/`, `en.ini`) are gitignored.
- Translations: `Translation/_src/<lang>.ini` + `Pet/lang/<lang>.ini` + `Modules/*/lang/<lang>.ini` merged → `Translation/<lang>.ini` (**UTF-16 LE with BOM**), embedded via `TranslationRes.rc` as RCDATA. Read at runtime with `Tr(L"Section", L"Key")`.
- Icon gotcha: `Build.py` picks the **first alphabetically** image at project root (ignoring `Pet.png`, `GeneratedIcon.*`) — dropping a stray `*.png` at root silently changes the app icon.

## Config files (gitignored, user-local)

- `Config/*.ini`, `MakePath.txt` (make path for `Build.bat`), `ProjectName.txt`/`TargetName.txt` (exe name for `Build.py`; `.exe` appended if missing), `HostHint.txt`, `Version.txt`, `Log/`.