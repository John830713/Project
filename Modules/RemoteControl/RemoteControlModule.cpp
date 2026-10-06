#include "RemoteControlModule.h"
#include "RemoteControlHook.h"
#include "../../Core/ConfigManager.h"
#include "../../Core/Logger.h"
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
#include <vector>

namespace {

// A vanished peer must not pin a worker thread indefinitely. Without these the
// accepted socket blocks in send()/recv() until the OS gives up (Windows
// keepalive defaults to a 2 hour idle probe), holding m_svMx the whole time and
// deadlocking StopRemoteControl on the UI thread.
constexpr DWORD kIoTimeoutMs = 3000;
constexpr DWORD kKeepAliveIdleMs = 5000;
constexpr DWORD kKeepAliveIntervalMs = 1000;

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
    {
        std::lock_guard<std::mutex> lk(self->m_svMx);
        if (self->m_svSocket != INVALID_SOCKET)
            NetworkService::Send(self->m_svSocket, hs.data(), static_cast<int>(hs.size()));
    }

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
        NetworkService::Send(c, hs.data(), static_cast<int>(hs.size()));

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
            if (m_svSocket != INVALID_SOCKET &&
                !NetworkService::Send(m_svSocket, batch.data(),
                                      static_cast<int>(batch.size())))
            {
                closesocket(m_svSocket);
                m_svSocket = INVALID_SOCKET;
                lost = true;
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
        if (select(0, &fs, nullptr, nullptr, &tv) <= 0) continue;

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
        }
    }
}

//==============================================================================
// Client Thread (connect + receive + replay)
//==============================================================================

void RemoteControlModule::RunClient() {
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
            if (sr == 0) continue;

            // Each Replay* function handles its own s_replaying guard, so we
            // drain all available messages immediately with no settle delay.
            bool ok2 = true;
            do {
                std::vector<uint8_t> pay;
                if (!NetworkService::RecvMsg(s, pay)) { ok2 = false; break; }

                auto* p = pay.data();
                auto n = static_cast<uint32_t>(pay.size());

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
                    std::lock_guard<std::mutex> lk(m_clMx);
                    if (m_clSocket != INVALID_SOCKET)
                        NetworkService::Send(m_clSocket, reply.data(),
                                             static_cast<int>(reply.size()));
                    break;
                }
                case RemoteControl::Proto::HOOK_STATUS:
                    if (n >= 2) {
                        InterlockedExchange(&m_targetHookActive, p[1] ? 1 : 0);
                        UpdateLed();
                    }
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

    SetWindowPos(m_led, HWND_TOPMOST, x, y, ledSize, ledSize,
                 SWP_NOACTIVATE | SWP_NOZORDER);
}

void RemoteControlModule::RunLeds() {
    const unsigned epoch = m_epoch.load();
    while (m_epoch == epoch) {
        Sleep(200);
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
