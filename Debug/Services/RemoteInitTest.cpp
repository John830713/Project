// RemoteInitTest.cpp - Tests for RemoteInitService
//
// Covers everything that does not need an elevated process: the environment
// probe, the default checkbox list, and the backup file round-trip. Applying
// settings (reg/netsh/sc/winrm) is exercised by hand; here only the "nothing
// selected" path is checked so the elevation guard stays covered.
//
// mingw32-make Debug/Services/RemoteInitTest.exe
// (or add to test: target in Makefile)

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Services/ProcessRunner.h"
#include "Services/RemoteInitService.h"

static int g_tests = 0;
static int g_fails = 0;

#define CHECK(cond, msg) do { \
    g_tests++; \
    if (!(cond)) { \
        printf("FAIL %s\n", msg); \
        g_fails++; \
    } else { \
        printf("OK   %s\n", msg); \
    } } while (0)

static bool HasItem(const std::vector<RemoteInitItem>& items, const wchar_t* id) {
    for (const auto& item : items) {
        if (item.id == id) return true;
    }
    return false;
}

static const RemoteInitItem* FindItem(const std::vector<RemoteInitItem>& items,
                                      const wchar_t* id) {
    for (const auto& item : items) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

static bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static bool ContainsW(const std::wstring& haystack, const std::wstring& needle) {
    return haystack.find(needle) != std::wstring::npos;
}

int main() {
    RemoteInitService* service = RemoteInitService::Get();
    CHECK(service != nullptr, "singleton available");
    CHECK(service == RemoteInitService::Get(), "singleton is stable");

    //--- environment probe -------------------------------------------------
    RemoteInitStatus status = service->CheckEnvironment();
    CHECK(!status.productName.empty(), "product name detected");
    CHECK(!status.productVersion.empty(), "product version detected");
    CHECK(!status.buildNumber.empty(), "build number detected");
    CHECK(!status.computerName.empty(), "computer name detected");
    CHECK(!status.domainOrWorkgroup.empty(), "domain/workgroup detected");
    CHECK(!status.rdpGroupName.empty(), "rdp group name resolved");
    CHECK(status.rdpPort > 0 && status.rdpPort <= 65535, "rdp port is valid");
    CHECK(status.rdpSecurityLayer <= 2, "rdp security layer is in range");
    CHECK(status.winrmStartType >= 2 && status.winrmStartType <= 4,
          "winrm start type is valid");
    CHECK(status.winrmHttpListener == !status.winrmListener.empty(),
          "winrm listener flag matches its text");
    CHECK(!status.firewallProfiles.empty(), "firewall profile state detected");
    CHECK(!status.networkCategory.empty(), "network category detected");
    CHECK(status.isElevated == ProcessRunner::IsElevated(),
          "elevation matches ProcessRunner");

    //--- default item list -------------------------------------------------
    std::vector<RemoteInitItem> items = service->GetDefaultItems(status);
    CHECK(items.size() == 7, "seven remote init items");
    CHECK(!items.empty() && items.front().id == L"rdp", "rdp is the first item");

    CHECK(HasItem(items, L"nla"), "nla item present");
    CHECK(HasItem(items, L"rdp_firewall"), "rdp firewall item present");
    CHECK(HasItem(items, L"winrm"), "winrm item present");
    CHECK(HasItem(items, L"winrm_firewall"), "winrm firewall item present");
    CHECK(HasItem(items, L"rdp_users"), "rdp users item present");
    CHECK(HasItem(items, L"blankpass"), "blank password item present");

    bool allDescribed = true;
    for (const auto& item : items) {
        if (item.id.empty() || item.label.empty() || item.desc.empty()) {
            allDescribed = false;
        }
    }
    CHECK(allDescribed, "every item has id/label/desc");

    const RemoteInitItem* rdp = FindItem(items, L"rdp");
    if (rdp) {
        if (status.isWindowsHome) {
            CHECK(!rdp->selectable, "rdp item disabled on Home editions");
            CHECK(!rdp->checked, "disabled rdp item starts unchecked");
        } else {
            CHECK(rdp->selectable, "rdp item selectable on Pro editions");
            CHECK(rdp->checked == (status.rdpDeny != 0),
                  "rdp checked when RDP is currently disabled");
        }
    }

    const RemoteInitItem* nla = FindItem(items, L"nla");
    if (nla) {
        CHECK(nla->checked == (status.nlaUserAuth != 1),
              "nla checked when NLA is currently off");
    }

    const RemoteInitItem* rdpFw = FindItem(items, L"rdp_firewall");
    if (rdpFw) {
        CHECK(rdpFw->checked == !status.rdpFirewallEnabled,
              "rdp firewall checked when the rule is missing or disabled");
    }

    const RemoteInitItem* winrmFw = FindItem(items, L"winrm_firewall");
    if (winrmFw) {
        CHECK(winrmFw->checked == !status.winrmFirewallEnabled,
              "winrm firewall checked when the rule is missing or disabled");
    }

    const RemoteInitItem* rdpUsers = FindItem(items, L"rdp_users");
    if (rdpUsers) {
        CHECK(rdpUsers->checked == !status.userInRdpGroup,
              "rdp users checked when the account is not in the group");
    }

    const RemoteInitItem* blankPass = FindItem(items, L"blankpass");
    if (blankPass) {
        CHECK(blankPass->selectable, "blank password item selectable");
        CHECK(!blankPass->checked, "blank password item never checked by default");
    }

    //--- backup round-trip -------------------------------------------------
    std::wstring backupPath = service->CreateBackup(status);
    CHECK(!backupPath.empty(), "backup path returned");
    CHECK(std::filesystem::exists(backupPath), "backup file written");

    std::string content;
    {
        std::ifstream file(backupPath.c_str(), std::ios::binary);
        if (file.is_open()) {
            content.assign(std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>());
        }
    }
    CHECK(!content.empty(), "backup file is not empty");
    CHECK(Contains(content, "\"rdpDeny\""), "backup stores rdpDeny");
    CHECK(Contains(content, "\"rdpPort\""), "backup stores rdpPort");
    CHECK(Contains(content, "\"rdpSecurityLayer\""),
          "backup stores rdpSecurityLayer");
    CHECK(Contains(content, "\"nlaUserAuth\""), "backup stores nlaUserAuth");
    CHECK(Contains(content, "\"limitBlankPass\""), "backup stores limitBlankPass");
    CHECK(Contains(content, "\"winrmStartType\""), "backup stores winrmStartType");
    CHECK(Contains(content, "\"winrmRunning\""), "backup stores winrmRunning");
    CHECK(Contains(content, "\"rdpFirewall\""), "backup stores rdpFirewall");
    CHECK(Contains(content, "\"rdpFirewallEnabled\""),
          "backup stores rdpFirewallEnabled");
    CHECK(Contains(content, "\"winrmFirewall\""), "backup stores winrmFirewall");
    CHECK(Contains(content, "\"winrmFirewallEnabled\""),
          "backup stores winrmFirewallEnabled");
    CHECK(Contains(content, "\"rdpUsers\""), "backup stores rdpUsers");
    CHECK(Contains(content, "\"rdpDeny\": " + std::to_string(status.rdpDeny)),
          "backup value matches current rdpDeny");
    CHECK(Contains(content, "\"rdpPort\": " + std::to_string(status.rdpPort)),
          "backup value matches current rdpPort");

    CHECK(!service->RestoreBackup(L""), "restore rejects an empty path");
    CHECK(!service->RestoreBackup(L"Z:\\nonexistent\\remote_init.json"),
          "restore rejects a missing file");

    //--- apply guard -------------------------------------------------------
    std::vector<RemoteInitItem> none;
    RemoteInitResult result = service->ApplySelected(none, L"tester");
    if (ProcessRunner::IsElevated()) {
        CHECK(result.allOk, "nothing selected reports success");
        CHECK(!result.logs.empty() && ContainsW(result.logs.front(), L"SKIP"),
              "nothing selected logs a skip line");
    } else {
        CHECK(!result.allOk, "non-elevated apply is refused");
        CHECK(result.logs.size() == 1, "non-elevated apply logs one line");
    }

    //--- cleanup -----------------------------------------------------------
    if (!backupPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(backupPath, ec);
        std::filesystem::path dir = std::filesystem::path(backupPath).parent_path();
        if (dir.filename() == L"RemoteInit") {
            std::filesystem::remove(dir, ec);  // only removes it when empty
        }
    }

    printf("\n%d passed, %d failed\n", g_tests - g_fails, g_fails);
    return g_fails == 0 ? 0 : 1;
}
