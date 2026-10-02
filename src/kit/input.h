#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace kit
{
    inline bool held(int virtualKey) { return (GetAsyncKeyState(virtualKey) & 0x8000) != 0; }
    inline bool shiftHeld() { return held(VK_SHIFT); }
    inline bool altHeld() { return held(VK_MENU); }
}
