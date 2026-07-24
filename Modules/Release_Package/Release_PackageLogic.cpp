//==============================================================================
// Release_PackageLogic.cpp - Release_Package pure logic implementation
//==============================================================================

#include "Release_PackageLogic.h"
#include <windows.h>
#include <regex>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace RPLogic {

std::wstring NormalizePath(const std::wstring& s) {
    std::wstring r = s;
    std::replace(r.begin(), r.end(), L'\\', L'/');
    return r;
}

std::wstring BaseName(const std::wstring& path) {
    auto p = path.find_last_of(L"/\\");
    std::wstring n = (p != std::wstring::npos) ? path.substr(p + 1) : path;
    auto d = n.find_last_of(L'.');
    return (d != std::wstring::npos) ? n.substr(0, d) : n;
}

std::wstring RunProcess(const std::wstring& exe, const std::wstring& args) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return L"";
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    std::wstring wcmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, &wcmd[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);
    if (!ok) { CloseHandle(hRead); return L""; }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    std::wstring output;
    wchar_t buf[4096];
    DWORD n;
    while (ReadFile(hRead, buf, sizeof(buf) - sizeof(wchar_t), &n, nullptr) && n > 0) {
        buf[n / sizeof(wchar_t)] = 0;
        output += buf;
    }
    CloseHandle(hRead);
    return output;
}

std::vector<std::pair<std::wstring, int>> ListArchive(
    const std::wstring& archive,
    const std::wstring& sevenZip)
{
    std::wstring output = RunProcess(sevenZip, L"l -slt \"" + archive + L"\"");
    std::vector<std::pair<std::wstring, int>> files;
    std::wstring curPath;
    int curSize = 0;
    std::wistringstream iss(output);
    std::wstring line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty()) {
            if (!curPath.empty()) { files.push_back({curPath, curSize}); curPath.clear(); curSize = 0; }
            continue;
        }
        if (line.rfind(L"Path = ", 0) == 0) curPath = line.substr(7);
        else if (line.rfind(L"Size = ", 0) == 0) {
            try { curSize = std::stoi(line.substr(7)); } catch (...) {}
        }
    }
    if (!curPath.empty()) files.push_back({curPath, curSize});
    return files;
}

std::map<std::wstring, std::wstring> ExtractTokens(
    const std::wstring& archivePath,
    const std::vector<RPParam>& params)
{
    std::wstring base = BaseName(archivePath);
    std::map<std::wstring, std::wstring> result;
    for (auto& p : params) {
        if (p.name.empty()) continue;
        if (!p.value.empty()) { result[p.name] = p.value; continue; }
        if (p.regex.empty()) continue;
        try {
            std::wsmatch m;
            if (std::regex_search(base, m, std::wregex(p.regex)))
                result[p.name] = (m.size() > 1) ? m[1].str() : m[0].str();
            else
                result[p.name] = L"";
        } catch (...) { result[p.name] = L""; }
    }
    return result;
}

std::wstring ApplyTokens(
    const std::wstring& tmpl,
    const std::map<std::wstring, std::wstring>& toks,
    const std::wstring& fname,
    const std::wstring& ext,
    const std::wstring& dirname)
{
    std::wstring s = tmpl;
    auto rep = [&](const std::wstring& k, const std::wstring& v) {
        std::wstring ph = L"{" + k + L"}";
        size_t pos;
        while ((pos = s.find(ph)) != std::wstring::npos) s.replace(pos, ph.size(), v);
    };
    rep(L"filename", fname); rep(L"ext", ext); rep(L"dirname", dirname);
    for (auto& [k, v] : toks) rep(k, v);
    return s;
}

bool GlobMatch(const std::wstring& fname, const std::wstring& pattern) {
    if (pattern == L"*") return true;
    if (pattern.find(L'*') == std::wstring::npos && pattern.find(L'?') == std::wstring::npos)
        return fname == pattern;
    std::wstring re = L"^";
    for (wchar_t c : pattern) {
        if (c == L'.') re += L"\\.";
        else if (c == L'*') re += L".*";
        else if (c == L'?') re += L".";
        else re += c;
    }
    re += L"$";
    try { return std::regex_match(fname, std::wregex(re)); }
    catch (...) { return fname == pattern; }
}

std::vector<RPMatchResult> MatchFiles(
    const std::vector<std::pair<std::wstring, int>>& archiveFiles,
    const std::map<std::wstring, std::wstring>& tokens,
    const std::vector<RPRule>& rules)
{
    std::vector<RPMatchResult> results;
    for (int ri = 0; ri < (int)rules.size(); ri++) {
        auto& rule = rules[ri];
        if (!rule.enabled) continue;
        std::wstring srcDir = NormalizePath(rule.sourceDir);
        std::wstring destDir = rule.destDir;

        for (auto& [fpath, fsize] : archiveFiles) {
            std::wstring norm = NormalizePath(fpath);
            std::wstring rel;
            if (!srcDir.empty()) {
                std::wstring prefix = srcDir + L"/";
                if (norm == srcDir) continue;
                else if (norm.size() >= prefix.size() && norm.compare(0, prefix.size(), prefix) == 0)
                    rel = norm.substr(prefix.size());
                else continue;
            } else rel = norm;
            if (rel.empty()) continue;

            std::wstring fname = rel;
            auto sl = fname.find_last_of(L'/');
            if (sl != std::wstring::npos) fname = fname.substr(sl + 1);

            if (!GlobMatch(fname, rule.pattern)) continue;

            std::wstring nameNoExt = fname, ext;
            auto dot = fname.find_last_of(L'.');
            if (dot != std::wstring::npos) { nameNoExt = fname.substr(0, dot); ext = fname.substr(dot); }

            std::wstring dirBase;
            if (!srcDir.empty()) {
                auto s2 = srcDir.find_last_of(L'/');
                dirBase = (s2 != std::wstring::npos) ? srcDir.substr(s2 + 1) : srcDir;
            }
            std::wstring newName = ApplyTokens(rule.rename, tokens, nameNoExt, ext, dirBase);
            std::wstring relDir;
            auto rs = rel.find_last_of(L'/');
            if (rs != std::wstring::npos) relDir = rel.substr(0, rs);

            std::wstring dst;
            if (!relDir.empty()) dst = destDir + L"\\" + relDir + L"\\" + newName;
            else dst = destDir + L"\\" + newName;
            std::replace(dst.begin(), dst.end(), L'/', L'\\');
            results.push_back({norm, dst, ri});
        }
    }
    return results;
}

bool ExtractFiles(
    const std::wstring& archivePath,
    const std::wstring& sevenZip,
    const std::vector<RPMatchResult>& matches,
    const std::wstring& outputDir)
{
    wchar_t tmpPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpPath);
    std::wstring tmpDir = std::wstring(tmpPath) + L"7zext_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(tmpDir.c_str(), nullptr);
    std::wstring filelistPath = tmpDir + L"\\_filelist.txt";
    {
        std::wofstream fl(filelistPath.c_str());
        for (auto& m : matches) fl << m.originalPath << L"\n";
    }

    std::wstring extractArgs = L"x \"" + archivePath + L"\" -o\"" + tmpDir + L"\" @\"" + filelistPath + L"\" -y";
    RunProcess(sevenZip, extractArgs);

    int moved = 0;
    for (auto& m : matches) {
        std::wstring srcW = tmpDir + L"\\" + m.originalPath;
        if (!fs::exists(srcW)) {
            std::wstring alt = m.originalPath;
            std::replace(alt.begin(), alt.end(), L'/', L'\\');
            srcW = tmpDir + L"\\" + alt;
        }
        if (!fs::exists(srcW)) continue;
        std::wstring dstW = m.destPath;
        fs::create_directories(fs::path(dstW).parent_path());
        try {
            fs::rename(srcW, dstW);
            moved++;
        } catch (...) {
            try { fs::copy_file(srcW, dstW, fs::copy_options::overwrite_existing); fs::remove(srcW); moved++; }
            catch (...) {}
        }
    }

    std::error_code ec;
    fs::remove_all(tmpDir, ec);
    return moved > 0;
}

} // namespace RPLogic