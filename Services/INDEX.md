# Services

Reusable technical services that modules can call without duplicating common logic.

## Files

| File | Role |
|------|------|
| `TranslationService.h/cpp` | Singleton i18n: English text as key, Tr() fallback |
| `FileService.h/cpp` | Binary file read/write, existence/size checks, extension matching |
| `ChecksumService.h/cpp` | File checksum calculation (simple additive algorithm) |
| `ClipboardService.h/cpp` | Clipboard text copy (`CF_UNICODETEXT`) |
| `NetworkService.h/cpp` | Network utility functions |
| `FileTransferService.h/cpp` | File transfer state management (used by RemoteControl at header level) |

## Translation System Detail

- English text IS the key — no separate key IDs
- Sources: `Translation/_src/{lang}.ini` (core) + `Modules/*/lang/{lang}.ini` (per-module)
- `Build.py` merges → `Translation/{lang}.ini` (UTF-16 LE with BOM)
- `TranslationRes.rc` embeds `Translation/zh-TW.ini` as RCDATA
- Missing keys: `Tr()` falls back to English key text (graceful degradation)
- Adding a language: create `Translation/_src/{lang}.ini` + per-module lang files

**CRITICAL — INI key/value stripping in Build.py:**
- `Build.py`'s `parse_ini_sections()` calls `.strip()` on both key and value
- Keys must NOT have leading/trailing whitespace
- Trailing spaces in values are lost — use C++ concatenation for prefix+name:
  `Tr(L"AutoKey", L"▶ Start") + L" " + actionName`

## Rules

1. Services must NOT depend on any module
2. Modules focus on feature logic; shared technical details live in Services
3. Services are typically stateless static methods, but may hold state if needed

## Referenced by

- [INDEX.md](../INDEX.md)
