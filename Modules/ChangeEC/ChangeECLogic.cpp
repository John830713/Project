//==============================================================================
// ChangeECLogic.cpp - Change EC logic implementation
//==============================================================================

#include "ChangeECLogic.h"
#include "../../Core/Logger.h"
#include "../../Services/FileService.h"
#include <algorithm>

//==============================================================================
// DetectFiles - Identify BIOS and EC files
//==============================================================================

void ChangeECLogic::DetectFiles(const std::filesystem::path& file1, const std::filesystem::path& file2, 
                                std::filesystem::path& biosOut, std::filesystem::path& ecOut) {
    if (FileService::HasExtension(file1, L".rom") && FileService::HasExtension(file2, L".bin")) { biosOut = file1; ecOut = file2; }
    else if (FileService::HasExtension(file2, L".rom") && FileService::HasExtension(file1, L".bin")) { biosOut = file2; ecOut = file1; }
    else {
        if (FileService::GetFileSize(file1) >= FileService::GetFileSize(file2)) { biosOut = file1; ecOut = file2; }
        else { biosOut = file2; ecOut = file1; }
    }
}

//==============================================================================
// Generate - Merge EC binary into BIOS ROM
//==============================================================================

bool ChangeECLogic::Generate(const ChangeECOpenRequest& req, unsigned long offset, const std::filesystem::path& outputDir, const std::wstring& logSourceName) {
    try {
        std::vector<uint8_t> romData = FileService::ReadBinaryFile(std::filesystem::path(req.biosPath));
        std::vector<uint8_t> ecData = FileService::ReadBinaryFile(std::filesystem::path(req.ecPath));

        if (romData.size() < offset + ecData.size()) {
            romData.resize(offset + ecData.size(), 0xFF);
        }

        std::copy(ecData.begin(), ecData.end(), romData.begin() + offset);

        std::filesystem::path outPath = outputDir / (std::filesystem::path(req.biosPath).stem().wstring() + L"_mod.rom");
        if (!FileService::WriteBinaryFile(outPath, romData)) {
            Logger::WriteModuleLog(logSourceName, L"Failed to write output file.");
            return false;
        }

        Logger::WriteModuleLog(logSourceName, L"Generate success: " + outPath.wstring());
        return true;
    } catch (const std::exception& e) {
        Logger::WriteModuleLog(logSourceName, std::wstring(L"Generate exception: ") + std::wstring(e.what(), e.what() + strlen(e.what())));
        return false;
    } catch (...) {
        Logger::WriteModuleLog(logSourceName, L"Generate failed: Unknown exception.");
        return false;
    }
}