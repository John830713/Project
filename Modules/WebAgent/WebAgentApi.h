//==============================================================================
// WebAgentApi.h - Route handlers for the Web Agent HTTP surface
// Purpose:   Owns the shared server state (sessions, command queue, reports)
//            and registers every route onto an HttpServer. The JSON shapes are
//            kept byte-compatible with the original Python Flask server so the
//            existing Tampermonkey core.js / modules need no changes.
//
// Protocol source of truth: D:\Tampermonkey\resources\tools\local\server.py
//==============================================================================

#pragma once

#include "HttpServer.h"
#include "WebAgentTypes.h"

#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace webagent {

// Per-browser-tab state, mirroring the Python `sessions[sid]` dict.
struct SessionInfo {
    std::string url;
    std::string title;
    std::string state = "IDLE";
    std::string agent = "?";
    bool tracking = false;
    bool tagged = false;
    double lastSeen = 0.0; // seconds since epoch
};

// A queued manual command. `json` is the serialized command object; `filter`
// is the optional `_session` value ("" == no filter, deliver to any session).
struct QueuedCommand {
    std::string json;
    std::string filter;
    bool hasFilter = false;
};

// A stored report (POST /report or the merged body of POST /poll).
struct ReportEntry {
    std::string session;
    std::string cmd;
    std::string json; // includes the injected "time" field
};

class WebAgentApi {
public:
    WebAgentApi();

    // Register all routes onto `server`. Called once at module start.
    void RegisterRoutes(HttpServer& server);

    // Root directory that supplies JS modules (the old D:\Tampermonkey tree).
    void SetResourceRoot(const std::string& root) { m_resourceRoot = root; }
    const std::string& GetResourceRoot() const { return m_resourceRoot; }

    // Base directory for .agent/browser artifacts (dump / hidden selectors).
    void SetAgentDir(const std::string& dir) { m_agentDir = dir; }

    // Poll interval hints echoed to the browser (milliseconds).
    void SetPollIntervals(int busy, int idle) { m_pollBusy = busy; m_pollIdle = idle; }

    void SetShowConsole(bool on) { m_showConsole = on; }

    static constexpr int kDefaultPort = 8921;

private:
    //--- Route handlers ---
    HttpResponse HandlePing(const HttpRequest& req);
    HttpResponse HandleHello(const HttpRequest& req);
    HttpResponse HandleTag(const HttpRequest& req);
    HttpResponse HandlePoll(const HttpRequest& req);
    HttpResponse HandleReport(const HttpRequest& req);
    HttpResponse HandleCommand(const HttpRequest& req);
    HttpResponse HandleCommands(const HttpRequest& req);
    HttpResponse HandleStatus(const HttpRequest& req);
    HttpResponse HandleMetrics(const HttpRequest& req);
    HttpResponse HandleQueue(const HttpRequest& req);
    HttpResponse HandleReports(const HttpRequest& req);
    HttpResponse HandleResults(const HttpRequest& req);
    HttpResponse HandleTasks(const HttpRequest& req);
    HttpResponse HandleTaskStart(const HttpRequest& req, const std::string& name);
    HttpResponse HandleTaskStop(const HttpRequest& req);
    HttpResponse HandleHidden(const HttpRequest& req);
    HttpResponse HandleDump(const HttpRequest& req);

    //--- Static file serving (P1) ---
    HttpResponse HandleServeFile(const std::string& relPath);
    HttpResponse HandleServeRoot(const std::string& tail);
    HttpResponse HandleModulesJson();
    HttpResponse HandleDashboard();

    //--- Helpers ---
    std::string JsonEscape(const std::string& in) const;
    void CleanupSessionsLocked();
    void PurgeCommandsLocked(const std::vector<std::string>& deadSessions);
    bool IsTrackingUrl(const std::string& url) const;

    // Append a parsed report (adds "time"). Assumes lock held.
    void AppendReportLocked(const std::string& body);

    // Deliver a matching command to `sid`, or return false if none available.
    // `out` receives the command JSON augmented with tagged + poll_ms.
    bool PopCommandForSessionLocked(const std::string& sid, std::string& out);

    // Read `<root>/<rel>`; looks under m_resourceRoot then m_resourceRoot/modules.
    bool TryReadJs(const std::string& rel, std::string& out, std::string& resolvedPath);
    static std::string ReadFileOrEmpty(const std::string& path);

    std::string m_resourceRoot;
    std::string m_agentDir;
    int m_pollBusy = 300;
    int m_pollIdle = 1200;
    bool m_showConsole = false;

    std::mutex m_mutex;
    std::map<std::string, SessionInfo> m_sessions;
    std::deque<QueuedCommand> m_queue;
    std::deque<QueuedCommand> m_pendingFiltered; // commands awaiting their session
    std::deque<ReportEntry> m_reports;           // ring buffer, max 500

    std::chrono::steady_clock::time_point m_startTime;

    static constexpr int kSessionTtlSec = 300;
    static constexpr int kMaxQueue = 100;
    static constexpr size_t kMaxReports = 500;
};

} // namespace webagent
