# UI

Shared UI components. Contains the settings dialog used by both pet config and modules.

## Files

| File | Role |
|------|------|
| `SettingsDialog.h/cpp` | Modal settings window with Pet tab + per-module tabs + `Remote Init` tab (last index) |
| `NetworkInfoDialog.h/cpp` | Two-tab dialog: network adapter/IP viewer (copy-to-clipboard) + "Remote Control Init" tab (RDP/WinRM checkboxes, backup/apply/restore via `RemoteInitService`) |

## SettingsDialog Rules

1. Modal window; Pet tab is always tab 0, module tabs at index 1..N, `Remote Init` last (`m_modules.size() + 1`). Total tab count = `1 + m_modules.size() + 1` — index maths (wheel paging, `TabCtrl_SetCurSel`) must include the Remote Init tab.
2. Config fields auto-generated per module via `GetConfigDefinitions()`. `Bool` → checkbox, `Int`/`String` → edit box. Scrollbar if fields exceed visible area.
3. Pet tab is seeded from **every** `PetConfig::GetDefinitions()` key first, then overwritten by `PetConfig::Load()`. A key missing from the seed map silently falls back to its definition default (this is how `autoStart` used to render unchecked and get deleted on Save), so a new Pet key needs both its definition and a seed entry.
4. `ReadFieldsFromControls()` covers **config tabs only** (`1 + m_modules.size()`); the Remote Init tab holds action checkboxes and `m_editedValues` has no slot for it.
5. Tab labels translated via `Tr(L"TabLabels", module->GetDisplayName())`. Each module needs a `[TabLabels]` entry.
6. On **Save**: read controls → save PetConfig → `module->SetValue()` each module → `SaveAllConfigs()` → `ApplyAllConfigs()`
7. On **Cancel**: discard changes
8. Missing config keys auto-filled with defaults by `ConfigManager::EnsureModuleConfig()` during `LoadModuleConfig()`. Existing keys never overwritten.
9. **CRITICAL — button preservation**: `OpenCustomSettings()` must NOT destroy+repopulate parent controls. When clicking "Manage Actions..." (ID 1000):
   a) `ReadFieldsFromControls()` to save current edits
   b) Call `mod->OpenCustomSettings(hwnd)` (may open modal child dialog)
   c) Leave existing controls intact — DestroyWindow + PopulateFields would destroy the Manage button itself
10. Module config field labels translated via `Tr(L"Common", def.label.c_str())` by SettingsDialog. Standard keys like "Enable Module" go in `[Common]` section of lang files.

## Referenced by

- [INDEX.md](../INDEX.md)
