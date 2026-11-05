# Project Layout

```
D:\Project/
├── main.cpp                 Application entry (wWinMain)
├── HostApp.h/.cpp           App host: init, msg loop, module lifecycle, console reader
├── Makefile                 Build rules (MinGW-g++), auto-runs `py BuildUI.py --generate`
├── BuildUI.vbs              Recommended launcher: build tool with no console
├── BuildUI.bat              Same launcher for cmd users (brief console flash)
├── BuildUI.py               Single build tool: codegen + clean/build/tests + module/carrier UI
├── Config/BuildUI.json      Machine-local build config (release/debug, carrier, flavour; gitignored)
├── ProjectPackager.py       Packaging report / export-clean
├── ProjectSnapshot.py       Project context snapshot for agents
├── GeneratedModuleRegistry.* Auto-generated (by BuildUI.py)
├── GeneratedBuild.mk        Auto-generated (by BuildUI.py)
├── GeneratedIcon.*          Auto-generated (by BuildUI.py)
├── GeneratedVersion.*       Auto-generated (by BuildUI.py) — FILEVERSION resource
├── TranslationRes.rc        Embeds Translation/zh-TW.ini as RCDATA
├── Pet.png                  Desktop pet image (loaded by MainWindow)
├── Config/Config_Pet.ini    Pet config (opacity, scale, position, move, snap edge, start with Windows)
├── Config/Config_*.ini      Per-module config files
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
│   ├── AutoStart.h/.cpp     Per-user Run entry ("Start with Windows")
│   ├── LoaderConfig.h/.cpp  Startup config filter (which modules to load)
│   ├── ModuleManager.h/.cpp Module registry, lifecycle orchestration, drop dispatch
│   ├── InputManager.h/.cpp  Input event routing
│   ├── Logger.h/.cpp        Module-aware logging (Log/*.txt)
│   ├── ResourceIds.h        Shared resource ID (IDI_APP_ICON) for RC + C++
│   ├── UpdateManager.h/.cpp  Single instance + in-place rename-swap update
│   └── DebugConsole.h/.cpp  DEBUG_CONSOLE: AllocConsole, DBG macro, interactive cmds
│
├── Services/                Stateless singleton services
│   ├── TranslationService   i18n: Tr(L"Section", L"Key") → zh-TW.ini lookup
│   ├── FileService          File I/O: read/write binary, exists, size, extension
│   ├── ClipboardService     Win32 clipboard wrappers
│   ├── ChecksumService      CRC/hash computation
│   ├── NetworkService       WinSock2 client/server wrappers
│   ├── FileTransferService  File transfer over network
│   ├── ProcessRunner        CreateProcessW with pipes, timeout, elevation helpers
│   └── RemoteInitService    One-shot RDP/WinRM enablement (probe, apply, restore)
│
├── Pet/                     Desktop pet GDI+ layered window
│   ├── MainWindow.h/.cpp    The core: render, popups, move, edge-snap, drop dispatch
│   └── PetConfig.h/.cpp     Config load/save for pet (opacity, scale, position, etc.)
│
├── Tray/                    System-tray shell (message window + Shell_NotifyIcon)
│   └── TrayHost.h/.cpp       Icon, tray menu, in-process pet rebuild watchdog
│
├── UI/                      Dialogs and launcher window
│   ├── SettingsDialog.h/.cpp Settings window: tabbed per-module config + pet fields
│   ├── MenuOrderDialog.h/.cpp Context-menu ordering editor
│   ├── ModuleLauncherWindow.h/.cpp Window-carrier module list
│   └── NetworkInfoDialog.h/.cpp Network info display (IPv4 adapters, copy buttons)
│
├── Modules/                 Feature plugin modules (each is a .module.ini package)
│   ├── AutoKey/             Keyboard macro record/replay + hotkey triggers
│   ├── ChangeEC/            EC firmware patcher: merge .bin into .rom at offset
│   ├── Checksum/            File hash calculator (CRC/MD5/SHA1 display)
│   ├── NetworkInfo/         Network adapter info: IPv4 per-IP copy + scrollable dialog
│   ├── Release_Package/     7z archive extractor with token-based rule engine
│   ├── RemoteControl/       Remote control via TCP: keyboard/mouse forwarding
│   ├── RomCombiner/         ROM concatenation: combine two .rom files into one
│   ├── RomSeparator/        ROM split: slice combined .rom at configurable boundary
│   ├── Template/            New-module template (copy + rename)
│   └── WebAgent/            HTTP bridge for the Tampermonkey Web Agent (C++ port)
│
├── Debug/                   Test code (not built by default, `make test`)
│   ├── AutoKey/AutoKeyTest.cpp          Parser, VK names, INI roundtrip
│   ├── UI/TooltipTest.cpp               Tooltip control behavior
│   ├── Core/DebugStateTest.cpp          IDebugStateProvider pattern
│   ├── Core/MenuOrderTest.cpp           Menu ordering, unique ids, dispatch
│   ├── Core/UpdateManagerTest.cpp       Version ordering, swap rollback, backup cleanup
│   ├── RemoteControl/ClipboardTest.cpp  Clipboard image/text sync (text+DIB round-trip, wire pack/unpack)
│   ├── RemoteControl/DisconnectTest.cpp Dead-peer timeouts, epoch generations, Stop latency
│   ├── RemoteControl/DiagnosticsTest.cpp Diagnostics/health probes
│   ├── WebAgent/WebAgentTest.cpp        URL decode, router, static files, full command protocol
│   ├── Services/RemoteInitTest.cpp      State probe, command planning, restore
│   └── UI/GuiTest.cpp                   E2E GUI test (no Makefile target, manual compile)
│
├── Translation/             Generated merged locale (Translation/zh-TW.ini)
│
└── resources/               INDEX chain, reference docs, tools, skills
    ├── INDEX.md
    ├── reference/           Project-specific docs (this directory)
    └── tools/               Project scripts (debug-build, run-tests, etc.)
```
