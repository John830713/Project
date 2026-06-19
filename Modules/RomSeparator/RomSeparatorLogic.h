//==============================================================================
// RomSeparatorLogic.h - ROM splitting logic interface
// Purpose:   Provides static methods for splitting ROM files, including size
//            validation, split calculation, and file I/O.
//==============================================================================

#pragma once

#include "RomSeparatorTypes.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

//==============================================================================
// RomSeparatorLogic
//==============================================================================
class RomSeparatorLogic {
public:
    //--- Size Constants ---
    static constexpr unsigned long SIZE_8MB  = 8  * 1024 * 1024;
    static constexpr unsigned long SIZE_16MB = 16 * 1024 * 1024;
    static constexpr unsigned long SIZE_32MB = 32 * 1024 * 1024;

public:
    //--- Public Methods ---
    static std::vector<SplitOption> GetPossibleSplits(unsigned long long totalSize);
    static bool IsSupportedRomSize(unsigned long long totalSize);

    static std::wstring SizeToMBString(unsigned long long size);

    static std::pair<fs::path, fs::path> BuildOutputPaths(
        const fs::path& outputDir,
        const SplitOption& split);

    static RomSeparatorGenerateResult Generate(
        const RomSeparatorGenerateRequest& request,
        const fs::path& hostBaseDir);

};