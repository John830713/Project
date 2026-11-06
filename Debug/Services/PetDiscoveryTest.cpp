// PetDiscoveryTest.cpp - Tests for PetDiscoveryService
//
// Covers the offline surface only: the beacon wire format and the local
// computer name. The UDP worker itself is exercised by hand across two
// machines, because a unit test that broadcasts on the real LAN would show up
// in every running pet's "其他寵物" list on the build machine.
//
// mingw32-make Debug/Services/PetDiscoveryTest.exe

#include <cstdio>
#include <string>
#include <vector>

#include "Services/PetDiscoveryService.h"

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

static bool HasControlChars(const std::wstring& text) {
    for (wchar_t c : text) {
        if (c < 0x20) return true;
    }
    return false;
}

int main() {
    //--- local identity ----------------------------------------------------
    std::wstring self = PetDiscoveryService::LocalComputerName();
    CHECK(!self.empty(), "local computer name is non-empty");
    CHECK(!HasControlChars(self), "local computer name has no control chars");

    //--- beacon round-trip: ASCII -----------------------------------------
    {
        std::vector<char> beacon = PetDiscoveryService::EncodeBeacon(L"PC-01");
        CHECK(beacon.size() == 8 + 5, "ASCII beacon length is magic + name");
        CHECK(std::string(beacon.data(), 8) == "PETDISC1", "ASCII beacon magic");

        std::wstring decoded;
        CHECK(PetDiscoveryService::DecodeBeacon(beacon.data(),
                                                (int)beacon.size(), decoded),
              "ASCII beacon decodes");
        CHECK(decoded == L"PC-01", "ASCII name round-trips");
    }

    //--- beacon round-trip: CJK -------------------------------------------
    {
        std::vector<char> beacon = PetDiscoveryService::EncodeBeacon(L"測試機");
        std::wstring decoded;
        CHECK(PetDiscoveryService::DecodeBeacon(beacon.data(),
                                                (int)beacon.size(), decoded),
              "CJK beacon decodes");
        CHECK(decoded == L"測試機", "CJK name round-trips as UTF-8");
    }

    //--- rejection paths ---------------------------------------------------
    {
        std::wstring decoded;
        const char shortPacket[4] = { 'P', 'E', 'T', 'D' };
        CHECK(!PetDiscoveryService::DecodeBeacon(shortPacket, 4, decoded),
              "short packet rejected");

        std::vector<char> beacon = PetDiscoveryService::EncodeBeacon(L"PC-02");
        beacon[0] = 'X';
        CHECK(!PetDiscoveryService::DecodeBeacon(beacon.data(),
                                                 (int)beacon.size(), decoded),
              "bad magic rejected");

        std::vector<char> magicOnly(beacon.begin(), beacon.begin() + 8);
        CHECK(!PetDiscoveryService::DecodeBeacon(magicOnly.data(),
                                                 (int)magicOnly.size(), decoded),
              "magic with no name rejected");

        const char invalidUtf8[] = { 'P', 'E', 'T', 'D', 'I', 'S', 'C', '1',
                                     (char)0xFF, (char)0xFE };
        CHECK(!PetDiscoveryService::DecodeBeacon(invalidUtf8,
                                                 (int)sizeof(invalidUtf8),
                                                 decoded),
              "invalid UTF-8 rejected");
    }

    //--- untrusted network input is sanitized ------------------------------
    {
        std::vector<char> beacon = PetDiscoveryService::EncodeBeacon(L"A\x01" L"B");
        std::wstring decoded;
        CHECK(PetDiscoveryService::DecodeBeacon(beacon.data(),
                                                (int)beacon.size(), decoded),
              "control-char beacon still decodes");
        CHECK(!HasControlChars(decoded), "control characters are replaced");
        CHECK(decoded == L"A?B", "replacement keeps the visible characters");
    }

    //--- empty name keeps the magic only (never broadcast) -----------------
    {
        std::vector<char> beacon = PetDiscoveryService::EncodeBeacon(L"");
        CHECK(beacon.size() == 8, "empty name encodes magic only");
        std::wstring decoded;
        CHECK(!PetDiscoveryService::DecodeBeacon(beacon.data(),
                                                 (int)beacon.size(), decoded),
              "magic-only beacon is not accepted as a peer");
    }

    //--- no worker started -> no peers ------------------------------------
    CHECK(PetDiscoveryService::Get() != nullptr, "singleton available");
    CHECK(PetDiscoveryService::Get()->GetPeers().empty(),
          "no peers before Start()");

    printf("\n%d/%d passed\n", g_tests - g_fails, g_tests);
    return g_fails == 0 ? 0 : 1;
}
