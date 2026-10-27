//==============================================================================
// WebAgentApi.cpp - Route handlers for the Web Agent HTTP surface
//
// The JSON request/response shapes here are deliberately identical to the
// original Python Flask server (D:\Tampermonkey\resources\tools\local\server.py)
// so the existing browser core.js and station modules need no changes.
//==============================================================================

#include "WebAgentApi.h"

#include "../../Core/Logger.h"
#include "../Release_Package/json.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using nlohmann::json;

namespace webagent {

namespace {
constexpr const wchar_t* kLogSource = L"WebAgent";
const char* kJsContentType = "application/x-javascript; charset=utf-8";

// Tracking/ad domains whose sessions never receive task commands.
const char* kTrackingDomains[] = {
    "doubleclick.net", "googleadservices.com", "googlesyndication.com",
    "google-analytics.com", "googletagmanager.com", "facebook.com/tr",
    "adsrvr.org", "adservice.google.com",
    "pubmatic.com", "inmobi.com", "criteo.com", "rubiconproject.com",
    "openx.net", "adnxs.com", "indexww.com", "id5-sync.com",
    "sharethrough.com", "casalemedia.com", "turn.com", "bidswitch.net",
    "adsymptotic.com", "onaudience.com", "smartadserver.com",
};

double NowSeconds() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

bool PathIsSafe(const std::string& rel) {
    if (rel.empty()) return false;
    if (rel.find("..") != std::string::npos) return false;
    if (rel[0] == '/' || rel[0] == '\\') return false;
    if (rel.find(':') != std::string::npos) return false;
    return true;
}

std::string JoinPath(const std::string& base, const std::string& rel) {
    if (base.empty()) return rel;
    char last = base.back();
    if (last == '/' || last == '\\') return base + rel;
    return base + "/" + rel;
}

// Extract the host portion of a URL ("https://a.b:80/x" -> "a.b").
std::string UrlHost(const std::string& url) {
    size_t scheme = url.find("://");
    size_t start = (scheme == std::string::npos) ? 0 : scheme + 3;
    size_t end = url.find_first_of("/?#", start);
    std::string host = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        host = host.substr(0, colon);
    }
    return host;
}

std::string Base64Decode(const std::string& in) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int lut[256];
    for (int& v : lut) v = -1;
    for (size_t i = 0; i < chars.size(); ++i) {
        lut[static_cast<unsigned char>(chars[i])] = static_cast<int>(i);
    }
    std::string out;
    int val = 0, bits = 0;
    for (char c : in) {
        if (c == '=') break;
        int d = lut[static_cast<unsigned char>(c)];
        if (d < 0) continue;
        val = (val << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((val >> bits) & 0xFF));
        }
    }
    return out;
}

std::string AugmentCommand(const std::string& raw, bool tagged, int pollMs) {
    json j = json::parse(raw, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        j = json::object();
    }
    j["tagged"] = tagged;
    j["poll_ms"] = pollMs;
    return j.dump();
}

} // namespace

WebAgentApi::WebAgentApi() {
    m_startTime = std::chrono::steady_clock::now();
}

std::string WebAgentApi::JsonEscape(const std::string& in) const {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

//==============================================================================
// Route registration
//==============================================================================

void WebAgentApi::RegisterRoutes(HttpServer& server) {
    server.AddRoute("GET", "/ping", [this](const HttpRequest& r) { return HandlePing(r); });
    server.AddRoute("POST", "/ping", [this](const HttpRequest& r) { return HandlePing(r); });

    server.AddRoute("POST", "/hello", [this](const HttpRequest& r) { return HandleHello(r); });
    server.AddRoute("POST", "/tag", [this](const HttpRequest& r) { return HandleTag(r); });
    server.AddRoute("GET", "/poll", [this](const HttpRequest& r) { return HandlePoll(r); });
    server.AddRoute("POST", "/poll", [this](const HttpRequest& r) { return HandlePoll(r); });
    server.AddRoute("POST", "/report", [this](const HttpRequest& r) { return HandleReport(r); });

    server.AddRoute("POST", "/command", [this](const HttpRequest& r) { return HandleCommand(r); });
    server.AddRoute("POST", "/commands", [this](const HttpRequest& r) { return HandleCommands(r); });

    server.AddRoute("GET", "/status", [this](const HttpRequest& r) { return HandleStatus(r); });
    server.AddRoute("GET", "/metrics", [this](const HttpRequest& r) { return HandleMetrics(r); });
    server.AddRoute("GET", "/queue", [this](const HttpRequest& r) { return HandleQueue(r); });
    server.AddRoute("GET", "/reports", [this](const HttpRequest& r) { return HandleReports(r); });
    server.AddRoute("GET", "/results", [this](const HttpRequest& r) { return HandleResults(r); });

    server.AddRoute("GET", "/tasks", [this](const HttpRequest& r) { return HandleTasks(r); });
    server.AddRoute("POST", "/task/stop", [this](const HttpRequest& r) { return HandleTaskStop(r); });
    server.AddRoute("POST", "/task*", [this](const HttpRequest& r, const std::string& tail) {
        return HandleTaskStart(r, tail);
    });

    server.AddRoute("GET", "/hidden", [this](const HttpRequest& r) { return HandleHidden(r); });
    server.AddRoute("POST", "/hidden", [this](const HttpRequest& r) { return HandleHidden(r); });
    server.AddRoute("DELETE", "/hidden", [this](const HttpRequest& r) { return HandleHidden(r); });
    server.AddRoute("GET", "/dump", [this](const HttpRequest& r) { return HandleDump(r); });
    server.AddRoute("POST", "/dump", [this](const HttpRequest& r) { return HandleDump(r); });

    //--- Static JS surface (paths mirror the Flask server) ---
    server.AddRoute("GET", "/agent.core.js", [this](const HttpRequest&) {
        return HandleServeFile("agent/core.js");
    });
    server.AddRoute("GET", "/loader-core.js", [this](const HttpRequest&) {
        return HandleServeFile("resources/tools/local/loader-core.js");
    });
    server.AddRoute("GET", "/universal.loader.user.js", [this](const HttpRequest&) {
        return HandleServeFile("agent/universal.loader.user.js");
    });
    server.AddRoute("GET", "/agent.user.js", [this](const HttpRequest&) {
        return HandleServeFile("agent/standalone.user.js");
    });
    server.AddRoute("GET", "/agent.loader.user.js", [this](const HttpRequest&) {
        return HandleServeFile("agent/loader.user.js");
    });
    server.AddRoute("GET", "/modules", [this](const HttpRequest&) {
        return HandleModulesJson();
    });
    server.AddRoute("GET", "/serve*", [this](const HttpRequest&, const std::string& tail) {
        return HandleServeRoot(tail);
    });

    server.AddRoute("GET", "/", [this](const HttpRequest&) { return HandleDashboard(); });
    server.AddRoute("GET", "/dashboard", [this](const HttpRequest&) { return HandleDashboard(); });

    Logger::WriteModuleLog(kLogSource, L"WebAgentApi: routes registered (P0+P1+P2).");
}

//==============================================================================
// Utility
//==============================================================================

bool WebAgentApi::IsTrackingUrl(const std::string& url) const {
    if (url.size() > 500) {
        return true;
    }
    std::string host = UrlHost(url);
    for (const char* td : kTrackingDomains) {
        if (host.find(td) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void WebAgentApi::CleanupSessionsLocked() {
    double now = NowSeconds();
    std::vector<std::string> expired;
    for (const auto& kv : m_sessions) {
        if (now - kv.second.lastSeen > kSessionTtlSec) {
            expired.push_back(kv.first);
        }
    }
    for (const auto& sid : expired) {
        m_sessions.erase(sid);
    }
    if (!expired.empty()) {
        PurgeCommandsLocked(expired);
    }
}

void WebAgentApi::PurgeCommandsLocked(const std::vector<std::string>& deadSessions) {
    if (deadSessions.empty()) {
        return;
    }
    auto isDead = [&](const QueuedCommand& q) {
        if (!q.hasFilter) return false;
        return std::find(deadSessions.begin(), deadSessions.end(), q.filter) != deadSessions.end();
    };
    m_pendingFiltered.erase(
        std::remove_if(m_pendingFiltered.begin(), m_pendingFiltered.end(), isDead),
        m_pendingFiltered.end());
    m_queue.erase(
        std::remove_if(m_queue.begin(), m_queue.end(), isDead),
        m_queue.end());
}

void WebAgentApi::AppendReportLocked(const std::string& body) {
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return;
    }
    j["time"] = NowSeconds();
    ReportEntry entry;
    entry.session = j.value("session", "");
    entry.cmd = j.value("cmd", "");
    entry.json = j.dump();
    m_reports.push_back(std::move(entry));
    while (m_reports.size() > kMaxReports) {
        m_reports.pop_front();
    }
}

bool WebAgentApi::PopCommandForSessionLocked(const std::string& sid, std::string& out) {
    bool tagged = false;
    {
        auto it = m_sessions.find(sid);
        if (it != m_sessions.end()) {
            tagged = it->second.tagged;
        }
    }

    // 1) Pending filtered commands (matched in place, others preserved).
    for (size_t i = 0; i < m_pendingFiltered.size(); ++i) {
        const QueuedCommand& q = m_pendingFiltered[i];
        if (!q.hasFilter || q.filter == sid) {
            out = AugmentCommand(q.json, tagged, m_pollBusy);
            m_pendingFiltered.erase(m_pendingFiltered.begin() + static_cast<long>(i));
            return true;
        }
    }

    // 2) Main queue: examine at most 100 entries, parking non-matching filtered
    //    commands into the pending list (matches Python's attempts < 100).
    int attempts = 0;
    while (attempts < 100 && !m_queue.empty()) {
        QueuedCommand q = std::move(m_queue.front());
        m_queue.pop_front();
        if (!q.hasFilter || q.filter == sid) {
            out = AugmentCommand(q.json, tagged, m_pollBusy);
            return true;
        }
        m_pendingFiltered.push_back(std::move(q));
        ++attempts;
    }
    return false;
}

//==============================================================================
// Core protocol handlers
//==============================================================================

HttpResponse WebAgentApi::HandlePing(const HttpRequest& req) {
    (void)req;
    return HttpResponse::Json(200, "{\"ok\":true,\"service\":\"webagent-cpp\",\"pong\":true}");
}

HttpResponse WebAgentApi::HandleHello(const HttpRequest& req) {
    json body = json::parse(req.body, nullptr, false);
    if (!body.is_object()) {
        return HttpResponse::BadRequest("invalid json");
    }

    std::string sid = body.value("session", "?");
    std::string url = body.value("url", "");

    std::lock_guard<std::mutex> lock(m_mutex);

    // Remove old sessions for the same URL (page reload / new tab takeover).
    std::vector<std::string> stale;
    for (const auto& kv : m_sessions) {
        if (kv.first != sid && kv.second.url == url && !kv.second.tracking) {
            stale.push_back(kv.first);
        }
    }
    for (const auto& old : stale) {
        m_sessions.erase(old);
    }
    PurgeCommandsLocked(stale);

    SessionInfo s;
    s.url = url;
    s.title = body.value("title", "");
    s.lastSeen = NowSeconds();
    s.state = "IDLE";
    s.agent = body.value("agentVersion", "?");
    s.tracking = IsTrackingUrl(url);
    m_sessions[sid] = s;

    Logger::WriteModuleLog(kLogSource,
        L"[hello] " + std::wstring(sid.begin(), sid.end()));
    return HttpResponse::Json(200, "{\"ok\":true}");
}

HttpResponse WebAgentApi::HandleTag(const HttpRequest& req) {
    json body = json::parse(req.body, nullptr, false);
    if (!body.is_object()) {
        return HttpResponse::BadRequest("invalid json");
    }
    std::string sid = body.value("session", "");
    bool tagged = body.value("tagged", true);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sessions.find(sid);
        if (it != m_sessions.end()) {
            it->second.tagged = tagged;
        }
    }
    return HttpResponse::NoContent();
}

HttpResponse WebAgentApi::HandlePoll(const HttpRequest& req) {
    std::string sid = req.GetQuery("session");
    if (sid.empty()) sid = "?";
    std::string url = req.GetQuery("url");

    std::lock_guard<std::mutex> lock(m_mutex);

    // Accept a merged report from the browser (POST body).
    if (req.method == "POST" && !req.body.empty()) {
        json body = json::parse(req.body, nullptr, false);
        if (body.is_object() && body.contains("cmd")) {
            AppendReportLocked(req.body);
        }
    }

    // Session registration / refresh.
    auto it = m_sessions.find(sid);
    if (it == m_sessions.end()) {
        SessionInfo s;
        s.url = url;
        s.lastSeen = NowSeconds();
        s.state = "IDLE";
        s.agent = "?";
        s.tracking = IsTrackingUrl(url);
        m_sessions[sid] = s;
    } else {
        it->second.lastSeen = NowSeconds();
        if (!url.empty()) {
            it->second.url = url;
        }
        it->second.tracking = IsTrackingUrl(it->second.url);
    }

    bool tagged = m_sessions[sid].tagged;

    // 1) Active task: none in v1 (task engine deferred).
    // 2) Tracking sessions never receive manual commands.
    auto buildSimple = [&](bool includePollHint) {
        json out = json::object();
        out["tagged"] = tagged;
        if (includePollHint) {
            out["poll_ms"] = m_pollBusy;
        }
        return out.dump();
    };

    if (!m_sessions[sid].tracking) {
        std::string cmd;
        if (PopCommandForSessionLocked(sid, cmd)) {
            return HttpResponse::Json(200, cmd);
        }
    } else {
        return HttpResponse::Json(200, buildSimple(false));
    }

    // Periodic cleanup (Python does this when int(time) % 30 == 0).
    if (static_cast<long long>(NowSeconds()) % 30 == 0) {
        CleanupSessionsLocked();
    }

    // Signal a fast poll if work remains for other sessions.
    bool hasWork = !m_queue.empty() || !m_pendingFiltered.empty();
    return HttpResponse::Json(200, buildSimple(hasWork));
}

HttpResponse WebAgentApi::HandleReport(const HttpRequest& req) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        AppendReportLocked(req.body);
    }
    return HttpResponse::NoContent();
}

HttpResponse WebAgentApi::HandleCommand(const HttpRequest& req) {
    json body = json::parse(req.body, nullptr, false);
    if (!body.is_object()) {
        return HttpResponse::BadRequest("invalid json");
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (static_cast<int>(m_queue.size()) >= kMaxQueue) {
        json err = {{"error", "queue full"}, {"queue_size", m_queue.size()}};
        return HttpResponse::Json(429, err.dump());
    }

    QueuedCommand q;
    q.hasFilter = body.contains("_session") && !body["_session"].is_null();
    if (q.hasFilter) {
        q.filter = body["_session"].is_string() ? body["_session"].get<std::string>()
                                                : body["_session"].dump();
        body.erase("_session");
    }
    q.json = body.dump();
    m_queue.push_back(std::move(q));

    json out = {{"ok", true}, {"queue_size", m_queue.size()}};
    return HttpResponse::Json(200, out.dump());
}

HttpResponse WebAgentApi::HandleCommands(const HttpRequest& req) {
    json body = json::parse(req.body, nullptr, false);
    if (!body.is_array()) {
        return HttpResponse::BadRequest("expected array");
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (static_cast<int>(m_queue.size()) + static_cast<int>(body.size()) > kMaxQueue) {
        json err = {
            {"error", "queue overflow"},
            {"queue_size", m_queue.size()},
            {"pushed", body.size()},
        };
        return HttpResponse::Json(429, err.dump());
    }

    int pushed = 0;
    for (auto& item : body) {
        if (!item.is_object()) continue;
        QueuedCommand q;
        q.hasFilter = item.contains("_session") && !item["_session"].is_null();
        if (q.hasFilter) {
            q.filter = item["_session"].is_string() ? item["_session"].get<std::string>()
                                                    : item["_session"].dump();
            item.erase("_session");
        }
        q.json = item.dump();
        m_queue.push_back(std::move(q));
        ++pushed;
    }

    json out = {{"ok", true}, {"pushed", pushed}, {"queue_size", m_queue.size()}};
    return HttpResponse::Json(200, out.dump());
}

//==============================================================================
// Status / queries
//==============================================================================

HttpResponse WebAgentApi::HandleStatus(const HttpRequest& req) {
    (void)req;
    std::lock_guard<std::mutex> lock(m_mutex);
    CleanupSessionsLocked();

    json sessions = json::object();
    for (const auto& kv : m_sessions) {
        const SessionInfo& s = kv.second;
        sessions[kv.first] = {
            {"url", s.url},
            {"title", s.title},
            {"state", s.state},
            {"agent", s.agent},
            {"tracking", s.tracking},
            {"tagged", s.tagged},
        };
    }

    json lastReport = nullptr;
    if (!m_reports.empty()) {
        json parsed = json::parse(m_reports.back().json, nullptr, false);
        if (!parsed.is_discarded()) {
            lastReport = std::move(parsed);
        }
    }

    json out = {
        {"sessions", sessions},
        {"queue_size", m_queue.size()},
        {"reports_count", m_reports.size()},
        {"last_report", lastReport},
        {"task", nullptr},
        {"tasks_available", json::array()},
    };
    return HttpResponse::Json(200, out.dump());
}

HttpResponse WebAgentApi::HandleMetrics(const HttpRequest& req) {
    (void)req;
    std::lock_guard<std::mutex> lock(m_mutex);
    double now = NowSeconds();
    int active = 0;
    int tracking = 0;
    for (const auto& kv : m_sessions) {
        if (now - kv.second.lastSeen < 60.0) ++active;
        if (kv.second.tracking) ++tracking;
    }
    auto elapsed = std::chrono::steady_clock::now() - m_startTime;
    long long uptime = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

    json out = {
        {"uptime", uptime},
        {"sessions_total", m_sessions.size()},
        {"sessions_active_60s", active},
        {"sessions_tracking", tracking},
        {"queue_size", m_queue.size()},
        {"pending_filtered", m_pendingFiltered.size()},
        {"reports_count", m_reports.size()},
        {"task_active", false},
    };
    return HttpResponse::Json(200, out.dump());
}

HttpResponse WebAgentApi::HandleQueue(const HttpRequest& req) {
    (void)req;
    std::lock_guard<std::mutex> lock(m_mutex);
    json out = {
        {"queue_size", m_queue.size()},
        {"pending_filtered", m_pendingFiltered.size()},
        {"sessions", m_sessions.size()},
        {"task", nullptr},
    };
    return HttpResponse::Json(200, out.dump());
}

HttpResponse WebAgentApi::HandleReports(const HttpRequest& req) {
    int limit = 50;
    std::string limitStr = req.GetQuery("limit");
    if (!limitStr.empty()) {
        try { limit = std::stoi(limitStr); } catch (...) { limit = 50; }
    }
    if (limit < 0) limit = 0;
    bool drain = req.GetQuery("drain") == "1";
    std::string sess = req.GetQuery("session");
    std::string cmdFilter = req.GetQuery("cmd");

    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<const ReportEntry*> filtered;
    filtered.reserve(m_reports.size());
    for (const auto& r : m_reports) {
        if (!sess.empty() && r.session != sess) continue;
        if (!cmdFilter.empty() && r.cmd != cmdFilter) continue;
        filtered.push_back(&r);
    }

    size_t start = filtered.size() > static_cast<size_t>(limit)
                       ? filtered.size() - static_cast<size_t>(limit)
                       : 0;
    json arr = json::array();
    for (size_t i = start; i < filtered.size(); ++i) {
        json item = json::parse(filtered[i]->json, nullptr, false);
        if (!item.is_discarded()) {
            arr.push_back(std::move(item));
        }
    }

    if (drain) {
        m_reports.clear();
    }
    return HttpResponse::Json(200, arr.dump());
}

HttpResponse WebAgentApi::HandleResults(const HttpRequest& req) {
    int limit = 20;
    std::string limitStr = req.GetQuery("limit");
    if (!limitStr.empty()) {
        try { limit = std::stoi(limitStr); } catch (...) { limit = 20; }
    }

    std::string path = JoinPath(m_resourceRoot, "task_results.jsonl");
    std::ifstream f(path);
    json entries = json::array();
    if (f.is_open()) {
        std::string line;
        while (std::getline(f, line)) {
            // trim CR
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            if (line.empty()) continue;
            json item = json::parse(line, nullptr, false);
            if (!item.is_discarded()) {
                entries.push_back(std::move(item));
            }
        }
    }

    if (limit >= 0 && static_cast<int>(entries.size()) > limit) {
        json tail = json::array();
        size_t start = entries.size() - static_cast<size_t>(limit);
        for (size_t i = start; i < entries.size(); ++i) {
            tail.push_back(entries[i]);
        }
        entries = std::move(tail);
    }
    return HttpResponse::Json(200, entries.dump());
}

HttpResponse WebAgentApi::HandleTasks(const HttpRequest& req) {
    (void)req;
    return HttpResponse::Json(200, "{\"available\":[]}");
}

HttpResponse WebAgentApi::HandleTaskStart(const HttpRequest& req, const std::string& tail) {
    (void)req;
    std::string name = tail;
    if (!name.empty() && name[0] == '/') name.erase(0, 1);
    json out = {
        {"error", "Task \"" + name + "\" not found"},
        {"available", ""},
    };
    return HttpResponse::Json(404, out.dump());
}

HttpResponse WebAgentApi::HandleTaskStop(const HttpRequest& req) {
    (void)req;
    return HttpResponse::Json(200, "{\"ok\":true}");
}

//==============================================================================
// Dump / hidden selectors
//==============================================================================

HttpResponse WebAgentApi::HandleDump(const HttpRequest& req) {
    std::string browserDir = JoinPath(m_agentDir, "browser");

    if (req.method == "GET") {
        std::string path = JoinPath(browserDir, "element_dump.json");
        std::string content = ReadFileOrEmpty(path);
        if (content.empty()) {
            return HttpResponse::Json(404, "{\"error\":\"no dump yet\"}");
        }
        return HttpResponse::Json(200, content);
    }

    json data = json::parse(req.body, nullptr, false);
    if (!data.is_object()) {
        return HttpResponse::BadRequest("invalid json");
    }
    data["dumped_at"] = NowSeconds();

    std::error_code ec;
    std::filesystem::create_directories(browserDir, ec);

    // Extract and persist a screenshot (base64 data URL) separately.
    if (data.contains("screenshot") && data["screenshot"].is_string()) {
        std::string shot = data["screenshot"].get<std::string>();
        if (shot.rfind("data:image", 0) == 0) {
            size_t comma = shot.find(',');
            if (comma != std::string::npos) {
                std::string b64 = shot.substr(comma + 1);
                std::string png = Base64Decode(b64);
                std::string shotPath = JoinPath(browserDir, "element_dump_screenshot.png");
                std::ofstream out(shotPath, std::ios::binary);
                out.write(png.data(), static_cast<std::streamsize>(png.size()));
                out.close();
                data["screenshot_file"] = shotPath;
            }
        }
        data.erase("screenshot");
    }

    std::string dumpPath = JoinPath(browserDir, "element_dump.json");
    std::ofstream out(dumpPath, std::ios::binary);
    out << data.dump(2);
    out.close();

    json resp = {{"ok", true}, {"path", dumpPath}};
    return HttpResponse::Json(200, resp.dump());
}

HttpResponse WebAgentApi::HandleHidden(const HttpRequest& req) {
    std::string browserDir = JoinPath(m_agentDir, "browser");
    std::string hiddenPath = JoinPath(browserDir, "hidden_selectors.json");

    if (req.method == "GET") {
        std::string content = ReadFileOrEmpty(hiddenPath);
        if (content.empty()) {
            return HttpResponse::Json(200, "{\"selectors\":[],\"urls\":[]}");
        }
        return HttpResponse::Json(200, content);
    }

    json current = {{"selectors", json::array()}, {"urls", json::array()}};
    {
        std::string existing = ReadFileOrEmpty(hiddenPath);
        if (!existing.empty()) {
            json parsed = json::parse(existing, nullptr, false);
            if (parsed.is_object()) {
                current = parsed;
            }
        }
    }
    if (!current.contains("selectors") || !current["selectors"].is_array()) {
        current["selectors"] = json::array();
    }
    if (!current.contains("urls") || !current["urls"].is_array()) {
        current["urls"] = json::array();
    }

    if (req.method == "DELETE") {
        json body = json::parse(req.body.empty() ? "{}" : req.body, nullptr, false);
        std::string url = body.is_object() ? body.value("url", "") : "";
        if (!url.empty()) {
            json keptUrls = json::array();
            for (const auto& u : current["urls"]) {
                if (u.is_string() && u.get<std::string>() != url) {
                    keptUrls.push_back(u);
                }
            }
            current["urls"] = keptUrls;
        } else {
            current = {{"selectors", json::array()}, {"urls", json::array()}};
        }
    } else { // POST
        json body = json::parse(req.body, nullptr, false);
        if (!body.is_object()) {
            return HttpResponse::BadRequest("invalid json");
        }
        if (body.contains("selectors") && body["selectors"].is_array()) {
            for (const auto& s : body["selectors"]) {
                if (!s.is_string()) continue;
                bool exists = false;
                for (const auto& existing : current["selectors"]) {
                    if (existing.is_string() && existing.get<std::string>() == s.get<std::string>()) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    current["selectors"].push_back(s);
                }
            }
        }
        std::string url = body.value("url", "");
        if (!url.empty()) {
            bool exists = false;
            for (const auto& u : current["urls"]) {
                if (u.is_string() && u.get<std::string>() == url) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                current["urls"].push_back(url);
            }
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(browserDir, ec);
    std::ofstream out(hiddenPath, std::ios::binary);
    out << current.dump(2);
    out.close();

    json resp = {{"ok", true}, {"count", current["selectors"].size()}};
    return HttpResponse::Json(200, resp.dump());
}

//==============================================================================
// Static file serving (P1)
//==============================================================================

std::string WebAgentApi::ReadFileOrEmpty(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        return "";
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool WebAgentApi::TryReadJs(const std::string& rel, std::string& out, std::string& resolvedPath) {
    if (!PathIsSafe(rel)) {
        return false;
    }
    std::string candidates[2] = {
        JoinPath(m_resourceRoot, rel),
        JoinPath(JoinPath(m_resourceRoot, "modules"), rel),
    };
    for (const std::string& cand : candidates) {
        std::string content = ReadFileOrEmpty(cand);
        if (!content.empty()) {
            out = content;
            resolvedPath = cand;
            return true;
        }
    }
    return false;
}

HttpResponse WebAgentApi::HandleServeFile(const std::string& relPath) {
    if (m_resourceRoot.empty()) {
        return HttpResponse::Json(404, "{\"error\":\"resource root not configured\"}");
    }
    std::string full = JoinPath(m_resourceRoot, relPath);
    std::string content = ReadFileOrEmpty(full);
    if (content.empty()) {
        return HttpResponse::Json(404, "{\"error\":\"not found\"}");
    }
    content.push_back('\n');
    HttpResponse resp = HttpResponse::Text(200, content, kJsContentType);
    resp.extraHeaders.push_back({"Cache-Control", "no-cache, no-store, must-revalidate"});
    return resp;
}

HttpResponse WebAgentApi::HandleServeRoot(const std::string& tail) {
    std::string name = tail;
    if (!name.empty() && (name[0] == '/' || name[0] == '\\')) {
        name.erase(0, 1);
    }
    if (!PathIsSafe(name)) {
        return HttpResponse::Json(400, "{\"error\":\"invalid name\"}");
    }
    std::string content, resolved;
    if (!TryReadJs(name, content, resolved)) {
        return HttpResponse::Json(404, "{\"error\":\"not found\"}");
    }
    return HttpResponse::Text(200, content, kJsContentType);
}

HttpResponse WebAgentApi::HandleDashboard() {
    static const char* kHtml = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<title>Web Agent</title>
<style>
*{box-sizing:border-box}
body{font-family:system-ui,sans-serif;max-width:900px;margin:0 auto;padding:20px;background:#1e1e2e;color:#cdd6f4}
h1{font-size:1.3rem;margin:0 0 16px;color:#89b4fa}
.card{background:#313244;border-radius:8px;padding:14px;margin:12px 0}
.card h2{font-size:.9rem;margin:0 0 10px;color:#a6adc8;text-transform:uppercase;letter-spacing:.05em}
.tt{font-family:ui-monospace,monospace;font-size:.8rem}
.badge{padding:1px 7px;border-radius:9px;font-size:.72rem;font-weight:bold}
.g{background:#a6e3a1;color:#1e1e2e}.b{background:#89b4fa;color:#1e1e2e}.r{background:#f38ba8;color:#1e1e2e}
input,select,button{font:inherit;padding:6px 8px;border-radius:5px;border:1px solid #585b70;background:#1e1e2e;color:#cdd6f4}
button{cursor:pointer;background:#89b4fa;color:#1e1e2e;border:none;font-weight:600}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:6px 0}
#log{background:#11111b;padding:10px;border-radius:5px;font:12px/1.5 ui-monospace,monospace;height:170px;overflow-y:auto;white-space:pre-wrap;color:#a6e3a1}
.sess{margin:3px 0;font-size:.85rem}
.mono{color:#f9e2af}
</style></head><body>
<h1>Web Agent Control</h1>
<div class="card"><h2>Sessions</h2><div id="sessions">loading...</div></div>
<div class="card"><h2>Queue / Metrics</h2><div id="queue" class="tt">loading...</div></div>
<div class="card"><h2>Send Command</h2>
  <div class="row">
    <select id="cmd">
      <option>ping</option><option>navigate</option><option>wait</option>
      <option>find</option><option>find_and_click</option><option>click</option>
      <option>get_text</option><option>get_attr</option><option>eval</option>
      <option>inject_ui</option><option>set_config</option><option>scroll_into_view</option>
      <option>type</option>
    </select>
    <input id="params" value="{}" placeholder="JSON params" style="flex:2">
    <select id="target"><option value="">(any session)</option></select>
    <button onclick="sendCmd()">Send</button>
  </div>
</div>
<div class="card"><h2>Reports</h2><div id="reports" class="tt">loading...</div></div>
<div class="card"><h2>Log</h2><div id="log"></div></div>
<script>
function log(m){var e=document.getElementById('log');e.textContent+='['+new Date().toLocaleTimeString()+'] '+m+'\n';e.scrollTop=e.scrollHeight}
function get(p){return fetch(p).then(function(r){return r.json()})}
function post(p,b){return fetch(p,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b||{})}).then(function(r){return r.json()})}
var knownSessions={};
function refresh(){
  get('/status').then(function(d){
    var s=document.getElementById('sessions');var ids=Object.keys(d.sessions);
    if(!ids.length){s.innerHTML='<span class="tt">No sessions connected.</span>'}
    else{
      s.innerHTML=ids.map(function(k){var v=d.sessions[k];
        return '<div class="sess"><span class="badge '+(v.tracking?'r':'b')+'">'+(v.tracking?'TRACKING':v.state)+'</span> '+
          '<span class="mono">'+k.slice(0,10)+'</span> '+(v.url||'')+'</div>';}).join('');
    }
    document.getElementById('queue').textContent=
      'queue_size='+d.queue_size+'  reports='+d.reports_count+'  task='+(d.task&&d.task.name?d.task.name:'none');
    var sel=document.getElementById('target');
    ids.forEach(function(k){ if(!knownSessions[k]){knownSessions[k]=1;var o=document.createElement('option');o.value=k;o.textContent=k;sel.appendChild(o);} });
  }).catch(function(){});
  get('/metrics').then(function(m){
    document.getElementById('queue').textContent=
      'sessions='+m.sessions_total+' (active '+m.sessions_active_60s+', tracking '+m.sessions_tracking+')  '+
      'queue='+m.queue_size+'  pending='+m.pending_filtered+'  reports='+m.reports_count+'  uptime='+m.uptime+'s';
  }).catch(function(){});
}
function refreshReports(){
  get('/reports?limit=15').then(function(a){
    if(!a||!a.length){document.getElementById('reports').innerHTML='<span class="tt">No reports yet.</span>';return;}
    document.getElementById('reports').innerHTML=a.slice().reverse().map(function(r){
      var c=(r.result==='OK'||r.result==='PONG'||r.result==='CLICKED')?'g':'r';
      return '<div class="sess"><span class="badge '+c+'">'+r.cmd+'</span> '+r.result+' <span style="color:#6c7086">'+(r.url||'')+'</span></div>';
    }).join('');
  }).catch(function(){});
}
function sendCmd(){
  var p={};try{p=JSON.parse(document.getElementById('params').value||'{}')}catch(e){log('bad JSON');return}
  p.cmd=document.getElementById('cmd').value;
  var t=document.getElementById('target').value;if(t)p._session=t;
  post('/command',p).then(function(r){log('queued '+p.cmd+' (n='+(r.queue_size!==undefined?r.queue_size:'?')+')')}).catch(function(e){log('err '+e)});
}
refresh();refreshReports();setInterval(refresh,3000);setInterval(refreshReports,4000);
</script>
</body></html>)HTML";
    return HttpResponse::Text(200, kHtml, "text/html; charset=utf-8");
}

HttpResponse WebAgentApi::HandleModulesJson() {
    std::string path = JoinPath(JoinPath(m_resourceRoot, "modules"), "modules.json");
    std::string raw = ReadFileOrEmpty(path);
    if (raw.empty()) {
        return HttpResponse::Json(200, "[]");
    }

    json data = json::parse(raw, nullptr, false);
    if (data.is_discarded()) {
        return HttpResponse::Json(500, "{\"error\":\"invalid modules.json\"}");
    }
    if (data.is_array()) {
        for (auto& m : data) {
            if (!m.is_object() || !m.contains("script") || !m["script"].is_string()) {
                continue;
            }
            std::string scriptName = m["script"].get<std::string>();
            if (scriptName.empty()) continue;

            std::string content, resolved;
            if (!TryReadJs(scriptName, content, resolved)) {
                continue;
            }
            if (content.size() > 2000) {
                content.resize(2000);
            }
            size_t at = content.find("@version");
            if (at == std::string::npos) continue;
            size_t start = at + 8; // strlen("@version")
            while (start < content.size() && (content[start] == ' ' || content[start] == '\t')) {
                ++start;
            }
            size_t end = content.find_first_of("\r\n", start);
            std::string ver = content.substr(
                start, end == std::string::npos ? std::string::npos : end - start);
            while (!ver.empty() && (ver.back() == ' ' || ver.back() == '\t')) {
                ver.pop_back();
            }
            m["version"] = ver;
        }
    }
    return HttpResponse::Json(200, data.dump());
}

} // namespace webagent
