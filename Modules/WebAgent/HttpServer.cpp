//==============================================================================
// HttpServer.cpp - Minimal HTTP/1.1 server implementation
//==============================================================================

#include "HttpServer.h"

#include "../../Services/NetworkService.h"
#include "../../Core/Logger.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace webagent {

namespace {

constexpr const wchar_t* kLogSource = L"WebAgent";
constexpr int kRecvTimeoutMs = 5000;
constexpr size_t kMaxRequestBytes = 8 * 1024 * 1024;

const char* StatusText(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        default:  return "OK";
    }
}

} // namespace

bool ReadFileToString(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

HttpServer::HttpServer() = default;

HttpServer::~HttpServer() {
    Stop();
}

void HttpServer::AddRoute(const std::string& method, const std::string& path,
                          HttpHandler handler) {
    std::lock_guard<std::mutex> lock(m_routeMutex);
    m_routes[method + " " + path].exact = std::move(handler);
}

void HttpServer::AddRoute(const std::string& method, const std::string& path,
                          std::function<HttpResponse(const HttpRequest&, const std::string&)> handler) {
    std::lock_guard<std::mutex> lock(m_routeMutex);
    m_routes[method + " " + path].prefix = std::move(handler);
}

bool HttpServer::Start(const std::string& host, int port) {
    if (m_running.load()) {
        return true;
    }

    if (!NetworkService::Initialize()) {
        Logger::WriteModuleLog(kLogSource, L"HttpServer: WSAStartup failed.");
        return false;
    }

    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        Logger::WriteModuleLog(kLogSource, L"HttpServer: socket() failed.");
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        addr.sin_addr.s_addr = inet_addr(host.c_str());
    }

    if (bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        Logger::WriteModuleLog(kLogSource,
            L"HttpServer: bind failed on port " + std::to_wstring(port) + L".");
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }

    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        Logger::WriteModuleLog(kLogSource, L"HttpServer: listen() failed.");
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }

    m_port = port;
    m_running.store(true);
    m_thread = std::thread(&HttpServer::ListenLoop, this);

    Logger::WriteModuleLog(kLogSource,
        L"HttpServer listening on " + std::wstring(host.begin(), host.end()) +
        L":" + std::to_wstring(port) + L".");
    return true;
}

void HttpServer::Stop() {
    if (!m_running.exchange(false)) {
        // Still join a lingering thread if any.
        if (m_thread.joinable()) {
            m_thread.join();
        }
        return;
    }

    // Close the listener to break the blocking accept(); the loop then exits.
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    Logger::WriteModuleLog(kLogSource, L"HttpServer stopped.");
}

void HttpServer::ListenLoop() {
    while (m_running.load()) {
        sockaddr_in clientAddr = {};
        int addrLen = sizeof(clientAddr);
        SOCKET client = accept(m_listenSocket,
                               reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (client == INVALID_SOCKET) {
            if (!m_running.load()) {
                break; // socket closed by Stop()
            }
            continue;
        }
        // Parse + respond inline; short-lived connection.
        NetworkService::ApplyTimeouts(client, kRecvTimeoutMs);
        HandleConnection(client);
        shutdown(client, SD_BOTH);
        closesocket(client);
    }
}

bool HttpServer::ParseRequest(const std::string& raw, size_t headerEnd, HttpRequest& out) {
    std::string headers = raw.substr(0, headerEnd);
    std::istringstream stream(headers);
    std::string line;

    if (!std::getline(stream, line)) {
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    // Request line: METHOD TARGET VERSION
    std::istringstream reqLine(line);
    std::string target;
    if (!(reqLine >> out.method >> target >> out.version)) {
        return false;
    }

    size_t q = target.find('?');
    if (q == std::string::npos) {
        out.path = UrlDecode(target);
    } else {
        out.path = UrlDecode(target.substr(0, q));
        out.query = target.substr(q + 1);
    }

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        // trim leading spaces in value
        size_t start = value.find_first_not_of(" \t");
        value = (start == std::string::npos) ? "" : value.substr(start);
        for (char& c : key) {
            c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        }
        out.headers[key] = value;
    }
    return true;
}

void HttpServer::HandleConnection(SOCKET client) {
    std::string buffer;
    buffer.reserve(8192);
    char chunk[4096];
    size_t headerEnd = std::string::npos;
    size_t contentLength = 0;

    while (true) {
        int n = recv(client, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            break;
        }
        buffer.append(chunk, static_cast<size_t>(n));
        if (buffer.size() > kMaxRequestBytes) {
            break;
        }

        if (headerEnd == std::string::npos) {
            headerEnd = buffer.find("\r\n\r\n");
            if (headerEnd == std::string::npos) {
                continue; // headers not complete yet
            }
            // Determine Content-Length so we can read the full body.
            std::string lower = buffer.substr(0, headerEnd);
            for (char& c : lower) {
                c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
            }
            size_t cl = lower.find("content-length:");
            if (cl != std::string::npos) {
                size_t nl = lower.find("\r\n", cl);
                std::string num = lower.substr(cl + 15, nl - (cl + 15));
                contentLength = static_cast<size_t>(strtoul(num.c_str(), nullptr, 10));
            }
        }

        if (headerEnd != std::string::npos) {
            size_t bodyStart = headerEnd + 4;
            size_t have = buffer.size() - bodyStart;
            if (have >= contentLength) {
                break; // full request received
            }
        }
    }

    if (headerEnd == std::string::npos) {
        return; // never got valid headers
    }

    HttpRequest req;
    if (!ParseRequest(buffer, headerEnd, req)) {
        std::string resp = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
        return;
    }

    size_t bodyStart = headerEnd + 4;
    if (bodyStart < buffer.size()) {
        req.body = buffer.substr(bodyStart, contentLength);
    }

    HttpResponse r = Dispatch(req);

    std::string statusText = StatusText(r.status);
    std::ostringstream out;
    out << "HTTP/1.1 " << r.status << " " << statusText << "\r\n";
    out << "Content-Type: " << r.contentType << "\r\n";
    out << "Content-Length: " << r.body.size() << "\r\n";
    out << "Access-Control-Allow-Origin: *\r\n";
    out << "Access-Control-Allow-Methods: GET, POST, OPTIONS, DELETE\r\n";
    out << "Access-Control-Allow-Headers: Content-Type\r\n";
    for (const auto& h : r.extraHeaders) {
        out << h.first << ": " << h.second << "\r\n";
    }
    out << "Connection: close\r\n\r\n";
    std::string head = out.str();

    NetworkService::Send(client, head.data(), static_cast<int>(head.size()));
    if (!r.body.empty()) {
        NetworkService::Send(client, r.body.data(), static_cast<int>(r.body.size()));
    }
}

HttpResponse HttpServer::Dispatch(const HttpRequest& req) {
    // CORS preflight: answer OPTIONS for any path immediately.
    if (req.method == "OPTIONS") {
        return HttpResponse::NoContent();
    }

    RouteEntry entry;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(m_routeMutex);
        auto it = m_routes.find(req.method + " " + req.path);
        if (it != m_routes.end()) {
            entry = it->second;
            found = true;
        }
    }

    if (found && entry.exact) {
        return entry.exact(req);
    }

    // Prefix routes: longest match wins. Used for /serve/<name>, /task/<name>.
    RouteEntry best;
    size_t bestLen = 0;
    std::string bestPrefix;
    {
        std::lock_guard<std::mutex> lock(m_routeMutex);
        for (const auto& kv : m_routes) {
            if (!kv.second.prefix) {
                continue;
            }
            // key = "METHOD /prefix"
            size_t sp = kv.first.find(' ');
            std::string m = kv.first.substr(0, sp);
            std::string p = kv.first.substr(sp + 1);
            if (m != req.method) {
                continue;
            }
            if (p.size() >= 1 && p.back() == '*') {
                std::string prefix = p.substr(0, p.size() - 1);
                if (req.path.compare(0, prefix.size(), prefix) == 0 && p.size() > bestLen) {
                    bestLen = p.size();
                    best = kv.second;
                    bestPrefix = prefix;
                }
            }
        }
    }
    if (bestLen > 0 && best.prefix) {
        std::string tail = req.path.substr(bestPrefix.size());
        return best.prefix(req, tail);
    }

    return HttpResponse::NotFound();
}

} // namespace webagent
