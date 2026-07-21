# Project Layout

```
D:\Project/
├── main.cpp                 Application entry (wWinMain)
├── HostApp.h/.cpp           App host: init, msg loop, module lifecycle, console reader
├── Makefile                 Build rules (MinGW-g++), auto-runs Build.py
├── Build.bat                One-click build (clean → Build.py → make)
├── Build.py                 Codegen: module registry, icon, merged translations
├── ProjectPackager.py       Packaging report / export-clean
├── ProjectSnapshot.py       Project context snapshot for agents
├── GenerateModuleRegistry.* Auto-generated (by Build.py)
├── GeneratedBuild.mk        Auto-generated (by Build.py)
├── GeneratedIcon.*          Auto-generated (by Build.py)
├── TranslationRes.rc        Embeds Translation/zh-TW.ini as RCDATA
├── Pet.png                  Desktop pet image (loaded by MainWindow)
├── Config_Pet.ini           Pet config (opacity, scale, position, move)
├── Config_*.ini             Per-module config files
├── Log/                     Log files per module
├── debug_log.txt            Debug console log (DEBUG_CONSOLE only)
│
├── Core/                    Shared infrastructure
│   ├── IFeatureModule.h     Pure virtual: lifecycle, config, context menu
│   ├── IDropActionProvider.h Pure virtual: drag-drop handling
│   ├── IDebugStateProvider.h Pure virtual: debug state dump
│   ├── IHostContext.h       Abstract host access (HWND, HINSTANCE, TranslationService)
│   ├── IInputHandler.h      Input event interface
│   ├── ConfigManager.h/.cpp INI persistence via GetPrivateProfileStringW
│   ├── LoaderConfig.h/.cpp  Startup config filter (which modules to load)
│   ├── ModuleManager.h/.cpp Module registry, lifecycle orchestration, drop dispatch
│   ├── InputManager.h/.cpp  Input event routing
│   ├── Logger.h/.cpp        Module-aware logging (Log/*.txt)
│   └── DebugConsole.h/.cpp  DEBUG_CONSOLE: AllocConsole, DBG macro, interactive cmds
│
├── Services/                Stateless singleton services
│   ├── TranslationService   i18n: Tr(L"Section", L"Key") → zh-TW.ini lookup
│   ├── FileService          File I/O: read/write binary, exists, size, extension
│   ├── ClipboardService     Win32 clipboard wrappers
│   ├── ChecksumService      CRC/hash computation
│   ├── NetworkService       WinSock2 client/server wrappers
│   └── FileTransferService  File transfer over network
│
├── Pet/                     Desktop pet GDI+ layered window
│   ├── MainWindow.h/.cpp    2303 lines — the core: render, popups, move, edge-snap
│   └── PetConfig.h/.cpp     Config load/save for pet (opacity, scale, position, etc.)
│
├── UI/                      Dialogs
│   ├── SettingsDialog.h/.cpp Settings window: tabbed per-module config + pet fields
│   └── NetworkInfoDialog.h/.cpp Network info display (IPv4 adapters, copy buttons)
│
├── Modules/                 Feature plugin modules (each is a .module.ini package)
│   ├── AutoKey/             Keyboard macro record/replay + hotkey triggers
│   ├── ChangeEC/            EC firmware patcher: merge .bin into .rom at offset
│   ├── Checksum/            File hash calculator (CRC/MD5/SHA1 display)
│   ├── NetworkInfo/         Network adapter info: IPv4 per-IP copy + scrollable dialog
│   ├── RemoteControl/       Remote control via TCP: keyboard/mouse forwarding
│   ├── RomCombiner/         ROM concatenation: combine two .rom files into one
│   ├── RomSeparator/        ROM split: slice combined .rom at configurable boundary
│   └── Template/            New-module template (copy + rename)
│
├── Debug/                   Test code (not built by default, `make test`)
│   ├── AutoKey/AutoKeyTest.cpp    65 tests: parser, VK names, INI roundtrip
│   ├── UI/TooltipTest.cpp         17 tests: tooltip control behavior
│   ├── Core/DebugStateTest.cpp    9 tests: IDebugStateProvider pattern
│   └── UI/GuiTest.cpp             E2E GUI test (no Makefile target, manual compile)
│
├── Translation/             Generated merged locale (Translation/zh-TW.ini)
│
└── resources/               INDEX chain, reference docs, tools, skills
    ├── INDEX.md
    ├── reference/           Project-specific docs (this directory)
    └── tools/               Project scripts (debug-build, run-tests, etc.)
```
