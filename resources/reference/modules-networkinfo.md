# NetworkInfo Module — Adapter Info Display

Displays active network adapters with IPv4 addresses. Minimal IFeatureModule + custom dialog.

## Architecture

```
NetworkInfoModule (IFeatureModule only — no IDropActionProvider)
  └── creates → NetworkInfoDialog (modal, scrollable, copy-button-per-IP)
```

## Key patterns

| Aspect | Detail |
|--------|--------|
| **Context menu** | 1 item: "Network Info..." under Function submenu. Opens modal dialog. |
| **Drop** | N/A — no drag-drop support. |
| **Config** | Only `Enabled` (Bool). No other config fields. |
| **Dependency** | `-liphlpapi` via module.ini Libraries field (GetAdaptersAddresses). |

## Dialog features

- ListView-less: iterates adapters via `GetAdaptersAddresses(AF_INET)`
- Per-IP copy button with shell32 icon (index 261, owner-draw fallback)
- Scrollable content (`WS_VSCROLL` + mouse wheel)
- Auto-sized height (max 500px)
- Dialog width: 340px, adapter info width: 300px

## Implementation notes

- Only IPv4 (AF_INET). IPv6 not shown.
- Adapter name, description, status, and per-IP address listed.
- Copy button sends IP text to clipboard via `ClipboardService`.
