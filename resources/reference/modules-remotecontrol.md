# RemoteControl Module — Detailed Reference

Remote control via WinSock2 network protocol. Most complex module in the project.

## Architecture

```
RemoteControlModule
  ├── Network Server (4-thread pool)
  │     ├── Accept thread — listens for connections
  │     ├── Send thread — sends commands to remote
  │     ├── Server-recv thread — receives from remote
  │     └── Client-recv thread — processes local input
  ├── RemoteControlHook (WH_KEYBOARD_LL, WH_MOUSE_LL)
  └── RemoteControlWindow (LED status indicators)
```

## Wire Protocol

Defined in `RemoteControlTypes.h`. Custom binary protocol over TCP:
- Packet headers with type, length, sequence fields
- Supports keyboard/mouse event relay
- File transfer via `FileTransferService`

## External Dependencies

Only module linking additional libraries:
- `ws2_32` — WinSock2
- `ole32` — COM (for clipboard/file transfer)

## Header Include Order

`RemoteControlModule.h` includes `<winsock2.h>` before `<windows.h>`. If `GeneratedModuleRegistry.cpp` includes `RemoteControlModule.h` after other module headers that include `<windows.h>` first, a `#warning` is emitted. This is harmless but should be resolved by including `<winsock2.h>` first in `RemoteControlModule.cpp`.

## FileTransferService Integration

`FileTransferService` is used at the header level — `RemoteControlModule` has a member `m_ftRx` of type `FileTransferService::ReceiveState`. This is the tightest coupling to any service in the project.

## Thread Pool

4 threads managed internally:
1. **Accept** — `accept()` loop on listening socket
2. **Send** — outbound command queue
3. **Server-recv** — reads from connected remote
4. **Client-recv** — processes locally captured input

## LED Status Windows

`RemoteControlWindow` creates small overlay windows to indicate connection status. Uses `WS_EX_LAYERED | WS_EX_TOPMOST` for always-on-top display.
