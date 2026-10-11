// Threads:
//   No hook of its own; evaluate() is called from every thread.
//   g_lock guards g_cache, g_playerOff, g_playerAny, g_optIn, g_listPath, g_pathLogged; Info is copied out by value.

#include "kit/input.h"   // Windows.h with NOMINMAX first

#include <core/Functions.h>
#include <kenshi/Kenshi.h>     // KenshiLib::GetKenshiVersion

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

#include "core/Math.h"
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
    std::set<Character*> g_playerAny;   // player characters who pair blunt and hackers too
    bool g_allowed[NUM_SKILL_TYPES] = { false };
    int g_forceTier = 0;                // [Test] force_tier
    float g_followUpChance = -1.0f;     // [Test] follow_up_chance (< 0: design values)
    float g_attackCap = 1.5f;           // cfg::DUAL_ATTACK_CAP ([Speed] dual_attack_cap), applySettings
    float g_blockCap = 4.0f;            // cfg::DUAL_BLOCK_CAP
    float g_speedBonus = 0.3f;          // cfg::SPEED_BONUS
    bool g_layout = true;                           // game version 1.0.65
    std::string g_version;

    // Which NPCs may fight dual. all = any NPC with an allowed pair that passes the stat gate;
    // authored = only NPCs another mod opted in; off = none. Opted-in NPCs (their CHARACTER or
    // FACTION record listed in [NPC] opt_in or in Nitoryu.mod's opt-in squad) skip the stat gate and
    // the max_tier cap.
    enum NpcMode { NPC_OFF, NPC_AUTHORED, NPC_ALL };
    NpcMode g_npcMode = NPC_ALL;                    // [NPC] dual
    int g_npcMaxTier = 3;                           // cfg::NPC_MAX_TIER ([NPC] max_tier)
    const char* OPT_IN_RECORD = "11-Nitoryu.mod";   // SQUAD_TEMPLATE: the NPC opt-in squad
    std::set<std::string> g_iniOptIn;               // [NPC] opt_in (string ids)
    std::set<std::string> g_optIn;                  // ini + the record's refs, under g_lock
    int g_npcRecordLeft = 12;                       // npc_record detail lines

    // A Path set for a character, as a floor on the computed one. The same three routes as its Dual
    // Wielding (Ambidex.cpp `authored`): the ini, a field of the CHARACTER or FACTION record, the second
    // value of a `squad` / `squad2` reference in the lists of Nitoryu.mod.
    const std::string PATH_KEY = "nitoryu path";    // float, below 0 = not set
    std::map<std::string, float> g_iniPath;         // [NPC] path=<string id>:<value>,... (read once)
    std::map<std::string, float> g_listPath;        // record id -> Path from the lists; under g_lock
    std::set<std::string> g_pathLogged;             // records with a path_authored line; under g_lock

    struct Lock
    {
        Lock() { EnterCriticalSection(&g_lock); }
        ~Lock() { LeaveCriticalSection(&g_lock); }
    };

    float clamp01x(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    float cap100(float v) { return clamp01x(v, 0.0f, 100.0f); }

    // The tier lists are settings (cfg::TIER_DP, cfg::TIER_MIN_AMBI; index 0 = T1). A list shorter
    // than five leaves the missing tiers out of reach, never open to everyone.
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

    // The weapons listed in an inventory section, in grid order.
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

    // The Path (docs/BALANCE.md): a weighted average of the inputs, each capped at 100.
    float dpOf(float skillLow, float dex, float atk, float def, float ambiEff)
    {
        core::PathWeights w = { cfg::f(cfg::W_SKILL), cfg::f(cfg::W_DEX), cfg::f(cfg::W_ATTACK), cfg::f(cfg::W_DEFENCE),
                                cfg::f(cfg::W_AMBI) };
        return core::pathOf(w, skillLow, dex, atk, def, ambiEff);
    }

    // Beginners are slower (x0.85 at DP 0, x1 at DP 30); masters with high Dexterity are
    // faster, up to x(1 + dual_speed_bonus) at Dex 100 + DP 100. dex is already capped at 100.
    float speedFactorOf(float dp, float dex)
    {
        return core::speedFactor(dp, dex, cfg::f(cfg::SPEED0), cfg::f(cfg::SPEED_FULL_DP), cfg::f(cfg::SPEED_FROM),
                                 g_speedBonus);
    }

    // The highest tier the effective Dual Wielding allows, at most `t`.
    int capByAmbi(int t, float ambiEff)
    {
        float minAmbi[tuning::TIERS];
        int n = cfg::items(cfg::TIER_MIN_AMBI);
        for (int i = 0; i < tuning::TIERS; ++i)
            minAmbi[i] = tierMinAmbi(i);
        return core::capByAmbi(t, ambiEff, minAmbi, n < tuning::TIERS ? n : tuning::TIERS);
    }

    std::string recordIdOf(GameData* d) { return d != NULL ? d->stringID : std::string(); }

    // The NPC's own record or its faction's record is on the opt-in list.
    bool optedIn(Character* c)
    {
        std::string own = recordIdOf(c->getGameData());
        Faction* f = c->getFaction();
        std::string fac = f != NULL ? recordIdOf(f->data) : std::string();
        // the same opt-in as a field of the CHARACTER or FACTION record itself (`nitoryu dual wielder`)
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
        // What a spawned NPC's record id is (the template's, or a copy's).
        if (log)
            kit::Diag("npc_record").kv("who", c->displayName).kv("record", own)
                .kv("record_name", c->getGameData() != NULL ? c->getGameData()->name : std::string())
                .kv("faction", fac).kv("opted_in", r).emit();
        return r;
    }

    // The Path set for this character, below 0 when none is. The character's own record wins over its
    // faction's; for one record the player's ini wins over a mod's field, and the field over the lists.
    float pathFloor(Character* c, const char*& from)
    {
        static const char* const FROM[2][3] = { { "ini_character", "character", "list_character" },
                                                { "ini_faction", "faction", "list_faction" } };
        Faction* f = c->getFaction();
        GameData* recs[2] = { c->getGameData(), f != NULL ? f->data : NULL };
        for (int i = 0; i < 2; ++i)
        {
            if (recs[i] == NULL)
                continue;
            std::map<std::string, float>::const_iterator it = g_iniPath.find(recs[i]->stringID);
            if (it != g_iniPath.end())
            {
                from = FROM[i][0];
                return cap100(it->second);
            }
            float v = kit::fcsFloat(recs[i], PATH_KEY, -1.0f);
            if (v == v && v >= 0.0f)
            {
                from = FROM[i][1];
                return cap100(v);
            }
            // The game's string is copied before the lock; nothing of the game is read under it, so a fault
            // caught by computeSafe's __except can never leave g_lock held (its destructor would not run).
            const std::string id = recs[i]->stringID;
            Lock l;
            it = g_listPath.find(id);
            if (it != g_listPath.end())
            {
                from = FROM[i][2];
                return cap100(it->second);
            }
        }
        return -1.0f;
    }

    stance::Info compute(Character* c, Weapon* mainOverride)
    {
        if (!g_layout)
            return blank("game_version");   // the left blade cannot be built on this layout
        if (stance::rigOf(c) == stance::RIG_NEVER)
            return blank("race_refused");   // the RACE record's `nitoryu rig` = 2
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
        // Two on the back win over a hip weapon (2+1 carries: the back pair is used).
        r.offFromHip = nb < 2 && r.off == hip[0];

        // the weapon's own record first (`nitoryu dual wield`), then the category list
        if (stance::weaponRefused(r.main) || stance::weaponRefused(r.off))
            return blank("weapon_refused");
        if (!stance::weaponAllowedFor(c, r.main) || !stance::weaponAllowedFor(c, r.off))
            return blank("type_not_allowed");
        if (r.main->is2HandedOnly() || r.off->is2HandedOnly())
            return blank("two_handed_only");
        // A second weapon with no `bare sword` mesh cannot be shown in the left hand: such a pair
        // fights one-handed (a clean fallback, no half-built stance).
        if (kit::fcsFile(r.off->data, "bare sword", "").empty())
            return blank("no_bare_mesh");
        MedicalSystem* med = c->getMedical();
        if (med != NULL && (!med->isLeftArmOk() || !med->isRightArmOk()))
            return blank("arm");
        // CharStats::rangedMode is not checked: it is the vanilla "Ranged" behaviour button (keep
        // distance, prefer crossbows), not "a crossbow is in hand", and a crossbow in hand already
        // fails the pair check above.
        bool player = c->isPlayerCharacter();
        if (player && !stance::playerAllowed(c))
            return blank("toggled_off");

        r.skillMain = st->getStat(stance::statFor(r.main->category), false);
        r.skillOff = st->getStat(stance::statFor(r.off->category), false);
        // Effective values, as the game's own combat formulas use them:
        // Dex with health and age, attack/defence with gear, weapon and indoor bonuses.
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

        // Inputs are capped at 100: modded games go past it. The Dual Wielding that counts is the
        // effective one: overload and heavy armour cut it hard (docs/BALANCE.md).
        ambidex::Effective amb = ambidex::effective(c);
        r.ambi = amb.a;
        r.ambiEff = amb.eff;
        r.overload = amb.overload;
        r.dex = cap100(dex);
        r.dp = dpOf(s, dex, atk, def, amb.eff);
        // A Path set for the character is a floor on the computed one, and the rank it gives stands
        // above the Dual Wielding cap and [NPC] max_tier (a mod's master is a master).
        const char* pathFrom = "";
        float pathMin = pathFloor(c, pathFrom);
        int pathTier = pathMin >= 0.0f ? stance::tierOf(pathMin) : 0;
        if (pathMin > r.dp)
        {
            diag::count(pathFrom[0] == 'i' ? "path_authored_ini" : pathFrom[0] == 'l' ? "path_authored_list" : "path_authored_field");
            bool first;
            const std::string record = recordIdOf(c->getGameData());    // read before the lock
            {
                Lock l;
                first = g_pathLogged.size() < 24 && g_pathLogged.insert(record).second;
            }
            if (first)
                kit::Diag("path_authored").kv("who", c->displayName).kv("from", pathFrom).kv("value", pathMin)
                    .kv("was", r.dp).kv("record", record).emit();
            r.dp = pathMin;
        }
        if (g_forceTier > 0)
        {
            // Test aid: at least that tier's DP, so its techniques (min skill = DP gate) qualify.
            float floor = tierDp(g_forceTier - 1);
            if (r.dp < floor)
                r.dp = floor;
        }
        r.tierByDp = stance::tierOf(r.dp);
        r.tier = capByAmbi(r.tierByDp, r.ambiEff);         // tiers need Dual Wielding
        if (r.tier < r.tierByDp)
            r.tierCap = "ambi";
        if (!player && !r.optedIn && r.tier > g_npcMaxTier)
        {
            r.tier = g_npcMaxTier;                          // [NPC] max_tier
            r.tierCap = "npc";
        }
        if (pathTier > r.tier)
        {
            r.tier = pathTier;                              // the set Path's own rank, above both caps
            if (r.tier >= r.tierByDp)
                r.tierCap = "";
        }
        if (g_forceTier > r.tier)
            r.tier = g_forceTier;                           // test aid: above every cap
        if (r.tier < r.tierByDp)
            diag::count(r.tierCap[0] == 'n' ? "tier_capped_npc" : "tier_capped_ambi");
        r.clumsy = core::clumsiness(cfg::f(cfg::CLUMSY0), cfg::f(cfg::CLUMSY_SLOPE), r.dp);
        r.guardPenalty = balance::dividedGuard(r.tier);
        r.speedFactor = speedFactorOf(r.dp, r.dex);
        r.dual = true;
        return r;
    }

    // setWeapon also runs while characters load, so a read can meet a half-built
    // object: fault into a counter instead of the game. No C++ objects live in
    // guarded()'s frame (C2712).
    // With /EHsc the frames between the fault and this __except are left without their destructors, so
    // compute() and what it calls never read game memory while one of our locks is held. A stack overflow
    // is not caught: swallowed, the next one would end the game with no dump.
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
        __except (GetExceptionCode() == EXCEPTION_STACK_OVERFLOW ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
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

    // The settings this module keeps a copy of (cfg: the table < FCS < the ini). Called from init and
    // again by the main loop when cfg::refresh saw a change (a load, another record).
    void applySettings()
    {
        // the allow-list of weapon categories
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
        // [NPC]
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
            g_cache.clear();        // a cached stance was computed with the old values
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
        // [NPC] opt_in stays an ini list (and the opt-in squad's references)
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
        // [NPC] path=<string id>:<value>, CHARACTER or FACTION ids, comma separated
        int badPath = cfg::readIdValues(path, "NPC", "path", g_iniPath);
        if (!g_iniPath.empty() || badPath > 0)
            kit::Diag("npc_path").kv("from_ini", (int)g_iniPath.size()).kv("bad_entries", badPath).emit();
        applySettings();
    }

    // Main loop, after each game load (with the CONSTANTS read): the ini list plus every
    // CHARACTER and FACTION another mod added to the opt-in squad's reference lists.
    void refreshOptIn()
    {
        if (ou == NULL)
            return;
        GameData* d = ou->gamedata.getData(OPT_IN_RECORD);
        std::set<std::string> ids = g_iniOptIn;
        int fromData = 0;
        // Dual Wielding in plain FCS. A character's val0 (2 or more; FCS adds an entry with 1) is its
        // Dual Wielding; the four lists below also give their own value to every CHARACTER and FACTION
        // in them, and opt them in. A record in two lists takes the higher value.
        struct { const char* record; float skill; } LISTS[] = {
            { OPT_IN_RECORD, -1.0f }, { "23-Nitoryu.mod", 40.0f }, { "24-Nitoryu.mod", 60.0f },
            { "25-Nitoryu.mod", 80.0f }, { "26-Nitoryu.mod", 100.0f } };
        // In `squad` and `squad2` (the lists FCS shows two values for) the second value, 2 or more, is
        // the character's Path. A record in two lists takes the higher value.
        std::map<std::string, float> skills, paths;
        for (int r = 0; r < 5; ++r)
        {
            GameData* rec = r == 0 ? d : ou->gamedata.getData(LISTS[r].record);
            if (rec == NULL)
                continue;
            const char* lists[] = { "squad", "squad2", "leader", "choosefrom list", "faction" };
            for (int i = 0; i < 5; ++i)
            {
                const Ogre::vector<GameDataReference>::type* v = rec->getReferenceListIfExists(lists[i]);
                if (v == NULL)
                    continue;
                for (size_t k = 0; k < v->size(); ++k)
                {
                    const GameDataReference& ref = (*v)[k];
                    if (ids.insert(ref.sid).second)
                        ++fromData;
                    if (i < 2 && ref.values.value[1] >= 2)
                    {
                        float p = (float)ref.values.value[1];
                        std::map<std::string, float>::iterator pa = paths.find(ref.sid);
                        if (pa == paths.end())
                            paths[ref.sid] = p;
                        else if (p > pa->second)
                            pa->second = p;
                    }
                    float own = i < 4 && ref.values.value[0] >= 2 ? (float)ref.values.value[0] : LISTS[r].skill;
                    if (own < 0.0f)
                        continue;
                    std::map<std::string, float>::iterator at = skills.find(ref.sid);
                    if (at == skills.end())
                        skills[ref.sid] = own;
                    else if (own > at->second)
                        at->second = own;
                }
            }
        }
        ambidex::setListSkills(skills);
        bool changed;
        {
            Lock l;
            changed = ids != g_optIn || paths != g_listPath;
            if (changed)
            {
                g_optIn = ids;
                g_listPath = paths;
                g_cache.clear();
            }
        }
        if (changed || d == NULL)
        {
            static bool missingLogged = false;
            if (d != NULL || !missingLogged)
                kit::Diag("npc_opt_in").kv("record_found", d != NULL).kv("from_data", fromData)
                    .kv("from_ini", (int)g_iniOptIn.size()).kv("total", (int)ids.size())
                    .kv("with_skill", (int)skills.size()).kv("with_path", (int)paths.size()).emit();
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

    // The dual speed factor may slow a fighter below the game's result freely,
    // but a bonus never lifts it past the cap; if the game (or another mod)
    // already returned more than the cap, that result is kept.
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

    bool anyWeapon(Character* c)
    {
        Lock l;
        return g_playerAny.find(c) != g_playerAny.end();
    }

    bool toggleAnyWeapon(Character* c)
    {
        Lock l;
        g_cache.erase(c);
        std::set<Character*>::iterator it = g_playerAny.find(c);
        if (it == g_playerAny.end())
        {
            g_playerAny.insert(c);
            return true;
        }
        g_playerAny.erase(it);
        return false;
    }

    void setAnyWeapon(Character* c, bool on)
    {
        Lock l;
        g_cache.erase(c);
        if (on)
            g_playerAny.insert(c);
        else
            g_playerAny.erase(c);
    }

    bool categoryAllowedFor(Character* c, WeaponCategory cat)
    {
        if (allowed(cat))
            return true;
        return (cat == SKILL_BLUNT || cat == SKILL_HACKERS) && c != NULL && c->isPlayerCharacter() && anyWeapon(c);
    }

    bool weaponAllowedFor(Character* c, Weapon* w)
    {
        if (w == NULL)
            return false;
        int v = kit::fcsInt(w->data, "nitoryu dual wield", 0);
        return v == 2 ? false : v == 1 ? true : categoryAllowedFor(c, w->category);
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
        float thresholds[tuning::TIERS];
        int n = cfg::items(cfg::TIER_DP);
        for (int i = 0; i < tuning::TIERS; ++i)
            thresholds[i] = tierDp(i);
        return core::tierOf(dp, thresholds, n < tuning::TIERS ? n : tuning::TIERS, tuning::TIERS);
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
        g_playerAny.erase(c);
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
        if (a != NULL && b != NULL && stance::categoryAllowedFor(c, a->category) && stance::categoryAllowedFor(c, b->category))
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
        float dp = dpOf(skill < 0.0f ? 0.0f : skill, effDex(st), effAttack(st), effDefence(st), ambidex::effective(c).eff);
        const char* from = "";
        float pathMin = pathFloor(c, from);                 // the window shows what the fight uses
        return pathMin > dp ? pathMin : dp;
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
        const char* from = "";
        float pathMin = c != NULL ? pathFloor(c, from) : -1.0f;
        if (pathMin >= 0.0f && tierOf(pathMin) > p.tier)
            p.tier = tierOf(pathMin);                       // as in compute
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
        return a != NULL && b != NULL && stance::categoryAllowedFor(c, a->category) && stance::categoryAllowedFor(c, b->category)
            && !a->is2HandedOnly() && !b->is2HandedOnly();
    }

    bool needsAnyWeapon(Character* c)
    {
        Inventory* inv = c != NULL ? c->getInventory() : NULL;
        if (inv == NULL)
            return false;
        Weapon* back[2] = { NULL, NULL };
        Weapon* hip[1] = { NULL };
        int nb = weaponsIn(inv, "back", back, 2);
        int nh = weaponsIn(inv, "hip", hip, 1);
        Weapon* w[2] = { nb >= 1 ? back[0] : NULL, nb >= 2 ? back[1] : nh == 1 ? hip[0] : NULL };
        bool locked = false;
        for (int i = 0; i < 2; ++i)
        {
            if (w[i] == NULL || w[i]->is2HandedOnly() || weaponRefused(w[i]))
                return false;
            if (weaponAllowed(w[i]))
                continue;
            if (w[i]->category != SKILL_BLUNT && w[i]->category != SKILL_HACKERS)
                return false;
            locked = true;
        }
        return locked;
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
