# Hang / Shutdown Deadlock Playbook

Step-by-step for "the app is still running but frozen" — Windows shows `AppHangB1`
in the Application event log, no dialog, no `Log\Crash.txt`. This is **not** the
silent-exit case; see [crash-silent-exit-playbook.md](crash-silent-exit-playbook.md)
for that one.

## Forward links

| Path | Description |
|------|-------------|
| [debugging-guide.md](debugging-guide.md) | Higher-level traps first |
| [crash-silent-exit-playbook.md](crash-silent-exit-playbook.md) | When the process actually dies |

## Referenced by

- [debugging-guide.md](debugging-guide.md)

---

## Step 1 — confirm it is a hang, not a crash

| Check | Hang | Crash |
|---|---|---|
| Process still in Task Manager | yes | no |
| `Log\Crash.txt` | absent | present |
| `Log\Main.txt` | `initialization completed`, **no** `shutdown completed` | same, but `Crash.txt` exists |
| Application event log | `AppHangB1`, `P1 = <exe name>` | `Application Error` / WER crash |

`initialization completed` + no `shutdown completed` + no `Crash.txt` + the process
alive = a stuck clean-exit path (usually `ModuleManager::ShutdownAllModules`).

## Step 2 — capture a dump

Preferred, because it needs no debugger attached before the freeze:

```powershell
# Task Manager: right-click Project.exe -> "Create dump file"
#   -> %TEMP%\Project.DMP
# Or via WER: an AppHang report archive lands under
#   C:\ProgramData\Microsoft\Windows\WER\ReportArchive\
```

To attach live (debug build on `PATH`), use the console debugger so the flag set is
reproducible and scriptable:

```powershell
cdb -p <pid> -c ".dump /ma D:\Project\.agent\hang.dmp; q"
```

`symfix; .reload` first so ntdll/kernel32 resolve.

## Step 3 — read the hang

```powershell
cdb -z D:\Project\.agent\hang.dmp -c ".symfix; .reload; !analyze -v -hang; ~*k; q"
```

- `BLOCKING_THREAD` / `!analyze -hang` names the thread Windows waited on. If that is
  **Thread 0 (UI)**, the shutdown path is stuck.
- `~*k` prints every thread. Read the blocking thread's frame first, then the others.

The window functions this app calls cross-thread (`SetWindowPos`, `ShowWindow`,
`SendMessage`, `DestroyWindow`, `GetWindowText`) send a message to the window's owner
and **block until the owner pumps**. That is the whole game — see Step 5.

## Step 4 — map MinGW RVAs to symbols (no PDB needed)

WinDbg prints `Project+0x<rva>`. MinGW puts DWARF in the debug `.exe`, so `nm`
recovers the symbols. Two rules:

- The image base is `0x140000000`; the address to look up is `0x140000000 + rva`.
- **Do not trust `addr2line` alone.** It reports the *nearest preceding line*, which
  names the wrong file/function when there is no exact entry (it returned
  `Release_PackageWindow.cpp` for a `RemoteControlModule` frame). Use `nm` symbol
  boundaries instead.

```powershell
$env:PATH = "D:\Program Files\MSYS2Portable\App\msys64\ucrt64\bin;$env:PATH"
# PowerShell '>' writes UTF-16 and breaks parsing -> force ASCII:
nm -n --defined-only Debug\Project.exe | Out-File -Encoding ascii syms.txt
```

Then binary-search the target VMA against the sorted symbol addresses and demangle
with `c++filt`. `.agent/map_offsets.py` is a ready-made script for this (edit the
`targets` list).

**Pitfall:** `nm ... > file` / `Out-File` default on Windows PowerShell 5.1 is UTF-16
LE, so a naive reader sees `\x00` between every character. Always pass
`-Encoding ascii` (or pipe through `cmd /c ... >`).

## Step 5 — the pattern: a `join()`ed worker calling another thread's window

Real case (`8cb024d`), reproduced from a WER AppHang dump:

```
T0 (UI):  DestroyLeds -> std::thread::join -> pthread_join -> WaitForSingleObject
#6 (LED): RunLeds -> UpdateLedPositions -> NtUserSetWindowPos
```

The UI thread is in `join()` (not pumping). The worker it is joining calls
`SetWindowPos` on `m_led`, a window **created by the UI thread**, so the worker blocks
until the UI thread pumps — which it never will. Circular wait.

Rules that follow (apply to every module with a worker thread):

- A worker thread must **never** make a synchronous call against a window it does not
  own. Use `SWP_ASYNCWINDOWPOS` on `SetWindowPos`, or `PostMessage` for other updates.
- Corollary: any thread you `join()` during shutdown must be able to finish without
  the joining thread doing anything. `detach()` hides the deadlock; it does not avoid
  the cross-thread call.
- Registry of the fix: `Modules/RemoteControl/RemoteControlModule.cpp:1011`
  (`SWP_ASYNCWINDOWPOS`) and `:1019` (epoch re-check after `Sleep` so shutdown skips
  the move).

## Step 6 — verify with a bounded repro harness

Do not eyeball it — assert the exit time. `.agent/hang-test.ps1` launches a build, waits
for `PetMainWindowClass`, posts `WM_CLOSE`, then `WaitForExit` with a hard timeout:

```powershell
powershell -File .agent\hang-test.ps1 -Exe D:\Project\Project.exe
# RESULT: CLEAN EXIT in ~400ms   vs   RESULT: HANG - still alive after 8000ms
```

- Keep the timeout short (seconds). A deadlock never resolves; a longer wait just
  wastes time.
- `FindWindow` was **unreliable cross-process** here (returned 0 while the window
  existed). Use `EnumWindows` + `GetWindowThreadProcessId` + `GetClassName` instead —
  that is what the harness does.
- `WM_CLOSE` on the pet window (`PetMainWindowClass`) reaches the same
  `WM_DESTROY -> PostQuitMessage` path as the tray Exit item.

## Reference table

| Symptom | Look at |
|---|---|
| `AppHangB1`, process alive, no `Crash.txt` | Step 1 — stuck shutdown, not a crash |
| `!analyze -hang` blocking thread = UI, `join()`/`WaitForSingleObject` frame | a worker it joins is blocked |
| worker frame in `NtUserSetWindowPos`/`NtUserShowWindow`/`NtUserMessageCall` | cross-thread window call — Step 5 |
| `Project+0x...` will not resolve in `addr2line` | use `nm` boundaries, base `0x140000000` |
| symbol file parses as `\x00` separated | PowerShell wrote UTF-16; use `-Encoding ascii` |
| `FindWindow` returns 0 for a live window | use `EnumWindows` |
| `mingw32-make test` dies with `syntax error near '('` | invoke with `SHELL=cmd.exe` |
