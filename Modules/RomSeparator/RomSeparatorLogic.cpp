//==============================================================================
// RomSeparatorLogic.cpp - ROM splitting logic implementation
//==============================================================================

#include "RomSeparatorLogic.h"
#include "../../Services/FileService.h"
#include <stdexcept>

//==============================================================================
// GetPossibleSplits
//==============================================================================

std::vector<SplitOption> RomSeparatorLogic::GetPossibleSplits(unsigned long long totalSize) {
    std::vector<SplitOption> result;
    const unsigned long validParts[] = { SIZE_8MB, SIZE_16MB, SIZE_32MB };

    for (unsigned long left : validParts) {
        for (unsigned long right : validParts) {
            if (static_cast<unsigned long long>(left) + static_cast<unsigned long long>(right) == totalSize) {
                result.push_back({ left, right });
            }
        }
    }

    return result;
}

//==============================================================================
// IsSupportedRomSize
//==============================================================================

bool RomSeparatorLogic::IsSupportedRomSize(unsigned long long totalSize) {
    return !GetPossibleSplits(totalSize).empty();
}

//==============================================================================
// SizeToMBString
//==============================================================================

std::wstring RomSeparatorLogic::SizeToMBString(unsigned long long size) {
    return std::to_wstring(size / (1024ULL * 1024ULL)) + L"MB";
}

//==============================================================================
// BuildOutputPaths
//==============================================================================

std::pair<fs::path, fs::path> RomSeparatorLogic::BuildOutputPaths(
    const fs::path& outputDir,
    const SplitOption& split) {

    if (split.leftSize == split.rightSize) {
        return {
            outputDir / (L"Rom1_" + SizeToMBString(split.leftSize) + L".rom"),
            outputDir / (L"Rom2_" + SizeToMBString(split.rightSize) + L".rom")
        };
    }

    return {
        outputDir / (L"Rom_" + SizeToMBString(split.leftSize) + L".rom"),
        outputDir / (L"Rom_" + SizeToMBString(split.rightSize) + L".rom")
    };
}

//==============================================================================
// Generate
//==============================================================================

RomSeparatorGenerateResult RomSeparatorLogic::Generate(
    const RomSeparatorGenerateRequest& request,
    const fs::path& hostBaseDir) {

    if (!FileService::FileExists(request.romPath)) {
        throw std::runtime_error("Input ROM file not found.");
    }

    const auto totalSize = FileService::GetFileSize(request.romPath);
    const auto expectedSize =
        static_cast<unsigned long long>(request.split.leftSize) +
        static_cast<unsigned long long>(request.split.rightSize);

    if (totalSize != expectedSize) {
        throw std::runtime_error("Split size mismatch.");
    }

    fs::path outputDir;
    if (request.outputMode == RomSeparatorOutputMode::HostFolder) {
        outputDir = hostBaseDir;
    } else {
        outputDir = request.romPath.parent_path();
    }

    auto outputPaths = BuildOutputPaths(outputDir, request.split);

    std::vector<uint8_t> data = FileService::ReadBinaryFile(request.romPath);

    std::vector<uint8_t> rom1Data(
        data.begin(),
        data.begin() + static_cast<std::ptrdiff_t>(request.split.leftSize)
    );

    std::vector<uint8_t> rom2Data(
        data.begin() + static_cast<std::ptrdiff_t>(request.split.leftSize),
        data.begin() + static_cast<std::ptrdiff_t>(request.split.leftSize + request.split.rightSize)
    );

    FileService::WriteBinaryFile(outputPaths.first, rom1Data);
    FileService::WriteBinaryFile(outputPaths.second, rom2Data);

    RomSeparatorGenerateResult result;
    result.outputPath1 = outputPaths.first;
    result.outputPath2 = outputPaths.second;
    return result;
}