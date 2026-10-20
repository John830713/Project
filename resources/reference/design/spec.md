# System Architecture

## 1. Module System (`IFeatureModule`)

Every module implements `IFeatureModule` (`Core/IFeatureModule.h`).

### Lifecycle

```
Initialize(hInst, host)
    ↓
LoadConfig()        ← reads Config_<Name>.ini
    ↓
ApplyConfig()       ← apply values to runtime state
    ↓
   ... module runs ...
    ↓
Shutdown()          ← calls SaveConfig()
```

### Registration

`Build.py` scans `Modules/*/*.module.ini` and generates:

| Generated file | Content |
|---|---|
| `GeneratedModuleRegistry.cpp` | `RegisterGeneratedModules()` — instantiates each module, checks LoaderConfig filter |
| `GeneratedBuild.mk` | `CPP_OBJS` + `LIBS_EXTRA` — all `.cpp` paths and per-module libs |

### Module INI format (`Modules/<Name>/<Name>.module.ini`)

```ini
[Module]
Name=NetworkInfo         ; must match GetModuleName()
Sources=NetworkInfoModule.cpp
Headers=NetworkInfoModule.h
Libraries=               ; extra linker flags, e.g. -liphlpapi
Enabled=1                ; considered by Build.py for optional exclusion
```

### Pure-virtual requirements

Every module must implement:

| Method | Returns | Notes |
|---|---|---|
| `GetModuleName()` | `const wchar_t*` | Used as key in LoaderConfig + registry filter |
| `GetDisplayName()` | `const wchar_t*` | Shown in Settings tab label |
| `GetConfigFileName()` | `const wchar_t*` | e.g. `Config_Foo.ini` |
| `GetConfigSectionName()` | `const wchar_t*` | e.g. `L"Foo"` |
| `GetLogSourceName()` | `const wchar_t*` | Prefix for `Logger::WriteModuleLog` |
| `GetConfigDefinitions()` | `vector<ConfigFieldDefinition>` | Field metadata for Settings UI |
| `Initialize()` | void | Store `hInst` + `host` |
| `LoadConfig()` | void | Usually delegates to `ConfigManager::LoadModuleConfig` |
| `SaveConfig()` | void | Usually delegates to `ConfigManager::SaveModuleConfig` |
| `ApplyConfig()` | void | Push config values into runtime state |
| `Shutdown()` | void | Usually calls `SaveConfig()` |
| `GetValue(key)` | `wstring` | Read single config value |
| `SetValue(key, value)` | void | Write single config value |

### Optional overrides

| Method | Default | Purpose |
|---|---|---|
| `GetVersion()` | `L"1.0.0"` | Displayed in About dialog |
| `GetPurpose()` | `L""` | One-line description |
| `GetContextMenuItems()` | `{}` | Returns right-click menu items for Function submenu |
| `ExecuteContextMenuItem(id)` | no-op | Handles click on context menu item |
| `HasCustomSettings()` | `false` | If true, `OpenCustomSettings()` is called from Settings |
| `OpenCustomSettings(parent)` | no-op | Opens custom settings dialog |

### Minimal module pattern

```cpp
class MyModule : public IFeatureModule {
    // identity + config methods omitted for brevity (see TemplateModule)
    std::vector<ContextMenuItem> GetContextMenuItems() const override {
        if (GetValue(L"Enabled") != L"1") return {};
        return { { 100, L"Open My Dialog..." } };
    }
    void ExecuteContextMenuItem(int itemId) override {
        if (itemId != 100 || !m_host) return;
        MyDialog dlg(m_host->GetMainWindow());
        dlg.Show();
    }
};
```

---

## 2. Context Menu Popup System

The right-click menu is **entirely custom-drawn** — no `HMENU` / `TrackPopupMenu`.

### Architecture

```
OnContextMessage (WM_CONTEXTMENU)
  └→ ShowContextPopup()
       └→ CreateWindow(CLASS_CONTEXT_POPUP, ContextPopupProc)
            ├── WM_PAINT:  iterate g_popupKind[], draw each item
            │              PIK_ACTION → label from g_popupCmd[] switch
            │              PIK_SUBMENU → "▸" arrow
            │              PIK_SEP → horizontal line
            │              PIK_SLIDER → trackbar
            ├── WM_MOUSEMOVE:  highlight hovered item, start auto-open timer
            └── WM_LBUTTONDOWN:
                 ├─ PIK_ACTION  → HandleMessage(WM_COMMAND, g_popupCmd[i])
                 ├─ PIK_SUBMENU (Pet) → PetSubPopup (g_petKind[])
                 └─ PIK_SUBMENU (Function) → CreateSubPopup from ModuleManager::GetMenuGroups()
```

### Popup descriptor arrays

Static arrays in `MainWindow.cpp` define the main menu structure:

```cpp
static PopupItemKind g_popupKind[] = {
    PIK_SUBMENU,  // 0: Pet ▸
    PIK_SUBMENU,  // 1: Function ▸  (modules register here)
    PIK_SEP,      // 2:
    PIK_ACTION,   // 3: Settings...
    PIK_ACTION,   // 4: About...
    PIK_SEP,      // 5:
    PIK_ACTION,   // 6: Exit
};
static int g_popupCmd[] = { 0, 0, 0, FILE_ID_SETTINGS, FILE_ID_ABOUT, 0, FILE_ID_EXIT };
```

Item constants: `FILE_ID_xxx` (static constexpr) match `ID_xxx` (enum in `MainWindow.h`).

| Constant | Value | Handler |
|---|---|---|
| `FILE_ID_EXIT` | 100 | `PostQuitMessage(0)` |
| `FILE_ID_SETTINGS` | 101 | `OpenSettings(hwnd)` |
| `FILE_ID_TOPMOST` | 102 | `ToggleTopmost()` |
| `FILE_ID_ABOUT` | 9999 | `ShowAboutDialog(hwnd)` |

### Dynamic submenu (Function ▸)

Modules provide items via `IFeatureModule::GetContextMenuItems()`. `ModuleManager::GetMenuGroups()` assigns global `uniqueId` = `ID_MENU_BASE + index`. Click dispatch: `HandleMessage(WM_COMMAND, uniqueId)` → `m_moduleManager->ExecuteContextMenuItem(uniqueId - ID_MENU_BASE)` → routes to original module + itemId.

### Menu order control

Modules appear in Function submenu sorted by priority from `Config_Pet.ini [MenuOrder]`:

```ini
[MenuOrder]
AutoKey=100
ChangeEC=200
Checksum=300
NetworkInfo=400
RemoteControl=500
RomCombiner=600
RomSeparator=700
```

- Lower number = higher priority (appears first)
- Default priority = 999 if not specified
- `GetModulePriority()` reads from `Config_Pet.ini [MenuOrder]` using `GetPrivateProfileStringW`
- `GetMenuGroups()` uses `std::stable_sort` by priority, then assigns sequential `uniqueId`
- Users can edit via Pet submenu → Edit Order → `MenuOrderDialog` popup

### Pet submenu

Static array `g_petKind[]` in `MainWindow.cpp`:

```cpp
static PopupItemKind g_petKind[] = {
    PIK_SUBMENU,  // 0: Always on Top
    PIK_SUBMENU,  // 1: Move ▸
    PIK_SEP,      // 2:
    PIK_SUBMENU,  // 3: Opacity ▸
    PIK_SUBMENU,  // 4: Scale ▸
    PIK_SEP,      // 5:
    PIK_ACTION,   // 6: Edit Order
};
```

File IDs: `ID_PET_TOPMOST=103`, `ID_PET_EDIT_ORDER=108`

### SubPopupProc

Reusable popup window for module submenus. Uses `std::vector<PopupItem>` for layout + dispatch. Same WM_PAINT/WM_MOUSEMOVE/WM_LBUTTONDOWN pattern.

---

## 3. Config System

### ConfigFieldDefinition

```cpp
struct ConfigFieldDefinition {
    std::wstring key;           // INI key name
    std::wstring label;         // Tr(L"Common", label) for Settings UI
    ConfigValueType type;       // Bool → checkbox, Int/String → edit, Directory → browse
    std::wstring defaultValue;
    int minValue;
    int maxValue;
};
```

### INI persistence

`ConfigManager::LoadModuleConfig(file, section, definitions)` → returns `map<key, value>`.
`ConfigManager::SaveModuleConfig(file, section, map)` → writes each key=value.

- Files stored in project root as `Config_<ModuleName>.ini`
- Uses Win32 `GetPrivateProfileStringW` / `WritePrivateProfileStringW`
- Missing keys auto-filled with defaults

### Settings dialog

`SettingsDialog` auto-generates the UI from `GetConfigDefinitions()`:
- Tab 0 = Pet config (static layout)
- Tab 1..N = per-module tabs (dynamic layout)
- Bool → checkbox, Int/String → edit box (Int with trackbar), Directory → edit + browse button
- Scrollbar if fields exceed visible area

---

## 4. Build System

`Build.py` orchestrates:

1. **Scan** `Modules/*/*.module.ini` for module definitions
2. **Scan** `Core/`, `Services/`, `UI/`, `Pet/`, `Modules/*/` for `*.cpp`
3. **Generate** `GeneratedModuleRegistry.cpp` — include each module header, create `RegisterGeneratedModules()` with LoaderConfig filtering
4. **Generate** `GeneratedBuild.mk` — `CPP_OBJS` list, `LIBS_EXTRA` concatenated from all module `.ini` files
5. **Generate** `GeneratedIcon.*` — convert `Test.png` to `.ico` + `.rc`
6. **Merge** translations — collect `*/lang/zh-TW.ini` into `Translation/zh-TW.ini`

Run `py Build.py` after adding/removing any `.cpp` file or module.

---

## 5. Dialog Patterns

All dialogs are created programmatically (no `.rc` resource scripts).

### Modal dialog pattern

```cpp
class MyDialog {
    HWND m_hwnd;
    HWND m_parent;
public:
    MyDialog(HWND parent) : m_hwnd(nullptr), m_parent(parent) {}
    INT_PTR Show() {
        // Register window class (once)
        WNDCLASSW wc = {};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        wc.lpszClassName = L"MyDialogClass";
        RegisterClassW(&wc);

        // Create window
        m_hwnd = CreateWindowExW(0, L"MyDialogClass", L"Title",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            x, y, w, h, m_parent, nullptr, GetModuleHandleW(nullptr), this);
        if (!m_hwnd) return -1;

        ShowWindow(m_hwnd, SW_SHOW);

        // Modal message loop
        MSG msg;
        while (IsWindow(m_hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return 0;
    }
};
```

### WM_CREATE + GWLP_USERDATA pattern

```cpp
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto* self = static_cast<MyDialog*>(((CREATESTRUCTW*)lParam)->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->Init(hwnd);   // create child controls
        return 0;
    }
    auto* self = (MyDialog*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (self) return self->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
```

### Child control creation

All controls use `CreateWindowW` / `CreateWindowExW` with `WS_CHILD | WS_VISIBLE`.
Common control classes:

| Class | Include | Init |
|---|---|---|
| `L"BUTTON"` | `<windows.h>` | — |
| `L"EDIT"` | `<windows.h>` | — |
| `L"STATIC"` | `<windows.h>` | — |
| `WC_LISTVIEWW` | `<commctrl.h>` | `InitCommonControlsEx(ICC_LISTVIEW_CLASSES)` |
| `WC_TABCONTROLW` | `<commctrl.h>` | `InitCommonControlsEx(ICC_TAB_CLASSES)` |
| `TRACKBAR_CLASS` | `<commctrl.h>` | `InitCommonControlsEx(ICC_BAR_CLASSES)` |

---

## 6. Translation System

### Lookup

`TranslationService::Get()->Tr(L"Section", L"Key")` → looks up `[Section]` in `Translation/zh-TW.ini` → returns translated text or fallback to `Key`.

### Merge process

`Build.py` collects all `*/lang/zh-TW.ini` files and merges them into a single `Translation/zh-TW.ini`. Each module adds its `Modules/<Name>/lang/zh-TW.ini` with section matching `GetModuleName()`. Pet translations live in `Pet/lang/zh-TW.ini` under `[Pet]` section.

### Section naming convention

| Source | INI Section |
|---|---|
| Pet menu labels | `[Pet]` |
| Each module | `[<ModuleName>]` (e.g. `[AutoKey]`, `[NetworkInfo]`) |
| Settings field labels | `[Common]` (shared across modules) |
| Tab labels | `[TabLabels]` |

---

## 7. Pet Window (MainWindow)

### Window properties

- **Style**: `WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_ACCEPTFILES | WS_POPUP`
- **Rendering**: `UpdateLayeredWindow(ULW_ALPHA)` — per-pixel alpha + `SourceConstantAlpha` for opacity
- **Image**: `Pet.png` loaded via GDI+ `Bitmap::FromFile` → `GetHBITMAP` → memory DC → `UpdateLayeredWindow`
- **Fallback**: 64×64 green rectangle if `Pet.png` missing
- **Dragging**: `WM_NCHITTEST` returns `HTCAPTION` — entire window acts as title bar
- **Position**: Persisted to `Config_Pet.ini` on `WM_EXITSIZEMOVE`, restored on startup (centered if `-1`)

### Message loop

Standard `GetMessageW` loop in `HostApp::Run()`. No dedicated game loop. Motion driven by `SetTimer` (ID=1, interval=`m_moveSpeed`).

### Movement (OnMoveTimer)

- Random walk: 15%/tick change direction, 3%/tick pause 1-5s
- Edge behavior: bounce (reverse direction) or shuttle (wrap around)
- Config: `moveEnabled`, `moveStep`, `moveSpeed`, `moveShuttle`

### Edge snap

When dragged within threshold of screen edge → shrink to 24×24 indicator dot (rounded rect + LED color). Timer 2 pulses at 250ms. Pauses auto-movement.

### Context popup system

Entirely custom-drawn (no `HMENU`/`TrackPopupMenu` for main menu):

```
Root (ContextPopupProc): [Pet▸, Function▸, ---, Settings, About, ---, Exit]
  ├── Pet▸ (PetPopupProc): [Always on Top, Move▸, ---, Opacity▸, Scale▸]
  │     ├── Move▸ (SubPopupProc): [Still, Move▸, Step▸, Speed▸, Shuttle]
  │     │     ├── Step▸ (SliderSubPopupProc): trackbar + value
  │     │     └── Speed▸ (SliderSubPopupProc): trackbar + value
  │     ├── Opacity▸ (SliderSubPopupProc): trackbar + live preview
  │     └── Scale▸ (SliderSubPopupProc): trackbar + live preview
  └── Function▸ (SubPopupProc): module-provided items from ModuleManager::GetMenuGroups()
```

- Submenus open on **300ms hover timer** (not click)
- Popup tree dismisses via `WM_ACTIVATE(WA_INACTIVE)` — sibling check prevents cascade-close
- `CloseContextPopup()` cascades through all tracked HWNDs
- Sliders: `MA_NOACTIVATE` subclass on trackbar to prevent focus stealing
- Committed on popup dismiss or reverted via -1 sentinel

### Drag-drop

`WS_EX_ACCEPTFILES` → `WM_DROPFILES` → `OnDropFiles`:
1. Extract files into `DropContext`
2. `ModuleManager::GetDropActions(ctx)` — queries `IDropActionProvider` from all modules
3. If >1 action: `ShowDropActionMenu()` — Win32 `TrackPopupMenu` (only place standard menu is used)
4. `ModuleManager::ExecuteDropAction(resolved, ctx)`

### Edge snap indicator

`CreateSnapIndicator()`: 24×24 GDI+ bitmap with rounded rect gradient + LED dot:
- Green = moving, Gray = still
- RemoteControl module can set color via `SetSnapLedColor()`
- Timer 2 pulses every 250ms

### Custom WM_APP messages

| Message | wParam | Purpose |
|---------|--------|---------|
| `WM_APP_SLIDER_CHANGE` (WM_APP+2) | `ID_PET_OPACITY` (110) / `ID_PET_SCALE` (111) / `ID_PET_MOVE_STEP` (105) / `ID_PET_MOVE_SPEED` (106) | Slider value changed (+ new value or -1 to cancel) |
| `WM_APP_RELOAD_PETCONFIG` (WM_APP+3) | — | Re-read `Config_Pet.ini` from disk |
| `WM_APP_CONTEXT_ACTION` (WM_APP+4) | — | Context popup tree dismissed |

---

## 8. Logging

`Core/Logger` writes to `Log/` as **UTF-8** (converted via `WideCharToMultiByte`).

Log files are opened with `std::ofstream`, not `std::wofstream`. A `std::wofstream` throws
`std::ios_failure` ("_M_convert_to_external conversion error") when the message contains
characters not representable in the external encoding — which happens for any CJK path on a
narrow-locale stream. An uncaught `std::ios_failure` from a logging call aborts the process
with `std::terminate` and **no dialog**, so writing a user-supplied path to a log is enough
to kill the app.

`Logger::Write` / `WriteModuleLog` therefore convert to UTF-8 explicitly and never throw —
logging must not be able to take down the caller.

### Crash reporting (`main.cpp`)

Crash records bypass `Logger` entirely: they are written with raw `CreateFileW` / `WriteFile`
(in append mode) so a broken Logger can never mask the failure it is meant to report.

| Hook | Catches | Output |
|------|---------|--------|
| `SetUnhandledExceptionFilter(CrashFilter)` | SEH: access violations, stack overflow, divide-by-zero | `Log\Crash.txt` |
| `std::set_terminate(TerminateHandler)` | Uncaught C++ exceptions (e.g. `std::stoi` on garbage config) | `Log\Crash.txt` |
| top-level `try/catch` in `wWinMain` | Anything thrown during startup/teardown | `Log\Crash.txt` |
| `GetMessageW == -1` check in the message loop | Corrupted message queue; the old code ignored the return and looped forever | `Log\Error.txt` |

`Log\Crash.txt` is **UTF-8, append-only, one record per crash** (timestamp + exception code +
message; CJK paths preserved). When a session ends without a normal exit, read it first —
see [debugging-guide.md](debugging-guide.md) §1 and [crash-silent-exit-playbook.md](crash-silent-exit-playbook.md).

---

## 9. Debug System

### Two compile-time gates

| Gate | Build flag | Effect |
|------|-----------|--------|
| `DEBUG_CONSOLE` | `CXXFLAGS_EXTRA=-DDEBUG_CONSOLE=1` | `AllocConsole`, `DBG()` macro, interactive CLI + pipe server |
| `ENABLE_DEBUG_STATE` | `-DENABLE_DEBUG_STATE=1` | `IDebugStateProvider` + `DebugGetState()` (test builds only) |
| `PET_AUTOTEST` | `-DPET_AUTOTEST=1` | Pet context-menu click-through auto-test; self-quits after it runs |

### Debug console (`Core/DebugConsole.h/.cpp`)

**DBG macro**: `wprintf` to console + `fwprintf` to `debug_log.txt`. Both `fflush()` after each write. Zero-cost no-op when `DEBUG_CONSOLE=0`.

**Interactive commands** (typed in AllocConsole window, processed on background reader thread):

| Command | Description |
|---------|-------------|
| `help` | List commands |
| `modules` / `lsmod` | List registered modules |
| `mod <name>` | Show module info + context menu items |
| `mod <name> config` | Dump all config keys |
| `mod <name> log [n]` | Last n lines of module log |
| `mod <name> cmd <id>` | Execute context menu item by original ID |
| `log <category> [n]` | Last n lines of arbitrary log |
| `status` | Window position, size, topmost, module count |
| `tray <status\|show\|hide\|reset\|killwindow>` | Inspect / drive the tray + pet window (see §12) |
| `clear` | Clear console |
| `quit` / `exit` | Post WM_QUIT, graceful shutdown |

**Console reader thread**: Background thread reads stdin. Commands dispatched to main thread via `PostThreadMessageW(WM_APP_CONSOLE_CMD)`. Quit/exit post `WM_QUIT` to main thread.

**AI-drivable pipe server** (`StartPipeServer` / `StopPipeServer`): a second
entry point into the same `RunConsoleCommand` path, for automated agents that
cannot type into the `AllocConsole` window.

- Named pipe `\\.\pipe\ProjectDebug`. One UTF-16LE command line per connection;
  the server returns the captured `DBG()` output, then closes.
- Protocol is deliberately trivial (connect → write → read → close) so a
  PowerShell/Python caller needs no handshake.
- Requests are marshalled to the main thread via `WM_APP_CONSOLE_PIPE_CMD`
  (a `PipeRequest` pointer); the command runs there because
  `RunConsoleCommand` is not thread-safe and tray commands touch
  main-thread-owned windows. The pipe thread waits on a `doneEvent`
  (bounded, 15 s) and answers the client instead of hanging if the main
  thread is wedged.
- `DBG()` mirrors its text into a capture buffer while
  `DebugConsoleBeginCapture()` / `DebugConsoleEndCapture()` bracket a
  request, so the caller sees exactly what the console prints.
- The server keeps a fresh listening instance ready before serving the
  current client (canonical multi-instance pattern) and uses
  `PeekNamedPipe` before `ReadFile`, so a client that connects then aborts
  cannot wedge the thread.
- Debug-only: no-op inline stubs when `DEBUG_CONSOLE=0`.

**`HostApp` wiring**: `Run()` calls `DebugConsoleSetHost(this)` then
`StartPipeServer()` before the message loop and `StopPipeServer()` after.
The host pointer lets tray commands reach `HostApp` state
(`IsTrayIconAdded`, `GetTrayWatchdogTicks`, `DebugDestroyMainWindow`).

### Debug state (`IDebugStateProvider`)

- Single method: `std::wstring DebugGetState() const`
- Only `AutoKeyModule` implements it (production)
- `ModuleManager::DebugGetState()` iterates modules, `dynamic_cast`s to `IDebugStateProvider*`, collects text dumps
- Guarded by `#ifdef ENABLE_DEBUG_STATE`

### Test infrastructure

- **8 test targets** built via `make test` (not included in `make all`)
- Each test is a standalone `.exe` with minimal macro-based framework (`ASSERT_EQ`, `ASSERT_TRUE`, etc.)
- Tests use `-DENABLE_DEBUG_STATE=1` and `-O0 -g` (no `DEBUG_CONSOLE`)
- Return 0 = pass, 1 = fail
- New test files under `Debug/` are **not auto-discovered**: add a hardcoded Makefile rule
  (see AGENTS.md)

| Test | File | Cases |
|------|------|-------|
| AutoKeyTest | `Debug/AutoKey/AutoKeyTest.cpp` | 65 — parser, VK names, modifiers, edge cases, INI roundtrip |
| TooltipTest | `Debug/UI/TooltipTest.cpp` | 17 — Win32 tooltip cbSize, TTF_TRACK, activation ordering, mouse-move triggers |
| DebugStateTest | `Debug/Core/DebugStateTest.cpp` | 9 — empty manager, module with/without state, direct query |
| MenuOrderTest | `Debug/Core/MenuOrderTest.cpp` | 13 — menu ordering, unique ids, execute dispatch |
| ClipboardTest | `Debug/RemoteControl/ClipboardTest.cpp` | 32 — clipboard hook/replay roundtrip (links real `RemoteControlHook.cpp`) |
| DisconnectTest | `Debug/RemoteControl/DisconnectTest.cpp` | 54 — dead-peer timeouts, self-deadlock, keepalive, epoch generations, reconnect survival, spawn-once, Stop latency (links real `NetworkService.cpp` + `FileTransferService.cpp`) |
| UpdateManagerTest | `Debug/Core/UpdateManagerTest.cpp` | 29 — version ordering, rename-swap + rollback, stale-backup cleanup (links real `Core/UpdateManager.cpp`) |
| DiagnosticsTest | `Debug/RemoteControl/DiagnosticsTest.cpp` | 35 — dbgReq/dbgResp/exeSwap/exeResult pack+unpack, seq survives, unknown-type passthrough, snapshot formatting (links real `Services/NetworkService.cpp`) |
| GuiTest | `Debug/UI/GuiTest.cpp` | No Makefile target — manual compile. E2E: find pet window, open Settings, manage actions, verify persistence. |

### MainWindow auto-test

When `PET_AUTOTEST` is defined, a timer (ID=99) fires 3s after startup and runs an 8-step automated UI test: open context menu → navigate Pet>Move>Step → set trackbar to 42 → close → verify `m_moveStep == 42`. Logs PASS/FAIL then `PostQuitMessage`. (Gated on `PET_AUTOTEST`, not `DEBUG_CONSOLE` — the self-quit is fatal for manual/agent testing of a `DEBUG_CONSOLE` build.)

---

## 10. Single-Instance & In-Place Executable Update

`Core/UpdateManager` keeps exactly one running image and lets a newer build replace the
on-disk executable **while the old instance is still running**. Windows blocks writing or
deleting a loaded image but allows renaming it — every swap is rename-based, so the pet and
any RemoteControl session are never interrupted.

### Version source (`Build.py` → `GeneratedVersion.rc`)

Each build embeds an auto-generated VERSIONINFO: `FILEVERSION (YYYY, MMDD, HHMM, SS*100+seq)` packed into `a.b.c.d`, with a sequence suffix so two builds in the same second still compare as different. A build **without** a resource (old/partial builds) reads as `0.0.0.0` and is therefore replaceable by any versioned build. `GeneratedVersion.*` is gitignored and removed by `make distclean`.

### Startup gate (`main.cpp` → `UpdateManager::BeginSession`)

Called after `ConfigManager::Initialize` / `Logger::Initialize` (crash handlers already installed) and an early `TranslationService::Load(PetConfig::Load().language)` — **before the main window exists**. Returning `false` exits `wWinMain` with 0; a second copy never starts.

| Situation | Detection | Action |
|-----------|-----------|--------|
| No other instance | mutex free + no peer process | keep mutex, `RemoveStaleBackups`, run normally |
| Peer running, this build **≤ on-disk** | file versions | 2-second notice (with both versions), exit |
| Peer running, this build **newer** | file versions | rename-swap at the peer's path, 2-second "update takes effect on next launch" notice, exit |
| Mutex set but peer path unqueryable | `QueryFullProcessImageNameW` fails | "already running" notice, exit |

- Peer discovery is a **Toolhelp32 process scan keyed on the executable name** (`FindPeerProcess`), not just the mutex — it detects builds that predate this feature. The PEB image path is fixed at process creation, so even after an earlier rename-swap the running instance still reports the original registered path: always the right swap target.
- The notice is a self-owned `WS_POPUP` with its own message loop and a 2 s `WM_TIMER` (`MessageBox` has no built-in timeout). Strings come from the `[Update]` translation section via an injected `NoticeTranslator` (`TranslationService` is loaded early just for this).

### Rename-and-swap (`SwapExecutable`)

1. `MoveFileW(target → <exe>.<YYYYMMDDHHMMSS>.old)` — renaming a loaded image is allowed
2. `CopyFileW(new build → target)`
3. copy failed → `MoveFileW(backup → target)` rollback; the error notice states whether the original was restored

Target missing (nothing installed at the registered path): plain copy, no backup. Stale `*.old` backups are cleaned by the next primary launch (`RemoveStaleBackups`); a file still locked by a running old image is skipped (`DeleteFileW` fails) and retried on the following launch.

### Resume state (`RemoteControlModule`)

The start/stop context-menu handlers persist a `ResumeState` flag (0/1) straight in `Config_RemoteControl.ini` — deliberately **outside** `m_definitions`, so it never appears in the settings UI and `SaveConfig` never touches it. `ApplyConfig` starts RemoteControl when `AutoStart` **or** `ResumeState` is set, and `Shutdown()`/the destructor stop the service **without** clearing the flag. A session that ends while RemoteControl is active (including a restart after an in-place update) therefore resumes where it left off; only an explicit menu **Stop** writes 0.

Covered by `UpdateManagerTest` (29 checks).

---

## 11. Remote Diagnostics & Deploy Backdoor (`RemoteControlModule`)

A management channel that rides the existing RemoteControl TCP session **outside** the F9
control state. Either role may query the other's runtime snapshot or push a newer build;
older peers simply skip the unknown frame types (no default case). All reports land in
`Log/RemoteDiagnostics.txt` on both sides, so a support agent needs only one side's
workspace. The three context-menu commands map to menu ids 2 (Diagnostics) and 3 (Deploy).

| Frame | Payload | Direction |
|-------|---------|-----------|
| `DBG_REQ` (16) | `[seq:2]` | requester → responder |
| `DBG_RESP` (17) | `[seq:2][snapshot text]` | responder → requester |
| `EXE_SWAP` (18) | `[size:8][filename]` | deployer → target, **sent before the file** |
| `EXE_RESULT` (19) | `[ok:1][message]` | target → deployer |

- The requester records the `seq` with a send tick and measures RTT against the echo
  (clock-sync free); pending requests time out after 5 s and log `TIMEOUT` to the report.
- The snapshot (`CollectSnapshot`) is a plain-text block: session line, runtime uptime,
  socket state, thread liveness, wire counters, a UI liveness probe (cross-thread
  `SendMessageTimeout`) and the receiver's own log tail.
- Deploy: `DeployMyBuild` announces `EXE_SWAP` **before** queueing the file. The target's
  worker snapshots the old mtime of `SavePath\filename`, then polls until the size matches
  **and** the mtime proves a fresh write (a leftover from an earlier deploy can sit there
  with the same size), re-reads the version until it parses (mid-flush garbage reads as
  `0.0.0.0`), checks `IsNewer(incoming, own)`, then does the rename-swap via
  `UpdateManager`. Refusals follow the path-traversal / busy / version-gate rules and
  always reply with `EXE_RESULT ok=0`; success swaps the running image and replies `ok=1`
  (`DEPLOY ACCEPTED`).

Handled in **both** recv loops (`RunSvRecv` and the client-side loop), so a server, a
controller, or a disconnected target can all be probed or upgraded. `WireTap=1` mirrors
every frame (outbound `TX` / inbound `RX`) into the module log with its type and length.

Covered by `DiagnosticsTest` (35 checks) and the six-phase E2E in
`.agent/e2e-backdoor.ps1` (boot+connect, A→B diag, B→A diag, version-gate refusal,
version bump + restart + reconnect, successful swap).

---

## 12. Tray System (`Tray/TrayHost`)

The application-shell layer. It gives the process a presence in the notification
area and a way back to the pet when the pet body itself is hidden, and it can
rebuild the pet window after an in-process failure.

### Ownership & lifecycle

`HostApp` owns a `TrayHost` by value (`HostApp.h`), constructed with `this`. In
`HostApp::Initialize` the tray is created **after** modules initialize/config/apply
(`m_trayHost.Initialize()`), and in `HostApp::Run` it is shut down **before**
modules (`m_trayHost.Shutdown()` → `m_moduleManager.ShutdownAllModules()`). The
tray is deliberately attached at the `HostApp` layer, not the `MainWindow` layer,
so it also works in a future "clean" mode with no pet.

`TrayHost::Initialize` registers `TrayHostMessageWindow` and creates an
`HWND_MESSAGE`-only window (parent `HWND_MESSAGE`, style 0). A `SetTimer(1, 3000)`
watchdog tick runs for the window's whole life. The tray callback message is
`WM_APP + 12` (`kTrayCallbackMessage`), deliberately **not** `WM_APP + 10`
(which is `WM_APP_CONSOLE_CMD`, §11) so the two never alias.

### Icon & resource ID

The icon comes from `LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), …)`
(or `LoadIconW`), falling back to `IDI_APPLICATION`. `IDI_APP_ICON` is defined
only in `Core/ResourceIds.h`; `Build.py` writes `GeneratedIcon.rc` as
`#include "Core/ResourceIds.h"` + `IDI_APP_ICON ICON "GeneratedIcon.ico"`. That
header is the single definition for both the RC (windres) and C++, so the old
windres implicit auto-assignment (which made `#define IDI_APP_ICON 101` in a
comment meaningless) no longer applies.

### Menu & command IDs

Right-click (and `WM_CONTEXTMENU`) on the icon opens a standard `TrackPopupMenu`
menu (the pet's own menu is custom-drawn; the tray uses a plain `HMENU` so the
shell renders it):

```
Show Pet / Hide Pet     — toggle pet visibility (double-click also toggles)
Settings                — open the pet settings dialog
Reset Pet Window        — recreate the pet window in-process
---
<module groups>         — GetMenuGroups(), one submenu per module
---
Exit
```

The tray takes its own high command band so it cannot collide with the pet
window's `100–111` / `2000+` / `6000+` / `9999`:

| ID | Constant | Action |
|----|----------|--------|
| `12000` | `ID_TRAY_TOGGLE_PET` | Toggle visibility |
| `12001` | `ID_TRAY_SETTINGS` | Settings (shows the pet first) |
| `12003` | `ID_TRAY_RESET_PET` | Recreate the pet window |
| `12100` | `ID_TRAY_MODULE_BASE` | Base for module items (offset by module-item index) |
| `12999` | `ID_TRAY_EXIT` | `PostQuitMessage(0)` |

Module items reuse the existing `GetMenuGroups()` / `ExecuteContextMenuItem`
path: the tray re-runs `GetMenuGroups()` each time the menu opens, assigns
`ID_TRAY_MODULE_BASE + runningIndex`, and dispatches `cmd - ID_TRAY_MODULE_BASE`.
`ContextMenuItem::flags` uses the shared `kContextMenuItemDisabled` /
`kContextMenuItemChecked` bits (`Core/IFeatureModule.h`) so the pet window and
the tray agree without one including the other. The tray shows no tooltips.

### Visibility persistence

`Config/Config_Tray.ini` `[Tray] petVisible` (`1`/`0`, definitions in
`HostApp.cpp`) records whether the pet window should be visible. `Initialize`
reads it and starts the window `SW_HIDE` when off; double-clicking the icon
calls `SetMainWindowVisible()` which `ShowWindow`s and saves the flag, so a
hidden pet stays hidden across restarts.

`MainWindow::Create` honours its `nCmdShow` argument (the window is created
without `WS_VISIBLE`, then shown only when `nCmdShow != SW_HIDE`) so a hidden
pet stays hidden across a window rebuild; `RecreateMainWindow` passes
`SW_SHOW`/`SW_HIDE` according to `wasVisible`.

### Window recreation (in-process)

`Reset Pet Window` and the watchdog both call `HostApp::RecreateMainWindow()`:

1. Record `wasVisible` (`IsWindowVisible`).
2. `m_window->SetRecreating(true)` — `MainWindow::WM_DESTROY` then **skips**
   `PostQuitMessage(0)`, so the single shared message loop survives.
3. `DestroyWindow(m_mainWindow)` → `delete m_window` → `new MainWindow` +
   `Create(wasVisible ? SW_SHOW : SW_HIDE)`.
4. `m_trayHost.OnPetWindowRecreated()` refreshes the tooltip.

`MainWindow` exposes `SetRecreating(bool)` for the flag and a public
`OpenSettings(HWND)` so the tray can reach Settings while the pet is hidden
(no context menu is available then).

### Watchdog & the main-thread limit

The `WM_TIMER` tick bumps `TrayHost::m_watchdogTicks` (exposed via
`GetWatchdogTicks()` for the debug channel), then calls
`HostApp::IsMainWindowAlive()` (`IsWindow`). When the pet window is gone it
logs a warning and calls `RecreateMainWindow()`.

**This is the architectural limit of v1.** The pet window, the tray message
window, and the `GetMessageW` loop in `HostApp::Run` all live on the main thread.
A hard hang of the pet window therefore freezes the tray too, and the in-process
watchdog can only detect a *destroyed* window, never a wedged thread. Recovering
a genuine hang needs an **external watchdog** (separate process) — deferred to
Phase 2, as are service start/stop, health polling, and progress feedback. The
v1 tray is host-level only: it does not manage services.

### Debug channel

When built with `DEBUG_CONSOLE`, the `tray` console command (§11) drives this
subsystem over the named-pipe server:

| Command | Effect |
|---------|--------|
| `tray status` | Icon-added flag, pet window alive/visible, watchdog tick count |
| `tray show` / `tray hide` | `SetMainWindowVisible` (hides/shows + persists the flag) |
| `tray reset` | `RecreateMainWindow()` |
| `tray killwindow` | Destroy the pet window without recreating, to exercise the watchdog |

`HostApp::DebugDestroyMainWindow()` suppresses the `WM_DESTROY` quit and leaves
`m_mainWindow`/`m_window` pointing at the dead window so the watchdog sees it
missing and rebuilds it — the only way to test the recovery path without a real
crash. Behaviour is verified by `.agent/tray-smoke.ps1` (15 checks: status,
hide/show, reset while hidden/visible, watchdog ticking, killwindow + rebuild).

### Logging & strings

`Logger::Write(L"Tray", …)` for add/remove/rebuild events. Menu labels come from
the `[Tray]` section via `Tr(L"Tray", L"…")` — added to
`Translation/_src/zh-TW.ini`, so no `Pet/lang/` or module lang file is needed
(host-level strings).

---

## 13. Agent Documentation Convention (`AGENTS.md`)

### Design principle

`AGENTS.md` at project root is a **lightweight entry point**, not a comprehensive reference. It should:

1. State what the project is (one line)
2. Point to `INDEX.md` for the full INDEX chain
3. Point to key reference locations (toolchain, architecture)

All detailed information lives in the INDEX chain (`resources/reference/`, `D:\Agent\resources\reference\`). Agents navigate the chain lazily, reading only what they need.

### Anti-patterns

- Do NOT duplicate content from INDEX chain docs into `AGENTS.md`
- Do NOT add build commands, module details, or conventions that already exist in reference docs
- Do NOT expand `AGENTS.md` beyond ~15 lines

### What belongs in `AGENTS.md`

Only information that helps an agent **start** navigating:

- Project identity (one line)
- Entry point for INDEX chain (`INDEX.md`)
- Key reference paths for common tasks (build, architecture)

Everything else is discovered via the INDEX chain.

