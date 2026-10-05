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
| `initialization completed`, no `shutdown completed` | **crash** | go to Step 3 |
| both present | clean `WM_QUIT` exit | go to Step 4 |

No useful content at all → the process died before `Logger::Initialize`, or it is a
different binary than the one you think. Confirm which `.exe` the user runs:
`Logger::GetBaseDir()` derives `Log\` from `GetModuleFileNameW`, so logs land beside
the executable, not in the source tree.

## Step 2 — add crash reporting before reproducing again

Without this, Step 1 is the only evidence available. In `main.cpp`, before
`HostApp`:

- `SetUnhandledExceptionFilter` → log the exception code and
  `GetExceptionInformation()` to `Log\Crash.txt`, then flush.
- `std::set_terminate` → log the terminate reason.

Both must flush explicitly; the process is about to die.

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