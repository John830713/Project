# Usage Patterns

## Create a new module

1. Create `Modules/<Name>/` directory
2. Write `<Name>.module.ini`:
   ```ini
   [Module]
   Name=<Name>
   Sources=<Name>Module.cpp
   Headers=<Name>Module.h
   Libraries=
   Enabled=1
   ConfigFile=Config_<Name>.ini
   ```
3. Implement `IFeatureModule` in `<Name>Module.h/.cpp`
4. Add `Modules/<Name>/lang/zh-TW.ini` with `[<Name>]` section for menu labels
5. Run `py Build.py` to regenerate registry + build files
6. Build with `mingw32-make`

### Minimal module checklist

- [ ] Pure virtuals: identity, config, lifecycle, value access
- [ ] Context menu: `GetContextMenuItems()` + `ExecuteContextMenuItem()`
- [ ] Config: at minimum `{ L"Enabled", L"Enable Module", Bool, L"1", 0, 1 }`
- [ ] INDEX.md + `.index.json` in the module directory
- [ ] `lang/zh-TW.ini` with `[<ModuleName>]` section for context menu labels

---

## Add a hard-coded main menu item

Only for core Pet functionality. Modules use Function submenu instead.

1. Add `ID_xxx` to `MainWindow.h` enum
2. Add `FILE_ID_xxx` static constexpr in `MainWindow.cpp`
3. Insert `PIK_ACTION` into `g_popupKind[]` and entry in `g_popupCmd[]`
4. Add `case FILE_ID_xxx:` label in WM_PAINT
5. Add `else if (cmd == ID_xxx)` handler in WM_COMMAND

---

## Create a dialog

1. Write a class following the modal dialog pattern (see [spec.md](spec.md) §5)
2. Create controls in `Init()` using `CreateWindowW`
3. Handle `WM_COMMAND` for buttons, `WM_CLOSE` to destroy
4. Place in `UI/` if shared across modules, or inside `Modules/<Name>/` if module-private

### Clipboard copy pattern

```cpp
if (OpenClipboard(hwnd)) {
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
    if (h) {
        wcscpy((wchar_t*)GlobalLock(h), text);
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}
```

---

## Use the INDEX chain

Every directory needs `.index.json`:
```json
{"version": 1, "forward": [], "referenced_by": [".."]}
```

For leaf directories that refer outward, add `forward` entries:
```json
{"version": 1, "forward": ["D:\\Agent\\resources\\reference\\git"], "referenced_by": [".."]}
```

Run chain check: `py "D:\Agent\resources\tools\common\check-chain.py"`

---

## Add translation strings

1. Create `Modules/<Name>/lang/zh-TW.ini`
2. Use `[<ModuleName>]` section matching `GetModuleName()`
3. Format: `Key=Translation`
4. Run `py Build.py` to merge into `Translation/zh-TW.ini`

---

## Notes

- Always `#include "NetworkInfoDialog.h"` or equivalent in the module, not in MainWindow
- `m_host->GetMainWindow()` gives the parent HWND for dialogs
- `ConfigManager::LoadModuleConfig` auto-fills missing keys with defaults
- After adding a `.cpp` file anywhere, run `py Build.py` before `mingw32-make`
