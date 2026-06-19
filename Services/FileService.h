#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class FileService {
public:
    static std::vector<uint8_t> ReadBinaryFile(const fs::path& path);
    static bool WriteBinaryFile(const fs::path& path, const std::vector<uint8_t>& data);

    static bool FileExists(const fs::path& path);
    static uint64_t GetFileSize(const fs::path& path);

    static bool HasExtension(const fs::path& path, const std::wstring& ext);
};