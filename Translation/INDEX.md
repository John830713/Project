# Translation

Localization system. English text as key, merged translation files.

## Files

| File | Role |
|------|------|
| `en.ini` | English source (manually maintained, tracked in git) |
| `_src/zh-TW.ini` | Traditional Chinese source (core translations) |

## How It Works

1. Sources: `Translation/_src/{lang}.ini` (core) + `Modules/*/lang/{lang}.ini` (per-module)
2. `Build.py` merges → `Translation/{lang}.ini` (UTF-16 LE with BOM)
3. `TranslationRes.rc` embeds `Translation/zh-TW.ini` as RCDATA
4. `TranslationService` loads the embedded data at runtime
5. `Tr(section, key)` translates or falls back to English

## Adding a Language

1. Create `Translation/_src/{lang}.ini`
2. Create `Modules/*/lang/{lang}.ini` for each module
3. Run `py Build.py` — generates merged file

## Referenced by

- [INDEX.md](../INDEX.md)
