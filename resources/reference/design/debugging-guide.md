# Debugging Guide

Traps that cost real debugging time in this codebase. Read before diagnosing a crash
or a "silent exit".

## Forward links

| Path | Description |
|------|-------------|
| [crash-silent-exit-playbook.md](crash-silent-exit-playbook.md) | Classify and localize a silent process exit |

## Referenced by

- [INDEX.md](../INDEX.md)

---

## 1. The app has no crash reporting at all

`grep` for `SetUnhandledExceptionFilter`, `std::set_terminate`, `_set_abort_behavior`
across the project returns **nothing**.

Consequences:

- An access violation ends the process with zero trace.
- An uncaught C++ exception calls `std::terminate` — also silent.
- The binary is linked `-mwindows`, so there is **no console and no stderr**.

Any crash therefore looks identical to "the pet just vanished". Before hunting a
crash, add the exception handlers (see [playbook](crash-silent-exit-playbook.md))
so the next occurrence produces evidence.

## 2. `Log\Main.txt` classifies every exit

`Core/Logger` is **not** gated by `DEBUG_CONSOLE` — it writes `Log\*.txt` in Release
too. `Log\Main.txt` therefore tells you what kind of exit happened:

| `Log\Main.txt` content | Meaning | Where |
|---|---|---|
| only `initialization started` | `MainWindow::Create` failed, `wWinMain` returned 1 | `main.cpp:36`, message in `Log\Error.txt` |
| `initialization completed`, **no** `shutdown completed` | crash mid-run (no clean unwind) | `HostApp.cpp:77` vs `:118` |
| both lines present | clean exit — something posted `WM_QUIT` | `MainWindow.cpp:846` (WM_DESTROY) or `:930` (ID_EXIT) |

Release has no `debug_log.txt`: `DBG()` compiles to nothing unless
`-DDEBUG_CONSOLE=1` (`Core/DebugConsole.h:5-9`).

## 3. Never write a user-supplied path with `std::wofstream`

This is the single most expensive trap in the project.

`std::wofstream` throws `std::ios_failure`
(`basic_filebuf::_M_convert_to_external conversion error`) when the text contains
characters not representable in the external encoding. On a narrow-locale stream that
means **any CJK character**. If the throw is uncaught, the process aborts with no
dialog — so a one-line log call can kill the app.

Real instance: `ChecksumModule::ProcessFile` did
`Logger::WriteModuleLog(..., L"File     : " + filePath)`. Dropping a file from
`C:\Users\...\新增資料夾\BIOS.bin` aborted the process with exit code
`-1073740791`. Fixed in `Core/Logger.cpp` by converting to UTF-8 explicitly and
never letting the logger throw.

**Isolate path vs content before theorizing.** A CJK *path* is fine; a CJK *log
message* was fatal. Four probes settled it:

| Path | Content | Result |
|---|---|---|
| ASCII | ASCII | OK |
| **CJK** | ASCII | **OK** |
| ASCII | **CJK** | **THROW** |
| CJK | CJK | **THROW** |

Rules that follow:

- Log files are UTF-8 written via `WideCharToMultiByte` + `std::ofstream`.
- Any new logging helper must be non-throwing by contract.
- `std::ifstream`/`std::ofstream` with a `std::filesystem::path` are safe for CJK —
  `FileService` and `ChecksumService` read CJK paths correctly.

## 4. Simulate at the function level, not with fake input events

Do **not** try to reproduce a drag-drop by synthesizing input:

- Cross-process `PostMessageW(WM_DROPFILES)` fails with `ERROR_INVALID_HANDLE` (6).
- A hand-built `HDROP` is not a valid shell drop; it hangs inside `DragQueryFileW`
  or faults. Any crash it produces is an artifact of the harness, not the app.

Once the user narrows the report to a specific menu action, drop the UI
reproduction entirely and call the same functions the handler calls, in the same
order, linking the real `.cpp` files:

```cpp
Logger::Initialize(GetModuleHandleW(nullptr));
ChecksumService::CalculateFileChecksum(fs::path(p), checksum);
ClipboardService::CopyText(checksum);
Logger::WriteModuleLog(L"Checksum", L"File     : " + p);
```

Compile with the project include path and the real sources:

```powershell
$env:PATH = "D:\Program Files\MSYS2Portable\App\msys64\ucrt64\bin;$env:PATH"
g++ -std=c++17 -O0 -g -I. harness.cpp Core/Logger.cpp Services/ChecksumService.cpp -o harness.exe
```

Print a marker before each step so the last marker names the crash site. Note that
`std::wcout` output gets mangled when redirected — convert to UTF-8 with
`WideCharToMultiByte` and use `fwrite` instead.

A linked test binary needs `ucrt64\bin` on `PATH` or it fails to start with
`0xC0000135` (DLL not found).

## 5. Narrow the report before instrumenting

"Release build exits silently after ~7 seconds" is not a diagnosis. Two dead ends
came from treating details in the report as load-bearing:

- The ~7 s figure matches the `DEBUG_CONSOLE` AUTOTEST timer chain
  (`3000+600+800+800+500+800+300+500 = 7100 ms`, then `PostQuitMessage`) which is a
  **documented, intentional** feature (`design/spec.md`, "MainWindow auto-test"). It
  is compiled out of Release and was irrelevant to the actual bug.
- CJK path was assumed to be the cause before measuring.

Ask which menu item was chosen, whether any dialog appeared, and whether the timing
matters. The real bug was one line past the point everyone was looking at.

## 6. Read `resources/` before touching code

The INDEX chain documents intent that is not obvious from the source — including
that the AUTOTEST exists on purpose. Check in this order:

1. `resources/reference/project-layout.md`
2. `resources/reference/design/spec.md` — architecture, drop flow, debug gates
3. `resources/reference/modules-overview.md` — which modules implement
   `IDropActionProvider`
4. `resources/tools/local/` — `debug-build.ps1`, `view-log.ps1`, `run-tests.ps1`,
   `clean-debug.ps1`, `select-modules.ps1`

## 7. Build and run notes

- Toolchain: `D:\Program Files\MSYS2Portable\App\msys64\ucrt64\bin` (GCC 16.1.0,
  Make 4.4.1, windres 2.46). `MakePath.txt` points `Build.bat` at it; the scripts
  in `resources/tools/local/` call bare `mingw32-make`, so they need that directory
  prepended to `PATH`.
- `bash.exe` is absent from `usr\bin` — pacman is unusable, but the native UCRT64
  tools work standalone.
- Release idle test: launch `Project.exe`, wait ~12 s, confirm the process is alive
  and `Log\Main.txt` shows `initialization completed` with no `shutdown completed`.
- Verify with `mingw32-make test` (5 test executables, 0 = pass).