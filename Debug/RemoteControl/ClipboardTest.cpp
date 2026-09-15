//==============================================================================
// ClipboardTest.cpp - RemoteControl clipboard sync mechanism test
// Purpose:   Verifies the clipboard + protocol mechanism used by both roles of
//            the RemoteControl module WITHOUT a live connection (connectivity
//            is verified separately). Each "flow" calls the exact functions the
//            module threads call:
//
//              Master (controller -> target):
//                ClipReadAll -> Proto::clip(CLIP_SET) -> Proto::unclip
//                -> target ClipWriteAll     (RunSend + CLIP_SET handler)
//              Slave (target -> controller):
//                ClipReadAll -> Proto::clip(CLIP_RESP) -> Proto::unclip
//                -> controller ClipWriteAll (CLIP_GET handler + RunSvRecv)
//
// Note: this test writes to the real Windows clipboard.
//==============================================================================

#include "Modules/RemoteControl/RemoteControlHook.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using RemoteControl::ClipboardPayload;
using namespace RemoteControl;

namespace {

int g_pass = 0;
int g_fail = 0;

void Check(bool ok, const char* name) {
    if (ok) { g_pass++; std::printf("  [PASS] %s\n", name); }
    else    { g_fail++; std::printf("  [FAIL] %s\n", name); }
}

bool PayloadEq(const ClipboardPayload& a, const ClipboardPayload& b) {
    return a.text == b.text && a.dib == b.dib;
}

// clip() returns a framed message ([len:4][payload]); the real receiver strips
// the length via NetworkService::RecvMsg before touching the payload. Mirror
// that here so unclip() sees exactly what the module threads see.
bool Unframe(const std::vector<uint8_t>& framed, std::vector<uint8_t>& pay) {
    if (framed.size() < 4) return false;
    uint32_t n = Proto::r32(framed.data());
    if (framed.size() != 4 + n) return false;
    pay.assign(framed.data() + 4, framed.data() + 4 + n);
    return true;
}

// 2x2 32bpp bottom-up DIB: BITMAPINFOHEADER + RGBA pixels (row-padded to 4).
std::vector<uint8_t> MakeDib() {
    const LONG w = 2, h = 2;
    const DWORD rowBytes = ((static_cast<DWORD>(w) * 32 + 31) / 32) * 4; // 8
    const DWORD imgBytes = rowBytes * static_cast<DWORD>(h);            // 16
    std::vector<uint8_t> d(sizeof(BITMAPINFOHEADER) + imgBytes, 0);
    auto* bi = reinterpret_cast<BITMAPINFOHEADER*>(d.data());
    bi->biSize = sizeof(BITMAPINFOHEADER);
    bi->biWidth = w;
    bi->biHeight = h; // positive => bottom-up
    bi->biPlanes = 1;
    bi->biBitCount = 32;
    bi->biCompression = BI_RGB;
    bi->biSizeImage = imgBytes;
    for (size_t i = sizeof(BITMAPINFOHEADER); i < d.size(); ++i)
        d[i] = static_cast<uint8_t>(i & 0xFF); // recognizable pattern
    return d;
}

} // namespace

int main() {
    std::printf("=== RemoteControl ClipboardTest ===\n");

    std::vector<uint8_t> dib = MakeDib();

    // ---- Protocol pack/unpack ----
    {
        std::printf("-- protocol: pack/unpack --\n");
        ClipboardPayload src;
        src.text = L"hello world \x2022";
        auto m = Proto::clip(Proto::CLIP_SET, src);
        std::vector<uint8_t> pay;
        Check(Unframe(m, pay), "text payload frames");
        ClipboardPayload out;
        int tc = 0, db = 0;
        Check(Proto::unclip(pay, out, &tc, &db), "text payload parses");
        Check(out.text == src.text && out.dib.empty(), "text bytes preserved");
        Check(tc == static_cast<int>(src.text.size()), "text char count");
    }
    {
        ClipboardPayload src;
        src.text = L"with image";
        src.dib = dib;
        auto m = Proto::clip(Proto::CLIP_SET, src);
        std::vector<uint8_t> pay;
        Check(Unframe(m, pay), "text+image message frames");
        ClipboardPayload out;
        int tc = 0, db = 0;
        Check(Proto::unclip(pay, out, &tc, &db), "text+image payload parses");
        Check(PayloadEq(out, src), "text+image bytes preserved");
        Check(db == static_cast<int>(dib.size()), "image byte count");
    }
    {
        ClipboardPayload empty;
        auto m = Proto::clip(Proto::CLIP_RESP, empty);
        std::vector<uint8_t> pay;
        Check(Unframe(m, pay), "empty message frames");
        ClipboardPayload out;
        int tc = 0, db = 0;
        Check(Proto::unclip(pay, out, &tc, &db), "empty container parses");
        Check(out.text.empty() && out.dib.empty(), "empty means clear");
    }
    {
        ClipboardPayload src;
        src.text = L"keep-me";
        src.dib.assign(RemoteControl::Proto::kMaxClipPayload + 1, 0xAB);
        auto m = Proto::clip(Proto::CLIP_SET, src);
        std::vector<uint8_t> pay;
        Check(Unframe(m, pay), "oversized message frames");
        ClipboardPayload out;
        int tc = 0, db = 0;
        Check(Proto::unclip(pay, out, &tc, &db), "oversized payload parses");
        Check(out.text == L"keep-me" && out.dib.empty(),
              "oversized image dropped, text kept");
    }
    {
        std::vector<uint8_t> bad;
        bad.push_back(Proto::CLIP_SET);
        bad.push_back(2); // claims 2 blocks
        bad.push_back(Proto::CLIP_FMT_TEXT);
        bad.push_back(0xFF); bad.push_back(0xFF); bad.push_back(0xFF); bad.push_back(0xFF);
        ClipboardPayload out;
        int tc = 0, db = 0;
        Check(!Proto::unclip(bad, out, &tc, &db), "malformed payload rejected");
    }

    // ---- Real clipboard round-trip (same process) ----
    {
        std::printf("-- clipboard primitives --\n");
        ClipboardPayload src;
        src.text = L"round-trip";
        src.dib = dib;
        Check(RemoteControlHook::ClipWriteAll(src), "write text+image");
        ClipboardPayload out;
        RemoteControlHook::ClipReadAll(out);
        Check(PayloadEq(out, src), "read back text+image");
    }
    {
        ClipboardPayload src; // empty -> clear
        Check(RemoteControlHook::ClipWriteAll(src), "write empty clears");
        ClipboardPayload out;
        RemoteControlHook::ClipReadAll(out);
        Check(out.text.empty() && out.dib.empty(), "clipboard is empty");
    }

    // ---- Master flow: controller pushes clipboard to target ----
    {
        std::printf("-- master (controller -> target) --\n");
        ClipboardPayload ctl;
        ctl.text = L"master-msg";
        ctl.dib = dib;
        Check(RemoteControlHook::ClipWriteAll(ctl), "controller clipboard set");

        // controller side: RunSend pendingToggleOff path
        ClipboardPayload pushed;
        RemoteControlHook::ClipReadAll(pushed);
        auto wire = Proto::clip(Proto::CLIP_SET, pushed);
        std::vector<uint8_t> pay;
        Check(Unframe(wire, pay), "controller packs CLIP_SET frame");
        Check(Proto::unclip(pay, pushed, nullptr, nullptr), "controller payload parses");

        // target side: RunClient CLIP_SET handler
        ClipboardPayload recv;
        int tc = 0, db = 0;
        Check(Proto::unclip(pay, recv, &tc, &db), "target parses CLIP_SET");
        Check(tc == static_cast<int>(ctl.text.size()), "target text count");
        Check(RemoteControlHook::ClipWriteAll(recv), "target writes clipboard");

        ClipboardPayload ver;
        RemoteControlHook::ClipReadAll(ver);
        Check(PayloadEq(ver, ctl), "target clipboard == controller clipboard");
    }

    // ---- Slave flow: target pushes clipboard back to controller ----
    {
        std::printf("-- slave (target -> controller) --\n");
        ClipboardPayload tgt;
        tgt.text = L"slave-reply";
        tgt.dib = dib;
        Check(RemoteControlHook::ClipWriteAll(tgt), "target clipboard set");

        // target side: RunClient CLIP_GET handler
        ClipboardPayload read;
        RemoteControlHook::ClipReadAll(read);
        auto wire = Proto::clip(Proto::CLIP_RESP, read);
        std::vector<uint8_t> pay;
        Check(Unframe(wire, pay), "target packs CLIP_RESP frame");

        // controller side: RunSvRecv CLIP_RESP handler
        ClipboardPayload rcvd;
        int tc = 0, db = 0;
        Check(Proto::unclip(pay, rcvd, &tc, &db), "controller parses CLIP_RESP");
        Check(db == static_cast<int>(dib.size()), "controller image count");
        Check(RemoteControlHook::ClipWriteAll(rcvd), "controller writes clipboard");

        ClipboardPayload ver;
        RemoteControlHook::ClipReadAll(ver);
        Check(PayloadEq(ver, tgt), "controller clipboard == target clipboard");
    }

    std::printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}