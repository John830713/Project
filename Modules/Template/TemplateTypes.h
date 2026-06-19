//==============================================================================
// TemplateTypes.h - Template data type definitions
// Purpose:   Defines data structures used by the Template module.
//==============================================================================

#pragma once
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

//---
// TemplateInfo - Stores template-specific information
//---
struct TemplateInfo {
    fs::path path;
    unsigned long originalSize = 0;
    std::wstring displayType;
    //---
    // Effective size in bytes
    //---
    unsigned long long effectiveSize = 0;

    //---
    // Generic flag for template logic
    //---
    bool needsProcess = false;
    int priority = 0;
};

//---
// TemplateOpenRequest - Request data for template operations
//---
struct TemplateOpenRequest {
    fs::path source1Path;
    fs::path source2Path;
    //---
    // Output path: exe/base dir (true) or source folder (false)
    //---
    bool outputToOrg = true;
};

//---
// TemplateGenerateResult - Return data for operation results
//---
struct TemplateGenerateResult {
    bool success;
    fs::path outputPath;
    std::wstring errorMessage;
};