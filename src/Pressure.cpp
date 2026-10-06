#include "kit/input.h"

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

    bool g_on = true;
    bool g_shout = true;
    float g_points = -1.0f;
    int g_maxStacks = 5;
    double g_decayMs = 3000.0;

    struct Mark
    {
        Character* by;
        int stacks;
        float points;
        double t;
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

    bool g_tempoOn = true;

    float tempoOf(Character* c)
    {
        if (!g_tempoOn || c == NULL)
            return 0.0f;
        stance::Info s = stance::evaluate(c, NULL);
        return s.dual && s.tier >= 3 ? balance::at(balance::TEMPO, s.dp) : 0.0f;
    }

    swordStateEnum (*who_orig)(CombatClassAI*, Character*, float, float&) = NULL;
    swordStateEnum who_hook(CombatClassAI* self, Character* who, float opp, float& ret)
    {
        diag::HookCall hc(diag::HK_WHO_ATTACKS);
        if (!g_enabled || !(g_on || g_tempoOn) || self == NULL || self->me == NULL || who == NULL || who == self->me)
            return who_orig(self, who, opp, ret);
        double now = defense::clockMs();

        float lead = g_on ? masterLead(who, self->me, now) : 0.0f;

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

            Lock l;
            std::map<Character*, Mark>::iterator it = g_marks.find(attacker);
            if (it != g_marks.end() && it->second.by == victim && it->second.stacks > 0)
            {
                it->second.stacks = 0;
                diag::count("pressure_broken");
            }
            return;
        }

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
            if (m.by != attacker)
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
