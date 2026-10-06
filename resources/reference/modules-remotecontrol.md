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
- Clipboard sync (`CLIP_SET`/`CLIP_GET`/`CLIP_RESP`) carries a multi-format container: UTF-16 text block plus optional `CF_DIB` image block; receiver replaces its clipboard atomically. Images > `kMaxClipPayload` (~10 MB wire limit) are dropped at pack time, text is always kept.
- Pack/unpack lives in `RemoteControlTypes.h` (`Proto::clip`/`Proto::unclip`); `RemoteControlHook::ClipReadAll`/`ClipWriteAll` also fall back to converting a `CF_BITMAP` DDB to a 32bpp DIB. Verified by `Debug/RemoteControl/ClipboardTest`.

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

Lifecycle is governed by an **epoch generation counter** (`m_epoch`), not a stop flag:

- `StartRemoteControl` and `StopRemoteControl` each do `m_epoch.fetch_add(1)`.
- Every worker captures the value at spawn and loops `while (m_epoch == myEpoch)`.
- A Stop/Start cycle therefore **cannot revive threads from the previous cycle** —
  the old `m_quit = false` reset in Start did exactly that, duplicating `RunClient`
  (two connections) and `RunLeds`. The old flag is gone entirely.
- `RunClient`'s 3s retry backoff sleeps in 100 ms slices checking the epoch, so
  Stop takes effect during it.

`StopRemoteControl` closes three sockets to wake blocked calls:

| Socket | Owner | How Stop gets it |
|---|---|---|
| `m_svListen` (listening) | `RunAccept` (local var mirrored in `std::atomic<SOCKET> m_svListen`) | `exchange(INVALID_SOCKET)` then `closesocket` — wakes a blocked `accept()`. Only the side that wins the swap closes, so there is no double-close on a reused handle value. |
| `m_svSocket` | under `m_svMx` | lock + close |
| `m_clSocket` | under `m_clMx` | lock + close |

The LED mover thread (`RunLeds`) is **joinable**: `DestroyLeds` joins it (outside
`m_ledMx`) before `DestroyWindow`, and every `m_led` accessor takes `m_ledMx`.

### Stop latency

If Stop lands while a file chunk is mid-send, the UI thread waits for `m_svMx`
until the sender's blocking `send()` returns — bounded by `SO_SNDTIMEO`
(`kIoTimeoutMs` = 3 s; measured in `DisconnectTest`). An `AbortSends()` helper was
evaluated and rejected: the wait is inside one syscall that only `closesocket()`
or the timeout can interrupt, and `closesocket()` is exactly what needs the mutex.

### Connection lifecycle details

- `RunSvRecv` treats a failed `RecvMsg` as connection loss: it clears `m_svSocket`,
  closes the socket, updates the LED, and **continues the loop** (it must not exit —
  a client that reconnects is accepted by `RunAccept` and needs this receiver alive).
- `RunClient` checks `SO_ERROR` after the non-blocking `connect()` completes;
  `select()` reports a failed connect as writable, so without it refused hosts
  fall through to the auth send.
- Ports are parsed with `RemoteControl::TryParsePort` (`RemoteControlTypes.h`):
  never throws (an uncaught `std::stoi` on a hand-edited INI used to
  `std::terminate` the process from a worker thread), rejects trailing
  junk/overflow, clamps privileged ports to 1024.
- `FileTransferService::StartSvSender`/`StartClSender` spawn their worker at most
  once (previously one leaked detached thread per Start/Stop cycle).

### Shared-state locking

| Mutex | Guards | Accessors |
|---|---|---|
| `m_valuesMx` | `m_values` config map — read by workers per message, written by the settings dialog | `GetValue`/`SetValue`/`GetBoolValue`/`GetStringValue`/`LoadConfig`/`SaveConfig` (snapshots before file I/O) |
| `m_ftRxMx` | `m_ftRx` receive state — reset by RunAccept/Stop, fed by RunSvRecv | `ResetRecv`/`ProcessMsg` calls |
| `m_ledMx` | LED `HWND` + positions | `SetLedColor`/`UpdateLedPositions`/`GetLedColor`/`ShowLed` |
| `s_tbMx` (FileTransferService) | `s_taskbar`/`s_progHwnd` COM pointers — `Stop` Releases while senders may be inside `updateTaskbar` | `SetProgressHwnd`/`updateTaskbar` |

## Dead-Peer Handling

Every connected socket (accepted in `RunAccept`, and dialed in `RunClient`) gets two settings, via `NetworkService::ApplyTimeouts` and `NetworkService::ApplyKeepAlive`:

| Setting | Value | Why |
|---|---|---|
| `SO_SNDTIMEO` / `SO_RCVTIMEO` | 3000 ms | Without it a blocking `send()`/`recv()` on a vanished peer returns only when the OS gives up — potentially minutes. The worker holds `m_svMx` that whole time, so `StopRemoteControl` on the UI thread cannot proceed. |
| `SO_KEEPALIVE` + `SIO_KEEPALIVE_VALS` | 5 s idle, 1 s interval | Bare `SO_KEEPALIVE` leaves the Windows **2-hour** idle probe in place, so a powered-off client is not noticed in any useful timeframe. |

`RunSvRecv`'s failed-`RecvMsg` handling is described under
[Connection lifecycle details](#connection-lifecycle-details) — before the fix it
`Sleep(200); continue`-ed on a dead handle and left the LED green.

### Mutex discipline

`UpdateLed()` locks both `m_svMx` and `m_clMx` itself. It must therefore **never** be called while either is held — `std::mutex` is not recursive, so doing so permanently parks the worker thread while it owns the mutex, which is exactly what made the app unkillable from the right-click menu. Guarded by `Debug/RemoteControl/DisconnectTest`.

Note: `RunSend` and `RunSvRecv` may both decide the connection is lost; each clears `m_svSocket` only if it still holds the socket it saw, so only one of them closes the handle.

## Hook and Stop Behavior

`RemoteControlHook` installs `WH_KEYBOARD_LL`/`WH_MOUSE_LL`. `StopRemoteControl` calls `RemoteControlHook::Stop()` before tearing down the LED windows, then destroys them and resets the file-transfer receive state.

## LED Status Windows

`RemoteControlWindow` creates small overlay windows to indicate connection status. Uses `WS_EX_LAYERED | WS_EX_TOPMOST` for always-on-top display.
