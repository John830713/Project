# 建置流程總結

## 達成目標

1. 確認 Build.bat 為正確的建置進入點
2. 記錄 build → trim 完整流程與輸出
3. 更新 Design.txt 修正過時的 build 指令

## 流程細節

### Build.bat (無參數)
```
0. cd /d "%~dp0" (D:\Project)
1. 讀 MakePath.txt → 自訂 make 路徑
2. 未設定則搜尋 PATH: mingw32-make > make > gmake
3. 未找到則檢查 MSYS2 預設位置
4. 找到後：D:\Program Files\MSYS2Portable\App\msys64\ucrt64\bin\mingw32-make.exe
5. 前置 toolchain bin/ 到 PATH
6. make clean → 刪除 *.o *.d Project.exe
7. py Build.py → 掃描 Modules/*/*.module.ini 產生 GeneratedBuild.mk、GeneratedModuleRegistry.cpp、
   GeneratedIcon.rc (從 Test.png)、合併翻譯
8. make → 編譯 34 個 .cpp → .o，連結 Project.exe
9. 無 trim 參數則結束，按任意鍵退出
```

### Build.bat trim
同上，第 8 步後追加 `make trim`（Makefile 無此 target，失敗但忽略，不影響結果）

### 編譯參數
```
g++ -O2 -Wall -std=c++17 -MMD -MP -static-libgcc -static-libstdc++ -static -municode -c <file>.cpp -o <file>.o
windres TranslationRes.rc -O coff -o TranslationRes.o
Link: -lgdiplus -lgdi32 -luser32 -lkernel32 -lcomctl32 -lshell32 -lws2_32 -lole32 -luuid -lwinmm -mwindows
      -lws2_32 -lole32 (duplicated from LIBS + modules)
```

### 警告
- `Modules/RemoteControl/RemoteControlModule.h:9` → winsock2.h 在 windows.h 之後引入（`#warning Please include winsock2.h before windows.h`）— 無害

### 34 個編譯單元
main, HostApp, GeneratedModuleRegistry, Core(ConfigManager, InputManager, Logger, ModuleManager),
Services(ChecksumService, ClipboardService, FileService, FileTransferService, NetworkService, TranslationService),
UI(SettingsDialog), Pet(MainWindow, PetConfig),
AutoKey(AutoKeyParser, AutoKeyModule, AutoKeyDialog),
ChangeEC(ChangeECModule, ChangeECWindow, ChangeECLogic),
Checksum(ChecksumModule),
RemoteControl(RemoteControlModule, RemoteControlHook),
RomCombiner(RomCombinerModule, RomCombinerWindow, RomCombinerLogic),
RomSeparator(RomSeparatorModule, RomSeparatorWindow, RomSeparatorLogic),
Template(TemplateModule, TemplateLogic, TemplateWindow)

## 遇到的問題

- `make clean` 在無 .o 檔時 exit 3（`mingw32-make: [Makefile:104: clean] Error 3 (ignored)`）— Build.bat 忽略此錯誤，後續仍正常
- Makefile 無 `trim` target → trim 參數下 `make trim` 失敗但 script 忽略
- winsock2.h 順序警告 — 因 `GeneratedModuleRegistry.cpp` include 順序造成

## 後續建議

- 可考慮在 Makefile 加入 `trim` target（`del *.o *.d`）
- winsock2.h 順序可透過在 `RemoteControlModule.cpp` 優先引入 <winsock2.h> 解決
