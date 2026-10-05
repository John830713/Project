# Debug/RemoteControl

RemoteControl clipboard sync mechanism tests (no live connection required).

## Files

| File | Role |
|------|------|
| `ClipboardTest.cpp` | Verifies clipboard + wire-protocol mechanism used by both roles. Target: `Debug/RemoteControl/ClipboardTest.exe` (links `-lgdi32` for `GetDIBits`). Compiles the real `Modules/RemoteControl/RemoteControlHook.cpp`. |
| `DisconnectTest.cpp` | Verifies the disconnect/shutdown path over a real loopback socket pair. Target: `Debug/RemoteControl/DisconnectTest.exe` (links `-lws2_32`). Compiles the real `Services/NetworkService.cpp`. |

## Coverage

- Protocol: `Proto::clip`/`Proto::unclip` round-trip for text, text+image, empty (clear), oversized (image dropped, text kept), and malformed payloads.
- Clipboard primitives: `ClipWriteAll`/`ClipReadAll` text+DIB round-trip against the real Windows clipboard.
- Master flow (controller → target): the exact call sequence of `RunSend` + `CLIP_SET` handler.
- Slave flow (target → controller): the exact call sequence of `CLIP_GET` handler + `RunSvRecv`.

## DisconnectTest

Guards the master-side hang that made the app unkillable when the client PC was powered off. `RunSend()` held `m_svMx` and called `UpdateLed()` on send failure; `UpdateLed()` locks `m_svMx` again, so the worker self-deadlocked *while owning the mutex* the UI thread needs in `StopRemoteControl()`. Verified by injecting that ordering back into the test — it hangs.

- `ApplyTimeouts` bounds a blocking `send()` on a peer whose receive window never opens (negative control: without it the test hangs).
- `RecvMsg` returns instead of hanging when the peer dies mid-frame, so the master loop can clear `m_svSocket`.
- The `RunSend` failure path leaves `m_svMx` acquirable, reproducing a `StopRemoteControl()`-equivalent stop with a bounded `try_lock` poll (`std::mutex` has no `try_lock_for`).
- `ApplyKeepAlive` applied/rejected as expected.

## Referenced by

- [Debug/INDEX.md](../INDEX.md)