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

inline std::vector<uint8_t> clip(uint8_t t, const wchar_t* s, int n) {
    if (n < 0) throw std::invalid_argument("clip: negative character count");
    if (n == 0) {
        uint8_t hdr[5] = { t };
        w32(hdr + 1, 0);
        return frame(hdr, 5);
    }
    if (s == nullptr) throw std::invalid_argument("clip: null string");
    uint32_t b = static_cast<uint32_t>(static_cast<size_t>(n) * sizeof(wchar_t));
    std::vector<uint8_t> p(5 + b);
    p[0] = t;
    w32(p.data() + 1, b);
    std::memcpy(p.data() + 5, s, b);
    return frame(p.data(), static_cast<uint32_t>(p.size()));
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
