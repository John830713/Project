# AGENTS.md — Project

Windows desktop pet with ROM plugin system. Win32 API + GDI+ + C++17, MinGW/g++. No CI, no package manager.

Navigation: [INDEX.md](INDEX.md) (directory chain) · [resources/reference/project-layout.md](resources/reference/project-layout.md) (map) · [resources/reference/design/spec.md](resources/reference/design/spec.md) (arch: §1 modules, §4 build, §9 debug/tests, §12 tray, §14 WebAgent)

## Build (cmd.exe, not bash)

- `Build.bat` = `make clean` → `py Build.py` → `make`, pauses. `Build.bat debug` / `make debug` adds `-DDEBUG_CONSOLE=1` (DEBUG_CONSOLE build).
- Toolchain: `mingw32-make` is **not on PATH**. `Build.bat` reads the **full path to `mingw32-make.exe`** from `MakePath.txt` (no file: searches common MSYS2 paths), then prepends its bin dir to `PATH`. This machine: `D:\Program Files\MSYS2Portable\App\msys64\ucrt64\bin\mingw32-make.exe`.
- `make` auto-runs `py Build.py` only when `GeneratedBuild.mk`/`Translation/zh-TW.ini` is missing (or translation sources are newer). **Run `py Build.py` yourself after adding/removing any `.cpp`/`.rc`/module** (otherwise new files are silently ignored).
- `Build.py` compiles root `main.cpp`, `HostApp.cpp`, `GeneratedModuleRegistry.cpp`; **non-recursive** `*.cpp` in `Core|Services|UI|Pet|Tray`; and only files listed in a module's `Sources=`. New or unlisted files are silently ignored.
- Every `py Build.py` rewrites `GeneratedVersion.rc` with timestamp version `YYYY.MMDD.HHMM.SS*100+seq` (monotonic, drives in-place update/relaunch). Never hand-edit.
- Link fails with `Permission denied` only when the link target `Project.exe` itself is running — a copy started from another folder holds its own file and does not block the build. Launching a build while any copy runs does not open a second window (single instance is keyed on the image *name*, `Core/UpdateManager.cpp`): a build newer than the copy is hot-swapped into that copy's path (version first, last-write time when the versions are equal, so a plain `make` rebuild still updates), only the same file reports "already running". `BuildUI` checks both cases.
- `make distclean` wipes all `Generated*` + `Translation/zh-TW.ini`. There is no `trim` target (`Build.bat trim` errors after a successful build).
- `py Build.py --modules A,B` builds only those modules (unselected ones are left out of the registry and the object list); `--list-modules` prints the manifest inventory as JSON. Used by `BuildUI.py` (GUI: module checkboxes + carrier + build), see [resources/reference/flavors-buildui.md](resources/reference/flavors-buildui.md).
- Carriers: `LoaderConfig::Mode {Pet, Tray, Window}` chosen by `Config_Loader.json` `mode` or `Project.exe --carrier <pet|tray|window>`; `--flavor <name>` loads `Config/Config_Flavor_<name>.json` instead. Tray/Window create the pet window hidden (tray + module lifecycle depend on it); Window adds `UI/ModuleLauncherWindow` listing every module context-menu item.

## Modules

- `Modules/<Name>/<Name>.module.ini`: `Name` must equal directory name and main header `<Name>Module.h`; `Class` defaults to `<Name>Module`. Mismatches surface at generated-registry compile after `py Build.py`.
- New module: copy `Modules/Template/`, rename files/class and update `Sources=`/`Headers=`, then `py Build.py`. `Enabled=0` excludes at build; `Config/Config_Loader.json` (`{mode, modules[]}`) filters which compiled modules register at runtime (missing/empty list = all). Runtime config/log use manifest `ConfigFile`/`LogFile` (under `Config/`, `Log/`).

## Tests & debug

- `mingw32-make SHELL=cmd.exe test` builds **and runs** 9 EXEs (not part of `make all`). **`SHELL=cmd.exe` is mandatory** (test/clean recipes use `for %%t`/`del`; MSYS sh will error).
- New tests under `Debug/` are **not auto-discovered**: add a hardcoded Makefile rule, append the exe to `TEST_TARGETS`, and add it to `resources/tools/local/run-tests.ps1`. Tests compile real sources as units — keep Console/DBG out of what they link.
- Quick runner: `powershell -ExecutionPolicy Bypass -File resources\tools\local\run-tests.ps1 [-Filter <name>] [-Rebuild]` (prepends toolchain PATH; hardcodes `D:\Project` and the MSYS2Portable path — edit if the checkout moves). Names: AutoKey, Tooltip, DebugState, MenuOrder, UpdateManager, Clipboard, Disconnect, Diagnostics, WebAgent.
- DEBUG_CONSOLE build exposes AI-drivable pipe `\\.\pipe\ProjectDebug`: one UTF-16LE command per connection, returns captured `DBG()` output. Commands: `status`, `modules`, `mod <name> [config|log|cmd <id>]`, `log <category> [n]`, `tray status|show|hide|reset|killwindow`, `quit` (`quit` replies before exit).
- WebAgent serves browser bridge over HTTP (default `0.0.0.0:8921`; `/status`, `/dashboard`). Inert if `ResourceRoot` (`D:\Tampermonkey`) missing: no listener, `IsAvailable()` hides menu group/settings tab (machines without JS tree appear as if module not shipped).
- Crash handlers in `main.cpp` append to `Log/Crash.txt` (SEH, uncaught C++, `std::terminate`); `Log/*.txt` is written in Release too, but `DBG()`/`debug_log.txt` exist only in DEBUG_CONSOLE builds. Read `design/debugging-guide.md` and the silent-exit/hang playbooks before chasing a crash.

## Codegen, translations, icon

- `py Build.py` → `GeneratedBuild.mk`, `GeneratedModuleRegistry.*`, `GeneratedIcon.*`, `GeneratedVersion.*`, `Translation/<lang>.ini`. All `Generated*` and `Translation/*.ini` (except `_src/`, `en.ini`) are gitignored — never edit.
- English strings in code are translation keys for `Tr()`. Edit English first, then `_src/zh-TW.ini` (or `Pet/lang/`, `Modules/*/lang/`); merged into UTF-16 LE + BOM and embedded via `TranslationRes.rc`. `Translation/en.ini` is reference-only and never loaded. Module display names must go through `TranslationService::TrModuleLabel()`, not `GetDisplayName()`.
- Icon: `Build.py` takes **first alphabetically** root image (`.png/.ico/.jpg/.jpeg/.bmp`, ignoring `Pet.png`/`GeneratedIcon.*`) — stray image silently changes app icon; `Test.png` intended. PNG conversion needs Pillow, otherwise the icon is skipped (warning only). `IDI_APP_ICON` defined once in `Core/ResourceIds.h`.

## Config files (gitignored, user-local)

`Config/*.ini`, `Log/`, `MakePath.txt`, `ProjectName.txt` (display name), `TargetName.txt` (exe name), `HostHint.txt`, `Version.txt` (About text only — binary file version comes from `GeneratedVersion.rc`).
