# NetworkInfo

Lists active network adapters with IP addresses. Right-click → Function → Network Info...

## Implementation

- `NetworkInfoModule.h/cpp` — IFeatureModule with context menu item
- Uses `UI/NetworkInfoDialog` for the ListView-based display
- `GetAdaptersAddresses` (iphlpapi) for network enumeration
- Copy button copies selected IP to clipboard

## Config

- `Config_NetworkInfo.ini` — only `Enabled` (Bool, default 1)
