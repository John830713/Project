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
