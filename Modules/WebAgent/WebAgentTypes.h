//==============================================================================
// WebAgentTypes.h - Shared value types for the Web Agent module
// Purpose:   Small POD-ish structures passed between the HTTP layer and the
//            API handlers. Kept free of winsock so tests can include it without
//            pulling in the networking headers.
//==============================================================================

#pragma once

#include <map>
#include <string>
#include <vector>

namespace webagent {

// A parsed HTTP request. Header keys are lower-cased for case-insensitive
// lookup; the request line is split into method / target / query.
struct HttpRequest {
    std::string method;                                   // GET, POST, ...
    std::string path;                                     // "/poll"
    std::string query;                                    // "session=abc"
    std::string version;                                  // "HTTP/1.1"
    std::map<std::string, std::string> headers;           // lower-cased keys
    std::string body;                                     // raw request body

    // Returns the value of a query-string parameter, or "" if absent.
    std::string GetQuery(const std::string& key) const;
};

// An HTTP response handed back to the connection writer. Body is sent verbatim
// after the status line + headers (which the writer generates).
struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
    std::vector<std::pair<std::string, std::string>> extraHeaders;

    static HttpResponse Json(int status, const std::string& body);
    static HttpResponse Text(int status, const std::string& body,
                             const std::string& contentType = "text/plain; charset=utf-8");
    static HttpResponse NoContent();
    static HttpResponse NotFound(const std::string& message = "not found");
    static HttpResponse BadRequest(const std::string& message = "bad request");
};

} // namespace webagent
