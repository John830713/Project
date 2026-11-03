# 建置流程總結

## 進入點

只有一個建置工具：

```
BuildUI.vbs        <- 建議：完全沒有 Console
BuildUI.bat        <- 同上，但 .bat 本身會閃一下 Console
py BuildUI.py      <- 保留 Console，方便看 traceback
```

舊的 `Build.bat`、`Build.py`、`Clear.bat` 已整合進 `BuildUI.py`（`BuildUI.py`
同時負責 codegen、clean、build、test 與 UI）。`Makefile` 需要 codegen 時會呼叫
`py BuildUI.py --generate`。

## 建置模式

模式存在 `Config/BuildUI.json`（gitignored）：

| mode | 內容 |
|------|------|
| `release` | 一般版本（不開 Console） |
| `debug` | 追加 `-DDEBUG_CONSOLE=1`：AllocConsole、`DBG()`、`\\.\pipe\ProjectDebug`（AI debug） |

UI 右側有模式選項；切換模式會強制先 `make clean`，避免 Release/Debug 物件混用。
命令列：`py BuildUI.py --build --mode debug`。

## 一次建置的流程

```
0. cd 到專案根目錄
1. 讀 MakePath.txt → mingw32-make.exe 完整路徑（找不到則查常見 MSYS2 路徑）
2. 前置 toolchain bin/ 到 PATH
3. （可選）make SHELL=cmd.exe clean
4. py BuildUI.py --generate [--modules A,B]
     - 掃描 Modules/*/*.module.ini → GeneratedModuleRegistry.cpp
     - 掃描 Core/Services/UI/Pet/Tray + Modules → GeneratedBuild.mk
     - 轉換 Test.png → GeneratedIcon.ico/.rc（內建 PNG 解碼，不需要 Pillow）
     - 合併翻譯 → Translation/zh-TW.ini
     - 產生 GeneratedVersion.rc
5. make SHELL=cmd.exe [CXXFLAGS_EXTRA=-DDEBUG_CONSOLE=1]
6. UI：Build / 執行 / 執行測試 / 清理
```

## 命令列等價物

```cmd
py BuildUI.py --generate [--modules A,B]
py BuildUI.py --list-modules
py BuildUI.py --build [--mode release|debug] [--clean] [--test]
py BuildUI.py --test
py BuildUI.py --clear        :: make distclean
```

## 編譯參數

```
g++ -O2 -Wall -std=c++17 -MMD -MP -static-libgcc -static-libstdc++ -static -municode -c <file>.cpp -o <file>.o
windres TranslationRes.rc -O coff -o TranslationRes.o
windres GeneratedIcon.rc -O coff -o GeneratedIcon.o
Link: -lgdiplus -lgdi32 -luser32 -lkernel32 -lcomctl32 -lshell32 -lws2_32 -lole32 -luuid -lwinmm -mwindows
      -lversion -liphlpapi -lcomdlg32
```

## 注意事項

- `make clean` 在無 `.o` 檔時會回報 ignored error，不影響後續步驟。
- `make` 只在 `GeneratedBuild.mk` / `Translation/zh-TW.ini` 缺少或翻譯來源較新時
  自動跑 `py BuildUI.py --generate`；新增/移除 `.cpp` 後請手動執行一次。
- 沒有 `trim` target。
- `winsock2.h` 必須在 `windows.h` 之前引入（見 §15 gotchas）。
