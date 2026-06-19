//==============================================================================
// ChangeECLogic.h - Change EC logic interface
// Purpose:   Provides core logic for detecting and generating EC modifications.
//==============================================================================

#pragma once
#include "ChangeECTypes.h"
#include <vector>
#include <filesystem>

//==============================================================================
// class ChangeECLogic
//==============================================================================

class ChangeECLogic {
public:
    static void DetectFiles(const std::filesystem::path& file1, const std::filesystem::path& file2, 
                            std::filesystem::path& biosOut, std::filesystem::path& ecOut);
    
    // Added logSourceName parameter for log output
    static bool Generate(const ChangeECOpenRequest& req, unsigned long offset, 
                         const std::filesystem::path& outputDir, const std::wstring& logSourceName);
};