# AGENTS.md — Project

Windows desktop pet with ROM plugin system. Win32 API + GDI+ + C++17, MinGW/g++. No CI, no package manager.

Navigation: [INDEX.md](INDEX.md) (directory chain) · [resources/reference/project-layout.md](resources/reference/project-layout.md) (map) · [resources/reference/design/spec.md](resources/reference/design/spec.md) (arch: §1 modules, §4 build, §9 debug/tests, §12 tray, §14 WebAgent)

## Build (cmd.exe, not bash)

- `Build.bat` = `make clean` → `py Build.py` → `make`, pauses. `Build.bat debug` / `make debug` adds `-DDEBUG_CONSOLE=1` (DEBUG_CONSOLE build).
- Toolchain: `mingw32-make` is **not on PATH**. `Build.bat` reads `MakePath.txt` or searches common MSYS2 paths and prepends that bin dir to `PATH`. If missing, provide `MakePath.txt` (e.g. `D:\Program Files\MSYS2Portable\App\msys64\ucrt64\bin`).
- `make` auto-runs `py Build.py` only if `GeneratedBuild.mk` or `Translation/zh-TW.ini` is missing. **Run `py Build.py` yourself after adding/removing any `.cpp`/`.rc`/module** (otherwise new files are silently ignored).
- `Build.py` compiles only: root `main.cpp`, `HostApp.cpp`, `GeneratedModuleRegistry.cpp`; **non-recursive** `*.cpp` in `Core|Services|UI|Pet|Tray`. Module sources come from manifests. New root files/subdirs are ignored.
- Every `py Build.py` rewrites `GeneratedVersion.rc` with timestamp version `YYYY.MMDD.HHMM.SS*100+seq` (monotonic, drives in-place update/relaunch). Never hand-edit.
- Link fails with `Permission denied` if `Project.exe` is running — stop it first.
- `make distclean` wipes all `Generated*` + `Translation/zh-TW.ini`. No `trim` target (Build.bat ignores it).

## Modules

- `Modules/<Name>/<Name>.module.ini`: `Name` must equal directory name and main header `<Name>Module.h`; `Class` defaults to `<Name>Module`. Mismatches surface at generated-registry compile after `py Build.py`.
- New module: copy `Modules/Template/`, rename files/class, then `py Build.py`. `Enabled=0` excludes without deleting files. Runtime config/log use manifest `ConfigFile`/`LogFile` (under `Config/`, `Log/`).

## Tests & debug

- `mingw32-make SHELL=cmd.exe test` builds **and runs** 9 EXEs (not part of `make all`). **`SHELL=cmd.exe` is mandatory** (test/clean recipes use `for %%t`/`del`; MSYS sh will error).
- New tests under `Debug/` are **not auto-discovered**: add a hardcoded Makefile rule, append the exe to `TEST_TARGETS`, and add it to `resources/tools/local/run-tests.ps1`. Tests compile real sources as units — keep Console/DBG out of what they link.
- Quick runner: `powershell -ExecutionPolicy Bypass -File resources\tools\local\run-tests.ps1 [-Filter <name>] [-Rebuild]` (prepends toolchain PATH). Names: AutoKey, Tooltip, DebugState, MenuOrder, UpdateManager, Clipboard, Disconnect, Diagnostics, WebAgent.
- DEBUG_CONSOLE build exposes AI-drivable pipe `\\.\pipe\ProjectDebug`: one UTF-16LE command per connection, returns captured `DBG()` output. Commands: `status`, `modules`, `mod <name> config`, `log <category>`, `tray status|show|hide|reset|killwindow`, `quit` (`quit` replies before exit).
- WebAgent serves browser bridge over HTTP (default `0.0.0.0:8921`; `/status`, `/dashboard`). Inert if `ResourceRoot` (`D:\Tampermonkey`) missing: no listener, `IsAvailable()` hides menu group/settings tab (machines without JS tree appear as if module not shipped).
- No crash reporter — read `design/debugging-guide.md` and silent-exit/hang playbooks before chasing crashes.

## Codegen, translations, icon

- `py Build.py` → `GeneratedBuild.mk`, `GeneratedModuleRegistry.*`, `GeneratedIcon.*`, `GeneratedVersion.*`, `Translation/<lang>.ini`. All `Generated*` and `Translation/*.ini` (except `_src/`, `en.ini`) are gitignored — never edit.
- English strings in code are translation keys for `Tr()`. Edit English first, then `_src/zh-TW.ini` (or `Pet/lang/`, `Modules/*/lang/`); merged into UTF-16 LE + BOM and embedded via `TranslationRes.rc`. `Translation/en.ini` is reference-only and never loaded.
- Icon: `Build.py` takes **first alphabetically** root image (`.png/.ico/.jpg/.jpeg/.bmp`, ignoring `Pet.png`/`GeneratedIcon.*`) — stray image silently changes app icon; `Test.png` intended. `IDI_APP_ICON` defined once in `Core/ResourceIds.h`.

## Config files (gitignored, user-local)

`Config/*.ini`, `Log/`, `MakePath.txt`, `ProjectName.txt`/`TargetName.txt` (exe name), `HostHint.txt`, `Version.txt`.
