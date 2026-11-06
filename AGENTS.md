# AGENTS.md — Project

Windows desktop pet with ROM/utility plugin modules. Win32 API + GDI+ + C++17, MinGW/g++. No CI, linter, formatter, or package manager — building and `make test` are the only verification.

Navigation: [INDEX.md](INDEX.md) (directory chain) · [resources/reference/project-layout.md](resources/reference/project-layout.md) (map) · [resources/reference/design/spec.md](resources/reference/design/spec.md) (arch: §1 modules, §4 build, §9 debug/tests, §12 tray, §14 WebAgent). Every doc directory needs `INDEX.md` + `.index.json`; verify with `python D:\Agent\resources\tools\common\chain-check\chain_check.py --root D:\Project --verify`.

## Build (cmd.exe, not bash)

- One build tool: `BuildUI.vbs` (no console), `BuildUI.bat` (same, brief console flash) or `py BuildUI.py` (keeps a console for tracebacks). It owns codegen, clean, build, tests and the module/carrier checkbox UI. Keep `BuildUI.vbs` pure ASCII (WSH reads it as ANSI without a BOM); a silent windowless failure lands in `.agent/buildui_error.log` plus a MessageBox.
- Toolchain: `mingw32-make` is **not on PATH**. `MakePath.txt` holds the **full path to `mingw32-make.exe`** (no file: `BuildUI.py` falls back to known MSYS2 paths), and the tool prepends its bin dir to `PATH` for every child. This machine: `D:\Program Files\MSYS2Portable\App\msys64\ucrt64\bin\mingw32-make.exe`.
- Build modes live in `Config/BuildUI.json` (gitignored): `"mode": "release" | "debug"`. `debug` adds `-DDEBUG_CONSOLE=1` (AllocConsole + `DBG()` + the `\\.\pipe\ProjectDebug` pipe). Switching modes forces a `make clean` so Release/Debug objects never mix. A plain `make` protects itself too: `.build-flags` records `$(TARGET)|$(CXXFLAGS)|$(LIBS_EXTRA)` and every object depends on it, so any flag change (including a bare `mingw32-make`, which never cleans) rebuilds all objects. Mixing flag sets is not cosmetic — `-DDEBUG_CONSOLE` changes `MainWindow`'s layout, and the resulting heap corruption surfaced as `0xC0000005` inside `std::locale` on startup. CLI: `py BuildUI.py --build --mode debug`; the raw `mingw32-make SHELL=cmd.exe debug` target still works.
- One build = `make clean` (optional) → `py BuildUI.py --generate` (codegen) → `make`. `make` auto-runs generate only when `GeneratedBuild.mk` / `Translation/zh-TW.ini` is missing (or translation sources are newer). **Run it yourself after adding/removing any `.cpp`/`.rc`/module**; codegen compiles root `main.cpp`, `HostApp.cpp`, `GeneratedModuleRegistry.cpp`, **non-recursive** `*.cpp` in `Core|Services|UI|Pet|Tray`, and only a module's `Sources=` — new or unlisted files are silently ignored.
- Every codegen run rewrites `GeneratedVersion.rc` with timestamp version `YYYY.MMDD.HHMM.SS*100+seq` (monotonic, drives in-place update/relaunch). Never hand-edit.
- Link fails with `Permission denied` only when the link target exe itself (default `Project.exe`; `TargetName.txt` renames it) is running — a copy started from another folder holds its own file and does not block the build. Launching a build while any copy runs does not open a second window (single instance is keyed on the image *name*, `Core/UpdateManager.cpp`): a build newer than the copy is hot-swapped into that copy's path (version first, last-write time when the versions are equal, so a plain `make` rebuild still updates), only the same file reports "already running". `BuildUI` checks both cases.
- `make distclean` (UI: 清理 / `py BuildUI.py --clear`) wipes all `Generated*` + `Translation/zh-TW.ini`. There is no `trim` target.
- `py BuildUI.py --generate --modules A,B` builds only those modules (unselected ones are left out of the registry and the object list); `--list-modules` prints the manifest inventory as JSON; `--build [--clean] [--test]`, `--test` and `--clear` are the headless equivalents of the UI buttons. See [resources/reference/flavors-buildui.md](resources/reference/flavors-buildui.md).
- Carriers: `LoaderConfig::Mode {Pet, Tray, Window}` chosen by `Config_Loader.json` `mode` or `Project.exe --carrier <pet|tray|window>`; `--flavor <name>` loads `Config/Config_Flavor_<name>.json` instead. Tray/Window create the pet window hidden (tray + module lifecycle depend on it); Window adds `UI/ModuleLauncherWindow` listing every module context-menu item. `--flavor` filters at runtime; `--modules` filters at build time; they are independent.

## Modules

- `Modules/<Name>/<Name>.module.ini`: `Name` must equal directory name and main header `<Name>Module.h`; `Class` defaults to `<Name>Module`. Mismatches surface at generated-registry compile after `py BuildUI.py --generate`.
- New module: copy `Modules/Template/`, rename files/class and update `Sources=`/`Headers=` (plus `Libraries=`/`Resources=` when needed), then `py BuildUI.py --generate`. Module dirs also need `INDEX.md` + `.index.json`. `Enabled=0` excludes at build; `Config/Config_Loader.json` (`{mode, modules[]}`) filters which compiled modules register at runtime (missing/empty list = all). Runtime config/log use manifest `ConfigFile`/`LogFile` (under `Config/`, `Log/`).
- Core/Services/UI/Pet/Tray must not `#include Modules/...`; depend on a Core interface + `dynamic_cast` instead, or that module can no longer be dropped by `--modules`.
- `ModuleManager` already remaps each `ContextMenuItem.itemId` to a unique id; `ModuleMenuGroup::items[].itemId` is that global id. Do not add another offset when rendering menus — a double offset dispatches the wrong item or nothing.
- `winsock2.h` **must** precede `windows.h` (see `Modules/WebAgent/WebAgentModule.h`); otherwise WinSock and `windows.h` types collide. Avoid `s_host`/`s_addr`/`s_net`-style identifiers — `inaddr.h` defines them as macros.

## Tests & debug

- `mingw32-make SHELL=cmd.exe test` builds **and runs** 11 EXEs (not part of `make all`). **`SHELL=cmd.exe` is mandatory** (test/clean recipes use `for %%t`/`del`; MSYS sh will error).
- New tests under `Debug/` are **not auto-discovered**: add a hardcoded Makefile rule, append the exe to `TEST_TARGETS`, and add it to `resources/tools/local/run-tests.ps1`. Tests compile real sources as units — keep Console/DBG out of what they link. `Debug/UI/GuiTest.cpp` is manual (no Makefile target).
- Quick runner: `powershell -ExecutionPolicy Bypass -File resources\tools\local\run-tests.ps1 [-Filter <name>] [-Rebuild]` (prepends toolchain PATH; hardcodes `D:\Project` and the MSYS2Portable path — edit if the checkout moves). Names: AutoKey, Tooltip, DebugState, MenuOrder, UpdateManager, Clipboard, Disconnect, Diagnostics, WebAgent, RemoteInit, PetDiscovery. Test exes need `ucrt64\bin` on PATH or fail with `0xC0000135`.
- DEBUG_CONSOLE build exposes AI-drivable pipe `\\.\pipe\ProjectDebug`: one UTF-16LE command per connection, returns captured `DBG()` output. Commands: `status`, `modules`, `mod <name> [config|log|cmd <id>]`, `log <category> [n]`, `tray status|show|hide|reset|killwindow`, `quit` (`quit` replies before exit).
- WebAgent serves browser bridge over HTTP (default `0.0.0.0:8921`; `/status`, `/dashboard`). Inert if `ResourceRoot` (`D:\Tampermonkey`) missing: no listener, `IsAvailable()` hides menu group/settings tab (machines without JS tree appear as if module not shipped).
- Crash handlers in `main.cpp` append to `Log/Crash.txt` (SEH, uncaught C++, `std::terminate`); `Log/*.txt` is written in Release too, but `DBG()`/`debug_log.txt` exist only in DEBUG_CONSOLE builds. `Log/` is derived from the running exe's path, not the source tree; a missing `shutdown completed` in `Log/Main.txt` with no `Crash.txt` means hang/clean-exit, not crash. Read `design/debugging-guide.md` and the playbooks before chasing a crash.
- Logging must never throw: `std::wofstream` aborts on CJK content, so log through the UTF-8 `Logger` (`WideCharToMultiByte`); never add a wide-char stream for user-supplied paths.
- A worker thread must not call `SetWindowPos`/`ShowWindow`/`SendMessage`/`DestroyWindow` on a UI-owned window (and a joined worker must finish without the joiner pumping); use `SWP_ASYNCWINDOWPOS` or `PostMessage`.

## Codegen, translations, icon

- `py BuildUI.py --generate` → `GeneratedBuild.mk`, `GeneratedModuleRegistry.*`, `GeneratedIcon.*`, `GeneratedVersion.*`, `Translation/<lang>.ini`. All `Generated*` and `Translation/*.ini` (except `_src/`, `en.ini`) are gitignored — never edit.
- English strings in code are translation keys for `Tr()`. Edit English first, then `_src/zh-TW.ini` (or `Pet/lang/`, `Modules/*/lang/`); merged into UTF-16 LE + BOM and embedded via `TranslationRes.rc`. `Translation/en.ini` is reference-only and never loaded. Module display names must go through `TranslationService::TrModuleLabel()`, not `GetDisplayName()`, and every `Modules/*/lang/zh-TW.ini` needs a `[TabLabels]` entry for it; `ModuleManager` does not translate context-menu labels, so modules must `Tr()` them before returning. `BuildUI.py` strips key/value whitespace — put separator spaces in C++ (`+ L" " +`), not in the INI.
- Icon: `BuildUI.py` takes **first alphabetically** root image (`.png/.ico/.jpg/.jpeg/.bmp`, ignoring `Pet.png`/`GeneratedIcon.*`) - stray image silently changes app icon; `Test.png` intended. PNG sources are converted by a built-in zlib/PNG decoder (no Pillow needed); Pillow is only a fallback for exotic layouts. `IDI_APP_ICON` defined once in `Core/ResourceIds.h`.

## Config files (gitignored, user-local)

`Config/*.ini`, `Config/*.json` (incl. `BuildUI.json`), `Flavors/`, `Log/`, `debug_log.txt`, `Build.log`, `.build-flags`, `MakePath.txt`, `ProjectName.txt` (display name), `TargetName.txt` (exe name), `HostHint.txt`, `Version.txt` (About text only — binary file version comes from `GeneratedVersion.rc`).

## Workflow

- Work in small, self-contained slices and commit each as soon as it is verified. A long multi-file pass can run out of tokens mid-way and leave everything uncommitted; one logical change per commit is recoverable, a half-finished sweep is not.
- `.agent/` is disposable scratch space for one-off scripts and logs (gitignored). Promote a helper to `resources/tools/local/` (and its INDEX) only once it proves stable and reusable.
