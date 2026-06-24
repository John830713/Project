//==============================================================================
// RemoteControlHook.h - Input hook capture and replay
// Purpose:   Provides low-level keyboard/mouse hook capture (WH_KEYBOARD_LL,
//            WH_MOUSE_LL) for the Server/Controller side, and SendInput
//            replay functions for the Client/Target side.
//==============================================================================

#pragma once
#include "RemoteControlTypes.h"
#include <windows.h>

class RemoteControlHook {
public:
    using EventCallback = void(*)(const RemoteControl::Event* e, void* ud);
    using ToggleCallback = void(*)(int active, void* ud);

    static bool Start(EventCallback ecb, ToggleCallback tcb, void* ud);
    static void Stop();

    static bool IsActive();
    static void SetActive(bool on);
    static void SetReplaying(bool on);

    static void ReplayKey(uint16_t vk, uint16_t scan, uint8_t ext, int up);
    static void ReplayMouseMove(int32_t dx, int32_t dy);
    static void ReplayMouseBtn(uint8_t btn, int down);
    static void ReplayMouseScroll(int32_t delta);
    static void ReplayReleaseAll();

    static int  ClipRead(wchar_t* buf, int cap);
    static int  ClipWrite(const wchar_t* txt, int len);

    static constexpr ULONG_PTR kMagic = 0xB0B0C0C0;

private:
    static LRESULT CALLBACK KbProc(int n, WPARAM w, LPARAM l);
    static LRESULT CALLBACK MsProc(int n, WPARAM w, LPARAM l);
};
