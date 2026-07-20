# RemoteControl

Remote control via WinSock2 network protocol. Most complex module.

## Files

| File | Role |
|------|------|
| `RemoteControlModule.h/cpp` | IFeatureModule + IDropActionProvider impl, network server |
| `RemoteControlHook.h/cpp` | Low-level keyboard/mouse hooks (`WH_KEYBOARD_LL`, `WH_MOUSE_LL`) |
| `RemoteControlWindow.h/cpp` | LED status windows |
| `RemoteControlTypes.h` | Wire protocol types, packet definitions |

## Interfaces

- `IFeatureModule` — yes
- `IDropActionProvider` — yes

## Notes

- Only module linking external libraries (`ws2_32`, `ole32`)
- Uses 4-thread pool: accept, send, server-recv, client
- `FileTransferService` used at header level (`m_ftRx` member)
- `winsock2.h` must be included before `windows.h` — warning if order reversed
- Detailed analysis: [resources/reference/modules-remotecontrol.md](../../resources/reference/modules-remotecontrol.md)

## Core/Services used

ConfigManager, Logger, TranslationService, NetworkService, FileTransferService

## Referenced by

- [Modules/INDEX.md](../INDEX.md)
