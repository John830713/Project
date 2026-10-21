//==============================================================================
// UpdateManager.h - Single-instance guard + in-place executable update
//
// Windows blocks writing/deleting a running image but ALLOWS renaming it.
// That is what makes a zero-downtime update possible: rename the registered
// executable aside (the running process keeps using the renamed file), copy
// the newer build into the original path, and let the swap take effect on
// the next launch. The running instance — and any RemoteControl session —
// is never interrupted.
//==============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace UpdateManager {

// Translates an English key (section "Update") into the active UI language.
// Injected by the caller so this module has no dependency on services that
// are not initialized yet at startup time.
using NoticeTranslator = std::function<std::wstring(const std::wstring&)>;

// Reads a file's 4-part version (a.b.c.d) packed as a.b.c.d in one integer.
// Returns 0 when the file carries no VERSIONINFO resource — such builds are
// treated as the oldest possible version so any versioned build can replace
// them.
std::uint64_t ReadFileVersion(const std::wstring& path);

// Formats a packed version as "a.b.c.d".
std::wstring VersionToString(std::uint64_t version);

// True when `candidate` is strictly newer than `current`.
bool IsNewer(std::uint64_t candidate, std::uint64_t current);

// Builds the backup name for a swap: <dir>\<exe>.<YYYYMMDDHHMMSS>.old
std::wstring MakeBackupName(const std::wstring& target);

// Swaps `source` over `target`:
//   1. rename target -> backup          (allowed while target is running)
//   2. copy source -> target
//   3. if the copy fails, rename backup back (rollback) and report the error
// When target does not exist yet, only the copy is performed.
bool SwapExecutable(const std::wstring& source,
                    const std::wstring& target,
                    const std::wstring& backup,
                    std::wstring* errorOut);

// Deletes "<dir>\<exe>.<...>.old" backups left by previous swaps. Files still
// locked by a running old image are skipped (DeleteFile fails) and retried on
// the next launch. Returns the number of files deleted.
int RemoveStaleBackups(const std::wstring& modulePath);

// Must be the first thing called in wWinMain.
//
// - No other instance running: publishes this instance (named mutex),
//   cleans stale backups and returns true — run the app normally.
// - Another instance running: compares this build against the peer's
//   registered path. Newer -> in-place swap (running instance untouched);
//   same/older -> a notice window. Both paths show a notice that auto-closes
//   after two seconds and return false — wWinMain exits without starting a
//   second copy.
//
// `waitForHandoff` is set by a relaunch we performed ourselves: the previous
// instance is still winding down, so the mutex (and its peer) may briefly
// still exist. In that mode we retry acquiring the mutex for a few seconds and
// start normally once the old instance lets go, instead of refusing.
bool BeginSession(const NoticeTranslator& tr, bool waitForHandoff = false);

//------------------------------------------------------------------------------
// Relaunch handoff (named event Local\<exe base>_Reload)
//
// The double-clicked newer build cannot restart the running instance directly.
// After a successful swap it calls SignalReload(); the running instance polls
// PollReloadRequest() and, on a signal, relaunches itself from the registered
// path (now the new build) and quits. The spawned process runs with
// --reload-wait so BeginSession waits for the old instance to release the mutex.
//------------------------------------------------------------------------------

// Name of the handoff event for the given executable path.
std::wstring ReloadEventName(const std::wstring& exePath);

// (Clicked copy) Ask the running instance to relaunch. `exePath` is the
// registered path to relaunch (the freshly swapped build). Auto-resets on the
// next poll. Returns false if the event could not be created/signalled.
bool SignalReload(const std::wstring& exePath);

// (Running instance) Non-blocking. When a reload has been requested, returns
// true once and fills `exePathOut` with the path to relaunch.
bool PollReloadRequest(std::wstring* exePathOut);

// Spawns `exePath` with the reload-wait flag and returns true on success.
bool SpawnReloadedInstance(const std::wstring& exePath);

// True when this process was started with the reload-wait flag. Set once from
// wWinMain's command line.
void SetReloadWaitRequested(bool requested);
bool IsReloadWaitRequested();

// Shows an auto-closing (2s) topmost notice window with `text` under the
// given caption. Blocks the calling thread in its own message loop — safe
// from any thread (used by RemoteControl's deploy channel on recv threads),
// but the caller is paused for the notice duration.
void ShowNoticeText(const std::wstring& text, const std::wstring& caption);

}  // namespace UpdateManager
