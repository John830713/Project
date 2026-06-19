//==============================================================================
// TemplateLogic.h - Template business logic
// Purpose:   Provides static methods for template data processing, output
//            path determination, and data normalization.
//==============================================================================

#pragma once
#include "TemplateTypes.h"
#include <filesystem>

class TemplateLogic {
public:
    //==============================================================================
    // Public Static Methods
    //==============================================================================
    static TemplateGenerateResult Generate(
        const TemplateOpenRequest& request, 
        const TemplateInfo& info1, 
        const TemplateInfo& info2, 
        const std::filesystem::path& outputDir);

    static std::filesystem::path GetOutputPath(
        const TemplateOpenRequest& request, 
        const TemplateInfo& info1, 
        const std::filesystem::path& baseDir);

    static TemplateInfo NormalizeData(const std::wstring& path);
};