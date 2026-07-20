# UI

Shared UI components. Contains the settings dialog used by both pet config and modules.

## Files

| File | Role |
|------|------|
| `SettingsDialog.h/cpp` | Modal settings window with Pet tab + per-module tabs |

## SettingsDialog Rules

1. Modal window; Pet tab is always tab 0, module tabs follow at index 1..N
2. Config fields auto-generated per module via `GetConfigDefinitions()`. `Bool` → checkbox, `Int`/`String` → edit box. Scrollbar if fields exceed visible area.
3. Tab labels translated via `Tr(L"TabLabels", module->GetDisplayName())`. Each module needs a `[TabLabels]` entry.
4. On **Save**: read controls → save PetConfig → `module->SetValue()` each module → `SaveAllConfigs()` → `ApplyAllConfigs()`
5. On **Cancel**: discard changes
6. Missing config keys auto-filled with defaults by `ConfigManager::EnsureModuleConfig()` during `LoadModuleConfig()`. Existing keys never overwritten.
7. **CRITICAL — button preservation**: `OpenCustomSettings()` must NOT destroy+repopulate parent controls. When clicking "Manage Actions..." (ID 1000):
   a) `ReadFieldsFromControls()` to save current edits
   b) Call `mod->OpenCustomSettings(hwnd)` (may open modal child dialog)
   c) Leave existing controls intact — DestroyWindow + PopulateFields would destroy the Manage button itself
8. Module config field labels translated via `Tr(L"Common", def.label.c_str())` by SettingsDialog. Standard keys like "Enable Module" go in `[Common]` section of lang files.

## Referenced by

- [INDEX.md](../INDEX.md)
