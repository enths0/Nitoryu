// Threads:
//   No hook of its own. tick(): the main loop. observe, offHandHit, offHandParry, special, forget: any thread
//   (the combat hooks, the character destructor). The table's functions: any thread, getSetting the main loop.
//   g_lock guards g_ring, g_seen and the writes to g_subs. It is held only around those containers: never
//   across a call into an addon, the game or another module. It takes no other lock.
//   g_subs is append-only: an entry is written whole, then g_subCount is raised; a reader takes the count
//   first and reads the entries below it. `dead` is the only field that changes afterwards.
//   Every call into an addon goes through a guard (callInit, callEvent, callRule): a fault there switches that
//   callback off and is logged, the game goes on.
//   g_filters (the move filters) is append-only like g_subs and runs inline like a pair rule, under the same marker.
//   g_rules is append-only like g_subs. A pair rule runs inline on the asking thread; t_inRule marks the thread
//   while it runs, so a rule that asks back (isDual, getInfo) gets NITORYU_E_BUSY instead of a second evaluation.

#include "kit/input.h"   // Windows.h with NOMINMAX first

#include <tlhelp32.h>

#include <cstring>
#include <map>
#include <string>

#include "api/NitoryuApi.h"
#include "core/Ring.h"
#include "Api.h"
#include "Ambidex.h"
#include "Diagnostics.h"
#include "Moves.h"
#include "Settings.h"
#include "Stance.h"
#include "kit/diag.h"

namespace
{
    static_assert(NITORYU_MOVE_STRIKE == moves::SINGLE && NITORYU_MOVE_TWIN == moves::TWIN
                  && NITORYU_MOVE_FINISHER == moves::FINISHER && NITORYU_MOVE_OPENER == moves::OPENER,
                  "NitoryuMoveKind is moves::Kind");
    static_assert(NITORYU_PAIR_DEFAULT == core::ANSWER_DEFAULT && NITORYU_PAIR_ALLOW == core::ANSWER_ALLOW
                  && NITORYU_PAIR_REFUSE == core::ANSWER_REFUSE
                  && NITORYU_PAIR_ALLOW_TWO_HANDED == core::ANSWER_ALLOW_TWO_HANDED,
                  "NitoryuPairVerdict is core::PluginAnswer");

    CRITICAL_SECTION g_lock;
    volatile LONG g_ready = 0;          // the lock exists
    volatile LONG g_started = 0;        // the plugin started with its core hooks
    volatile LONG g_open = 0;           // the table is handed out
    bool g_decided = false;             // main loop: the setting was read, the addons were looked for
    DWORD g_startTick = 0;
    char g_version[32] = "";
    const char* g_initModule = NULL;    // main loop: the addon whose init is running

    // The settings come from FCS once the game data is loaded. Wait this long for it, then go by the ini.
    const DWORD DECIDE_WAIT_MS = 15000;

    struct Lock
    {
        Lock() { EnterCriticalSection(&g_lock); }
        ~Lock() { LeaveCriticalSection(&g_lock); }
    };

    struct Sub
    {
        int type;
        NitoryuEventFn fn;
        void* userData;
        char addon[48];
        volatile LONG dead;             // its callback faulted
    };
    const int MAX_SUBS = 64;
    Sub g_subs[MAX_SUBS];
    volatile LONG g_subCount = 0;

    struct Rule
    {
        NitoryuPairRuleFn fn;
        void* userData;
        char addon[48];
        volatile LONG dead;             // its callback faulted
    };
    const int MAX_RULES = 16;
    Rule g_rules[MAX_RULES];
    volatile LONG g_ruleCount = 0;
    // One `api_pair_rule` line for each distinct decision (rule, the two categories, the answer); under g_lock.
    const int MAX_RULE_LINES = 48;
    unsigned g_ruleSeen[MAX_RULE_LINES];
    int g_ruleSeenCount = 0;

    struct Filter
    {
        NitoryuMoveFilterFn fn;
        void* userData;
        char addon[48];
        volatile LONG dead;             // its callback faulted
    };
    const int MAX_FILTERS = 16;
    Filter g_filters[MAX_FILTERS];
    volatile LONG g_filterCount = 0;
    __declspec(thread) int t_inRule = 0;

    struct Queued
    {
        int type;
        Character* c;
        Character* other;
        Weapon* weapon;
        int value;
        float amount;
        char text[48];
    };
    const int RING = 256;
    core::Ring<Queued, RING> g_ring;

    // What the last fresh evaluation said about a character that fought dual at least once.
    struct Seen
    {
        bool dual;                      // as the subscribers were last told
        int rank;
        int level;                      // Dual Wielding, whole part
        DWORD offSince;                 // GetTickCount of the first "not dual" since then, 0: none waiting
        const char* offReason;          // stance::Info::reason of the latest one (a literal)
    };
    std::map<Character*, Seen> g_seen;
    // In a fight the stance drops out for a moment now and then (nothing in the hands between two states of
    // the game's own weapon handling) and is back within a fraction of a second. "Dual off" is told only
    // when it stayed off this long, so a subscriber gets one on and one off per draw and sheathe.
    const DWORD OFF_HOLD_MS = 600;
    int g_offWaiting = 0;               // entries of g_seen with offSince set; under g_lock

    bool listening() { return g_open != 0 && g_subCount > 0; }

    void copyText(char* out, size_t outSize, const char* text)
    {
        size_t n = text != NULL ? strlen(text) : 0;
        if (n > outSize - 1)
            n = outSize - 1;
        if (n > 0)
            memcpy(out, text, n);
        out[n] = '\0';
    }

    Queued make(int type, Character* c, Character* other, Weapon* weapon, int value, float amount, const char* text)
    {
        Queued q;
        q.type = type;
        q.c = c;
        q.other = other;
        q.weapon = weapon;
        q.value = value;
        q.amount = amount;
        copyText(q.text, sizeof(q.text), text);
        return q;
    }

    void push(const Queued& q)
    {
        Lock l;
        g_ring.push(q);     // a full ring counts the loss; the tick reports it
    }

    // ---- the reasons ----

    struct ReasonName
    {
        const char* stance;             // stance::Info::reason
        int code;
    };
    const ReasonName REASONS[] = {
        { "dual", NITORYU_REASON_DUAL },
        { "not_drawn", NITORYU_REASON_NOT_DRAWN },
        { "layout", NITORYU_REASON_LAYOUT },
        { "hands_not_in_pair", NITORYU_REASON_HANDS_NOT_IN_PAIR },
        { "weapon_refused", NITORYU_REASON_WEAPON_REFUSED },
        { "type_not_allowed", NITORYU_REASON_TYPE_NOT_ALLOWED },
        { "two_handed_only", NITORYU_REASON_TWO_HANDED_ONLY },
        { "no_bare_mesh", NITORYU_REASON_NO_BARE_MESH },
        { "arm", NITORYU_REASON_ARM },
        { "toggled_off", NITORYU_REASON_TOGGLED_OFF },
        { "race_refused", NITORYU_REASON_RACE_REFUSED },
        { "npc_dex", NITORYU_REASON_NPC },
        { "npc_skill", NITORYU_REASON_NPC },
        { "npc_strength", NITORYU_REASON_NPC },
        { "npc_not_opted_in", NITORYU_REASON_NPC },
        { "npc_off", NITORYU_REASON_NPC },
        { "game_version", NITORYU_REASON_GAME_VERSION },
        { "plugin_refused", NITORYU_REASON_PLUGIN_REFUSED },
        { "fault", NITORYU_REASON_NOT_READY },
        { "no_character", NITORYU_REASON_NOT_READY },
        { "no_stats_or_inventory", NITORYU_REASON_NOT_READY },
    };
    const int REASON_COUNT = sizeof(REASONS) / sizeof(REASONS[0]);

    int reasonCode(const char* stanceReason)
    {
        if (stanceReason == NULL)
            return NITORYU_REASON_OTHER;
        for (int i = 0; i < REASON_COUNT; ++i)
            if (strcmp(REASONS[i].stance, stanceReason) == 0)
                return REASONS[i].code;
        return NITORYU_REASON_OTHER;
    }

    // ---- the guards around a call into an addon ----
    // No C++ object lives in these frames. A stack overflow is not caught: swallowed, the next one would end
    // the game with no dump.

    bool callInit(NitoryuAddon_InitFn fn, const NitoryuApi* table)
    {
        __try { fn(table); return true; }
        __except (GetExceptionCode() == EXCEPTION_STACK_OVERFLOW ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool callEvent(NitoryuEventFn fn, const NitoryuEvent* ev, void* userData)
    {
        __try { fn(ev, userData); return true; }
        __except (GetExceptionCode() == EXCEPTION_STACK_OVERFLOW ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool callRule(NitoryuPairRuleFn fn, const NitoryuPairQuery* q, NitoryuPairAnswer* a, void* userData)
    {
        __try { fn(q, a, userData); return true; }
        __except (GetExceptionCode() == EXCEPTION_STACK_OVERFLOW ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool callFilter(NitoryuMoveFilterFn fn, const NitoryuMoveQuery* q, void* userData, int* answer)
    {
        __try { *answer = fn(q, userData); return true; }
        __except (GetExceptionCode() == EXCEPTION_STACK_OVERFLOW ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // A plugin's number for a pair: 1 for anything that is not a number between 0.1 and 10.
    float pairNumber(float v) { return v == v && v >= 0.1f && v <= 10.0f ? v : 1.0f; }

    // ---- the table ----

    int copyOut(const char* text, char* out, unsigned outSize)
    {
        if (out == NULL || outSize == 0)
            return NITORYU_E_ARG;
        size_t n = strlen(text);
        if (n + 1 > outSize)
        {
            out[0] = '\0';
            return NITORYU_E_ARG;
        }
        memcpy(out, text, n + 1);
        return (int)n;
    }

    int NITORYU_CALL apiGetVersion(char* out, unsigned outSize)
    {
        return copyOut(g_version, out, outSize);
    }

    int NITORYU_CALL apiIsDual(void* character)
    {
        if (!g_open)
            return NITORYU_E_OFF;
        if (character == NULL)
            return NITORYU_E_ARG;
        if (t_inRule > 0)
            return NITORYU_E_BUSY;
        try
        {
            return stance::evaluate(static_cast<Character*>(character), NULL).dual ? 1 : 0;
        }
        catch (...)
        {
            return NITORYU_E_FAULT;
        }
    }

    int NITORYU_CALL apiGetInfo(void* character, NitoryuInfo* out)
    {
        if (!g_open)
            return NITORYU_E_OFF;
        if (character == NULL || out == NULL || out->size < (int)(2 * sizeof(int)))
            return NITORYU_E_ARG;
        if (t_inRule > 0)
            return NITORYU_E_BUSY;
        try
        {
            Character* c = static_cast<Character*>(character);
            stance::Info s = stance::evaluate(c, NULL);
            NitoryuInfo full;
            memset(&full, 0, sizeof(full));
            full.dual = s.dual ? 1 : 0;
            full.reason = reasonCode(s.reason);
            if (s.dual)
            {
                full.rank = s.tier;
                full.pathRank = s.tierByDp;
                full.offFromHip = s.offFromHip ? 1 : 0;
                full.optedIn = s.optedIn ? 1 : 0;
                full.path = s.dp;
                full.dualWielding = s.ambi;
                full.dualWieldingInFight = s.ambiEff;
                full.mainWeapon = s.main;
                full.offWeapon = s.off;
            }
            else if (full.reason != NITORYU_REASON_NOT_READY)
            {
                // the evaluation read the character without a fault, so the skill can be read too
                ambidex::Effective e = ambidex::effective(c);
                full.dualWielding = e.a;
                full.dualWieldingInFight = e.eff;
            }
            int n = out->size < (int)sizeof(full) ? out->size : (int)sizeof(full);
            full.size = n;
            memcpy(out, &full, (size_t)n);
            return NITORYU_OK;
        }
        catch (...)
        {
            return NITORYU_E_FAULT;
        }
    }

    const char* NITORYU_CALL apiReasonName(int reason)
    {
        switch (reason)
        {
        case NITORYU_REASON_NPC:        return "npc";
        case NITORYU_REASON_NOT_READY:  return "not_ready";
        case NITORYU_REASON_OTHER:      return "other";
        default:
            break;
        }
        for (int i = 0; i < REASON_COUNT; ++i)
            if (REASONS[i].code == reason)
                return REASONS[i].stance;
        return "other";
    }

    int NITORYU_CALL apiGetSetting(const char* fcsKey, char* out, unsigned outSize)
    {
        if (!g_open)
            return NITORYU_E_OFF;
        if (fcsKey == NULL || out == NULL || outSize == 0)
            return NITORYU_E_ARG;
        if (GetCurrentThreadId() != diag::mainThread())
            return NITORYU_E_THREAD;
        try
        {
            for (int id = 0; id < cfg::COUNT; ++id)
                if (strcmp(cfg::fcsKey((cfg::Id)id), fcsKey) == 0)
                    return copyOut(cfg::valueText((cfg::Id)id).c_str(), out, outSize);
            return NITORYU_E_ARG;
        }
        catch (...)
        {
            return NITORYU_E_FAULT;
        }
    }

    int NITORYU_CALL apiInvalidate(void* character)
    {
        if (!g_open)
            return NITORYU_E_OFF;
        if (character == NULL)
            return NITORYU_E_ARG;
        if (t_inRule > 0)
            return NITORYU_E_BUSY;
        try
        {
            stance::invalidate(static_cast<Character*>(character));
            return NITORYU_OK;
        }
        catch (...)
        {
            return NITORYU_E_FAULT;
        }
    }

    int NITORYU_CALL apiSubscribe(const char* addonName, int eventType, NitoryuEventFn fn, void* userData)
    {
        if (!g_open)
            return NITORYU_E_OFF;
        if (fn == NULL || eventType < NITORYU_EV_ALL || eventType > NITORYU_EV_CHARACTER_GONE)
            return NITORYU_E_ARG;
        const char* name = addonName != NULL && addonName[0] != '\0' ? addonName
                           : (g_initModule != NULL && GetCurrentThreadId() == diag::mainThread() ? g_initModule : "?");
        Lock l;
        LONG n = g_subCount;
        if (n >= MAX_SUBS)
            return NITORYU_E_FULL;
        Sub& s = g_subs[n];
        s.type = eventType;
        s.fn = fn;
        s.userData = userData;
        copyText(s.addon, sizeof(s.addon), name);
        s.dead = 0;
        InterlockedExchange(&g_subCount, n + 1);
        return NITORYU_OK;
    }

    int NITORYU_CALL apiRegisterPairRule(const char* addonName, NitoryuPairRuleFn fn, void* userData)
    {
        if (!g_open)
            return NITORYU_E_OFF;
        if (fn == NULL)
            return NITORYU_E_ARG;
        const char* name = addonName != NULL && addonName[0] != '\0' ? addonName
                           : (g_initModule != NULL && GetCurrentThreadId() == diag::mainThread() ? g_initModule : "?");
        Lock l;
        LONG n = g_ruleCount;
        if (n >= MAX_RULES)
            return NITORYU_E_FULL;
        Rule& r = g_rules[n];
        r.fn = fn;
        r.userData = userData;
        copyText(r.addon, sizeof(r.addon), name);
        r.dead = 0;
        InterlockedExchange(&g_ruleCount, n + 1);
        return NITORYU_OK;
    }

    int NITORYU_CALL apiRegisterMoveFilter(const char* addonName, NitoryuMoveFilterFn fn, void* userData)
    {
        if (!g_open)
            return NITORYU_E_OFF;
        if (fn == NULL)
            return NITORYU_E_ARG;
        const char* name = addonName != NULL && addonName[0] != '\0' ? addonName
                           : (g_initModule != NULL && GetCurrentThreadId() == diag::mainThread() ? g_initModule : "?");
        Lock l;
        LONG n = g_filterCount;
        if (n >= MAX_FILTERS)
            return NITORYU_E_FULL;
        Filter& f = g_filters[n];
        f.fn = fn;
        f.userData = userData;
        copyText(f.addon, sizeof(f.addon), name);
        f.dead = 0;
        InterlockedExchange(&g_filterCount, n + 1);
        return NITORYU_OK;
    }

    const NitoryuApi g_table = {
        NITORYU_API_VERSION,
        (int)sizeof(NitoryuApi),
        &apiGetVersion,
        &apiIsDual,
        &apiGetInfo,
        &apiReasonName,
        &apiGetSetting,
        &apiInvalidate,
        &apiSubscribe,
        &apiRegisterPairRule,
        &apiRegisterMoveFilter,
    };

    // ---- the addons ----

    struct Found
    {
        NitoryuAddon_InitFn fn;
        char name[64];
    };
    const int MAX_ADDONS = 16;

    // Main loop, once: every loaded module that exports NitoryuAddon_Init gets the table. A module found is
    // kept loaded for the session (we hold its callbacks).
    int findAddons()
    {
        HMODULE self = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&g_table), &self);
        HANDLE snap = INVALID_HANDLE_VALUE;
        for (int tries = 0; tries < 4 && snap == INVALID_HANDLE_VALUE; ++tries)
        {
            snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
            if (snap == INVALID_HANDLE_VALUE && GetLastError() != ERROR_BAD_LENGTH)
                break;
        }
        if (snap == INVALID_HANDLE_VALUE)
        {
            kit::Diag("api_scan_failed").kv("error", (int)GetLastError()).emit();
            return 0;
        }
        static Found found[MAX_ADDONS];
        int n = 0;
        MODULEENTRY32W me;
        me.dwSize = sizeof(me);
        for (BOOL ok = Module32FirstW(snap, &me); ok && n < MAX_ADDONS; ok = Module32NextW(snap, &me))
        {
            if (me.hModule == NULL || me.hModule == self)
                continue;
            FARPROC p = GetProcAddress(me.hModule, NITORYU_ADDON_INIT_NAME);
            if (p == NULL)
                continue;
            HMODULE pinned = NULL;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(p), &pinned)
                || pinned == NULL)
                continue;       // it was unloaded in between
            found[n].fn = reinterpret_cast<NitoryuAddon_InitFn>(p);
            found[n].name[0] = '\0';
            WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, found[n].name, sizeof(found[n].name), NULL, NULL);
            found[n].name[sizeof(found[n].name) - 1] = '\0';
            ++n;
        }
        CloseHandle(snap);
        for (int i = 0; i < n; ++i)
        {
            LONG before = g_subCount, rulesBefore = g_ruleCount, filtersBefore = g_filterCount;
            g_initModule = found[i].name;
            bool ok = callInit(found[i].fn, &g_table);
            g_initModule = NULL;
            if (!ok)
                diag::count("api_callback_fault");
            kit::Diag("api_addon").kv("module", found[i].name).kv("subscriptions", (int)(g_subCount - before))
                .kv("pair_rules", (int)(g_ruleCount - rulesBefore)).kv("move_filters", (int)(g_filterCount - filtersBefore))
                .kv("init", ok ? "ok" : "fault").emit();
        }
        return n;
    }

    // Main loop: the setting `nitoryu addons`, once it can be read; then the table opens and the addons are
    // called.
    void decide()
    {
        if (!cfg::recordFound() && GetTickCount() - g_startTick < DECIDE_WAIT_MS)
            return;
        g_decided = true;
        if (!cfg::b(cfg::ADDONS))
        {
            kit::Diag("api_config").kv("addons", 0).emit();
            return;
        }
        InterlockedExchange(&g_open, 1);
        int n = findAddons();
        kit::Diag("api_config").kv("addons", 1).kv("api_version", NITORYU_API_VERSION).kv("found", n)
            .kv("subscriptions", (int)g_subCount).kv("pair_rules", (int)g_ruleCount)
            .kv("move_filters", (int)g_filterCount).emit();
    }

    void deliver(const Queued& q)
    {
        LONG n = g_subCount;
        for (LONG i = 0; i < n; ++i)
        {
            Sub& s = g_subs[i];
            if (s.dead || (s.type != NITORYU_EV_ALL && s.type != q.type))
                continue;
            NitoryuEvent ev;        // a copy for each subscriber
            ev.size = (int)sizeof(ev);
            ev.type = q.type;
            ev.character = q.c;
            ev.other = q.other;
            ev.weapon = q.weapon;
            ev.value = q.value;
            ev.amount = q.amount;
            ev.text = q.text;
            if (callEvent(s.fn, &ev, s.userData))
                continue;
            InterlockedExchange(&s.dead, 1);
            diag::count("api_callback_fault");
            kit::Diag("api_callback_fault").kv("addon", s.addon).kv("kind", "event").kv("event", q.type).emit();
        }
    }
}

namespace api
{
    void init()
    {
        InitializeCriticalSection(&g_lock);
        InterlockedExchange(&g_ready, 1);
    }

    void start(const char* version)
    {
        copyText(g_version, sizeof(g_version), version);
        g_startTick = GetTickCount();
        InterlockedExchange(&g_started, 1);
    }

    void tick()
    {
        if (!g_started)
            return;
        if (!g_decided)
            decide();
        if (!listening())
            return;
        static Queued batch[RING];      // main loop only
        int n = 0;
        unsigned dropped = 0;
        {
            Lock l;
            while (n < RING && g_ring.pop(batch[n]))
                ++n;
            dropped = g_ring.takeDropped();
        }
        for (unsigned i = 0; i < dropped && i < 64; ++i)
            diag::count("api_events_dropped");
        for (int i = 0; i < n; ++i)
            deliver(batch[i]);
        // the "dual off" that waited out its hold: told now (the next frame delivers it)
        DWORD now = GetTickCount();
        Lock l;
        if (g_offWaiting <= 0)
            return;
        for (std::map<Character*, Seen>::iterator it = g_seen.begin(); it != g_seen.end(); ++it)
        {
            Seen& e = it->second;
            if (e.offSince == 0 || now - e.offSince < OFF_HOLD_MS)
                continue;
            g_ring.push(make(NITORYU_EV_DUAL_OFF, it->first, NULL, NULL, reasonCode(e.offReason), 0.0f, e.offReason));
            e.dual = false;
            e.offSince = 0;
            --g_offWaiting;
        }
    }

    void observe(Character* c, const stance::Info& s)
    {
        if (!listening() || c == NULL || s.reason == NULL || strcmp(s.reason, "fault") == 0)
            return;
        try
        {
            int level = (int)s.ambi;
            Lock l;
            std::map<Character*, Seen>::iterator it = g_seen.find(c);
            if (it == g_seen.end())
            {
                if (!s.dual)
                    return;
                Seen first = { true, s.tier, level, 0, "" };
                g_seen[c] = first;
                g_ring.push(make(NITORYU_EV_DUAL_ON, c, NULL, NULL, s.tier, 0.0f, ""));
                return;
            }
            Seen& e = it->second;
            if (!s.dual)
            {
                // told as dual: the off waits out its hold (the tick tells it); the reason is the latest one
                if (e.dual)
                {
                    if (e.offSince == 0)
                    {
                        DWORD now = GetTickCount();
                        e.offSince = now != 0 ? now : 1;
                        ++g_offWaiting;
                    }
                    e.offReason = s.reason;
                }
                return;
            }
            if (e.offSince != 0)
            {
                e.offSince = 0;         // back before the hold ran out: nothing is told
                --g_offWaiting;
                diag::count("api_dual_off_held");
            }
            if (!e.dual)
                g_ring.push(make(NITORYU_EV_DUAL_ON, c, NULL, NULL, s.tier, 0.0f, ""));
            else
            {
                if (s.tier != e.rank)
                    g_ring.push(make(NITORYU_EV_RANK_CHANGED, c, NULL, NULL, s.tier, (float)e.rank, ""));
                if (level != e.level)
                    g_ring.push(make(NITORYU_EV_DUAL_WIELDING_CHANGED, c, NULL, NULL, 0, s.ambi, ""));
            }
            e.dual = true;
            e.rank = s.tier;
            e.level = level;
        }
        catch (...)
        {
            diag::count("api_observe_fault");
        }
    }

    void offHandHit(Character* attacker, Character* victim, Weapon* off, int moveKind, const char* track)
    {
        if (listening())
            push(make(NITORYU_EV_OFF_HAND_HIT, attacker, victim, off, moveKind, 0.0f, track));
    }

    void offHandParry(Character* defender, Character* attacker)
    {
        if (listening())
            push(make(NITORYU_EV_OFF_HAND_PARRY, defender, attacker, NULL, 0, 0.0f, ""));
    }

    void special(Character* c, int moveKind, const char* tag)
    {
        if (listening())
            push(make(NITORYU_EV_SPECIAL, c, NULL, NULL, moveKind, 0.0f, tag));
    }

    bool hasPairRules() { return g_open != 0 && g_ruleCount > 0; }

    bool hasMoveFilters() { return g_open != 0 && g_filterCount > 0; }

    int moveFilter(const MoveAsk& m)
    {
        LONG n = g_filterCount;
        if (!g_open || n == 0 || t_inRule > 0)
            return NITORYU_FILTER_DEFAULT;
        NitoryuMoveQuery q;
        memset(&q, 0, sizeof(q));
        q.size = (int)sizeof(q);
        q.kind = m.kind;
        q.mainHand = m.mainHand ? 1 : 0;
        q.categoryMain = m.categoryMain;
        q.categoryOff = m.categoryOff;
        q.twoHandedPair = m.twoHandedPair ? 1 : 0;
        q.listed = m.listed ? 1 : 0;
        q.character = m.c;
        q.mainWeapon = m.main;
        q.offWeapon = m.off;
        q.record = m.record;
        q.recordId = m.recordId != NULL ? m.recordId : "";
        q.track = m.track != NULL ? m.track : "";
        int result = NITORYU_FILTER_DEFAULT;
        for (LONG i = 0; i < n; ++i)
        {
            Filter& f = g_filters[i];
            if (f.dead)
                continue;
            NitoryuMoveQuery asked = q;         // a copy for each filter
            int answer = NITORYU_FILTER_DEFAULT;
            ++t_inRule;
            bool ok = callFilter(f.fn, &asked, f.userData, &answer);
            --t_inRule;
            if (!ok)
            {
                InterlockedExchange(&f.dead, 1);
                diag::count("api_callback_fault");
                kit::Diag("api_callback_fault").kv("addon", f.addon).kv("kind", "move_filter").emit();
                continue;
            }
            if (answer == NITORYU_FILTER_REFUSE)
                return NITORYU_FILTER_REFUSE;
            if (answer == NITORYU_FILTER_ALLOW)
                result = NITORYU_FILTER_ALLOW;
        }
        return result;
    }

    int pairRules(Character* c, Weapon* a, Weapon* b, const core::PairFacts& f, const core::PairVerdict& own,
                  bool player, stance::PairTuning& t)
    {
        LONG n = g_ruleCount;
        if (!g_open || n == 0 || t_inRule > 0)
            return core::ANSWER_DEFAULT;
        NitoryuPairQuery q;
        memset(&q, 0, sizeof(q));
        q.size = (int)sizeof(q);
        q.allowed = own.allowed ? 1 : 0;
        q.reason = reasonCode(core::pairReasonName(own.reason));
        q.categoryA = f.a.category;
        q.categoryB = f.b.category;
        q.twoHandedA = f.a.twoHandedOnly ? 1 : 0;
        q.twoHandedB = f.b.twoHandedOnly ? 1 : 0;
        q.player = player ? 1 : 0;
        q.character = c;
        q.weaponA = a;
        q.weaponB = b;
        int result = core::ANSWER_DEFAULT;
        const Rule* by = NULL;
        NitoryuPairAnswer best;
        memset(&best, 0, sizeof(best));
        for (LONG i = 0; i < n; ++i)
        {
            Rule& r = g_rules[i];
            if (r.dead)
                continue;
            NitoryuPairQuery asked = q;         // a copy for each rule
            NitoryuPairAnswer ans;
            ans.size = (int)sizeof(ans);
            ans.answer = NITORYU_PAIR_DEFAULT;
            ans.weight = ans.attackSpeed = ans.blockSpeed = ans.offHandDamage = 1.0f;
            ++t_inRule;
            bool ok = callRule(r.fn, &asked, &ans, r.userData);
            --t_inRule;
            if (!ok)
            {
                InterlockedExchange(&r.dead, 1);
                diag::count("api_callback_fault");
                kit::Diag("api_callback_fault").kv("addon", r.addon).kv("kind", "pair_rule").emit();
                continue;
            }
            if (ans.answer == NITORYU_PAIR_REFUSE)
            {
                result = core::ANSWER_REFUSE;
                by = &r;
                break;
            }
            if ((ans.answer == NITORYU_PAIR_ALLOW || ans.answer == NITORYU_PAIR_ALLOW_TWO_HANDED) && by == NULL)
            {
                result = ans.answer;
                best = ans;
                by = &r;
            }
        }
        if (by == NULL)
            return core::ANSWER_DEFAULT;
        if (result != core::ANSWER_REFUSE)
        {
            t.answer = result;
            t.weight = pairNumber(best.weight);
            t.attackSpeed = pairNumber(best.attackSpeed);
            t.blockSpeed = pairNumber(best.blockSpeed);
            t.offDamage = pairNumber(best.offHandDamage);
        }
        // one line for each distinct decision: the rule is asked every frame from the inventory
        bool first = false;
        {
            int lo = q.categoryA < q.categoryB ? q.categoryA : q.categoryB;
            int hi = q.categoryA < q.categoryB ? q.categoryB : q.categoryA;
            unsigned key = ((unsigned)(by - g_rules) << 24) | ((unsigned)(lo & 0xFF) << 16) | ((unsigned)(hi & 0xFF) << 8)
                           | (unsigned)(result & 0xFF);
            Lock l;
            int i = 0;
            while (i < g_ruleSeenCount && g_ruleSeen[i] != key)
                ++i;
            if (i == g_ruleSeenCount && g_ruleSeenCount < MAX_RULE_LINES)
            {
                g_ruleSeen[g_ruleSeenCount++] = key;
                first = true;
            }
        }
        if (first)
            kit::Diag("api_pair_rule").kv("addon", by->addon).kv("answer", result).kv("category_a", q.categoryA)
                .kv("category_b", q.categoryB).kv("own_allowed", q.allowed).kv("weight", t.weight)
                .kv("attack_speed", t.attackSpeed).kv("block_speed", t.blockSpeed).kv("off_damage", t.offDamage).emit();
        return result;
    }

    void forget(Character* c)
    {
        if (!g_ready || g_subCount == 0)
            return;
        Lock l;
        std::map<Character*, Seen>::iterator it = g_seen.find(c);
        if (it == g_seen.end())
            return;
        if (it->second.offSince != 0)
            --g_offWaiting;
        g_seen.erase(it);
        if (g_open)
            g_ring.push(make(NITORYU_EV_CHARACTER_GONE, c, NULL, NULL, 0, 0.0f, ""));
    }
}

// The way in for a plugin that only reads. NULL until the table is open, and for a version this build does not have.
extern "C" __declspec(dllexport) const NitoryuApi* __cdecl Nitoryu_GetApi(int wantedVersion)
{
    if (!g_open || wantedVersion < 1 || wantedVersion > NITORYU_API_VERSION)
        return NULL;
    return &g_table;
}
