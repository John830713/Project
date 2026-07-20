# Tests

Unit tests and test utilities. Only compiled via dedicated Makefile targets — NOT built during `mingw32-make all`.

## Files

| File | Role |
|------|------|
| `AutoKeyTest.cpp` | AutoKey parser tests (65 cases). Target: `Tests/AutoKeyTest.exe` |
| `TooltipTest.cpp` | Tooltip window tests. Target: `Tests/TooltipTest.exe` |
| `DebugStateTest.cpp` | Debug state dump tests. Target: `Tests/DebugStateTest.exe` |
| `GuiTest.cpp` | GUI test (no Makefile target — compile manually if needed) |

## Build & Run

- `mingw32-make test` — builds and runs all 3 test targets sequentially
- Test-specific CXXFLAGS: `-O0 -g -Wall -std=c++17 -DENABLE_DEBUG_STATE=1` (static linking)
- Test targets link against `-luser32 -lkernel32` only (no GDI+ or module libs)
- `DebugStateTest` includes `Core/ModuleManager.cpp` directly to access `IDebugStateProvider`
- `AutoKeyTest` includes `Modules/AutoKey/AutoKeyParser.cpp` directly

## Referenced by

- [INDEX.md](../INDEX.md)
