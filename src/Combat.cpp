#include "kit/input.h"

#include <Debug.h>
#include <core/Functions.h>

#include <kenshi/Kenshi.h>
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/Gear.h>
#include <kenshi/GameData.h>
#include <kenshi/Damages.h>
#include <kenshi/RaceData.h>
#include <kenshi/util/TripleInt.h>
#include <kenshi/combat/CombatClass.h>
#include <kenshi/combat/CombatTechniqueData.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Combat.h"
#include "Stance.h"
#include "Tuning.h"
#include "Settings.h"
#include "Balance.h"
#include "Diagnostics.h"
#include "Feedback.h"
#include "Visual.h"
#include "Arms.h"
#include "Defense.h"
#include "Ambidex.h"
#include "Lefty.h"
#include "Moves.h"
#include "Lang.h"
#include "Pressure.h"
#include "Mastery.h"
#include "Blades.h"
#include "kit/diag.h"
#include "kit/fcs.h"

namespace
{
    volatile bool g_enabled = true;

    __declspec(thread) bool t_inSetWeapon = false;
    __declspec(thread) Weapon* t_settingWeapon = NULL;
    __declspec(thread) float t_skillOverride = -1.0f;
    __declspec(thread) CharStats* t_overrideFor = NULL;

    struct PickRedirect
    {
        CharStats* st;
        WeaponCategory saved;
        CharStats* prevFor;
        float prevSkill;
        PickRedirect(CharStats* s, float skill)
            : st(s), saved(s->currentWeaponType), prevFor(t_overrideFor), prevSkill(t_skillOverride)
        {
            s->currentWeaponType = ATTACK_NULL;
            t_overrideFor = s;
            t_skillOverride = skill;
        }
        ~PickRedirect()
        {
            st->currentWeaponType = saved;
            t_overrideFor = prevFor;
            t_skillOverride = prevSkill;
        }
    };

    struct SetWeaponScope
    {
        bool prevIn;
        Weapon* prevW;
        explicit SetWeaponScope(Weapon* w) : prevIn(t_inSetWeapon), prevW(t_settingWeapon)
        {
            t_inSetWeapon = true;
            t_settingWeapon = w;
        }
        ~SetWeaponScope()
        {
            t_inSetWeapon = prevIn;
            t_settingWeapon = prevW;
        }
    };

    std::string nameOf(RootObjectBase* o) { return o != NULL ? o->displayName : std::string("<null>"); }

    struct TechRec
    {
        GameData* data;
        int number;
        moves::Def def;
        float priority;
        bool fromFields;
        char tag[20];
        char timingFrom[48];
    };
    CRITICAL_SECTION g_techLock;
    std::map<CombatTechniqueData*, TechRec> g_tech;
    int g_techOurs = 0;
    int g_techThird = 0;
    int g_techIgnored = 0;
    bool g_techLogged = false;

    struct Special
    {
        CombatTechniqueData* tech;
        TechRec rec;
    };
    const int MAX_SPECIALS = 12;
    std::vector<Special> g_specials;

    bool specialBefore(const Special& a, const Special& b)
    {
        if (a.rec.priority != b.rec.priority)
            return a.rec.priority > b.rec.priority;
        return a.rec.def.maxGap < b.rec.def.maxGap;
    }

    const int DASH_RECORD = 10;
    const int SPIN_RECORD = 12;
    double g_iniDashCooldownMs = -1.0;
    double g_iniSpinCooldownMs = -1.0;
    float g_iniSpinMinSkill = -1.0f;
    bool g_spinOn = true;

    int numberOfRecord(GameData* d)
    {
        if (d == NULL)
            return 0;
        const std::string& id = d->stringID;
        static const char* SUFFIX = "-Nitoryu.mod";
        size_t n = strlen(SUFFIX);
        if (id.size() <= n || id.compare(id.size() - n, n, SUFFIX) != 0)
            return 0;
        return atoi(id.c_str());
    }

    void copyText(char* dst, size_t size, const std::string& s)
    {
        strncpy_s(dst, size, s.c_str(), _TRUNCATE);
    }

    TechRec describe(CombatTechniqueData* self, GameData* d)
    {
        TechRec rec;
        rec.data = d;
        rec.number = numberOfRecord(d);
        rec.def = moves::noMove();
        rec.priority = kit::fcsFloat(d, "chance", 1.0f);
        rec.fromFields = false;
        rec.tag[0] = '\0';
        rec.timingFrom[0] = '\0';
        const moves::Record* table = rec.number > 0 ? moves::recordOfNumber(rec.number) : NULL;
        int mv = kit::fcsInt(d, "nitoryu move", -1);
        if (mv >= 0)
        {
            rec.fromFields = true;
            rec.def.kind = mv >= moves::SINGLE && mv < moves::KIND_COUNT ? (moves::Kind)mv : moves::OTHER;
            int ar = kit::fcsInt(d, "nitoryu arms", moves::ARMS_BY_KIND);
            rec.def.arms = ar >= moves::ARMS_BY_KIND && ar <= moves::ARMS_MAIN_ONLY ? ar : moves::ARMS_BY_KIND;
            rec.def.clip = moves::clipByName(kit::fcsString(d, "nitoryu clip", "").c_str());
            rec.def.needsClip = kit::fcsBool(d, "nitoryu needs clip", false);
            rec.def.echo = kit::fcsBool(d, "nitoryu echo", false);
            float roll = kit::fcsFloat(d, "nitoryu roll", 1.0f);
            rec.def.roll = roll < 0.0f ? 0.0f : roll > 1.0f ? 1.0f : roll;
            int cd = kit::fcsInt(d, "nitoryu cooldown ms", 0);
            rec.def.cooldownMs = cd < 0 ? 0.0 : (double)cd;
            rec.def.minGap = kit::fcsFloat(d, "nitoryu min gap", 0.0f);
            rec.def.maxGap = kit::fcsFloat(d, "nitoryu max gap", 0.0f);
            copyText(rec.timingFrom, sizeof(rec.timingFrom), kit::fcsString(d, "nitoryu timing from", ""));
        }
        else if (table != NULL)
        {
            rec.def = table->def;
            if (table->source != NULL)
                copyText(rec.timingFrom, sizeof(rec.timingFrom), table->source);
        }

        if (rec.def.kind != moves::OTHER && self != NULL && !self->skillTypes[ATTACK_NULL])
        {
            rec.def = moves::noMove();
            rec.fromFields = false;
            ++g_techIgnored;
        }
        copyText(rec.tag, sizeof(rec.tag),
                 table != NULL && table->tag[0] != '\0' ? table->tag : moves::KINDS[rec.def.kind].name);
        return rec;
    }

    CombatTechniqueData* (*techCtor_orig)(CombatTechniqueData*, GameData*) = NULL;
    CombatTechniqueData* techCtor_hook(CombatTechniqueData* self, GameData* d)
    {
        CombatTechniqueData* r = techCtor_orig(self, d);
        EnterCriticalSection(&g_techLock);
        TechRec rec = describe(self, d);
        g_tech[self] = rec;
        if (rec.def.kind != moves::OTHER)
        {
            ++(rec.number > 0 ? g_techOurs : g_techThird);
            if (moves::KINDS[rec.def.kind].special)
            {

                for (size_t i = 0; i < g_specials.size(); ++i)
                    if (g_specials[i].rec.data == d)
                    {
                        g_specials.erase(g_specials.begin() + i);
                        break;
                    }
                if ((int)g_specials.size() < MAX_SPECIALS)
                {
                    Special s = { self, rec };
                    g_specials.push_back(s);
                    std::sort(g_specials.begin(), g_specials.end(), specialBefore);
                }
            }
        }
        LeaveCriticalSection(&g_techLock);
        return r;
    }

    int specialsOf(moves::Kind kind, Special* out)
    {
        int n = 0;
        EnterCriticalSection(&g_techLock);
        for (size_t i = 0; i < g_specials.size() && n < MAX_SPECIALS; ++i)
            if (g_specials[i].rec.def.kind == kind)
                out[n++] = g_specials[i];
        LeaveCriticalSection(&g_techLock);
        return n;
    }

    bool techRec(CombatTechniqueData* t, TechRec& out)
    {
        if (t == NULL)
            return false;
        EnterCriticalSection(&g_techLock);
        std::map<CombatTechniqueData*, TechRec>::iterator it = g_tech.find(t);
        bool ok = it != g_tech.end();
        if (ok)
            out = it->second;
        LeaveCriticalSection(&g_techLock);
        return ok;
    }

    bool isNitoryuTechnique(CombatTechniqueData* t)
    {
        if (t == NULL)
            return false;
        TechRec r;
        if (techRec(t, r))
            return r.def.kind != moves::OTHER;
        if (t->skillTypes[ATTACK_NULL])
        {
            diag::count("category21_unregistered");
            return true;
        }
        return false;
    }

    bool g_mirrorDash = true;
    float g_farGap = 35.0f;

    bool hasSpecials(const stance::Info& s)
    {
        return s.dual && s.tier >= 3 && s.ambiEff >= cfg::f(cfg::SPECIAL_MIN_AMBI);
    }

    stance::Info notDual(const char* why)
    {
        stance::Info r = stance::evaluate(NULL, NULL);
        r.reason = why;
        return r;
    }

    stance::Info stanceOf(CharStats* st)
    {
        if (!g_enabled || st == NULL)
            return notDual("disabled");
        if (t_inSetWeapon)
            return t_settingWeapon != NULL ? stance::evaluate(st->me, t_settingWeapon) : notDual("sheathing");
        return stance::evaluate(st->me, NULL);
    }

    __declspec(thread) bool t_seeded = false;
    float roll01()
    {
        if (!t_seeded)
        {
            srand((unsigned)GetTickCount() ^ (unsigned)GetCurrentThreadId());
            t_seeded = true;
        }
        return (float)rand() / (float)(RAND_MAX + 1);
    }

    std::string frameName(void* a)
    {
        HMODULE m = NULL;
        if (a == NULL || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                             reinterpret_cast<LPCSTR>(a), &m) || m == NULL)
            return "unknown";
        char path[MAX_PATH] = { 0 };
        GetModuleFileNameA(m, path, MAX_PATH);
        const char* base = strrchr(path, '\\');
        base = base != NULL ? base + 1 : path;
        char out[MAX_PATH + 32];
        sprintf_s(out, "%s+0x%llx", base,
                  (unsigned long long)(reinterpret_cast<uintptr_t>(a) - reinterpret_cast<uintptr_t>(m)));
        return out;
    }

    std::string keyOf(const std::string& s)
    {
        std::string r(s);
        for (size_t i = 0; i < r.size(); ++i)
            if (!isalnum((unsigned char)r[i]))
                r[i] = '_';
        return r;
    }

    CRITICAL_SECTION g_callerLock;
    std::set<std::string> g_callerSeen;

    __declspec(noinline) void noteCaller(CharStats* self, bool farPick, float range, float reach)
    {
        void* frames[8] = { 0 };
        USHORT n = RtlCaptureStackBackTrace(1, 8, frames, NULL);
        HMODULE own = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&noteCaller), &own);
        std::string chain[3];
        int k = 0;
        for (USHORT i = 0; i < n && k < 3; ++i)
        {
            HMODULE m = NULL;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(frames[i]), &m);
            if (m != NULL && m == own)
                continue;
            chain[k++] = frameName(frames[i]);
        }
        std::string counter = std::string(farPick ? "pick_far_caller_" : "pick_near_caller_") + keyOf(chain[0]);
        diag::count(counter.c_str());
        std::string sig = (farPick ? "far|" : "near|") + chain[0] + "|" + chain[1];
        bool fresh = false;
        EnterCriticalSection(&g_callerLock);
        if (g_callerSeen.size() < 32 && g_callerSeen.insert(sig).second)
            fresh = true;
        LeaveCriticalSection(&g_callerLock);
        if (fresh && diag::takeDetail())
            kit::Diag("pick_caller").kv("who", nameOf(self->me)).kv("far", farPick).kv("range", range).kv("reach", reach)
                .kv("gap", range - reach).kv("f1", chain[0]).kv("f2", chain[1]).kv("f3", chain[2]).emit();
    }

    const char* gapBucket(float gap)
    {
        return gap <= 50.0f ? "pick_far_gap_25_50" : gap <= 100.0f ? "pick_far_gap_50_100" : "pick_far_gap_100_up";
    }

    void restoreReach(CharStats* st);

    void (*setWeapon_orig)(CharStats*, Weapon*) = NULL;
    void setWeapon_hook(CharStats* self, Weapon* w)
    {
        {
            SetWeaponScope scope(w);
            setWeapon_orig(self, w);
        }
        if (!g_enabled || self == NULL || self->me == NULL)
            return;
        stance::invalidate(self->me);
        if (w == NULL)
        {
            visual::noteSheathe(self->me);
            restoreReach(self);
            return;
        }
        stance::Info s = stance::evaluate(self->me, w);
        diag::count((std::string("draw_") + s.reason).c_str());
        bool player = self->me->isPlayerCharacter();

        if (s.dual && player)
            feedback::say(self->me, lang::text("speech.two_blades").c_str(), 20000);
        if (s.dual)
            visual::noteDraw(self->me);
        if (!(s.dual || player) || !diag::takeDetail())
            return;
        kit::Diag d("stance");
        d.kv("who", nameOf(self->me)).kv("player", player).kv("dual", s.dual).kv("reason", s.reason);
        if (s.dual)
            d.kv("main", nameOf(s.main)).kv("off", nameOf(s.off))
             .kv("main_cat", (int)s.main->category).kv("off_cat", (int)s.off->category)
             .kv("skill_main", s.skillMain).kv("skill_off", s.skillOff)
             .kv("dp", s.dp).kv("tier", s.tier).kv("clumsy", s.clumsy).kv("guard_pen", s.guardPenalty)
             .kv("speed", s.speedFactor).kv("w_dual", s.weightDual)
             .kv("ambi", s.ambi).kv("ambi_eff", s.ambiEff).kv("overload", s.overload)
             .kv("share", stance::offHandShare(s.dp))
             .kv("pair", blades::pairName(blades::pairOf(s.main, s.off))).kv("pair_mixed", blades::mixedTypes(s.main, s.off))
             .kv("atk_speed_field", self->attackSpeed).kv("blk_speed_field", self->blockSpeed);
        d.emit();
    }

    bool indoors(Character* c)
    {
        return c != NULL && (bool)c->isIndoors();
    }

    float offAttackBonus(const stance::Info& s, Character* c)
    {
        Sword* sw = s.off != NULL ? s.off->isSword() : NULL;
        float b = s.off != NULL ? (float)s.off->modAttack : 0.0f;
        if (sw != NULL && indoors(c))
            b += (float)sw->modIndoors;
        return cfg::f(cfg::OFF_WEAPON_BONUS_SHARE) * b;
    }

    float offDefenceBonus(const stance::Info& s, Character* c)
    {
        Sword* sw = s.off != NULL ? s.off->isSword() : NULL;
        if (sw == NULL)
            return 0.0f;
        float usual = cfg::f(cfg::OFF_WEAPON_BONUS_SHARE);

        float own = blades::offDefenceShare(s, (float)sw->modDefence, usual) * (float)sw->modDefence;
        return own + (indoors(c) ? usual * (float)sw->modIndoors : 0.0f);
    }

    void (*weightSpeed_orig)(CharStats*, float) = NULL;
    void weightSpeed_hook(CharStats* self, float weaponWeight)
    {
        stance::Info s = stanceOf(self);
        if (s.dual)
        {
            diag::count("weight_dual");
            weightSpeed_orig(self, s.weightDual);
            return;
        }
        weightSpeed_orig(self, weaponWeight);
    }

    float (*speed_orig)(CharStats*, float, float, bool) = NULL;

    struct SpeedRec { float gameAtk, dualAtk, gameBlk, dualBlk, flowAtk; };
    CRITICAL_SECTION g_speedLock;
    std::map<CharStats*, SpeedRec> g_speed;

    void noteSpeed(CharStats* st, bool isBlock, float game, float dual, float flowK = 1.0f)
    {
        EnterCriticalSection(&g_speedLock);
        if (g_speed.size() > 4096)
            g_speed.clear();
        std::map<CharStats*, SpeedRec>::iterator it = g_speed.find(st);
        if (it == g_speed.end())
        {
            SpeedRec z = { -1.0f, -1.0f, -1.0f, -1.0f, 1.0f };
            it = g_speed.insert(std::make_pair(st, z)).first;
        }
        if (isBlock) { it->second.gameBlk = game; it->second.dualBlk = dual; }
        else { it->second.gameAtk = game; it->second.dualAtk = dual; it->second.flowAtk = flowK; }
        LeaveCriticalSection(&g_speedLock);
    }

    float speed_hook(CharStats* self, float weightMult, float skill, bool isBlock)
    {
        float r = speed_orig(self, weightMult, skill, isBlock);
        stance::Info s = stanceOf(self);
        if (s.dual)
        {
            diag::count(isBlock ? "speed_block_dual" : "speed_attack_dual");

            float bonus = balance::dexBonus();
            if (bonus > 0.0f && self != NULL)
            {
                float dex = self->dexterityActual();
                float lo = isBlock ? 0.6f : 0.8f, hi = isBlock ? 4.0f : 1.2f;
                float raw0 = std::max(lo, std::min(hi, (0.6f * dex + 0.4f * skill) * 0.01f));
                float raw1 = std::max(lo, std::min(hi, (0.6f * dex * (1.0f + bonus) + 0.4f * skill) * 0.01f));
                if (raw1 > raw0)
                {
                    r = std::min(r * raw1 / raw0, std::max(hi, r));
                    diag::count(isBlock ? "dex_bonus_speed_block" : "dex_bonus_speed_attack");
                }
            }
            float game = r;

            Character* me = self != NULL ? self->me : NULL;
            float flowK = isBlock ? 1.0f : flow::speedFactor(me);
            r = stance::applySpeed(r, s.speedFactor * flowK, isBlock, isBlock ? 0.0f : flow::capBonus(me));
            noteSpeed(self, isBlock, game, r, flowK);
        }
        else if (g_enabled && self != NULL && !t_inSetWeapon && lefty::active(self->me))
        {
            diag::count(isBlock ? "lefty_speed_block" : "lefty_speed_attack");
            r *= lefty::speedFactor(self->me);
        }
        return r;
    }

    float (*meleeAttack_orig)(CharStats*) = NULL;
    float meleeAttack_hook(CharStats* self)
    {
        float r = meleeAttack_orig(self);
        if (!g_enabled || self == NULL || t_inSetWeapon)
            return r;
        stance::Info s = stance::evaluate(self->me, NULL);
        if (s.dual)
            r += offAttackBonus(s, self->me) - s.clumsy;
        return r;
    }

    float (*blockChance_orig)(CharStats*, float) = NULL;
    float blockChance_hook(CharStats* self, float versus)
    {
        stance::Info s = stanceOf(self);
        if (s.dual)
        {
            diag::count("block_chance_dual");
            versus += s.clumsy + s.guardPenalty - offDefenceBonus(s, self->me);
        }
        else if (g_enabled && self != NULL && !t_inSetWeapon && lefty::active(self->me))
        {
            diag::count("lefty_block_chance");
            versus += lefty::blockPenalty(self->me);
        }
        return blockChance_orig(self, versus);
    }

    float (*equippedSkill_orig)(CharStats*) = NULL;
    float equippedSkill_hook(CharStats* self)
    {

        if (t_skillOverride >= 0.0f && self == t_overrideFor)
            return t_skillOverride;
        return equippedSkill_orig(self);
    }

    CRITICAL_SECTION g_pickLock;

    typedef moves::Kind Want;

    Want kindOf(CombatTechniqueData* t)
    {
        Want k = (Want)combat::moveKind(t);
        return k == moves::OTHER ? moves::SINGLE : k;
    }

    const char* wantName(Want w) { return moves::KINDS[w].name; }

    bool targetStaggered(Character* me)
    {
        CombatClass* cc = me != NULL ? me->getCombatClass() : NULL;
        if (cc == NULL)
            return false;
        hand h = cc->_getAttackTarget();
        Character* t = h.getCharacter();
        CombatClass* tc = t != NULL ? t->getCombatClass() : NULL;
        return tc != NULL && tc->combatState == STUMBLE;
    }

    bool targetFullyPressured(Character* me)
    {
        CombatClass* cc = me != NULL ? me->getCombatClass() : NULL;
        if (cc == NULL)
            return false;
        Character* t = cc->_getAttackTarget().getCharacter();
        return t != NULL && pressure::stacks(t, me) >= pressure::maxStacks();
    }

    CombatTechniqueData* (*choose_orig)(CharStats*, float, float, CombatTechniqueData*, bool) = NULL;

    typedef std::pair<Character*, GameData*> UseKey;
    std::map<UseKey, double> g_lastUse;

    double cooldownOf(const Special& s)
    {
        if (s.rec.number == DASH_RECORD && g_iniDashCooldownMs >= 0.0)
            return g_iniDashCooldownMs;
        if (s.rec.number == SPIN_RECORD && g_iniSpinCooldownMs >= 0.0)
            return g_iniSpinCooldownMs;
        return s.rec.def.cooldownMs;
    }

    bool cooldownOver(const Special& s, Character* me, double now)
    {
        double ms = cooldownOf(s);
        EnterCriticalSection(&g_pickLock);
        std::map<UseKey, double>::iterator it = g_lastUse.find(UseKey(me, s.rec.data));
        bool ok = it == g_lastUse.end() || now - it->second >= ms || now < it->second;
        LeaveCriticalSection(&g_pickLock);
        return ok;
    }

    void noteUse(const Special& s, Character* me, double now)
    {
        EnterCriticalSection(&g_pickLock);
        if (g_lastUse.size() > 8192)
            g_lastUse.clear();
        g_lastUse[UseKey(me, s.rec.data)] = now;
        LeaveCriticalSection(&g_pickLock);
    }

    void count(const std::string& name) { diag::count(name.c_str()); }
    std::string tagOf(const Special& s) { return std::string(s.rec.tag); }

    float minSkillOf(const Special& s)
    {
        if (s.rec.number == SPIN_RECORD && g_iniSpinMinSkill >= 0.0f)
            return g_iniSpinMinSkill;
        return s.tech != NULL ? s.tech->minSkill : 0.0f;
    }

    bool switchedOn(const Special& s)
    {
        if (s.rec.number == DASH_RECORD)
            return combat::dashOn();
        if (s.rec.number == SPIN_RECORD)
            return g_spinOn;
        return true;
    }

    bool available(const Special& s, const stance::Info& st)
    {
        const moves::Def& d = s.rec.def;
        return switchedOn(s) && st.dp >= minSkillOf(s)
            && !(s.rec.number == SPIN_RECORD && st.tier < tuning::SPIN_MIN_TIER)
            && !(d.needsClip && d.clip >= 0 && !arms::clipOn((arms::Clip)d.clip));
    }

    bool up(const Special& s, const stance::Info& st, Character* me, double now)
    {
        return available(s, st) && visual::bladeReady(me) && cooldownOver(s, me, now);
    }

    std::map<Character*, double> g_lastDouble;

    std::map<Character*, double> g_crossArmed;

    void armCross(Character* me, double now)
    {
        EnterCriticalSection(&g_pickLock);
        if (g_crossArmed.size() > 4096)
            g_crossArmed.clear();
        g_crossArmed[me] = now;
        LeaveCriticalSection(&g_pickLock);
    }

    bool crossArmed(Character* me, double now)
    {
        EnterCriticalSection(&g_pickLock);
        std::map<Character*, double>::iterator it = g_crossArmed.find(me);
        bool on = it != g_crossArmed.end() && now >= it->second && now - it->second <= cfg::d(cfg::CROSS_ARM_MS);
        LeaveCriticalSection(&g_pickLock);
        return on;
    }

    void disarmCross(Character* me)
    {
        EnterCriticalSection(&g_pickLock);
        g_crossArmed.erase(me);
        LeaveCriticalSection(&g_pickLock);
    }

    bool riposteWindow(Character* me, double now)
    {
        if (!cfg::b(cfg::CROSS_RIPOSTE))
            return false;
        double last = defense::lastMainBlockMs(me);
        double parry = defense::lastParryMs(me);
        if (parry > last)
            last = parry;
        return last >= 0.0 && now >= last && now - last <= cfg::d(cfg::CROSS_RIPOSTE_WINDOW_MS);
    }
    std::map<Character*, CombatTechniqueData*> g_lastOff;
    double g_doubleCooldownMs = 5000.0;
    bool g_noRepeat = true;

    std::map<Character*, CombatTechniqueData*> g_foreignQuiet;

    void setForeignQuiet(Character* me, CombatTechniqueData* t)
    {
        EnterCriticalSection(&g_pickLock);
        if (t != NULL)
        {
            if (g_foreignQuiet.size() > 4096)
                g_foreignQuiet.clear();
            g_foreignQuiet[me] = t;
        }
        else
            g_foreignQuiet.erase(me);
        LeaveCriticalSection(&g_pickLock);
    }

    bool foreignQuiet(Character* me, CombatTechniqueData* t)
    {
        EnterCriticalSection(&g_pickLock);
        std::map<Character*, CombatTechniqueData*>::iterator it = g_foreignQuiet.find(me);
        bool r = it != g_foreignQuiet.end() && it->second == t;
        LeaveCriticalSection(&g_pickLock);
        return r;
    }

    bool doubleReady(Character* me, double now, const char* counter)
    {

        double cooldown = mugen::doubleCooldownMs(me, g_doubleCooldownMs);
        if (cooldown < g_doubleCooldownMs)
            diag::count("mugen_cooldown_used");
        EnterCriticalSection(&g_pickLock);
        std::map<Character*, double>::iterator it = g_lastDouble.find(me);
        bool ok = it == g_lastDouble.end() || now - it->second >= cooldown || now < it->second;
        LeaveCriticalSection(&g_pickLock);
        if (!ok)
            diag::count(counter);
        return ok;
    }

    void noteDouble(Character* me, double now)
    {
        EnterCriticalSection(&g_pickLock);
        if (g_lastDouble.size() > 4096)
            g_lastDouble.clear();
        g_lastDouble[me] = now;
        LeaveCriticalSection(&g_pickLock);
    }

    CombatTechniqueData* lastOff(Character* me)
    {
        EnterCriticalSection(&g_pickLock);
        std::map<Character*, CombatTechniqueData*>::iterator it = g_lastOff.find(me);
        CombatTechniqueData* r = it != g_lastOff.end() ? it->second : NULL;
        LeaveCriticalSection(&g_pickLock);
        return r;
    }

    void noteOff(Character* me, CombatTechniqueData* t)
    {
        EnterCriticalSection(&g_pickLock);
        if (g_lastOff.size() > 4096)
            g_lastOff.clear();
        g_lastOff[me] = t;
        LeaveCriticalSection(&g_pickLock);
    }

    struct Carry { Want want; double t; };
    std::map<Character*, Carry> g_carry;
    bool g_carryOn = true;

    void carryOff(Character* me, Want want, double now)
    {
        EnterCriticalSection(&g_pickLock);
        if (g_carry.size() > 4096)
            g_carry.clear();
        Carry c = { want, now };
        g_carry[me] = c;
        LeaveCriticalSection(&g_pickLock);
    }

    bool takeCarry(Character* me, double now, Want& want)
    {
        EnterCriticalSection(&g_pickLock);
        std::map<Character*, Carry>::iterator it = g_carry.find(me);
        bool ok = it != g_carry.end() && now - it->second.t <= cfg::d(cfg::OFF_CARRY_MS) && now >= it->second.t;
        if (ok)
            want = it->second.want;
        if (it != g_carry.end())
            g_carry.erase(it);
        LeaveCriticalSection(&g_pickLock);
        return ok;
    }

    CRITICAL_SECTION g_missLock;
    std::set<std::string> g_missSeen;

    void noteMissingTrack(const std::string& rig, const std::string& track)
    {
        std::string key = rig + "|" + track;
        bool fresh = false;
        EnterCriticalSection(&g_missLock);
        if (g_missSeen.size() < 64 && g_missSeen.insert(key).second)
            fresh = true;
        LeaveCriticalSection(&g_missLock);
        if (fresh)
            kit::Diag("base_tracks_missing").kv("rig", rig).kv("track", track).emit();
    }

    struct Pick
    {
        CharStats* self;
        float range, reach;
        CombatTechniqueData* last;
        bool stationary;
        stance::Info s;
        bool special;
        double now;
        int budget;

        CombatTechniqueData* draw()
        {
            if (budget <= 0)
            {
                diag::count("pick_budget_spent");
                return NULL;
            }
            --budget;
            diag::count("pick_orig_call");
            return choose_orig(self, range, reach, last, stationary);
        }

        CombatTechniqueData* game()
        {
            diag::count("pick_orig_call");
            return choose_orig(self, range, reach, last, stationary);
        }
    };

    CombatTechniqueData* drawOffHand(Pick& p, Want want, int tries, bool noTwin = false,
                                     CombatTechniqueData* exact = NULL)
    {
        float dp = stance::poolSkill(p.s);
        CombatTechniqueData* last = g_noRepeat && !moves::KINDS[want].special ? lastOff(p.self->me) : NULL;
        CombatTechniqueData* repeat = NULL;
        CombatTechniqueData* fallback = NULL;
        CombatTechniqueData* r = NULL;
        CombatTechniqueData* other = NULL;
        int nulls = 0;
        {
            PickRedirect redirect(p.self, dp);
            for (int i = 0; i < tries && r == NULL && p.budget > 0; ++i)
            {
                CombatTechniqueData* t = p.draw();
                if (!isNitoryuTechnique(t))
                {
                    if (t == NULL)
                        ++nulls;
                    else if (other == NULL)
                        other = t;
                    continue;
                }

                std::string rig;
                if (!arms::hasTrack(p.self->me, t->animation, &rig))
                {
                    diag::count("base_track_missing_skip");
                    noteMissingTrack(rig, t->animation);
                    continue;
                }

                TechRec tr;
                if (techRec(t, tr) && tr.def.needsClip && tr.def.clip >= 0 && !arms::clipOn((arms::Clip)tr.def.clip))
                {
                    diag::count("record_clip_off_skip");
                    continue;
                }
                if (exact != NULL)
                {
                    if (t == exact)
                        r = t;
                    continue;
                }
                Want k = kindOf(t);
                if (noTwin && moves::KINDS[k].twin)
                    continue;
                if (t == last && k == want)
                {
                    repeat = t;
                    continue;
                }
                if (k == want)
                    r = t;
                else if (fallback == NULL && !moves::KINDS[k].special && t != last)
                    fallback = t;
            }
        }
        if (r == NULL && repeat != NULL)
            diag::count("offhand_repeat_kept");
        else if (repeat != NULL)
            diag::count("offhand_repeat_avoided");
        if (r == NULL)
            r = repeat;
        if (r == NULL && !moves::KINDS[want].special)
            r = fallback;
        if (r == NULL)
        {

            diag::count(other != NULL ? "draw_miss_got_other" : "draw_miss_got_null");
            if (diag::takeDetail())
                kit::Diag("draw_miss").kv("who", nameOf(p.self->me)).kv("want", wantName(want)).kv("range", p.range)
                    .kv("reach", p.reach).kv("gap", p.range - p.reach).kv("stationary", p.stationary).kv("dp", dp)
                    .kv("nulls", nulls).kv("other", other != NULL ? other->animation : std::string("-")).emit();
        }
        return r;
    }

    CRITICAL_SECTION g_reachLock;
    struct SavedReach { float game; float ours; };
    std::map<CharStats*, SavedReach> g_reachSaved;

    void raiseReach(CharStats* st, float target)
    {
        float cur = st->longestReachingAttack;
        EnterCriticalSection(&g_reachLock);
        if (g_reachSaved.size() > 4096)
            g_reachSaved.clear();
        std::map<CharStats*, SavedReach>::iterator it = g_reachSaved.find(st);
        bool ours = it != g_reachSaved.end() && it->second.ours == cur;
        float game = ours ? it->second.game : cur;
        if (game >= target)
        {
            if (it != g_reachSaved.end())
                g_reachSaved.erase(it);
            LeaveCriticalSection(&g_reachLock);
            if (ours)
                st->longestReachingAttack = game;
            diag::count("dash_reach_kept");
            return;
        }
        SavedReach sr = { game, target };
        g_reachSaved[st] = sr;
        LeaveCriticalSection(&g_reachLock);
        if (cur != target)
        {
            st->longestReachingAttack = target;
            diag::count("dash_reach_raised");
        }
    }

    void restoreReach(CharStats* st)
    {
        EnterCriticalSection(&g_reachLock);
        std::map<CharStats*, SavedReach>::iterator it = g_reachSaved.find(st);
        bool have = it != g_reachSaved.end();
        SavedReach sr = { 0.0f, 0.0f };
        if (have)
        {
            sr = it->second;
            g_reachSaved.erase(it);
        }
        LeaveCriticalSection(&g_reachLock);
        if (!have)
            return;
        if (st->longestReachingAttack == sr.ours)
        {
            st->longestReachingAttack = sr.game;
            diag::count("dash_reach_restored");
        }
        else
            diag::count("dash_reach_game_rewrote");
    }

    bool longTwoHanded(CombatTechniqueData* t)
    {
        if (t == NULL || isNitoryuTechnique(t) || t->initialDistance <= cfg::f(cfg::FOREIGN_REPICK_OVER))
            return false;
        TechRec rec;
        if (!techRec(t, rec) || rec.data == NULL)
            return true;
        return !kit::fcsBool(rec.data, "1 handed", false);
    }

    float engageDistance(const Pick& p)
    {
        Special op[MAX_SPECIALS];
        int n = specialsOf(moves::OPENER, op);
        const Special* best = NULL;
        bool ok[MAX_SPECIALS];
        float widest = -1.0f;
        for (int i = 0; i < n; ++i)
        {
            ok[i] = available(op[i], p.s);
            if (!ok[i])
                continue;
            if (op[i].rec.def.maxGap > widest)
                widest = op[i].rec.def.maxGap;
            if (best == NULL && up(op[i], p.s, p.self->me, p.now))
                best = &op[i];
        }
        if (best != NULL)
        {
            bool rival = false;
            for (int i = 0; i < n; ++i)
                if (ok[i] && &op[i] != best && op[i].rec.priority >= best->rec.priority)
                    rival = true;
            if (!rival)
                return best->rec.def.maxGap;
        }
        return widest;
    }

    CombatTechniqueData* sharedDashCooldown(Pick& p, CombatTechniqueData* r)
    {
        if (!cfg::b(cfg::LONG_DASH_SHARED) || r == NULL || !combat::isForeignLongDash(r))
            return r;
        Special op[MAX_SPECIALS];
        int n = specialsOf(moves::OPENER, op);
        const Special* dash = NULL;
        for (int i = 0; i < n && dash == NULL; ++i)
            if (op[i].rec.number == DASH_RECORD)
                dash = &op[i];
        if (dash == NULL)
            return r;
        Character* me = p.self->me;
        if (cooldownOver(*dash, me, p.now))
        {
            noteUse(*dash, me, p.now);
            diag::count("long_dash_shared_started");
            return r;
        }
        CombatTechniqueData* alt = NULL;
        for (int i = 0; i < 6 && alt == NULL && p.budget > 0; ++i)
        {
            CombatTechniqueData* t = p.draw();
            if (t != NULL && !combat::isForeignLongDash(t))
                alt = t;
        }
        diag::count(alt != NULL ? "long_dash_shared_repicked" : "long_dash_shared_walk_in");
        if (diag::takeDetail())
            kit::Diag("long_dash_shared").kv("who", nameOf(me)).kv("tech", r->animation)
                .kv("instead", alt != NULL ? alt->animation : std::string("walk in")).emit();
        return alt;
    }

    CombatTechniqueData* finishPick(Pick& p, CombatTechniqueData* r)
    {
        r = sharedDashCooldown(p, r);

        bool mirrored = p.special && g_mirrorDash && r != NULL && combat::isForeignLongDash(r)
                        && doubleReady(p.self->me, p.now, "double_hit_wait_foreign");
        if (mirrored)
        {
            noteDouble(p.self->me, p.now);
            diag::count("long_dash_mirrored");
            if (diag::takeDetail())
                kit::Diag("special_pick").kv("who", nameOf(p.self->me)).kv("move", "foreign_dash_mirrored")
                    .kv("tech", r->animation).kv("distance", r->initialDistance).kv("dp", p.s.dp)
                    .kv("range", p.range).kv("reach", p.reach).emit();
        }

        if (!mirrored && r != NULL && !isNitoryuTechnique(r) && r->initialDistance > cfg::f(cfg::FOREIGN_REPICK_OVER))
        {
            if (!longTwoHanded(r))
                diag::count("long_one_handed_kept");
            else
            {
                CombatTechniqueData* alt = NULL;
                for (int i = 0; i < 6 && alt == NULL && p.budget > 0; ++i)
                {
                    CombatTechniqueData* t = p.draw();
                    if (t != NULL && !longTwoHanded(t))
                        alt = t;
                }
                diag::count(alt != NULL ? "two_handed_avoided" : "two_handed_kept");
                if (alt != NULL && diag::takeDetail())
                    kit::Diag("two_handed_avoided").kv("who", nameOf(p.self->me)).kv("tech", r->animation)
                        .kv("distance", r->initialDistance).kv("instead", alt->animation).emit();
                if (alt != NULL)
                    r = alt;
            }
        }

        bool quiet = !mirrored && p.special && g_mirrorDash && r != NULL && combat::isForeignLongDash(r);
        setForeignQuiet(p.self->me, quiet ? r : NULL);

        float engage = p.special ? engageDistance(p) : -1.0f;
        if (engage > 0.0f)
            raiseReach(p.self, engage);
        else
            restoreReach(p.self);
        return r;
    }

    CombatTechniqueData* stageFinisher(Pick& p, const Special& f, const char* trigger)
    {
        Character* me = p.self->me;
        std::string tag = tagOf(f);
        if (!available(f, p.s))
            return NULL;
        if (!cooldownOver(f, me, p.now))
        {
            count("special_" + tag + "_cooldown");
            return NULL;
        }
        if (!doubleReady(me, p.now, ("double_hit_wait_" + tag).c_str()) || roll01() >= f.rec.def.roll)
            return NULL;
        CombatTechniqueData* r = drawOffHand(p, moves::FINISHER, 10, false, f.tech);

        if (r == NULL && f.tech != NULL && p.range <= p.reach + f.tech->initialDistance)
        {
            r = f.tech;
            count("special_" + tag + "_direct");
        }
        std::string trig(trigger);
        count(r != NULL ? (trig == "stagger" ? "special_" + tag : "special_" + tag + "_" + trig) : "special_" + tag + "_no_fit");
        if (r == NULL)
            return NULL;
        noteUse(f, me, p.now);
        noteDouble(me, p.now);
        if (diag::takeDetail())
            kit::Diag("special_pick").kv("who", nameOf(me)).kv("move", tag)
                .kv("trigger", trigger).kv("dp", p.s.dp)
                .kv("ambi_eff", p.s.ambiEff).kv("range", p.range).kv("reach", p.reach).emit();
        return r;
    }

    CombatTechniqueData* stageOpener(Pick& p, float gap, const Special& o)
    {
        Character* me = p.self->me;
        const moves::Def& d = o.rec.def;
        std::string tag = tagOf(o);
        if (!available(o, p.s))
            return NULL;

        bool bladeOn = visual::bladeReady(me);
        if (!bladeOn)
            count(tag + "_wait_blade");
        if (!bladeOn || gap < d.minGap || gap > d.maxGap)
            return NULL;
        if (!cooldownOver(o, me, p.now))
        {
            count("special_" + tag + "_cooldown");
            return NULL;
        }

        if (d.echo && !doubleReady(me, p.now, ("double_hit_wait_" + tag).c_str()))
            return NULL;
        if (roll01() >= d.roll)
            return NULL;
        CombatTechniqueData* r = drawOffHand(p, moves::OPENER, 10, false, o.tech);
        count(r != NULL ? "special_" + tag : "special_" + tag + "_no_fit");
        if (r == NULL)
            return NULL;
        noteUse(o, me, p.now);
        noteDouble(me, p.now);
        if (diag::takeDetail())
            kit::Diag("special_pick").kv("who", nameOf(me)).kv("move", tag).kv("dp", p.s.dp)
                .kv("ambi_eff", p.s.ambiEff).kv("range", p.range).kv("reach", p.reach).emit();
        return r;
    }

    float twinShare(const stance::Info& s)
    {
        float m = blades::twinMult(s.main, s.off);
        if (m != 1.0f)
            diag::count("pair_equal_twin_roll");
        float v = balance::at(balance::TWIN_SHARE, s.dp) * m;
        return v > 1.0f ? 1.0f : v;
    }

    CombatTechniqueData* stageOffHand(Pick& p)
    {
        Character* me = p.self->me;
        bool counter = defense::takeCounter(me);
        if (counter && roll01() >= defense::counterChance(p.s.dp))
        {
            diag::count("counter_not_rolled");
            counter = false;
        }

        bool chain = mugen::takeChain(me, p.now) && !counter;
        Want want = chain ? moves::TWIN : moves::SINGLE;
        bool carried = !counter && !chain && takeCarry(me, p.now, want);
        if (!counter && !carried && !chain)
        {
            if (roll01() >= stance::offHandShare(p.s.dp))
            {
                diag::count("offhand_not_rolled");
                return NULL;
            }
            want = roll01() < twinShare(p.s) ? moves::TWIN : moves::SINGLE;
        }

        if (counter)
            want = roll01() < twinShare(p.s) ? moves::TWIN : moves::SINGLE;
        bool noTwin = !chain && !doubleReady(me, p.now, want == moves::TWIN ? "double_hit_wait_twin" : "double_hit_wait_quiet");
        if (noTwin)
            want = moves::SINGLE;
        CombatTechniqueData* r = drawOffHand(p, want, 6, noTwin);
        if (chain)
            diag::count(!isNitoryuTechnique(r) ? "mugen_chain_no_fit"
                        : combat::isTwinTechnique(r) ? "mugen_chain_twin" : "mugen_chain_single");
        if (isNitoryuTechnique(r))
        {
            noteOff(me, r);
            if (combat::isTwinTechnique(r))
                noteDouble(me, p.now);
            if (counter)
                diag::count(combat::isTwinTechnique(r) ? "counter_twin" : "counter_single");
            if (carried)
                diag::count("offhand_carry_used");
            diag::count(counter ? "counter_fired" : combat::isTwinTechnique(r) ? "offhand_chosen_twin" : "offhand_chosen");
            if (diag::takeDetail())
                kit::Diag(counter ? "counter_pick" : "offhand_pick").kv("who", nameOf(me)).kv("tech", r->animation)
                    .kv("want", wantName(want)).kv("carried", carried).kv("dp", p.s.dp).kv("tier", p.s.tier)
                    .kv("range", p.range).kv("reach", p.reach).emit();
            return r;
        }

        if (counter)
            diag::count("counter_no_fit");
        diag::count(carried ? "offhand_carry_dropped" : "offhand_none");
        if (!carried && g_carryOn)
        {
            carryOff(me, want, p.now);
            diag::count("offhand_carried");
        }
        return NULL;
    }

    CombatTechniqueData* choose_hook(CharStats* self, float range, float reach, CombatTechniqueData* last,
                                     bool stationary)
    {

        if (t_inSetWeapon)
        {
            diag::count("pick_in_setweapon");
            return choose_orig(self, range, reach, last, stationary);
        }
        stance::Info s = stanceOf(self);
        if (!s.dual)
        {
            restoreReach(self);
            return choose_orig(self, range, reach, last, stationary);
        }
        Pick p;
        p.self = self;
        p.range = range;
        p.reach = reach;
        p.last = last;
        p.stationary = stationary;
        p.s = s;
        p.special = false;
        p.now = defense::clockMs();
        p.budget = tuning::PICK_BUDGET;
        diag::count("pick_dual");
        if (s.tier < 1)
        {

            diag::count("offhand_tier0");
            return finishPick(p, p.game());
        }
        p.special = s.tier >= 3 && s.ambiEff >= cfg::f(cfg::SPECIAL_MIN_AMBI);

        bool staggered = p.special && targetStaggered(self->me);
        if (p.special)
            diag::count(staggered ? "pick_special_target_staggered" : "pick_special_target_steady");
        bool pressed = p.special && !staggered && targetFullyPressured(self->me);
        if (pressed)
            diag::count("pick_special_target_full_pressure");
        bool riposte = p.special && !staggered && !pressed && riposteWindow(self->me, p.now);
        if (riposte)
            diag::count("pick_special_riposte");
        if (staggered || pressed || riposte)
            armCross(self->me, p.now);
        bool armed = p.special && !staggered && !pressed && !riposte && crossArmed(self->me, p.now);

        float gap = range - reach;
        bool farPick = gap > g_farGap;
        noteCaller(self, farPick, range, reach);
        if (farPick)
        {
            diag::count("pick_far");
            diag::count(gapBucket(gap));
            return finishPick(p, p.game());
        }
        if (p.special)
        {
            Special sp[MAX_SPECIALS];
            if (staggered || pressed || riposte || armed)
            {
                const char* trigger = staggered ? "stagger" : pressed ? "pressure" : riposte ? "riposte" : "armed";
                int nf = specialsOf(moves::FINISHER, sp);
                for (int i = 0; i < nf; ++i)
                {
                    CombatTechniqueData* r = stageFinisher(p, sp[i], trigger);
                    if (r != NULL)
                    {
                        disarmCross(self->me);
                        return r;
                    }
                }
            }

            int no = specialsOf(moves::OPENER, sp);
            for (int i = 0; i < no; ++i)
            {
                bool waits = false;
                for (int j = 0; j < i && !waits; ++j)
                    if (sp[j].rec.priority > sp[i].rec.priority && up(sp[j], p.s, self->me, p.now))
                    {
                        count("special_" + tagOf(sp[i]) + "_waits_for_" + tagOf(sp[j]));
                        waits = true;
                    }
                if (waits)
                    continue;
                CombatTechniqueData* r = stageOpener(p, gap, sp[i]);
                if (r != NULL)
                    return finishPick(p, r);
            }
        }
        CombatTechniqueData* r = stageOffHand(p);
        return finishPick(p, r != NULL ? r : p.game());
    }

    float term(float lo, float hi, float x) { return lo + (hi - lo) * x / 200.0f; }
    float ratio(float a, float b) { return b > 0.001f ? a / b : 1.0f; }

    float dexBonusCut(CharStats* st, const stance::Info& s, float bonus)
    {
        Sword* ms = s.main != NULL ? s.main->isSword() : NULL;
        if (bonus <= 0.0f || st == NULL || ms == NULL)
            return 1.0f;
        defense::Constants k = defense::constants();
        float lo = st->cutDamageMin, hi = k.found ? k.cut99 : 200.0f;
        float dex = st->dexterityActual();
        return ratio(term(lo, hi, (s.skillMain + dex * (1.0f + bonus)) * ms->cutDamage),
                     term(lo, hi, (s.skillMain + dex) * ms->cutDamage));
    }

    void applyDexDamage(Character* who, Damages& d)
    {
        stance::Info s = stance::evaluate(who, NULL);
        if (!s.dual)
            return;

        float inFlow = flow::dexBonus(who);
        float k = dexBonusCut(who->getStats(), s, balance::dexBonus() + inFlow);
        if (k != 1.0f)
        {
            d.cut *= k;
            diag::count("dex_bonus_damage");
            if (inFlow > 0.0f)
                diag::count("flow_damage");
        }
    }

    float offTargetRatio(CharStats* st, Weapon* off, Character* victim)
    {
        if (st == NULL || off == NULL || off->data == NULL || victim == NULL)
            return 1.0f;
        RaceData* race = victim->getRace();
        float mainK = 1.0f, offK = 1.0f;
        if (race != NULL && race->robot)
        {
            mainK = st->bonusRobots;
            offK = kit::fcsFloat(off->data, "robot damage mult", 1.0f);
        }
        else if (victim->isAnimal() != NULL)
        {
            mainK = st->bonusAnimals;
            offK = kit::fcsFloat(off->data, "animal damage mult", 1.0f);
        }
        else if (victim->isHuman() != NULL)
        {
            mainK = st->bonusHumans;
            offK = kit::fcsFloat(off->data, "human damage mult", 1.0f);
        }
        if (race != NULL && race->data != NULL)
        {
            auto m = st->bonusRaces.find(race->data);
            if (m != st->bonusRaces.end())
                mainK *= m->second;
            const Ogre::vector<GameDataReference>::type* v = off->data->getReferenceListIfExists("race damage");
            if (v != NULL)
                for (size_t k = 0; k < v->size(); ++k)
                    if ((*v)[k].sid == race->data->stringID)
                    {
                        offK *= (float)(*v)[k].values.value[0] * 0.01f;
                        break;
                    }
        }
        if (!(mainK > 0.0f) || !_finite(mainK) || !_finite(offK) || offK < 0.0f)
            return 1.0f;
        float r = offK / mainK;
        if (r != 1.0f)
            diag::count(r > 1.0f ? "offhand_target_bonus_up" : "offhand_target_bonus_down");
        return r;
    }

    void scaleOffHand(Character* attacker, Character* victim, const stance::Info& s, Damages& d, float& cutRatio,
                      float& bluntRatio)
    {
        CharStats* st = attacker->getStats();
        Sword* ms = s.main->isSword();
        Sword* os = s.off->isSword();
        cutRatio = bluntRatio = 1.0f;
        if (st != NULL && ms != NULL && os != NULL)
        {
            float dex = st->dexterityActual();
            float str = st->strengthActual();

            defense::Constants k = defense::constants();
            float cutLo = st->cutDamageMin;
            float cutHi = k.found ? k.cut99 : 200.0f;
            float bluntLo = st->bluntDamageMin;
            float bluntHi = k.found ? k.blunt99 : 200.0f;
            cutRatio = ratio(term(cutLo, cutHi, (s.skillOff + dex) * os->cutDamage),
                             term(cutLo, cutHi, (s.skillMain + dex) * ms->cutDamage));
            bluntRatio = ratio(term(bluntLo, bluntHi, (0.5f * s.skillOff + 1.5f * str) * os->bluntDamage),
                               term(bluntLo, bluntHi, (0.5f * s.skillMain + 1.5f * str) * ms->bluntDamage));
        }

        float k = balance::at(balance::OFF_HAND_DAMAGE, s.dp);

        float own = kit::fcsFloat(s.off->data, "nitoryu off hand damage", 1.0f);
        if (own != 1.0f)
        {
            k *= own < 0.0f ? 0.0f : own;
            diag::count("offhand_weapon_factor");
        }
        float t = offTargetRatio(st, s.off, victim);
        d.cut *= k * cutRatio * t;
        d.blunt *= k * bluntRatio * t;
        d.pierce *= k * t;
        d.bleedMult = s.off->bleedDamage;

        d.armourPenetration = kit::fcsFloat(s.off->data, "armour penetration", 0.0f);
    }

    HitMaterialType (*hitBy_orig)(Character*, CutDirection, Damages&, Character*, CombatTechniqueData*, int) = NULL;
    HitMaterialType hitBy_inner(Character* self, CutDirection dir, Damages& damage, Character* who,
                                CombatTechniqueData* attack, int comboID);

    struct Tally
    {
        std::string name;
        bool dual;
        int landed, blocked, missed, ours;
        float dealt, taken;

        std::string stats;
        std::map<std::string, int> vs;
    };

    std::string statsText(Character* c)
    {
        CharStats* st = c->getStats();
        if (st == NULL)
            return "-";
        RaceData* race = c->getRace();
        Weapon* w = c->getCurrentWeapon();
        stance::Info s = stance::evaluate(c, NULL);
        StatsEnumerated ws = w != NULL ? stance::statFor(w->category) : STAT_NONE;
        char buf[400];
        sprintf_s(buf, "race=%s weapon=%s skill=%.0f atk=%.0f def=%.0f dodge=%.0f dex=%.0f str=%.0f tough=%.0f ath=%.0f",
                  race != NULL && race->data != NULL ? race->data->name.c_str() : "-",
                  w != NULL ? w->displayName.c_str() : "-",
                  ws != STAT_NONE ? st->getStat(ws, false) : 0.0f,
                  st->getStat(STAT_MELEE_ATTACK, false) + (float)st->skillBonusAttack_melee(true),
                  st->getStat(STAT_MELEE_DEFENCE, false) + (float)st->skillBonusDefence(true),
                  st->getStat(STAT_DODGE, false), st->getStat(STAT_DEXTERITY, false), st->getStat(STAT_STRENGTH, false),
                  st->getStat(STAT_TOUGHNESS, false), st->getStat(STAT_ATHLETICS, false));
        std::string r = buf;
        if (s.dual)
        {
            sprintf_s(buf, " dp=%.0f tier=%d ambi=%.0f ambi_eff=%.0f", s.dp, s.tier, s.ambi, s.ambiEff);
            r += buf;
        }
        for (size_t i = 0; i < r.size(); ++i)
            if (r[i] == '\'')
                r[i] = '`';
        return r;
    }
    CRITICAL_SECTION g_fightLock;
    std::map<Character*, Tally> g_fight;

    Tally& tallyOf(Character* c)
    {
        std::map<Character*, Tally>::iterator it = g_fight.find(c);
        if (it == g_fight.end())
        {
            Tally t;
            t.name = c->displayName;
            t.dual = false;
            t.landed = t.blocked = t.missed = t.ours = 0;
            t.dealt = t.taken = 0.0f;
            t.stats = statsText(c);
            it = g_fight.insert(std::make_pair(c, t)).first;
        }
        return it->second;
    }

    bool hitLanded(HitMaterialType r) { return r != HIT_MISSED && r != HIT_SWORD; }

    HitMaterialType origHit(Character* self, CutDirection dir, Damages& damage, Character* who,
                            CombatTechniqueData* attack, int comboID)
    {
        HitMaterialType r = hitBy_orig(self, dir, damage, who, attack, comboID);
        if (!g_enabled || !diag::stage() || self == NULL || who == NULL)
            return r;
        bool whoDual = stance::evaluate(who, NULL).dual;
        bool selfDual = stance::evaluate(self, NULL).dual;
        if (!(whoDual || selfDual || who->isPlayerCharacter() || self->isPlayerCharacter()))
            return r;
        float dmg = hitLanded(r) ? damage.cut + damage.blunt + damage.pierce : 0.0f;
        EnterCriticalSection(&g_fightLock);
        if (g_fight.size() < 256)
        {
            Tally& a = tallyOf(who);
            a.dual = a.dual || whoDual;
            if (hitLanded(r)) ++a.landed; else if (r == HIT_SWORD) ++a.blocked; else ++a.missed;
            if (isNitoryuTechnique(attack))
                ++a.ours;
            a.dealt += dmg;
            ++a.vs[self->displayName];
            Tally& v = tallyOf(self);
            ++v.vs[who->displayName];
            v.dual = v.dual || selfDual;
            v.taken += dmg;
        }
        LeaveCriticalSection(&g_fightLock);
        return r;
    }

    void noteLandedHit(Character* victim, Character* who, const Damages& d)
    {
        if (!g_enabled || who == NULL || victim == NULL || !stance::evaluate(who, NULL).dual)
            return;
        CombatClass* vc = victim->getCombatClass();
        char key[48];
        sprintf_s(key, "flesh_victim_state_%d", vc != NULL ? (int)vc->combatState : -1);
        diag::count(key);
        float dmg = d.cut + d.blunt + d.pierce;
        diag::count(dmg < 5.0f ? "flesh_damage_0_5" : dmg < 10.0f ? "flesh_damage_5_10" : dmg < 20.0f ? "flesh_damage_10_20"
                    : dmg < 40.0f ? "flesh_damage_20_40" : "flesh_damage_40_up");
    }

    HitMaterialType hitBy_hook(Character* self, CutDirection dir, Damages& damage, Character* who,
                               CombatTechniqueData* attack, int comboID)
    {
        HitMaterialType r = hitBy_inner(self, dir, damage, who, attack, comboID);
        if (hitLanded(r))
        {
            arms::clipHit(self);
            noteLandedHit(self, who, damage);
        }
        return r;
    }

    struct EchoNames { const char* notDual; const char* blocked; const char* missed; const char* landed; const char* line; };
    const EchoNames FOREIGN_ECHO = { "foreign_dash_hit_not_dual", "foreign_dash_off_blocked", "foreign_dash_off_missed",
                                     "foreign_dash_off_landed", "foreign_dash_hit" };

    HitMaterialType echoHit(Character* self, CutDirection dir, Damages& damage, Character* who,
                            CombatTechniqueData* attack, int comboID, const EchoNames& n)
    {

        __declspec(align(16)) char offBuf[sizeof(Damages)];
        memcpy(offBuf, &damage, sizeof(Damages));
        Damages& off = *reinterpret_cast<Damages*>(offBuf);
        HitMaterialType r = origHit(self, dir, damage, who, attack, comboID);
        stance::invalidate(who);
        stance::Info s = stance::evaluate(who, NULL);
        if (!s.dual)
        {
            diag::count(n.notDual);
            return r;
        }
        float cr = 1.0f, br = 1.0f;
        float cutMain = off.cut;
        scaleOffHand(who, self, s, off, cr, br);
        float cutScaled = off.cut;
        ambidex::noteImpact(who, attack, true);
        HitMaterialType r2 = origHit(self, dir, off, who, attack, comboID);
        diag::count(r2 == HIT_SWORD ? n.blocked : (r2 == HIT_MISSED ? n.missed : n.landed));
        if (hitLanded(r2))
        {
            arms::clipHit(self);
            noteLandedHit(self, who, off);
        }
        if (diag::takeDetail())
            kit::Diag(n.line).kv("attacker", nameOf(who)).kv("victim", nameOf(self))
                .kv("tech", attack->animation).kv("combo", comboID).kv("main_result", (int)r).kv("off_result", (int)r2)
                .kv("cut_in", cutMain).kv("cut_off", cutScaled).kv("cut_off_after_armour", off.cut).emit();
        return r;
    }

    HitMaterialType hitBy_inner(Character* self, CutDirection dir, Damages& damage, Character* who,
                                CombatTechniqueData* attack, int comboID)
    {
        diag::countThread();

        defense::HitScope scope(self, who, attack, comboID);

        {
            CombatTechniqueData::ImpactPoint* ip = isNitoryuTechnique(attack) && comboID >= 0
                && comboID < attack->numImpactPoints() ? attack->impactPoint(comboID) : NULL;
            ambidex::noteImpact(who, attack, ip != NULL && (int)ip->limb == 0);
        }

        if (g_enabled && who != NULL && !isNitoryuTechnique(attack) && lefty::active(who))
        {
            float k = lefty::damageFactor(who);
            damage.cut *= k;
            damage.blunt *= k;
            damage.pierce *= k;
            HitMaterialType r = origHit(self, dir, damage, who, attack, comboID);
            diag::count(r == HIT_SWORD ? "lefty_hit_blocked" : (r == HIT_MISSED ? "lefty_hit_missed" : "lefty_hit_landed"));
            if (diag::takeDetail())
                kit::Diag("lefty_hit").kv("attacker", nameOf(who)).kv("victim", nameOf(self))
                    .kv("tech", attack != NULL ? attack->animation : std::string("?")).kv("damage_factor", k)
                    .kv("cut_after_armour", damage.cut).kv("result", (int)r).emit();
            return r;
        }

        if (g_enabled && who != NULL)
            applyDexDamage(who, damage);
        if (g_enabled && who != NULL && combat::twinsForeignDash(who, attack))
            return echoHit(self, dir, damage, who, attack, comboID, FOREIGN_ECHO);

        TechRec echoRec;
        if (g_enabled && who != NULL && techRec(attack, echoRec) && echoRec.def.kind != moves::OTHER && echoRec.def.echo)
        {
            std::string tag(echoRec.tag);
            std::string notDual = tag + "_hit_not_dual", blocked = tag + "_off_blocked", missed = tag + "_off_missed",
                        landed = tag + "_off_landed", line = tag + "_hit";
            EchoNames names = { notDual.c_str(), blocked.c_str(), missed.c_str(), landed.c_str(), line.c_str() };
            return echoHit(self, dir, damage, who, attack, comboID, names);
        }
        if (!g_enabled || who == NULL || !isNitoryuTechnique(attack)
            || comboID < 0 || comboID >= attack->numImpactPoints())
            return origHit(self, dir, damage, who, attack, comboID);
        CombatTechniqueData::ImpactPoint* p = attack->impactPoint(comboID);
        bool twin = combat::moveKind(attack) == moves::TWIN;
        if (p == NULL || (int)p->limb != 0)
        {
            diag::count("offhand_tech_main_impact");

            blades::Scissor scissor(who, self, twin ? stance::evaluate(who, NULL) : notDual("not_twin"), false, twin);
            HitMaterialType r = origHit(self, dir, damage, who, attack, comboID);
            scissor.landed(r);
            return r;
        }

        stance::invalidate(who);
        stance::Info s = stance::evaluate(who, NULL);
        if (!s.dual)
        {
            diag::count("offhand_hit_not_dual");
            return origHit(self, dir, damage, who, attack, comboID);
        }

        bool splits = twin || combat::moveKind(attack) == moves::FINISHER;
        Character* flanker = splits ? blades::flanker(who, self, s, attack, comboID) : NULL;
        if (flanker != NULL)
        {
            float fcr = 1.0f, fbr = 1.0f;
            float cutIn = damage.cut;

            scaleOffHand(who, flanker, s, damage, fcr, fbr);
            float cutOut = damage.cut;
            HitMaterialType fr;
            {
                defense::HitScope inner(flanker, who, attack, comboID);
                fr = origHit(flanker, dir, damage, who, attack, comboID);
            }
            diag::count(fr == HIT_SWORD ? "split_hit_blocked" : (fr == HIT_MISSED ? "split_hit_missed" : "split_hit_landed"));
            if (hitLanded(fr))
            {
                arms::clipHit(flanker);
                noteLandedHit(flanker, who, damage);
                mugen::onTwinLanded(who);
            }
            if (diag::takeDetail())
                kit::Diag("split_hit").kv("attacker", nameOf(who)).kv("main_target", nameOf(self))
                    .kv("flanker", nameOf(flanker)).kv("tech", attack->animation).kv("combo", comboID)
                    .kv("cut_in", cutIn).kv("cut_out", cutOut).kv("cut_after_armour", damage.cut)
                    .kv("result", (int)fr).emit();

            return HIT_MISSED;
        }
        float cutBefore = damage.cut, bluntBefore = damage.blunt;
        float cr = 1.0f, br = 1.0f;
        scaleOffHand(who, self, s, damage, cr, br);

        float cutScaled = damage.cut, bluntScaled = damage.blunt;

        blades::Scissor scissor(who, self, s, true, twin);
        HitMaterialType r = origHit(self, dir, damage, who, attack, comboID);
        scissor.landed(r);
        diag::count(r == HIT_SWORD ? "offhand_hit_blocked" : (r == HIT_MISSED ? "offhand_hit_missed" : "offhand_hit_landed"));

        if (hitLanded(r) && twin)
            mugen::onTwinLanded(who);
        if ((who->isPlayerCharacter() || self->isPlayerCharacter()) && diag::takeDetail())
            kit::Diag("offhand_hit").kv("attacker", nameOf(who)).kv("victim", nameOf(self))
                .kv("tech", attack->animation).kv("combo", comboID).kv("tier", s.tier)
                .kv("cut_in", cutBefore).kv("cut_out", cutScaled).kv("cut_after_armour", damage.cut)
                .kv("blunt_in", bluntBefore).kv("blunt_out", bluntScaled)
                .kv("cut_ratio", cr).kv("blunt_ratio", br).kv("result", (int)r).emit();
        return r;
    }

    double iniNumber(const std::string& path, const char* section, const char* key)
    {
        char buf[32] = { 0 };
        GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), path.c_str());
        return buf[0] != '\0' ? atof(buf) : -1.0;
    }

    bool hook(KenshiLib::HookStatus s, const char* what)
    {
        if (s == KenshiLib::SUCCESS)
            return true;
        ErrorLog(std::string("Nitoryu: could not hook ") + what);
        return false;
    }
}

namespace combat
{
    bool isOffHandTechnique(CombatTechniqueData* t) { return isNitoryuTechnique(t); }

    int moveKind(CombatTechniqueData* t)
    {
        TechRec rec;
        if (techRec(t, rec))
            return rec.def.kind;
        if (!isNitoryuTechnique(t))
            return moves::OTHER;
        bool left = false, right = false;
        int n = t->numImpactPoints();
        for (int i = 0; i < n; ++i)
        {
            CombatTechniqueData::ImpactPoint* p = t->impactPoint(i);
            if (p == NULL)
                continue;
            left = left || (int)p->limb == 0;
            right = right || (int)p->limb == 1;
        }
        return left && right ? moves::TWIN : moves::SINGLE;
    }

    bool isTwinTechnique(CombatTechniqueData* t) { return moves::KINDS[moveKind(t)].twin; }

    int armsModeOf(CombatTechniqueData* t)
    {
        TechRec rec;
        if (techRec(t, rec))
            return rec.def.kind != moves::OTHER ? (int)moves::armsMode(rec.def) : -1;
        int k = moveKind(t);
        return k == moves::OTHER ? -1 : (int)(moves::KINDS[k].twin ? arms::TWIN : arms::SWAP);
    }

    int attackClip(CombatTechniqueData* t)
    {
        TechRec rec;
        return techRec(t, rec) && rec.def.kind != moves::OTHER ? rec.def.clip : -1;
    }

    int tailClip(CombatTechniqueData* t)
    {
        TechRec rec;
        if (!techRec(t, rec) || rec.def.kind == moves::OTHER || rec.def.clip < 0)
            return -1;
        int k = rec.def.clip;
        if (rec.def.kind == moves::OPENER && !rec.def.needsClip)
            return k;
        return rec.def.needsClip && moves::fullBody(k) && arms::clipOn((arms::Clip)k) ? k : -1;
    }

    bool g_dashOn = true;
    bool dashOn() { return g_dashOn; }
    void setDashOn(bool on) { g_dashOn = on; }

    bool isForeignLongDash(CombatTechniqueData* t)
    {
        return t != NULL && !isNitoryuTechnique(t) && t->initialDistance >= cfg::f(cfg::LONG_DASH_MIN);
    }

    bool twinsForeignDash(Character* c, CombatTechniqueData* t)
    {
        return g_enabled && g_mirrorDash && c != NULL && isForeignLongDash(t) && hasSpecials(stance::evaluate(c, NULL))
            && !foreignQuiet(c, t);
    }

    void setEnabled(bool on) { g_enabled = on; }
    bool enabled() { return g_enabled; }

    void enforceSpeed(Character* c)
    {
        CharStats* st = c != NULL ? c->getStats() : NULL;
        if (!g_enabled || st == NULL)
            return;
        stance::Info s = stance::evaluate(c, NULL);
        if (!s.dual)
            return;
        SpeedRec rec = { -1.0f, -1.0f, -1.0f, -1.0f, 1.0f };
        EnterCriticalSection(&g_speedLock);
        std::map<CharStats*, SpeedRec>::iterator it = g_speed.find(st);
        if (it != g_speed.end())
            rec = it->second;
        LeaveCriticalSection(&g_speedLock);
        float flowK = flow::speedFactor(c), flowCap = flow::capBonus(c);

        bool fixed = false;
        if (st->attackSpeed != rec.dualAtk && st->attackSpeed > 0.0f)
        {
            float base = st->attackSpeed;
            float dual = stance::applySpeed(base, s.speedFactor * flowK, false, flowCap);
            if (dual != base)
            {
                st->attackSpeed = dual;
                noteSpeed(st, false, base, dual, flowK);
                fixed = true;
                diag::count(base == rec.gameAtk ? "speed_field_game_value_atk" : "speed_field_other_atk");
            }
        }
        else if (rec.flowAtk != flowK && rec.gameAtk > 0.0f && st->attackSpeed > 0.0f)
        {

            float dual = stance::applySpeed(rec.gameAtk, s.speedFactor * flowK, false, flowCap);
            st->attackSpeed = dual;
            noteSpeed(st, false, rec.gameAtk, dual, flowK);
            diag::count(flowK > 1.0f ? "flow_speed_on" : "flow_speed_off");
            if (diag::takeDetail())
                kit::Diag("flow_speed").kv("who", nameOf(c)).kv("flow", flowK > 1.0f).kv("base", rec.gameAtk)
                    .kv("was", rec.dualAtk).kv("now", dual).emit();
        }
        if (st->blockSpeed != rec.dualBlk && st->blockSpeed > 0.0f)
        {
            float base = st->blockSpeed;
            float dual = stance::applySpeed(base, s.speedFactor, true);
            if (dual != base)
            {
                st->blockSpeed = dual;
                noteSpeed(st, true, base, dual);
                fixed = true;
                diag::count(base == rec.gameBlk ? "speed_field_game_value_blk" : "speed_field_other_blk");
            }
        }
        if (fixed)
        {
            diag::count("speed_field_restored");
            static int left = 20;
            if (left > 0)
            {
                --left;
                kit::Diag("speed_field_restored").kv("who", nameOf(c)).kv("was_atk", rec.dualAtk)
                    .kv("now_atk", st->attackSpeed).kv("was_blk", rec.dualBlk).kv("now_blk", st->blockSpeed)
                    .kv("factor", s.speedFactor).emit();
            }
        }
    }

    std::string topOpponent(const Tally& t)
    {
        std::string best = "-";
        int n = 0;
        for (std::map<std::string, int>::const_iterator it = t.vs.begin(); it != t.vs.end(); ++it)
            if (it->second > n)
            {
                n = it->second;
                best = it->first;
            }
        return best;
    }

    void emitFight()
    {
        std::map<Character*, Tally> copy;
        EnterCriticalSection(&g_fightLock);
        copy.swap(g_fight);
        LeaveCriticalSection(&g_fightLock);
        for (std::map<Character*, Tally>::iterator it = copy.begin(); it != copy.end(); ++it)
        {
            const Tally& t = it->second;
            int n = t.landed + t.blocked + t.missed;
            if (n == 0 && t.taken <= 0.0f)
                continue;
            kit::Diag("fight").kv("who", t.name).kv("dual", t.dual).kv("impacts", n).kv("landed", t.landed)
                .kv("blocked", t.blocked).kv("missed", t.missed).kv("ours", t.ours).kv("dealt", t.dealt)
                .kv("taken", t.taken).kv("per_landed", t.landed > 0 ? t.dealt / t.landed : 0.0f)
                .kv("vs", topOpponent(t)).kv("stats", t.stats).emit();
        }
    }

    void forget(Character* c)
    {
        if (c == NULL)
            return;
        CharStats* st = c->getStats();
        EnterCriticalSection(&g_pickLock);
        for (std::map<UseKey, double>::iterator it = g_lastUse.lower_bound(UseKey(c, (GameData*)NULL));
             it != g_lastUse.end() && it->first.first == c;)
            g_lastUse.erase(it++);
        g_carry.erase(c);
        g_lastDouble.erase(c);
        g_crossArmed.erase(c);
        g_lastOff.erase(c);
        g_foreignQuiet.erase(c);
        LeaveCriticalSection(&g_pickLock);
        EnterCriticalSection(&g_fightLock);
        g_fight.erase(c);
        LeaveCriticalSection(&g_fightLock);
        if (st == NULL)
            return;
        EnterCriticalSection(&g_speedLock);
        g_speed.erase(st);
        LeaveCriticalSection(&g_speedLock);
        EnterCriticalSection(&g_reachLock);
        g_reachSaved.erase(st);
        LeaveCriticalSection(&g_reachLock);
    }

    int missingTrackCount()
    {
        EnterCriticalSection(&g_missLock);
        int n = (int)g_missSeen.size();
        LeaveCriticalSection(&g_missLock);
        return n;
    }

    void syncTimings()
    {
        std::map<std::string, std::vector<CombatTechniqueData*> > byId;
        std::map<std::string, std::string> sourceOf;
        EnterCriticalSection(&g_techLock);
        for (std::map<CombatTechniqueData*, TechRec>::iterator it = g_tech.begin(); it != g_tech.end(); ++it)
        {
            if (it->second.data == NULL)
                continue;
            byId[it->second.data->stringID].push_back(it->first);
            if (it->second.def.kind != moves::OTHER && it->second.timingFrom[0] != '\0')
                sourceOf[it->second.data->stringID] = it->second.timingFrom;
        }
        LeaveCriticalSection(&g_techLock);
        for (std::map<std::string, std::string>::iterator w = sourceOf.begin(); w != sourceOf.end(); ++w)
        {
            const std::string& id = w->first;
            const std::string& source = w->second;
            std::map<std::string, std::vector<CombatTechniqueData*> >::iterator o = byId.find(id), s = byId.find(source);
            if (o == byId.end() || s == byId.end() || s->second.empty())
            {
                diag::count("m3_source_missing");
                continue;
            }
            CombatTechniqueData* src = s->second[0];
            int changed = 0;
            bool countsDiffer = false;
            for (size_t k = 0; k < o->second.size(); ++k)
            {
                CombatTechniqueData* t = o->second[k];
                if (fabs(t->animSpeedMultiplier - src->animSpeedMultiplier) > 1e-4f)
                {
                    t->animSpeedMultiplier = src->animSpeedMultiplier;
                    ++changed;
                }
                if (fabs(t->hesitationPoint - src->hesitationPoint) > 1e-4f)
                {
                    t->hesitationPoint = src->hesitationPoint;
                    ++changed;
                }
                if (fabs(t->acceptableEndTime01 - src->acceptableEndTime01) > 1e-4f)
                {
                    t->acceptableEndTime01 = src->acceptableEndTime01;
                    ++changed;
                }
                int n = t->numImpactPoints();
                if (n != src->numImpactPoints())
                {
                    countsDiffer = true;
                    continue;
                }
                for (int j = 0; j < n; ++j)
                {
                    CombatTechniqueData::ImpactPoint* a = t->impactPoint(j);
                    CombatTechniqueData::ImpactPoint* b = src->impactPoint(j);
                    if (a == NULL || b == NULL)
                        continue;
                    if (fabs(a->impactAnimationFrame - b->impactAnimationFrame) > 1e-4f)
                    {
                        a->impactAnimationFrame = b->impactAnimationFrame;
                        ++changed;
                    }
                    if (fabs(a->motionStopsAnimationFrame - b->motionStopsAnimationFrame) > 1e-4f)
                    {
                        a->motionStopsAnimationFrame = b->motionStopsAnimationFrame;
                        ++changed;
                    }
                }
            }
            diag::count(changed > 0 ? "m3_timing_synced" : "m3_timing_same");
            if (changed > 0 || countsDiffer)
                kit::Diag("m3_timing").kv("record", id).kv("source", source).kv("changed", changed)
                    .kv("impact_counts_differ", countsDiffer).emit();
        }
    }

    void logTechniques()
    {
        if (g_techLogged)
            return;
        int total, ours, third, ignored, fromFields = 0, specials;
        std::vector<TechRec> added;
        EnterCriticalSection(&g_techLock);
        total = (int)g_tech.size();
        ours = g_techOurs;
        third = g_techThird;
        ignored = g_techIgnored;
        specials = (int)g_specials.size();
        for (std::map<CombatTechniqueData*, TechRec>::iterator it = g_tech.begin(); it != g_tech.end(); ++it)
        {
            if (it->second.def.kind == moves::OTHER)
                continue;
            if (it->second.fromFields)
                ++fromFields;
            if (it->second.number == 0 && added.size() < 24)
                added.push_back(it->second);
        }
        LeaveCriticalSection(&g_techLock);
        if (total == 0)
            return;
        g_techLogged = true;

        kit::Diag("techniques").kv("total", total).kv("ours", ours).kv("third_party", third)
            .kv("from_fields", fromFields).kv("specials", specials).kv("ignored", ignored)
            .kv("fallback", ours == 0 ? "category 21" : "-").emit();
        for (size_t i = 0; i < added.size(); ++i)
        {
            const TechRec& r = added[i];
            kit::Diag("move_added").kv("record", kit::recordId(r.data)).kv("name", kit::recordName(r.data))
                .kv("kind", moves::KINDS[r.def.kind].name).kv("arms", arms::modeName(moves::armsMode(r.def)))
                .kv("clip", r.def.clip >= 0 ? moves::CLIPS[r.def.clip].name : "-").kv("echo", r.def.echo)
                .kv("cooldown_ms", r.def.cooldownMs).kv("min_gap", r.def.minGap).kv("max_gap", r.def.maxGap)
                .kv("roll", r.def.roll).kv("chance", r.priority).emit();
        }
        if (ours == 0)
            diag::count("registry_fallback");
        if (ours + third > 0)
            syncTimings();
    }

    void applySettings()
    {
        g_carryOn = cfg::b(cfg::OFF_CARRY_ON);
        g_mirrorDash = cfg::b(cfg::MIRROR_LONG_DASH);
        g_farGap = cfg::f(cfg::FAR_PICK_GAP);
        double dh = cfg::d(cfg::DOUBLE_HIT_COOLDOWN_MS);
        g_doubleCooldownMs = dh < 0.0 ? 0.0 : dh;
        g_noRepeat = cfg::b(cfg::NO_REPEAT);
    }

    bool installHooks()
    {
        InitializeCriticalSection(&g_pickLock);
        InitializeCriticalSection(&g_callerLock);
        InitializeCriticalSection(&g_fightLock);
        InitializeCriticalSection(&g_reachLock);
        InitializeCriticalSection(&g_techLock);
        InitializeCriticalSection(&g_missLock);
        InitializeCriticalSection(&g_speedLock);
        applySettings();

        std::string ini = stance::iniFile();
        g_spinOn = GetPrivateProfileIntA("Moves", "spin", 1, ini.c_str()) != 0;
        g_iniDashCooldownMs = iniNumber(ini, "Moves", "dash_cooldown_ms");
        g_iniSpinCooldownMs = iniNumber(ini, "Moves", "spin_cooldown_ms");
        g_iniSpinMinSkill = (float)iniNumber(ini, "Moves", "spin_min_skill");
        bool ok = true;
        ok &= hook(KenshiLib::AddHook(KenshiLib::GetRealAddress(&CharStats::setWeapon), setWeapon_hook, &setWeapon_orig),
                   "CharStats::setWeapon");
        ok &= hook(KenshiLib::AddHook(KenshiLib::GetRealAddress(&CharStats::_calculateWeaponWeightSpeedMultiplier),
                                      weightSpeed_hook, &weightSpeed_orig),
                   "CharStats::_calculateWeaponWeightSpeedMultiplier");
        ok &= hook(KenshiLib::AddHook(KenshiLib::GetRealAddress(&CharStats::calculateAttackOrBlockSpeed), speed_hook, &speed_orig),
                   "CharStats::calculateAttackOrBlockSpeed");
        ok &= hook(KenshiLib::AddHook(KenshiLib::GetRealAddress(&CharStats::getMeleeAttack), meleeAttack_hook, &meleeAttack_orig),
                   "CharStats::getMeleeAttack");
        ok &= hook(KenshiLib::AddHook(KenshiLib::GetRealAddress(&CharStats::_calculateBlockChance), blockChance_hook, &blockChance_orig),
                   "CharStats::_calculateBlockChance");
        ok &= hook(KenshiLib::AddHook(KenshiLib::GetRealAddress(&CharStats::_NV_getEquippedWeaponSkill),
                                      equippedSkill_hook, &equippedSkill_orig),
                   "CharStats::getEquippedWeaponSkill");
        ok &= hook(KenshiLib::AddHook(KenshiLib::GetRealAddress(&CharStats::chooseAttack), choose_hook, &choose_orig),
                   "CharStats::chooseAttack");
        ok &= hook(KenshiLib::AddHook(KenshiLib::GetRealAddress(&Character::_NV_hitByMeleeAttack), hitBy_hook, &hitBy_orig),
                   "Character::hitByMeleeAttack");

        if (!hook(KenshiLib::AddHook(KenshiLib::GetRealAddress(&CombatTechniqueData::_CONSTRUCTOR), techCtor_hook, &techCtor_orig),
                  "CombatTechniqueData::CombatTechniqueData"))
            kit::Diag("techniques").kv("registry", "off").kv("fallback", "category 21").emit();
        return ok;
    }
}
