//==============================================================================
// RomCombinerLogic.h - ROM combination logic
// Purpose:   Provides static methods for normalizing ROMs, generating
//            combined output, and resolving output paths.
//==============================================================================

#pragma once
#include "RomCombinerTypes.h"
#include <filesystem>

class RomCombinerLogic {
public:
    //==============================================================================
    // Public Static Methods
    //==============================================================================
    static RomCombinerGenerateResult Generate(
        const RomCombinerOpenRequest& request, 
        const RomInfo& info1, 
        const RomInfo& info2, 
        const std::filesystem::path& outputDir);

    static std::filesystem::path GetOutputPath(
        const RomCombinerOpenRequest& request, 
        const RomInfo& info1, 
        const std::filesystem::path& baseDir);
    
    static RomInfo NormalizeRom(const std::wstring& path);
};