# AutoKey 模組分析總結

## 達成目標

1. 完整逆向分析 AutoKey 模組的架構、資料流、控制流
2. 釐清六個原始檔的職責與互動關係
3. 記錄熱鍵系統從 `RegisterHotKey → WM_HOTKEY → OnHotkey → RunAction → SendInput` 完整鏈路
4. 確認 65 案單元測試均通過，無回歸

## 遇到的問題

- `AutoKeyParser.h` 未列在 `AutoKey.module.ini` 的 `Headers=` 中（設計疏忽，但透過 include chain 仍可編譯）
- `AutoKeyAction` 的 `running` 與 `thread` 欄位在 move assignment 時需特殊處理（`InterlockedExchange`）
- 歷史 bug：DestroyWindow 時機造成編輯後清單空白、WS_GROUP 缺少造成 RadioButton 分組異常

## 解決方案

- 翻譯 key 不可有尾端空白：`Build.py` 的 `parse_ini_sections()` 會 `.strip()` key/value；C++ 中 prefix+name 空白由 `+ L" " +` 顯式處理
- 模組熱鍵衝突：各 action 的 `id` 作為 `RegisterHotKey` 的識別碼，`ModuleManager` 的 menu route 用 unique id 避免衝突

## 後續建議

- 考慮加入 `AutoKeyParser.h` 到 module ini 的 Headers 清單
- 可考慮將 EditDialog 獨立為類別檔以降低 AutoKeyDialog.cpp 的複雜度（~419 行）
- 若需擴充按鍵序列語法（如支援 Ctrl+Shift+Esc），擴充 `ParseSequence()` 即可
