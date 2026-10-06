#include "kit/input.h"

#include "kit/log.h"
#include <core/Functions.h>

#include <kenshi/Kenshi.h>
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/Gear.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/combat/CombatClass.h>
#include <kenshi/combat/CombatTechniqueData.h>
#include <kenshi/util/hand.h>

#include <cmath>
#include <cstdlib>
#include <map>
#include <string>

#include "Blades.h"
#include "Balance.h"
#include "Defense.h"
#include "Diagnostics.h"
#include "Feedback.h"
#include "Lang.h"
#include "Settings.h"
#include "kit/diag.h"
#include "kit/fcs.h"
#include "Hooks.h"

namespace
{
    const int MAX_ATTACKERS = 6;
    const double ONE_SWING_MS = 250.0;
    const float FORCE_PART = 10000.0f;
    const unsigned SPLIT_SAY_GAP_MS = 3000;

    volatile bool g_enabled = true;
    bool g_woundHook = false;

    bool g_pairOn = true;
    float g_shortRatio = 0.8f;
    float g_equalRatio = 0.9f;
    float g_guardMult = 1.3f;
    float g_twinMult = 1.3f;
    float g_mixedDefence = 1.0f;
    bool g_scissorOn = true;
    double g_scissorMs = 1000.0;
    bool g_splitOn = true;
    int g_splitTier = 4;
    float g_splitCos = 0.0f;
    float g_splitReach = 1.0f;
    double g_attackerMs = 4000.0;

    struct First
    {
        GameData* part;
        double t;
        bool offBlade;
    };
    typedef std::pair<Character*, Character*> Hit;

    struct Attackers
    {
        hand who[MAX_ATTACKERS];
        double t[MAX_ATTACKERS];
        int n;
    };

    struct LastSplit
    {
        CombatTechniqueData* attack;
        int combo;
        double t;
    };

    CRITICAL_SECTION g_lock;
    std::map<Hit, First> g_first;
    std::map<Character*, Attackers> g_attackers;
    std::map<Character*, LastSplit> g_lastSplit;

    struct Lock
    {
        Lock() { EnterCriticalSection(&g_lock); }
        ~Lock() { LeaveCriticalSection(&g_lock); }
    };

    float roll01() { return (float)rand() / ((float)RAND_MAX + 1.0f); }

    bool down(Character* c) { return c->isDead() || c->isDown() || c->isUnconcious() || c->isRagdoll() || c->isBeingCarried(); }

    bool same(const hand& h, Character* c) { return h == static_cast<const RootObjectBase*>(c); }

    float lengthOf(Weapon* w)
    {
        if (w == NULL || w->data == NULL)
            return 0.0f;
        float own = kit::fcsFloat(w->data, "nitoryu pair length", 0.0f);
        if (own > 0.0f)
            return own;
        return (float)kit::fcsInt(w->data, "length", 0);
    }

    __declspec(thread) GameData* t_woundPart = NULL;

    GameData* (*addWound_orig)(MedicalSystem*, bool, CutDirection, Damages&, int&, RootObject*, AttackDirection::Enum&,
                               Harpoon*) = NULL;
    GameData* addWound_hook(MedicalSystem* self, bool lowBlow, CutDirection area, Damages& damage, int& material,
                            RootObject* attacker, AttackDirection::Enum& attackDirection, Harpoon* harpoon)
    {
        diag::HookCall hc(diag::HK_ADD_WOUND);
        GameData* part = addWound_orig(self, lowBlow, area, damage, material, attacker, attackDirection, harpoon);
        t_woundPart = part;
        return part;
    }
}

namespace blades
{
    void init()
    {
        InitializeCriticalSection(&g_lock);
        applySettings();
    }

    void applySettings()
    {
        g_pairOn = cfg::b(cfg::PAIR_ON);
        g_shortRatio = cfg::f(cfg::PAIR_SHORT_RATIO);
        g_equalRatio = cfg::f(cfg::PAIR_EQUAL_RATIO);
        float g = cfg::f(cfg::PAIR_GUARD_MULT);
        g_guardMult = g < 0.0f ? 0.0f : g;
        float t = cfg::f(cfg::PAIR_TWIN_MULT);
        g_twinMult = t < 0.0f ? 0.0f : t;
        g_mixedDefence = cfg::f(cfg::PAIR_MIXED_DEFENCE);
        g_scissorOn = cfg::b(cfg::SCISSOR_ON);
        double w = cfg::d(cfg::SCISSOR_WINDOW_MS);
        g_scissorMs = w < 0.0 ? 0.0 : w;
        g_splitOn = cfg::b(cfg::SPLIT_ON);
        g_splitTier = cfg::i(cfg::SPLIT_MIN_TIER);
        int arc = cfg::i(cfg::SPLIT_ARC_DEG);
        arc = arc < 0 ? 0 : (arc > 360 ? 360 : arc);
        g_splitCos = (float)cos((double)arc * 0.5 * 3.14159265358979 / 180.0);
        float r = cfg::f(cfg::SPLIT_REACH);
        g_splitReach = r < 0.0f ? 0.0f : r;
        double a = cfg::d(cfg::SPLIT_ATTACKER_MS);
        g_attackerMs = a < 0.0 ? 0.0 : a;
    }

    bool installHooks()
    {
        g_woundHook = hooks::add("MedicalSystem::addWound", "scissor", KenshiLib::GetRealAddress(&MedicalSystem::addWound), addWound_hook,
                                         &addWound_orig) == KenshiLib::SUCCESS;
        if (!g_woundHook)
            kit::log::error("Nitoryu: could not hook MedicalSystem::addWound");
        return g_woundHook;
    }

    void setEnabled(bool on) { g_enabled = on; }

    void forget(Character* c)
    {
        Lock l;
        for (std::map<Hit, First>::iterator it = g_first.begin(); it != g_first.end();)
            if (it->first.first == c || it->first.second == c)
                g_first.erase(it++);
            else
                ++it;
        g_attackers.erase(c);
        g_lastSplit.erase(c);
    }

    Pair pairOf(Weapon* a, Weapon* b)
    {
        if (!g_enabled || !g_pairOn)
            return PAIR_PLAIN;
        float la = lengthOf(a), lb = lengthOf(b);
        if (!(la > 0.0f) || !(lb > 0.0f))
            return PAIR_PLAIN;
        float ratio = la < lb ? la / lb : lb / la;
        if (ratio <= g_shortRatio)
            return PAIR_LONG_SHORT;
        return ratio >= g_equalRatio ? PAIR_EQUAL : PAIR_PLAIN;
    }

    const char* pairName(Pair p) { return p == PAIR_LONG_SHORT ? "long_short" : p == PAIR_EQUAL ? "equal" : "plain"; }

    bool mixedTypes(Weapon* a, Weapon* b)
    {
        return g_enabled && g_pairOn && a != NULL && b != NULL && a->category != b->category;
    }

    bool mixedDefence(Weapon* main, Weapon* off)
    {
        Sword* sw = off != NULL ? off->isSword() : NULL;
        return sw != NULL && sw->modDefence > 0 && mixedTypes(main, off)
            && g_mixedDefence > cfg::f(cfg::OFF_WEAPON_BONUS_SHARE);
    }

    float guardMult(Weapon* a, Weapon* b) { return pairOf(a, b) == PAIR_LONG_SHORT ? g_guardMult : 1.0f; }

    float twinMult(Weapon* a, Weapon* b) { return pairOf(a, b) == PAIR_EQUAL ? g_twinMult : 1.0f; }

    float offDefenceShare(const stance::Info& s, float bonus, float usual)
    {

        if (bonus <= 0.0f || !mixedTypes(s.main, s.off) || g_mixedDefence <= usual)
            return usual;
        diag::count("pair_mixed_defence");
        return g_mixedDefence;
    }

    Scissor::Scissor(Character* a, Character* v, const stance::Info& s, bool off, bool twin)
        : attacker(a), victim(v), offBlade(off), on(false), second(false), forced(NULL), savedChance(0.0f), wanted(NULL)
    {
        t_woundPart = NULL;
        if (!g_enabled || !g_scissorOn || !g_woundHook || !twin || a == NULL || v == NULL || !s.dual)
            return;
        on = true;
        double now = defense::clockMs();
        GameData* part = NULL;
        {
            Lock l;
            std::map<Hit, First>::iterator it = g_first.find(Hit(a, v));

            if (it != g_first.end() && it->second.offBlade != off)
            {
                if (now >= it->second.t && now - it->second.t <= g_scissorMs)
                    part = it->second.part;
                g_first.erase(it);
            }
        }
        if (part == NULL)
            return;
        second = true;
        wanted = part;
        if (roll01() >= balance::at(balance::SCISSOR_CHANCE, s.dp))
        {
            diag::count("scissor_not_rolled");
            return;
        }
        MedicalSystem* med = v->getMedical();
        if (med == NULL)
            return;
        for (uint32_t i = 0; i < med->anatomy.size(); ++i)
        {
            MedicalSystem::HealthPartStatus* p = med->anatomy[i];
            if (p == NULL || p->data != part)
                continue;
            if (!(p->hitChance > 0.0f))
            {
                diag::count("scissor_part_no_chance");
                return;
            }
            savedChance = p->hitChance;
            p->hitChance = savedChance * FORCE_PART;
            forced = p;
            diag::count("scissor_forced");
            return;
        }
        diag::count("scissor_part_gone");
    }

    Scissor::~Scissor()
    {
        if (forced != NULL)
            static_cast<MedicalSystem::HealthPartStatus*>(forced)->hitChance = savedChance;
    }

    void Scissor::landed(HitMaterialType r)
    {
        if (!on)
            return;

        GameData* part = r != HIT_MISSED && r != HIT_SWORD ? t_woundPart : NULL;
        if (second)
        {
            if (forced != NULL && part != NULL)
                diag::count(part == wanted ? "scissor_same_part" : "scissor_other_part");
            else if (forced != NULL)
                diag::count("scissor_second_no_wound");
            if (forced != NULL && part == wanted && diag::takeDetail())
                kit::Diag("scissor").kv("attacker", attacker->displayName).kv("victim", victim->displayName)
                    .kv("part", part != NULL ? part->name : std::string("?")).emit();
            return;
        }
        if (part == NULL)
            return;
        First f = { part, defense::clockMs(), offBlade };
        Lock l;
        if (g_first.size() > 4096)
            g_first.clear();
        g_first[Hit(attacker, victim)] = f;
    }

    void noteAttacker(Character* victim, Character* attacker)
    {
        if (!g_enabled || !g_splitOn || victim == NULL || attacker == NULL || victim == attacker)
            return;
        stance::Info s = stance::evaluate(victim, NULL);
        if (!s.dual || s.tier < g_splitTier)
            return;
        double now = defense::clockMs();
        Lock l;
        if (g_attackers.size() > 4096)
            g_attackers.clear();
        std::map<Character*, Attackers>::iterator it = g_attackers.find(victim);
        if (it == g_attackers.end())
        {
            Attackers z;
            z.n = 0;
            it = g_attackers.insert(std::make_pair(victim, z)).first;
        }
        Attackers& a = it->second;
        int slot = -1, oldest = 0;
        for (int i = 0; i < a.n; ++i)
        {
            if (same(a.who[i], attacker))
                slot = i;
            if (a.t[i] < a.t[oldest])
                oldest = i;
        }
        if (slot < 0)
            slot = a.n < MAX_ATTACKERS ? a.n++ : oldest;
        a.who[slot] = attacker->getHandle();
        a.t[slot] = now;
    }

    Character* flanker(Character* me, Character* mainTarget, const stance::Info& s, CombatTechniqueData* attack,
                       int comboID)
    {
        if (!g_enabled || !g_splitOn || me == NULL || mainTarget == NULL || !s.dual || s.off == NULL)
            return NULL;
        if (s.tier < g_splitTier)
            return NULL;
        double now = defense::clockMs();
        diag::count("split_check");
        Attackers list;
        list.n = 0;
        {
            Lock l;
            std::map<Character*, LastSplit>::iterator ls = g_lastSplit.find(me);
            if (ls != g_lastSplit.end() && ls->second.attack == attack && ls->second.combo == comboID
                && now >= ls->second.t && now - ls->second.t < ONE_SWING_MS)
            {
                diag::count("split_once_per_swing");
                return NULL;
            }
            std::map<Character*, Attackers>::iterator it = g_attackers.find(me);
            if (it != g_attackers.end())
                list = it->second;
        }

        float reach = g_splitReach * 0.5f * lengthOf(s.off) + me->getRadius();
        CombatClass* cc = me->getCombatClass();
        Ogre::Vector3 at = me->getPosition();
        Character* best = NULL;
        float bestDist = 0.0f, bestDot = 0.0f;
        int fresh = 0;
        for (int i = 0; i < list.n; ++i)
        {
            if (now < list.t[i] || now - list.t[i] > g_attackerMs)
                continue;
            Character* c = list.who[i].getCharacter();
            if (c == NULL || c == me || c == mainTarget)
                continue;
            ++fresh;
            if (down(c))
            {
                diag::count("split_flanker_down");
                continue;
            }

            bool inZone = false;
            if (cc != NULL)
                for (uint32_t k = 0; k < cc->targetsInAttackZone.size() && !inZone; ++k)
                    inZone = same(cc->targetsInAttackZone[k], c);
            if (inZone)
            {
                diag::count("split_flanker_in_zone");
                continue;
            }
            Ogre::Vector3 p = c->getPosition();
            float dx = p.x - at.x, dz = p.z - at.z;
            float dist = sqrt(dx * dx + dz * dz);
            if (dist > reach + c->getRadius())
            {
                diag::count("split_flanker_far");
                if (diag::takeDetail())
                    kit::Diag("split_check").kv("who", me->displayName).kv("flanker", c->displayName)
                        .kv("refused", "far").kv("dist", dist).kv("reach", reach + c->getRadius()).emit();
                continue;
            }
            float dot = defense::headingDot(me, c);
            if (dot > 1.5f || dot < g_splitCos)
            {
                diag::count(dot > 1.5f ? "split_heading_unknown" : "split_flanker_behind");
                continue;
            }
            if (best == NULL || dist < bestDist)
            {
                best = c;
                bestDist = dist;
                bestDot = dot;
            }
        }
        if (best == NULL)
        {
            diag::count(fresh > 0 ? "split_no_flanker_in_reach" : "split_no_attacker");
            return NULL;
        }
        if (roll01() >= balance::at(balance::SPLIT_CHANCE, s.dp))
        {
            diag::count("split_not_rolled");
            return NULL;
        }
        {
            Lock l;
            if (g_lastSplit.size() > 4096)
                g_lastSplit.clear();
            LastSplit ls = { attack, comboID, now };
            std::map<Character*, LastSplit>::iterator it = g_lastSplit.find(me);
            if (it != g_lastSplit.end())
                it->second = ls;
            else
                g_lastSplit.insert(std::make_pair(me, ls));
        }
        diag::count("split_redirect");
        feedback::say(me, lang::text("speech.split").c_str(), SPLIT_SAY_GAP_MS);
        if (diag::takeDetail())
            kit::Diag("split").kv("who", me->displayName).kv("main_target", mainTarget->displayName)
                .kv("flanker", best->displayName).kv("dist", bestDist).kv("reach", reach + best->getRadius())
                .kv("facing_dot", bestDot).kv("dp", s.dp).kv("tier", s.tier).emit();
        return best;
    }
}
