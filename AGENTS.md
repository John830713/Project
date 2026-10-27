# AGENTS.md — Project

Windows desktop pet with ROM plugin system. Win32 API + GDI+ + C++17, MinGW/g++. No CI, no package manager.

Navigation: [INDEX.md](INDEX.md) is the directory chain; [resources/reference/project-layout.md](resources/reference/project-layout.md) is the map;
[resources/reference/design/spec.md](resources/reference/design/spec.md) is the architecture (§1 modules, §4 build, §9 debug/tests, §12 tray, §14 WebAgent).

## Build (cmd.exe, not bash)

- `Build.bat` = `make clean` → `py Build.py` → make, then **pauses**. `Build.bat debug` / `make debug` adds `-DDEBUG_CONSOLE=1`.
- `mingw32-make` is **not on PATH** — the toolchain is the path in `MakePath.txt` (`D:\Program Files\MSYS2Portable\App\msys64\ucrt64\bin`); prepend it to `PATH` or let `Build.bat` do it.
- `make` re-runs `py Build.py` **only** when `GeneratedBuild.mk` or `Translation/zh-TW.ini` is missing → run `py Build.py` yourself after adding/removing any `.cpp`/`.rc`/module.
- `Build.py` compiles only `main.cpp`, `HostApp.cpp`, `GeneratedModuleRegistry.cpp` at root, plus **non-recursive** `*.cpp` in `Core|Services|UI|Pet|Tray`; module sources come from the manifest. A new root file or subdirectory is silently ignored.
- Every `py Build.py` rewrites `GeneratedVersion.rc` with a timestamp version (`YYYY.MMDD.HHMM.SS*100+seq`) — each build is "newer", which is what drives the in-place update/relaunch. Never hand-edit it.
- The link fails with `Permission denied` while `Project.exe` runs — stop it first. `make distclean` wipes all `Generated*` + `Translation/zh-TW.ini`. There is **no `trim` target** (Build.bat ignores that failure).

## Modules

- `Modules/<Name>/<Name>.module.ini`: `Name` must equal the directory name and the main header must be `<Name>Module.h`; `Class` defaults to `<Name>Module`. A mismatch only surfaces when the generated registry compiles, after `py Build.py`.
- New module: copy `Modules/Template/`, rename files/class, then `py Build.py`. `Enabled=0` excludes it without deleting files. Manifest fields: spec.md §1.
- Runtime config/log use the manifest's `ConfigFile`/`LogFile` (files under `Config/`, `Log/`).

## Tests & debug

- `mingw32-make SHELL=cmd.exe test` builds **and runs** 9 EXEs (not part of `make all`). `SHELL=cmd.exe` is mandatory — the `test`/`clean` recipes use `for %%t`/`del`, so plain make runs MSYS sh and dies with `syntax error near unexpected token '('` (or silently skips the clean).
- New tests under `Debug/` are **not auto-discovered**: add a hardcoded Makefile rule, append the exe to `TEST_TARGETS`, and add it to `resources/tools/local/run-tests.ps1`. Tests compile real sources as units — keep Console/DBG behaviour out of what they link.
- A `DEBUG_CONSOLE` build exposes an AI-drivable pipe `\\.\pipe\ProjectDebug`: one UTF-16LE command line per connection, returns the captured `DBG()` output, same commands as the console (`status`, `modules`, `mod <name> config`, `tray status|show|hide|reset|killwindow`, `quit`). Use it to inspect a running app instead of guessing.
- WebAgent serves the browser bridge over HTTP (default `0.0.0.0:8921`; `/status`, `/dashboard`). It stays inert when `ResourceRoot` (`D:\Tampermonkey`) is missing.
- No crash reporter: read `design/debugging-guide.md` and the silent-exit / hang playbooks before chasing a crash.

## Codegen, translations, icon

- `py Build.py` → `GeneratedBuild.mk`, `GeneratedModuleRegistry.*`, `GeneratedIcon.*`, `GeneratedVersion.*`, `Translation/<lang>.ini`. All `Generated*` and `Translation/*.ini` (except `_src/`, `en.ini`) are gitignored — never edit them.
- **English strings in the code ARE the translation keys**: `Tr()` returns the text verbatim for `lang=en` or a missing key. Edit English first, then `_src/zh-TW.ini` (or `Pet/lang/`, `Modules/*/lang/`); it merges into UTF-16 LE + BOM and is embedded via `TranslationRes.rc`. `Translation/en.ini` is reference-only and never loaded.
- Icon: `Build.py` takes the **first alphabetically** root image (`.png/.ico/.jpg/.jpeg/.bmp`, ignoring `Pet.png`/`GeneratedIcon.*`) — a stray image silently changes the app icon; `Test.png` is intended. `IDI_APP_ICON` is defined once in `Core/ResourceIds.h`, not in the generated `.rc`.

## Config files (gitignored, user-local)

`Config/*.ini`, `Log/`, `MakePath.txt`, `ProjectName.txt`/`TargetName.txt` (exe name), `HostHint.txt`, `Version.txt`.
