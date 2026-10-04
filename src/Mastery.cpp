#include "kit/input.h"

#include <kenshi/Kenshi.h>
#include <kenshi/Character.h>

#include <cstdlib>
#include <map>
#include <string>

#include "Mastery.h"
#include "Balance.h"
#include "Defense.h"
#include "Diagnostics.h"
#include "Feedback.h"
#include "Lang.h"
#include "Settings.h"
#include "Stance.h"
#include "kit/diag.h"

namespace
{
    const int MAX_ATTACKS = 8;
    const double NEVER = -1.0e18;
    const unsigned MUGEN_SAY_GAP_MS = 3000;

    volatile bool g_enabled = true;

    bool g_flowOn = true;
    int g_flowTier = 4;
    int g_flowAttacks = 3;
    double g_flowWindowMs = 5000.0;
    double g_flowMs = 4000.0;
    double g_flowCooldownMs = 5000.0;
    float g_flowDex = 0.3f;
    float g_flowSpeed = 0.3f;
    float g_flowCap = 0.3f;
    bool g_mugenOn = true;
    int g_mugenTier = 5;
    double g_chainWindowMs = 2500.0;

    struct Flow
    {
        double t[MAX_ATTACKS];
        int n;
        double start, end;
    };

    CRITICAL_SECTION g_lock;
    std::map<Character*, Flow> g_flow;
    std::map<Character*, double> g_chain;

    const double MUGEN_LINGER_MS = 700.0;
    struct ChainShown
    {
        double armed, taken;
    };
    std::map<Character*, ChainShown> g_chainShown;

    struct Lock
    {
        Lock() { EnterCriticalSection(&g_lock); }
        ~Lock() { LeaveCriticalSection(&g_lock); }
    };

    float roll01() { return (float)rand() / ((float)RAND_MAX + 1.0f); }

    bool down(Character* c) { return c->isDown() || c->isUnconcious() || c->isRagdoll() || c->isBeingCarried(); }

    double clampMs(double v) { return v < 0.0 ? 0.0 : v; }
}

namespace mastery
{
    void init()
    {
        InitializeCriticalSection(&g_lock);
        applySettings();
    }

    void applySettings()
    {
        g_flowOn = cfg::b(cfg::FLOW_ON);
        g_flowTier = cfg::i(cfg::FLOW_MIN_TIER);
        int n = cfg::i(cfg::FLOW_ATTACKS);
        g_flowAttacks = n < 1 ? 1 : (n > MAX_ATTACKS ? MAX_ATTACKS : n);
        g_flowWindowMs = clampMs(cfg::d(cfg::FLOW_WINDOW_MS));
        g_flowMs = clampMs(cfg::d(cfg::FLOW_DURATION_MS));
        g_flowCooldownMs = clampMs(cfg::d(cfg::FLOW_COOLDOWN_MS));
        float dex = cfg::f(cfg::FLOW_DEX_BONUS);
        g_flowDex = dex < 0.0f ? 0.0f : (dex > 1.0f ? 1.0f : dex);
        float sp = cfg::f(cfg::FLOW_SPEED);
        g_flowSpeed = sp < 0.0f ? 0.0f : (sp > 1.0f ? 1.0f : sp);
        float cap = cfg::f(cfg::FLOW_CAP_BONUS);
        g_flowCap = cap < 0.0f ? 0.0f : cap;
        g_mugenOn = cfg::b(cfg::MUGEN_ON);
        g_mugenTier = cfg::i(cfg::MUGEN_MIN_TIER);
        g_chainWindowMs = clampMs(cfg::d(cfg::MUGEN_CHAIN_WINDOW_MS));
    }

    void setEnabled(bool on) { g_enabled = on; }

    void forget(Character* c)
    {
        Lock l;
        g_flow.erase(c);
        g_chain.erase(c);
        g_chainShown.erase(c);
    }
}

namespace flow
{
    void onAttacked(Character* victim, Character* attacker)
    {
        if (!g_enabled || !g_flowOn || victim == NULL || attacker == NULL || victim == attacker)
            return;
        stance::Info s = stance::evaluate(victim, NULL);
        if (!s.dual || s.tier < g_flowTier || down(victim))
            return;
        double now = defense::clockMs();
        bool started = false;
        {
            Lock l;
            if (g_flow.size() > 4096)
                g_flow.clear();
            std::map<Character*, Flow>::iterator it = g_flow.find(victim);
            if (it == g_flow.end())
            {
                Flow z;
                z.n = 0;
                z.start = z.end = NEVER;
                it = g_flow.insert(std::make_pair(victim, z)).first;
            }
            Flow& f = it->second;

            if (f.end > NEVER && now >= f.start && now < f.end + g_flowCooldownMs)
            {
                diag::count(now < f.end ? "flow_attack_in_flow" : "flow_attack_in_cooldown");
                return;
            }
            int k = 0;
            for (int i = 0; i < f.n; ++i)
                if (now >= f.t[i] && now - f.t[i] <= g_flowWindowMs)
                    f.t[k++] = f.t[i];
            if (k == MAX_ATTACKS)
            {
                for (int i = 1; i < MAX_ATTACKS; ++i)
                    f.t[i - 1] = f.t[i];
                --k;
            }
            f.t[k++] = now;
            f.n = k;
            if (f.n >= g_flowAttacks)
            {
                f.n = 0;
                f.start = now;
                f.end = now + g_flowMs;
                started = true;
            }
        }
        diag::count("flow_attack_counted");
        if (!started)
            return;
        diag::count("flow_start");

        feedback::say(victim, lang::text("speech.flow").c_str(), 0);
        if (diag::takeDetail())
            kit::Diag("flow").kv("who", victim->displayName).kv("dp", s.dp).kv("tier", s.tier)
                .kv("last_attacker", attacker->displayName).kv("lasts_ms", g_flowMs).emit();
    }

    bool active(Character* c)
    {
        if (!g_enabled || !g_flowOn || c == NULL)
            return false;
        double now = defense::clockMs();
        {
            Lock l;
            std::map<Character*, Flow>::iterator it = g_flow.find(c);
            if (it == g_flow.end() || now < it->second.start || now >= it->second.end)
                return false;
        }
        if (!down(c))
            return true;

        Lock l;
        std::map<Character*, Flow>::iterator it = g_flow.find(c);
        if (it != g_flow.end() && now < it->second.end)
        {
            it->second.end = now;
            diag::count("flow_broken_down");
        }
        return false;
    }

    float dexBonus(Character* c) { return g_flowDex > 0.0f && active(c) ? g_flowDex : 0.0f; }
    float speedFactor(Character* c) { return g_flowSpeed > 0.0f && active(c) ? 1.0f + g_flowSpeed : 1.0f; }
    float capBonus(Character* c) { return g_flowCap > 0.0f && active(c) ? g_flowCap : 0.0f; }

    void shown(std::vector<Shown>& out)
    {
        if (!g_enabled || !g_flowOn || g_flowMs <= 0.0)
            return;
        double now = defense::clockMs();
        Lock l;
        for (std::map<Character*, Flow>::iterator it = g_flow.begin(); it != g_flow.end(); ++it)
        {
            const Flow& f = it->second;
            if (now < f.start || now >= f.end)
                continue;
            Shown s;
            s.who = it->first->getHandle();
            s.left = (float)((f.end - now) / g_flowMs);
            out.push_back(s);
        }
    }
}

namespace mugen
{
    double doubleCooldownMs(Character* c, double base)
    {
        if (!g_enabled || !g_mugenOn || c == NULL)
            return base;
        stance::Info s = stance::evaluate(c, NULL);
        if (!s.dual || s.tier < g_mugenTier)
            return base;
        double m = clampMs((double)balance::at(balance::MUGEN_COOLDOWN, s.dp));
        return m < base ? m : base;
    }

    void onTwinLanded(Character* attacker)
    {
        if (!g_enabled || !g_mugenOn || attacker == NULL)
            return;
        stance::Info s = stance::evaluate(attacker, NULL);
        if (!s.dual || s.tier < g_mugenTier)
            return;
        if (roll01() >= balance::at(balance::MUGEN_CHAIN, s.dp))
        {
            diag::count("mugen_chain_not_rolled");
            return;
        }
        double now = defense::clockMs();
        {
            Lock l;
            if (g_chain.size() > 4096)
                g_chain.clear();
            g_chain[attacker] = now;
            if (g_chainShown.size() > 4096)
                g_chainShown.clear();
            ChainShown cs = { now, NEVER };
            g_chainShown[attacker] = cs;
        }
        diag::count("mugen_chain_armed");
        feedback::say(attacker, lang::text("speech.mugen").c_str(), MUGEN_SAY_GAP_MS);
    }

    bool takeChain(Character* c, double now)
    {
        if (!g_enabled || !g_mugenOn || c == NULL)
            return false;
        double t = NEVER;
        {
            Lock l;
            std::map<Character*, double>::iterator it = g_chain.find(c);
            if (it == g_chain.end())
                return false;
            t = it->second;
            g_chain.erase(it);
        }
        bool fresh = now >= t && now - t <= g_chainWindowMs;
        {
            Lock l;
            std::map<Character*, ChainShown>::iterator s = g_chainShown.find(c);
            if (s != g_chainShown.end())
            {
                if (fresh)
                    s->second.taken = now;
                else
                    g_chainShown.erase(s);
            }
        }
        diag::count(fresh ? "mugen_chain_taken" : "mugen_chain_expired");
        return fresh;
    }

    void shown(std::vector<hand>& out)
    {
        if (!g_enabled || !g_mugenOn)
            return;
        double now = defense::clockMs();
        Lock l;
        for (std::map<Character*, ChainShown>::iterator it = g_chainShown.begin(); it != g_chainShown.end();)
        {
            const ChainShown& s = it->second;
            bool on = s.taken > NEVER ? now >= s.taken && now - s.taken <= MUGEN_LINGER_MS
                                      : now >= s.armed && now - s.armed <= g_chainWindowMs;
            if (!on)
            {
                g_chainShown.erase(it++);
                continue;
            }
            out.push_back(it->first->getHandle());
            ++it;
        }
    }
}
