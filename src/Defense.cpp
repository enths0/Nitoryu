#include "kit/input.h"

#include "kit/log.h"
#include <core/Functions.h>

#include <kenshi/Kenshi.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/GameData.h>
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/Damages.h>
#include <kenshi/util/hand.h>
#include <kenshi/combat/CombatClass.h>
#include <kenshi/combat/CombatTechniqueData.h>

#include <cmath>
#include <cstdlib>
#include <map>
#include <string>

#include "Defense.h"
#include "Pressure.h"
#include "Mastery.h"
#include "Blades.h"
#include "Combat.h"
#include "Stance.h"
#include "Settings.h"
#include "Balance.h"
#include "Diagnostics.h"
#include "kit/diag.h"
#include "kit/fcs.h"
#include "Hooks.h"

namespace
{

    const double COMBO_GAP_MS = 3000.0;
    const double FOE_TTL_MS = 3000.0;
    const int RECENT = 4;
    const int GUARD_DETAIL_BUDGET = 150;

    volatile bool g_enabled = true;

    bool g_guardOn = true;
    bool g_counterOn = true;
    double g_cooldownMs = 1000.0;
    double g_counterMs = 1500.0;
    float g_counterChance = -1.0f;

    struct Impact
    {
        Character* who;
        CombatTechniqueData* tech;
        int combo;
        double t;
    };

    struct Defender
    {
        Impact recent[RECENT];
        int next;

        hand foe;
        double foeT;
        hand lastBlocked;
        double lastBlockedT;
        double lastParryT;
        double mainBlockT;
        bool parryBack, mainBlockBack;
        double counterT;
        double seenT;
    };

    CRITICAL_SECTION g_lock;
    std::map<Character*, Defender> g_def;
    int g_detailLeft = GUARD_DETAIL_BUDGET;

    struct Lock
    {
        Lock() { EnterCriticalSection(&g_lock); }
        ~Lock() { LeaveCriticalSection(&g_lock); }
    };

    float g_cosArc = -0.342f;
    const int AXIS_SAMPLES = 24;
    double g_axisSum[4] = { 0.0, 0.0, 0.0, 0.0 };
    int g_axisN = 0;
    int g_axis = -1;
    const Ogre::Vector3 AXES[4] = { Ogre::Vector3::UNIT_Z, Ogre::Vector3::NEGATIVE_UNIT_Z,
                                    Ogre::Vector3::UNIT_X, Ogre::Vector3::NEGATIVE_UNIT_X };
    const char* AXIS_NAME[4] = { "+z", "-z", "+x", "-x" };

    bool flatDir(const Ogre::Vector3& a, const Ogre::Vector3& b, Ogre::Vector3& out)
    {
        out = b - a;
        out.y = 0.0f;
        return out.normalise() > 0.001f;
    }

    void learnAxis(Character* me, Character* foe)
    {
        if (g_axis >= 0 || me == NULL || foe == NULL)
            return;
        Ogre::Vector3 to;
        if (!flatDir(me->getPosition(), foe->getPosition(), to))
            return;
        Ogre::Quaternion q = me->getOrientation();
        for (int i = 0; i < 4; ++i)
        {
            Ogre::Vector3 f = q * AXES[i];
            f.y = 0.0f;
            if (f.normalise() > 0.001f)
                g_axisSum[i] += f.dotProduct(to);
        }
        if (++g_axisN < AXIS_SAMPLES)
            return;
        int best = 0;
        for (int i = 1; i < 4; ++i)
            if (g_axisSum[i] > g_axisSum[best])
                best = i;
        double mean = g_axisSum[best] / g_axisN;
        if (mean < 0.6)
        {

            diag::count("guard_axis_unclear");
            for (int i = 0; i < 4; ++i)
                g_axisSum[i] = 0.0;
            g_axisN = 0;
            return;
        }
        g_axis = best;
        kit::Diag("guard_axis").kv("axis", AXIS_NAME[best]).kv("mean_dot", mean).kv("samples", g_axisN).emit();
    }

    bool inArc(Character* me, Character* who, float& dotOut)
    {
        int axis;
        {
            Lock l;
            axis = g_axis;
        }
        dotOut = 2.0f;
        if (axis < 0)
        {
            diag::count("guard_arc_unlearned");
            return true;
        }
        Ogre::Vector3 to;
        if (!flatDir(me->getPosition(), who->getPosition(), to))
            return true;
        Ogre::Vector3 f = me->getOrientation() * AXES[axis];
        f.y = 0.0f;
        if (f.normalise() <= 0.001f)
            return true;
        dotOut = f.dotProduct(to);
        return dotOut >= g_cosArc;
    }

    LARGE_INTEGER g_freq, g_lastQpc;
    double g_gameMs = 0.0;
    bool g_clockStarted = false;

    float gameSpeed()
    {
        if (ou == NULL)
            return 1.0f;
        if (ou->isPaused())
            return 0.0f;
        float v = ou->getFrameSpeedMultiplier();
        return v >= 0.0f && v <= 20.0f ? v : 1.0f;
    }

    defense::Constants g_const = { false, 70.0f, 0.0f, 200.0f, 0.0f, 200.0f, 1.0f };

    float blockBase()
    {
        float v = g_const.found ? g_const.blockBase : 70.0f;
        return v > 1.0f && v <= 100.0f ? v : 70.0f;
    }

    double advanceClock()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (g_clockStarted)
        {
            double dt = 1000.0 * (double)(now.QuadPart - g_lastQpc.QuadPart) / (double)g_freq.QuadPart;
            if (dt > 0.0)
                g_gameMs += dt * gameSpeed();
        }
        g_clockStarted = true;
        g_lastQpc = now;
        return g_gameMs;
    }

    Defender& stateOf(Character* c, double now)
    {
        if (g_def.size() > 2048)
        {
            for (std::map<Character*, Defender>::iterator it = g_def.begin(); it != g_def.end();)
            {
                if (now - it->second.seenT > 60000.0)
                    g_def.erase(it++);
                else
                    ++it;
            }
        }
        std::map<Character*, Defender>::iterator it = g_def.find(c);
        if (it == g_def.end())
        {
            Defender d;
            for (int i = 0; i < RECENT; ++i)
            {
                d.recent[i].who = NULL;
                d.recent[i].tech = NULL;
                d.recent[i].combo = -1;
                d.recent[i].t = -1e18;
            }
            d.next = 0;
            d.foeT = -1e18;
            d.lastBlockedT = -1e18;
            d.lastParryT = -1e18;
            d.mainBlockT = -1e18;
            d.parryBack = d.mainBlockBack = false;
            d.counterT = -1.0;
            it = g_def.insert(std::make_pair(c, d)).first;
        }
        it->second.seenT = now;
        return it->second;
    }

    bool takeGuardDetail()
    {
        Lock l;
        if (g_detailLeft <= 0)
            return false;
        --g_detailLeft;
        return true;
    }

    std::string nameOf(Character* c) { return c != NULL ? c->displayName : std::string("<null>"); }

    __declspec(thread) Character* t_victim = NULL;
    __declspec(thread) Character* t_attacker = NULL;
    __declspec(thread) CombatTechniqueData* t_attack = NULL;
    __declspec(thread) int t_combo = -1;
    __declspec(thread) Character* t_offParry = NULL;

    __declspec(thread) bool t_seeded = false;
    float roll01()
    {
        if (!t_seeded)
        {
            srand((unsigned)GetTickCount() ^ ((unsigned)GetCurrentThreadId() << 8));
            t_seeded = true;
        }
        return (float)rand() / (float)(RAND_MAX + 1);
    }

    void armCounter(Defender& d, int tier, double now)
    {
        if (g_counterOn && tier >= cfg::i(cfg::COUNTER_MIN_TIER))
        {
            d.counterT = now;
            diag::count("counter_armed");
        }
    }

    bool (*initBlock_orig)(CombatClass*, bool) = NULL;
    bool initBlock_hook(CombatClass* self, bool isStumbling)
    {
        diag::HookCall hc(diag::HK_INIT_BLOCK);
        bool r = initBlock_orig(self, isStumbling);
        if (!g_enabled || !g_guardOn || self == NULL || self->me == NULL)
            return r;
        Character* foe = self->blockingTarget;
        if (foe == NULL)
            foe = self->blockingTargetH.getCharacter();
        if (foe == NULL)
            return r;
        if (g_axis < 0)
        {
            Lock l;
            learnAxis(self->me, foe);
        }
        stance::Info s = stance::evaluate(self->me, NULL);
        if (!s.dual || s.tier < cfg::i(cfg::GUARD_MIN_TIER))
            return r;
        hc.ours();
        diag::count(self->blockingTarget != NULL ? "guard_foe_captured" : "guard_foe_captured_h");
        Lock l;
        double now = advanceClock();
        Defender& d = stateOf(self->me, now);
        d.foe = foe->getHandle();
        d.foeT = now;
        return r;
    }

    HitMaterialType (*iHit_orig)(CombatClass*, CutDirection, Damages&, Character*) = NULL;
    HitMaterialType offHandGuard(CombatClass* self, CutDirection dir, Damages& damage, Character* who, bool& ours)
    {
        HitMaterialType r = iHit_orig(self, dir, damage, who);
        if (!g_enabled || self == NULL || who == NULL || self->me == NULL || who == self->me)
            return r;
        Character* me = self->me;
        stance::Info s = stance::evaluate(me, NULL);
        if (!s.dual)
            return r;
        ours = true;

        bool haveCtx = t_victim == me && t_attacker == who;
        CombatTechniqueData* tech = haveCtx ? t_attack : NULL;
        int combo = haveCtx ? t_combo : -1;
        if (!haveCtx)
            diag::count("guard_no_context");

        float facingDot = 2.0f;
        const bool behind = !inArc(me, who, facingDot);
        const bool backRank = s.tier >= cfg::i(cfg::BACK_GUARD_MIN_TIER);

        bool comboHit = false;
        hand foeH;
        bool haveFoe = false;
        bool foeFallback = false;
        double now = 0.0, sinceParry = 0.0;
        {
            Lock l;
            now = advanceClock();
            Defender& d = stateOf(me, now);
            if (tech != NULL && combo >= 1)
            {
                for (int i = 0; i < RECENT; ++i)
                {
                    const Impact& p = d.recent[i];
                    if (p.who == who && p.tech == tech && p.combo < combo && now - p.t < COMBO_GAP_MS)
                        comboHit = true;
                }
            }
            Impact& slot = d.recent[d.next];
            d.next = (d.next + 1) % RECENT;
            slot.who = who;
            slot.tech = tech;
            slot.combo = combo;
            slot.t = now;

            if (r == HIT_SWORD)
            {

                d.lastBlocked = who->getHandle();
                d.lastBlockedT = now;
                d.mainBlockT = now;
                d.mainBlockBack = behind && backRank;
                if (d.mainBlockBack)
                    diag::count("guard_main_block_back");
                armCounter(d, s.tier, now);
                diag::count("guard_main_block");
                return r;
            }
            if (now - d.foeT < FOE_TTL_MS)
            {
                foeH = d.foe;
                haveFoe = true;
            }
            else if (now - d.lastBlockedT < FOE_TTL_MS)
            {
                foeH = d.lastBlocked;
                haveFoe = true;
                foeFallback = true;
            }
            sinceParry = now - d.lastParryT;
        }
        Character* foe = haveFoe ? foeH.getCharacter() : NULL;
        if (haveFoe && foe == NULL)
            diag::count("guard_foe_gone");

        if (r != HIT_FLESH || !g_guardOn)
            return r;
        if (s.tier < cfg::i(cfg::GUARD_MIN_TIER))
        {
            diag::count("guard_tier_low");
            return r;
        }

        const char* busy = self->combatState == CHOP_WEAPON ? "guard_own_swing"
                         : self->combatState == STUMBLE ? "guard_staggered"
                         : !self->_isInCombatMode() ? "guard_not_in_combat"
                         : (me->isDown() || me->isUnconcious() || me->isRagdoll() || me->isBeingCarried()) ? "guard_down"
                         : NULL;
        if (busy != NULL)
        {
            diag::count(busy);
            return r;
        }

        if (behind && !backRank)
        {
            diag::count("guard_behind");
            return r;
        }
        if (behind)
            diag::count("guard_back_try");
        bool group = behind || (foe != NULL && who != foe);

        bool mainFoe = !comboHit && !group && s.tier >= cfg::i(cfg::MAIN_GUARD_MIN_TIER);
        if (!comboHit && !group && !mainFoe)
        {
            diag::count(foe == NULL ? "guard_no_foe" : "guard_main_foe_first");
            return r;
        }
        if (group && foeFallback)
            diag::count("guard_foe_fallback");
        const char* trigger = behind ? "back" : group ? "group" : comboHit ? "combo" : "main";
        diag::count(group ? "guard_group_try" : comboHit ? "guard_combo_try" : "guard_main_try");
        if (sinceParry < g_cooldownMs)
        {
            diag::count("guard_cooldown");
            return r;
        }

        CharStats* myStats = me->getStats();
        CharStats* hisStats = who->getStats();
        if (myStats == NULL || hisStats == NULL)
        {
            diag::count("guard_no_stats");
            return r;
        }

        float bc = myStats->_calculateBlockChance(hisStats->getMeleeAttack());
        float baseBc = blockBase();
        float g = balance::at(balance::OFF_HAND_GUARD, s.dp);

        float pairMult = blades::guardMult(s.main, s.off);
        if (pairMult != 1.0f)
        {
            g *= pairMult;
            diag::count("pair_long_short_parry_roll");
        }
        float share = group ? 1.0f : comboHit ? cfg::f(cfg::COMBO_GUARD_SHARE) : cfg::f(cfg::MAIN_GUARD_SHARE);
        if (behind)
            share *= cfg::f(cfg::BACK_GUARD_SHARE);
        float chance = share * g * bc / baseBc;
        bool ok = roll01() < chance;
        if (ok)
        {
            Lock l;
            Defender& d = stateOf(me, now);
            if (now - d.lastParryT < g_cooldownMs)
                ok = false;
            else
            {
                d.lastParryT = now;
                d.parryBack = behind;
                if (behind)
                    diag::count("guard_back_ok");
                t_offParry = me;
                d.lastBlocked = who->getHandle();
                d.lastBlockedT = now;
                armCounter(d, s.tier, now);
            }
        }
        diag::count(ok ? (group ? "guard_group_ok" : comboHit ? "guard_combo_ok" : "guard_main_ok")
                       : (group ? "guard_group_fail" : comboHit ? "guard_combo_fail" : "guard_main_fail"));
        if ((me->isPlayerCharacter() || who->isPlayerCharacter() || ok) && takeGuardDetail())
            kit::Diag("guard").kv("defender", nameOf(me)).kv("attacker", nameOf(who)).kv("trigger", trigger)
                .kv("combo", combo).kv("tech", tech != NULL ? tech->animation : std::string("?"))
                .kv("foe", nameOf(foe)).kv("fallback", foeFallback)
                .kv("chance", chance).kv("bc", bc).kv("base", baseBc).kv("tier", s.tier).kv("dp", s.dp)
                .kv("since_parry_ms", sinceParry > 1e9 ? -1.0 : sinceParry).kv("game_speed", gameSpeed())
                .kv("facing_dot", facingDot)
                .kv("result", ok ? "parried" : "hit").emit();
        return ok ? HIT_SWORD : r;
    }

    HitMaterialType iHit_hook(CombatClass* self, CutDirection dir, Damages& damage, Character* who)
    {
        diag::HookCall hc(diag::HK_I_HIT);
        bool ours = false;
        HitMaterialType r = offHandGuard(self, dir, damage, who, ours);
        if (ours)
            hc.ours();
        if (g_enabled && self != NULL)
        {
            pressure::onImpact(self->me, who, r);
            flow::onAttacked(self->me, who);
            blades::noteAttacker(self->me, who);
        }
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

namespace defense
{

    float headingDot(Character* me, Character* who)
    {
        float d = 2.0f;
        if (me != NULL && who != NULL)
            inArc(me, who, d);
        return d;
    }

    void forget(Character* c)
    {
        Lock l;
        g_def.erase(c);
    }

    void init()
    {
        InitializeCriticalSection(&g_lock);
        g_detailLeft = diag::budget(GUARD_DETAIL_BUDGET);
        QueryPerformanceFrequency(&g_freq);
        applySettings();
    }

    void applySettings()
    {
        g_guardOn = cfg::b(cfg::GUARD_ON);
        g_counterOn = cfg::b(cfg::COUNTER_ON);
        double cd = cfg::d(cfg::GUARD_COOLDOWN_MS);
        g_cooldownMs = cd < 0.0 ? 0.0 : cd;
        double cw = cfg::d(cfg::COUNTER_WINDOW_MS);
        g_counterMs = cw < 0.0 ? 0.0 : cw;
        int arc = cfg::i(cfg::GUARD_ARC_DEG);
        arc = arc < 0 ? 0 : arc > 180 ? 180 : arc;
        g_cosArc = (float)cos((double)arc * 3.14159265358979 / 180.0);
        float c = cfg::f(cfg::COUNTER_CHANCE_FIXED);
        g_counterChance = c < 0.0f ? -1.0f : (c > 1.0f ? 1.0f : c);
    }

    bool installHooks()
    {
        bool ok = true;
        ok &= hook(hooks::add("CombatClass::_iHitYouAreYouHit", "guard", KenshiLib::GetRealAddress(&CombatClass::_iHitYouAreYouHit), iHit_hook, &iHit_orig),
                   "CombatClass::_iHitYouAreYouHit");
        ok &= hook(hooks::add("CombatClass::initialiseBlock", "guard", KenshiLib::GetRealAddress(&CombatClass::initialiseBlock), initBlock_hook, &initBlock_orig),
                   "CombatClass::initialiseBlock");
        char b[160];

        sprintf_s(b, "guard:%s, cooldown %d ms, arc %.0f deg, counter %s (%d ms window, chance %.2f; -1 = by DP)",
                  g_guardOn ? "on" : "off", (int)g_cooldownMs, acos(g_cosArc) * 180.0 / 3.14159265358979,
                  g_counterOn ? "on" : "off", (int)g_counterMs, g_counterChance);
        kit::Diag("guard_config").kv("text", b).emit();
        return ok;
    }

    void setEnabled(bool on) { g_enabled = on; }

    volatile double g_frameMs = 0.0;

    void tick()
    {
        double now;
        {
            Lock l;
            now = advanceClock();
        }
        g_frameMs = now;
    }

    double frameMs() { return g_frameMs; }

    void readConstants()
    {
        if (ou == NULL)
            return;
        GameData* d = ou->gamedata.getDataByName("GLOBAL CONSTANTS", CONSTANTS);
        if (d == NULL)
        {
            diag::count("constants_not_found");
            return;
        }
        Constants c;
        c.found = true;
        c.damageMult = kit::fcsFloat(d, "damage multiplier", 1.0f);
        c.blockBase = kit::fcsFloat(d, "base block chance", 80.0f);
        c.cut1 = kit::fcsFloat(d, "cut damage 1", 20.0f) * c.damageMult;
        c.cut99 = kit::fcsFloat(d, "cut damage 99", 100.0f) * c.damageMult;
        c.blunt1 = kit::fcsFloat(d, "blunt damage 1", 0.0f) * c.damageMult;
        c.blunt99 = kit::fcsFloat(d, "blunt damage 99", 100.0f) * c.damageMult;
        bool first = !g_const.found;
        g_const = c;
        if (!first)
            return;
        kit::Diag("constants").kv("block_base", c.blockBase).kv("cut_99", c.cut99).kv("blunt_99", c.blunt99)
            .kv("cut_1", c.cut1).kv("damage_mult", c.damageMult).emit();
    }

    Constants constants() { return g_const; }

    double clockMs()
    {
        Lock l;
        return advanceClock();
    }

    HitScope::HitScope(Character* victim, Character* attacker, CombatTechniqueData* attack, int comboID)
        : prevVictim(t_victim), prevAttacker(t_attacker), prevAttack(t_attack), prevCombo(t_combo),
          prevParry(t_offParry)
    {
        t_offParry = NULL;
        t_victim = victim;
        t_attacker = attacker;
        t_attack = attack;
        t_combo = comboID;
    }

    HitScope::~HitScope()
    {
        t_victim = prevVictim;
        t_attacker = prevAttacker;
        t_attack = prevAttack;
        t_combo = prevCombo;
        t_offParry = prevParry;
    }

    bool offParried(Character* victim) { return victim != NULL && t_offParry == victim; }

    float counterChance(float dp) { return g_counterChance >= 0.0f ? g_counterChance : balance::at(balance::COUNTER_CHANCE, dp); }

    double lastParryMs(Character* c)
    {
        Lock l;
        std::map<Character*, Defender>::iterator it = g_def.find(c);
        return it != g_def.end() && it->second.lastParryT > -1e17 ? it->second.lastParryT : -1.0;
    }

    double lastMainBlockMs(Character* c)
    {
        Lock l;
        std::map<Character*, Defender>::iterator it = g_def.find(c);
        return it != g_def.end() && it->second.mainBlockT > -1e17 ? it->second.mainBlockT : -1.0;
    }

    bool lastParryBack(Character* c)
    {
        Lock l;
        std::map<Character*, Defender>::iterator it = g_def.find(c);
        return it != g_def.end() && it->second.parryBack;
    }

    bool lastMainBlockBack(Character* c)
    {
        Lock l;
        std::map<Character*, Defender>::iterator it = g_def.find(c);
        return it != g_def.end() && it->second.mainBlockBack;
    }

    bool takeCounter(Character* c)
    {
        if (!g_enabled || !g_counterOn || c == NULL)
            return false;
        Lock l;
        std::map<Character*, Defender>::iterator it = g_def.find(c);
        if (it == g_def.end() || it->second.counterT < 0.0)
            return false;
        double now = advanceClock();
        bool fresh = now - it->second.counterT <= g_counterMs;
        it->second.counterT = -1.0;
        diag::count(fresh ? "counter_taken" : "counter_expired");
        return fresh;
    }
}
