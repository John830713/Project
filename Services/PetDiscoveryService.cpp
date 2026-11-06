#include "PetDiscoveryService.h"

#include "NetworkService.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>

namespace {

constexpr char kMagic[8] = { 'P', 'E', 'T', 'D', 'I', 'S', 'C', '1' };
constexpr size_t kMaxNameBytes = 255;

std::wstring LowerName(const std::wstring& name) {
    std::wstring out = name;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return out;
}

bool SameName(const std::wstring& a, const std::wstring& b) {
    return LowerName(a) == LowerName(b);
}

std::wstring FormatIpv4(const sockaddr_in& addr) {
    uint32_t value = ntohl(addr.sin_addr.s_addr);
    wchar_t buffer[32] = {};
    swprintf(buffer, 32, L"%u.%u.%u.%u",
             (value >> 24) & 0xFF, (value >> 16) & 0xFF,
             (value >> 8) & 0xFF, value & 0xFF);
    return buffer;
}

}  // namespace

PetDiscoveryService* PetDiscoveryService::Get() {
    static PetDiscoveryService instance;
    return &instance;
}

PetDiscoveryService::~PetDiscoveryService() {
    Stop();
}

std::wstring PetDiscoveryService::LocalComputerName() {
    wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(name, &size) || size == 0) return L"";
    return std::wstring(name, size);
}

std::vector<char> PetDiscoveryService::EncodeBeacon(const std::wstring& name) {
    std::vector<char> out(kMagic, kMagic + sizeof(kMagic));
    if (name.empty()) return out;

    int bytes = WideCharToMultiByte(CP_UTF8, 0, name.c_str(),
                                    static_cast<int>(name.size()),
                                    nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return out;

    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, name.c_str(),
                        static_cast<int>(name.size()), utf8.data(), bytes,
                        nullptr, nullptr);
    if (utf8.size() > kMaxNameBytes) utf8.resize(kMaxNameBytes);
    out.insert(out.end(), utf8.begin(), utf8.end());
    return out;
}

bool PetDiscoveryService::DecodeBeacon(const char* data, int size,
                                       std::wstring& name) {
    if (!data || size <= static_cast<int>(sizeof(kMagic))) return false;
    if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0) return false;

    const char* utf8 = data + sizeof(kMagic);
    int bytes = size - static_cast<int>(sizeof(kMagic));
    std::wstring wide(static_cast<size_t>(bytes), L'\0');
    int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, bytes,
                                    wide.data(), bytes);
    if (chars <= 0) return false;
    wide.resize(static_cast<size_t>(chars));

    // The name is untrusted network input; never feed control characters to
    // DrawTextW or the log.
    for (wchar_t& c : wide) {
        if (c < 0x20) c = L'?';
    }
    name = wide;
    return true;
}

void PetDiscoveryService::Start() {
    if (m_thread.joinable()) return;

    if (!NetworkService::Initialize()) return;
    m_wsaReady = true;

    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET) {
        NetworkService::Cleanup();
        m_wsaReady = false;
        return;
    }

    BOOL yes = TRUE;
    setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST,
               reinterpret_cast<const char*>(&yes), sizeof(yes));
    setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_port = htons(kPort);
    local.sin_addr.s_addr = INADDR_ANY;
    if (bind(m_socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        NetworkService::Cleanup();
        m_wsaReady = false;
        return;
    }

    u_long nonBlocking = 1;
    ioctlsocket(m_socket, FIONBIO, &nonBlocking);

    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        NetworkService::Cleanup();
        m_wsaReady = false;
        return;
    }

    m_thread = std::thread(&PetDiscoveryService::Run, this);
}

void PetDiscoveryService::Stop() {
    if (m_stopEvent) SetEvent(m_stopEvent);
    if (m_thread.joinable()) m_thread.join();

    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
    if (m_wsaReady) {
        NetworkService::Cleanup();
        m_wsaReady = false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_peers.clear();
}

void PetDiscoveryService::Run() {
    uint64_t nextBroadcast = 0;
    while (WaitForSingleObject(m_stopEvent, 100) == WAIT_TIMEOUT) {
        uint64_t now = GetTickCount64();
        if (now >= nextBroadcast) {
            Broadcast();
            nextBroadcast = now + kBeaconIntervalMs;
        }
        ReceivePending();
        Prune(now);
    }
}

void PetDiscoveryService::Broadcast() {
    if (m_socket == INVALID_SOCKET) return;

    std::vector<char> beacon = EncodeBeacon(LocalComputerName());
    if (beacon.size() <= sizeof(kMagic)) return;

    sockaddr_in target = {};
    target.sin_family = AF_INET;
    target.sin_port = htons(kPort);
    target.sin_addr.s_addr = INADDR_BROADCAST;
    sendto(m_socket, beacon.data(), static_cast<int>(beacon.size()), 0,
           reinterpret_cast<sockaddr*>(&target), sizeof(target));
}

void PetDiscoveryService::ReceivePending() {
    for (;;) {
        char buffer[512] = {};
        sockaddr_in from = {};
        int fromLen = sizeof(from);
        int received = recvfrom(m_socket, buffer, sizeof(buffer), 0,
                                reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (received == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) return;
            return;
        }

        std::wstring name;
        if (!DecodeBeacon(buffer, received, name) || name.empty()) continue;
        if (SameName(name, LocalComputerName())) continue;

        PetPeer peer;
        peer.computerName = name;
        peer.address = FormatIpv4(from);
        peer.lastSeenMs = GetTickCount64();

        std::lock_guard<std::mutex> lock(m_mutex);
        m_peers[LowerName(name)] = peer;
    }
}

void PetDiscoveryService::Prune(uint64_t now) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_peers.begin(); it != m_peers.end();) {
        if (now >= it->second.lastSeenMs &&
            now - it->second.lastSeenMs > kPeerTtlMs) {
            it = m_peers.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<PetPeer> PetDiscoveryService::GetPeers() const {
    std::vector<PetPeer> out;
    uint64_t now = GetTickCount64();
    std::wstring self = LocalComputerName();

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& kv : m_peers) {
        if (now >= kv.second.lastSeenMs &&
            now - kv.second.lastSeenMs > kPeerTtlMs) {
            continue;
        }
        if (SameName(kv.second.computerName, self)) continue;
        out.push_back(kv.second);
    }

    std::sort(out.begin(), out.end(),
              [](const PetPeer& a, const PetPeer& b) {
                  return LowerName(a.computerName) < LowerName(b.computerName);
              });
    return out;
}
