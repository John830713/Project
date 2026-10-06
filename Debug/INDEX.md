# Debug

Module-level debug content: unit tests, test utilities, and E2E GUI test.

Organized by module/category to mirror the source structure. Only compiled via dedicated Makefile targets — NOT built during `mingw32-make all`.

## Forward links

| Path | Description |
|------|-------------|
| [AutoKey/](AutoKey/INDEX.md) | AutoKey parser tests |
| [Core/](Core/INDEX.md) | Cross-cutting module debug tests |
| [UI/](UI/INDEX.md) | UI-layer test utilities |
| [RemoteControl/](RemoteControl/INDEX.md) | RemoteControl clipboard + disconnect tests |

## Build & Run

- `mingw32-make test` — builds and runs all test targets sequentially
- Test-specific CXXFLAGS: `-O0 -g -Wall -std=c++17 -DENABLE_DEBUG_STATE=1` (static linking)
- Test targets link against `-luser32 -lkernel32` only (no GDI+ or module libs); TooltipTest adds `-lcomctl32`, ClipboardTest adds `-lgdi32`, DisconnectTest adds `-lws2_32 -lole32 -luuid`, UpdateManagerTest adds `-lversion -lgdi32`

## Referenced by

- [INDEX.md](../INDEX.md)
