# NetworkInfo

Shows the machine's real network configuration, plus the RDP / WinRM state used
for remote-init backup and restore. Right-click → Function → Network Info...

## Implementation

- `NetworkInfoModule.h/cpp` — IFeatureModule with context menu item
- `UI/NetworkInfoDialog` holds the two-tab window:
  - **網路資訊**: one card per active adapter (friendly name + driver
    description) with `MAC`, `狀態` (link speed + MTU), `IPv4` (address /
    mask / prefix), `IPv6`, `預設閘道`, `DNS`, `WINS`, and `DHCP`
    (server + lease obtained/expires). Every row has a copy button that puts
    the raw value on the clipboard.
  - **遠端控制初始化**: a read-only multi-line `EDIT` with the live RDP / WinRM /
    firewall / group state (selectable for copy), followed by the remote-init
    checkbox list and the refresh / backup / restore / apply buttons.
- Enumeration uses `GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX |
  GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_INCLUDE_WINS_INFO)`; DHCP lease, WINS
  and the DHCP server come from the legacy `GetAdaptersInfo` view (same
  `AdapterName` GUID), because the modern struct does not expose them.
- The dialog reads the OS directly; nothing here comes from
  `Config_NetworkInfo.ini`.

## Config

- `Config_NetworkInfo.ini` — only `Enabled` (Bool, default 1)
