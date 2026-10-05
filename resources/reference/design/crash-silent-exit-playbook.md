# Silent Exit Playbook

Step-by-step for "the pet disappeared with no error dialog".

## Referenced by

- [debugging-guide.md](debugging-guide.md)

---

## Step 1 — classify the exit

`Log\Main.txt` is written in Release (`Core/Logger` is not `DEBUG_CONSOLE`-gated).

| Content | Classification | Next step |
|---|---|---|
| only `initialization started` | init failure, no window ever appeared | read `Log\Error.txt` |
| `initialization completed`, no `shutdown completed` | **crash** | go to Step 2 |
| both present | clean `WM_QUIT` exit | go to Step 4 |

`Log\` also holds `Crash.txt` whenever a crash handler ran, and `Error.txt` for
non-fatal failures.

No useful content at all → the process died before `Logger::Initialize`, or it is a
different binary than the one you think. Confirm which `.exe` the user runs:
`Logger::GetBaseDir()` derives `Log\` from `GetModuleFileNameW`, so logs land beside
the executable, not in the source tree.

## Step 2 — read `Log\Crash.txt`

`main.cpp` installs crash handlers before any service starts, so a crash is recorded.
`Crash.txt` is UTF-8 and append-only:

```
[2026-10-05 18:13:38] C++ exception
  type    : std::exception
  what    : Failed to open file: C:\...\BIOS.bin

[2026-10-05 18:13:48] SEH exception
  exception code : 0xC0000005
  exception addr : 0x00007FFA...
  info[0]        : 0x0000000000000000
  thread id      : 5736
```

Read it with UTF-8 encoding — `Get-Content` may show mojibake for non-ASCII.

| Entry | Meaning |
|---|---|
| `SEH exception`, code `0xC0000005` | access violation; `info[0]` is the faulting address |
| `C++ exception` | a `std::exception` escaped to `wWinMain`; `what()` is the message |
| `std::terminate` | terminate reached without a `what()` |
| nothing, with no `shutdown completed` in `Main.txt` | not a crash — go to Step 4 |

`HostApp::Run()` breaks the message loop on `GetMessageW` returning `-1` and writes
`GetMessageW failed; terminating message loop.` to `Log\Error.txt`.

## Step 3 — localize the crash

Once `Crash.txt` gives an exception code and (in a debug build) a stack:

- Build debug with `resources/tools/local/debug-build.ps1` or
  `mingw32-make CXXFLAGS_EXTRA=-DDEBUG_CONSOLE=1`.
- Reproduce, then read `debug_log.txt` — it holds the last `DBG()` before the fault.

If the crash is in a drop handler, do **not** synthesize the drop. Ask what menu
action was chosen, then call the handler's functions directly in a linked harness —
see "Simulate at the function level" in [debugging-guide.md](debugging-guide.md).

## Step 4 — clean exit

`WM_QUIT` has exactly three sources in Release:

| Source | Location |
|---|---|
| main window `WM_DESTROY` | `Pet/MainWindow.cpp:846` |
| context menu Exit (`ID_EXIT`) | `Pet/MainWindow.cpp:930` |
| debug console `quit` command | debug builds only |

If the user did not choose Exit, something destroyed the main window. Instrument the
drop path — `OnDropFiles` (`MainWindow.cpp:964`), `ShowDropActionMenu`
(`MainWindow.cpp:2290`), and `ModuleManager::GetDropActions` /
`ExecuteDropAction` (`Core/ModuleManager.cpp:82`/`:104`) currently write **no logs at
all**. Add `Logger::Write` calls to each and the ambiguity disappears.

Note `HostApp::Run()` uses `while (GetMessageW(...))` without checking `-1`: a
`GetMessageW` error silently ends the loop. It now breaks with an `Error` log entry
and returns 1.

## Reference table

| Symptom | Look at |
|---|---|
| exits after ~7 s in a **debug** build | AUTOTEST — intentional, `design/spec.md` §9 |
| dies on drop + "Get Checksum" + CJK path | `std::wofstream` + CJK — fixed in `Core/Logger.cpp` |
| `Log\Main.txt` stops mid-entry | process died inside that `Logger::Write` |
| `0xC0000135` running a test `.exe` | add `ucrt64\bin` to `PATH` |