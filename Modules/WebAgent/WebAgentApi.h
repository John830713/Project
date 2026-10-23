//==============================================================================
// WebAgentApi.h - Route handlers for the Web Agent HTTP surface
// Purpose:   Owns the shared server state (sessions, command queue, reports)
//            and registers every route onto an HttpServer. The JSON shapes are
//            kept byte-compatible with the original Python Flask server so the
//            existing Tampermonkey core.js / modules need no changes.
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

// A queued manual command + its optional session filter.
struct QueuedCommand {
    std::string json;
    std::string sessionFilter; // empty == no filter
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

private:
    //--- Individual route handlers ---
    HttpResponse HandlePing(const HttpRequest& req);
    HttpResponse HandleStatus(const HttpRequest& req);

    //--- Static file serving (P1) ---
    HttpResponse HandleServeFile(const std::string& relPath);        // /agent.core.js etc.
    HttpResponse HandleServeRoot(const std::string& tail);           // /serve/<name>
    HttpResponse HandleModulesJson();                                // /modules

    //--- Helpers ---
    std::string JsonEscape(const std::string& in) const;
    SessionInfo& TouchSession(const std::string& sid, const std::string& url);
    void CleanupSessionsLocked();

    // Read `<root>/<rel>` and return a JS response; the file is looked up under
    // m_resourceRoot then m_resourceRoot/modules (mirrors Flask's /serve).
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
    std::deque<std::string> m_reports; // ring buffer, JSON strings, max 500

    std::chrono::steady_clock::time_point m_startTime;

    static constexpr int kSessionTtlSec = 300;
    static constexpr int kMaxQueue = 100;
    static constexpr size_t kMaxReports = 500;

public:
    static constexpr int kDefaultPort = 8921;
};

} // namespace webagent
