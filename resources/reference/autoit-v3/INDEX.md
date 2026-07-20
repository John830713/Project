# AutoIt v3 — Project patterns

## Window classes

| Window | Class |
|--------|-------|
| Pet main | `PetMainWindowClass` |
| Settings dialog | `SettingsDialogClass` |
| AutoKey dialog | `AutoKeyDialogClass` |
| AutoKey edit dialog | `AutoKeyEditDialogClass` |

## Custom messages

| Message | Value | Effect |
|---------|-------|--------|
| `WM_APP_OPEN_SETTINGS` | `WM_APP + 4` | Opens settings dialog |
| `WM_APP_SWITCH_TAB` | `0x8002` | Switches tab (wParam = tab index) |
