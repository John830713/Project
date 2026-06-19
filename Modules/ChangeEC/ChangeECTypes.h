//==============================================================================
// ChangeECTypes.h - Change EC type definitions
// Purpose:   Defines data structures used by the Change EC module.
//==============================================================================

#pragma once
#include <string>
#include <filesystem>

struct ChangeECOpenRequest {
    std::wstring biosPath;  //--- Path to the BIOS ROM file
    std::wstring ecPath;    //--- Path to the EC binary file
};