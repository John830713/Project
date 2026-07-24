//==============================================================================
// Release_PackageTypes.h - Release_Package data type definitions
// Purpose:   Defines data structures for token extraction and rule matching.
//==============================================================================

#pragma once

#include <string>
#include <vector>

struct RPParam {
    std::wstring name;
    std::wstring value;
    std::wstring regex;
    std::wstring example;
};

struct RPRule {
    bool enabled = true;
    bool useDefaultSrc = true;
    std::wstring sourceDir;
    std::wstring pattern = L"*";
    bool useDefaultDest = true;
    std::wstring destDir;
    std::wstring rename = L"{filename}{ext}";
};

struct RPMatchResult {
    std::wstring originalPath;
    std::wstring destPath;
    int ruleIndex;
};