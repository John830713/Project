//==============================================================================
// WebAgentApi.cpp - Route handler registration + P0 implementations
//==============================================================================

#include "WebAgentApi.h"

#include "../../Core/Logger.h"
#include "../Release_Package/json.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace webagent {

namespace {
constexpr const wchar_t* kLogSource = L"WebAgent";

const char* kJsContentType = "application/x-javascript; charset=utf-8";

double NowSeconds() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

// Reject any path containing ".." to prevent escaping the resource root.
bool PathIsSafe(const std::string& rel) {
    if (rel.empty()) return false;
    if (rel.find("..") != std::string::npos) return false;
    if (!rel.empty() && (rel[0] == '/' || rel[0] == '\\')) return false;
    if (rel.find(':') != std::string::npos) return false;
    return true;
}

std::string JoinPath(const std::string& base, const std::string& rel) {
    if (base.empty()) return rel;
    char last = base.back();
    if (last == '/' || last == '\\') return base + rel;
    return base + "/" + rel;
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

void WebAgentApi::RegisterRoutes(HttpServer& server) {
    server.AddRoute("GET", "/ping", [this](const HttpRequest& r) { return HandlePing(r); });
    server.AddRoute("POST", "/ping", [this](const HttpRequest& r) { return HandlePing(r); });
    server.AddRoute("GET", "/status", [this](const HttpRequest& r) { return HandleStatus(r); });

    //--- P1: static JS surface (paths mirror the Flask server) ---
    server.AddRoute("GET", "/agent.core.js", [this](const HttpRequest&) {
        return HandleServeFile("agent/core.js");
    });
    server.AddRoute("GET", "/loader-core.js", [this](const HttpRequest&) {
        return HandleServeFile("resources/tools/loader-core.js");
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

    Logger::WriteModuleLog(kLogSource, L"WebAgentApi: routes registered (P0+P1).");
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
    // Try the resource root, then its modules/ subdirectory (Flask's /serve does
    // exactly this two-step lookup).
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
    // Flask appends a trailing newline for core.js / loader-core.js; harmless for
    // the others and keeps the byte stream identical to the Python server.
    content.push_back('\n');
    HttpResponse resp = HttpResponse::Text(200, content, kJsContentType);
    // Never cache the core scripts: they update live.
    resp.extraHeaders.push_back({"Cache-Control", "no-cache, no-store, must-revalidate"});
    return resp;
}

HttpResponse WebAgentApi::HandleServeRoot(const std::string& tail) {
    // tail arrives as "/<name>" (the route is "/serve*"). Strip the leading '/'.
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

HttpResponse WebAgentApi::HandleModulesJson() {
    std::string path = JoinPath(JoinPath(m_resourceRoot, "modules"), "modules.json");
    std::string raw = ReadFileOrEmpty(path);
    if (raw.empty()) {
        // Flask returns [] when the file is missing.
        return HttpResponse::Json(200, "[]");
    }

    // Inject each module's @version (parsed from the script's first 2000 bytes)
    // so the dashboard / loader can display versions, exactly like the Python
    // server. Uses nlohmann::json for correctness.
    try {
        auto data = nlohmann::json::parse(raw, nullptr, false);
        if (data.is_discarded()) {
            return HttpResponse::Json(500, "{\"error\":\"invalid modules.json\"}");
        }
        if (data.is_array()) {
            for (auto& m : data) {
                if (!m.is_object() || !m.contains("script") || !m["script"].is_string()) {
                    continue;
                }
                std::string scriptName = m["script"].get<std::string>();
                if (scriptName.empty()) {
                    continue;
                }
                std::string content, resolved;
                if (TryReadJs(scriptName, content, resolved)) {
                    if (content.size() > 2000) {
                        content.resize(2000);
                    }
                    // Find "@version <text>" to end of line.
                    size_t at = content.find("@version");
                    if (at != std::string::npos) {
                        size_t start = at + 8; // strlen("@version")
                        while (start < content.size() &&
                               (content[start] == ' ' || content[start] == '\t')) {
                            ++start;
                        }
                        size_t end = content.find_first_of("\r\n", start);
                        std::string ver = content.substr(start, end == std::string::npos
                                                                    ? std::string::npos
                                                                    : end - start);
                        // trim trailing spaces
                        while (!ver.empty() && (ver.back() == ' ' || ver.back() == '\t')) {
                            ver.pop_back();
                        }
                        m["version"] = ver;
                    }
                }
            }
        }
        return HttpResponse::Json(200, data.dump());
    } catch (const std::exception& e) {
        return HttpResponse::Json(500, std::string("{\"error\":\"") + JsonEscape(e.what()) + "\"}");
    }
}

HttpResponse WebAgentApi::HandlePing(const HttpRequest& req) {
    (void)req;
    return HttpResponse::Json(200, "{\"ok\":true,\"service\":\"webagent-cpp\",\"pong\":true}");
}

HttpResponse WebAgentApi::HandleStatus(const HttpRequest& req) {
    (void)req;

    std::lock_guard<std::mutex> lock(m_mutex);
    CleanupSessionsLocked();

    std::ostringstream out;
    out << "{";
    out << "\"sessions\":{";
    bool first = true;
    for (const auto& kv : m_sessions) {
        if (!first) out << ",";
        first = false;
        const SessionInfo& s = kv.second;
        out << "\"" << JsonEscape(kv.first) << "\":{";
        out << "\"url\":\"" << JsonEscape(s.url) << "\",";
        out << "\"title\":\"" << JsonEscape(s.title) << "\",";
        out << "\"state\":\"" << JsonEscape(s.state) << "\",";
        out << "\"agent\":\"" << JsonEscape(s.agent) << "\",";
        out << "\"tracking\":" << (s.tracking ? "true" : "false") << ",";
        out << "\"tagged\":" << (s.tagged ? "true" : "false");
        out << "}";
    }
    out << "},";
    out << "\"queue_size\":" << m_queue.size() << ",";
    out << "\"reports_count\":" << m_reports.size() << ",";
    out << "\"last_report\":" << (m_reports.empty() ? "null" : m_reports.back()) << ",";
    out << "\"task\":null,";
    out << "\"tasks_available\":[]";
    out << "}";

    return HttpResponse::Json(200, out.str());
}

SessionInfo& WebAgentApi::TouchSession(const std::string& sid, const std::string& url) {
    SessionInfo& s = m_sessions[sid];
    if (s.lastSeen == 0.0) {
        s.agent = "?";
        s.state = "IDLE";
    }
    s.lastSeen = NowSeconds();
    if (!url.empty()) {
        s.url = url;
    }
    return s;
}

void WebAgentApi::CleanupSessionsLocked() {
    double now = NowSeconds();
    for (auto it = m_sessions.begin(); it != m_sessions.end();) {
        if (now - it->second.lastSeen > kSessionTtlSec) {
            it = m_sessions.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace webagent
