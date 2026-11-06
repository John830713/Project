#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// One pet seen on the LAN.
struct PetPeer {
    std::wstring computerName;
    std::wstring address;  // dotted IPv4 of the sender
    uint64_t lastSeenMs = 0;
};

// LAN presence discovery for other running pets.
//
// Every instance broadcasts a small UDP beacon (port kPort) containing its
// computer name, and collects the beacons it receives. There is no handshake
// and no payload beyond the name: the goal is only "which machines have a pet
// online right now", so a lost datagram is fixed by the next interval.
//
// The worker thread owns the socket and the peer table; UI code only calls
// GetPeers() for a snapshot, so no window is ever touched off the UI thread.
class PetDiscoveryService {
public:
    static PetDiscoveryService* Get();

    static constexpr unsigned short kPort = 45789;
    static constexpr uint64_t kBeaconIntervalMs = 3000;
    // Three missed beacons before a machine drops off the list.
    static constexpr uint64_t kPeerTtlMs = 10000;

    // Idempotent. Safe to call again after Stop() (tray "Reset Pet Window").
    void Start();
    void Stop();

    // Snapshot of live peers (TTL-filtered, self excluded), sorted by name.
    std::vector<PetPeer> GetPeers() const;

    static std::wstring LocalComputerName();

    // Wire format: 8-byte magic "PETDISC1" followed by the UTF-8 name
    // (no terminator). Exposed for the headless unit test.
    static std::vector<char> EncodeBeacon(const std::wstring& name);
    static bool DecodeBeacon(const char* data, int size, std::wstring& name);

private:
    PetDiscoveryService() = default;
    ~PetDiscoveryService();
    PetDiscoveryService(const PetDiscoveryService&) = delete;
    PetDiscoveryService& operator=(const PetDiscoveryService&) = delete;

    void Run();
    void Broadcast();
    void ReceivePending();
    void Prune(uint64_t now);

    SOCKET m_socket = INVALID_SOCKET;
    HANDLE m_stopEvent = nullptr;
    std::thread m_thread;
    bool m_wsaReady = false;

    mutable std::mutex m_mutex;
    std::map<std::wstring, PetPeer> m_peers;  // key: lower-cased name
};
