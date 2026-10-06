//==============================================================================
// RemoteControlTypes.h - Protocol definitions and shared types
// Purpose:   Wire protocol message types, serialization helpers, and
//            input event structures for the Remote Control module.
//==============================================================================

#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <type_traits>
#include <string>

namespace RemoteControl {

struct ClipboardPayload {
    std::wstring text;        // UTF-16 clipboard text (empty = none)
    std::vector<uint8_t> dib; // CF_DIB bytes (empty = none)
};

// Defensive port parser for user-editable INI values. Never throws: worker
// threads read these values, and an uncaught std::stoi exception there would
// std::terminate the whole process. Returns false for empty/garbage/out-of-
// -range input (out is then set to def); clamps 1..1023 up to 1024 because
// privileged ports are unusable here.
inline bool TryParsePort(const std::wstring& s, int def, int& out) {
    out = def;
    if (s.empty()) return false;
    try {
        size_t pos = 0;
        int port = std::stoi(s, &pos);
        if (pos != s.size() || port < 1 || port > 65535) return false;
        out = (port < 1024) ? 1024 : port;
        return true;
    } catch (...) {
        return false;
    }
}

namespace Proto {

enum MsgType : uint8_t {
    AUTH = 0,
    KEY_DOWN,
    KEY_UP,
    MOUSE_MOVE,
    MOUSE_DOWN,
    MOUSE_UP,
    MOUSE_SCROLL,
    RELEASE_ALL,
    CLIP_SET,
    CLIP_GET,
    CLIP_RESP,
    FT_BEGIN,
    FT_CHUNK_MSG,
    FT_END,
    FT_DIR_BEGIN,
    HOOK_STATUS
};

enum ClipFormat : uint8_t {
    CLIP_FMT_TEXT = 0,
    CLIP_FMT_DIB = 1
};

// Must stay below NetworkService::kMaxMsgSize (10 MB); the frame prefix plus
// per-format headers must never push a message over the wire limit.
constexpr uint32_t kMaxClipPayload = 10 * 1024 * 1024 - 64;

inline void w16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

inline void w32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}

inline void w64(uint8_t* p, uint64_t v) {
    w32(p, static_cast<uint32_t>(v >> 32));
    w32(p + 4, static_cast<uint32_t>(v & 0xFFFFFFFFULL));
}

inline uint16_t r16(const uint8_t* p) {
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

inline uint32_t r32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

inline int32_t ri32(const uint8_t* p) {
    return static_cast<int32_t>(r32(p));
}

inline uint64_t r64(const uint8_t* p) {
    return (static_cast<uint64_t>(r32(p)) << 32) | r32(p + 4);
}

inline std::vector<uint8_t> frame(const uint8_t* d, uint32_t n) {
    if (n > 0 && d == nullptr)
        throw std::invalid_argument("frame: null data with non-zero length");
    std::vector<uint8_t> f(4 + n);
    w32(f.data(), n);
    if (n > 0) std::memcpy(f.data() + 4, d, n);
    return f;
}

inline std::vector<uint8_t> auth(const char* pw, int len) {
    if (len < 0) throw std::invalid_argument("auth: negative length");
    if (len > 0 && pw == nullptr) throw std::invalid_argument("auth: null password");
    std::vector<uint8_t> p(1 + static_cast<size_t>(len));
    p[0] = AUTH;
    if (len > 0) std::memcpy(p.data() + 1, pw, static_cast<size_t>(len));
    return frame(p.data(), static_cast<uint32_t>(p.size()));
}

inline std::vector<uint8_t> key(uint8_t t, uint16_t vk, uint16_t sc, uint8_t ext) {
    uint8_t p[6] = { t };
    w16(p + 1, vk);
    w16(p + 3, sc);
    p[5] = ext;
    return frame(p, 6);
}

inline std::vector<uint8_t> mmove(int32_t dx, int32_t dy) {
    uint8_t p[9] = { MOUSE_MOVE };
    w32(p + 1, static_cast<uint32_t>(dx));
    w32(p + 5, static_cast<uint32_t>(dy));
    return frame(p, 9);
}

inline std::vector<uint8_t> mbtn(uint8_t t, uint8_t b) {
    uint8_t p[2] = { t, b };
    return frame(p, 2);
}

inline std::vector<uint8_t> mscrl(int32_t d) {
    uint8_t p[5] = { MOUSE_SCROLL };
    w32(p + 1, static_cast<uint32_t>(d));
    return frame(p, 5);
}

inline std::vector<uint8_t> simple(uint8_t t) {
    return frame(&t, 1);
}

inline std::vector<uint8_t> clip(uint8_t t, const ClipboardPayload& c) {
    // Layout: [type][format count][(fmt][len:4][data)]*  — one frame holds the
    // complete clipboard state (text + optional DIB image) so the receiver can
    // replace its clipboard atomically. An empty container means "clear".
    std::vector<uint8_t> p;
    p.push_back(t);
    p.push_back(0); // format count placeholder

    bool hasText = !c.text.empty();
    bool hasDib  = !c.dib.empty();

    size_t textBytes = c.text.size() * sizeof(wchar_t);
    size_t total = 2u;
    if (hasText) total += textBytes + 5u;
    if (hasDib)  total += c.dib.size() + 5u;

    // Keep the frame within the wire limit; on overflow drop the image but keep
    // the text (text arrives as its own block and always fits in practice).
    if (hasDib && total > kMaxClipPayload) {
        hasDib = false;
        total -= c.dib.size() + 5u;
    }

    p[1] = static_cast<uint8_t>((hasText ? 1 : 0) + (hasDib ? 1 : 0));

    if (hasText) {
        p.push_back(CLIP_FMT_TEXT);
        uint8_t h[4];
        w32(h, static_cast<uint32_t>(textBytes));
        p.insert(p.end(), h, h + 4);
        const uint8_t* s = reinterpret_cast<const uint8_t*>(c.text.c_str());
        p.insert(p.end(), s, s + textBytes);
    }
    if (hasDib) {
        p.push_back(CLIP_FMT_DIB);
        uint8_t h[4];
        w32(h, static_cast<uint32_t>(c.dib.size()));
        p.insert(p.end(), h, h + 4);
        p.insert(p.end(), c.dib.begin(), c.dib.end());
    }
    return frame(p.data(), static_cast<uint32_t>(p.size()));
}

// Inverse of clip(): parses a CLIP_SET/CLIP_RESP container payload (already
// stripped of the frame header) back into a ClipboardPayload. Returns false on
// malformed data. The optional count outputs mirror what the module logs.
inline bool unclip(const std::vector<uint8_t>& pay, ClipboardPayload& out,
                   int* textChars, int* dibBytes)
{
    out.text.clear();
    out.dib.clear();
    if (textChars) *textChars = 0;
    if (dibBytes)  *dibBytes  = 0;
    if (pay.size() < 2) return false;

    uint32_t fmtCount = pay[1];
    size_t pos = 2;
    for (uint32_t i = 0; i < fmtCount; i++) {
        if (pos + 5 > pay.size()) return false;
        uint8_t fmt = pay[pos];
        uint32_t n  = r32(pay.data() + pos + 1);
        pos += 5;
        if (pos + n > pay.size()) return false;

        if (fmt == CLIP_FMT_TEXT) {
            uint32_t ch = n / sizeof(wchar_t);
            out.text.assign(reinterpret_cast<const wchar_t*>(pay.data() + pos), ch);
            if (textChars) *textChars = static_cast<int>(ch);
        } else if (fmt == CLIP_FMT_DIB) {
            out.dib.assign(pay.data() + pos, pay.data() + pos + n);
            if (dibBytes) *dibBytes = static_cast<int>(n);
        }
        pos += n;
    }
    return true;
}

inline std::vector<uint8_t> fileBegin(const char* name, int nlen, uint64_t size) {
    if (nlen < 0) throw std::invalid_argument("fileBegin: negative name length");
    if (nlen > 0 && name == nullptr) throw std::invalid_argument("fileBegin: null name");
    if (nlen > 0xFFFF) throw std::invalid_argument("fileBegin: name too long");
    std::vector<uint8_t> p(1 + 2 + static_cast<size_t>(nlen) + 8);
    p[0] = FT_BEGIN;
    w16(p.data() + 1, static_cast<uint16_t>(nlen));
    if (nlen > 0) std::memcpy(p.data() + 3, name, static_cast<size_t>(nlen));
    w64(p.data() + 3 + nlen, size);
    return frame(p.data(), static_cast<uint32_t>(p.size()));
}

inline std::vector<uint8_t> fileChunk(const void* d, int len) {
    if (len < 0) throw std::invalid_argument("fileChunk: negative length");
    if (len > 0 && d == nullptr) throw std::invalid_argument("fileChunk: null data");
    std::vector<uint8_t> p(1 + static_cast<size_t>(len));
    p[0] = FT_CHUNK_MSG;
    if (len > 0) std::memcpy(p.data() + 1, d, static_cast<size_t>(len));
    return frame(p.data(), static_cast<uint32_t>(p.size()));
}

inline std::vector<uint8_t> dirBegin(const char* relPath, int nlen) {
    if (nlen < 0) throw std::invalid_argument("dirBegin: negative length");
    if (nlen > 0 && relPath == nullptr) throw std::invalid_argument("dirBegin: null path");
    if (nlen > 0xFFFF) throw std::invalid_argument("dirBegin: path too long");
    std::vector<uint8_t> p(1 + 2 + static_cast<size_t>(nlen));
    p[0] = FT_DIR_BEGIN;
    w16(p.data() + 1, static_cast<uint16_t>(nlen));
    if (nlen > 0) std::memcpy(p.data() + 3, relPath, static_cast<size_t>(nlen));
    return frame(p.data(), static_cast<uint32_t>(p.size()));
}

inline std::vector<uint8_t> hookStatus(uint8_t active) {
    uint8_t p[2] = { HOOK_STATUS, active };
    return frame(p, 2);
}

} // namespace Proto

enum class EventType : uint8_t {
    KeyDown = 1,
    KeyUp,
    MouseMove,
    MouseDown,
    MouseUp,
    MouseScroll
};

struct Event {
    EventType type;
    union {
        struct { uint16_t vk, scan; uint8_t ext; } key;
        struct { int32_t dx, dy; }                   move;
        struct { uint8_t button; }                   btn;
        struct { int32_t delta; }                    scroll;
    };
};

} // namespace RemoteControl
