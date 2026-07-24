//==============================================================================
// Release_PackageLogic.h - Release_Package pure logic
// Purpose:   Stateless utility functions for token extraction, rule matching,
//            and 7-Zip process execution.
//==============================================================================

#pragma once

#include "Release_PackageTypes.h"
#include <string>
#include <vector>
#include <map>

namespace RPLogic {

std::wstring BaseName(const std::wstring& path);

std::map<std::wstring, std::wstring> ExtractTokens(
    const std::wstring& archivePath,
    const std::vector<RPParam>& params);

std::wstring ApplyTokens(
    const std::wstring& tmpl,
    const std::map<std::wstring, std::wstring>& toks,
    const std::wstring& fname,
    const std::wstring& ext,
    const std::wstring& dirname);

bool GlobMatch(const std::wstring& fname, const std::wstring& pattern);

std::vector<std::pair<std::wstring, int>> ListArchive(
    const std::wstring& archive,
    const std::wstring& sevenZip);

std::vector<RPMatchResult> MatchFiles(
    const std::vector<std::pair<std::wstring, int>>& archiveFiles,
    const std::map<std::wstring, std::wstring>& tokens,
    const std::vector<RPRule>& rules);

std::wstring RunProcess(const std::wstring& exe, const std::wstring& args);

bool ExtractFiles(
    const std::wstring& archivePath,
    const std::wstring& sevenZip,
    const std::vector<RPMatchResult>& matches,
    const std::wstring& outputDir);

std::wstring NormalizePath(const std::wstring& s);

} // namespace RPLogic