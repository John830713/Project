//==============================================================================
// HttpServer.h - Minimal HTTP/1.1 server for the Web Agent module
// Purpose:   Provides a blocking, single-listener server that parses simple
//            requests and dispatches them to registered handlers. This is a
//            purpose-built replacement for the Flask server in the original
//            Tampermonkey Web Agent; it speaks the exact same JSON protocol.
//
// Design:
//   - One listener thread owns the LISTEN socket and accepts in a loop.
//   - Each accepted connection is parsed and handled inline (short-lived,
//     "Connection: close" responses) so a client that vanishes cannot tie up
//     a worker thread indefinitely.
//   - Handlers are looked up in a router table keyed by method + path.
//==============================================================================

#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "WebAgentTypes.h"

namespace webagent {

// Handler signature: given a parsed request, produce a response. Called on the
// listener thread, so handlers must not block for long.
using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

class HttpServer {
public:
    HttpServer();
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Register a route. `path` must start with '/'. Later registrations for the
    // same method+path replace earlier ones. A path of "*" acts as a catch-all
    // fallback for that method (used to serve static files / 404s).
    void AddRoute(const std::string& method, const std::string& path, HttpHandler handler);
    void AddRoute(const std::string& method, const std::string& path,
                  std::function<HttpResponse(const HttpRequest&, const std::string& tail)> handler);

    // Bind + listen + spawn the listener thread. Returns false on bind/listen
    // failure (the caller logs the reason). host defaults to 0.0.0.0.
    bool Start(const std::string& host, int port);
    void Stop();

    bool IsRunning() const { return m_running.load(); }
    int GetPort() const { return m_port; }

private:
    struct RouteEntry {
        HttpHandler exact;
        std::function<HttpResponse(const HttpRequest&, const std::string& tail)> prefix;
    };

    void ListenLoop();
    void HandleConnection(SOCKET client);
    HttpResponse Dispatch(const HttpRequest& req);
    static bool ParseRequest(const std::string& raw, size_t headerEnd, HttpRequest& out);

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    SOCKET m_listenSocket = INVALID_SOCKET;
    int m_port = 0;

    std::mutex m_routeMutex;
    // key = method + " " + path
    std::unordered_map<std::string, RouteEntry> m_routes;
};

// Read an entire file into a string. Returns false if the file is missing.
bool ReadFileToString(const std::string& path, std::string& out);

} // namespace webagent
