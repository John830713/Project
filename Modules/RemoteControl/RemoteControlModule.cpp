#include "RemoteControlModule.h"
#include "RemoteControlHook.h"
#include "../../Core/ConfigManager.h"
#include "../../Core/Logger.h"
#include "../../Core/UpdateManager.h"
#include "../../Services/NetworkService.h"
#include "../../Services/FileTransferService.h"
#include "../../Services/TranslationService.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <shobjidl.h>
#include <mstcpip.h>
#include <cstdio>
#include <cwchar>
#include <cstdarg>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <vector>

namespace {

// A vanished peer must not pin a worker thread indefinitely. Without these the
// accepted socket blocks in send()/recv() until the OS gives up (Windows
// keepalive defaults to a 2 hour idle probe), holding m_svMx the whole time and
// deadlocking StopRemoteControl on the UI thread.
constexpr DWORD kIoTimeoutMs = 3000;
constexpr DWORD kKeepAliveIdleMs = 5000;
constexpr DWORD kKeepAliveIntervalMs = 1000;

// Marks a worker thread as alive for the diagnostics snapshot. RAII so every
// exit path (epoch mismatch, recv failure) clears the flag.
struct ThreadFlag {
    std::atomic<bool>& f;
    explicit ThreadFlag(std::atomic<bool>& x) : f(x) { f = true; }
    ~ThreadFlag() { f = false; }
};

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring r(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &r[0], n);
    return r;
}

std::wstring FileNamePart(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? path : path.substr(pos + 1);
}

std::wstring ModulePath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

bool FileSizeOf(const std::wstring& path, uint64_t* out) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        return false;
    *out = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) |
           fad.nFileSizeLow;
    return true;
}

std::string NowStamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char b[32];
    std::snprintf(b, sizeof(b), "%04u-%02u-%02u %02u:%02u:%02u", st.wYear,
                  st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return b;
}

// Last maxLines lines of a UTF-8 log file (shared read while Logger appends).
std::vector<std::string> ReadTailLines(const std::filesystem::path& p,
                                       int maxLines) {
    std::vector<std::string> out;
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE |
                               FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return out;

    LARGE_INTEGER sz{};
    if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0) {
        uint64_t take = static_cast<uint64_t>(sz.QuadPart);
        if (take > 64 * 1024) take = 64 * 1024;
        std::string buf(static_cast<size_t>(take), '\0');
        LARGE_INTEGER off{};
        off.QuadPart = sz.QuadPart - static_cast<LONGLONG>(take);
        DWORD got = 0;
        if (SetFilePointerEx(h, off, nullptr, FILE_BEGIN) &&
            ReadFile(h, &buf[0], static_cast<DWORD>(take), &got, nullptr)) {
            buf.resize(got);
            // Drop a partial first line when starting mid-file.
            size_t start = (take < static_cast<uint64_t>(sz.QuadPart))
                               ? buf.find('\n')
                               : 0;
            if (start == std::string::npos) start = 0;
            else ++start;
            std::string cur;
            for (size_t i = start; i < buf.size(); ++i) {
                if (buf[i] == '\n') {
                    if (!cur.empty() && cur.back() == '\r') cur.pop_back();
                    out.push_back(cur);
                    cur.clear();
                } else {
                    cur += buf[i];
                }
            }
            if (!cur.empty()) {
                if (cur.back() == '\r') cur.pop_back();
                out.push_back(cur);
            }
            if (static_cast<int>(out.size()) > maxLines)
                out.erase(out.begin(),
                          out.end() - static_cast<size_t>(maxLines));
        }
    }
    CloseHandle(h);
    return out;
}

}  // namespace

//==============================================================================
// Constructor / Configuration
//==============================================================================

RemoteControlModule::RemoteControlModule() {
    InitializeDefinitions();
    InitializeDefaults();
}

RemoteControlModule::~RemoteControlModule() {
    StopRemoteControl();
}

void RemoteControlModule::InitializeDefinitions() {
    m_definitions = {
        { L"ControllerEnabled", L"Controller Mode (Server)",      ConfigValueType::Bool,   L"1",        0, 1 },
        { L"ListenPort",        L"Server Port",                    ConfigValueType::Int,    L"5500",     1024, 65535 },
        { L"TargetPw",          L"Server Password",                ConfigValueType::String, L"changeme", 0, 0 },
        { L"TargetEnabled",     L"Target Mode (Client)",           ConfigValueType::Bool,   L"1",        0, 1 },
        { L"TargetIP",          L"Controller IP (to connect)",     ConfigValueType::String, L"0.0.0.0",  0, 0 },
        { L"TargetPort",        L"Controller Port (to connect)",   ConfigValueType::Int,    L"5500",     1024, 65535 },
        { L"ControllerPw",      L"Auth Password (to send)",        ConfigValueType::String, L"changeme", 0, 0 },
        { L"SavePath",          L"Save Path",                      ConfigValueType::Directory, L"D:\\Temp", 0, 0 },
        { L"AutoStart",         L"Auto Start",                     ConfigValueType::Bool,   L"0",        0, 1 },
        { L"WireTap",           L"Wire frame tap log (0/1)",        ConfigValueType::Bool,   L"0",        0, 1 },
    };
}

void RemoteControlModule::InitializeDefaults() {
    std::lock_guard<std::mutex> lk(m_valuesMx);
    for (const auto& def : m_definitions) {
        m_values[def.key] = def.defaultValue;
    }
}

const std::vector<ConfigFieldDefinition>& RemoteControlModule::GetConfigDefinitions() const {
    return m_definitions;
}

//==============================================================================
// Lifecycle
//==============================================================================

void RemoteControlModule::Initialize(HINSTANCE hInst, IHostContext* host) {
    m_hInst = hInst;
    m_host = host;
    m_startTick = GetTickCount64();
    NetworkService::Initialize();
}

void RemoteControlModule::LoadConfig() {
    auto loaded = ConfigManager::LoadModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        m_definitions
    );
    std::lock_guard<std::mutex> lk(m_valuesMx);
    m_values = std::move(loaded);
}

void RemoteControlModule::SaveConfig() {
    // Copy under the lock, then do file I/O on the copy — worker threads
    // must never wait on a disk write.
    std::map<std::wstring, std::wstring> snapshot;
    {
        std::lock_guard<std::mutex> lk(m_valuesMx);
        snapshot = m_values;
    }
    ConfigManager::SaveModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        snapshot
    );
}

void RemoteControlModule::ApplyConfig() {
    // AutoStart is an explicit user setting; ResumeState records whether
    // RemoteControl was left running when the previous session ended, so the
    // next launch (including the one after an in-place update restart)
    // resumes exactly where it left off.
    if (GetBoolValue(L"AutoStart", false) || ReadResumeState()) {
        StartRemoteControl();
    }
}

bool RemoteControlModule::ReadResumeState() const {
    wchar_t buf[8] = {};
    GetPrivateProfileStringW(GetConfigSectionName(), L"ResumeState", L"0", buf, 8,
                             ConfigManager::GetModuleConfigPath(GetConfigFileName()).c_str());
    return buf[0] == L'1';
}

void RemoteControlModule::SaveResumeState(bool active) const {
    WritePrivateProfileStringW(GetConfigSectionName(), L"ResumeState",
                               active ? L"1" : L"0",
                               ConfigManager::GetModuleConfigPath(GetConfigFileName()).c_str());
}

void RemoteControlModule::Shutdown() {
    StopRemoteControl();
    SaveConfig();
}

std::wstring RemoteControlModule::GetValue(const std::wstring& key) const {
    std::lock_guard<std::mutex> lk(m_valuesMx);
    auto it = m_values.find(key);
    if (it != m_values.end()) return it->second;
    return L"";
}

void RemoteControlModule::SetValue(const std::wstring& key, const std::wstring& value) {
    std::lock_guard<std::mutex> lk(m_valuesMx);
    m_values[key] = value;
}

//==============================================================================
// Private Helpers
//==============================================================================

bool RemoteControlModule::GetBoolValue(const std::wstring& key, bool defaultValue) const {
    std::lock_guard<std::mutex> lk(m_valuesMx);
    auto it = m_values.find(key);
    if (it == m_values.end()) return defaultValue;
    return it->second == L"1" || it->second == L"true" || it->second == L"TRUE";
}

std::wstring RemoteControlModule::GetStringValue(
    const std::wstring& key, const std::wstring& defaultVal) const
{
    std::lock_guard<std::mutex> lk(m_valuesMx);
    auto it = m_values.find(key);
    if (it == m_values.end()) return defaultVal;
    return it->second;
}

int RemoteControlModule::GetPortValue(const std::wstring& key, int defaultValue) const {
    // Config INI is user-editable, and this runs on worker threads where an
    // uncaught std::stoi exception would std::terminate the whole process.
    std::wstring v = GetValue(key);
    int port = defaultValue;
    if (!RemoteControl::TryParsePort(v, defaultValue, port)) {
        if (!v.empty())
            Log(L"[!] Invalid %ls=%ls, using %d\n", key.c_str(), v.c_str(),
                defaultValue);
        return defaultValue;
    }
    return port;
}

std::string RemoteControlModule::ToUtf8(const std::wstring& ws) const {
    if (ws.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1,
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string r(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1,
                        &r[0], n, nullptr, nullptr);
    return r;
}

void RemoteControlModule::Log(const wchar_t* fmt, ...) const {
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf, 1024, fmt, ap);
    va_end(ap);
    Logger::WriteModuleLog(GetLogSourceName(), buf);
}

//==============================================================================
// Hook Callbacks
//==============================================================================

void RemoteControlModule::OnHookEvent(const RemoteControl::Event* e, void* ud) {
    auto* self = static_cast<RemoteControlModule*>(ud);
    if (!self || !e) return;

    if (e->type == RemoteControl::EventType::MouseMove) {
        InterlockedExchangeAdd(&self->m_mouseDx, static_cast<LONG>(e->move.dx));
        InterlockedExchangeAdd(&self->m_mouseDy, static_cast<LONG>(e->move.dy));
        self->m_queueCv.notify_one();
        return;
    }

    std::vector<uint8_t> m;
    switch (e->type) {
    case RemoteControl::EventType::KeyDown:
        m = RemoteControl::Proto::key(RemoteControl::Proto::KEY_DOWN,
                                      e->key.vk, e->key.scan, e->key.ext);
        break;
    case RemoteControl::EventType::KeyUp:
        m = RemoteControl::Proto::key(RemoteControl::Proto::KEY_UP,
                                      e->key.vk, e->key.scan, e->key.ext);
        break;
    case RemoteControl::EventType::MouseDown:
        m = RemoteControl::Proto::mbtn(RemoteControl::Proto::MOUSE_DOWN, e->btn.button);
        break;
    case RemoteControl::EventType::MouseUp:
        m = RemoteControl::Proto::mbtn(RemoteControl::Proto::MOUSE_UP, e->btn.button);
        break;
    case RemoteControl::EventType::MouseScroll:
        m = RemoteControl::Proto::mscrl(e->scroll.delta);
        break;
    default:
        return;
    }
    self->EnqueueEvent(std::move(m));
}

void RemoteControlModule::OnHookToggle(int active, void* ud) {
    auto* self = static_cast<RemoteControlModule*>(ud);
    if (!self) return;

    if (active) {
        InterlockedExchange(&self->m_pendingToggleOff, 1);
        self->m_queueCv.notify_one();
    } else {
        self->EnqueueEvent(RemoteControl::Proto::simple(RemoteControl::Proto::RELEASE_ALL));
        self->EnqueueEvent(RemoteControl::Proto::simple(RemoteControl::Proto::CLIP_GET));
    }

    // Notify connected client of hook state change
    auto hs = RemoteControl::Proto::hookStatus(static_cast<uint8_t>(active ? 1 : 0));
    self->SendToPeer(INVALID_SOCKET, &self->m_svMx, hs);

    self->UpdateLed();
}

void RemoteControlModule::EnqueueEvent(std::vector<uint8_t>&& msg) {
    {
        std::lock_guard<std::mutex> lk(m_queueMx);
        if (m_eventQueue.size() < 10000)
            m_eventQueue.push(std::move(msg));
    }
    m_queueCv.notify_one();
}

//==============================================================================
// Server Threads (accept + send events + recv control)
//==============================================================================

void RemoteControlModule::RunAccept() {
    ThreadFlag alive(m_thrAccept);
    const unsigned epoch = m_epoch.load();
    SOCKET sv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sv == INVALID_SOCKET) {
        Log(L"[!] server socket() failed\n"); return;
    }

    int one = 1;
    setsockopt(sv, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<char*>(&one), sizeof(one));

    int port = GetPortValue(L"ListenPort", 5500);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        Log(L"[!] bind() on port %d failed\n", port); closesocket(sv); return;
    }
    if (listen(sv, 1) == SOCKET_ERROR) {
        Log(L"[!] listen() failed\n"); closesocket(sv); return;
    }

    Log(L"[*] Server listening on port %d\n", port);
    m_svListen.store(sv);

    while (m_epoch == epoch) {
        sockaddr_in peer{};
        int pl = sizeof(peer);
        SOCKET c = accept(sv, reinterpret_cast<sockaddr*>(&peer), &pl);
        if (c == INVALID_SOCKET) {
            // Stop closes m_svListen to unblock accept(); other errors are
            // transient. Never spin: wait briefly before trying again.
            if (m_epoch != epoch) break;
            Sleep(100);
            continue;
        }

        setsockopt(c, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<char*>(&one), sizeof(one));
        NetworkService::ApplyTimeouts(c, kIoTimeoutMs);
        NetworkService::ApplyKeepAlive(c, kKeepAliveIdleMs,
                                       kKeepAliveIntervalMs);

        std::vector<uint8_t> pay;
        if (!NetworkService::RecvMsg(c, pay) || pay.empty() || pay[0] != RemoteControl::Proto::AUTH) {
            closesocket(c); continue;
        }

        std::string pw(reinterpret_cast<char*>(pay.data() + 1), pay.size() - 1);
        std::string expectedPw = ToUtf8(GetValue(L"TargetPw"));
        if (pw != expectedPw) {
            Log(L"[-] Auth failed\n"); closesocket(c); continue;
        }

        Log(L"[+] Client authenticated\n");

        {
            std::lock_guard<std::mutex> lk(m_svMx);
            if (m_svSocket != INVALID_SOCKET) closesocket(m_svSocket);
            m_svSocket = c;
        }

        auto hs = RemoteControl::Proto::hookStatus(
            static_cast<uint8_t>(RemoteControlHook::IsActive() ? 1 : 0));
        SendToPeer(c, &m_svMx, hs);

        UpdateLed();
        {
            std::lock_guard<std::mutex> lk(m_ftRxMx);
            FileTransferService::ResetRecv(m_ftRx);
        }
    }

    // Only the side that wins the swap closes: StopRemoteControl exchanges
    // m_svListen to INVALID_SOCKET before closesocket(), and closing the
    // same handle twice could kill an unrelated socket that reused the value.
    SOCKET expected = sv;
    if (m_svListen.compare_exchange_strong(expected, INVALID_SOCKET))
        closesocket(sv);
}

void RemoteControlModule::RunSend() {
    ThreadFlag alive(m_thrSend);
    const unsigned epoch = m_epoch.load();
    while (m_epoch == epoch) {
        {
            std::unique_lock<std::mutex> lk(m_queueMx);
            m_queueCv.wait_for(lk, std::chrono::milliseconds(4));
        }

        std::vector<uint8_t> batch;

        if (InterlockedExchange(&m_pendingToggleOff, 0)) {
            RemoteControl::ClipboardPayload clip;
            RemoteControlHook::ClipReadAll(clip);
            auto m = RemoteControl::Proto::clip(
                RemoteControl::Proto::CLIP_SET, clip);
            batch.insert(batch.end(), m.begin(), m.end());
            Log(L"[*] Clipboard sent (text %d chars, image %d bytes)\n",
                static_cast<int>(clip.text.size()),
                static_cast<int>(clip.dib.size()));
        }

        LONG dx = InterlockedExchange(&m_mouseDx, 0);
        LONG dy = InterlockedExchange(&m_mouseDy, 0);
        if (dx || dy) {
            auto m = RemoteControl::Proto::mmove(dx, dy);
            batch.insert(batch.end(), m.begin(), m.end());
        }

        {
            std::lock_guard<std::mutex> lk(m_queueMx);
            while (!m_eventQueue.empty()) {
                auto& m = m_eventQueue.front();
                batch.insert(batch.end(), m.begin(), m.end());
                m_eventQueue.pop();
            }
        }

        bool lost = false;
        if (!batch.empty()) {
            std::lock_guard<std::mutex> lk(m_svMx);
            if (m_svSocket != INVALID_SOCKET) {
                if (!NetworkService::Send(m_svSocket, batch.data(),
                                          static_cast<int>(batch.size()))) {
                    closesocket(m_svSocket);
                    m_svSocket = INVALID_SOCKET;
                    lost = true;
                } else {
                    CountTx(batch);
                }
            }
        }

        // UpdateLed() locks m_svMx and m_clMx itself, so it must never run
        // while either is held here or this thread deadlocks against itself.
        if (lost) {
            UpdateLed();
            Log(L"[!] Send connection lost\n");
        }
    }
}

void RemoteControlModule::RunSvRecv() {
    ThreadFlag alive(m_thrSvRecv);
    const unsigned epoch = m_epoch.load();
    SOCKET lastC = INVALID_SOCKET;
    while (m_epoch == epoch) {
        SOCKET c;
        {
            std::lock_guard<std::mutex> lk(m_svMx);
            c = m_svSocket;
        }

        if (c != lastC) {
            std::lock_guard<std::mutex> lk(m_ftRxMx);
            FileTransferService::ResetRecv(m_ftRx);
            lastC = c;
        }
        if (c == INVALID_SOCKET) { Sleep(500); continue; }

        fd_set fs; FD_ZERO(&fs); FD_SET(c, &fs);
        timeval tv = {0, 500000};
        if (select(0, &fs, nullptr, nullptr, &tv) <= 0) {
            CheckDiagTimeout();
            continue;
        }

        std::vector<uint8_t> pay;
        if (!NetworkService::RecvMsg(c, pay)) {
            // Peer is gone (or the idle connection just died). Tear the socket
            // down instead of spinning on it: leaving m_svSocket set keeps the
            // LED green and makes every later Send fail on a dead handle.
            // Never leave the loop here — this thread must survive so that a
            // client which reconnects (RunAccept accepts a new socket) still
            // has a receiver. Falling through with an INVALID_SOCKET socket
            // takes the Sleep(500) branch above until a new one shows up.
            bool owned = false;
            {
                std::lock_guard<std::mutex> lk(m_svMx);
                if (m_svSocket == c) {
                    m_svSocket = INVALID_SOCKET;
                    owned = true;
                }
            }
            if (owned) {
                closesocket(c);
                UpdateLed();
                Log(L"[!] Receive connection lost\n");
            }
            continue;
        }

        m_rxPkt.fetch_add(1);
        m_rxBytes.fetch_add(pay.size() + 4);
        m_lastRxTick.store(GetTickCount64());
        if (!pay.empty())
            WireTap(false, pay[0], static_cast<uint32_t>(pay.size()));

        std::wstring savePath = GetValue(L"SavePath");
        std::string savePathA = ToUtf8(savePath);
        wchar_t savePathW[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, savePathA.c_str(), -1,
                            savePathW, MAX_PATH);

        {
            // ProcessMsg writes received file data through m_ftRx; the same
            // state is reset concurrently by RunAccept and StopRemoteControl.
            std::lock_guard<std::mutex> lk(m_ftRxMx);
            if (FileTransferService::ProcessMsg(
                    pay.data(), static_cast<uint32_t>(pay.size()),
                    m_ftRx, savePathW))
                continue;
        }

        if (pay.size() >= 2 && pay[0] == RemoteControl::Proto::CLIP_RESP) {
            RemoteControl::ClipboardPayload clip;
            int textChars = 0, dibBytes = 0;
            if (RemoteControl::Proto::unclip(pay, clip, &textChars, &dibBytes)) {
                RemoteControlHook::ClipWriteAll(clip);
                Log(L"[*] Clipboard synced (text %d chars, image %d bytes)\n",
                    textChars, dibBytes);
            }
        } else if (pay.size() >= 3 && pay[0] == RemoteControl::Proto::DBG_REQ) {
            {
                std::lock_guard<std::mutex> lk(m_clMx);
                if (m_clSocket != INVALID_SOCKET) {
                    (void)ForwardToOther(&m_svMx, pay);
                    continue;
                }
            }
            HandleDiagRequest(RemoteControl::Proto::r16(pay.data() + 1),
                              "server", c, &m_svMx);
        } else if (pay.size() >= 1 && pay[0] == RemoteControl::Proto::DBG_RESP) {
            HandleDiagResponse(pay.data(), static_cast<uint32_t>(pay.size()));
        } else if (pay.size() >= 1 && pay[0] == RemoteControl::Proto::EXE_RESULT) {
            HandleExeResult(pay.data(), static_cast<uint32_t>(pay.size()));
        } else if (pay.size() >= 1 && pay[0] == RemoteControl::Proto::EXE_SWAP) {
            {
                std::lock_guard<std::mutex> lk(m_clMx);
                if (m_clSocket != INVALID_SOCKET) {
                    (void)ForwardToOther(&m_svMx, pay);
                    continue;
                }
            }
            HandleExeSwap(pay.data(), static_cast<uint32_t>(pay.size()), c,
                          &m_svMx, "server");
        }
    }
}

//==============================================================================
// Client Thread (connect + receive + replay)
//==============================================================================

void RemoteControlModule::RunClient() {
    ThreadFlag alive(m_thrClient);
    const unsigned epoch = m_epoch.load();
    std::wstring wIP = GetValue(L"TargetIP");
    int port = GetPortValue(L"TargetPort", 5500);
    std::string ip = ToUtf8(wIP);
    std::string password = ToUtf8(GetValue(L"ControllerPw"));

    std::wstring savePath = GetValue(L"SavePath");
    std::string savePathA = ToUtf8(savePath);
    wchar_t savePathW[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, savePathA.c_str(), -1, savePathW, MAX_PATH);

    // Stop must take effect during the 3s retry backoff too, so sleep in
    // small slices and abandon as soon as our generation is superseded.
    auto retryWait = [this, epoch] {
        for (int i = 0; i < 30; ++i) {
            if (m_epoch != epoch) return;
            Sleep(100);
        }
    };

    while (m_epoch == epoch) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) {
            Log(L"[!] socket() failed\n");
            retryWait(); continue;
        }

        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
            Log(L"[!] Invalid target IP: %ls\n", wIP.c_str());
            closesocket(s);
            retryWait(); continue;
        }

        connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

        fd_set fs; FD_ZERO(&fs); FD_SET(s, &fs);
        timeval tv = {10, 0};
        if (select(0, nullptr, &fs, nullptr, &tv) <= 0) {
            closesocket(s);
            retryWait(); continue;
        }

        // A failed non-blocking connect() also marks the socket writable, so
        // select() alone reports success. Read SO_ERROR to tell refused/
        // unreachable hosts apart from a real connection.
        int soErr = 0;
        int soLen = sizeof(soErr);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR,
                       reinterpret_cast<char*>(&soErr), &soLen) != 0 ||
            soErr != 0)
        {
            Log(L"[!] connect to %ls:%d failed (err %d)\n", wIP.c_str(), port,
                soErr);
            closesocket(s);
            retryWait(); continue;
        }

        nb = 0;
        ioctlsocket(s, FIONBIO, &nb);

        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<char*>(&one), sizeof(one));
        NetworkService::ApplyTimeouts(s, kIoTimeoutMs);
        NetworkService::ApplyKeepAlive(s, kKeepAliveIdleMs,
                                       kKeepAliveIntervalMs);

        auto am = RemoteControl::Proto::auth(
            password.c_str(), static_cast<int>(password.size()));
        if (!NetworkService::Send(s, am.data(), static_cast<int>(am.size()))) {
            closesocket(s);
            retryWait(); continue;
        }

        {
            std::lock_guard<std::mutex> lk(m_clMx);
            m_clSocket = s;
        }

        Log(L"[+] Connected to server %ls:%d\n", wIP.c_str(), port);
        InterlockedExchange(&m_targetHookActive, 0);
        UpdateLed();

        FileTransferService::ReceiveState clFtRx{};

        bool ok = true;
        while (ok && m_epoch == epoch) {
            fd_set rs; FD_ZERO(&rs); FD_SET(s, &rs);
            timeval rtv = {0, 200000};
            int sr = select(0, &rs, nullptr, nullptr, &rtv);

            if (m_epoch != epoch) { ok = false; break; }
            if (sr < 0) { ok = false; break; }
            if (sr == 0) { CheckDiagTimeout(); continue; }

            // Each Replay* function handles its own s_replaying guard, so we
            // drain all available messages immediately with no settle delay.
            bool ok2 = true;
            do {
                std::vector<uint8_t> pay;
                if (!NetworkService::RecvMsg(s, pay)) { ok2 = false; break; }

                auto* p = pay.data();
                auto n = static_cast<uint32_t>(pay.size());

                m_rxPkt.fetch_add(1);
                m_rxBytes.fetch_add(pay.size() + 4);
                m_lastRxTick.store(GetTickCount64());
                if (!pay.empty())
                    WireTap(false, pay[0], static_cast<uint32_t>(pay.size()));

                if (FileTransferService::ProcessMsg(p, n, clFtRx, savePathW))
                    continue;

                if (n < 1) continue;

                switch (p[0]) {
                case RemoteControl::Proto::KEY_DOWN:
                    if (n >= 6)
                        RemoteControlHook::ReplayKey(
                            RemoteControl::Proto::r16(p + 1),
                            RemoteControl::Proto::r16(p + 3), p[5], 0);
                    break;
                case RemoteControl::Proto::KEY_UP:
                    if (n >= 6)
                        RemoteControlHook::ReplayKey(
                            RemoteControl::Proto::r16(p + 1),
                            RemoteControl::Proto::r16(p + 3), p[5], 1);
                    break;
                case RemoteControl::Proto::MOUSE_MOVE:
                    if (n >= 9) {
                        RemoteControlHook::ReplayMouseMove(
                            RemoteControl::Proto::ri32(p + 1),
                            RemoteControl::Proto::ri32(p + 5));
                    }
                    break;
                case RemoteControl::Proto::MOUSE_DOWN:
                    if (n >= 2) RemoteControlHook::ReplayMouseBtn(p[1], 1);
                    break;
                case RemoteControl::Proto::MOUSE_UP:
                    if (n >= 2) RemoteControlHook::ReplayMouseBtn(p[1], 0);
                    break;
                case RemoteControl::Proto::MOUSE_SCROLL:
                    if (n >= 5)
                        RemoteControlHook::ReplayMouseScroll(
                            RemoteControl::Proto::ri32(p + 1));
                    break;
                case RemoteControl::Proto::RELEASE_ALL:
                    RemoteControlHook::ReplayReleaseAll();
                    break;
                case RemoteControl::Proto::CLIP_SET: {
                    RemoteControl::ClipboardPayload clip;
                    int textChars = 0, dibBytes = 0;
                    if (RemoteControl::Proto::unclip(pay, clip, &textChars, &dibBytes)) {
                        RemoteControlHook::ClipWriteAll(clip);
                        Log(L"[*] Clipboard received (text %d chars, image %d bytes)\n",
                            textChars, dibBytes);
                    }
                    break;
                }
                case RemoteControl::Proto::CLIP_GET: {
                    RemoteControl::ClipboardPayload clip;
                    RemoteControlHook::ClipReadAll(clip);
                    auto reply = RemoteControl::Proto::clip(
                        RemoteControl::Proto::CLIP_RESP, clip);
                    SendToPeer(s, &m_clMx, reply);
                    break;
                }
                case RemoteControl::Proto::HOOK_STATUS:
                    if (n >= 2) {
                        InterlockedExchange(&m_targetHookActive, p[1] ? 1 : 0);
                        UpdateLed();
                    }
                    break;
                case RemoteControl::Proto::DBG_REQ:
                    if (n >= 3)
                        HandleDiagRequest(RemoteControl::Proto::r16(p + 1),
                                          "client", s, &m_clMx);
                    break;
                case RemoteControl::Proto::DBG_RESP:
                    {
                        (void)ForwardToOther(&m_clMx, pay);
                    }
                    HandleDiagResponse(p, n);
                    break;
                case RemoteControl::Proto::EXE_RESULT:
                    {
                        (void)ForwardToOther(&m_clMx, pay);
                    }
                    HandleExeResult(p, n);
                    break;
                case RemoteControl::Proto::EXE_SWAP:
                    HandleExeSwap(p, n, s, &m_clMx, "client");
                    break;
                }

                fd_set more; FD_ZERO(&more); FD_SET(s, &more);
                timeval zero = {0, 0};
                if (select(0, &more, nullptr, nullptr, &zero) <= 0)
                    break;
            } while (true);

            if (!ok2) { ok = false; break; }
        }

        {
            std::lock_guard<std::mutex> lk(m_clMx);
            m_clSocket = INVALID_SOCKET;
        }

        closesocket(s);

        UpdateLed();
        if (m_epoch == epoch) {
            Log(L"[!] Disconnected from %ls:%d, retrying in 3s\n", wIP.c_str(), port);
            retryWait();
        }
    }
}

//==============================================================================
// LED Overlay Windows
//==============================================================================

LRESULT CALLBACK RemoteControlModule::LedWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        COLORREF color = static_cast<COLORREF>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (!color) color = RGB(160, 160, 160);
        HBRUSH hb = CreateSolidBrush(color);
        HPEN hp = CreatePen(PS_NULL, 0, 0);
        auto oldB = SelectObject(dc, hb);
        auto oldP = SelectObject(dc, hp);
        Ellipse(dc, 1, 1, 19, 19);
        SelectObject(dc, oldB);
        SelectObject(dc, oldP);
        DeleteObject(hb);
        DeleteObject(hp);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(h, m, w, l);
}

void RemoteControlModule::CreateLeds() {
    {
        std::lock_guard<std::mutex> lk(m_ledMx);
        if (m_led) return;
    }

    const wchar_t kClass[] = L"RCLedClass";
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = LedWndProc;
        wc.hInstance = m_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        RegisterClassW(&wc);
        reg = true;
    }

    {
        std::lock_guard<std::mutex> lk(m_ledMx);
        m_led = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
            kClass, L"RC Status", WS_POPUP,
            0, 0, 20, 20, nullptr, nullptr, m_hInst, nullptr);

        if (m_led) {
            SetLayeredWindowAttributes(m_led, 0, 200, LWA_ALPHA);
            SetWindowLongPtrW(m_led, GWLP_USERDATA, static_cast<LONG_PTR>(RGB(160, 160, 160)));
            ShowWindow(m_led, SW_SHOWNOACTIVATE);
        }
    }

    UpdateLedPositions();

    // Joinable, not detached: StopRemoteControl bumps the epoch first, so
    // DestroyLeds() can join here instead of leaving a thread racing the
    // window's destruction. CreateLeds only runs after an epoch bump, which
    // means any previous LED thread has already been joined by DestroyLeds.
    if (m_ledThread.joinable()) m_ledThread.join();
    m_ledThread = std::thread(&RemoteControlModule::RunLeds, this);
}

void RemoteControlModule::DestroyLeds() {
    // Join OUTSIDE the lock: RunLeds takes m_ledMx in UpdateLedPositions, so
    // holding it across the join would deadlock against the worker.
    if (m_ledThread.joinable()) m_ledThread.join();
    std::lock_guard<std::mutex> lk(m_ledMx);
    if (m_led) { DestroyWindow(m_led); m_led = nullptr; }
}

void RemoteControlModule::SetLedColor(COLORREF color) {
    std::lock_guard<std::mutex> lk(m_ledMx);
    if (!m_led) return;
    SetWindowLongPtrW(m_led, GWLP_USERDATA, static_cast<LONG_PTR>(color));
    InvalidateRect(m_led, nullptr, TRUE);
}

void RemoteControlModule::UpdateLed() {
    // Gray  = 中斷（無任何連線）
    // Green = 主控權在此（Controller idle，或 Target 正在接收回放）
    // Yellow = 主控權在對方（Controller 正送出輸入，或 Target idle）
    bool svConnected = false;
    {
        std::lock_guard<std::mutex> lk(m_svMx);
        svConnected = (m_svSocket != INVALID_SOCKET);
    }
    bool clConnected = false;
    {
        std::lock_guard<std::mutex> lk(m_clMx);
        clConnected = (m_clSocket != INVALID_SOCKET);
    }

    if (!svConnected && !clConnected) {
        SetLedColor(RGB(160, 160, 160));
        return;
    }

    bool localHook = RemoteControlHook::IsActive();
    bool remoteHook = (InterlockedExchangeAdd(&m_targetHookActive, 0) != 0);

    if (svConnected && localHook) {
        SetLedColor(RGB(210, 180, 0));  // Controller 啟動 Hook → 黃色
    } else if (clConnected && remoteHook) {
        SetLedColor(RGB(0, 210, 0));    // Target 收到回放 → 綠色
    } else if (svConnected) {
        SetLedColor(RGB(0, 210, 0));    // Controller idle → 綠色
    } else {
        SetLedColor(RGB(210, 180, 0));  // Target idle → 黃色
    }
}

void RemoteControlModule::UpdateLedPositions() {
    std::lock_guard<std::mutex> lk(m_ledMx);
    if (!m_led) return;
    HWND petHwnd = m_host->GetMainWindow();
    if (!petHwnd) return;

    RECT rc;
    GetWindowRect(petHwnd, &rc);

    int ledSize = 20;
    int cx = (rc.left + rc.right) / 2;
    int x = cx - ledSize / 2;
    int y = rc.bottom + 4;

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    if (x + ledSize > sw) x = sw - ledSize - 2;
    if (x < 0) x = 2;
    if (y + ledSize > sh) {
        y = rc.top - ledSize - 4;
        if (y < 0) y = 2;
    }
    if (y < 2) y = 2;

    // SWP_ASYNCWINDOWPOS: this runs on the LED mover thread, but m_led is
    // owned by the UI thread (CreateLeds runs there). A plain SetWindowPos
    // would block until the owner pumps its queue, so if StopRemoteControl
    // (UI thread) is inside DestroyLeds()->join() the two threads deadlock.
    // The async flag posts the request instead, letting the worker return to
    // RunLeds' epoch check and exit so the join can complete.
    SetWindowPos(m_led, HWND_TOPMOST, x, y, ledSize, ledSize,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_ASYNCWINDOWPOS);
}

void RemoteControlModule::RunLeds() {
    ThreadFlag alive(m_thrLed);
    const unsigned epoch = m_epoch.load();
    while (m_epoch == epoch) {
        Sleep(200);
        if (m_epoch != epoch) break;
        UpdateLedPositions();
    }
}

//==============================================================================
// LED query / visibility (used by Pet snap indicator)
//==============================================================================

COLORREF RemoteControlModule::GetLedColor() const {
    std::lock_guard<std::mutex> lk(m_ledMx);
    if (!m_led) return 0;
    return static_cast<COLORREF>(GetWindowLongPtrW(m_led, GWLP_USERDATA));
}

void RemoteControlModule::ShowLed(bool visible) {
    std::lock_guard<std::mutex> lk(m_ledMx);
    if (m_led) ShowWindow(m_led, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
}

//==============================================================================
// Remote Control Start / Stop
//==============================================================================

void RemoteControlModule::StartRemoteControl() {
    if (m_running.exchange(true)) return;

    // New generation: threads spawned below capture this value, and any
    // thread still winding down from a previous cycle sees a mismatch and
    // exits instead of coming back to life.
    m_epoch.fetch_add(1);

    // Use the Pet HWND for taskbar progress
    HWND petHwnd = m_host->GetMainWindow();
    FileTransferService::SetProgressHwnd(petHwnd);

    // Create LED overlays near the pet window
    CreateLeds();

    // Start file transfer senders
    FileTransferService::StartSvSender(&m_svSocket, &m_svMx);
    FileTransferService::StartClSender(&m_clSocket, &m_clMx);

    if (!RemoteControlHook::Start(OnHookEvent, OnHookToggle, this)) {
        Log(L"[!] Hook start failed\n");
        m_epoch.fetch_add(1);  // retire the LED thread started above
        DestroyLeds();
        m_running = false;
        return;
    }

    bool ctrlEnabled = GetBoolValue(L"ControllerEnabled", true);
    std::wstring targetIP = GetValue(L"TargetIP");
    bool targetConnect = GetBoolValue(L"TargetEnabled", true) && targetIP != L"0.0.0.0" && !targetIP.empty();

    // Controller threads
    if (ctrlEnabled) {
        m_acceptThread = std::thread(&RemoteControlModule::RunAccept, this);
        m_sendThread = std::thread(&RemoteControlModule::RunSend, this);
        m_svRecvThread = std::thread(&RemoteControlModule::RunSvRecv, this);
        m_acceptThread.detach();
        m_sendThread.detach();
        m_svRecvThread.detach();
        Log(L"[*] Controller threads started\n");
    }

    // Target thread
    if (targetConnect) {
        m_clientThread = std::thread(&RemoteControlModule::RunClient, this);
        m_clientThread.detach();
        Log(L"[*] Target thread started\n");
    }

    Log(L"[*] Remote control started (all threads)\n");
    m_running = true;
}

void RemoteControlModule::StopRemoteControl() {
    if (!m_running.exchange(false)) return;

    Log(L"[*] Stopping remote control\n");

    // Supersede the current generation: every worker loop (and the 3s retry
    // backoff in RunClient) compares against m_epoch and exits on mismatch.
    m_epoch.fetch_add(1);

    // Unblock RunAccept's accept() — it owns the listening socket, so the
    // only way to wake it is to close it from here.
    SOCKET ls = m_svListen.exchange(INVALID_SOCKET);
    if (ls != INVALID_SOCKET) closesocket(ls);

    // Close both connections to wake up threads
    {
        std::lock_guard<std::mutex> lk(m_svMx);
        if (m_svSocket != INVALID_SOCKET) {
            closesocket(m_svSocket);
            m_svSocket = INVALID_SOCKET;
        }
    }
    {
        std::lock_guard<std::mutex> lk(m_clMx);
        if (m_clSocket != INVALID_SOCKET) {
            closesocket(m_clSocket);
            m_clSocket = INVALID_SOCKET;
        }
    }
    m_queueCv.notify_one();

    RemoteControlHook::Stop();

    Sleep(50);

    DestroyLeds();

    FileTransferService::SetProgressHwnd(nullptr);
    {
        std::lock_guard<std::mutex> lk(m_ftRxMx);
        FileTransferService::ResetRecv(m_ftRx);
    }
}

//==============================================================================
// Drop Actions
//==============================================================================

bool RemoteControlModule::CanHandleDrop(const DropContext& ctx) const {
    return m_running && !ctx.filePaths.empty();
}

std::vector<DropActionDefinition> RemoteControlModule::GetDropActions(
    const DropContext& ctx) const
{
    if (!CanHandleDrop(ctx)) return {};
    return { { kActionStart, TranslationService::Get()->Tr(L"RemoteControl", L"Send via Remote Control") } };
}

void RemoteControlModule::ExecuteDropAction(int actionId, const DropContext& ctx) {
    if (actionId != kActionStart) return;
    for (const auto& path : ctx.filePaths) {
        FileTransferService::QueueSendAll(path.c_str());
    }
}

//==============================================================================
// Remote Diagnostics / Deploy Backdoor (spec section 10)
//
// Four message types handled inside the existing recv loops, so the channel
// works from either role regardless of F9 control state. Reports land in
// Log/RemoteDiagnostics.txt on both sides (requester: request+RTT+body,
// responder: response body) so the agent only needs one side's workspace.
//==============================================================================

bool RemoteControlModule::SendToPeer(SOCKET s, std::mutex* mx,
                                     const std::vector<uint8_t>& msg) {
    std::lock_guard<std::mutex> lk(*mx);
    SOCKET cur = (mx == &m_svMx) ? m_svSocket : m_clSocket;
    if (cur == INVALID_SOCKET) return false;
    // s == INVALID_SOCKET means "whoever is current" (event-driven sends);
    // a real handle must still match, so a reply is never fired at a
    // recycled fd from a connection that already died.
    if (s != INVALID_SOCKET && s != cur) return false;
    if (!NetworkService::Send(cur, msg.data(),
                              static_cast<int>(msg.size())))
        return false;
    m_txPkt.fetch_add(1);
    m_txBytes.fetch_add(msg.size() + 4);
    m_lastTxTick.store(GetTickCount64());
    if (msg.size() >= 5) WireTap(true, msg[4], static_cast<uint32_t>(msg.size() - 4));
    return true;
}
bool RemoteControlModule::ForwardToOther(void* fromMx, const std::vector<uint8_t>& frame) {
    if (frame.empty()) return false;
    if (fromMx == &m_svMx) {
        std::lock_guard<std::mutex> lk(m_clMx);
        if (m_clSocket != INVALID_SOCKET) {
            if (NetworkService::Send(m_clSocket, frame.data(),
                                     static_cast<int>(frame.size()))) {
                m_txPkt.fetch_add(1);
                m_txBytes.fetch_add(frame.size() + 4);
                m_lastTxTick.store(GetTickCount64());
                if (frame.size() >= 5)
                    WireTap(true, frame[4],
                            static_cast<uint32_t>(frame.size() - 4));
                return true;
            }
        }
        return false;
    }
    if (fromMx == &m_clMx) {
        std::lock_guard<std::mutex> lk(m_svMx);
        if (m_svSocket != INVALID_SOCKET) {
            if (NetworkService::Send(m_svSocket, frame.data(),
                                     static_cast<int>(frame.size()))) {
                m_txPkt.fetch_add(1);
                m_txBytes.fetch_add(frame.size() + 4);
                m_lastTxTick.store(GetTickCount64());
                if (frame.size() >= 5)
                    WireTap(true, frame[4],
                            static_cast<uint32_t>(frame.size() - 4));
                return true;
            }
        }
        return false;
    }
    return false;
}


// Walks a batch of length-prefixed frames (the RunSend output buffer) so the
// counters mirror what actually hit the wire, not just bytes handed to send().
void RemoteControlModule::CountTx(const std::vector<uint8_t>& framed) {
    size_t i = 0;
    uint64_t npkt = 0;
    while (i + 4 <= framed.size()) {
        uint32_t len = (static_cast<uint32_t>(framed[i]) << 24) |
                       (static_cast<uint32_t>(framed[i + 1]) << 16) |
                       (static_cast<uint32_t>(framed[i + 2]) << 8) |
                       static_cast<uint32_t>(framed[i + 3]);
        if (len == 0 || i + 4 + len > framed.size()) break;
        if (len >= 1) WireTap(true, framed[i + 4], len);
        ++npkt;
        i += 4 + len;
    }
    m_txPkt.fetch_add(npkt);
    m_txBytes.fetch_add(framed.size());
    m_lastTxTick.store(GetTickCount64());
}

void RemoteControlModule::WireTap(bool outbound, uint8_t type, uint32_t len) {
    if (!GetBoolValue(L"WireTap", false)) return;
    Log(L"[wire] %hs type=%u len=%u\n", outbound ? "TX" : "RX",
        static_cast<unsigned>(type), static_cast<unsigned>(len));
}

void RemoteControlModule::RequestDiagnostics() {
    SOCKET s = INVALID_SOCKET;
    std::mutex* mx = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_svMx);
        if (m_svSocket != INVALID_SOCKET) { s = m_svSocket; mx = &m_svMx; }
    }
    if (s == INVALID_SOCKET) {
        std::lock_guard<std::mutex> lk(m_clMx);
        if (m_clSocket != INVALID_SOCKET) { s = m_clSocket; mx = &m_clMx; }
    }
    if (s == INVALID_SOCKET) {
        Log(L"[!] diag: no peer connection\n");
        UpdateManager::ShowNoticeText(
            TranslationService::Get()->Tr(L"RemoteControl",
                                          L"No active peer connection."),
            TranslationService::Get()->Tr(L"RemoteControl", L"Diagnostics"));
        return;
    }

    uint16_t seq = m_diagSeq.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(m_diagMx);
        if (m_diagPending.size() >= 8) m_diagPending.erase(m_diagPending.begin());
        m_diagPending.push_back({seq, GetTickCount64()});
    }

    auto req = RemoteControl::Proto::dbgReq(seq);
    bool sent = SendToPeer(s, mx, req);
    Log(L"[*] diag: request seq=%u %s\n", static_cast<unsigned>(seq),
        sent ? L"sent" : L"send failed");
    if (!sent) {
        std::lock_guard<std::mutex> lk(m_diagMx);
        m_diagPending.erase(std::remove_if(m_diagPending.begin(),
                                           m_diagPending.end(),
                                           [&](const PendingDiag& d) {
                                               return d.seq == seq;
                                           }),
                            m_diagPending.end());
    }
}

void RemoteControlModule::DeployMyBuild() {
    SOCKET s = INVALID_SOCKET;
    std::mutex* mx = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_svMx);
        if (m_svSocket != INVALID_SOCKET) { s = m_svSocket; mx = &m_svMx; }
    }
    if (s == INVALID_SOCKET) {
        std::lock_guard<std::mutex> lk(m_clMx);
        if (m_clSocket != INVALID_SOCKET) { s = m_clSocket; mx = &m_clMx; }
    }
    if (s == INVALID_SOCKET) {
        Log(L"[!] deploy: no peer connection\n");
        UpdateManager::ShowNoticeText(
            TranslationService::Get()->Tr(L"RemoteControl",
                                          L"No active peer connection."),
            TranslationService::Get()->Tr(L"RemoteControl", L"Deploy"));
        return;
    }

    std::wstring own = ModulePath();
    uint64_t size = 0;
    if (!FileSizeOf(own, &size) || size == 0) {
        Log(L"[!] deploy: cannot stat own executable\n");
        return;
    }
    std::string name = ToUtf8(FileNamePart(own));

    // Announce BEFORE queueing the file: the receiver snapshots the target's
    // old mtime when the EXE_SWAP lands, so its size poll can tell the stale
    // leftover from the incoming file even when the sizes are identical.
    auto msg = RemoteControl::Proto::exeSwap(size, name.data(), name.size());
    bool sent = SendToPeer(s, mx, msg);
    if (sent) {
        // FT pushes through its own sender thread; the receiver polls until
        // the file lands completely.
        FileTransferService::QueueSendAll(own.c_str());
    }

    Log(L"[*] deploy: %hs (%llu bytes) %s\n", name.c_str(),
        static_cast<unsigned long long>(size), sent ? L"announced" : L"failed");
    AppendDiagReport("DEPLOY REQUEST name=" + name +
                         " size=" + std::to_string(size),
                     sent ? "EXE_SWAP announced to peer"
                          : "EXE_SWAP send failed (no peer?)");
}

void RemoteControlModule::HandleDiagRequest(uint16_t seq, const char* role,
                                            SOCKET s, std::mutex* mx) {
    Log(L"[*] diag: request seq=%u from %hs\n", static_cast<unsigned>(seq),
        role);
    RemoteControl::Diag::SnapshotData snap = CollectSnapshot(role);
    std::string text = RemoteControl::Diag::BuildSnapshotText(snap);
    auto resp = RemoteControl::Proto::dbgResp(seq, text.data(), text.size());
    bool sent = SendToPeer(s, mx, resp);
    Log(L"[*] diag: response seq=%u %s (%llu bytes)\n",
        static_cast<unsigned>(seq), sent ? L"sent" : L"send failed",
        static_cast<unsigned long long>(text.size()));
    AppendDiagReport(std::string("RESPONSE role=") + role +
                         " seq=" + std::to_string(seq),
                     text);
}

void RemoteControlModule::HandleDiagResponse(const uint8_t* p, uint32_t n) {
    if (n < 3) return;
    uint16_t seq = RemoteControl::Proto::r16(p + 1);
    std::string body(reinterpret_cast<const char*>(p + 3), n - 3);

    long long rtt = -1;
    {
        std::lock_guard<std::mutex> lk(m_diagMx);
        uint64_t now = GetTickCount64();
        for (auto it = m_diagPending.begin(); it != m_diagPending.end(); ++it) {
            if (it->seq == seq) {
                rtt = static_cast<long long>(now - it->tick);
                m_diagPending.erase(it);
                break;
            }
        }
    }

    std::string header = "REQUEST seq=" + std::to_string(seq);
    if (rtt >= 0) header += " rtt=" + std::to_string(rtt) + "ms";
    else header += " rtt=n/a";
    AppendDiagReport(header, body);
    Log(L"[*] diag: response seq=%u received, report written\n",
        static_cast<unsigned>(seq));
}

void RemoteControlModule::HandleExeResult(const uint8_t* p, uint32_t n) {
    if (n < 2) return;
    bool ok = p[1] != 0;
    std::string msg(reinterpret_cast<const char*>(p + 2),
                    n > 2 ? n - 2 : 0);
    Log(L"[*] deploy: result ok=%d msg=%hs\n", ok ? 1 : 0, msg.c_str());
    AppendDiagReport(std::string("DEPLOY RESULT ok=") + (ok ? "1" : "0"),
                     msg.empty() ? "(no message)" : msg);

    auto tr = TranslationService::Get();
    if (ok) {
        UpdateManager::ShowNoticeText(
            tr->Tr(L"Update",
                   L"Update complete. The new version takes effect on the next "
                   L"launch."),
            tr->Tr(L"RemoteControl", L"Deploy"));
    } else {
        std::wstring wmsg = Utf8ToWide(msg);
        std::wstring tmpl = tr->Tr(L"Update", L"Deploy refused: %s");
        wchar_t buf[512] = {};
        std::swprintf(buf, 512, tmpl.c_str(), wmsg.c_str());
        UpdateManager::ShowNoticeText(buf, tr->Tr(L"RemoteControl", L"Deploy"));
    }
}

void RemoteControlModule::HandleExeSwap(const uint8_t* p, uint32_t n, SOCKET s,
                                        std::mutex* mx, const char* role) {
    if (n <= 9) {
        const char* why = "bad EXE_SWAP payload";
        auto r = RemoteControl::Proto::exeResult(false, why, std::strlen(why));
        SendToPeer(s, mx, r);
        return;
    }
    uint64_t expect = RemoteControl::Proto::r64(p + 1);
    std::string nameA(reinterpret_cast<const char*>(p + 9), n - 9);
    std::wstring wname = Utf8ToWide(nameA);
    std::string roleStr(role);

    auto refuse = [&](const char* why) {
        Log(L"[!] deploy: refused from %hs: %hs\n", role, why);
        AppendDiagReport("DEPLOY REFUSED role=" + roleStr, why);
        auto r = RemoteControl::Proto::exeResult(false, why, std::strlen(why));
        SendToPeer(s, mx, r);
    };

    // Path-traversal guard: the name arrives over the wire and is joined to
    // SavePath below — never accept separators or parent references.
    if (wname.empty() || wname.size() > 200 ||
        wname.find_first_of(L"\\/") != std::wstring::npos ||
        wname.find(L"..") != std::wstring::npos) {
        refuse("invalid file name");
        return;
    }

    if (m_deployActive.exchange(true)) {
        refuse("previous deploy still running");
        return;
    }

    unsigned epoch = m_epoch.load();
    std::thread([this, expect, wname, epoch, s, mx, roleStr]() {
        auto fail = [this, s, mx, &roleStr](const char* why) {
            Log(L"[!] deploy: %hs\n", why);
            AppendDiagReport("DEPLOY REFUSED role=" + roleStr, why);
            auto r = RemoteControl::Proto::exeResult(false, why,
                                                     std::strlen(why));
            SendToPeer(s, mx, r);
            m_deployActive.store(false);
        };

        if (m_epoch.load() != epoch) {
            m_deployActive.store(false);
            return;
        }

        std::wstring save = GetValue(L"SavePath");
        if (save.empty()) { fail("SavePath is not configured"); return; }
        if (save.back() != L'\\' && save.back() != L'/') save += L'\\';
        std::wstring dest = save + wname;

        // The FT writer may still be streaming; poll until the size matches
        // the announced one (10s ceiling, aborts early on Stop/epoch change).
        // A leftover from a previous deploy can already sit at the target with
        // the same size, so a fresh write must be proven by an mtime newer
        // than the pre-announcement snapshot.
        std::error_code sec;
        auto stale = std::filesystem::last_write_time(dest, sec);
        bool complete = false;
        for (int i = 0; i < 50; ++i) {
            if (m_epoch.load() != epoch) {
                m_deployActive.store(false);
                return;
            }
            uint64_t sz = 0;
            std::error_code mec;
            auto wt = std::filesystem::last_write_time(dest, mec);
            if (FileSizeOf(dest, &sz) && sz == expect && mec == std::error_code{})
                if (wt > stale) { complete = true; break; }
            Sleep(200);
        }
        if (!complete) {
            uint64_t sz = 0;
            std::string got = FileSizeOf(dest, &sz)
                ? (std::to_string(sz) + "/" + std::to_string(expect) + " bytes")
                : "file missing";
            fail(("file incomplete after 10s (" + got + ")").c_str());
            return;
        }

        std::wstring own = ModulePath();
        if (dest == own) {
            fail("source path equals running executable");
            return;
        }

        uint64_t incomingV = UpdateManager::ReadFileVersion(dest);
        // The FT writer streams in place; the size can match while the final
        // bytes are still being flushed, so re-read the version until it
        // parses (0.0.0.0 means the resource was mid-write garbage).
        for (int i = 0; i < 25 && incomingV == 0; ++i) {
            if (m_epoch.load() != epoch) {
                m_deployActive.store(false);
                return;
            }
            Sleep(200);
            incomingV = UpdateManager::ReadFileVersion(dest);
        }
        uint64_t ownV = UpdateManager::ReadFileVersion(own);
        if (!UpdateManager::IsNewer(incomingV, ownV)) {
            std::string msg =
                "version " + ToUtf8(UpdateManager::VersionToString(incomingV)) +
                " is not newer than " + ToUtf8(UpdateManager::VersionToString(ownV));
            fail(msg.c_str());
            return;
        }

        std::wstring backup = UpdateManager::MakeBackupName(own);
        std::wstring err;
        if (!UpdateManager::SwapExecutable(dest, own, backup, &err)) {
            fail(ToUtf8(err).c_str());
            return;
        }

        Log(L"[+] deploy: swapped in %ls (backup %ls)\n", wname.c_str(),
            backup.c_str());
        AppendDiagReport("DEPLOY ACCEPTED role=" + roleStr,
                         "renamed " + ToUtf8(dest) + " -> " + ToUtf8(own) +
                             ", previous -> " + ToUtf8(backup));
        m_deployActive.store(false);
        auto r = RemoteControl::Proto::exeResult(true, "deployed", 8);
        SendToPeer(s, mx, r);
    }).detach();
}

void RemoteControlModule::CheckDiagTimeout() {
    std::lock_guard<std::mutex> lk(m_diagMx);
    if (m_diagPending.empty()) return;
    uint64_t now = GetTickCount64();
    for (auto it = m_diagPending.begin(); it != m_diagPending.end();) {
        if (now - it->tick > 5000) {
            Log(L"[!] diag: request seq=%u timed out\n",
                static_cast<unsigned>(it->seq));
            AppendDiagReport("REQUEST seq=" + std::to_string(it->seq),
                             "TIMEOUT after 5000ms (no DBG_RESP)");
            it = m_diagPending.erase(it);
        } else {
            ++it;
        }
    }
}

void RemoteControlModule::AppendDiagReport(const std::string& header,
                                           const std::string& body) {
    std::error_code ec;
    std::filesystem::path dir = Logger::GetLogDir();
    std::filesystem::create_directories(dir, ec);
    std::filesystem::path file = dir / L"RemoteDiagnostics.txt";

    HANDLE h = CreateFileW(file.c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE |
                               FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        Log(L"[!] diag: cannot open RemoteDiagnostics.txt\n");
        return;
    }

    std::string blob = "==== " + NowStamp() + " " + header + " ====\n" + body;
    if (blob.empty() || blob.back() != '\n') blob += '\n';
    blob += '\n';

    DWORD written = 0;
    WriteFile(h, blob.data(), static_cast<DWORD>(blob.size()), &written,
              nullptr);
    CloseHandle(h);
}

RemoteControl::Diag::SnapshotData RemoteControlModule::CollectSnapshot(
    const char* role) {
    using RemoteControl::Diag::SnapshotData;
    SnapshotData d;
    d.role = role;
    d.pid = GetCurrentProcessId();

    uint64_t start = m_startTick.load();
    uint64_t now = GetTickCount64();
    d.uptimeSec = start ? (now - start) / 1000 : 0;

    std::wstring wexe = ModulePath();
    d.exePath = ToUtf8(wexe);
    d.version = ToUtf8(UpdateManager::VersionToString(
        UpdateManager::ReadFileVersion(wexe)));

    d.running = m_running.load();
    d.epoch = m_epoch.load();
    {
        std::lock_guard<std::mutex> lk(m_queueMx);
        d.queueDepth = m_eventQueue.size();
    }
    d.hookLocal = RemoteControlHook::IsActive() ? 1 : 0;
    d.hookRemote = m_targetHookActive;

    auto sockStr = [](SOCKET v) -> std::string {
        if (v == INVALID_SOCKET) return "-";
        char b[24];
        std::snprintf(b, sizeof(b), "0x%llx", static_cast<unsigned long long>(v));
        return b;
    };
    {
        std::lock_guard<std::mutex> lk(m_svMx);
        d.serverSock = sockStr(m_svSocket);
    }
    {
        std::lock_guard<std::mutex> lk(m_clMx);
        d.clientSock = sockStr(m_clSocket);
    }
    d.listenSock = sockStr(m_svListen.load());

    d.thrAccept = m_thrAccept.load();
    d.thrSend = m_thrSend.load();
    d.thrSvRecv = m_thrSvRecv.load();
    d.thrClient = m_thrClient.load();
    d.thrLed = m_thrLed.load();

    d.txPkt = m_txPkt.load();
    d.txBytes = m_txBytes.load();
    d.rxPkt = m_rxPkt.load();
    d.rxBytes = m_rxBytes.load();
    uint64_t ltx = m_lastTxTick.load();
    uint64_t lrx = m_lastRxTick.load();
    d.lastTxAgoMs = ltx ? (now - ltx) : 0;
    d.lastRxAgoMs = lrx ? (now - lrx) : 0;

    // UI responsiveness probe: the reported symptom is a frozen window with
    // healthy worker threads, so this measures the main window directly.
    // SendMessageTimeout(WM_NULL) returns the wndproc result (0) even on
    // success, so the hang signal is GetLastError()==ERROR_TIMEOUT — not a
    // zero return value.
    d.uiState = "no window";
    HWND mainWnd = m_host ? m_host->GetMainWindow() : nullptr;
    if (mainWnd) {
        d.uiVisible = IsWindowVisible(mainWnd) != 0;
        DWORD_PTR ignored = 0;
        DWORD t0 = GetTickCount();
        SetLastError(0);
        LRESULT r = SendMessageTimeoutW(mainWnd, WM_NULL, 0, 0,
                                        SMTO_ABORTIFHUNG, 500, &ignored);
        DWORD err = GetLastError();
        if (r != 0 || err == 0) {
            char b[48];
            std::snprintf(b, sizeof(b), "responsive (%lu ms)",
                          static_cast<unsigned long>(GetTickCount() - t0));
            d.uiState = b;
        } else if (err == ERROR_TIMEOUT) {
            d.uiState = "HUNG (no response within 500ms)";
        } else {
            char b[48];
            std::snprintf(b, sizeof(b), "probe failed (err %lu)",
                          static_cast<unsigned long>(err));
            d.uiState = b;
        }
    }

    d.logTail = ReadTailLines(Logger::GetLogDir() / L"Log_RemoteControl.txt",
                              40);
    return d;
}

//==============================================================================
// Context Menu
//==============================================================================

std::vector<ContextMenuItem> RemoteControlModule::GetContextMenuItems() const {
    std::vector<ContextMenuItem> items;

    items.push_back({ m_running ? kMenuStop : kMenuStart,
        TranslationService::Get()->Tr(L"RemoteControl",
            m_running ? L"Stop Remote Control" : L"Start Remote Control") });

    std::wstring savePath = GetStringValue(L"SavePath", L"D:\\Temp");
    items.push_back({ kMenuSelectPath,
        TranslationService::Get()->Tr(L"RemoteControl", L"Select Save Path..."),
        MF_STRING, savePath });

    UINT avail = m_running ? MF_STRING : MF_GRAYED;
    items.push_back({ kMenuDiag,
        TranslationService::Get()->Tr(L"RemoteControl",
                                      L"Request Remote Diagnostics"),
        avail });
    items.push_back({ kMenuDeploy,
        TranslationService::Get()->Tr(L"RemoteControl",
                                      L"Deploy My Build to Peer"),
        avail });

    return items;
}

void RemoteControlModule::ExecuteContextMenuItem(int itemId) {
    if (itemId == kMenuStart) {
        StartRemoteControl();
        SaveResumeState(m_running.load());
    } else if (itemId == kMenuStop) {
        StopRemoteControl();
        SaveResumeState(m_running.load());
    } else if (itemId == kMenuSelectPath) {
        OpenSavePathDialog();
    } else if (itemId == kMenuDiag) {
        RequestDiagnostics();
    } else if (itemId == kMenuDeploy) {
        DeployMyBuild();
    }
}

void RemoteControlModule::OpenSavePathDialog() {
    HWND hParent = m_host ? m_host->GetMainWindow() : GetDesktopWindow();

    std::wstring currentPath = GetStringValue(L"SavePath", L"D:\\Temp");

    IFileOpenDialog* pDlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg));
    if (SUCCEEDED(hr) && pDlg) {
        DWORD flags;
        pDlg->GetOptions(&flags);
        pDlg->SetOptions(flags | FOS_PICKFOLDERS);

        if (!currentPath.empty()) {
            IShellItem* pFolder = nullptr;
            hr = SHCreateItemFromParsingName(currentPath.c_str(), nullptr,
                                              IID_PPV_ARGS(&pFolder));
            if (SUCCEEDED(hr) && pFolder) {
                pDlg->SetFolder(pFolder);
                pFolder->Release();
            }
        }

        hr = pDlg->Show(hParent);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            hr = pDlg->GetResult(&pItem);
            if (SUCCEEDED(hr) && pItem) {
                PWSTR pszPath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
                if (SUCCEEDED(hr) && pszPath) {
                    SetValue(L"SavePath", pszPath);
                    SaveConfig();
                    CoTaskMemFree(pszPath);
                }
                pItem->Release();
            }
        }
        pDlg->Release();
    }
}
