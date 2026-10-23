//==============================================================================
// WebAgentTest.cpp - Web Agent HTTP layer tests
// Purpose:   Covers the unit-testable parts of the Web Agent module:
//              - query-string parsing and URL decoding
//              - request-line + header parsing (via a live loopback round-trip)
//              - router exact/prefix dispatch and 404 fallback
//              - CORS headers and JSON content-type on responses
//            Starts a real server on an ephemeral port and talks to it over
//            loopback so the parse -> dispatch -> write path is exercised whole.
//==============================================================================

#include "Modules/WebAgent/HttpServer.h"
#include "Modules/WebAgent/WebAgentApi.h"
#include "Modules/WebAgent/WebAgentTypes.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <fstream>
#include <string>

#include <windows.h>

namespace {

int g_pass = 0;
int g_fail = 0;

void Check(bool ok, const char* name) {
    if (ok) { g_pass++; std::printf("  [PASS] %s\n", name); }
    else    { g_fail++; std::printf("  [FAIL] %s\n", name); }
}

void MkDir(const std::string& path) {
    CreateDirectoryA(path.c_str(), nullptr);
}

void WriteFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    f << content;
}

// Minimal blocking HTTP client for the test. Sends a raw request and returns
// the full response text.
std::string HttpRoundTrip(int port, const std::string& request) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return "";

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return "";
    }

    send(s, request.data(), static_cast<int>(request.size()), 0);

    std::string out;
    char buf[4096];
    int n;
    while ((n = recv(s, buf, sizeof(buf), 0)) > 0) {
        out.append(buf, static_cast<size_t>(n));
    }
    closesocket(s);
    return out;
}

bool Contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

} // namespace

int main() {
    std::printf("=== WebAgent HTTP tests ===\n");

    //--- Query string parsing ---
    {
        webagent::HttpRequest req;
        req.query = "session=abc123&url=http%3A%2F%2Fx%2Fy&tagged=1";
        Check(req.GetQuery("session") == "abc123", "query: session value");
        Check(req.GetQuery("tagged") == "1", "query: tagged value");
        Check(req.GetQuery("missing") == "", "query: missing key -> empty");
        Check(webagent::UrlDecode("a%20b+c") == "a b c", "urldecode: %20 and +");
        Check(webagent::UrlDecode("%2Fpath%2F") == "/path/", "urldecode: slashes");
        Check(webagent::UrlDecode("plain") == "plain", "urldecode: passthrough");
    }

    //--- Response factories ---
    {
        auto nf = webagent::HttpResponse::NotFound();
        Check(nf.status == 404, "response: NotFound status");
        Check(Contains(nf.body, "error"), "response: NotFound carries error key");
        Check(nf.contentType.find("json") != std::string::npos, "response: NotFound is json");
        auto nc = webagent::HttpResponse::NoContent();
        Check(nc.status == 204, "response: NoContent status");
    }

    //--- Live router round-trip ---
    {
        webagent::HttpServer server;
        server.AddRoute("GET", "/ping", [](const webagent::HttpRequest&) {
            return webagent::HttpResponse::Json(200, "{\"ok\":true}");
        });
        server.AddRoute("GET", "/exact", [](const webagent::HttpRequest&) {
            return webagent::HttpResponse::Json(200, "{\"path\":\"/exact\"}");
        });
        // Prefix route: /serve/<name>
        server.AddRoute("GET", "/serve*", [](const webagent::HttpRequest&, const std::string& tail) {
            return webagent::HttpResponse::Text(200, "tail=" + tail,
                                                "application/x-javascript; charset=utf-8");
        });
        server.AddRoute("POST", "/echo", [](const webagent::HttpRequest& req) {
            return webagent::HttpResponse::Json(200, "{\"len\":" + std::to_string(req.body.size()) + "}");
        });

        // Port 0 is not supported by the bind helper, so pick a high fixed port
        // unlikely to clash during a test run.
        int port = 18791;
        bool started = server.Start("127.0.0.1", port);
        Check(started, "server: start on loopback");
        if (started) {
            std::string resp = HttpRoundTrip(port, "GET /ping HTTP/1.1\r\nHost: x\r\n\r\n");
            Check(Contains(resp, "200 OK"), "server: ping status line");
            Check(Contains(resp, "{\"ok\":true}"), "server: ping body");
            Check(Contains(resp, "Access-Control-Allow-Origin: *"), "server: CORS header present");
            Check(Contains(resp, "Connection: close"), "server: connection close header");

            std::string exact = HttpRoundTrip(port, "GET /exact HTTP/1.1\r\nHost: x\r\n\r\n");
            Check(Contains(exact, "{\"path\":\"/exact\"}"), "router: exact match");

            std::string prefix = HttpRoundTrip(port, "GET /serve/foo.js HTTP/1.1\r\nHost: x\r\n\r\n");
            Check(Contains(prefix, "tail=/foo.js"), "router: prefix match captures tail");

            std::string miss = HttpRoundTrip(port, "GET /nope HTTP/1.1\r\nHost: x\r\n\r\n");
            Check(Contains(miss, "404 Not Found"), "router: unmatched -> 404");

            std::string preflight = HttpRoundTrip(port, "OPTIONS /ping HTTP/1.1\r\nHost: x\r\n\r\n");
            Check(Contains(preflight, "204 No Content"), "router: OPTIONS -> 204");

            std::string body = "hello"; // len 5
            std::string postReq =
                "POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\n" + body;
            std::string echoed = HttpRoundTrip(port, postReq);
            Check(Contains(echoed, "{\"len\":5}"), "server: POST body length");

            server.Stop();
            Check(!server.IsRunning(), "server: stops cleanly");
        }
    }

    //--- P1: static JS serving via WebAgentApi against a temp resource root ---
    {
        std::string root = "D:\\Project\\.agent\\webagent-testroot";
        MkDir("D:\\Project\\.agent");
        MkDir(root);
        MkDir(root + "\\agent");
        MkDir(root + "\\modules");
        WriteFile(root + "\\agent\\core.js", "// core vtest\nvar x=1;");
        WriteFile(root + "\\modules\\demo.js",
                  "// ==UserScript==\n// @version   1.2.3\n// ==/UserScript==\n");
        WriteFile(root + "\\modules\\modules.json",
                  "[{\"name\":\"demo\",\"script\":\"demo.js\",\"enabled\":true}]");

        webagent::WebAgentApi api;
        api.SetResourceRoot(root);

        webagent::HttpServer server;
        api.RegisterRoutes(server);
        int port = 18792;
        bool started = server.Start("127.0.0.1", port);
        Check(started, "static: server start");
        if (started) {
            std::string core = HttpRoundTrip(port, "GET /agent.core.js HTTP/1.1\r\nHost: x\r\n\r\n");
            Check(Contains(core, "200 OK"), "static: core.js 200");
            Check(Contains(core, "core vtest"), "static: core.js served body");
            Check(Contains(core, "no-cache"), "static: core.js no-cache header");
            Check(Contains(core, "x-javascript"), "static: core.js js content-type");

            std::string modules = HttpRoundTrip(port, "GET /modules HTTP/1.1\r\nHost: x\r\n\r\n");
            Check(Contains(modules, "200 OK"), "static: /modules 200");
            Check(Contains(modules, "demo"), "static: /modules lists module");
            Check(Contains(modules, "1.2.3"), "static: /modules injects @version");

            std::string serve = HttpRoundTrip(port,
                "GET /serve/demo.js HTTP/1.1\r\nHost: x\r\n\r\n");
            Check(Contains(serve, "200 OK"), "static: /serve/<module> 200");
            Check(Contains(serve, "UserScript"), "static: /serve served module body");

            std::string traversal = HttpRoundTrip(port,
                "GET /serve/..%2f..%2fsecret HTTP/1.1\r\nHost: x\r\n\r\n");
            Check(Contains(traversal, "400") || Contains(traversal, "404"),
                  "static: path traversal blocked");

            std::string missing = HttpRoundTrip(port,
                "GET /agent.core.js.missing HTTP/1.1\r\nHost: x\r\n\r\n");
            Check(Contains(missing, "404"), "static: unknown path 404");

            server.Stop();
        }
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
