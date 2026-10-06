//==============================================================================
// UpdateManagerTest.cpp - In-place executable update test
// Purpose:   Covers the unit-testable core of Core/UpdateManager:
//              - VERSIONINFO version reading and ordering (builds without a
//                resource must compare as 0.0.0.0 so any versioned build can
//                replace them)
//              - the rename-and-swap sequence, including rollback when the
//                copy fails (the running executable may only be RENAMED, never
//                written - the whole update scheme rests on that)
//              - stale *.old cleanup: matching files are removed, a file
//                locked by a running old image is skipped and removed later
//==============================================================================

#include "Core/UpdateManager.h"

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_pass = 0;
int g_fail = 0;

void Check(bool ok, const char* name) {
    if (ok) { g_pass++; std::printf("  [PASS] %s\n", name); }
    else    { g_fail++; std::printf("  [FAIL] %s\n", name); }
}

uint64_t Pack(unsigned a, unsigned b, unsigned c, unsigned d) {
    return (static_cast<uint64_t>(a) << 48) | (static_cast<uint64_t>(b) << 32) |
           (static_cast<uint64_t>(c) << 16) | static_cast<uint64_t>(d);
}

// Test workspace under %TEMP%\UpdateManagerTest, wiped at start and end so
// runs are idempotent.
std::wstring g_dir;

std::wstring TempPath(const std::wstring& name) {
    return g_dir + L"\\" + name;
}

void WriteTestFile(const std::wstring& path, const std::string& content) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written,
              nullptr);
    CloseHandle(h);
}

std::string ReadTestFile(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";
    std::string out;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0) {
        out.append(buf, got);
    }
    CloseHandle(h);
    return out;
}

void ClearDir() {
    WIN32_FIND_DATAW data{};
    HANDLE h = FindFirstFileW((g_dir + L"\\*").c_str(), &data);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(data.cFileName, L".") == 0 ||
                wcscmp(data.cFileName, L"..") == 0) {
                continue;
            }
            DeleteFileW((g_dir + L"\\" + data.cFileName).c_str());
        } while (FindNextFileW(h, &data));
        FindClose(h);
    }
}

//------------------------------------------------------------------------------
// Version reading / ordering
//------------------------------------------------------------------------------

void TestVersionReading() {
    std::printf("\n-- Version reading --\n");

    Check(UpdateManager::ReadFileVersion(TempPath(L"does-not-exist.exe")) == 0,
          "missing file reports version 0");

    const std::wstring plain = TempPath(L"plain.txt");
    WriteTestFile(plain, "no version resource here");
    Check(UpdateManager::ReadFileVersion(plain) == 0,
          "file without VERSIONINFO reports version 0 (old builds are updatable)");
    DeleteFileW(plain.c_str());

    // Project.exe carries the auto-generated resource; optional (a fresh
    // checkout may not have a main build yet).
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring self(exe);
    std::wstring project = self.substr(0, self.find_last_of(L"\\/") + 1) +
                           L"Project.exe";
    if (GetFileAttributesW(project.c_str()) != INVALID_FILE_ATTRIBUTES) {
        uint64_t v = UpdateManager::ReadFileVersion(project);
        Check(v != 0, "Project.exe VERSIONINFO is readable");
        Check(UpdateManager::VersionToString(v).find(L".") !=
                  std::wstring::npos,
              "version formats as a.b.c.d");
        std::printf("       (Project.exe = %ls)\n",
                    UpdateManager::VersionToString(v).c_str());
    } else {
        std::printf("  [SKIP] Project.exe not built, skipping resource check\n");
    }
}

void TestVersionOrdering() {
    std::printf("\n-- Version ordering --\n");

    const uint64_t oldBuild = Pack(2026, 1006, 1800, 0);
    const uint64_t newBuild = Pack(2026, 1006, 1830, 0);

    Check(UpdateManager::IsNewer(newBuild, oldBuild),
          "later timestamp is newer");
    Check(!UpdateManager::IsNewer(oldBuild, newBuild),
          "earlier timestamp is not newer");
    Check(!UpdateManager::IsNewer(newBuild, newBuild),
          "equal versions are not newer");
    Check(UpdateManager::IsNewer(oldBuild, 0),
          "any versioned build is newer than a build with no resource");
    Check(!UpdateManager::IsNewer(0, 0),
          "two resource-less builds are not newer than each other");
    Check(UpdateManager::VersionToString(Pack(2026, 1006, 1818, 1900)) ==
              L"2026.1006.1818.1900",
          "VersionToString renders the packed components");
    Check(UpdateManager::VersionToString(0) == L"0.0.0.0",
          "VersionToString renders zero");
}

//------------------------------------------------------------------------------
// Rename-and-swap
//------------------------------------------------------------------------------

void TestSwapSuccess() {
    std::printf("\n-- Swap: success --\n");

    const std::wstring target = TempPath(L"app.exe");
    const std::wstring source = TempPath(L"app.new.exe");
    const std::wstring backup = UpdateManager::MakeBackupName(target);

    WriteTestFile(target, "OLD");
    WriteTestFile(source, "NEW");

    std::wstring error;
    bool ok = UpdateManager::SwapExecutable(source, target, backup, &error);

    Check(ok, "swap succeeds");
    Check(error.empty(), "no error reported on success");
    Check(ReadTestFile(target) == "NEW", "target now holds the new build");
    Check(GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES,
          "backup exists at the generated name");
    Check(ReadTestFile(backup) == "OLD", "backup holds the original content");

    DeleteFileW(target.c_str());
    DeleteFileW(source.c_str());
    DeleteFileW(backup.c_str());
}

void TestSwapRollback() {
    std::printf("\n-- Swap: copy failure rolls the rename back --\n");

    const std::wstring target = TempPath(L"app.exe");
    const std::wstring missing = TempPath(L"no-such-source.exe");
    const std::wstring backup = UpdateManager::MakeBackupName(target);

    WriteTestFile(target, "OLD");

    std::wstring error;
    bool ok = UpdateManager::SwapExecutable(missing, target, backup, &error);

    Check(!ok, "swap fails when the source cannot be copied");
    Check(!error.empty(), "failure reports an error message");
    Check(ReadTestFile(target) == "OLD", "target still holds the original build");
    Check(GetFileAttributesW(backup.c_str()) == INVALID_FILE_ATTRIBUTES,
          "no backup is left behind after rollback");

    DeleteFileW(target.c_str());
}

void TestSwapTargetMissing() {
    std::printf("\n-- Swap: nothing at the registered path --\n");

    const std::wstring target = TempPath(L"never-installed.exe");
    const std::wstring source = TempPath(L"app.new.exe");
    const std::wstring backup = UpdateManager::MakeBackupName(target);

    WriteTestFile(source, "NEW");

    std::wstring error;
    bool ok = UpdateManager::SwapExecutable(source, target, backup, &error);

    Check(ok, "swap copies straight into place");
    Check(ReadTestFile(target) == "NEW", "target holds the new build");
    Check(GetFileAttributesW(backup.c_str()) == INVALID_FILE_ATTRIBUTES,
          "no backup is created when there was nothing to rename");

    DeleteFileW(target.c_str());
    DeleteFileW(source.c_str());
}

void TestBackupName() {
    std::printf("\n-- Backup naming --\n");

    const std::wstring target = TempPath(L"app.exe");
    const std::wstring backup = UpdateManager::MakeBackupName(target);

    Check(backup.rfind(target + L".", 0) == 0,
          "backup lives next to the target with the exe name as prefix");
    Check(backup.size() >= 5 &&
              backup.compare(backup.size() - 4, 4, L".old") == 0,
          "backup ends with .old");
    // <target>.<YYYYMMDDHHMMSS>.old -> 14 digits between the last two dots
    size_t lastDot = backup.find_last_of(L'.');
    size_t prevDot = backup.find_last_of(L'.', lastDot - 1);
    bool digits = (lastDot != std::wstring::npos &&
                   prevDot != std::wstring::npos &&
                   lastDot - prevDot == 15);
    if (digits) {
        for (size_t i = prevDot + 1; i < lastDot; ++i) {
            if (backup[i] < L'0' || backup[i] > L'9') digits = false;
        }
    }
    Check(digits, "backup embeds a 14-digit timestamp");
}

//------------------------------------------------------------------------------
// Stale backup cleanup
//------------------------------------------------------------------------------

void TestRemoveStaleBackups() {
    std::printf("\n-- Stale backup cleanup --\n");

    const std::wstring modulePath = TempPath(L"AnyName.exe");
    const std::wstring staleA = TempPath(L"AnyName.exe.20260101000000.old");
    const std::wstring staleB = TempPath(L"AnyName.exe.20260101000001.old");
    const std::wstring foreign = TempPath(L"Other.exe.20260101000000.old");
    const std::wstring locked = TempPath(L"AnyName.exe.20260101000002.old");

    WriteTestFile(staleA, "a");
    WriteTestFile(staleB, "b");
    WriteTestFile(foreign, "x");
    WriteTestFile(locked, "locked");

    // Hold the locked file without FILE_SHARE_DELETE: DeleteFile fails, which
    // is exactly the state while an old image still runs from its backup.
    HANDLE hLock = CreateFileW(locked.c_str(), GENERIC_READ, 0, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    int removed = UpdateManager::RemoveStaleBackups(modulePath);

    Check(removed == 2, "both unlocked matching backups removed");
    Check(GetFileAttributesW(staleA.c_str()) == INVALID_FILE_ATTRIBUTES &&
              GetFileAttributesW(staleB.c_str()) == INVALID_FILE_ATTRIBUTES,
          "stale backups are gone");
    Check(GetFileAttributesW(foreign.c_str()) != INVALID_FILE_ATTRIBUTES,
          "a backup of a different exe name is left alone");
    Check(GetFileAttributesW(locked.c_str()) != INVALID_FILE_ATTRIBUTES,
          "locked backup (running old image) is skipped, not force-deleted");

    if (hLock != INVALID_HANDLE_VALUE) CloseHandle(hLock);

    int second = UpdateManager::RemoveStaleBackups(modulePath);
    Check(second == 1 && GetFileAttributesW(locked.c_str()) == INVALID_FILE_ATTRIBUTES,
          "locked backup is removed on the retry after it unlocks");

    DeleteFileW(foreign.c_str());
}

}  // namespace

//==============================================================================

int main() {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    g_dir = std::wstring(tmp) + L"UpdateManagerTest";
    CreateDirectoryW(g_dir.c_str(), nullptr);
    ClearDir();

    TestVersionReading();
    TestVersionOrdering();
    TestBackupName();
    TestSwapSuccess();
    TestSwapRollback();
    TestSwapTargetMissing();
    TestRemoveStaleBackups();

    ClearDir();
    RemoveDirectoryW(g_dir.c_str());

    std::printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
