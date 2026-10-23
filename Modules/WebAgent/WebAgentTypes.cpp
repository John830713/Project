//==============================================================================
// WebAgentTypes.cpp - Value type helpers for the Web Agent module
//==============================================================================

#include "WebAgentTypes.h"

namespace webagent {

std::string UrlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%' && i + 2 < in.size()) {
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                return -1;
            };
            int hi = hex(in[i + 1]);
            int lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(c);
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string HttpRequest::GetQuery(const std::string& key) const {
    // Walk "a=1&b=2" looking for key=... and return the decoded value.
    std::string needle = key + "=";
    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        if (pair.compare(0, needle.size(), needle) == 0) {
            return UrlDecode(pair.substr(needle.size()));
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
