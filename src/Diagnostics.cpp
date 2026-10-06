#include "kit/input.h"

#include "Diagnostics.h"
#include "kit/diag.h"

#include <cstdio>

namespace
{
    CRITICAL_SECTION g_lock;
    kit::Counters g_counts;
    const int DETAIL_BUDGET = 300;
    int g_detailLeft = DETAIL_BUDGET;
    unsigned g_mainThread = 0;
    diag::Level g_level = diag::NORMAL;

    struct Lock
    {
        Lock() { EnterCriticalSection(&g_lock); }
        ~Lock() { LeaveCriticalSection(&g_lock); }
    };
}

namespace diag
{
    void init() { InitializeCriticalSection(&g_lock); }

    void configure(const std::string& iniFile)
    {
        char env[16] = { 0 }, lvl[16] = { 0 };
        GetPrivateProfileStringA("General", "env", "prod", env, sizeof(env), iniFile.c_str());
        GetPrivateProfileStringA("General", "diag", "", lvl, sizeof(lvl), iniFile.c_str());
        g_level = _stricmp(env, "stage") == 0 ? STAGE : NORMAL;
        if (_stricmp(lvl, "off") == 0)
            g_level = OFF;
        else if (_stricmp(lvl, "normal") == 0)
            g_level = NORMAL;
        else if (_stricmp(lvl, "stage") == 0)
            g_level = STAGE;
        else if (_stricmp(lvl, "verbose") == 0)
            g_level = VERBOSE;
        g_detailLeft = budget(DETAIL_BUDGET);
    }

    Level level() { return g_level; }

    const char* levelName()
    {
        static const char* names[] = { "off", "normal", "stage", "verbose" };
        return names[g_level];
    }

    bool stage() { return g_level >= STAGE; }

    int budget(int base) { return g_level == OFF ? 0 : g_level == VERBOSE ? base * 10 : base; }

    void count(const char* reason)
    {
        if (g_level == OFF)
            return;
        Lock l;
        g_counts.inc(reason);
    }

    void countThread()
    {
        if (g_level == OFF)
            return;
        char key[48];
        DWORD id = GetCurrentThreadId();
        sprintf_s(key, "thread_%s_%lu", id == g_mainThread ? "main" : "other", (unsigned long)id);
        Lock l;
        g_counts.inc(key);
    }

    void emitCounts()
    {
        if (g_level == OFF)
            return;
        kit::Counters copy;
        {
            Lock l;
            copy = g_counts;
            g_counts.clear();
        }

        int queued = 0, dropped = 0;
        kit::log::totals(queued, dropped);
        if (queued > 0 || dropped > 0)
        {
            copy.add("log_lines_queued", queued);
            copy.add("log_lines_dropped", dropped);
        }
        copy.emit("counts");
    }

    bool takeDetail()
    {
        Lock l;
        if (g_detailLeft <= 0)
        {
            if (g_level != OFF)
                g_counts.inc("detail_budget_spent");
            return false;
        }
        --g_detailLeft;
        return true;
    }

    unsigned mainThread() { return g_mainThread; }
    void setMainThread(unsigned id) { g_mainThread = id; }
}
