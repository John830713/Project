# BuildUI.py — 模組挑選器與載體 Flavor

`BuildUI.bat`（或 `py BuildUI.py`）開一個勾選式建置工具：勾選要的模組、選一個載體，
按 Build 就產生對應的 exe 行為組合。

## 概念

| 名詞 | 意思 |
|------|------|
| 模組 | `Modules/<Name>/` 的功能單元（`IFeatureModule`） |
| 載體 | 模組被「裝」在哪個外殼裡：`pet` / `tray` / `window` |
| Flavor | 一組「模組 × 載體」的存檔，`Config/Config_Flavor_<名稱>.json` |

模組程式碼**不隨載體改變**。`IFeatureModule` 暴露的 `GetContextMenuItems()` /
`ExecuteContextMenuItem()` / `GetConfigDefinitions()` 是介面無關的，所以任何載體都只是
換一個殼去呼叫同一份 `GeneratedModuleRegistry`。

## 三種載體

| 載體 | 行為 | 實作 |
|------|------|------|
| `pet` | 寵物視窗 + 系統匣（原本的完整程式） | `HostApp` + `Pet/MainWindow` |
| `tray` | 只有系統匣圖示，寵物視窗建立但永不顯示 | 同上，`Initialize(..., startHidden=true)` |
| `window` | 一般視窗，列出所有模組的選單動作 | `UI/ModuleLauncherWindow` |

`tray` / `window` 仍然會建立寵物視窗（系統匣、模組生命週期、熱鍵都掛在 `HostApp` 上），
只是不顯示，而且**不會改寫**使用者存的寵物顯示狀態。

## 用法

```
BuildUI.vbs        <- 建議：完全沒有 Console
BuildUI.bat        <- 同上，但 .bat 本身會閃一下 Console
py BuildUI.py      <- 保留 Console，方便看 traceback
```

`BuildUI.vbs` 是**沒有 Console 的入口**（wscript 本身就是 GUI 程式，完全不會閃視窗），
它會依序找 `pyw.exe` → `pythonw.exe` → `python.exe` 來跑 `BuildUI.py`。
`BuildUI.bat` 只是轉呼叫它，給習慣用 cmd 的人；`.bat` 本身仍會短暫閃一個 Console
（Windows 行為，無法避免），所以**建議雙擊 `BuildUI.vbs`**。

要在 Console 裡看錯誤時直接跑：

```cmd
py BuildUI.py
```

視窗無聲消失（`pythonw` 的例外沒有地方印）時，查看 `.agent/buildui_error.log`，
工具的未攔截例外會寫在裡面，同時跳一個 MessageBox。

> `BuildUI.vbs` 只能有 ASCII 內容：Windows Script Host 沒看到 BOM 時會用 ANSI
> 讀 `.vbs`，中文字串會變成編譯錯誤（`Expected end of statement`）。訊息文字請留英文。

`BuildUI.py` 產生的子行程（`Build.py`、`mingw32-make`、`tasklist`、`taskkill`、
`Project.exe`）一律帶 `CREATE_NO_WINDOW`：`pythonw` 沒有 Console，console 子行程
若不加這個旗標會自己開一個空的黑視窗。

1. 左側勾選模組（「全部編入」= 不加 `--modules`，行為同舊的 Build.bat）
2. 右側選載體、Flavor 名稱
3. `Build` → 寫 Flavor 檔 → `py Build.py --modules ...` → `mingw32-make SHELL=cmd.exe`
4. `執行` → `Project.exe --flavor <名稱>`

產出物：

```
Config/Config_Flavor_<名稱>.json     { "mode": "tray", "modules": ["ChangeEC"] }
Flavors/Run_<名稱>.bat               cd 到專案根目錄後帶 --flavor 啟動
```

兩者都 gitignored。最後一次勾選存在 `.agent/buildui_state.json`。

## 命令列等價物

```cmd
py Build.py --list-modules
py Build.py --modules ChangeEC,Checksum
mingw32-make SHELL=cmd.exe

Project.exe --flavor ChangeEC-tray
Project.exe --carrier window          :: 覆寫 Flavor 檔裡的 mode，除錯用
```

`--flavor` 是「執行期」過濾（`Config/Config_Flavor_<name>.json`），
`--modules` 是「建置期」過濾（不編進 exe）。兩者可以獨立使用。

## 為什麼 `--modules` 需要核心端配合

子集建置會讓「核心程式碼引用某個模組」浮出來，目前處理過的兩處：

- `Pet/MainWindow.cpp` 原本直接 `#include` 並 `static_cast<RemoteControlModule*>`，
  排除 RemoteControl 就連結失敗。已改為 `Core/ILedIndicator.h` + `dynamic_cast`。
- `UI/NetworkInfoDialog.cpp`（永遠編譯）用 `GetAdaptersAddresses`（`iphlpapi`），
  但該 lib 原本只寫在 NetworkInfo 的 `Libraries=`。`iphlpapi` / `comdlg32` 已移進
  Makefile 的 `LIBS`。

**新增模組時若核心（Core/Services/UI/Pet/Tray）要直接引用它，請走 Core 的介面 +
`dynamic_cast`，不要 `#include Modules/...`**，否則該模組就再也不能被排除。

## 限制

- `Console` 載體尚未實作：模組目前只有「選單動作」這種入口，真 CLI 需要模組額外宣告命令介面。
- `pet` 載體不會因為只勾一個模組而變小（寵物本體永遠在）。要瘦身請用 `tray` / `window`，
  或直接看 exe 大小差異（實測 ChangeEC-only：4.79 MB vs 全模組 6.14 MB）。
- 建置時若**這份建置輸出**（`D:\Project\Project.exe`）正在執行，工具會問要不要結束它
  （只有那個檔案被佔用時連結才會 `Permission denied`）。從其他資料夾開的副本佔用的是它自己
  的 exe，**不會**擋住建置，工具只會在 log 註明「另一個副本正在執行」。
- `執行` 前若偵測到任何同名（`Project.exe`）的行程，工具會先問要不要結束：程式以檔名判定
  單一實例，直接開新檔不會出現第二個視窗，而是走更新流程（比點擊的檔案新就把新檔熱替換進那個
  副本的路徑並重啟它）。只有在「點的就是正在跑的那個檔案」時才顯示「已在執行中」。
- 單一實例 mutex 仍有效：不同 Flavor 不能同時執行。
- 各 `resources/tools/local/*.ps1` 仍硬編碼 `D:\Project`，與本工具無關但同樣要注意。
