// Pressure. Vanilla flow:
//   CombatClassAI::isInMeiDistance (attacker A, in range) calls the target B's
//   whoAttacksYouOrMe(A, A's getAttackChance, &timer). B compares its own attack
//   chance with A's (weights 1 + "attack chance factor" x difference);
//   the loser blocks. So raising A's chance inside B's call, or lowering B's
//   chance inside A's call (B asking A), tilts the contest the same way.
// Threads:
//   who_hook (CombatClassAI::whoAttacksYouOrMe) - the AI's thread.
//   g_lock guards g_marks; the contest is shifted after the lock is left.

#include "kit/input.h"   // Windows.h with NOMINMAX first

#include "kit/log.h"
#include <core/Functions.h>

#include <kenshi/Kenshi.h>
#include <kenshi/Character.h>
#include <kenshi/combat/CombatClass.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>

#include "Pressure.h"
#include "Defense.h"
#include "Feedback.h"
#include "Lang.h"
#include "Stance.h"
#include "Settings.h"
#include "Balance.h"
#include "Diagnostics.h"
#include "kit/diag.h"
#include "Hooks.h"

namespace
{
    const int DETAIL_BUDGET = 80;
    const unsigned SHOUT_GAP_MS = 15000;

    volatile bool g_enabled = true;
    // copies of the settings (cfg: the table < FCS < the ini), set by applySettings
    bool g_on = true;                   // cfg::PRESSURE_ON ([Pressure] pressure)
    bool g_shout = true;                // [Pressure] shout (the ini only: a test aid)
    float g_points = -1.0f;             // cfg::PRESSURE_POINTS_FIXED ([Pressure] points); < 0: the DP curve
    int g_maxStacks = 5;                // cfg::PRESSURE_MAX_STACKS
    double g_decayMs = 3000.0;          // cfg::PRESSURE_DECAY_MS

    struct Mark
    {
        Character* by;      // the master
        int stacks;
        float points;       // attack levels per stack, from the master's DP when stacked
        double t;           // game time of the last gain (or of the last applied decay step)
    };

    CRITICAL_SECTION g_lock;
    std::map<Character*, Mark> g_marks;
    int g_detailLeft = DETAIL_BUDGET;

    struct Lock
    {
        Lock() { EnterCriticalSection(&g_lock); }
        ~Lock() { LeaveCriticalSection(&g_lock); }
    };

    std::string nameOf(Character* c) { return c != NULL ? c->displayName : std::string("<null>"); }

    // Caller holds the lock: fade one stack per g_decayMs since the last change.
    int current(Mark& m, double now)
    {
        if (m.stacks > 0 && g_decayMs > 0.0 && now - m.t >= g_decayMs)
        {
            int n = (int)((now - m.t) / g_decayMs);
            m.stacks = m.stacks > n ? m.stacks - n : 0;
            m.t += n * g_decayMs;
        }
        return m.stacks;
    }

    // Attack levels to add to the master's side of a contest between a and b
    // (> 0: a is the master of b; < 0: b is the master of a; 0: no pressure).
    float masterLead(Character* a, Character* b, double now)
    {
        Lock l;
        std::map<Character*, Mark>::iterator it = g_marks.find(b);
        if (it != g_marks.end() && it->second.by == a)
            return current(it->second, now) * it->second.points;
        it = g_marks.find(a);
        if (it != g_marks.end() && it->second.by == b)
            return -current(it->second, now) * it->second.points;
        return 0.0f;
    }

    bool takeDetail()
    {
        Lock l;
        if (g_detailLeft <= 0)
            return false;
        --g_detailLeft;
        return true;
    }

    bool g_tempoOn = true;              // [Pressure] tempo

    float tempoOf(Character* c)
    {
        if (!g_tempoOn || c == NULL)
            return 0.0f;
        stance::Info s = stance::evaluate(c, NULL);
        return s.dual && s.tier >= 3 ? balance::at(balance::TEMPO, s.dp) : 0.0f;
    }

    // self (B) decides against `who` (A), whose attack chance is `opp`.
    swordStateEnum (*who_orig)(CombatClassAI*, Character*, float, float&) = NULL;
    swordStateEnum who_hook(CombatClassAI* self, Character* who, float opp, float& ret)
    {
        diag::HookCall hc(diag::HK_WHO_ATTACKS);
        if (!g_enabled || !(g_on || g_tempoOn) || self == NULL || self->me == NULL || who == NULL || who == self->me)
            return who_orig(self, who, opp, ret);
        double now = defense::clockMs();
        // who is the master of self: who's chance goes up; self is the master of who: down.
        float lead = g_on ? masterLead(who, self->me, now) : 0.0f;
        // Tempo, a dual master's standing initiative (by DP), for either side.
        float tempo = tempoOf(who) - tempoOf(self->me);
        if (tempo != 0.0f)
        {
            diag::count(tempo > 0.0f ? "tempo_asker_dual" : "tempo_target_dual");
            lead += tempo;
        }
        if (lead == 0.0f)
            return who_orig(self, who, opp, ret);
        hc.ours();
        float shifted = opp + lead;
        swordStateEnum r = who_orig(self, who, shifted, ret);
        // r is `who`'s next move: CHOP_WEAPON = who attacks, BLOCK = who blocks.
        bool masterAsks = lead > 0.0f;
        bool masterWins = masterAsks ? r == CHOP_WEAPON : r == BLOCK;
        diag::count(masterAsks ? "pressure_contest_master_asks" : "pressure_contest_target_asks");
        if (r == CHOP_WEAPON || r == BLOCK)
            diag::count(masterWins ? "pressure_contest_master_won" : "pressure_contest_target_won");
        if ((self->me->isPlayerCharacter() || who->isPlayerCharacter()) && takeDetail())
            kit::Diag("pressure_contest").kv("self", nameOf(self->me)).kv("who", nameOf(who))
                .kv("opp", opp).kv("lead", lead).kv("result", (int)r).emit();
        return r;
    }

    bool hook(KenshiLib::HookStatus s, const char* what)
    {
        if (s == KenshiLib::SUCCESS)
            return true;
        kit::log::error(std::string("Nitoryu: could not hook ") + what);
        return false;
    }
}

namespace pressure
{
    void init()
    {
        InitializeCriticalSection(&g_lock);
        g_detailLeft = diag::budget(DETAIL_BUDGET);
        std::string path = stance::iniFile();
        g_shout = GetPrivateProfileIntA("Pressure", "shout", 1, path.c_str()) != 0;
        applySettings();
    }

    // From cfg (the table < FCS < the ini); at start and after a change.
    void applySettings()
    {
        g_on = cfg::b(cfg::PRESSURE_ON);
        g_tempoOn = cfg::b(cfg::TEMPO_ON);
        int mx = cfg::i(cfg::PRESSURE_MAX_STACKS);
        g_maxStacks = mx < 1 ? 1 : (mx > 20 ? 20 : mx);
        double dm = cfg::d(cfg::PRESSURE_DECAY_MS);
        g_decayMs = dm < 0.0 ? 0.0 : dm;
        g_points = cfg::f(cfg::PRESSURE_POINTS_FIXED);
    }

    bool installHooks()
    {
        bool ok = hook(hooks::add("CombatClassAI::whoAttacksYouOrMe", "pressure", KenshiLib::GetRealAddress(&CombatClassAI::_NV_whoAttacksYouOrMe), who_hook, &who_orig),
                       "CombatClassAI::whoAttacksYouOrMe");
        char b[160];
        sprintf_s(b, "pressure:%s, max %d stacks, decay %d ms, points %.2f (-1 = by DP)",
                  g_on ? "on" : "off", g_maxStacks, (int)g_decayMs, g_points);
        kit::Diag("pressure_config").kv("text", b).emit();
        return ok;
    }

    void setEnabled(bool on) { g_enabled = on; }

    void onImpact(Character* victim, Character* attacker, HitMaterialType r)
    {
        if (!g_enabled || !g_on || victim == NULL || attacker == NULL || victim == attacker || r == HIT_MISSED)
            return;
        if (r != HIT_SWORD)
        {
            // A hit landed: if the attacker is under the victim's pressure, the pressure breaks.
            Lock l;
            std::map<Character*, Mark>::iterator it = g_marks.find(attacker);
            if (it != g_marks.end() && it->second.by == victim && it->second.stacks > 0)
            {
                it->second.stacks = 0;
                diag::count("pressure_broken");
            }
            return;
        }
        // The victim blocked: a stack if the attacker is a dual fighter at PRESSURE_MIN_TIER+.
        stance::Info s = stance::evaluate(attacker, NULL);
        if (!s.dual || s.tier < cfg::i(cfg::PRESSURE_MIN_TIER))
            return;
        float pts = g_points >= 0.0f ? g_points : balance::at(balance::PRESSURE_POINTS, s.dp);
        double now = defense::clockMs();
        int n = 0;
        {
            Lock l;
            std::map<Character*, Mark>::iterator it = g_marks.find(victim);
            if (it == g_marks.end())
            {
                Mark m = { attacker, 0, pts, now };
                it = g_marks.insert(std::make_pair(victim, m)).first;
            }
            Mark& m = it->second;
            current(m, now);
            if (m.by != attacker)      // a new master takes over this target
            {
                m.by = attacker;
                m.stacks = 0;
            }
            if (m.stacks < g_maxStacks)
                ++m.stacks;
            m.points = pts;
            m.t = now;
            n = m.stacks;
        }
        diag::count("pressure_stack");
        if (n == g_maxStacks)
            diag::count("pressure_full");
        // The visible cue: the target shouts.
        if (g_shout && n == std::min(cfg::i(cfg::PRESSURE_SHOUT_STACKS), g_maxStacks))
        {
            diag::count("pressure_shout");
            feedback::say(victim, lang::text("speech.pressured").c_str(), SHOUT_GAP_MS);
        }
    }

    void forget(Character* c)
    {
        Lock l;
        g_marks.erase(c);
        for (std::map<Character*, Mark>::iterator it = g_marks.begin(); it != g_marks.end();)
        {
            if (it->second.by == c)
                g_marks.erase(it++);
            else
                ++it;
        }
    }

    int stacks(Character* target, Character* master)
    {
        double now = defense::clockMs();
        Lock l;
        std::map<Character*, Mark>::iterator it = g_marks.find(target);
        if (it == g_marks.end() || it->second.by != master)
            return 0;
        return current(it->second, now);
    }

    int maxStacks() { return g_maxStacks; }

    void shown(std::vector<Shown>& out)
    {
        if (!g_enabled || !g_on)
            return;
        double now = defense::clockMs();
        Lock l;
        for (std::map<Character*, Mark>::iterator it = g_marks.begin(); it != g_marks.end(); ++it)
        {
            int n = current(it->second, now);
            if (n <= 0 || it->second.by == NULL)
                continue;
            Shown s;
            s.target = it->first->getHandle();
            s.by = it->second.by->getHandle();
            s.stacks = n;
            out.push_back(s);
        }
    }
}
