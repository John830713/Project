#include "RemoteInitService.h"

#include "../Core/Logger.h"
#include "AdminCommand.h"
#include "FileService.h"
#include "ProcessRunner.h"

#include <lmaccess.h>
#include <lmapibuf.h>
#include <lmerr.h>
#include <lmjoin.h>
#include <netfw.h>
#include <netlistmgr.h>
#include <securitybaseapi.h>
#include <winsvc.h>

#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

namespace {

const wchar_t* const kRdpKey =
    L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server";
const wchar_t* const kRdpTcpKey =
    L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\WinStations\\RDP-Tcp";
const wchar_t* const kLsaKey = L"SYSTEM\\CurrentControlSet\\Control\\Lsa";
const wchar_t* const kVersionKey =
    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

// reg.exe needs a root key name (HKLM\\...) while the constants above are
// relative to HKEY_LOCAL_MACHINE for RegGetValueW; prefix it here so both
// paths stay in sync.
std::wstring RegAddCommand(const wchar_t* key, const wchar_t* name, long value) {
    return L"reg add \"HKLM\\" + std::wstring(key) + L"\" /v " + name +
           L" /t REG_DWORD /d " + std::to_wstring(value) + L" /f";
}

DWORD ReadDword(const wchar_t* key, const wchar_t* name, DWORD fallback) {
    DWORD value = fallback;
    DWORD size = sizeof(value);
    LONG rc = RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_DWORD,
                           nullptr, &value, &size);
    return rc == ERROR_SUCCESS ? value : fallback;
}

std::wstring ReadString(const wchar_t* key, const wchar_t* name) {
    wchar_t buffer[512] = {};
    DWORD size = sizeof(buffer);
    LONG rc = RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ,
                           nullptr, buffer, &size);
    return rc == ERROR_SUCCESS ? std::wstring(buffer) : std::wstring();
}

std::string Utf8FromWide(const std::wstring& text) {
    if (text.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                                   nullptr, 0, nullptr, nullptr);
    if (size <= 0) return std::string();
    std::string out((size_t)size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0],
                        size, nullptr, nullptr);
    return out;
}

std::wstring WideFromUtf8(const std::string& text) {
    if (text.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(),
                                   nullptr, 0);
    if (size <= 0) return std::wstring();
    std::wstring out((size_t)size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0],
                        size);
    return out;
}

std::wstring ToLower(std::wstring text) {
    for (auto& c : text) c = static_cast<wchar_t>(towlower(c));
    return text;
}

// Child-process output is shown inside a MessageBox: flatten it and cap the
// length so one noisy command cannot fill the dialog.
std::wstring OneLine(const std::wstring& text, size_t maxLen = 200) {
    std::wstring out;
    bool lastSpace = false;
    for (wchar_t c : text) {
        if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
        if (c == L' ') {
            if (lastSpace) continue;
            lastSpace = true;
        } else {
            lastSpace = false;
        }
        out += c;
        if (out.size() >= maxLen) {
            out += L"...";
            break;
        }
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    return out;
}

bool RunStep(const std::wstring& label, const std::wstring& args,
             std::vector<std::wstring>& logs) {
    ProcessResult result = ProcessRunner::RunCmd(args);
    if (result.Ok()) {
        logs.push_back(L"[OK] " + label);
        return true;
    }

    std::wstring line = L"[FAIL] " + label + L" (exit=" +
                        std::to_wstring(result.exitCode) + L")";
    std::wstring output = OneLine(result.output);
    if (!output.empty()) line += L": " + output;
    logs.push_back(line);
    return false;
}

bool FirewallRuleExists(const std::wstring& name) {
    ProcessResult result = ProcessRunner::RunCmd(
        L"netsh advfirewall firewall show rule name=\"" + name + L"\"");
    if (!result.started) return false;
    return ToLower(result.output).find(ToLower(name)) != std::wstring::npos;
}

// Adding a rule twice would leave duplicates behind, so enable the existing
// rule when it is already there.
bool EnsureFirewallRule(const std::wstring& name, const std::wstring& port,
                        std::vector<std::wstring>& logs) {
    if (FirewallRuleExists(name)) {
        return RunStep(L"啟用防火牆規則 " + name,
                       L"netsh advfirewall firewall set rule name=\"" + name +
                           L"\" new enable=Yes",
                       logs);
    }
    return RunStep(L"建立防火牆規則 " + name + L" (TCP " + port + L")",
                   L"netsh advfirewall firewall add rule name=\"" + name +
                       L"\" dir=in action=allow protocol=TCP localport=" + port +
                       L" profile=any",
                   logs);
}

// winrm prints its listener inventory in English regardless of the UI locale,
// so parsing "Transport = HTTP" / "Port = 5985" is stable. Returns
// "HTTP *:5985" (or the real address/port) and leaves it empty when no HTTP
// listener is configured.
std::wstring QueryWinrmHttpListener() {
    ProcessResult result = ProcessRunner::RunCmd(L"winrm enumerate winrm/config/listener");
    if (!result.started || result.output.empty()) return L"";

    std::wstring address;
    std::wstring transport;
    std::wstring port;

    auto flush = [&]() -> std::wstring {
        if (ToLower(transport) != L"http" || port.empty()) return L"";
        return L"HTTP " + address + L":" + port;
    };

    size_t pos = 0;
    while (pos <= result.output.size()) {
        size_t end = result.output.find_first_of(L"\r\n", pos);
        if (end == std::wstring::npos) end = result.output.size();
        std::wstring line = result.output.substr(pos, end - pos);
        pos = end + 1;
        if (pos < result.output.size() &&
            (result.output[pos] == L'\n' || result.output[pos] == L'\r')) {
            ++pos;
        }

        while (!line.empty() && (line.back() == L' ' || line.back() == L'\t')) {
            line.pop_back();
        }
        size_t first = line.find_first_not_of(L" \t");
        if (first != std::wstring::npos) line.erase(0, first);

        if (line.empty()) {
            std::wstring listener = flush();
            if (!listener.empty()) return listener;
            address.clear();
            transport.clear();
            port.clear();
            continue;
        }

        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = line.substr(0, eq);
        std::wstring value = line.substr(eq + 1);
        while (!key.empty() && (key.back() == L' ' || key.back() == L'\t')) key.pop_back();
        size_t vfirst = value.find_first_not_of(L" \t");
        value = vfirst == std::wstring::npos ? L"" : value.substr(vfirst);

        if (key == L"Address") address = value;
        else if (key == L"Transport") transport = value;
        else if (key == L"Port") port = value;
    }

    return flush();
}

// Local group members through NetAPI (locale-independent, unlike parsing
// `net localgroup`). Returns domain\name strings; empty on access denied.
std::vector<std::wstring> QueryLocalGroupMembers(const std::wstring& group) {
    std::vector<std::wstring> members;
    if (group.empty()) return members;

    LPBYTE buffer = nullptr;
    DWORD read = 0;
    DWORD total = 0;
    NET_API_STATUS status = NetLocalGroupGetMembers(
        nullptr, group.c_str(), 3, &buffer, MAX_PREFERRED_LENGTH, &read,
        &total, nullptr);
    if (status != NERR_Success || !buffer) return members;

    auto* entries = reinterpret_cast<LOCALGROUP_MEMBERS_INFO_3*>(buffer);
    for (DWORD i = 0; i < read; ++i) {
        if (entries[i].lgrmi3_domainandname) {
            members.push_back(entries[i].lgrmi3_domainandname);
        }
    }
    NetApiBufferFree(buffer);
    return members;
}

// Matches both "MACHINE\user" and a bare "user" entry.
bool GroupHasUser(const std::vector<std::wstring>& members,
                  const std::wstring& user) {
    if (user.empty()) return false;
    std::wstring wanted = ToLower(user);
    for (const auto& entry : members) {
        std::wstring lower = ToLower(entry);
        if (lower == wanted) return true;
        size_t slash = lower.rfind(L'\\');
        if (slash != std::wstring::npos && lower.substr(slash + 1) == wanted) {
            return true;
        }
    }
    return false;
}

DWORD QueryServiceStartType(const wchar_t* serviceName) {
    DWORD startType = 3;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return startType;
    SC_HANDLE service = OpenServiceW(scm, serviceName, SERVICE_QUERY_CONFIG);
    if (service) {
        DWORD needed = 0;
        QueryServiceConfigW(service, nullptr, 0, &needed);
        if (needed > 0) {
            std::vector<BYTE> buffer(needed);
            auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
            if (QueryServiceConfigW(service, config, needed, &needed)) {
                startType = config->dwStartType;
            }
        }
        CloseServiceHandle(service);
    }
    CloseServiceHandle(scm);
    return startType;
}

// Firewall rules are read through INetFwPolicy2 instead of `netsh ... show
// rule`, whose field labels are localised and therefore unparsable.
struct FirewallInfo {
    bool rdpRule = false;
    bool rdpEnabled = false;
    bool winrmRule = false;
    bool winrmEnabled = false;
    std::wstring profiles;  // "domain on / private on / public off"
};

const wchar_t* ProfileStateWord(bool enabled) {
    return enabled ? L"on" : L"off";
}

// netfw.h declares CLSID_NetFwPolicy2 / IID_INetFwPolicy2 as extern and the
// MinGW libuuid does not ship them, so define the two GUIDs locally.
const CLSID kNetFwPolicy2Clsid = {
    0xe2b3c97f, 0x6ae1, 0x41ac, { 0x81, 0x7a, 0xf6, 0xf9, 0x21, 0x66, 0xd7, 0xdd }
};
const IID kNetFwPolicy2Iid = {
    0x98325047, 0xc671, 0x4174, { 0x8d, 0x81, 0xde, 0xfc, 0xd3, 0xf0, 0x31, 0x86 }
};

FirewallInfo QueryFirewallInfo() {
    FirewallInfo info;

    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool comInitialized = (hrCom == S_OK);

    INetFwPolicy2* policy = nullptr;
    HRESULT hr = CoCreateInstance(kNetFwPolicy2Clsid, nullptr, CLSCTX_ALL,
                                  kNetFwPolicy2Iid,
                                  reinterpret_cast<void**>(&policy));
    if (hr == S_OK && policy) {
        VARIANT_BOOL on = VARIANT_FALSE;
        std::wstring domain, priv, pub;
        if (SUCCEEDED(policy->get_FirewallEnabled(NET_FW_PROFILE2_DOMAIN, &on))) {
            domain = std::wstring(L"domain ") + ProfileStateWord(on != VARIANT_FALSE);
        }
        if (SUCCEEDED(policy->get_FirewallEnabled(NET_FW_PROFILE2_PRIVATE, &on))) {
            priv = std::wstring(L"private ") + ProfileStateWord(on != VARIANT_FALSE);
        }
        if (SUCCEEDED(policy->get_FirewallEnabled(NET_FW_PROFILE2_PUBLIC, &on))) {
            pub = std::wstring(L"public ") + ProfileStateWord(on != VARIANT_FALSE);
        }
        info.profiles = domain + L" / " + priv + L" / " + pub;

        INetFwRules* rules = nullptr;
        if (SUCCEEDED(policy->get_Rules(&rules)) && rules) {
            auto lookup = [&](const wchar_t* name, bool& exists, bool& enabled) {
                INetFwRule* rule = nullptr;
                BSTR ruleName = SysAllocString(name);
                if (ruleName &&
                    SUCCEEDED(rules->Item(ruleName, &rule)) && rule) {
                    exists = true;
                    VARIANT_BOOL ruleEnabled = VARIANT_FALSE;
                    if (SUCCEEDED(rule->get_Enabled(&ruleEnabled))) {
                        enabled = ruleEnabled != VARIANT_FALSE;
                    }
                    rule->Release();
                }
                if (ruleName) SysFreeString(ruleName);
            };
            lookup(RemoteInitService::kRdpRuleName, info.rdpRule, info.rdpEnabled);
            lookup(RemoteInitService::kWinrmRuleName, info.winrmRule,
                   info.winrmEnabled);
            rules->Release();
        }
        policy->Release();
    }

    if (comInitialized) CoUninitialize();
    return info;
}

// NetGetJoinInformation distinguishes a domain join from a workgroup and is
// the only locale-independent way to read that name.
void QueryJoinInfo(RemoteInitStatus& status) {
    LPWSTR name = nullptr;
    NETSETUP_JOIN_STATUS joinStatus = NetSetupUnknownStatus;
    if (NetGetJoinInformation(nullptr, &name, &joinStatus) == NERR_Success) {
        status.domainJoined = joinStatus == NetSetupDomainName;
        status.domainOrWorkgroup = name ? name : L"";
        if (name) NetApiBufferFree(name);
    }
}

// Built-in group names are localised, so resolve "Remote Desktop Users" from
// its well-known SID instead of hard-coding the English name.
std::wstring RdpGroupName() {
    const wchar_t* fallback = L"Remote Desktop Users";

    BYTE sidBuffer[SECURITY_MAX_SID_SIZE] = {};
    DWORD sidSize = sizeof(sidBuffer);
    if (!CreateWellKnownSid(WinBuiltinRemoteDesktopUsersSid, nullptr,
                            sidBuffer, &sidSize)) {
        return fallback;
    }

    wchar_t name[256] = {};
    DWORD nameLen = 256;
    wchar_t domain[256] = {};
    DWORD domainLen = 256;
    SID_NAME_USE use = SidTypeGroup;
    if (!LookupAccountSidW(nullptr, sidBuffer, name, &nameLen,
                           domain, &domainLen, &use)) {
        return fallback;
    }
    return name;
}

std::wstring CurrentUserName() {
    wchar_t name[256] = {};
    DWORD size = 256;
    if (!GetUserNameW(name, &size)) return L"";
    return name;
}

// Fallback for a non-elevated process: NetLocalGroupGetMembers needs admin
// rights, `net localgroup` usually does not.
bool LocalGroupHasUser(const std::wstring& group, const std::wstring& userName) {
    if (group.empty() || userName.empty()) return false;
    ProcessResult result = ProcessRunner::RunCmd(L"net localgroup \"" + group + L"\"");
    if (!result.started) return false;
    return ToLower(result.output).find(ToLower(userName)) != std::wstring::npos;
}

std::wstring QueryNetworkCategory() {
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool comInitialized = (hrCom == S_OK);
    std::wstring category = L"未知";

    INetworkListManager* manager = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL,
                                  IID_INetworkListManager,
                                  reinterpret_cast<void**>(&manager));
    if (hr == S_OK && manager) {
        IEnumNetworks* networks = nullptr;
        if (SUCCEEDED(manager->GetNetworks(NLM_ENUM_NETWORK_CONNECTED, &networks)) &&
            networks) {
            INetwork* network = nullptr;
            ULONG fetched = 0;
            if (networks->Next(1, &network, &fetched) == S_OK && network) {
                NLM_NETWORK_CATEGORY value = NLM_NETWORK_CATEGORY_PUBLIC;
                if (SUCCEEDED(network->GetCategory(&value))) {
                    switch (value) {
                        case NLM_NETWORK_CATEGORY_PRIVATE: category = L"私人"; break;
                        // Mingw headers stop at PUBLIC/PRIVATE; the SDK value is 2.
                        case 2: category = L"網域"; break;
                        case NLM_NETWORK_CATEGORY_PUBLIC: category = L"公用"; break;
                        default: break;
                    }
                }
                network->Release();
            }
            networks->Release();
        }
        manager->Release();
    }

    if (comInitialized) CoUninitialize();
    return category;
}

// The backup file is written by this service, so a key scan is enough and
// keeps the build free of a JSON dependency.
bool JsonFindKey(const std::string& text, const char* key, std::string& value) {
    std::string needle = std::string("\"") + key + "\"";
    size_t pos = text.find(needle);
    if (pos == std::string::npos) return false;
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
    size_t end = text.find_first_of(",}\r\n", pos);
    if (end == std::string::npos) end = text.size();
    value = text.substr(pos, end - pos);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    return true;
}

bool JsonGetInt(const std::string& text, const char* key, long& out) {
    std::string value;
    if (!JsonFindKey(text, key, value) || value.empty()) return false;
    char* stop = nullptr;
    long parsed = std::strtol(value.c_str(), &stop, 10);
    if (stop == value.c_str()) return false;
    out = parsed;
    return true;
}

// The file is UTF-8, so escape only what JSON requires (the bytes of
// multi-byte sequences pass through untouched).
std::string JsonEscape(const std::wstring& text) {
    std::string utf8 = Utf8FromWide(text);
    std::string out;
    out.reserve(utf8.size() + 8);
    for (unsigned char c : utf8) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8] = {};
                    snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

// Reads the "rdpUsers" array written by CreateBackup. Old backups (version 1)
// have no such key and simply return false.
bool JsonGetStringArray(const std::string& text, const char* key,
                        std::vector<std::wstring>& out) {
    std::string needle = std::string("\"") + key + "\"";
    size_t pos = text.find(needle);
    if (pos == std::string::npos) return false;
    pos = text.find('[', pos + needle.size());
    if (pos == std::string::npos) return false;
    size_t end = text.find(']', pos);
    if (end == std::string::npos) return false;

    out.clear();
    size_t i = pos + 1;
    while (i < end) {
        while (i < end && (text[i] == ' ' || text[i] == '\t' ||
                           text[i] == '\r' || text[i] == '\n' ||
                           text[i] == ',')) {
            ++i;
        }
        if (i >= end || text[i] != '"') break;
        ++i;

        std::string item;
        while (i < end && text[i] != '"') {
            char c = text[i];
            if (c == '\\' && i + 1 < end) {
                char esc = text[i + 1];
                i += 2;
                switch (esc) {
                    case 'n': item += '\n'; break;
                    case 'r': item += '\r'; break;
                    case 't': item += '\t'; break;
                    case 'b': item += '\b'; break;
                    case 'f': item += '\f'; break;
                    case 'u':
                        if (i + 4 <= end) {
                            unsigned code = static_cast<unsigned>(
                                std::strtoul(text.substr(i, 4).c_str(), nullptr, 16));
                            i += 4;
                            wchar_t wide = static_cast<wchar_t>(code);
                            item += Utf8FromWide(std::wstring(1, wide));
                        }
                        break;
                    default: item += esc; break;
                }
            } else {
                item += c;
                ++i;
            }
        }
        if (i < end && text[i] == '"') ++i;
        out.push_back(WideFromUtf8(item));
    }
    return true;
}

std::wstring ServiceStartKeyword(long startType) {
    switch (startType) {
        case 2: return L"auto";
        case 4: return L"disabled";
        default: return L"demand";
    }
}

}  // namespace

RemoteInitService* RemoteInitService::Get() {
    static RemoteInitService instance;
    return &instance;
}

RemoteInitStatus RemoteInitService::CheckEnvironment() const {
    RemoteInitStatus status;
    status.isElevated = ProcessRunner::IsElevated();
    status.productName = ProcessRunner::GetProductName();
    status.isWindowsHome = status.productName.find(L"Home") != std::wstring::npos;
    status.productVersion = ReadString(kVersionKey, L"DisplayVersion");
    status.buildNumber = ReadString(kVersionKey, L"CurrentBuildNumber");
    status.networkCategory = QueryNetworkCategory();

    wchar_t computer[256] = {};
    DWORD computerSize = 256;
    if (GetComputerNameW(computer, &computerSize)) {
        status.computerName = computer;
    }
    QueryJoinInfo(status);

    status.rdpDeny = ReadDword(kRdpKey, L"fDenyTSConnections", 1);
    status.nlaUserAuth = ReadDword(kRdpTcpKey, L"UserAuthentication", 1);
    status.limitBlankPass = ReadDword(kLsaKey, L"LimitBlankPasswordUse", 1);
    status.rdpPort = ReadDword(kRdpTcpKey, L"PortNumber", 3389);
    status.rdpSecurityLayer = ReadDword(kRdpTcpKey, L"SecurityLayer", 1);

    status.winrmServiceRunning = false;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm) {
        SC_HANDLE service = OpenServiceW(scm, L"WinRM", SERVICE_QUERY_STATUS);
        if (service) {
            SERVICE_STATUS info = {};
            if (QueryServiceStatus(service, &info)) {
                status.winrmServiceRunning = info.dwCurrentState == SERVICE_RUNNING;
            }
            CloseServiceHandle(service);
        }
        CloseServiceHandle(scm);
    }
    status.winrmStartType = QueryServiceStartType(L"WinRM");
    status.winrmListener = QueryWinrmHttpListener();
    status.winrmHttpListener = !status.winrmListener.empty();

    FirewallInfo firewall = QueryFirewallInfo();
    status.rdpFirewallRule = firewall.rdpRule;
    status.rdpFirewallEnabled = firewall.rdpEnabled;
    status.winrmFirewallRule = firewall.winrmRule;
    status.winrmFirewallEnabled = firewall.winrmEnabled;
    status.firewallProfiles = firewall.profiles;

    status.rdpGroupName = RdpGroupName();
    status.rdpUsers = QueryLocalGroupMembers(status.rdpGroupName);
    std::wstring userName = CurrentUserName();
    if (status.rdpUsers.empty()) {
        // Access denied without elevation: fall back to the net command.
        status.userInRdpGroup = LocalGroupHasUser(status.rdpGroupName, userName);
    } else {
        status.userInRdpGroup = GroupHasUser(status.rdpUsers, userName);
    }

    return status;
}

std::vector<RemoteInitItem> RemoteInitService::GetDefaultItems(
    const RemoteInitStatus& status) const {
    std::vector<RemoteInitItem> items;

    auto firewallState = [](bool exists, bool enabled) {
        if (!exists) return std::wstring(L"規則未建立");
        return std::wstring(enabled ? L"規則已啟用" : L"規則已停用");
    };

    RemoteInitItem rdp;
    rdp.id = L"rdp";
    rdp.label = L"啟用遠端桌面 (RDP)";
    if (status.isWindowsHome) {
        rdp.desc = L"家用版不支援被遠端連線，此項目無法套用";
        rdp.state = L"不支援";
        rdp.selectable = false;
        rdp.checked = false;
    } else {
        rdp.desc = L"允許其他機台以 mstsc 連線 (連接埠 " +
                   std::to_wstring(status.rdpPort) + L")";
        rdp.state = status.rdpDeny == 0 ? L"已啟用" : L"未啟用";
        rdp.checked = status.rdpDeny != 0;
    }
    items.push_back(rdp);

    RemoteInitItem nla;
    nla.id = L"nla";
    nla.label = L"啟用網路層級驗證 (NLA)";
    nla.desc = L"連線時必須先通過帳號密碼驗證";
    nla.state = status.nlaUserAuth == 1 ? L"已啟用" : L"未啟用";
    nla.checked = status.nlaUserAuth != 1;
    items.push_back(nla);

    RemoteInitItem rdpFirewall;
    rdpFirewall.id = L"rdp_firewall";
    rdpFirewall.label = L"開啟 RDP 防火牆";
    rdpFirewall.desc = L"允許遠端桌面連線通過防火牆 (TCP " +
                       std::to_wstring(status.rdpPort) + L")";
    rdpFirewall.state =
        firewallState(status.rdpFirewallRule, status.rdpFirewallEnabled);
    rdpFirewall.checked = !status.rdpFirewallEnabled;
    items.push_back(rdpFirewall);

    std::wstring winrmState;
    if (status.winrmServiceRunning && status.winrmHttpListener) {
        winrmState = L"服務與 Listener 已就緒";
    } else if (!status.winrmServiceRunning && !status.winrmHttpListener) {
        winrmState = L"服務與 Listener 未設定";
    } else if (!status.winrmServiceRunning) {
        winrmState = L"服務未執行";
    } else {
        winrmState = L"Listener 未建立";
    }

    RemoteInitItem winrm;
    winrm.id = L"winrm";
    winrm.label = L"啟用 WinRM 遠端管理";
    winrm.desc = L"啟動 WinRM 服務並建立 HTTP Listener";
    winrm.state = winrmState;
    winrm.checked = !status.winrmServiceRunning || !status.winrmHttpListener;
    items.push_back(winrm);

    RemoteInitItem winrmFirewall;
    winrmFirewall.id = L"winrm_firewall";
    winrmFirewall.label = L"開啟 WinRM 防火牆 (TCP 5985)";
    winrmFirewall.desc = L"允許 PowerShell 遠端連線通過防火牆";
    winrmFirewall.state =
        firewallState(status.winrmFirewallRule, status.winrmFirewallEnabled);
    winrmFirewall.checked = !status.winrmFirewallEnabled;
    items.push_back(winrmFirewall);

    RemoteInitItem rdpUsers;
    rdpUsers.id = L"rdp_users";
    rdpUsers.label = L"加入遠端桌面使用者群組";
    rdpUsers.desc = L"非系統管理員帳號連線 RDP 時需要";
    rdpUsers.state = status.userInRdpGroup ? L"目前使用者已在群組"
                                           : L"目前使用者不在群組";
    rdpUsers.checked = !status.userInRdpGroup;
    items.push_back(rdpUsers);

    // Security-relevant: never enabled unless the operator ticks it.
    RemoteInitItem blankPass;
    blankPass.id = L"blankpass";
    blankPass.label = L"允許空白密碼網路登入";
    blankPass.desc = L"僅限隔離測試網路，預設不勾選";
    blankPass.state = status.limitBlankPass == 1 ? L"已限制空白密碼"
                                                 : L"允許空白密碼";
    blankPass.checked = false;
    items.push_back(blankPass);

    return items;
}

bool RemoteInitService::ApplyItem(const std::wstring& id,
                                  const std::wstring& userName,
                                  std::vector<std::wstring>& logs) const {
    if (id == L"rdp") {
        return RunStep(L"啟用遠端桌面 (fDenyTSConnections=0)",
                       RegAddCommand(kRdpKey, L"fDenyTSConnections", 0), logs);
    }
    if (id == L"nla") {
        return RunStep(L"啟用 NLA (UserAuthentication=1)",
                       RegAddCommand(kRdpTcpKey, L"UserAuthentication", 1), logs);
    }
    if (id == L"rdp_firewall") {
        DWORD port = ReadDword(kRdpTcpKey, L"PortNumber", 3389);
        return EnsureFirewallRule(kRdpRuleName, std::to_wstring(port), logs);
    }
    if (id == L"winrm_firewall") {
        return EnsureFirewallRule(kWinrmRuleName, L"5985", logs);
    }
    if (id == L"blankpass") {
        logs.push_back(L"[WARN] 空白密碼登入會讓同網段任何帳號在無密碼時登入");
        return RunStep(L"允許空白密碼網路登入 (LimitBlankPasswordUse=0)",
                       RegAddCommand(kLsaKey, L"LimitBlankPasswordUse", 0), logs);
    }
    if (id == L"rdp_users") {
        std::wstring user = userName.empty() ? CurrentUserName() : userName;
        std::wstring group = RdpGroupName();
        if (user.empty()) {
            logs.push_back(L"[FAIL] 無法取得目前使用者名稱");
            return false;
        }
        std::vector<std::wstring> members = QueryLocalGroupMembers(group);
        bool present = members.empty() ? LocalGroupHasUser(group, user)
                                       : GroupHasUser(members, user);
        if (present) {
            logs.push_back(L"[SKIP] " + user + L" 已在「" + group + L"」群組");
            return true;
        }
        return RunStep(L"將 " + user + L" 加入「" + group + L"」",
                       L"net localgroup \"" + group + L"\" \"" + user + L"\" /add",
                       logs);
    }
    if (id == L"winrm") {
        bool ok = RunStep(L"設定 WinRM 服務為自動啟動",
                          L"sc config WinRM start= auto", logs);

        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        bool running = false;
        if (scm) {
            SC_HANDLE service = OpenServiceW(scm, L"WinRM", SERVICE_QUERY_STATUS);
            if (service) {
                SERVICE_STATUS info = {};
                if (QueryServiceStatus(service, &info)) {
                    running = info.dwCurrentState == SERVICE_RUNNING;
                }
                CloseServiceHandle(service);
            }
            CloseServiceHandle(scm);
        }
        if (!running) {
            ok = RunStep(L"啟動 WinRM 服務", L"sc start WinRM", logs) && ok;
        }
        if (QueryWinrmHttpListener().empty()) {
            ok = RunStep(L"建立 WinRM HTTP Listener",
                         L"winrm create winrm/config/Listener?Address=*+Transport=HTTP",
                         logs) && ok;
        }
        return ok;
    }

    logs.push_back(L"[FAIL] 未知的項目 id: " + id);
    return false;
}

RemoteInitResult RemoteInitService::ApplySelected(
    const std::vector<RemoteInitItem>& items, const std::wstring& userName) const {
    RemoteInitResult result;

    if (!ProcessRunner::IsElevated()) {
        result.allOk = false;
        result.logs.push_back(L"[FAIL] 需要以系統管理員身分執行才能套用設定");
        return result;
    }

    for (const auto& item : items) {
        if (!item.checked || !item.selectable) continue;
        if (!ApplyItem(item.id, userName, result.logs)) {
            result.allOk = false;
        }
    }

    if (result.logs.empty()) {
        result.logs.push_back(L"[SKIP] 沒有勾選任何項目");
    }

    for (const auto& line : result.logs) {
        Logger::Write(L"RemoteInit", line);
    }
    Logger::Write(L"RemoteInit",
                  result.allOk ? L"apply finished: all ok" : L"apply finished: failures");
    return result;
}

RemoteInitResult RemoteInitService::ApplySelectedElevated(
    const std::vector<RemoteInitItem>& items, const std::wstring& userName) const {
    std::vector<std::wstring> ids;
    for (const auto& item : items) {
        if (item.checked && item.selectable) ids.push_back(item.id);
    }

    // Nothing selected: no reason to raise a UAC prompt.
    if (ids.empty()) {
        RemoteInitResult result;
        result.logs.push_back(L"[SKIP] 沒有勾選任何項目");
        return result;
    }

    if (ProcessRunner::IsElevated()) return ApplySelected(items, userName);

    AdminCommand::Result admin = AdminCommand::ApplyRemoteInit(ids, userName);
    RemoteInitResult result;
    result.allOk = admin.allOk;
    result.logs = admin.lines;
    return result;
}

std::wstring RemoteInitService::CreateBackup(const RemoteInitStatus& status) const {
    SYSTEMTIME now = {};
    GetLocalTime(&now);

    wchar_t fileName[64] = {};
    swprintf(fileName, 64, L"backup_%04d%02d%02d_%02d%02d%02d.json",
             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

    std::filesystem::path dir = Logger::GetLogDir() / L"RemoteInit";
    std::filesystem::path file = dir / fileName;

    char created[32] = {};
    sprintf(created, "%04d-%02d-%02d %02d:%02d:%02d",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

    std::string json;
    json += "{\n";
    json += std::string("  \"created\": \"") + created + "\",\n";
    json += "  \"rdpDeny\": " + std::to_string(status.rdpDeny) + ",\n";
    json += "  \"rdpPort\": " + std::to_string(status.rdpPort) + ",\n";
    json += "  \"rdpSecurityLayer\": " +
            std::to_string(status.rdpSecurityLayer) + ",\n";
    json += "  \"nlaUserAuth\": " + std::to_string(status.nlaUserAuth) + ",\n";
    json += "  \"limitBlankPass\": " + std::to_string(status.limitBlankPass) + ",\n";
    json += "  \"winrmStartType\": " +
            std::to_string(status.winrmStartType) + ",\n";
    json += "  \"winrmRunning\": " +
            std::to_string(status.winrmServiceRunning ? 1 : 0) + ",\n";
    json += "  \"rdpFirewall\": " +
            std::to_string(status.rdpFirewallRule ? 1 : 0) + ",\n";
    json += "  \"rdpFirewallEnabled\": " +
            std::to_string(status.rdpFirewallEnabled ? 1 : 0) + ",\n";
    json += "  \"winrmFirewall\": " +
            std::to_string(status.winrmFirewallRule ? 1 : 0) + ",\n";
    json += "  \"winrmFirewallEnabled\": " +
            std::to_string(status.winrmFirewallEnabled ? 1 : 0) + ",\n";
    json += "  \"rdpUsers\": [";
    for (size_t i = 0; i < status.rdpUsers.size(); ++i) {
        if (i > 0) json += ", ";
        json += "\"" + JsonEscape(status.rdpUsers[i]) + "\"";
    }
    json += "]\n";
    json += "}\n";

    try {
        std::filesystem::create_directories(dir);
    } catch (...) {
        Logger::Write(L"RemoteInit", L"backup failed: cannot create directory");
        return L"";
    }

    std::vector<uint8_t> bytes(json.begin(), json.end());
    if (!FileService::WriteBinaryFile(file, bytes)) {
        Logger::Write(L"RemoteInit", L"backup failed: cannot write file");
        return L"";
    }

    Logger::Write(L"RemoteInit", L"backup written: " + file.wstring());
    return file.wstring();
}

bool RemoteInitService::RestoreBackupElevated(const std::wstring& path) const {
    if (path.empty()) return false;
    if (ProcessRunner::IsElevated()) return RestoreBackup(path);

    AdminCommand::Result admin = AdminCommand::RestoreRemoteInit(path);
    for (const auto& line : admin.lines) {
        Logger::Write(L"RemoteInit", line);
    }
    return admin.allOk;
}

bool RemoteInitService::RestoreBackup(const std::wstring& path) const {
    if (path.empty()) return false;
    if (!ProcessRunner::IsElevated()) return false;

    std::vector<uint8_t> bytes;
    try {
        bytes = FileService::ReadBinaryFile(path);
    } catch (...) {
        Logger::Write(L"RemoteInit", L"restore failed: cannot read " + path);
        return false;
    }
    std::string json(bytes.begin(), bytes.end());

    long rdpDeny = 1;
    long nla = 0;
    long blankPass = 1;
    long winrmStart = 3;
    long winrmRunning = 0;
    long rdpFirewall = 1;
    long winrmFirewall = 1;
    if (!JsonGetInt(json, "rdpDeny", rdpDeny) ||
        !JsonGetInt(json, "nlaUserAuth", nla) ||
        !JsonGetInt(json, "limitBlankPass", blankPass) ||
        !JsonGetInt(json, "winrmStartType", winrmStart) ||
        !JsonGetInt(json, "winrmRunning", winrmRunning) ||
        !JsonGetInt(json, "rdpFirewall", rdpFirewall) ||
        !JsonGetInt(json, "winrmFirewall", winrmFirewall)) {
        Logger::Write(L"RemoteInit", L"restore failed: unreadable backup " + path);
        return false;
    }

    // Version-1 backups have no port / security-layer / rule-state keys;
    // keeping the live port and skipping the missing keys keeps them usable.
    long rdpPort = static_cast<long>(ReadDword(kRdpTcpKey, L"PortNumber", 3389));
    long rdpSecurityLayer = -1;
    long rdpFirewallEnabled = -1;
    long winrmFirewallEnabled = -1;
    JsonGetInt(json, "rdpPort", rdpPort);
    JsonGetInt(json, "rdpSecurityLayer", rdpSecurityLayer);
    JsonGetInt(json, "rdpFirewallEnabled", rdpFirewallEnabled);
    JsonGetInt(json, "winrmFirewallEnabled", winrmFirewallEnabled);

    std::vector<std::wstring> rdpUsers;
    JsonGetStringArray(json, "rdpUsers", rdpUsers);

    std::vector<std::wstring> logs;
    bool ok = true;

    auto regStep = [&](const wchar_t* key, const wchar_t* name, long value) {
        if (!RunStep(std::wstring(L"還原 ") + name + L"=" + std::to_wstring(value),
                     RegAddCommand(key, name, value), logs)) {
            ok = false;
        }
    };

    regStep(kRdpKey, L"fDenyTSConnections", rdpDeny);
    regStep(kRdpTcpKey, L"UserAuthentication", nla);
    regStep(kRdpTcpKey, L"PortNumber", rdpPort);
    if (rdpSecurityLayer >= 0) {
        regStep(kRdpTcpKey, L"SecurityLayer", rdpSecurityLayer);
    }
    regStep(kLsaKey, L"LimitBlankPasswordUse", blankPass);

    if (!RunStep(L"還原 WinRM 啟動類型",
                 L"sc config WinRM start= " + ServiceStartKeyword(winrmStart), logs)) {
        ok = false;
    }
    if (winrmRunning) {
        RunStep(L"啟動 WinRM 服務", L"sc start WinRM", logs);
    } else {
        RunStep(L"停止 WinRM 服務", L"sc stop WinRM", logs);
    }

    // Deleting a rule that was never created returns non-zero; that is not a
    // restore failure, so these steps only log.
    if (!rdpFirewall) {
        RunStep(L"移除防火牆規則 " + std::wstring(kRdpRuleName),
                L"netsh advfirewall firewall delete rule name=\"" +
                    std::wstring(kRdpRuleName) + L"\"",
                logs);
    } else {
        if (!EnsureFirewallRule(kRdpRuleName, std::to_wstring(rdpPort), logs)) {
            ok = false;
        }
        if (rdpFirewallEnabled == 0) {
            RunStep(L"停用防火牆規則 " + std::wstring(kRdpRuleName),
                    L"netsh advfirewall firewall set rule name=\"" +
                        std::wstring(kRdpRuleName) + L"\" new enable=No",
                    logs);
        }
    }
    if (!winrmFirewall) {
        RunStep(L"移除防火牆規則 " + std::wstring(kWinrmRuleName),
                L"netsh advfirewall firewall delete rule name=\"" +
                    std::wstring(kWinrmRuleName) + L"\"",
                logs);
    } else {
        if (!EnsureFirewallRule(kWinrmRuleName, L"5985", logs)) {
            ok = false;
        }
        if (winrmFirewallEnabled == 0) {
            RunStep(L"停用防火牆規則 " + std::wstring(kWinrmRuleName),
                    L"netsh advfirewall firewall set rule name=\"" +
                        std::wstring(kWinrmRuleName) + L"\" new enable=No",
                    logs);
        }
    }

    // Group members are only added back, never removed: the backup is used to
    // recover access, not to prune accounts that were added later.
    if (!rdpUsers.empty()) {
        std::wstring group = RdpGroupName();
        std::vector<std::wstring> members = QueryLocalGroupMembers(group);
        for (const auto& user : rdpUsers) {
            bool present = members.empty() ? LocalGroupHasUser(group, user)
                                           : GroupHasUser(members, user);
            if (present) {
                logs.push_back(L"[SKIP] " + user + L" 已在「" + group + L"」群組");
                continue;
            }
            if (!RunStep(L"還原群組成員 " + user,
                         L"net localgroup \"" + group + L"\" \"" + user +
                             L"\" /add",
                         logs)) {
                ok = false;
            }
        }
    }

    for (const auto& line : logs) {
        Logger::Write(L"RemoteInit", L"restore: " + line);
    }
    return ok;
}
