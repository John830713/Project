#include "RemoteInitService.h"

#include "../Core/Logger.h"
#include "FileService.h"
#include "ProcessRunner.h"

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
const wchar_t* const kWinrmKey = L"SYSTEM\\CurrentControlSet\\Services\\WinRM";

DWORD ReadDword(const wchar_t* key, const wchar_t* name, DWORD fallback) {
    DWORD value = fallback;
    DWORD size = sizeof(value);
    LONG rc = RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_DWORD,
                           nullptr, &value, &size);
    return rc == ERROR_SUCCESS ? value : fallback;
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

bool WinrmHttpListenerExists() {
    ProcessResult result = ProcessRunner::RunCmd(L"winrm enumerate winrm/config/listener");
    if (!result.started) return false;
    std::wstring lower = ToLower(result.output);
    return lower.find(L"http") != std::wstring::npos;
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

bool InRdpGroup(const std::wstring& group, const std::wstring& userName) {
    if (group.empty() || userName.empty()) return false;
    ProcessResult result = ProcessRunner::RunCmd(L"net localgroup \"" + group + L"\"");
    if (!result.started) return false;
    return ToLower(result.output).find(ToLower(userName)) != std::wstring::npos;
}

std::wstring QueryNetworkCategory() {
    bool comInitialized = SUCCEEDED(
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
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
    status.networkCategory = QueryNetworkCategory();

    status.rdpDeny = ReadDword(kRdpKey, L"fDenyTSConnections", 1);
    status.nlaUserAuth = ReadDword(kRdpTcpKey, L"UserAuthentication", 1);
    status.limitBlankPass = ReadDword(kLsaKey, L"LimitBlankPasswordUse", 1);

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
    status.winrmHttpListener = WinrmHttpListenerExists();
    status.rdpFirewallRule = FirewallRuleExists(kRdpRuleName);
    status.winrmFirewallRule = FirewallRuleExists(kWinrmRuleName);
    status.userInRdpGroup = InRdpGroup(RdpGroupName(), CurrentUserName());

    return status;
}

std::vector<RemoteInitItem> RemoteInitService::GetDefaultItems(
    const RemoteInitStatus& status) const {
    std::vector<RemoteInitItem> items;

    RemoteInitItem rdp;
    rdp.id = L"rdp";
    rdp.label = L"啟用遠端桌面 (RDP)";
    if (status.isWindowsHome) {
        rdp.desc = L"家用版不支援被遠端連線，此項目無法套用";
        rdp.selectable = false;
        rdp.checked = false;
    } else {
        rdp.desc = L"允許其他機台以 mstsc 連線";
        rdp.checked = status.rdpDeny != 0;
    }
    items.push_back(rdp);

    items.push_back({ L"nla", L"啟用網路層級驗證 (NLA)",
                      L"連線時必須先通過帳號密碼驗證", status.nlaUserAuth != 1, true });

    items.push_back({ L"rdp_firewall", L"開啟 RDP 防火牆 (TCP 3389)",
                      L"允許遠端桌面連線通過防火牆", !status.rdpFirewallRule, true });

    items.push_back({ L"winrm", L"啟用 WinRM 遠端管理",
                      L"啟動 WinRM 服務並建立 HTTP Listener",
                      !status.winrmServiceRunning || !status.winrmHttpListener, true });

    items.push_back({ L"winrm_firewall", L"開啟 WinRM 防火牆 (TCP 5985)",
                      L"允許 PowerShell 遠端連線通過防火牆",
                      !status.winrmFirewallRule, true });

    items.push_back({ L"rdp_users", L"加入遠端桌面使用者群組",
                      L"非系統管理員帳號連線 RDP 時需要",
                      !status.userInRdpGroup, true });

    // Security-relevant: never enabled unless the operator ticks it.
    items.push_back({ L"blankpass", L"允許空白密碼網路登入",
                      L"僅限隔離測試網路，預設不勾選", false, true });

    return items;
}

bool RemoteInitService::ApplyItem(const std::wstring& id,
                                  const std::wstring& userName,
                                  std::vector<std::wstring>& logs) const {
    if (id == L"rdp") {
        return RunStep(L"啟用遠端桌面 (fDenyTSConnections=0)",
                       L"reg add \"" + std::wstring(kRdpKey) +
                           L"\" /v fDenyTSConnections /t REG_DWORD /d 0 /f",
                       logs);
    }
    if (id == L"nla") {
        return RunStep(L"啟用 NLA (UserAuthentication=1)",
                       L"reg add \"" + std::wstring(kRdpTcpKey) +
                           L"\" /v UserAuthentication /t REG_DWORD /d 1 /f",
                       logs);
    }
    if (id == L"rdp_firewall") {
        return EnsureFirewallRule(kRdpRuleName, L"3389", logs);
    }
    if (id == L"winrm_firewall") {
        return EnsureFirewallRule(kWinrmRuleName, L"5985", logs);
    }
    if (id == L"blankpass") {
        logs.push_back(L"[WARN] 空白密碼登入會讓同網段任何帳號在無密碼時登入");
        return RunStep(L"允許空白密碼網路登入 (LimitBlankPasswordUse=0)",
                       L"reg add \"" + std::wstring(kLsaKey) +
                           L"\" /v LimitBlankPasswordUse /t REG_DWORD /d 0 /f",
                       logs);
    }
    if (id == L"rdp_users") {
        std::wstring user = userName.empty() ? CurrentUserName() : userName;
        std::wstring group = RdpGroupName();
        if (user.empty()) {
            logs.push_back(L"[FAIL] 無法取得目前使用者名稱");
            return false;
        }
        if (InRdpGroup(group, user)) {
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
        if (!WinrmHttpListenerExists()) {
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
    json += "  \"nlaUserAuth\": " + std::to_string(status.nlaUserAuth) + ",\n";
    json += "  \"limitBlankPass\": " + std::to_string(status.limitBlankPass) + ",\n";
    json += "  \"winrmStartType\": " +
            std::to_string(ReadDword(kWinrmKey, L"Start", 3)) + ",\n";
    json += "  \"winrmRunning\": " +
            std::to_string(status.winrmServiceRunning ? 1 : 0) + ",\n";
    json += "  \"rdpFirewall\": " +
            std::to_string(status.rdpFirewallRule ? 1 : 0) + ",\n";
    json += "  \"winrmFirewall\": " +
            std::to_string(status.winrmFirewallRule ? 1 : 0) + "\n";
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

    std::vector<std::wstring> logs;
    bool ok = true;

    auto regStep = [&](const wchar_t* key, const wchar_t* name, long value) {
        if (!RunStep(std::wstring(L"還原 ") + name + L"=" + std::to_wstring(value),
                     L"reg add \"" + std::wstring(key) + L"\" /v " + name +
                         L" /t REG_DWORD /d " + std::to_wstring(value) + L" /f",
                     logs)) {
            ok = false;
        }
    };

    regStep(kRdpKey, L"fDenyTSConnections", rdpDeny);
    regStep(kRdpTcpKey, L"UserAuthentication", nla);
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
    }
    if (!winrmFirewall) {
        RunStep(L"移除防火牆規則 " + std::wstring(kWinrmRuleName),
                L"netsh advfirewall firewall delete rule name=\"" +
                    std::wstring(kWinrmRuleName) + L"\"",
                logs);
    }

    for (const auto& line : logs) {
        Logger::Write(L"RemoteInit", L"restore: " + line);
    }
    return ok;
}
