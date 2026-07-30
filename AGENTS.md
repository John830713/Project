# AGENTS.md — Project

Windows desktop pet with ROM plugin system. Win32 API + GDI+ + C++17, MinGW/g++.

Read [INDEX.md](INDEX.md) and [resources/reference/project-layout.md](resources/reference/project-layout.md) first for directory map and architecture.

## Build

- **`Build.bat`** — one-click build (clean → `py Build.py` → `mingw32-make`). Adds `-debug` for debug (AllocConsole + DBG traces).
- **`Build.bat -debug`** / **`make debug`** — debug build with `-DDEBUG_CONSOLE=1`.
- **`mingw32-make test`** — builds **and runs** all test EXEs (skipped by `make all`).
- **`py Build.py`** — codegen: scans `Modules/*/*.module.ini`, writes `GeneratedBuild.mk`, `GeneratedModuleRegistry.*`, `GeneratedIcon.*`, merged translations. Run after adding/removing any `.cpp` or module.
- **`make clean` + `py Build.py` + `make`** — manual equivalent of `Build.bat`.
- Makefile uses **`cmd.exe`** syntax — no POSIX shell constructs.
- All `Generated*` files are auto-generated and **gitignored**.

## Module system

- Modules live in `Modules/<Name>/` with a `<Name>.module.ini` manifest:
  ```ini
  [Module]
  Name=AutoKey
  Class=AutoKeyModule
  Sources=AutoKeyParser.cpp,AutoKeyModule.cpp,AutoKeyDialog.cpp
  Libraries=
  Enabled=1
  ```
- Set `Enabled=0` to exclude from build without removing files.
- Each module gets its own `Config_<Name>.ini` (in `Config/`, gitignored) and log file.

## Tests

- Four test EXEs: `Debug/AutoKey/AutoKeyTest.exe`, `Debug/UI/TooltipTest.exe`, `Debug/Core/DebugStateTest.exe`, `Debug/Core/MenuOrderTest.exe`.
- Compiled with `-O0 -g -DENABLE_DEBUG_STATE=1`; linked against `-luser32 -lkernel32` only (no GDI+).
- `Debug/UI/GuiTest.exe` has no Makefile target — compile manually if needed.

## Codegen & translation

- `Build.py` merges `Translation/_src/<lang>.ini` + `Pet/lang/<lang>.ini` + `Modules/*/lang/<lang>.ini` → `Translation/<lang>.ini` (UTF-16 LE with BOM).
- `TranslationRes.rc` embeds `Translation/zh-TW.ini` as RCDATA.

## Config files

- `Config/*.ini` and `MakePath.txt`, `ProjectName.txt`, `TargetName.txt` are gitignored — user-local only.
- `Log/` directory is gitignored.
