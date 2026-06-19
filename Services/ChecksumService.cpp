//==============================================================================
// ChecksumService.cpp - File checksum calculation implementation
//==============================================================================

#include "ChecksumService.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cwctype>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// IsSupportedFile - checks if file extension is .bin or .rom (case-insensitive)
//------------------------------------------------------------------------------

bool ChecksumService::IsSupportedFile(const fs::path& filePath) {
    std::wstring ext = filePath.extension().wstring();
    for (auto& c : ext) {
        c = static_cast<wchar_t>(towlower(c));
    }

    return ext == L".bin" || ext == L".rom";
}

//------------------------------------------------------------------------------
// CalculateFileChecksum - reads file in 8KB chunks and sums all bytes
// Returns the checksum as an uppercase hex string (zero-padded to 10 digits)
//------------------------------------------------------------------------------

bool ChecksumService::CalculateFileChecksum(const fs::path& filePath, std::wstring& checksumOut) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    constexpr size_t kChunkSize = 8192;
    std::vector<unsigned char> buffer(kChunkSize);
    unsigned long long checksum = 0;

    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        std::streamsize count = file.gcount();

        for (std::streamsize i = 0; i < count; ++i) {
            checksum += buffer[static_cast<size_t>(i)];
        }
    }

    std::wstringstream ss;
    ss << std::uppercase
       << std::setfill(L'0')
       << std::setw(10)
       << std::hex
       << checksum;

    checksumOut = ss.str();
    return true;
}
