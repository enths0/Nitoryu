#include "kit/input.h"

#include "Diagnostics.h"
#include "kit/diag.h"

#include <cstdio>
#include <exception>
#include <string>

namespace
{
    CRITICAL_SECTION g_lock;
    kit::Counters g_counts;
    const int DETAIL_BUDGET = 300;
    int g_detailLeft = DETAIL_BUDGET;
    unsigned g_mainThread = 0;
    diag::Level g_level = diag::NORMAL;

    const char* HOOK_NAMES[diag::HK_COUNT] = {
        "set_weapon", "weight_speed", "attack_speed", "melee_attack", "block_chance",
        "equipped_skill", "choose_attack", "hit_by", "i_hit", "init_block", "serialise", "update_stats",
        "char_dtor", "increase_stat", "xp_melee", "xp_training", "operate", "stats_window", "line_resize",
        "refresh_section", "cell_update", "who_attacks", "tech_ctor", "add_wound", "update_transforms",
        "melee_task" };
    volatile LONGLONG g_hookCalls[diag::HK_COUNT];
    volatile LONGLONG g_hookOurs[diag::HK_COUNT];
    volatile LONGLONG g_hookPassed[diag::HK_COUNT];
    volatile LONGLONG g_hookThrown[diag::HK_COUNT];
    volatile LONGLONG g_hookOffMain[diag::HK_COUNT];

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

    HookCall::HookCall(Hook h) : h_(h), ours_(false)
    {
        InterlockedIncrement64(&g_hookCalls[h]);
        if (g_mainThread != 0 && GetCurrentThreadId() != g_mainThread)
            InterlockedIncrement64(&g_hookOffMain[h]);
    }

    HookCall::~HookCall()
    {
        if (std::uncaught_exception())
            InterlockedIncrement64(&g_hookThrown[h_]);
        else
            InterlockedIncrement64(ours_ ? &g_hookOurs[h_] : &g_hookPassed[h_]);
    }

    void emitHooks()
    {
        if (g_level == OFF)
            return;
        std::string line("hooks");
        for (int h = 0; h < HK_COUNT; ++h)
        {

            LONGLONG ours = g_hookOurs[h], passed = g_hookPassed[h], thrown = g_hookThrown[h];
            LONGLONG calls = g_hookCalls[h];
            if (calls == 0)
                continue;
            char b[160];
            if (thrown > 0)
                sprintf_s(b, " %s=%lld/%lld/%lld/%lld", HOOK_NAMES[h], calls, ours, passed, thrown);
            else
                sprintf_s(b, " %s=%lld/%lld/%lld", HOOK_NAMES[h], calls, ours, passed);
            line += b;

            LONGLONG off = g_hookOffMain[h];
            if (off == 0)
                line += "@main";
            else if (off >= calls)
                line += "@other";
            else
            {
                sprintf_s(b, "@both:%lld", off);
                line += b;
            }
            LONGLONG open = calls - ours - passed - thrown;
            if (open != 0)
            {
                sprintf_s(b, " %s_open=%lld", HOOK_NAMES[h], open);
                line += b;
            }
        }
        kit::log::line(line);
    }

    unsigned mainThread() { return g_mainThread; }
    void setMainThread(unsigned id) { g_mainThread = id; }
}
