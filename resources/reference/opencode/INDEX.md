# opencode — Rules & Notes

OpenCode 桌面應用程式的使用規則與注意事項。

## Restart

### Before restarting

- **Announce first** — the script will hang the agent session; tell the user before executing
- Save any unsaved work — restart is immediate after the kill signal

### Path dependencies

- Hardcoded to `%LOCALAPPDATA%` — machine-specific, not portable
- Requires `OpenCode.exe` at the standard install path
- `taskkill /f /im OpenCode.exe` only targets OpenCode, safe for other processes

### Script variants

| Script | Starts mneme | Use case |
|--------|-------------|----------|
| `opencode/restart.ps1` | Yes | Full restart with mneme daemon |
| `opencode/restart.py` | No | Quick restart if mneme is already running |

### Auto-restart workflow

Restart + send a message to yourself after reboot:

```powershell
# 1. Start background timer (20s delay for OpenCode to initialize)
Start-Process -FilePath "powershell" -ArgumentList "-NoProfile -ExecutionPolicy Bypass -Command `"Start-Sleep -Seconds 20; powershell -NoProfile -ExecutionPolicy Bypass -File 'D:\Agent\resources\tools\common\opencode\send.ps1' -Text 'check - restart OK'`"" -WindowStyle Hidden

# 2. Execute restart
powershell -NoProfile -ExecutionPolicy Bypass -File D:\Agent\resources\tools\common\opencode\restart.ps1
```

Adjust the `Start-Sleep` seconds based on machine speed. 15-20s is typical.

### After restart

- OpenCode reloads `.opencode/tools/` from disk — no reinstall needed
- Verify tools and config are loaded correctly
- If mneme daemon fails, check `~/.mneme/.lock`

## Send message to self

Use `send.ps1` to send a message to the OpenCode chat window programmatically.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File D:\Agent\resources\tools\common\opencode\send.ps1 -Text "your message"
```

- Uses clipboard paste (`Ctrl+V`) to bypass IME issues
- Window title must match `"OpenCode"` exactly
- For background triggers, combine with `Start-Process` and `Start-Sleep`

## Referenced by

- [reference/INDEX.md](../INDEX.md)
