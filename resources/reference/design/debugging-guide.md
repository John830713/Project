# Debugging Guide

Traps that cost real debugging time in this codebase. Read before diagnosing a crash
or a "silent exit".

## Forward links

| Path | Description |
|------|-------------|
| [crash-silent-exit-playbook.md](crash-silent-exit-playbook.md) | Classify and localize a silent process exit |
| [hang-deadlock-playbook.md](hang-deadlock-playbook.md) | Capture and diagnose a freeze / shutdown deadlock (`AppHangB1`) |

## Referenced by

- [INDEX.md](../INDEX.md)

---

## 1. Crash reporting lives in `main.cpp` — read `Log\Crash.txt` first

`wWinMain` installs three handlers before touching any service:

| Handler | Catches |
|---|---|
| `SetUnhandledExceptionFilter(CrashFilter)` | SEH: access violations, stack overflow, divide-by-zero |
| top-level `try`/`catch` in `wWinMain` | C++ exceptions escaping `Initialize` / `Run` |
| `std::set_terminate(TerminateHandler)` | `std::terminate` as a last resort |

All three append to **`Log\Crash.txt`** (UTF-8) with a timestamp, exception code,
exception address, and for C++ exceptions the `what()` text. `CrashFilter` uses raw
`CreateFileW`/`WriteFile` rather than `Logger` or `std::ofstream`, because it runs with
the C++ runtime in an unknown state.

**Always read `Log\Crash.txt` before theorising.** The absence of a `Crash.txt` entry
alongside a missing `shutdown completed` in `Log\Main.txt` means the failure was not a
crash — look at the clean-exit path instead.

`Crash.txt` is append-only and never truncated, so entries from previous sessions
accumulate. Compare timestamps against `Log\Main.txt` to find the relevant one.

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
- Verify with `mingw32-make test` (8 test executables, 0 = pass). The `test` recipe
  uses cmd.exe syntax (`for %%t in (...) do`), so when invoking make from PowerShell
  pass `SHELL=cmd.exe`: `mingw32-make SHELL=cmd.exe test`. Without it make picks the
  MSYS `sh` and dies with `syntax error near unexpected token '('`.

## 8. A `join()`ed worker must not call another thread's window functions

A hang is a different symptom from a crash — record it separately. The one hit so far
(exit via the tray was `AppHangB1`, process alive, no `Crash.txt`) was:

```
T0 (UI):  DestroyLeds -> std::thread::join -> WaitForSingleObject
worker:   RunLeds -> UpdateLedPositions -> NtUserSetWindowPos   <-- blocked
```

`SetWindowPos`/`ShowWindow`/`SendMessage`/`DestroyWindow` on a window **owned by
another thread** block until that owner pumps its message queue. If the owner is the
UI thread sitting in `join()` (not pumping) and the joined worker is the one making
the call, neither can proceed.

- Fix pattern: `SWP_ASYNCWINDOWPOS` (or `PostMessage`) for cross-thread updates.
- Regression source: `8eca1f1` made the LED mover thread joinable; fixed in `8cb024d`.
- Full workflow, including WER dump capture, MinGW RVA→symbol mapping (`nm`, image base
  `0x140000000`), and a bounded repro harness: [hang-deadlock-playbook.md](hang-deadlock-playbook.md).