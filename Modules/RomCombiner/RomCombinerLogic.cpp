//==============================================================================
// RomCombinerLogic.cpp - ROM combination logic implementation
//==============================================================================

#include "RomCombinerLogic.h"
#include "../../Core/Logger.h"
#include "../../Services/FileService.h"
#include <string>
#include <cstdint>

namespace fs = std::filesystem;

//==============================================================================
// Helper Functions (anonymous namespace / file-static)
//==============================================================================

fs::path MakeTrimmedOutputPath(const RomInfo& info) {
    fs::path p = info.path;
    return p.replace_extension(L".trimmed.rom");
}

//==============================================================================
// NormalizeRom - reads ROM metadata and detects type / trim requirement
//==============================================================================

RomInfo RomCombinerLogic::NormalizeRom(const std::wstring& path) {
    fs::path p(path);
    RomInfo info;
    info.path = p;
    
    if (!FileService::FileExists(p)) return info;

    uint64_t size = FileService::GetFileSize(p);
    info.originalSize = size;

    const uint64_t SIZE_8MB = 8 * 1024 * 1024;
    const uint64_t SIZE_16MB = 16 * 1024 * 1024;
    const uint64_t SIZE_32MB = 32 * 1024 * 1024;

    auto SetInfo = [&](uint64_t effective, const std::wstring& type, bool trim, int rank) {
        info.effectiveSize = effective;
        info.displayType = type;
        info.needsTrim = trim;
        info.sizeRank = rank;
    };

    if (size == SIZE_8MB) SetInfo(SIZE_8MB, L"8MB", false, 8);
    else if (size == SIZE_8MB + 1024) SetInfo(SIZE_8MB, L"8MB+1024", true, 8);
    else if (size == SIZE_16MB) SetInfo(SIZE_16MB, L"16MB", false, 16);
    else if (size == SIZE_16MB + 1024) SetInfo(SIZE_16MB, L"16MB+1024", true, 16);
    else if (size == SIZE_32MB) SetInfo(SIZE_32MB, L"32MB", false, 32);
    else if (size == SIZE_32MB + 1024) SetInfo(SIZE_32MB, L"32MB+1024", true, 32);
    else {
        info.effectiveSize = size;
        info.displayType = L"Unknown";
    }

    return info;
}

//==============================================================================
// Generate - reads two ROMs and writes them concatenated to the output path
//==============================================================================

RomCombinerGenerateResult RomCombinerLogic::Generate(const RomCombinerOpenRequest& request, const RomInfo& info1, const RomInfo& info2, const fs::path& outputDir) {
    try {
        Logger::WriteModuleLog( L"RomCombiner", L"Starting Generate process...");

        auto rom1Data = FileService::ReadBinaryFile(info1.path);
        auto rom2Data = FileService::ReadBinaryFile(info2.path);

        if (rom1Data.empty() || rom2Data.empty()) {
            Logger::WriteModuleLog(L"RomCombiner", L"Error: Failed to read ROM data.");
            return { false, L"", L"Failed to read ROM data." };
        }

        if (info1.needsTrim) FileService::WriteBinaryFile(MakeTrimmedOutputPath(info1), rom1Data);
        if (info2.needsTrim) FileService::WriteBinaryFile(MakeTrimmedOutputPath(info2), rom2Data);

        fs::path outputPath = outputDir / L"Combined.rom";
        Logger::WriteModuleLog(L"RomCombiner", L"Writing to: " + outputPath.wstring());

        std::vector<uint8_t> combined;
        combined.reserve(rom1Data.size() + rom2Data.size());
        combined.insert(combined.end(), rom1Data.begin(), rom1Data.end());
        combined.insert(combined.end(), rom2Data.begin(), rom2Data.end());

        if (!FileService::WriteBinaryFile(outputPath, combined)) {
            Logger::WriteModuleLog(L"RomCombiner", L"Error: Failed to write output file.");
            return { false, L"", L"Failed to write output file." };
        }

        Logger::WriteModuleLog(L"RomCombiner", L"Generate success.");
        
        RomCombinerGenerateResult res;
        res.success = true;
        res.outputPath = outputPath;
        return res;
    }
    catch (const std::exception& e) {
        std::string err = e.what();
        Logger::WriteModuleLog(L"RomCombiner", L"Exception: " + std::wstring(err.begin(), err.end()));
        return { false, L"", L"Exception occurred" };
    }
}

//==============================================================================
// GetOutputPath - resolves the output directory based on user preference
//==============================================================================

fs::path RomCombinerLogic::GetOutputPath(const RomCombinerOpenRequest& request, const RomInfo& info1, const fs::path& baseDir) {
    return request.outputToOrg ? baseDir : info1.path.parent_path();
}