// The combat hooks. What each changes in the vanilla flow:
//   the attack speed and weight formula    -> the strength rule, the speed factor
//   the block chance formula               -> the divided guard, clumsiness (defence)
//   the technique pool and its selection   -> off-hand strikes (category ATTACK_NULL)
//   the damage formula                     -> off-hand damage scaling
//   an impact hits every victim in its arc -> the scaling is per hitByMeleeAttack call
// Threads:
//   hitBy_hook - combat threads.
//   choose_hook - the combat thread (the AI's pick), and inside setWeapon on a draw (t_inSetWeapon).
//   setWeapon_hook, weightSpeed_hook, speed_hook, meleeAttack_hook, blockChance_hook, equippedSkill_hook - the
//   combat threads and the main loop (the stats window, a draw).  techCtor_hook - the data load.
//   Locks, each for its own map and never held across a game original:
//     g_techLock - g_tech, g_specials, the technique counts;  g_callerLock - g_callerSeen;
//     g_speedLock - g_speed;  g_pickLock - the per-fighter pick state (cooldowns, carry-over);
//     g_missLock - g_missSeen;  g_reachLock - g_reachSaved;  g_fightLock - g_fight (the stage ledger).
//   Thread-locals carry one call's context to the hooks it calls into (t_inSetWeapon, t_skillOverride...).

#include "kit/input.h"   // Windows.h with NOMINMAX first

#include "kit/log.h"
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
#include "Foreign.h"
#include "SoundTrace.h"
#include "Moves.h"
#include "Lang.h"
#include "Pressure.h"
#include "Mastery.h"
#include "Blades.h"
#include "kit/diag.h"
#include "kit/fcs.h"
#include "Hooks.h"

namespace
{
    volatile bool g_enabled = true;

    // Per-thread context: the speed/weight functions also run from setWeapon wherever that is called.
    __declspec(thread) bool t_inSetWeapon = false;
    __declspec(thread) Weapon* t_settingWeapon = NULL;
    __declspec(thread) float t_skillOverride = -1.0f;
    __declspec(thread) CharStats* t_overrideFor = NULL;   // the override answers only for it

    // The engine fields a hook changes around an original call are restored by a destructor, so a
    // C++ exception thrown inside the game (Ogre throws for a missing animation name) cannot leave
    // a fighter with weapon type 21 or our skill override. Nested scopes restore what was there
    // before them.
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

    // ---- the registry of techniques ------------------------------------------------------
    // Every CombatTechniqueData is built once from its COMBAT_TECHNIQUE record
    // (setupCombatTechniques -> CombatTechniqueData(GameData*)). The constructor hook records each
    // one's GameData and knows ours by string id, so no track name is compared. If the registry
    // missed them (built before the hook, or copied by value), category 21 still marks ours
    // (`registry_fallback`). What a technique is to Nitoryu comes from its record's `nitoryu ...`
    // fields (moves::Def, docs/FCS.md), whoever made the record: a mod adds a dual-wield move as
    // data. Our own records fall back to the Moves.h table when the fields are missing.
    struct TechRec
    {
        GameData* data;
        int number;                 // our record's number ("<n>-Nitoryu.mod"), 0: not ours
        moves::Def def;             // def.kind == moves::OTHER: not a Nitoryu move
        float priority;             // the record's `chance`: the order among finishers and among openers
        bool fromFields;            // the def came from the record's fields (else the table, or not a move)
        char tag[20];               // counters and log lines: special_<tag>, <tag>_off_landed
        char timingFrom[48];        // `nitoryu timing from`: a string id, "" = none
        float weight;               // `nitoryu weight`, its share in the even pick (1 = as the others, 0 = never)
        float weightHigh;           // `nitoryu weight high`: the same at rank 3+, < 0: `nitoryu weight`
        bool mainHand;              // a strike of the right blade alone (`use R arm`, not `use L arm`)
    };
    CRITICAL_SECTION g_techLock;
    std::map<CombatTechniqueData*, TechRec> g_tech;     // under g_techLock
    int g_techOurs = 0;                                 // Nitoryu.mod's own moves
    int g_techThird = 0;                                // moves another mod added
    int g_techIgnored = 0;                              // records with `nitoryu move` set but outside the private pool
    bool g_techLogged = false;

    // The finishers and the openers, in pick order: the higher `chance` first; among equals the one with the
    // smaller max gap (the spin before the Dash). Under g_techLock; a pick copies what it needs.
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

    // [Moves] in Nitoryu.ini still tunes our own two openers (the player's override of their records).
    const int DASH_RECORD = 10;
    const int SPIN_RECORD = 12;
    const int BIG_CHOP_RECORD = 49;         // [Moves] big_chop_cooldown_ms overrides its `nitoryu cooldown ms`
    double g_iniDashCooldownMs = -1.0;      // [Moves] dash_cooldown_ms when present
    double g_iniBigChopCooldownMs = -1.0;   // [Moves] big_chop_cooldown_ms when present
    double g_iniSpinCooldownMs = -1.0;      // [Moves] spin_cooldown_ms when present
    float g_iniSpinMinSkill = -1.0f;        // [Moves] spin_min_skill when present
    bool g_spinOn = true;                   // [Moves] spin

    // Our records (Moves.h RECORDS, docs/FCS.md): string id -> number (0: not ours) -> kind.
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

    // The record's move, read when the game builds the technique. The order of the distance specials is
    // the FCS field `chance` (the higher goes first, docs/FCS.md).
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
        rec.weight = std::max(0.0f, kit::fcsFloat(d, "nitoryu weight", 1.0f));
        rec.weightHigh = kit::fcsFloat(d, "nitoryu weight high", -1.0f);
        rec.mainHand = kit::fcsBool(d, "use R arm", false) && !kit::fcsBool(d, "use L arm", true);
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
        // A move lives in the private pool (category ATTACK_NULL: no weapon picks it by itself). A record that
        // sets `nitoryu move` but kept a weapon flag would be drawn by the game for one-handed fighters too.
        if (rec.def.kind != moves::OTHER && self != NULL && !self->skillTypes[ATTACK_NULL])
        {
            rec.def = moves::noMove();
            rec.fromFields = false;
            ++g_techIgnored;        // under g_techLock (the caller)
        }
        copyText(rec.tag, sizeof(rec.tag),
                 table != NULL && table->tag[0] != '\0' ? table->tag : moves::KINDS[rec.def.kind].name);
        return rec;
    }

    CombatTechniqueData* (*techCtor_orig)(CombatTechniqueData*, GameData*) = NULL;
    CombatTechniqueData* techCtor_hook(CombatTechniqueData* self, GameData* d)
    {
        diag::HookCall hc(diag::HK_TECH_CTOR);
        CombatTechniqueData* r = techCtor_orig(self, d);
        EnterCriticalSection(&g_techLock);
        TechRec rec = describe(self, d);
        g_tech[self] = rec;
        if (rec.def.kind != moves::OTHER)
        {
            hc.ours();
            ++(rec.number > 0 ? g_techOurs : g_techThird);
            if (moves::KINDS[rec.def.kind].special)
            {
                // One entry per record: a rebuilt technique replaces its earlier object.
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

    // A pick's copy of the specials of one kind, in pick order. Returns how many.
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

    // Registry lookup: false when the technique was never registered.
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

    // Ours by the registry; category 21 when the registry does not know the technique.
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


    // copies of the settings (cfg: the table < FCS < the ini), set by combat::applySettings
    bool g_mirrorDash = true;                       // cfg::MIRROR_LONG_DASH ([Moves] mirror_long_dash)
    float g_farGap = 35.0f;                         // cfg::FAR_PICK_GAP ([Moves] far_pick_gap)

    // The specials (the Cross, the Dash, another mod's mirrored dash): gated by rank and effective Dual Wielding.
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

    // Inside setWeapon the new weapon is not in the hands yet: evaluate with it.
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

    // ---- who asks for an attack from far away -------------------------------------
    // With some mods fighters pick attacks from far outside reach. The pick hook's return addresses
    // name the caller as module + offset (an inline hook's jmp adds no frame). Logged once per caller
    // chain.

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

    // "Kenshi_x64.exe+0x1a2b" -> "Kenshi_x64_exe_0x1a2b" (a counter key the log parser reads).
    std::string keyOf(const std::string& s)
    {
        std::string r(s);
        for (size_t i = 0; i < r.size(); ++i)
            if (!isalnum((unsigned char)r[i]))
                r[i] = '_';
        return r;
    }

    CRITICAL_SECTION g_callerLock;
    std::set<std::string> g_callerSeen;             // under g_callerLock

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
                continue;                       // our own frames (the hook itself)
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

    // ---- setWeapon: marks the context, logs the stance the draw produced -------

    void restoreReach(CharStats* st);       // below

    void (*setWeapon_orig)(CharStats*, Weapon*) = NULL;
    void setWeapon_hook(CharStats* self, Weapon* w)
    {
        diag::HookCall hc(diag::HK_SET_WEAPON);
        {
            SetWeaponScope scope(w);
            setWeapon_orig(self, w);
        }
        if (!g_enabled || self == NULL || self->me == NULL)
            return;
        hc.ours();
        stance::invalidate(self->me);
        if (w == NULL)
        {
            visual::noteSheathe(self->me);  // sheathing: the tick takes the left blade away
            restoreReach(self);
            return;
        }
        stance::Info s = stance::evaluate(self->me, w);
        diag::count((std::string("draw_") + s.reason).c_str());
        bool player = self->me->isPlayerCharacter();
        // Visible confirmation for the player's own fighters (not a skill warning).
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

    // ---- the second weapon's own bonuses ----------------------------------------
    // The off weapon adds a share of its attack, defence and indoor bonuses (half: +4 attack
    // becomes +2). The main weapon's are the game's own.

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
        // two weapon types in the pair (katana + sabre): the second weapon's own defence bonus in full
        float own = blades::offDefenceShare(s, (float)sw->modDefence, usual) * (float)sw->modDefence;
        return own + (indoors(c) ? usual * (float)sw->modIndoors : 0.0f);
    }

    // ---- strength rule: W_dual replaces the weapon weight ----------------------

    void (*weightSpeed_orig)(CharStats*, float) = NULL;
    void weightSpeed_hook(CharStats* self, float weaponWeight)
    {
        diag::HookCall hc(diag::HK_WEIGHT_SPEED);
        stance::Info s = stanceOf(self);
        if (s.dual)
        {
            hc.ours();
            diag::count("weight_dual");
            weightSpeed_orig(self, s.weightDual);
            return;
        }
        weightSpeed_orig(self, weaponWeight);
    }

    // ---- speed factor ------------------------------------------------------

    float (*speed_orig)(CharStats*, float, float, bool) = NULL;
    // The last speeds our hook produced for a dual fighter, and the game's own value under them, so
    // a later writer (another plugin's setWeapon hook, a stats refresh that bypasses ours) can be
    // caught and undone (enforceSpeed). flowAtk is the flow factor inside dualAtk (1 outside flow),
    // so the main loop sees flow begin and end and rewrites the field from the same base.
    struct SpeedRec { float gameAtk, dualAtk, gameBlk, dualBlk, flowAtk; };
    CRITICAL_SECTION g_speedLock;
    std::map<CharStats*, SpeedRec> g_speed;         // under g_speedLock

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
        diag::HookCall hc(diag::HK_ATTACK_SPEED);
        float r = speed_orig(self, weightMult, skill, isBlock);
        stance::Info s = stanceOf(self);
        if (s.dual)
        {
            hc.ours();
            diag::count(isBlock ? "speed_block_dual" : "speed_attack_dual");
            // The game's own raw speed again, with Dexterity x (1 + dex_bonus):
            // raw = clamp((0.6 Dex + 0.4 skill) / 100, lo, hi).
            float bonus = balance::dexBonus();
            if (bonus > 0.0f && self != NULL)
            {
                float dex = self->dexterityActual();
                float lo = isBlock ? 0.6f : 0.8f, hi = isBlock ? 4.0f : 1.2f;
                float raw0 = std::max(lo, std::min(hi, (0.6f * dex + 0.4f * skill) * 0.01f));
                float raw1 = std::max(lo, std::min(hi, (0.6f * dex * (1.0f + bonus) + 0.4f * skill) * 0.01f));
                if (raw1 > raw0)
                {
                    r = std::min(r * raw1 / raw0, std::max(hi, r));    // the game's own cap still holds
                    diag::count(isBlock ? "dex_bonus_speed_block" : "dex_bonus_speed_attack");
                }
            }
            float game = r;
            // In flow the attack speed has its own factor and a higher cap (blocks: unchanged).
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

    // ---- clumsiness (attack) ---------------------------------------------------

    float (*meleeAttack_orig)(CharStats*) = NULL;
    float meleeAttack_hook(CharStats* self)
    {
        diag::HookCall hc(diag::HK_MELEE_ATTACK);
        float r = meleeAttack_orig(self);
        if (!g_enabled || self == NULL || t_inSetWeapon)
            return r;
        stance::Info s = stance::evaluate(self->me, NULL);
        if (s.dual)
        {
            hc.ours();
            r += offAttackBonus(s, self->me) - s.clumsy;
        }
        return r;
    }

    // ---- the character panel shows the fight's numbers ----------------------------
    // The panel's "Attack: 85 (+16)" is CharStats::getGUIDataForMainInfo: the total is
    // getMeleeAttack_melee(), the bracket skillBonusAttack_melee(true); "Defense:" is
    // getMeleeDefence_melee(true) and skillBonusDefence(true). The fight goes another way (getMeleeAttack,
    // _calculateBlockChance: the hooks above), so what we add there would never show. Inside the panel's call,
    // and only there, the four give a drawn dual fighter the same sums: the bracket gains the second blade's
    // share, the total also loses the clumsiness (and the divided guard). The fight's own calls are not touched.
    __declspec(thread) int t_inPanel = 0;           // inside getGUIDataForMainInfo
    __declspec(thread) int t_inPanelTotal = 0;      // inside a total, which asks for the bracket itself
    __declspec(thread) int t_inPanelStance = 0;     // inside stance::evaluate called from these hooks (it reads the brackets)
    bool g_panelPair = true;                        // [Stats] panel_pair_bonus

    struct Depth
    {
        explicit Depth(int& n) : n_(n) { ++n_; }
        ~Depth() { --n_; }
    private:
        int& n_;
        Depth(const Depth&);
        Depth& operator=(const Depth&);
    };

    bool panelDual(CharStats* self, stance::Info& s)
    {
        if (!g_enabled || !g_panelPair || t_inPanel == 0 || self == NULL || self->me == NULL || t_inSetWeapon)
            return false;
        // The stance's Path reads skillBonusAttack_melee and skillBonusDefence itself (Stance.cpp effAttack /
        // effDefence): on a stale stance cache this hook would ask for the stance, which asks for the bonus,
        // which comes back here, without end (a stack overflow, no crash dump). While the stance is being
        // worked out, the four hooks pass the game's own numbers through.
        if (t_inPanelStance != 0)
            return false;
        Depth d(t_inPanelStance);
        s = stance::evaluate(self->me, NULL);
        return s.dual;
    }

    int nearest(float v) { return (int)(v < 0.0f ? v - 0.5f : v + 0.5f); }

    void (*mainInfo_orig)(CharStats*, DatapanelGUI*, int, bool) = NULL;
    void mainInfo_hook(CharStats* self, DatapanelGUI* panel, int category, bool combatMode)
    {
        Depth d(t_inPanel);
        mainInfo_orig(self, panel, category, combatMode);
    }

    int (*bonusAttack_orig)(CharStats*, bool) = NULL;
    int bonusAttack_hook(CharStats* self, bool environment)
    {
        int r = bonusAttack_orig(self, environment);
        stance::Info s;
        if (t_inPanel != 0 && t_inPanelTotal == 0 && panelDual(self, s))
        {
            r += nearest(offAttackBonus(s, self->me));
            diag::count("panel_attack_bonus");
        }
        return r;
    }

    float (*attackMelee_orig)(CharStats*) = NULL;
    float attackMelee_hook(CharStats* self)
    {
        if (t_inPanel == 0)
            return attackMelee_orig(self);
        float r;
        {
            Depth d(t_inPanelTotal);
            r = attackMelee_orig(self);
        }
        stance::Info s;
        if (panelDual(self, s))
            r += offAttackBonus(s, self->me) - s.clumsy;        // as meleeAttack_hook
        return r;
    }

    int (*bonusDefence_orig)(CharStats*, bool) = NULL;
    int bonusDefence_hook(CharStats* self, bool environment)
    {
        int r = bonusDefence_orig(self, environment);
        stance::Info s;
        if (t_inPanel != 0 && t_inPanelTotal == 0 && panelDual(self, s))
        {
            r += nearest(offDefenceBonus(s, self->me));
            diag::count("panel_defence_bonus");
        }
        return r;
    }

    float (*defenceMelee_orig)(CharStats*, bool) = NULL;
    float defenceMelee_hook(CharStats* self, bool defensiveMode)
    {
        if (t_inPanel == 0)
            return defenceMelee_orig(self, defensiveMode);
        float r;
        {
            Depth d(t_inPanelTotal);
            r = defenceMelee_orig(self, defensiveMode);
        }
        stance::Info s;
        if (panelDual(self, s))
            r += offDefenceBonus(s, self->me) - s.clumsy - s.guardPenalty;      // as blockChance_hook's `versus`
        return r;
    }

    // ---- divided guard + clumsiness (defence) ----------------------------------
    // The block chance is rolled once per block start; a larger `versus` is the same as a lower defence.

    float (*blockChance_orig)(CharStats*, float) = NULL;
    float blockChance_hook(CharStats* self, float versus)
    {
        diag::HookCall hc(diag::HK_BLOCK_CHANCE);
        stance::Info s = stanceOf(self);
        if (s.dual)
        {
            hc.ours();
            diag::count("block_chance_dual");
            versus += s.clumsy + s.guardPenalty - offDefenceBonus(s, self->me);
        }
        else if (g_enabled && self != NULL && !t_inSetWeapon && lefty::active(self->me))
        {
            hc.ours();
            diag::count("lefty_block_chance");
            versus += lefty::blockPenalty(self->me);
        }
        return blockChance_orig(self, versus);
    }

    // ---- off-hand strikes: present category ATTACK_NULL and DP for one pick ------

    float (*equippedSkill_orig)(CharStats*) = NULL;
    float equippedSkill_hook(CharStats* self)
    {
        diag::HookCall hc(diag::HK_EQUIPPED_SKILL);
        // Only the picking fighter's skill is overridden, not every CharStats the game (or another
        // plugin) reads on this thread during the pick.
        if (t_skillOverride >= 0.0f && self == t_overrideFor)
        {
            hc.ours();
            return t_skillOverride;
        }
        return equippedSkill_orig(self);
    }

    // The per-fighter pick state (Cross/Dash cooldowns, carry-over) below. chooseAttack runs on the
    // one combat thread; the lock is cheap insurance.
    CRITICAL_SECTION g_pickLock;

    // What an off-hand pick should be. The pool (category ATTACK_NULL, gated by DP through `min
    // skill`) is drawn by the game's own weighting; we re-draw a few times until the wanted kind
    // comes up. The kinds are Moves.h's.
    typedef moves::Kind Want;

    Want kindOf(CombatTechniqueData* t)
    {
        Want k = (Want)combat::moveKind(t);
        return k == moves::OTHER ? moves::SINGLE : k;
    }

    const char* wantName(Want w) { return moves::KINDS[w].name; }

    // The fighter's current target is staggering (CombatClass::combatState STUMBLE): the Cross
    // finisher's window.
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

    // The Cross's second trigger: a target under full pressure (every stack of the fighter's own
    // pressure on it). Needs no stagger, so it does not depend on a modpack's damage or stagger
    // constants.
    bool targetFullyPressured(Character* me)
    {
        CombatClass* cc = me != NULL ? me->getCombatClass() : NULL;
        if (cc == NULL)
            return false;
        Character* t = cc->_getAttackTarget().getCharacter();
        return t != NULL && pressure::stacks(t, me) >= pressure::maxStacks();
    }

    CombatTechniqueData* (*choose_orig)(CharStats*, float, float, CombatTechniqueData*, bool) = NULL;

    // One cooldown per fighter and move (the finishers and the openers), game time. The key is the
    // move's record, so a rebuilt technique object keeps its cooldown.
    typedef std::pair<Character*, GameData*> UseKey;
    std::map<UseKey, double> g_lastUse;             // under g_pickLock

    double cooldownOf(const Special& s)
    {
        if (s.rec.number == DASH_RECORD && g_iniDashCooldownMs >= 0.0)
            return g_iniDashCooldownMs;
        if (s.rec.number == SPIN_RECORD && g_iniSpinCooldownMs >= 0.0)
            return g_iniSpinCooldownMs;
        return s.rec.def.cooldownMs;
    }

    // No counters (asked on every pick): the move is off its cooldown for this fighter.
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

    // `nitoryu cooldown ms` holds for every move of ours, not only the finishers and the openers (a move
    // that alone reaches from mid range would otherwise take half of all attacks). A plain move on its
    // cooldown is not drawn; its use is noted when the pick ends as it (finishPick).
    double plainCooldownMs(const TechRec& tr)
    {
        if (tr.number == BIG_CHOP_RECORD && g_iniBigChopCooldownMs >= 0.0)
            return g_iniBigChopCooldownMs;
        return tr.def.cooldownMs;
    }

    bool moveCooldownOver(CombatTechniqueData* t, Character* me, double now)
    {
        TechRec tr;
        if (t == NULL || me == NULL || !techRec(t, tr) || tr.def.kind == moves::OTHER || moves::KINDS[tr.def.kind].special)
            return true;
        double ms = plainCooldownMs(tr);
        if (ms <= 0.0)
            return true;
        EnterCriticalSection(&g_pickLock);
        std::map<UseKey, double>::iterator it = g_lastUse.find(UseKey(me, tr.data));
        bool ok = it == g_lastUse.end() || now - it->second >= ms || now < it->second;
        LeaveCriticalSection(&g_pickLock);
        return ok;
    }

    void noteMoveUse(CombatTechniqueData* t, Character* me, double now)
    {
        TechRec tr;
        if (t == NULL || me == NULL || !techRec(t, tr) || tr.def.kind == moves::OTHER || moves::KINDS[tr.def.kind].special
            || plainCooldownMs(tr) <= 0.0)
            return;
        EnterCriticalSection(&g_pickLock);
        if (g_lastUse.size() > 8192)
            g_lastUse.clear();
        g_lastUse[UseKey(me, tr.data)] = now;
        LeaveCriticalSection(&g_pickLock);
        diag::count("move_cooldown_started");
    }

    void count(const std::string& name) { diag::count(name.c_str()); }
    std::string tagOf(const Special& s) { return std::string(s.rec.tag); }

    // The DP a fighter needs for the move: the record's `min skill` (the pool's own gate), or the ini's for
    // the spin.
    float minSkillOf(const Special& s)
    {
        if (s.rec.number == SPIN_RECORD && g_iniSpinMinSkill >= 0.0f)
            return g_iniSpinMinSkill;
        return s.tech != NULL ? s.tech->minSkill : 0.0f;
    }

    bool switchedOn(const Special& s)
    {
        if (s.rec.number == DASH_RECORD)
            return combat::dashOn();        // [Moves] dash_slash
        if (s.rec.number == SPIN_RECORD)
            return g_spinOn;                // [Moves] spin
        return true;
    }

    // The move can be picked at all by this fighter: on, the DP at its skill, its clip on when it needs it (a
    // record with its own clip is not drawn while that clip is off: its base track alone would show).
    bool available(const Special& s, const stance::Info& st)
    {
        const moves::Def& d = s.rec.def;
        return switchedOn(s) && st.dp >= minSkillOf(s)
            && !(s.rec.number == SPIN_RECORD && st.tier < tuning::SPIN_MIN_TIER)
            && !(d.needsClip && d.clip >= 0 && !arms::clipOn((arms::Clip)d.clip));
    }

    // A distance special is "up" when only the distance and its roll are left to decide: one with a
    // lower `chance` waits for it.
    bool up(const Special& s, const stance::Info& st, Character* me, double now)
    {
        return available(s, st) && visual::bladeReady(me) && cooldownOver(s, me, now);
    }

    // The shared double-hit cooldown and the last off-hand move (no repeat), per fighter.
    std::map<Character*, double> g_lastDouble;             // under g_pickLock

    // A finisher trigger (stagger, full pressure, riposte) arms the Cross for CROSS_ARM_MS; it plays
    // at the next pick made in reach (with some mods the next attack is picked while the fighter
    // still walks in).
    std::map<Character*, double> g_crossArmed;             // under g_pickLock

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

    // The riposte finisher: this fighter's main blade blocked, or the second blade parried, within
    // CROSS_RIPOSTE_WINDOW_MS: the Cross answers in the enemy's dead time.
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
    std::map<Character*, CombatTechniqueData*> g_lastOff;  // under g_pickLock
    double g_doubleCooldownMs = 5000.0;                    // cfg::DOUBLE_HIT_COOLDOWN_MS
    bool g_noRepeat = true;                                // [Moves] no_repeat
    // Another mod's long dash picked while the cooldown runs: it plays one-bladed (twinsForeignDash).
    std::map<Character*, CombatTechniqueData*> g_foreignQuiet;   // under g_pickLock

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
        // Mugen shortens it at rank 5 (read before the lock: it evaluates the stance).
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

    // How often each ordinary move has been played, per rank, for the whole session (all fighters of that
    // rank together, so short fights even out too). A pick takes the least played of those that came up. Each
    // count is divided by the record's weight (`nitoryu weight`, at rank 3+ `nitoryu weight high`): weight 0.5
    // plays half as often, 0 never.
    const int PICK_TIERS = 6;
    std::map<CombatTechniqueData*, unsigned> g_played[PICK_TIERS];             // under g_pickLock

    int pickTier(int tier) { return tier < 0 ? 0 : tier >= PICK_TIERS ? PICK_TIERS - 1 : tier; }

    float pickWeight(CombatTechniqueData* t, int tier)
    {
        TechRec tr;
        if (!techRec(t, tr))
            return 1.0f;
        return tier >= 3 && tr.weightHigh >= 0.0f ? tr.weightHigh : tr.weight;
    }

    void notePlayed(int tier, CombatTechniqueData* t)
    {
        EnterCriticalSection(&g_pickLock);
        ++g_played[pickTier(tier)][t];
        LeaveCriticalSection(&g_pickLock);
    }

    // The least played of `n` candidates by played / weight (the first of equals, so the game's own order breaks
    // ties); a candidate of weight 0 is never taken. NULL: all of them weigh 0.
    CombatTechniqueData* leastPlayed(int tier, CombatTechniqueData** c, int n)
    {
        float w[16];
        for (int i = 0; i < n && i < 16; ++i)
            w[i] = pickWeight(c[i], tier);          // before the pick lock (techRec takes g_techLock)
        CombatTechniqueData* best = NULL;
        float bestS = 0.0f;
        EnterCriticalSection(&g_pickLock);
        std::map<CombatTechniqueData*, unsigned>& played = g_played[pickTier(tier)];
        for (int i = 0; i < n && i < 16; ++i)
        {
            if (w[i] <= 0.0f)
                continue;
            std::map<CombatTechniqueData*, unsigned>::iterator u = played.find(c[i]);
            float score = (float)(u != played.end() ? u->second : 0u) / w[i];
            if (best == NULL || score < bestS)
            {
                best = c[i];
                bestS = score;
            }
        }
        LeaveCriticalSection(&g_pickLock);
        return best;
    }

    // The `pick_balance` lines: every PICK_BALANCE_MS of game time, per rank, how often each move has been
    // played this session (`name:count:weight`), so a long test can be read from the log alone.
    const double PICK_BALANCE_MS = 120000.0;
    double g_pickBalanceLast = -1.0;                                             // under g_pickLock

    std::string moveName(CombatTechniqueData* t)
    {
        TechRec tr;
        if (techRec(t, tr) && tr.def.clip >= 0 && tr.def.clip < arms::CLIP_COUNT)
            return moves::CLIPS[tr.def.clip].name;
        if (techRec(t, tr) && tr.number > 0)
            return "rec" + std::to_string((long long)tr.number);
        return t != NULL ? t->animation : std::string("none");
    }

    void emitPickBalance(double now)
    {
        std::vector<std::pair<int, std::pair<CombatTechniqueData*, unsigned> > > rows;
        EnterCriticalSection(&g_pickLock);
        bool due = g_pickBalanceLast < 0.0 || now - g_pickBalanceLast >= PICK_BALANCE_MS || now < g_pickBalanceLast;
        if (due)
        {
            g_pickBalanceLast = now;
            for (int k = 0; k < PICK_TIERS; ++k)
                for (std::map<CombatTechniqueData*, unsigned>::iterator it = g_played[k].begin(); it != g_played[k].end(); ++it)
                    rows.push_back(std::make_pair(k, *it));
        }
        LeaveCriticalSection(&g_pickLock);
        if (!due)
            return;
        for (int k = 0; k < PICK_TIERS; ++k)
        {
            std::string list;
            for (size_t i = 0; i < rows.size(); ++i)
            {
                if (rows[i].first != k)
                    continue;
                char w[16];
                sprintf_s(w, "%.2g", pickWeight(rows[i].second.first, k));
                list += (list.empty() ? "" : ",") + moveName(rows[i].second.first) + ":" + std::to_string((unsigned long long)rows[i].second.second)
                        + ":" + w;
            }
            if (!list.empty())
                kit::Diag("pick_balance").kv("tier", k).kv("moves", list).emit();
        }
    }

    // The second blade's last pick was a riposte (ambidex: its impacts train more).
    std::map<Character*, CombatTechniqueData*> g_riposte;  // under g_pickLock

    void noteRiposte(Character* me, CombatTechniqueData* t)
    {
        EnterCriticalSection(&g_pickLock);
        if (t != NULL)
        {
            if (g_riposte.size() > 4096)
                g_riposte.clear();
            g_riposte[me] = t;
        }
        else
            g_riposte.erase(me);
        LeaveCriticalSection(&g_pickLock);
    }

    // Carry-over: an off-hand roll that found nothing in reach (with some mods fighters pick from
    // 20-35 away, our moves reach ~10-15) waits for the next pick instead of being lost, for up to
    // OFF_CARRY_MS of game time.
    struct Carry { Want want; double t; };
    std::map<Character*, Carry> g_carry;           // under g_pickLock
    bool g_carryOn = true;                          // [Moves] offhand_carry

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

    // One `base_tracks_missing` line per rig and track.
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

    // One dual pick: its arguments, the stance, and one budget of calls of the game's chooseAttack
    // shared by every stage (tuning::PICK_BUDGET). `draw` is a budgeted call (our draws, the
    // two-handed repick); `game` is the game's own pick when nothing of ours was taken, always made.
    struct Pick
    {
        CharStats* self;
        float range, reach;
        CombatTechniqueData* last;
        bool stationary;
        stance::Info s;
        bool special;           // hasSpecials: the Cross, the Dash, another mod's mirrored dash
        double now;             // defense::clockMs()
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

    // Up to `tries` draws from our pool (within the pick's budget); the first of the wanted kind
    // wins. A special move is never taken unless wanted. Returns NULL if nothing of ours fit.
    // noTwin (the double-hit cooldown) keeps twins out of the fallback too; the fighter's last
    // off-hand move is taken only when nothing else of the kind came up (no_repeat).
    // `exact` asks for one move (a finisher or an opener is drawn by its own record, so each keeps its
    // own cooldown, gaps and roll); the pool's weighting, distance fit and skill gate still decide if it comes up.
    // `hand` keeps the strikes of the right blade alone (TechRec::mainHand) out of the second blade's picks
    // (HAND_OFF) or takes only them (HAND_MAIN, stageMainHand).
    enum Hand { HAND_ANY, HAND_OFF, HAND_MAIN };

    CombatTechniqueData* drawOffHand(Pick& p, Want want, int tries, bool noTwin = false,
                                     CombatTechniqueData* exact = NULL, Hand hand = HAND_ANY)
    {
        float dp = stance::poolSkill(p.s);
        CombatTechniqueData* last = g_noRepeat && !moves::KINDS[want].special ? lastOff(p.self->me) : NULL;
        CombatTechniqueData* repeat = NULL;
        CombatTechniqueData* fallback = NULL;
        CombatTechniqueData* r = NULL;
        CombatTechniqueData* other = NULL;      // diagnostics: what came back instead
        int nulls = 0;
        // Even picks: every draw is looked at and the least played of the wanted kind wins (an exact
        // draw and a special keep the first that comes up).
        const bool even = exact == NULL && !moves::KINDS[want].special && cfg::b(cfg::EVEN_PICKS);
        const int MAX_CAND = 16;
        CombatTechniqueData* cand[MAX_CAND];
        int ncand = 0;
        {
            PickRedirect redirect(p.self, dp);  // category ATTACK_NULL and the DP, restored on any exit
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
                // Our move plays a vanilla base track; a rig without it (an animation pack that
                // replaced the human skeleton) would throw or T-pose.
                std::string rig;
                if (!arms::hasTrack(p.self->me, t->animation, &rig))
                {
                    diag::count("base_track_missing_skip");
                    noteMissingTrack(rig, t->animation);
                    continue;
                }
                // A record that exists for its own clip is not drawn while that clip's [Animations]
                // switch is off: the fighter keeps the moves it had before.
                TechRec tr;
                if (techRec(t, tr) && tr.def.needsClip && tr.def.clip >= 0 && !arms::clipOn((arms::Clip)tr.def.clip))
                {
                    diag::count("record_clip_off_skip");
                    continue;
                }
                bool mainHand = techRec(t, tr) && tr.mainHand;
                if ((hand == HAND_OFF && mainHand) || (hand == HAND_MAIN && !mainHand))
                {
                    diag::count(hand == HAND_OFF ? "pick_skip_main_hand" : "pick_skip_off_hand");
                    continue;
                }
                if (exact != NULL)
                {
                    if (t == exact)
                        r = t;
                    continue;
                }
                if (!moveCooldownOver(t, p.self->me, p.now))
                {
                    diag::count("pick_skip_cooldown");
                    continue;
                }
                Want k = kindOf(t);
                if (noTwin && moves::KINDS[k].twin)
                    continue;
                if (t == last && k == want)
                {
                    repeat = t;                 // the same move again: only if nothing else comes up
                    continue;
                }
                if (k == want)
                {
                    if (!even)
                        r = t;
                    else
                    {
                        bool have = false;
                        for (int c = 0; c < ncand; ++c)
                            have = have || cand[c] == t;
                        if (!have && ncand < MAX_CAND)
                            cand[ncand++] = t;
                    }
                }
                else if (fallback == NULL && !moves::KINDS[k].special && t != last)
                    fallback = t;
            }
        }
        if (even && ncand > 0)
        {
            r = leastPlayed(p.s.tier, cand, ncand);
            diag::count(ncand > 1 ? "pick_even_choice" : "pick_even_single");
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
            // Why nothing of ours fitted: the game returned nothing (no technique of category 21
            // passed its filter) or another technique.
            diag::count(other != NULL ? "draw_miss_got_other" : "draw_miss_got_null");
            if (diag::takeDetail())
                kit::Diag("draw_miss").kv("who", nameOf(p.self->me)).kv("want", wantName(want)).kv("range", p.range)
                    .kv("reach", p.reach).kv("gap", p.range - p.reach).kv("stationary", p.stationary).kv("dp", dp)
                    .kv("nulls", nulls).kv("other", other != NULL ? other->animation : std::string("-")).emit();
        }
        return r;
    }

    // Every pick of a fighter who has the Dash slash ends here:
    // - a long dash from the game's pool (another mod's) is replaced by ours when ours fits, so
    //   ours plays;
    // - the engage distance (CharStats::longestReachingAttack, what getMeiMax keeps the fighter at)
    //   is raised to our Dash's reach, never lowered (another mod's longer reach stays), so the
    //   fighter engages from where our Dash lands (the pick hook runs before the AI moves). The
    //   game's own value comes back when the fighter stops being dual.
    CRITICAL_SECTION g_reachLock;
    struct SavedReach { float game; float ours; };
    std::map<CharStats*, SavedReach> g_reachSaved;      // under g_reachLock

    // `target` is the distance of the special that is up (the Dash's, or the spin's window while the spin
    // goes first); our own earlier write may be lowered to it, the game's value never.
    void raiseReach(CharStats* st, float target)
    {
        float cur = st->longestReachingAttack;
        EnterCriticalSection(&g_reachLock);
        if (g_reachSaved.size() > 4096)
            g_reachSaved.clear();
        std::map<CharStats*, SavedReach>::iterator it = g_reachSaved.find(st);
        bool ours = it != g_reachSaved.end() && it->second.ours == cur;
        float game = ours ? it->second.game : cur;      // not our own write: the game's value
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

    // The fighter is not dual (or has no specials) any more: give back the game's value, unless
    // the game rewrote it since (then its own value is already there).
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

    // Another mod's long move that needs both hands on one hilt. By the record's `1 handed` flag; a
    // technique the registry does not know counts as two-handed.
    bool longTwoHanded(CombatTechniqueData* t)
    {
        if (t == NULL || isNitoryuTechnique(t) || t->initialDistance <= cfg::f(cfg::FOREIGN_REPICK_OVER))
            return false;
        TechRec rec;
        if (!techRec(t, rec) || rec.data == NULL)
            return true;
        return !kit::fcsBool(rec.data, "1 handed", false);
    }

    // Where a fighter with the special moves engages from. The opener that is up and has the highest
    // `chance` sets it (the others wait for it), unless another available opener ranks as high or
    // higher: then the widest window of the available ones (equal `chance`: both are tried, so the
    // wider one). -1: no opener is available.
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

    // Every long dash of a dual fighter, our Dash or another mod's (attack distance LONG_DASH_MIN+),
    // shares the Dash's cooldown: it is kept under our Dash's own key, so after another mod's dash
    // ours waits too, and after either the game's long-dash picks are drawn again (a closer move),
    // or, when none fits, the pick is NULL and the game walks the fighter in (as it does on its
    // own). The spin is not part of it (it has its own cooldown).
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

    // ---- only my strikes ----
    // The setting ONLY_MY_STRIKES ([Main] only_my_strikes, FCS `nitoryu only my strikes`, default on): every
    // attack of a dual fighter is a record that plays a clip of ours (its [Animations] switch on). A pick that
    // is anything else (a vanilla swing mirrored onto the left arm, another mod's move, a record of ours
    // without a clip) is drawn again from our pool. Specials come up only as the normal pick stages them.
    // Nothing of ours fits in reach (a rig without the tracks): the game's pick stays, counted as
    // test_only_ours_leak, so the fighter never stands still; out of reach (another mod's long dash, a move of
    // ours on its cooldown) the fighter walks in instead (only_ours_walk_in_far, only_ours_walk_in).

    bool oursWithClip(CombatTechniqueData* t)
    {
        TechRec tr;
        return t != NULL && techRec(t, tr) && tr.def.kind != moves::OTHER && tr.def.clip >= 0
            && arms::clipOn((arms::Clip)tr.def.clip);
    }

    CombatTechniqueData* testOnlyOurs(Pick& p, CombatTechniqueData* r)
    {
        if (!cfg::b(cfg::ONLY_MY_STRIKES) || (oursWithClip(r) && moveCooldownOver(r, p.self->me, p.now)))
            return r;
        CombatTechniqueData* alt = NULL;
        CombatTechniqueData* last = lastOff(p.self->me);
        CombatTechniqueData* repeat = NULL;
        bool cooling = false;
        // every draw is looked at and the least played (by weight, per rank) wins, as in the normal picks
        const int MAX_CAND = 16;
        CombatTechniqueData* cand[MAX_CAND];
        int ncand = 0;
        {
            PickRedirect redirect(p.self, stance::poolSkill(p.s));
            for (int i = 0; i < 16; ++i)
            {
                CombatTechniqueData* t = p.game();
                std::string rig;
                if (!oursWithClip(t) || moves::KINDS[kindOf(t)].special || !arms::hasTrack(p.self->me, t->animation, &rig))
                    continue;
                if (!moveCooldownOver(t, p.self->me, p.now))
                {
                    cooling = true;
                    continue;
                }
                if (t == last)
                {
                    repeat = t;
                    continue;
                }
                bool have = false;
                for (int c = 0; c < ncand; ++c)
                    have = have || cand[c] == t;
                if (!have && ncand < MAX_CAND)
                    cand[ncand++] = t;
            }
        }
        if (ncand > 0)
            alt = leastPlayed(p.s.tier, cand, ncand);
        if (alt == NULL)
            alt = repeat;
        if (alt != NULL)
        {
            noteOff(p.self->me, alt);
            notePlayed(p.s.tier, alt);
        }
        else if (r != NULL && (cooling || p.range > p.reach))
        {
            // out of reach nothing but ours is played (not another mod's long dash from 20+ beyond reach):
            // the fighter walks in and the next pick is from closer
            diag::count(cooling ? "only_ours_walk_in" : "only_ours_walk_in_far");
            return NULL;
        }
        diag::count(alt != NULL ? "test_only_ours_swapped" : "test_only_ours_leak");
        if (diag::takeDetail())
            kit::Diag("test_only_ours").kv("who", nameOf(p.self->me)).kv("was", r != NULL ? r->animation : std::string("none"))
                .kv("now", alt != NULL ? alt->animation : std::string("kept")).kv("range", p.range).kv("reach", p.reach).emit();
        return alt != NULL ? alt : r;
    }

    // The last stage of every dual pick except the Cross: another mod's long moves, and the
    // engage distance.
    CombatTechniqueData* finishPick(Pick& p, CombatTechniqueData* r)
    {
        r = testOnlyOurs(p, r);
        r = sharedDashCooldown(p, r);
        noteMoveUse(r, p.self->me, p.now);
        // what each pick ends as, by move (`pick_take_<clip>`, the game's own swings as pick_take_game), and
        // the per-rank tallies every 2 min (`pick_balance`)
        count(isNitoryuTechnique(r) ? "pick_take_" + moveName(r) : std::string("pick_take_game"));
        emitPickBalance(p.now);
        // Another mod's long move is not swapped for our Dash; our Dash keeps its own roll
        // (stageDash) and cooldown. A fighter with the specials keeps another mod's long dash: the
        // pose engine mirrors it onto the left arm and the off blade hits too (twinsForeignDash,
        // echoHit).
        bool mirrored = p.special && g_mirrorDash && r != NULL && combat::isForeignLongDash(r)
                        && doubleReady(p.self->me, p.now, "double_hit_wait_foreign");
        if (mirrored)
        {
            noteDouble(p.self->me, p.now);      // its second blade is a double hit too
            diag::count("long_dash_mirrored");
            if (diag::takeDetail())
                kit::Diag("special_pick").kv("who", nameOf(p.self->me)).kv("move", "foreign_dash_mirrored")
                    .kv("tech", r->animation).kv("distance", r->initialDistance).kv("dp", p.s.dp)
                    .kv("range", p.range).kv("reach", p.reach).emit();
        }
        // A dual fighter without the specials never plays another mod's long two-handed move (the
        // left hand cannot join the grip). Pick again, avoiding them. Two-handed by the record's
        // `1 handed` flag, so another mod's long ONE-handed move plays; vanilla's short two-handed
        // swings stay as ever.
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
        // a mirrorable long dash that stays after the cooldown said no plays one-bladed
        bool quiet = !mirrored && p.special && g_mirrorDash && r != NULL && combat::isForeignLongDash(r);
        setForeignQuiet(p.self->me, quiet ? r : NULL);
        // the fighter engages from the distance of the opener that is up
        float engage = p.special ? engageDistance(p) : -1.0f;
        if (engage > 0.0f)
            raiseReach(p.self, engage);
        else
            restoreReach(p.self);
        return r;
    }

    // Stage 1a: a finisher (the Cross), on a staggered or fully pressured target. Returned as drawn
    // (no finish: it is ours and the engage distance stays as the last pick left it).
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
        // The game's pool keeps the Cross at that range (range <= reach + initialDistance) but weighs it by
        // distance fit, so up close the short moves win every draw. The trigger has already decided; when
        // the game's own keep rule passes, the Cross is returned as is.
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

    // Stage 1b: an opener (the Dash slash, the spin, another mod's), from its distance window (range - reach
    // between the record's min and max gap), once the left blade is attached, off its own cooldown.
    CombatTechniqueData* stageOpener(Pick& p, float gap, const Special& o)
    {
        Character* me = p.self->me;
        const moves::Def& d = o.rec.def;
        std::string tag = tagOf(o);
        if (!available(o, p.s))
            return NULL;
        // Not before the left blade is attached (a pick can come right at the draw; the pose engine
        // only takes fighters whose blade is on, so the base track's own start would show first).
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
        // A move whose every impact is doubled (the spin: four hits) waits for the double-hit cooldown; the
        // Dash keeps only its own.
        if (d.echo && !doubleReady(me, p.now, ("double_hit_wait_" + tag).c_str()))
            return NULL;
        if (roll01() >= d.roll)
            return NULL;
        CombatTechniqueData* r = drawOffHand(p, moves::OPENER, 10, false, o.tech);
        count(r != NULL ? "special_" + tag : "special_" + tag + "_no_fit");
        if (r == NULL)
            return NULL;
        noteUse(o, me, p.now);
        noteDouble(me, p.now);          // no twin right after it (its own gate stays)
        if (diag::takeDetail())
            kit::Diag("special_pick").kv("who", nameOf(me)).kv("move", tag).kv("dp", p.s.dp)
                .kv("ambi_eff", p.s.ambiEff).kv("range", p.range).kv("reach", p.reach).emit();
        return r;
    }

    // Two equal blades twin more.
    float twinShare(const stance::Info& s)
    {
        float m = blades::twinMult(s.main, s.off);
        if (m != 1.0f)
            diag::count("pair_equal_twin_roll");
        float v = balance::at(balance::TWIN_SHARE, s.dp) * m;
        return v > 1.0f ? 1.0f : v;
    }

    // Stage 2: a counter (armed by a successful block) is a riposte, preferably a twin; otherwise an
    // earlier roll that found nothing in reach (the carry-over), otherwise the off hand strikes with
    // the DP share. NULL: not rolled, or nothing of ours fitted.
    CombatTechniqueData* stageOffHand(Pick& p)
    {
        Character* me = p.self->me;
        bool counter = defense::takeCounter(me);
        if (counter && roll01() >= defense::counterChance(p.s.dp))
        {
            diag::count("counter_not_rolled");
            counter = false;
        }
        // Mugen: a twin that landed armed a chain: this pick is a twin again, whatever the double-hit
        // cooldown (a counter keeps its own roll below).
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
        // A riposte rolls the twin share like any off-hand strike (always a twin, it would give
        // twin, block, twin in a row).
        if (counter)
            want = roll01() < twinShare(p.s) ? moves::TWIN : moves::SINGLE;
        bool noTwin = !chain && !doubleReady(me, p.now, want == moves::TWIN ? "double_hit_wait_twin" : "double_hit_wait_quiet");
        if (noTwin)
            want = moves::SINGLE;
        // 12 draws, so a rank's whole pool comes up for the even pick; the right blade's strikes are
        // stageMainHand's
        CombatTechniqueData* r = drawOffHand(p, want, 12, noTwin, NULL, HAND_OFF);
        if (chain)
            diag::count(!isNitoryuTechnique(r) ? "mugen_chain_no_fit"
                        : combat::isTwinTechnique(r) ? "mugen_chain_twin" : "mugen_chain_single");
        if (isNitoryuTechnique(r))
        {
            noteOff(me, r);
            notePlayed(p.s.tier, r);    // even picks
            noteRiposte(me, counter ? r : NULL);
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
        // Nothing in category 21 fitted: chooseAttack fell back to unarmed. The caller redoes it
        // normally; the roll is kept for the next pick (a first failure only: a carried roll that
        // fails again is dropped, so the share never grows past the design value).
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

    // A pick the second blade did not take may still be a strike of ours with the right blade: at [Moves]
    // main_move_share (FCS nitoryu main move share) it is drawn from those, the least played first; otherwise,
    // or when none is in reach, the game's own swing.
    CombatTechniqueData* stageMainHand(Pick& p)
    {
        if (roll01() >= cfg::f(cfg::MAIN_MOVE_SHARE))
        {
            diag::count("main_move_not_rolled");
            return NULL;
        }
        CombatTechniqueData* r = drawOffHand(p, moves::SINGLE, 12, true, NULL, HAND_MAIN);
        if (r == NULL || !isNitoryuTechnique(r))
        {
            diag::count("main_move_none");
            return NULL;
        }
        noteOff(p.self->me, r);
        notePlayed(p.s.tier, r);
        diag::count("main_move_chosen");
        if (diag::takeDetail())
            kit::Diag("main_move_pick").kv("who", nameOf(p.self->me)).kv("tech", moveName(r)).kv("dp", p.s.dp)
                .kv("tier", p.s.tier).kv("range", p.range).kv("reach", p.reach).emit();
        return r;
    }

    // Every pick may use the off hand, with a share that grows with DP (docs/BALANCE.md). The stages
    // in order, each drawing from the pick's budget: the specials (Cross, then the openers), the off
    // hand (counter, carry-over, roll), the game's own pick; every result but the Cross goes through
    // finishPick.
    CombatTechniqueData* choose_hook(CharStats* self, float range, float reach, CombatTechniqueData* last,
                                     bool stationary)
    {
        diag::HookCall hc(diag::HK_CHOOSE_ATTACK);
        // setWeapon calls chooseAttack on every draw (CharacterHuman::drawWeapon -> setWeapon) with
        // leftover arguments. Not an attack: the game's pick passes through, nothing is rolled or
        // remembered.
        if (t_inSetWeapon)
        {
            diag::count("pick_in_setweapon");
            return choose_orig(self, range, reach, last, stationary);
        }
        stance::Info s = stanceOf(self);
        if (!s.dual)
        {
            restoreReach(self);         // the game's engage distance back
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
        hc.ours();
        diag::count("pick_dual");
        if (s.tier < 1)
        {
            // A tier-0 dual fighter gets the same finish (another mod's two-handed long move is
            // picked again) as every other dual pick.
            diag::count("offhand_tier0");
            return finishPick(p, p.game());
        }
        p.special = s.tier >= 3 && s.ambiEff >= cfg::f(cfg::SPECIAL_MIN_AMBI);
        // why the Cross came up or not
        bool staggered = p.special && targetStaggered(self->me);
        if (p.special)
            diag::count(staggered ? "pick_special_target_staggered" : "pick_special_target_steady");
        bool pressed = p.special && !staggered && targetFullyPressured(self->me);
        if (pressed)
            diag::count("pick_special_target_full_pressure");
        bool riposte = p.special && !staggered && !pressed && riposteWindow(self->me, p.now);
        // The answer to a block is the X cut only at this chance, otherwise an ordinary riposte (or it
        // would be most of what a fighter plays).
        if (riposte && roll01() >= cfg::f(cfg::CROSS_RIPOSTE_CHANCE))
        {
            riposte = false;
            diag::count("pick_special_riposte_not_rolled");
        }
        if (riposte)
            diag::count("pick_special_riposte");
        if (staggered || pressed || riposte)
            armCross(self->me, p.now);
        bool armed = p.special && !staggered && !pressed && !riposte && crossArmed(self->me, p.now);
        // A pick from farther than any of our moves reaches is the game's alone. Nothing is rolled, so
        // the off-hand share, the carried roll and the Cross stay for the picks made in reach. Its
        // caller is logged (from here: noteCaller reads the stack above choose_hook).
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
            // The openers in the order of their records' `chance`; while one with a strictly higher
            // `chance` is up (it only missed its distance or its roll), the lower ones wait for it. Equal
            // `chance`: each is tried in turn. Any number of openers, ours or another mod's.
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
        if (r == NULL)
            r = stageMainHand(p);
        return finishPick(p, r != NULL ? r : p.game());
    }

    // ---- off-hand damage -----------------------------------------------------

    // the cut/blunt term of the damage formula: min + (max99 - min) * x / 200
    float term(float lo, float hi, float x) { return lo + (hi - lo) * x / 200.0f; }
    float ratio(float a, float b) { return b > 0.001f ? a / b : 1.0f; }

    // A dual fighter fights with its effective Dexterity x (1 + dex_bonus) in the game's two
    // Dexterity formulas: the cut damage (here) and the attack and block speed (speed_hook). Not in
    // DP, the stats window or anything outside combat.
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
        // In flow Dexterity counts FLOW_DEX_BONUS more, on top.
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

    // The game multiplies every melee hit by the MAIN weapon's bonus against the target
    // (CharStats::getTotalAttackDamageFor: bonusRobots if the target's race is robotic, else
    // bonusAnimals for an animal, else bonusHumans for a human, else 1; then x its `race damage` entry
    // for the target's race, val0 %). An off-hand hit takes the OFF weapon's own instead: the ratio
    // of the off weapon's bonus to the main's.
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
            // The load order's CONSTANTS on every build. The level-1 values are the game's own fields.
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
        // Near the off weapon's full damage (the OFF_HAND_DAMAGE curve by DP), since an off-hand
        // strike takes the place of a main one.
        float k = balance::at(balance::OFF_HAND_DAMAGE, s.dp);
        // the second weapon's own factor (its WEAPON record's `nitoryu off hand damage`, 1 = none)
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
        // The off weapon's own armour penetration, not the main one's. setWeapon loads the main's
        // into CharStats::bonusArmourPenetration from its FCS "armour penetration".
        d.armourPenetration = kit::fcsFloat(s.off->data, "armour penetration", 0.0f);
    }

    HitMaterialType (*hitBy_orig)(Character*, CutDirection, Damages&, Character*, CombatTechniqueData*, int) = NULL;
    HitMaterialType hitBy_inner(Character* self, CutDirection dir, Damages& damage, Character* who,
                                CombatTechniqueData* attack, int comboID);

    // ---- the fight ledger ------------------------------------------------------------
    // Every melee impact where the attacker or the victim is a player character or a dual fighter
    // is tallied per character: impacts landed, blocked, missed, damage after armour dealt and
    // taken. Stage only; one `fight` line per character with the counts line, then the tally
    // restarts.
    struct Tally
    {
        std::string name;
        bool dual;
        int landed, blocked, missed, ours;
        float dealt, taken;
        // the fighter's stats at its first impact of the window, and whom it traded impacts with
        std::string stats;
        std::map<std::string, int> vs;
    };

    // Race, weapon and the combat stats that decide a melee 1v1 (raw levels x health, as the game's
    // own formulas read them; attack/defence with the weapon's bonus), plus DP/tier/A when dual.
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

    // hitByMeleeAttack returns HIT_MISSED for a dodge, HIT_SWORD for a block, and for a hit that lands
    // the material it met: HIT_FLESH, or the armour's (HIT_METAL, HIT_CHAIN) when a worn piece covered the part.
    bool hitLanded(HitMaterialType r) { return r != HIT_MISSED && r != HIT_SWORD; }

    // The original hit, recorded. `damage` holds what is left after armour when it returns.
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

    // What a dual fighter's landed hits do to the target: its combat state right after the hit and
    // the damage left after armour (the stumble is decided inside the hit).
    void noteLandedHit(Character* victim, Character* who, const Damages& d)
    {
        if (!g_enabled || who == NULL || victim == NULL || !stance::evaluate(who, NULL).dual)
            return;
        CombatClass* vc = victim->getCombatClass();
        char key[48];
        sprintf_s(key, "flesh_victim_state_%d", vc != NULL ? (int)vc->combatState : -1);
        diag::count(key);       // the counter names keep "flesh"; armour hits count too
        float dmg = d.cut + d.blunt + d.pierce;
        diag::count(dmg < 5.0f ? "flesh_damage_0_5" : dmg < 10.0f ? "flesh_damage_5_10" : dmg < 20.0f ? "flesh_damage_10_20"
                    : dmg < 40.0f ? "flesh_damage_20_40" : "flesh_damage_40_up");
    }

    // A hit that lands (on flesh or on armour) makes a playing clip give way to the hit reaction.
    HitMaterialType hitBy_hook(Character* self, CutDirection dir, Damages& damage, Character* who,
                               CombatTechniqueData* attack, int comboID)
    {
        diag::HookCall hc(diag::HK_HIT_BY);
        // Every impact on a player character while another plugin's control is active, with whether its
        // block was up. We are the outermost hook here (that plugin's block hook sits inside), so r is
        // what the attacker gets.
        bool watch = self != NULL && foreign::active() && self->isPlayerCharacter();
        float cutIn = damage.cut, bluntIn = damage.blunt;
        HitMaterialType r = hitBy_inner(self, dir, damage, who, attack, comboID);
        // Ours when Nitoryu's side of the hit was in play: our move, or a dual or left-handed attacker.
        bool ours = g_enabled && who != NULL && (isNitoryuTechnique(attack) || stance::evaluate(who, NULL).dual
                                                 || lefty::active(who));
        if (ours)
            hc.ours();
        if (soundtrace::on())       // [Test] sound_trace
            soundtrace::impact(self, who, (int)r, comboID, attack != NULL ? attack->animation : std::string("?"), ours);
        if (watch)
            foreign::noteIncoming(self, nameOf(who), attack != NULL ? attack->animation : std::string("?"), comboID,
                                  (int)r, hitLanded(r), cutIn, bluntIn, damage.cut, damage.blunt);
        if (hitLanded(r))
        {
            arms::clipHit(self);
            noteLandedHit(self, who, damage);
        }
        return r;
    }

    // Another mod's long dash, mirrored onto the left arm by the pose engine, hits with both blades:
    // the main impact as the game made it, then the off blade's with the off-hand scaling of our
    // twin strikes. Like a twin's two impacts in one frame, the victim can block, take or dodge each
    // on its own. The counters of a hit followed by the off blade's (shared with the spin).
    struct EchoNames { const char* notDual; const char* blocked; const char* missed; const char* landed; const char* line; };
    const EchoNames FOREIGN_ECHO = { "foreign_dash_hit_not_dual", "foreign_dash_off_blocked", "foreign_dash_off_missed",
                                     "foreign_dash_off_landed", "foreign_dash_hit" };

    HitMaterialType echoHit(Character* self, CutDirection dir, Damages& damage, Character* who,
                            CombatTechniqueData* attack, int comboID, const EchoNames& n)
    {
        // The off blade's copy before the game applies armour to `damage` in place. A raw
        // copy: Damages is plain floats, and its copy constructor is a KenshiLib import.
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
        // The off-hand guard (Defense.cpp) runs inside the original, in
        // _iHitYouAreYouHit, which has no technique or comboID of its own.
        defense::HitScope scope(self, who, attack, comboID);
        // The attacker's xpMelee follows on this thread: tell it whether the off hand struck.
        {
            CombatTechniqueData::ImpactPoint* ip = isNitoryuTechnique(attack) && comboID >= 0
                && comboID < attack->numImpactPoints() ? attack->impactPoint(comboID) : NULL;
            ambidex::noteImpact(who, attack, ip != NULL && (int)ip->limb == 0);
        }
        // A left-hander's hit (a crippled right arm, the weapon in the left hand).
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
        // A dual fighter's Dexterity counts +dex_bonus in the cut damage, on every impact (main,
        // off-hand, twin, another mod's dash), before the off-hand scaling.
        if (g_enabled && who != NULL)
            applyDexDamage(who, damage);
        if (g_enabled && who != NULL && combat::twinsForeignDash(who, attack))
            return echoHit(self, dir, damage, who, attack, comboID, FOREIGN_ECHO);
        // A move whose record says `nitoryu echo`: each of its impacts (the right blade) is followed by the
        // left's (the spin: two impacts, four hits). The counters carry the move's tag: spin_off_landed ...
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
        if (p == NULL || (int)p->limb != 0)        // 0 = LEFT_ARM: the off hand struck
        {
            diag::count("offhand_tech_main_impact");
            // the right blade of a twin: where it lands is where the left one may follow
            blades::Scissor scissor(who, self, twin ? stance::evaluate(who, NULL) : notDual("not_twin"), false, twin);
            HitMaterialType r = origHit(self, dir, damage, who, attack, comboID);
            scissor.landed(r);
            return r;
        }
        // Fresh evaluation: the damage reads the weapons themselves, so never use
        // weapon pointers from an older cached evaluation.
        stance::invalidate(who);
        stance::Info s = stance::evaluate(who, NULL);
        if (!s.dual)
        {
            diag::count("offhand_hit_not_dual");
            return origHit(self, dir, damage, who, attack, comboID);
        }
        // The left blade of a twin goes to a second enemy at the flank instead of this one, who then takes
        // the right blade only. The Cross's left blade too.
        bool splits = twin || combat::moveKind(attack) == moves::FINISHER;
        Character* flanker = splits ? blades::flanker(who, self, s, attack, comboID) : NULL;
        if (flanker != NULL)
        {
            float fcr = 1.0f, fbr = 1.0f;
            float cutIn = damage.cut;
            // The damage was made for `self` (attackImpactCheck); the second weapon's own bonuses are read
            // against the flanker.
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
            // nothing reached `self`: the game's impact loop sees a miss for it
            return HIT_MISSED;
        }
        float cutBefore = damage.cut, bluntBefore = damage.blunt;
        float cr = 1.0f, br = 1.0f;
        scaleOffHand(who, self, s, damage, cr, br);
        // What Nitoryu passes on; the game then applies armour to `damage` in place.
        float cutScaled = damage.cut, bluntScaled = damage.blunt;
        // the left blade of a twin may take the body part the right one just hit
        blades::Scissor scissor(who, self, s, true, twin);
        HitMaterialType r = origHit(self, dir, damage, who, attack, comboID);
        scissor.landed(r);
        diag::count(r == HIT_SWORD ? "offhand_hit_blocked" : (r == HIT_MISSED ? "offhand_hit_missed" : "offhand_hit_landed"));
        // Mugen: the left blade of a twin landed: at rank 5 the next pick may be a twin again.
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

    // A number from the ini, or -1 when the key is absent or empty.
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
        kit::log::error(std::string("Nitoryu: could not hook ") + what);
        return false;
    }
}

namespace combat
{
    bool isOffHandTechnique(CombatTechniqueData* t) { return isNitoryuTechnique(t); }

    // The kind comes from the record's move (moves::Def). Without a registry entry: a twin is a Nitoryu
    // technique with a RIGHT_ARM impact (the main blade) and a LEFT_ARM impact (the off blade) in one
    // swing; anything else is a single. (No finisher or opener without its record: they need its
    // cooldown and gaps.)
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

    bool isRiposte(Character* c, CombatTechniqueData* t)
    {
        if (c == NULL || t == NULL)
            return false;
        EnterCriticalSection(&g_pickLock);
        std::map<Character*, CombatTechniqueData*>::iterator it = g_riposte.find(c);
        bool r = it != g_riposte.end() && it->second == t;
        LeaveCriticalSection(&g_pickLock);
        return r;
    }

    // The pose mode a Nitoryu move plays in (its record's `nitoryu arms`, else by its kind); -1: not ours.
    int armsModeOf(CombatTechniqueData* t)
    {
        TechRec rec;
        if (techRec(t, rec))
            return rec.def.kind != moves::OTHER ? (int)moves::armsMode(rec.def) : -1;
        int k = moveKind(t);
        return k == moves::OTHER ? -1 : (int)(moves::KINDS[k].twin ? arms::TWIN : arms::SWAP);
    }

    // The clip laid over the technique while it swings (the record's `nitoryu clip`), -1: none.
    int attackClip(CombatTechniqueData* t)
    {
        TechRec rec;
        return techRec(t, rec) && rec.def.kind != moves::OTHER ? rec.def.clip : -1;
    }

    // The clip that stays on until the technique's track ends, -1: none. An opener that plays with or
    // without its clip (the Dash) always has its tail; a move that needs its clip has it while the clip is a
    // full-body one and switched on; the Cross has none.
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

    bool g_dashOn = true;       // [Animations]-independent: [Moves] dash_slash
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

    // Main loop, for each drawn dual fighter (Visual.cpp tick): the stored attack and block
    // speed must carry our factor. A field that holds exactly the game's own value we computed
    // under it was rewritten by someone else: put ours back. A field that holds anything else
    // (new stats, a mod's own formula) is left alone and counted.
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
        // A field that is not our last value: either the game's plain value came back, or another
        // writer (a mod's own formula) put its number there; either way it is the base now, and our
        // factor goes on top of it once (the next frame finds our value and does nothing).
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
            // flow began or ended: the field holds our value, so the same base gets the new factor
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
        g_riposte.erase(c);
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

    // Main loop: once the game has built its techniques, one line with the registry's size.
    // Each single off-hand move takes its timing from the live source technique it was cloned from
    // (Moves.h RECORDS `source`): speed, hesitation, acceptable end, and each impact's hit and stop
    // frames when the impact counts match. The .mod froze vanilla's values; an animation pack that
    // retimes the source now carries over. Where the values are equal nothing moves.
    void syncTimings()
    {
        std::map<std::string, std::vector<CombatTechniqueData*> > byId;
        std::map<std::string, std::string> sourceOf;        // a move's string id -> its `nitoryu timing from`
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
        std::vector<TechRec> added;         // other mods' moves: one line each
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
        // from_fields = moves described by their records' `nitoryu ...` fields (fewer than ours = an
        // older .mod, the Moves.h table stands in). third_party = moves another mod added. ignored =
        // records that set `nitoryu move` outside the private pool.
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

    // From cfg (the table < FCS < the ini); at start and after a change.
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
        // The Dash's and the spin's numbers are on their records (FCS). A key that is present in the ini
        // is the player's own override of our two records; absent (the shipped ini): the record's value.
        std::string ini = stance::iniFile();
        g_spinOn = GetPrivateProfileIntA("Moves", "spin", 1, ini.c_str()) != 0;
        g_iniBigChopCooldownMs = iniNumber(ini, "Moves", "big_chop_cooldown_ms");
        g_iniDashCooldownMs = iniNumber(ini, "Moves", "dash_cooldown_ms");
        g_iniSpinCooldownMs = iniNumber(ini, "Moves", "spin_cooldown_ms");
        g_iniSpinMinSkill = (float)iniNumber(ini, "Moves", "spin_min_skill");
        bool ok = true;
        ok &= hook(hooks::add("CharStats::setWeapon", "core", KenshiLib::GetRealAddress(&CharStats::setWeapon), setWeapon_hook, &setWeapon_orig),
                   "CharStats::setWeapon");
        ok &= hook(hooks::add("CharStats::_calculateWeaponWeightSpeedMultiplier", "core", KenshiLib::GetRealAddress(&CharStats::_calculateWeaponWeightSpeedMultiplier),
                                      weightSpeed_hook, &weightSpeed_orig),
                   "CharStats::_calculateWeaponWeightSpeedMultiplier");
        ok &= hook(hooks::add("CharStats::calculateAttackOrBlockSpeed", "core", KenshiLib::GetRealAddress(&CharStats::calculateAttackOrBlockSpeed), speed_hook, &speed_orig),
                   "CharStats::calculateAttackOrBlockSpeed");
        ok &= hook(hooks::add("CharStats::getMeleeAttack", "core", KenshiLib::GetRealAddress(&CharStats::getMeleeAttack), meleeAttack_hook, &meleeAttack_orig),
                   "CharStats::getMeleeAttack");
        ok &= hook(hooks::add("CharStats::_calculateBlockChance", "core", KenshiLib::GetRealAddress(&CharStats::_calculateBlockChance), blockChance_hook, &blockChance_orig),
                   "CharStats::_calculateBlockChance");
        // The panel's numbers; not core. All five or none: a part of them would show half a sum.
        g_panelPair = GetPrivateProfileIntA("Stats", "panel_pair_bonus", 1, ini.c_str()) != 0;
        if (g_panelPair)
        {
            bool panel = hook(hooks::add("CharStats::skillBonusAttack_melee", "panel", KenshiLib::GetRealAddress(&CharStats::skillBonusAttack_melee),
                                         bonusAttack_hook, &bonusAttack_orig), "CharStats::skillBonusAttack_melee");
            panel = panel && hook(hooks::add("CharStats::getMeleeAttack_melee", "panel", KenshiLib::GetRealAddress(&CharStats::getMeleeAttack_melee),
                                             attackMelee_hook, &attackMelee_orig), "CharStats::getMeleeAttack_melee");
            panel = panel && hook(hooks::add("CharStats::skillBonusDefence", "panel", KenshiLib::GetRealAddress(&CharStats::skillBonusDefence),
                                             bonusDefence_hook, &bonusDefence_orig), "CharStats::skillBonusDefence");
            panel = panel && hook(hooks::add("CharStats::getMeleeDefence_melee", "panel", KenshiLib::GetRealAddress(&CharStats::getMeleeDefence_melee),
                                             defenceMelee_hook, &defenceMelee_orig), "CharStats::getMeleeDefence_melee");
            // last: without it the four above never act (t_inPanel stays 0)
            panel = panel && hook(hooks::add("CharStats::getGUIDataForMainInfo", "panel", KenshiLib::GetRealAddress(&CharStats::getGUIDataForMainInfo),
                                             mainInfo_hook, &mainInfo_orig), "CharStats::getGUIDataForMainInfo");
            if (!panel)
                g_panelPair = false;
        }
        ok &= hook(hooks::add("CharStats::getEquippedWeaponSkill", "core", KenshiLib::GetRealAddress(&CharStats::_NV_getEquippedWeaponSkill),
                                      equippedSkill_hook, &equippedSkill_orig),
                   "CharStats::getEquippedWeaponSkill");
        ok &= hook(hooks::add("CharStats::chooseAttack", "core", KenshiLib::GetRealAddress(&CharStats::chooseAttack), choose_hook, &choose_orig),
                   "CharStats::chooseAttack");
        ok &= hook(hooks::add("Character::hitByMeleeAttack", "core", KenshiLib::GetRealAddress(&Character::_NV_hitByMeleeAttack), hitBy_hook, &hitBy_orig),
                   "Character::hitByMeleeAttack");
        // Not core. Without it ours are known by category 21.
        if (!hook(hooks::add("CombatTechniqueData::CombatTechniqueData", "core", KenshiLib::GetRealAddress(&CombatTechniqueData::_CONSTRUCTOR), techCtor_hook, &techCtor_orig),
                  "CombatTechniqueData::CombatTechniqueData"))
            kit::Diag("techniques").kv("registry", "off").kv("fallback", "category 21").emit();
        return ok;
    }
}
