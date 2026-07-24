# AGENTS.md — Project

Windows desktop pet with ROM plugin system. Win32 API + GDI+ + C++17, MinGW/g++.

## Navigation

- **INDEX chain**: [INDEX.md](INDEX.md) — full directory/reference map
- **Build & toolchain**: [reference/toolchain/INDEX.md](D:\Agent\resources\reference\toolchain\INDEX.md) (via INDEX chain)
- **Architecture**: [resources/reference/](resources/reference/INDEX.md) — read **project-layout.md** first

## Key facts

- After adding/removing any `.cpp` file or module, run `py Build.py` before building.
- `mingw32-make test` builds and runs all test targets (not included in `make all`).
- `Build.bat` is the one-click build entry point (clean → codegen → make). Use `Build.bat -debug` for debug builds.
- Makefile recipes use `cmd.exe` syntax regardless of `SHELL` setting — no POSIX shell constructs.
