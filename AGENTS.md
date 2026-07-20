# AGENTS.md — Project

Windows desktop pet with ROM plugin system. Win32 API + GDI+ + C++17, MinGW/g++.

See [INDEX.md](INDEX.md) for the full INDEX chain navigating all source directories and reference docs.

## Build commands

| Command | What it does |
|---|---|
| `mingw32-make` | Release build |
| `mingw32-make debug` | Clean rebuild with `DEBUG_CONSOLE=1` (AllocConsole, full DBG traces) |
| `mingw32-make test` | Build + run all test targets (AutoKeyTest, TooltipTest, DebugStateTest) |
| `mingw32-make rebuild` | `clean` then `all` |
| `mingw32-make distclean` | `clean` + delete `GeneratedBuild.mk`, `GeneratedIcon.*`, `Translation\zh-TW.ini` (triggers full regeneration; does NOT delete `GeneratedModuleRegistry.*`) |
| `mingw32-make CXXFLAGS_EXTRA=-DDEBUG_CONSOLE=1` | **CAUTION**: incremental only — `main.o` / `DebugConsole.o` may not recompile. Prefer `Build.bat -debug` instead. |
| `py Build.py` | Regenerate `GeneratedBuild.mk`, `GeneratedModuleRegistry.*`, merged translations |
| `Build.bat` | Release build via MakePath.txt or MSYS2 (`clean` + `py Build.py` + `make`) |
| `Build.bat -debug` | Debug build (same but `CXXFLAGS_EXTRA=-DDEBUG_CONSOLE=1` — full rebuild guaranteed) |
| `Build.bat trim` | Release build + trim intermediate files |
| `py ProjectPackager.py --mode report` | Generate packaging report |
| `py ProjectPackager.py --mode export-clean` | Export a clean copy of the project |
| `py ProjectSnapshot.py [--summary\|--full]` | Generate a project context snapshot |

- Makefile auto-runs `py Build.py` if `GeneratedBuild.mk` or `Translation/zh-TW.ini` is missing.
- `Build.py` scans `Modules/*/*.module.ini`. No manual Makefile edits needed to add a module.
- `Tests/GuiTest.cpp` exists but has no Makefile target — compile manually if needed.

### Shell environment

`mingw32-make` (MinGW-w64) uses `cmd.exe` as the shell **regardless** of the `SHELL` variable. All recipes must be cmd.exe-compatible:
- **No** `rm -f` → use `del /f`
- **No** POSIX `for f in ...; do ...; done` → use cmd.exe `for %f in (...) do @...`
- Paths need `$(subst /,\,...)` for `del` to work
- Clean target pattern: `-@del /f /q $(subst /,\,$(ALL_OBJS)) 2>nul`
