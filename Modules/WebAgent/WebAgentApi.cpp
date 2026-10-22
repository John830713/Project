//==============================================================================
// WebAgentApi.cpp - Route handler registration + P0 implementations
//==============================================================================

#include "WebAgentApi.h"

#include "../../Core/Logger.h"

#include <cstdio>
#include <sstream>

namespace webagent {

namespace {
constexpr const wchar_t* kLogSource = L"WebAgent";

double NowSeconds() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
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
    Logger::WriteModuleLog(kLogSource, L"WebAgentApi: routes registered (P0 subset).");
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
