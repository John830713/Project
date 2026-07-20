# 新機台建置指南

從零開始在新機器上建置 Project Memory Framework（D:\Agent）環境。每一步都有驗證指令。

---

## 1. Prerequisites

### 1.1 Git

```powershell
git --version
# 預期: git version 2.x.x
```

若沒安裝：
```powershell
winget install Git.Git
```

### 1.2 mneme（記憶後端）

從 <https://github.com/amcharles/mneme/releases> 下載 mneme 二進位檔：

```powershell
Move-Item ~\Downloads\mneme-*.exe D:\Agent\resources\tools\common\mneme\mneme.exe -Force
D:\Agent\resources\tools\common\mneme\mneme.exe init
D:\Agent\resources\tools\common\mneme\mneme.exe init opencode
```

驗證：
```powershell
D:\Agent\resources\tools\common\mneme\mneme.exe status
# 預期: mneme is running (pid: xxx)
```

### 1.3 PowerShell ExecutionPolicy

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

---

## 2. Clone 專案

```powershell
cd D:\
git clone <repo-url> Agent
cd D:\Agent
```

---

## 3. 初始化 resources

```powershell
Copy-Item -Recurse "D:\Agent\template\resources" "D:\Agent\resources"
Remove-Item -Recurse -Force "D:\Agent\TOOLS" -ErrorAction SilentlyContinue
```

驗證：
```powershell
Test-Path "D:\Agent\resources\tools\common\git\INDEX.md"   # True
Test-Path "D:\Agent\TOOLS"                                  # False
```

---

## 4. mneme 設定

```powershell
# 啟動 daemon
D:\Agent\resources\tools\common\mneme\mneme.exe daemon

# lock 清理（daemon 異常時）
Remove-Item -Force "$env:USERPROFILE\.mneme\.lock" -ErrorAction SilentlyContinue
```

驗證 MCP 連線：在 OpenCode 中執行 `stats()`。

---

## 5. OpenCode 設定

`~/.config/opencode/opencode.json`：

```json
{
  "mcp": {
    "mneme": {
      "command": ["D:\\Agent\\resources\\tools\\common\\mneme\\mneme.exe", "run"],
      "timeout": 120000,
      "enabled": true,
      "type": "local"
    }
  }
}
```

---

## 6. 專案 INDEX chain 建置

一般專案請依照 `reference/chain/INDEX.md` 的「Project setup guide」建置 INDEX chain。

## 7. 驗證清單

```powershell
git --version
D:\Agent\resources\tools\common\mneme\mneme.exe status
Test-Path "D:\Agent\resources\tools\common\git\INDEX.md"
Test-Path "D:\Agent\TOOLS"
```

---

## 7. 常見問題

| 症狀 | 原因 | 解決 |
|------|------|------|
| mneme daemon 起不來 | lock 殘留 | `Remove-Item $env:USERPROFILE\.mneme\.lock -Force` |
| resources 是空的 | 未從範本複製 | `Copy-Item -Recurse template/resources resources` |
| 舊 TOOLS/ 殘留 | 升級 | `Remove-Item -Recurse TOOLS -Force` |
