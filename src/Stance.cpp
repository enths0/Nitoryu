#include "kit/input.h"

#include <core/Functions.h>
#include <kenshi/Kenshi.h>

#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/Inventory.h>
#include <kenshi/Gear.h>
#include <kenshi/GameData.h>
#include <kenshi/Faction.h>
#include <kenshi/RaceData.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/GameDataManager.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>

#include "Stance.h"
#include "Tuning.h"
#include "Settings.h"
#include "Balance.h"
#include "Diagnostics.h"
#include "Ambidex.h"
#include "kit/diag.h"
#include "kit/fcs.h"

namespace
{
    struct Cached
    {
        DWORD tick;
        stance::Info info;
    };

    CRITICAL_SECTION g_lock;
    std::map<Character*, Cached> g_cache;
    std::set<Character*> g_playerOff;
    bool g_allowed[NUM_SKILL_TYPES] = { false };
    int g_forceTier = 0;
    float g_followUpChance = -1.0f;
    float g_attackCap = 1.5f;
    float g_blockCap = 4.0f;
    float g_speedBonus = 0.3f;
    bool g_layout = true;
    std::string g_version;

    enum NpcMode { NPC_OFF, NPC_AUTHORED, NPC_ALL };
    NpcMode g_npcMode = NPC_ALL;
    int g_npcMaxTier = 3;
    const char* OPT_IN_RECORD = "11-Nitoryu.mod";
    std::set<std::string> g_iniOptIn;
    std::set<std::string> g_optIn;
    int g_npcRecordLeft = 12;

    struct Lock
    {
        Lock() { EnterCriticalSection(&g_lock); }
        ~Lock() { LeaveCriticalSection(&g_lock); }
    };

    float clamp01x(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    float cap100(float v) { return clamp01x(v, 0.0f, 100.0f); }

    float tierDp(int index)
    {
        return index >= 0 && index < cfg::items(cfg::TIER_DP) ? cfg::item(cfg::TIER_DP, index) : 1000.0f;
    }
    float tierMinAmbi(int index) { return cfg::item(cfg::TIER_MIN_AMBI, index); }

    stance::Info blank(const char* reason)
    {
        stance::Info r;
        r.dual = false;
        r.reason = reason;
        r.main = NULL;
        r.off = NULL;
        r.skillMain = r.skillOff = 0.0f;
        r.dp = 0.0f;
        r.tier = r.tierByDp = 0;
        r.tierCap = "";
        r.optedIn = false;
        r.clumsy = 0.0f;
        r.guardPenalty = 0.0f;
        r.speedFactor = 1.0f;
        r.weightDual = 0.0f;
        r.ambi = r.ambiEff = r.overload = r.dex = 0.0f;
        r.offFromHip = false;
        return r;
    }

    int weaponsIn(Inventory* inv, const char* name, Weapon** out, int max)
    {
        InventorySection* s = inv->getSection(std::string(name));
        if (s == NULL)
            return 0;
        const Ogre::vector<InventorySection::SectionItem>::type& items = s->getItems();
        int n = 0;
        for (size_t i = 0; i < items.size() && n < max; ++i)
        {
            Item* it = items[i].item;
            Weapon* w = it != NULL ? it->isWeapon() : NULL;
            if (w != NULL)
                out[n++] = w;
        }
        return n;
    }

    std::string iniPath()
    {
        HMODULE self = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&iniPath), &self);
        char path[MAX_PATH] = { 0 };
        GetModuleFileNameA(self, path, MAX_PATH);
        std::string p(path);
        size_t slash = p.find_last_of("\\/");
        return (slash == std::string::npos ? std::string(".") : p.substr(0, slash)) + "\\Nitoryu.ini";
    }

    float effDex(CharStats* st) { return st->dexterityActual(); }
    float effAttack(CharStats* st) { return st->getStat(STAT_MELEE_ATTACK, false) + (float)st->skillBonusAttack_melee(true); }
    float effDefence(CharStats* st) { return st->getStat(STAT_MELEE_DEFENCE, false) + (float)st->skillBonusDefence(true); }

    float dpOf(float skillLow, float dex, float atk, float def, float ambiEff)
    {
        return clamp01x(cfg::f(cfg::W_SKILL) * cap100(skillLow) + cfg::f(cfg::W_DEX) * cap100(dex) + cfg::f(cfg::W_ATTACK) * cap100(atk)
                        + cfg::f(cfg::W_DEFENCE) * cap100(def) + cfg::f(cfg::W_AMBI) * cap100(ambiEff), 0.0f, 100.0f);
    }

    float speedFactorOf(float dp, float dex)
    {
        float full = cfg::f(cfg::SPEED_FULL_DP);
        float speed0 = cfg::f(cfg::SPEED0);
        if (dp < full)
            return speed0 + (1.0f - speed0) * dp / full;
        float from = cfg::f(cfg::SPEED_FROM);
        float span = 100.0f - from;
        if (span <= 0.0f)
            return 1.0f + g_speedBonus;
        float dexN = clamp01x((dex - from) / span, 0.0f, 1.0f);
        float dpN = clamp01x((dp - from) / span, 0.0f, 1.0f);
        return 1.0f + g_speedBonus * dexN * dpN;
    }

    int capByAmbi(int t, float ambiEff)
    {
        while (t > 0 && ambiEff < tierMinAmbi(t - 1))
            --t;
        return t;
    }

    std::string recordIdOf(GameData* d) { return d != NULL ? d->stringID : std::string(); }

    bool optedIn(Character* c)
    {
        std::string own = recordIdOf(c->getGameData());
        Faction* f = c->getFaction();
        std::string fac = f != NULL ? recordIdOf(f->data) : std::string();

        bool byField = kit::fcsBool(c->getGameData(), "nitoryu dual wielder", false)
                       || (f != NULL && kit::fcsBool(f->data, "nitoryu dual wielder", false));
        if (byField)
            diag::count("npc_opt_in_by_field");
        bool r;
        bool log = false;
        {
            Lock l;
            r = byField || (!own.empty() && g_optIn.count(own) > 0) || (!fac.empty() && g_optIn.count(fac) > 0);
            if (g_npcRecordLeft > 0)
            {
                --g_npcRecordLeft;
                log = true;
            }
        }

        if (log)
            kit::Diag("npc_record").kv("who", c->displayName).kv("record", own)
                .kv("record_name", c->getGameData() != NULL ? c->getGameData()->name : std::string())
                .kv("faction", fac).kv("opted_in", r).emit();
        return r;
    }

    stance::Info compute(Character* c, Weapon* mainOverride)
    {
        if (!g_layout)
            return blank("game_version");
        if (stance::rigOf(c) == stance::RIG_NEVER)
            return blank("race_refused");
        CharStats* st = c->getStats();
        Inventory* inv = c->getInventory();
        if (st == NULL || inv == NULL)
            return blank("no_stats_or_inventory");

        Weapon* back[2] = { NULL, NULL };
        Weapon* hip[1] = { NULL };
        int nb = weaponsIn(inv, "back", back, 2);
        int nh = weaponsIn(inv, "hip", hip, 1);
        Weapon* a = NULL;
        Weapon* b = NULL;
        if (nb >= 2) { a = back[0]; b = back[1]; }
        else if (nb == 1 && nh == 1) { a = back[0]; b = hip[0]; }
        else return blank("layout");

        Weapon* hands = mainOverride != NULL ? mainOverride : c->getCurrentWeapon();
        if (hands == NULL)
            return blank("not_drawn");

        stance::Info r = blank("dual");
        if (hands == a) { r.main = a; r.off = b; }
        else if (hands == b) { r.main = b; r.off = a; }
        else if (hands->data != NULL && hands->data == a->data) { r.main = a; r.off = b; diag::count("pair_match_by_data"); }
        else if (hands->data != NULL && hands->data == b->data) { r.main = b; r.off = a; diag::count("pair_match_by_data"); }
        else return blank("hands_not_in_pair");

        r.offFromHip = nb < 2 && r.off == hip[0];

        if (stance::weaponRefused(r.main) || stance::weaponRefused(r.off))
            return blank("weapon_refused");
        if (!stance::weaponAllowed(r.main) || !stance::weaponAllowed(r.off))
            return blank("type_not_allowed");
        if (r.main->is2HandedOnly() || r.off->is2HandedOnly())
            return blank("two_handed_only");

        if (kit::fcsFile(r.off->data, "bare sword", "").empty())
            return blank("no_bare_mesh");
        MedicalSystem* med = c->getMedical();
        if (med != NULL && (!med->isLeftArmOk() || !med->isRightArmOk()))
            return blank("arm");

        bool player = c->isPlayerCharacter();
        if (player && !stance::playerAllowed(c))
            return blank("toggled_off");

        r.skillMain = st->getStat(stance::statFor(r.main->category), false);
        r.skillOff = st->getStat(stance::statFor(r.off->category), false);

        float dex = effDex(st);
        float atk = effAttack(st);
        float def = effDefence(st);
        float str = st->strengthActual();
        float s = r.skillMain < r.skillOff ? r.skillMain : r.skillOff;
        r.weightDual = cfg::f(cfg::WEIGHT_MULT) * (r.main->combatWeight + r.off->combatWeight);

        if (!player)
        {
            if (g_npcMode == NPC_OFF)
                return blank("npc_off");
            r.optedIn = optedIn(c);
            if (g_npcMode == NPC_AUTHORED && !r.optedIn)
                return blank("npc_not_opted_in");
            if (!r.optedIn)
            {
                if (dex < cfg::f(cfg::NPC_MIN_DEX)) return blank("npc_dex");
                if (s < cfg::f(cfg::NPC_MIN_SKILL)) return blank("npc_skill");
                if (str < cfg::f(cfg::NPC_STR_SHARE) * r.weightDual) return blank("npc_strength");
            }
        }

        ambidex::Effective amb = ambidex::effective(c);
        r.ambi = amb.a;
        r.ambiEff = amb.eff;
        r.overload = amb.overload;
        r.dex = cap100(dex);
        r.dp = dpOf(s, dex, atk, def, amb.eff);
        if (g_forceTier > 0)
        {

            float floor = tierDp(g_forceTier - 1);
            if (r.dp < floor)
                r.dp = floor;
        }
        r.tierByDp = stance::tierOf(r.dp);
        r.tier = capByAmbi(r.tierByDp, r.ambiEff);
        if (r.tier < r.tierByDp)
            r.tierCap = "ambi";
        if (!player && !r.optedIn && r.tier > g_npcMaxTier)
        {
            r.tier = g_npcMaxTier;
            r.tierCap = "npc";
        }
        if (g_forceTier > r.tier)
            r.tier = g_forceTier;
        if (r.tier < r.tierByDp)
            diag::count(r.tierCap[0] == 'n' ? "tier_capped_npc" : "tier_capped_ambi");
        float clumsy = cfg::f(cfg::CLUMSY0) - cfg::f(cfg::CLUMSY_SLOPE) * r.dp;
        r.clumsy = clumsy > 0.0f ? clumsy : 0.0f;
        r.guardPenalty = balance::dividedGuard(r.tier);
        r.speedFactor = speedFactorOf(r.dp, r.dex);
        r.dual = true;
        return r;
    }

    struct ComputeCtx
    {
        Character* c;
        Weapon* w;
        stance::Info out;
    };

    void computeInto(void* p)
    {
        ComputeCtx* x = static_cast<ComputeCtx*>(p);
        x->out = compute(x->c, x->w);
    }

    bool guarded(void (*fn)(void*), void* ctx)
    {
        __try { fn(ctx); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    stance::Info computeSafe(Character* c, Weapon* w)
    {
        ComputeCtx ctx;
        ctx.c = c;
        ctx.w = w;
        ctx.out = blank("fault");
        if (!guarded(computeInto, &ctx))
        {
            diag::count("stance_fault");
            return blank("fault");
        }
        return ctx.out;
    }
}

namespace stance
{
    std::string iniFile() { return iniPath(); }
    bool knownLayout() { return g_layout; }
    std::string gameVersion() { return g_version; }

    void applySettings()
    {

        bool allowed[NUM_SKILL_TYPES] = { false };
        for (int k = 0; k < cfg::items(cfg::WEAPON_CATEGORIES); ++k)
        {
            int v = (int)cfg::item(cfg::WEAPON_CATEGORIES, k);
            if (v >= 0 && v < NUM_SKILL_TYPES && v != SKILL_HEAVY && v != SKILL_BOW && v != ATTACK_POLEARMS)
                allowed[v] = true;
        }
        float attackCap = std::max(0.1f, cfg::f(cfg::DUAL_ATTACK_CAP));
        float blockCap = std::max(0.1f, cfg::f(cfg::DUAL_BLOCK_CAP));
        float speedBonus = std::max(0.0f, cfg::f(cfg::SPEED_BONUS));

        const std::string& m = cfg::text(cfg::NPC_MODE);
        NpcMode mode = m == "off" ? NPC_OFF : m == "authored" ? NPC_AUTHORED : NPC_ALL;
        int mt = cfg::i(cfg::NPC_MAX_TIER);
        mt = mt < 0 ? 0 : mt > 5 ? 5 : mt;
        {
            Lock l;
            for (int k = 0; k < NUM_SKILL_TYPES; ++k)
                g_allowed[k] = allowed[k];
            g_attackCap = attackCap;
            g_blockCap = blockCap;
            g_speedBonus = speedBonus;
            g_npcMode = mode;
            g_npcMaxTier = mt;
            g_cache.clear();
        }
        char b[160];
        sprintf_s(b, "dual=%s max_tier=%d opt_in_ini=%d", mode == NPC_OFF ? "off" : mode == NPC_AUTHORED ? "authored" : "all",
                  mt, (int)g_iniOptIn.size());
        kit::Diag("npc_config").kv("text", b).emit();
    }

    void init()
    {
        InitializeCriticalSection(&g_lock);
        g_version = KenshiLib::GetKenshiVersion().GetVersion();
        g_layout = g_version == "1.0.65";
        std::string path = iniPath();
        int ft = (int)GetPrivateProfileIntA("Test", "force_tier", 0, path.c_str());
        g_forceTier = ft >= 1 && ft <= 5 ? ft : 0;
        char chance[32] = { 0 };
        GetPrivateProfileStringA("Test", "follow_up_chance", "", chance, sizeof(chance), path.c_str());
        if (chance[0] != '\0')
        {
            float c = (float)atof(chance);
            g_followUpChance = c < 0.0f ? -1.0f : (c > 1.0f ? 1.0f : c);
        }

        char ids[2048] = { 0 };
        GetPrivateProfileStringA("NPC", "opt_in", "", ids, sizeof(ids), path.c_str());
        std::string cur;
        for (const char* q = ids;; ++q)
        {
            if (*q == ',' || *q == '\0')
            {
                size_t a = cur.find_first_not_of(" \t"), b = cur.find_last_not_of(" \t");
                if (a != std::string::npos)
                    g_iniOptIn.insert(cur.substr(a, b - a + 1));
                cur.clear();
                if (*q == '\0')
                    break;
            }
            else
                cur += *q;
        }
        g_optIn = g_iniOptIn;
        applySettings();
    }

    void refreshOptIn()
    {
        if (ou == NULL)
            return;
        GameData* d = ou->gamedata.getData(OPT_IN_RECORD);
        std::set<std::string> ids = g_iniOptIn;
        int fromData = 0;
        if (d != NULL)
        {
            const char* lists[] = { "squad", "squad2", "leader", "choosefrom list", "faction" };
            for (int i = 0; i < 5; ++i)
            {
                const Ogre::vector<GameDataReference>::type* v = d->getReferenceListIfExists(lists[i]);
                if (v == NULL)
                    continue;
                for (size_t k = 0; k < v->size(); ++k)
                    if (ids.insert((*v)[k].sid).second)
                        ++fromData;
            }
        }
        bool changed;
        {
            Lock l;
            changed = ids != g_optIn;
            if (changed)
            {
                g_optIn = ids;
                g_cache.clear();
            }
        }
        if (changed || d == NULL)
        {
            static bool missingLogged = false;
            if (d != NULL || !missingLogged)
                kit::Diag("npc_opt_in").kv("record_found", d != NULL).kv("from_data", fromData)
                    .kv("from_ini", (int)g_iniOptIn.size()).kv("total", (int)ids.size()).emit();
            if (d == NULL)
                missingLogged = true;
        }
    }

    float poolSkill(const Info& s)
    {
        if (s.tier >= s.tierByDp || s.tier >= 5)
            return s.dp;
        float lim = tierDp(s.tier) - 0.01f;
        return s.dp < lim ? s.dp : lim;
    }

    float applySpeed(float gameResult, float factor, bool isBlock, float capBonus)
    {
        float r = gameResult * factor;
        if (factor <= 1.0f)
            return r;
        float cap = std::max((isBlock ? g_blockCap : g_attackCap) + capBonus, gameResult);
        return r < cap ? r : cap;
    }

    float offHandShare(float dp)
    {
        if (g_followUpChance >= 0.0f)
            return g_followUpChance;
        return dp < tierDp(0) ? 0.0f : balance::at(balance::OFF_HAND_SHARE, dp);
    }

    bool testShareSet() { return g_followUpChance >= 0.0f; }

    int forcedTier() { return g_forceTier; }

    bool allowed(WeaponCategory c) { return c >= 0 && c < NUM_SKILL_TYPES && g_allowed[c]; }

    bool weaponRefused(Weapon* w)
    {
        return w != NULL && kit::fcsInt(w->data, "nitoryu dual wield", 0) == 2;
    }

    bool weaponAllowed(Weapon* w)
    {
        if (w == NULL)
            return false;
        int v = kit::fcsInt(w->data, "nitoryu dual wield", 0);
        return v == 2 ? false : v == 1 ? true : allowed(w->category);
    }

    int rigOf(Character* c)
    {
        RaceData* race = c != NULL ? c->getRace() : NULL;
        if (race == NULL)
            return RIG_AUTO;
        int v = kit::fcsInt(race->data, "nitoryu rig", RIG_AUTO);
        return v == RIG_FIXED_GRIP || v == RIG_NEVER ? v : RIG_AUTO;
    }

    std::string allowListText()
    {
        std::string s;
        for (int i = 0; i < NUM_SKILL_TYPES; ++i)
        {
            if (!g_allowed[i]) continue;
            char b[8];
            sprintf_s(b, "%d", i);
            if (!s.empty()) s += ',';
            s += b;
        }
        return s;
    }

    int tierOf(float dp)
    {
        int t = 0;
        for (int i = 0; i < tuning::TIERS; ++i)
            if (dp >= tierDp(i))
                t = i + 1;
        return t;
    }

    StatsEnumerated statFor(WeaponCategory c)
    {
        switch (c)
        {
        case SKILL_KATANAS: return STAT_KATANAS;
        case SKILL_SABRES: return STAT_SABRES;
        case SKILL_BLUNT: return STAT_BLUNT;
        case SKILL_HEAVY: return STAT_HEAVYWEAPONS;
        case SKILL_HACKERS: return STAT_HACKERS;
        case ATTACK_POLEARMS: return STAT_POLEARMS;
        default: return STAT_NONE;
        }
    }

    Info evaluate(Character* c, Weapon* mainOverride)
    {
        if (c == NULL)
            return blank("no_character");
        DWORD now = GetTickCount();
        if (mainOverride == NULL)
        {
            Lock l;
            std::map<Character*, Cached>::iterator it = g_cache.find(c);
            if (it != g_cache.end() && now - it->second.tick < tuning::STANCE_CACHE_MS)
                return it->second.info;
        }
        Info r = computeSafe(c, mainOverride);
        if (mainOverride == NULL)
        {
            Lock l;
            Cached& e = g_cache[c];
            e.tick = now;
            e.info = r;
        }
        return r;
    }

    void invalidate(Character* c)
    {
        Lock l;
        g_cache.erase(c);
    }

    bool playerAllowed(Character* c)
    {
        Lock l;
        return g_playerOff.find(c) == g_playerOff.end();
    }

    bool togglePlayer(Character* c)
    {
        Lock l;
        g_cache.erase(c);
        std::set<Character*>::iterator it = g_playerOff.find(c);
        if (it == g_playerOff.end())
        {
            g_playerOff.insert(c);
            return false;
        }
        g_playerOff.erase(it);
        return true;
    }

    void setPlayerAllowed(Character* c, bool on)
    {
        Lock l;
        g_cache.erase(c);
        if (on)
            g_playerOff.erase(c);
        else
            g_playerOff.insert(c);
    }

    void forget(Character* c)
    {
        Lock l;
        g_cache.erase(c);
        g_playerOff.erase(c);
    }

    float previewDP(Character* c)
    {
        CharStats* st = c != NULL ? c->getStats() : NULL;
        Inventory* inv = c != NULL ? c->getInventory() : NULL;
        if (st == NULL || inv == NULL)
            return 0.0f;
        float skill = -1.0f;
        Weapon* back[2] = { NULL, NULL };
        Weapon* hip[1] = { NULL };
        int nb = weaponsIn(inv, "back", back, 2);
        int nh = weaponsIn(inv, "hip", hip, 1);
        Weapon* a = nb >= 1 ? back[0] : NULL;
        Weapon* b = nb >= 2 ? back[1] : nh == 1 ? hip[0] : NULL;
        if (a != NULL && b != NULL && stance::allowed(a->category) && stance::allowed(b->category))
        {
            float sa = st->getStat(stance::statFor(a->category), false);
            float sb = st->getStat(stance::statFor(b->category), false);
            skill = sa < sb ? sa : sb;
        }
        else
        {
            WeaponCategory cats[2] = { SKILL_KATANAS, SKILL_SABRES };
            for (int i = 0; i < 2; ++i)
                if (stance::allowed(cats[i]))
                {
                    float v = st->getStat(stance::statFor(cats[i]), false);
                    if (v > skill)
                        skill = v;
                }
        }
        return dpOf(skill < 0.0f ? 0.0f : skill, effDex(st), effAttack(st), effDefence(st), ambidex::effective(c).eff);
    }

    Preview preview(Character* c)
    {
        Preview p;
        p.dp = previewDP(c);
        p.tierByDp = tierOf(p.dp);
        float amb = ambidex::effective(c).eff;
        p.tier = capByAmbi(p.tierByDp, amb);
        if (c != NULL && !c->isPlayerCharacter() && p.tier > g_npcMaxTier)
            p.tier = g_npcMaxTier;
        if (g_forceTier > p.tier)
            p.tier = g_forceTier;
        p.nextAmbi = p.tier < p.tierByDp && amb < tierMinAmbi(p.tier) ? tierMinAmbi(p.tier) : 0.0f;
        CharStats* st = c != NULL ? c->getStats() : NULL;
        p.speedFactor = st != NULL ? speedFactorOf(p.dp, cap100(effDex(st))) : 1.0f;
        return p;
    }

    bool carriedPair(Character* c, Weapon*& a, Weapon*& b)
    {
        a = b = NULL;
        Inventory* inv = c != NULL ? c->getInventory() : NULL;
        if (inv == NULL)
            return false;
        Weapon* back[2] = { NULL, NULL };
        Weapon* hip[1] = { NULL };
        int nb = weaponsIn(inv, "back", back, 2);
        int nh = weaponsIn(inv, "hip", hip, 1);
        a = nb >= 1 ? back[0] : NULL;
        b = nb >= 2 ? back[1] : nh == 1 ? hip[0] : NULL;
        return a != NULL && b != NULL && stance::allowed(a->category) && stance::allowed(b->category)
            && !a->is2HandedOnly() && !b->is2HandedOnly();
    }

    bool hasPair(Character* c)
    {
        Weapon* a = NULL;
        Weapon* b = NULL;
        return carriedPair(c, a, b);
    }

    void prune(unsigned olderThanMs)
    {
        DWORD now = GetTickCount();
        Lock l;
        for (std::map<Character*, Cached>::iterator it = g_cache.begin(); it != g_cache.end();)
        {
            if (now - it->second.tick > olderThanMs)
                g_cache.erase(it++);
            else
                ++it;
        }
    }
}
