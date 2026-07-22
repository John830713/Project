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

## 8. Debug System

### Two compile-time gates

| Gate | Build flag | Effect |
|------|-----------|--------|
| `DEBUG_CONSOLE` | `CXXFLAGS_EXTRA=-DDEBUG_CONSOLE=1` | `AllocConsole`, `DBG()` macro, interactive CLI |
| `ENABLE_DEBUG_STATE` | `-DENABLE_DEBUG_STATE=1` | `IDebugStateProvider` + `DebugGetState()` (test builds only) |

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
| `clear` | Clear console |
| `quit` / `exit` | Post WM_QUIT, graceful shutdown |

**Console reader thread**: Background thread reads stdin. Commands dispatched to main thread via `PostThreadMessageW(WM_APP_CONSOLE_CMD)`. Quit/exit post `WM_QUIT` to main thread.

### Debug state (`IDebugStateProvider`)

- Single method: `std::wstring DebugGetState() const`
- Only `AutoKeyModule` implements it (production)
- `ModuleManager::DebugGetState()` iterates modules, `dynamic_cast`s to `IDebugStateProvider*`, collects text dumps
- Guarded by `#ifdef ENABLE_DEBUG_STATE`

### Test infrastructure

- **3 test targets** built via `make test` (not included in `make all`)
- Each test is a standalone `.exe` with minimal macro-based framework (`ASSERT_EQ`, `ASSERT_TRUE`, etc.)
- Tests use `-DENABLE_DEBUG_STATE=1` and `-O0 -g` (no `DEBUG_CONSOLE`)
- Return 0 = pass, 1 = fail

| Test | File | Cases |
|------|------|-------|
| AutoKeyTest | `Debug/AutoKey/AutoKeyTest.cpp` | 65 — parser, VK names, modifiers, edge cases, INI roundtrip |
| TooltipTest | `Debug/UI/TooltipTest.cpp` | 17 — Win32 tooltip cbSize, TTF_TRACK, activation ordering, mouse-move triggers |
| DebugStateTest | `Debug/Core/DebugStateTest.cpp` | 9 — empty manager, module with/without state, direct query |
| GuiTest | `Debug/UI/GuiTest.cpp` | No Makefile target — manual compile. E2E: find pet window, open Settings, manage actions, verify persistence. |

### MainWindow auto-test

When `DEBUG_CONSOLE` is defined, a timer (ID=99) fires 3s after startup and runs an 8-step automated UI test: open context menu → navigate Pet>Move>Step → set trackbar to 42 → close → verify `m_moveStep == 42`. Logs PASS/FAIL then `PostQuitMessage`.

---

## 12. Agent Documentation Convention (`AGENTS.md`)

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

