//==============================================================================
// RomCombinerTypes.h - ROM Combiner type definitions
// Purpose:   Defines data structures used by the Rom Combiner module:
//            RomInfo, RomCombinerOpenRequest, RomCombinerGenerateResult.
//==============================================================================

#pragma once
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

//--- Stores metadata and detection results for a single ROM file
struct RomInfo {
    //--- Filesystem path to the ROM file
    fs::path path;
    //--- Raw file size in bytes
    unsigned long originalSize = 0;
    //--- Human-readable type string (e.g. "8MB", "16MB+1024")
    std::wstring displayType;
    unsigned long long effectiveSize = 0; // Effective size in bytes
    //--- Whether the ROM needs trimming before combining
    bool needsTrim = false;
    //--- Size ranking for sorting/comparison
    int sizeRank = 0;
};

//--- Holds the paths and preferences for a combine operation
struct RomCombinerOpenRequest {
    //--- First ROM file path
    fs::path rom1Path;
    //--- Second ROM file path
    fs::path rom2Path;
    bool outputToOrg = true; // Output path: exe/base dir (true) or ROM folder (false)
};

//--- Returned after a generation attempt
struct RomCombinerGenerateResult {
    //--- Whether the generation completed successfully
    bool success;
    //--- Path to the generated output file
    fs::path outputPath;
    //--- Error description if generation failed
    std::wstring errorMessage;
};