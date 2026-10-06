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

    int sharedCount();
    void logSummary();

    void checkOuter();
}
