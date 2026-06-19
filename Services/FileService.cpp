#include "FileService.h"

#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

std::vector<uint8_t> FileService::ReadBinaryFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }

    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size < 0) {
        throw std::runtime_error("Failed to determine file size: " + path.string());
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (size > 0) {
        file.read(reinterpret_cast<char*>(buffer.data()), size);
        if (!file) {
            throw std::runtime_error("Failed to read file: " + path.string());
        }
    }

    return buffer;
}

bool FileService::WriteBinaryFile(const fs::path& path, const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    if (!data.empty()) {
        file.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
        if (!file) {
            return false;
        }
    }

    return true;
}

bool FileService::FileExists(const fs::path& path) {
    return fs::exists(path) && fs::is_regular_file(path);
}

uint64_t FileService::GetFileSize(const fs::path& path) {
    try {
        if (!FileExists(path)) {
            return 0;
        }
        return fs::file_size(path);
    } catch (...) {
        return 0;
    }
}

bool FileService::HasExtension(const fs::path& path, const std::wstring& ext) {
    std::wstring actual = path.extension().wstring();
    for (auto& c : actual) c = static_cast<wchar_t>(towlower(c));
    std::wstring extLower = ext;
    for (auto& c : extLower) c = static_cast<wchar_t>(towlower(c));
    return actual == extLower;
}