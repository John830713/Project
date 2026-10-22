//==============================================================================
// WebAgentTypes.cpp - Value type helpers for the Web Agent module
//==============================================================================

#include "WebAgentTypes.h"

namespace webagent {

std::string HttpRequest::GetQuery(const std::string& key) const {
    // Walk "a=1&b=2" looking for key=... and return the raw (still URL-encoded)
    // value. Callers that need decoded values use UrlDecode().
    std::string needle = key + "=";
    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        if (pair.compare(0, needle.size(), needle) == 0) {
            return pair.substr(needle.size());
        }
        if (amp == std::string::npos) {
            break;
        }
        pos = amp + 1;
    }
    return "";
}

HttpResponse HttpResponse::Json(int status, const std::string& body) {
    HttpResponse r;
    r.status = status;
    r.contentType = "application/json; charset=utf-8";
    r.body = body;
    return r;
}

HttpResponse HttpResponse::Text(int status, const std::string& body,
                                const std::string& contentType) {
    HttpResponse r;
    r.status = status;
    r.contentType = contentType;
    r.body = body;
    return r;
}

HttpResponse HttpResponse::NoContent() {
    HttpResponse r;
    r.status = 204;
    r.contentType = "application/json; charset=utf-8";
    return r;
}

HttpResponse HttpResponse::NotFound(const std::string& message) {
    // Mirror Flask's jsonify({'error': ...}) shape so existing JS clients that
    // read `.error` keep working unchanged.
    return Json(404, "{\"error\":\"" + message + "\"}");
}

HttpResponse HttpResponse::BadRequest(const std::string& message) {
    return Json(400, "{\"error\":\"" + message + "\"}");
}

} // namespace webagent
