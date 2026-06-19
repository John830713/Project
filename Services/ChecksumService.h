//==============================================================================
// ChecksumService.h - File checksum calculation service
// Purpose:   Provides file validation and checksum calculation.
//            Currently supports .bin and .rom files with a simple additive
//            checksum algorithm.
//==============================================================================

#pragma once

#include <string>
#include <filesystem>

class ChecksumService {
public:
    static bool IsSupportedFile(const std::filesystem::path& filePath);
    static bool CalculateFileChecksum(const std::filesystem::path& filePath, std::wstring& checksumOut);
};
