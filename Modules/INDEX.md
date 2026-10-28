# Modules

Feature plugins, each implementing `IFeatureModule`. Auto-discovered at build time via `Build.py` scanning `*/*.module.ini`.

## Architecture

See [resources/reference/modules-overview.md](../resources/reference/modules-overview.md) for the shared module system reference (interfaces, patterns, dependency matrix).

## Translations

Every module can ship `lang/zh-TW.ini`; `py Build.py` merges `Translation/_src/`,
`Pet/lang/` and `Modules/*/lang/` into the embedded `Translation/zh-TW.ini`.
Section conventions (English text in code is always the key):

| Section | Key | Shown as |
|---------|-----|----------|
| `[<Module>]` | context menu label | tray / pet menu item |
| `[Common]` | `ConfigFieldDefinition::label` | settings dialog field label |
| `[TabLabels]` | `GetDisplayName()` | settings tab **and tray submenu title** — required, looked up via `TranslationService::TrModuleLabel()` |
| `[About]` | `GetPurpose()` | About dialog description |

## Forward links

| Path | Description |
|------|-------------|
| [AutoKey/](AutoKey/INDEX.md) | Keyboard automation (hotkeys, key sequences) |
| [ChangeEC/](ChangeEC/INDEX.md) | ROM character encoding converter |
| [Checksum/](Checksum/INDEX.md) | File checksum calculator |
| [RemoteControl/](RemoteControl/INDEX.md) | Remote control via WinSock2 |
| [RomCombiner/](RomCombiner/INDEX.md) | ROM file combiner |
| [RomSeparator/](RomSeparator/INDEX.md) | ROM file separator |
| [Template/](Template/INDEX.md) | Skeleton reference for new modules |
| [WebAgent/](WebAgent/INDEX.md) | HTTP bridge for the Tampermonkey Web Agent |

## Referenced by

- [INDEX.md](../INDEX.md)
