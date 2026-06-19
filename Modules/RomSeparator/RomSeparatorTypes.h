//==============================================================================
// RomSeparatorTypes.h - ROM Separator shared type definitions
// Purpose:   Defines data structures used by RomSeparator modules.
//==============================================================================

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

//--- SplitOption ---
struct SplitOption {
    unsigned long leftSize;
    unsigned long rightSize;
};

//--- RomSeparatorOutputMode ---
enum class RomSeparatorOutputMode {
    HostFolder,
    SourceFolder
};

//--- RomSeparatorOpenRequest ---
struct RomSeparatorOpenRequest {
    fs::path romPath;
    RomSeparatorOutputMode outputMode;
    bool confirmOverwrite;
    bool showSuccessMessage;
    bool verboseLog;
};

//--- RomSeparatorGenerateRequest ---
struct RomSeparatorGenerateRequest {
    fs::path romPath;
    SplitOption split;
    RomSeparatorOutputMode outputMode;
    bool confirmOverwrite;
};

//--- RomSeparatorGenerateResult ---
struct RomSeparatorGenerateResult {
    fs::path outputPath1;
    fs::path outputPath2;
};