#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Snapshot of every machine setting the remote-init UI offers to change.
//
// Everything here is read live from the OS (registry / SCM / firewall COM /
// NetAPI / winrm), never from the app's own config files, so the dialog shows
// the machine's real state and CreateBackup can capture it for a later restore.
struct RemoteInitStatus {
    bool isElevated = false;
    bool isWindowsHome = false;
    std::wstring productName;        // Windows 11 Pro
    std::wstring productVersion;     // 23H2 (DisplayVersion)
    std::wstring buildNumber;        // 22631
    std::wstring computerName;
    std::wstring domainOrWorkgroup;  // CONTOSO or WORKGROUP
    bool domainJoined = false;
    std::wstring networkCategory;  // 公用 / 私人 / 網域 / 未知

    DWORD rdpDeny = 1;         // Terminal Server\fDenyTSConnections
    DWORD nlaUserAuth = 1;     // Terminal Server\WinStations\RDP-Tcp\UserAuthentication
    DWORD limitBlankPass = 1;  // Lsa\LimitBlankPasswordUse
    DWORD rdpPort = 3389;      // Terminal Server\WinStations\RDP-Tcp\PortNumber
    DWORD rdpSecurityLayer = 1;  // 0=RDP 1=Negotiate 2=SSL/TLS

    bool winrmServiceRunning = false;
    DWORD winrmStartType = 3;  // 2=auto 3=demand 4=disabled
    bool winrmHttpListener = false;
    std::wstring winrmListener;  // "HTTP *:5985" (empty when none)

    bool rdpFirewallRule = false;
    bool rdpFirewallEnabled = false;
    bool winrmFirewallRule = false;
    bool winrmFirewallEnabled = false;
    std::wstring firewallProfiles;  // e.g. "domain on / private on / public off"

    bool userInRdpGroup = true;
    std::wstring rdpGroupName;
    std::vector<std::wstring> rdpUsers;  // domain\name entries of the local group
};

// One checkbox row. `id` is the stable key used by ApplySelected.
struct RemoteInitItem {
    std::wstring id;
    std::wstring label;
    std::wstring desc;
    bool checked = false;
    bool selectable = true;
};

struct RemoteInitResult {
    bool allOk = true;
    std::vector<std::wstring> logs;
};

// Remote control / remote debugging initialisation (RDP + WinRM).
//
// Every change is applied through plain commands (reg / netsh / sc / winrm /
// net) so the same code path works for cmd, PowerShell and registry tweaks.
// All apply/restore operations require an elevated process; the caller checks
// RemoteInitStatus::isElevated first and offers ProcessRunner::RestartElevated.
class RemoteInitService {
public:
    static RemoteInitService* Get();

    RemoteInitStatus CheckEnvironment() const;

    // Default checkbox list: already-satisfied items start unchecked, and
    // "allow blank password network logon" always starts unchecked.
    std::vector<RemoteInitItem> GetDefaultItems(const RemoteInitStatus& status) const;

    // Writes the current values to Log/RemoteInit/backup_<timestamp>.json.
    // Returns an empty string when the backup could not be written.
    std::wstring CreateBackup(const RemoteInitStatus& status) const;

    // Re-applies the values stored in a backup file.
    bool RestoreBackup(const std::wstring& path) const;

    // Applies every checked + selectable item, in list order.
    RemoteInitResult ApplySelected(const std::vector<RemoteInitItem>& items,
                                   const std::wstring& userName) const;

public:
    // Firewall rule names created by this service (also used by restore).
    static constexpr const wchar_t* kRdpRuleName = L"RemoteInit-RDP-3389";
    static constexpr const wchar_t* kWinrmRuleName = L"RemoteInit-WinRM-5985";

private:
    RemoteInitService() = default;

    bool ApplyItem(const std::wstring& id, const std::wstring& userName,
                   std::vector<std::wstring>& logs) const;
};
