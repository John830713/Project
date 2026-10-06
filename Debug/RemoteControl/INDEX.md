# Debug/RemoteControl

RemoteControl wire-protocol mechanism tests: clipboard sync, disconnect handling, and the diagnostics/deploy backdoor framing (no live connection required).

## Files

| File | Role |
|------|------|
| `ClipboardTest.cpp` | Verifies clipboard + wire-protocol mechanism used by both roles. Target: `Debug/RemoteControl/ClipboardTest.exe` (links `-lgdi32` for `GetDIBits`). Compiles the real `Modules/RemoteControl/RemoteControlHook.cpp`. |
| `DisconnectTest.cpp` | Verifies the disconnect/shutdown path over a real loopback socket pair. Target: `Debug/RemoteControl/DisconnectTest.exe` (links `-lws2_32 -lole32 -luuid`). Compiles the real `Services/NetworkService.cpp` + `Services/FileTransferService.cpp`. |
| `DiagnosticsTest.cpp` | Verifies the diagnostics/deploy backdoor frames end to end. Target: `Debug/RemoteControl/DiagnosticsTest.exe` (links `-lws2_32`). Compiles the real `Services/NetworkService.cpp`. |

## Coverage

- Protocol: `Proto::clip`/`Proto::unclip` round-trip for text, text+image, empty (clear), oversized (image dropped, text kept), and malformed payloads.
- Clipboard primitives: `ClipWriteAll`/`ClipReadAll` text+DIB round-trip against the real Windows clipboard.
- Master flow (controller → target): the exact call sequence of `RunSend` + `CLIP_SET` handler.
- Slave flow (target → controller): the exact call sequence of `CLIP_GET` handler + `RunSvRecv`.

## DisconnectTest

Target: `Debug/RemoteControl/DisconnectTest.exe` (links `-lws2_32 -lole32 -luuid`).
Compiles the real `Services/NetworkService.cpp` and `Services/FileTransferService.cpp`.
54 checks. Guards the master-side hang that made the app unkillable when the client PC was powered off. `RunSend()` held `m_svMx` and called `UpdateLed()` on send failure; `UpdateLed()` locks `m_svMx` again, so the worker self-deadlocked *while owning the mutex* the UI thread needs in `StopRemoteControl()`. Verified by injecting that ordering back into the test — it hangs.

- `ApplyTimeouts` bounds a blocking `send()` on a peer whose receive window never opens (negative control: without it the test hangs).
- `RecvMsg` returns instead of hanging when the peer dies mid-frame, so the master loop can clear `m_svSocket`.
- The `RunSend` failure path leaves `m_svMx` acquirable, reproducing a `StopRemoteControl()`-equivalent stop with a bounded `try_lock` poll (`std::mutex` has no `try_lock_for`).
- `ApplyKeepAlive` applied/rejected as expected.
- **Port parsing**: `TryParsePort` never throws, rejects trailing junk / overflow, clamps privileged ports to 1024 (a raw `std::stoi` on a hand-edited INI used to `std::terminate` from a worker thread).
- **Recv-loop reconnect survival**: after a clean `RecvMsg` failure the receiver keeps looping — 20 disconnect/reconnect cycles with a stable `m_epoch` and a live receiver.
- **Epoch generations**: `m_epoch` bumps (Start/Stop) never revive threads from a previous generation; no lingering connection after bump.
- **Spawn-once**: 5 Start/Stop cycles spawn exactly 2 file-transfer workers (old code: 10), via `FileTransferService::DebugSenderSpawnCount()`.
- **Stop latency bound**: when Stop lands mid-send, acquisition of the socket mutex is bounded by `SO_SNDTIMEO` (measured ≈1012 ms for a 1000 ms timeout), not by TCP retransmission. `AbortSends()` was evaluated and rejected — the wait is inside one blocking `send()` that only `closesocket()`/the timeout can interrupt.

## DiagnosticsTest

Target: `Debug/RemoteControl/DiagnosticsTest.exe` (links `-lws2_32`).
Compiles the real `Services/NetworkService.cpp`.
35 checks. Covers the backdoor frame formats defined in `RemoteControlTypes.h`.

- `dbgReq`/`dbgResp`/`exeSwap`/`exeResult` pack correctly and round-trip through the real
  `NetworkService` send/frame path; the seq value survives the wire.
- The four messages are distinguishable by their type byte (16/17/18/19).
- An unknown type still frames/parses (old-peer tolerance — the recv loops have no
  default case, so an old peer just drops it).
- Snapshot formatting renders session/runtime/socket/thread/wire-counter/UI-probe/log-tail
  lines without gaps.

## Referenced by

- [Debug/INDEX.md](../INDEX.md)