# Debug/RemoteControl

RemoteControl clipboard sync mechanism tests (no live connection required).

## Files

| File | Role |
|------|------|
| `ClipboardTest.cpp` | Verifies clipboard + wire-protocol mechanism used by both roles. Target: `Debug/RemoteControl/ClipboardTest.exe` (links `-lgdi32` for `GetDIBits`). Compiles the real `Modules/RemoteControl/RemoteControlHook.cpp`. |

## Coverage

- Protocol: `Proto::clip`/`Proto::unclip` round-trip for text, text+image, empty (clear), oversized (image dropped, text kept), and malformed payloads.
- Clipboard primitives: `ClipWriteAll`/`ClipReadAll` text+DIB round-trip against the real Windows clipboard.
- Master flow (controller → target): the exact call sequence of `RunSend` + `CLIP_SET` handler.
- Slave flow (target → controller): the exact call sequence of `CLIP_GET` handler + `RunSvRecv`.

## Referenced by

- [Debug/INDEX.md](../INDEX.md)