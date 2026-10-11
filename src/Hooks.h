// The hook manifest: every hook of Nitoryu goes in through hooks::add, so one place knows them all.
//
// Each call is one row: the game symbol, the feature group it belongs to (the names of the load line: core,
// pose, guard, ...), whether it went in, and whether another plugin had already detoured the target (its first
// bytes are a jump: E9 rel32 or FF 25; one `shared_hook` line each, the count on the compat line). The shared
// check reads the target right before our own detour goes in, so it can never see ours.
// After startPlugin, hooks::logSummary() writes one `hooks_installed` line: rows, ok, the failed symbols, shared.
//
// The install is KenshiLib::AddHook, one hook at a time.
//
// Threads: start-up only (startPlugin), one thread; checkOuter on the main loop, after start-up.
#pragma once

#include <core/Functions.h>
#include <stdint.h>

namespace hooks
{
    KenshiLib::HookStatus addRaw(const char* symbol, const char* group, void* target, void* detour, void** original);

    template<typename T>
    inline KenshiLib::HookStatus add(const char* symbol, const char* group, intptr_t target, void* detour, T** original)
    {
        return addRaw(symbol, group, (void*)target, detour, (void**)original);
    }

    template<typename T1, typename T2>
    inline KenshiLib::HookStatus add(const char* symbol, const char* group, T1* target, void* detour, T2** original)
    {
        return addRaw(symbol, group, (void*)target, detour, (void**)original);
    }

    int sharedCount();              // targets another plugin had detoured before us
    void logSummary();              // one `hooks_installed` line

    // A plugin that loads after us and hooks the same function sits outside our detour, and if it does not call
    // the original ours never runs. Right after each install the jump chain from the target is followed (E9 rel32,
    // FF 25 [rip], a few hops) and must end at our detour; on the first main-loop frame, when every plugin is in,
    // it is followed again. A row whose chain now ends elsewhere gets a `hook_outer` line naming the module the
    // chain ends in, and for the rows that carry Dual Wielding's growth and the off-hand guard an error line
    // saying to load Nitoryu below it (log only, nothing in game). Main loop, once.
    void checkOuter();
}
