# Reference

## Project-specific docs

| File | Description |
|------|-------------|
| [project-layout.md](project-layout.md) | **Start here** — full directory map with one-line descriptions |
| [chain/](chain/INDEX.md) | INDEX chain specification |
| [design/](design/INDEX.md) | System architecture — module system, context menu, config, build, dialogs, MainWindow, debug system |
| [design/debugging-guide.md](design/debugging-guide.md) | **Debugging traps** — no crash reporting, silent exit classification, CJK/wofstream abort, how to simulate a handler |
| [design/crash-silent-exit-playbook.md](design/crash-silent-exit-playbook.md) | Silent exit playbook — classify from `Log\Main.txt`, then localize |
| [design/hang-deadlock-playbook.md](design/hang-deadlock-playbook.md) | Hang playbook — WER dump, MinGW RVA→symbol mapping, cross-thread window deadlock |
| [autokey-analysis.md](autokey-analysis.md) | AutoKey 模組架構分析 |
| [autokey-class-diagram.txt](autokey-class-diagram.txt) | AutoKey 類別關係圖 |
| [build-process.md](build-process.md) | 建置流程記錄 |
| [modules-overview.md](modules-overview.md) | Module 系統架構總覽 |
| [modules-autokey.md](modules-autokey.md) | AutoKey 模組詳細參考 |
| [modules-changeec.md](modules-changeec.md) | ChangeEC — EC firmware patcher (detect, offset, generate) |
| [modules-romcombiner.md](modules-romcombiner.md) | RomCombiner — ROM concatenation (normalize, trim, combine) |
| [modules-romseparator.md](modules-romseparator.md) | RomSeparator — ROM split (size detection, slice, dual output) |
| [modules-remotecontrol.md](modules-remotecontrol.md) | RemoteControl 模組詳細參考 |
| [modules-networkinfo.md](modules-networkinfo.md) | NetworkInfo — adapter info dialog (IPv4, copy buttons, scroll) |
| [autoit-v3/](autoit-v3/INDEX.md) | AutoIt v3 專案視窗類別與自訂訊息 |

## Common references (cross-project)

Live at `D:\Agent\resources\reference\`:

| Path | Description |
|------|-------------|
| `conventions/` | File naming, data sync |
| `git/` | Git commit rules, workflow, safety |
| `log/` | Session log format |
| `mneme/` | Mneme capacity, scope, session flow |
| `opencode/` | OpenCode app rules, restart, send-to-self |
| `task/` | Task lifecycle and management |
| `tool/` | Tool lookup order, adding rules, git tracking |
| `toolchain/` | Build config reference (compiler flags, target, static linking, pre-existing warnings) |
| `result-archive/` | Result archive format |

## Referenced by

- [INDEX.md](../INDEX.md)
