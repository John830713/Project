# Debug/Core

Cross-cutting module debug and state inspection tests.

## Files

| File | Role |
|------|------|
| `DebugStateTest.cpp` | Debug state dump tests. Target: `Debug/Core/DebugStateTest.exe` |
| `MenuOrderTest.cpp` | Menu ordering/unique ids/dispatch tests (compiles real `Core/ModuleManager.cpp`). Target: `Debug/Core/MenuOrderTest.exe` |
| `UpdateManagerTest.cpp` | Version ordering, rename-swap rollback, stale-backup cleanup (compiles real `Core/UpdateManager.cpp`). Target: `Debug/Core/UpdateManagerTest.exe` |

## Referenced by

- [Debug/INDEX.md](../INDEX.md)
