// Dual Wielding (A in the code): growth and save, off-hand XP, the stats line.
// Vanilla flows:
//   xpMelee gives the weapon XP through the stats' current skill pointer -> growth and XP routing;
//   the combat hooks run off the main-loop thread -> A behind one lock.
// Save: a float in the character's STATS GameData, written after CharStats::serialise and read before
// CharStats::updateStats.
// Threads:
//   xpMelee_hook, increase_hook - combat threads; increaseStat is also called by the game for every other
//                                 skill, on any thread: it acts only on the pointer xpMelee captured on the
//                                 same thread (thread-local t_cap*).
//   serialise_hook, updateStats_hook - save and load.  charDtor_hook - any thread.
//   statsWindow_hook, lineResize_hook - main loop (UI).
//   g_lock guards g_amb, g_rankMax, g_foes. Never held across a game original or a call into another module.

#include "kit/input.h"   // Windows.h with NOMINMAX first

#include "kit/log.h"
#include <core/Functions.h>

#include <kenshi/Kenshi.h>
#include <kenshi/Globals.h>
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/Damages.h>
#include <kenshi/Gear.h>
#include <kenshi/Inventory.h>
#include <kenshi/Item.h>
#include <kenshi/GameData.h>
#include <kenshi/Faction.h>
#include <kenshi/gui/CharacterStatsWindow.h>
#include <kenshi/gui/DatapanelGUI.h>
#include <kenshi/gui/DataPanelLine.h>
#include <kenshi/combat/CombatClass.h>
#include <kenshi/combat/CombatTechniqueData.h>

#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_Widget.h>
#include <mygui/MyGUI_InputManager.h>
#include <OgreResourceGroupManager.h>
#include <OgreDataStream.h>
#include <kenshi/GameWorld.h>

#include <cstdio>
#include <cstring>
#include <climits>
#include <map>
#include <string>

#include "core/Math.h"
#include "Ambidex.h"
#include "Combat.h"
#include "Defense.h"
#include "Pressure.h"
#include "Mastery.h"
#include "Blades.h"
#include "Stance.h"
#include "Lefty.h"
#include "Feedback.h"
#include "Lang.h"
#include "Settings.h"
#include "Tuning.h"
#include "Balance.h"
#include "Diagnostics.h"
#include "kit/diag.h"
#include "kit/fcs.h"
#include "Hooks.h"

namespace
{
    const char* const SAVE_KEY = "NIT_ambidexterity";
    const char* const DUAL_OFF_KEY = "NIT_dual_off";   // 1 = the player turned the stance off
    const char* const ANY_WEAPON_KEY = "NIT_any_weapon";   // 1 = blunt and hackers pair for this character
    const char* const RANK_KEY = "NIT_rank";           // the highest rank reached
    const char* const UI_KEY = "nitoryu_ambidexterity";
    const int DETAIL_BUDGET = 60;

    volatile bool g_enabled = true;
    bool g_showLine = true;                 // [Stats] show
    float g_testA = -1.0f;                  // [Test] ambidexterity (< 0: off)
    bool g_testSkill = false;               // [Test] ambidexterity=skill: A = the best allowed weapon skill
    // A value a mod or the player gives a character. The field sits on CHARACTER and FACTION; the ini
    // list names either by string id.
    const std::string FIELD_KEY = "nitoryu dual wielding";   // float, below 0 = not set
    std::map<std::string, float> g_iniSkill;                 // [NPC] skill=<string id>:<value>,... (read once)
    float g_testNpcA = -1.0f;               // [Test] npc_ambidexterity (< 0: off): every NPC, never saved
    bool g_testNpcSkill = false;            // [Test] npc_ambidexterity=skill: an NPC's best allowed weapon skill
    std::string g_label = "Dual Wielding";  // [Stats] label

    // The hover description, in the vanilla layout; every string is [Stats]-overridable
    // (the game translates its own, not ours).
    struct Texts
    {
        std::string affectsTitle, affects, trainTitle, train, descTitle, desc, nextLevel;
        std::string inFight, overloaded, heavyArmour, proficiency, tier;      // the penalty and DP
        std::string tierCapped, speed;                                         // tier gate, speed
        std::string dexBonus;
        std::string pair, pairLongShort, pairEqual, pairMixed;                 // the carried pair
        std::string rank[6];                                                   // rank names, 1-5
        std::string ranksTitle, rankOpens[6], rankNeeds, rankNeedsDp;          // the five-rank list
        std::string rankUp;                                                    // the on-screen message
        std::string rankUpOpens;                                               // its second line
        std::string marksTitle, markFlow, markMugen, markPressure;             // what the marks mean
        std::string pathTitle, pathFormula, pathNote;                          // how the Path is counted
    };
    Texts g_text;

    // "Ronin (62)"; a tier without a name (0, or an empty lang key) keeps "62 (tier n)".
    std::string rankLabel(int tier, int dp)
    {
        char v[96];
        if (tier >= 1 && tier <= 5 && !g_text.rank[tier].empty())
            sprintf_s(v, "%s (%d)", g_text.rank[tier].c_str(), dp);
        else if (tier >= 1)
            sprintf_s(v, "%d (%s %d)", dp, g_text.tier.c_str(), tier);
        else
            sprintf_s(v, "%d", dp);
        return std::string(v);
    }

    std::string rankName(int tier)
    {
        if (tier >= 1 && tier <= 5 && !g_text.rank[tier].empty())
            return g_text.rank[tier];
        char n[16];
        sprintf_s(n, "%d", tier);
        return std::string(n);
    }

    // "30", "2.5": a setting's value for a text.
    std::string number(float v)
    {
        char n[32];
        sprintf_s(n, "%g", (double)((float)((int)(v * 10.0f + (v < 0.0f ? -0.5f : 0.5f))) / 10.0f));
        return std::string(n);
    }

    void replaceAll(std::string& s, const char* what, const std::string& with)
    {
        size_t n = strlen(what);
        for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + with.size()))
            s.replace(p, n, with);
    }

    // ---- rank colours -----------------------------------------------------------
    // Grey, white, blue, purple, red: the game's item grade colours. They are one MyGUI colour table,
    // kenshi_colours.xml, which a recolour mod replaces with its own copy, so rank n takes the key
    // ItemGrade n as the load order left it; a key the table lacks (vanilla has no ItemGrade5) keeps ours.
    // Index 0 is a locked rank.
    const char* const RANK_COLOUR_KEY[6] = { "Greyed", "ItemGrade1", "ItemGrade2", "ItemGrade3", "ItemGrade4", "ItemGrade5" };
    const char* const RANK_COLOUR_DEFAULT[6] = { "#444444", "#d9ccbf", "#4099b3", "#66fc80", "#fceb0d", "#e60000" };
    std::string g_rankColour[6];
    // the hover's headings (1) and the marks' names (2)
    const char* const HEAD_COLOUR_KEY[2] = { "Main", "Good" };
    const char* const HEAD_COLOUR_DEFAULT[2] = { "#afa68b", "#a8b774" };
    std::string g_headColour[2];
    bool g_coloursRead = false;
    bool g_coloursOn = true;                // [Stats] rank_colours
    bool g_rankUpMessage = true;            // [Stats] rank_up_message

    bool hexDigit(char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'); }

    // <Property key="ItemGrade3" value="#66fc80"/> -> "#66fc80"; "" when the key is not there.
    std::string colourIn(const std::string& xml, const char* key)
    {
        size_t at = xml.find(std::string("key=\"") + key + "\"");
        if (at == std::string::npos)
            return "";
        size_t close = xml.find('>', at);
        size_t v = xml.find("value=\"", at);
        if (v == std::string::npos || (close != std::string::npos && v > close) || v + 14 > xml.size())
            return "";
        std::string c = xml.substr(v + 7, 7);
        if (c[0] != '#')
            return "";
        for (int i = 1; i < 7; ++i)
            if (!hexDigit(c[i]))
                return "";
        return c;
    }

    // Main thread, once, at the first hover (the resource groups are complete by then).
    void readColours()
    {
        if (g_coloursRead)
            return;
        g_coloursRead = true;
        for (int i = 0; i < 6; ++i)
            g_rankColour[i] = RANK_COLOUR_DEFAULT[i];
        for (int i = 0; i < 2; ++i)
            g_headColour[i] = HEAD_COLOUR_DEFAULT[i];
        std::string xml, source = "not found";
        try
        {
            const char* names[3] = { "kenshi_colours.xml", "colours/kenshi_colours.xml", "gui/colours/kenshi_colours.xml" };
            Ogre::ResourceGroupManager& rgm = Ogre::ResourceGroupManager::getSingleton();
            for (int i = 0; i < 3 && xml.empty(); ++i)
            {
                if (!rgm.resourceExistsInAnyGroup(names[i]))
                    continue;
                Ogre::String group = rgm.findGroupContainingResource(names[i]);
                Ogre::DataStreamPtr s = rgm.openResource(names[i], group);
                if (!s.isNull())
                {
                    xml = s->getAsString();
                    source = std::string(names[i]) + " in " + group;
                }
            }
        }
        catch (...)
        {
            source = "exception";
        }
        int found = 0;
        for (int i = 0; i < 6; ++i)
        {
            std::string c = colourIn(xml, RANK_COLOUR_KEY[i]);
            if (c.empty())
                continue;
            g_rankColour[i] = c;
            ++found;
        }
        for (int i = 0; i < 2; ++i)
        {
            std::string c = colourIn(xml, HEAD_COLOUR_KEY[i]);
            if (!c.empty())
                g_headColour[i] = c;
        }
        kit::Diag("rank_colours").kv("source", source).kv("keys_found", found).kv("locked", g_rankColour[0])
            .kv("r1", g_rankColour[1]).kv("r2", g_rankColour[2]).kv("r3", g_rankColour[3]).kv("r4", g_rankColour[4])
            .kv("r5", g_rankColour[5]).kv("heading", g_headColour[0]).kv("heading2", g_headColour[1]).emit();
    }

    const char* headColour(int level)
    {
        readColours();
        return g_headColour[level == 2 ? 1 : 0].c_str();
    }

    // The hover's headings and the marks' names in one colour each, an empty row between sections, the
    // Path's parts one per line. Both colours come from the game's colour table by key, so a recolour mod
    // carries them: Main is the cream of the game's own "Stats Affected:" lines, Good its muted green.
    const char* headColour(int level);

    // A text of lang/*.ini as lines: a | starts a new line. `firstColour` paints the part before the first |.
    void setBlock(DatapanelGUI* panel, int cat, const std::string& text, const char* firstColour)
    {
        const char* const wrap = "Kenshi_WordWrapEmpty";
        size_t from = 0;
        for (bool first = true; from <= text.size(); first = false)
        {
            size_t bar = text.find('|', from);
            std::string part = text.substr(from, bar == std::string::npos ? std::string::npos : bar - from);
            if (!part.empty())
                panel->setLine((first && firstColour != NULL ? std::string(firstColour) : std::string()) + part, "",
                               wrap, wrap, cat);
            if (bar == std::string::npos)
                break;
            from = bar + 1;
        }
    }

    // An empty row between two sections; a line's text is its key, so each one is a different run of spaces.
    void setGap(DatapanelGUI* panel, int cat, int& gaps)
    {
        const char* const wrap = "Kenshi_WordWrapEmpty";
        panel->setLine(std::string(++gaps, ' '), "", wrap, wrap, cat);
    }

    std::string heading(const std::string& title) { return std::string(headColour(1)) + title; }

    // The MyGUI colour tag of a rank (0 or locked: the greyed one); "" while [Stats] rank_colours is off.
    std::string rankColour(int tier)
    {
        if (!g_coloursOn)
            return "";
        readColours();
        return g_rankColour[tier >= 1 && tier <= 5 ? tier : 0];
    }

    CRITICAL_SECTION g_lock;
    std::map<Character*, float> g_amb;
    // The highest rank a player character has reached (saved as NIT_rank), so the rank-up moment comes
    // once per rank and not again after an overload took a rank away for a while.
    std::map<Character*, int> g_rankMax;    // under g_lock
    // The last two enemies whose attack reached a dual fighter (blocked or landed), for "outnumbered".
    // The pointers are compared, never followed.
    struct Foes
    {
        Character* who[2];
        double t[2];
    };
    std::map<Character*, Foes> g_foes;      // under g_lock
    std::map<std::string, float> g_listSkill;   // record id -> Dual Wielding from the lists; under g_lock
    int g_detailLeft = DETAIL_BUDGET;
    // the weapon skills last accounted for, per dual fighter (watchWeaponXp, below)
    struct WeaponSeen
    {
        int stat[2];        // StatsEnumerated of the pair's weapon skills (-1: none); one entry for a same-type pair
        float lvl[2];       // their levels when last accounted for
        double next;        // game ms of the next look
    };
    std::map<Character*, WeaponSeen> g_seen;        // under g_lock
    const double SEEN_STEP_MS = 500.0;

    struct Lock
    {
        Lock() { EnterCriticalSection(&g_lock); }
        ~Lock() { LeaveCriticalSection(&g_lock); }
    };

    bool finite(float v) { return v == v && v > -1.0e6f && v < 1.0e6f; }

    bool lookup(Character* c, float& out)
    {
        Lock l;
        std::map<Character*, float>::iterator it = g_amb.find(c);
        if (it == g_amb.end())
            return false;
        out = it->second;
        return true;
    }

    // No upper cap of our own: increaseStat's limit decides, as for every skill (mods raise the
    // game's maximum level).
    void store(Character* c, float a)
    {
        Lock l;
        g_amb[c] = a < 0.0f ? 0.0f : a;
    }

    // The best allowed weapon skill (katana, sabre, blunt as the allow-list says). A character's
    // starting Dual Wielding is AMB_SEED x it.
    float bestSkill(Character* c)
    {
        CharStats* st = c != NULL ? c->getStats() : NULL;
        if (st == NULL)
            return 0.0f;
        float best = 0.0f;
        WeaponCategory cats[3] = { SKILL_KATANAS, SKILL_SABRES, SKILL_BLUNT };
        for (int i = 0; i < 3; ++i)
        {
            if (!stance::allowed(cats[i]))
                continue;
            float v = st->getStat(stance::statFor(cats[i]), false);
            if (finite(v) && v > best)
                best = v;
        }
        return best;
    }

    float seedFor(Character* c) { return cfg::f(cfg::AMB_SEED) * bestSkill(c); }

    // The Dual Wielding set for this character, below 0 when none is. The character's own record wins
    // over its faction's; for one record the player's ini wins over a mod's field, and the field over
    // the lists.
    float authored(Character* c, const char*& from)
    {
        static const char* const FROM[2][3] = { { "ini_character", "character", "list_character" },
                                                { "ini_faction", "faction", "list_faction" } };
        Faction* f = c->getFaction();
        GameData* recs[2] = { c->getGameData(), f != NULL ? f->data : NULL };
        for (int i = 0; i < 2; ++i)
        {
            if (recs[i] == NULL)
                continue;
            std::map<std::string, float>::const_iterator it = g_iniSkill.find(recs[i]->stringID);
            if (it != g_iniSkill.end())
            {
                from = FROM[i][0];
                return it->second;
            }
            float v = kit::fcsFloat(recs[i], FIELD_KEY, -1.0f);
            if (finite(v) && v >= 0.0f)
            {
                from = FROM[i][1];
                return v;
            }
            // the Dual Wielding lists of Nitoryu.mod (plain FCS): stance::refreshOptIn reads them after a load
            Lock l;
            it = g_listSkill.find(recs[i]->stringID);
            if (it != g_listSkill.end())
            {
                from = FROM[i][2];
                return it->second;
            }
        }
        return -1.0f;
    }

    bool takeDetail()
    {
        Lock l;
        if (g_detailLeft <= 0)
            return false;
        --g_detailLeft;
        return true;
    }

    // The attacker's xpMelee runs after the victim's hitByMeleeAttack returns, on the same thread.
    __declspec(thread) Character* t_impactAttacker = NULL;
    __declspec(thread) CombatTechniqueData* t_impactTech = NULL;
    __declspec(thread) bool t_impactOff = false;

    // ---- save and load ---------------------------------------------------------

    void (*serialise_orig)(CharStats*, GameData*) = NULL;
    void serialise_hook(CharStats* self, GameData* data)
    {
        diag::HookCall hc(diag::HK_SERIALISE);
        serialise_orig(self, data);
        if (!g_enabled || self == NULL || self->me == NULL || data == NULL)
            return;
        hc.ours();
        float a = 0.0f;
        if (lookup(self->me, a) && finite(a))
        {
            // A deliberate write into the save's own STATS data (not an FCS read).
            std::string key(SAVE_KEY);
            if (data->fdata.find(key) != data->fdata.end())
                data->fdata.find(key)->second = a;
            else
                data->fdata.insert(std::make_pair(key, a));
            diag::count("ambi_saved");
        }
        // The highest rank reached, so its moment is not shown again after a load.
        int rankMax = -1;
        {
            Lock l;
            std::map<Character*, int>::iterator it = g_rankMax.find(self->me);
            if (it != g_rankMax.end())
                rankMax = it->second;
        }
        if (rankMax >= 0)
        {
            std::string rk(RANK_KEY);
            if (data->fdata.find(rk) != data->fdata.end())
                data->fdata.find(rk)->second = (float)rankMax;
            else
                data->fdata.insert(std::make_pair(rk, (float)rankMax));
        }
        // The stance toggle: written when off, cleared to 0 when an earlier save had it.
        bool off = self->me->isPlayerCharacter() && !stance::playerAllowed(self->me);
        std::string dk(DUAL_OFF_KEY);
        if (data->fdata.find(dk) != data->fdata.end())
            data->fdata.find(dk)->second = off ? 1.0f : 0.0f;
        else if (off)
            data->fdata.insert(std::make_pair(dk, 1.0f));
        if (off)
            diag::count("toggle_saved_off");
        // The any-weapon switch, the same way.
        bool any = self->me->isPlayerCharacter() && stance::anyWeapon(self->me);
        std::string ak(ANY_WEAPON_KEY);
        if (data->fdata.find(ak) != data->fdata.end())
            data->fdata.find(ak)->second = any ? 1.0f : 0.0f;
        else if (any)
            data->fdata.insert(std::make_pair(ak, 1.0f));
        if (any)
            diag::count("any_weapon_saved");
    }

    void (*updateStats_orig)(CharStats*, GameData*) = NULL;
    void updateStats_hook(CharStats* self, GameData* data)
    {
        diag::HookCall hc(diag::HK_UPDATE_STATS);
        // Read before the original: it may rebuild the data it was given.
        bool has = false;
        float a = 0.0f;
        if (g_enabled && data != NULL && data->fdata.count(SAVE_KEY) != 0)
        {
            a = data->fdata.find(SAVE_KEY)->second;
            has = finite(a);
        }
        int dualOff = -1;
        if (g_enabled && data != NULL && data->fdata.count(DUAL_OFF_KEY) != 0)
            dualOff = data->fdata.find(DUAL_OFF_KEY)->second > 0.5f ? 1 : 0;
        int anyWeapon = -1;
        if (g_enabled && data != NULL && data->fdata.count(ANY_WEAPON_KEY) != 0)
            anyWeapon = data->fdata.find(ANY_WEAPON_KEY)->second > 0.5f ? 1 : 0;
        int rankMax = -1;
        if (g_enabled && data != NULL && data->fdata.count(RANK_KEY) != 0)
        {
            float r = data->fdata.find(RANK_KEY)->second;
            rankMax = finite(r) && r >= 0.0f ? (int)(r + 0.5f) : -1;
        }
        updateStats_orig(self, data);
        if (has || dualOff >= 0 || anyWeapon >= 0 || rankMax >= 0)
            hc.ours();                      // a save that carries a key of ours
        if (rankMax >= 0 && self != NULL && self->me != NULL)
        {
            Lock l;
            g_rankMax[self->me] = rankMax > 5 ? 5 : rankMax;
        }
        if (dualOff >= 0 && self != NULL && self->me != NULL)
        {
            stance::setPlayerAllowed(self->me, dualOff == 0);
            if (dualOff == 1)
                diag::count("toggle_loaded_off");
        }
        if (anyWeapon >= 0 && self != NULL && self->me != NULL)
        {
            stance::setAnyWeapon(self->me, anyWeapon == 1);
            if (anyWeapon == 1)
                diag::count("any_weapon_loaded");
        }
        // A template without the key is not an instruction to forget a loaded A. Nor is a negative
        // value (fcs.def's default -1 for STATS records edited in FCS: "not set", seed it).
        if (has && a >= 0.0f && self != NULL && self->me != NULL)
        {
            store(self->me, a);
            diag::count("ambi_loaded");
        }
    }

    void (*charDtor_orig)(Character*) = NULL;
    void charDtor_hook(Character* self)
    {
        diag::HookCall hc(diag::HK_CHAR_DTOR);
        hc.ours();                          // every character's records end here
        {
            Lock l;
            g_amb.erase(self);
            g_seen.erase(self);
            g_rankMax.erase(self);
            g_foes.erase(self);
        }
        // Every per-character record ends here, so a new character allocated at this address starts
        // clean. Visual and Arms (main loop only) re-check each entry's handle every frame instead.
        stance::forget(self);
        pressure::forget(self);
        mastery::forget(self);
        blades::forget(self);
        combat::forget(self);
        defense::forget(self);
        lefty::forget(self);
        feedback::forget(self);
        charDtor_orig(self);
    }

    // ---- growth and off-hand XP ------------------------------------------------

    // Every skill gain goes through increaseStat(stat, amount, limit):
    // stat += amount * ((limit - stat) / limit)^2, so each level is slower than the last. While our
    // xpMelee hook runs, the raw amounts given to the weapon skill are captured here, and A is raised
    // by the same function with its own level, so it levels like any vanilla skill (and follows mods
    // that change the curve by hooking it).
    __declspec(thread) float* t_capStat = NULL;
    __declspec(thread) float t_capAmount = 0.0f;
    __declspec(thread) float t_capLimit = 101.0f;
    // A hit taken gives the weapon skill nothing, so the Melee Defence amount of that call is captured
    // the same way.
    __declspec(thread) float* t_capDefence = NULL;
    __declspec(thread) float t_capDefenceAmount = 0.0f;

    void (*increase_orig)(float&, float, float) = NULL;
    void increase_hook(float& stat, float amount, float limit)
    {
        diag::HookCall hc(diag::HK_INCREASE_STAT);
        if (t_capStat != NULL && &stat == t_capStat)
        {
            hc.ours();
            t_capAmount += amount;
            t_capLimit = limit;
        }
        else if (t_capDefence != NULL && &stat == t_capDefence)
        {
            hc.ours();
            t_capDefenceAmount += amount;
            t_capLimit = limit;
        }
        increase_orig(stat, amount, limit);
    }

    // The skill XP goes to is pointed back, and the capture ends, even when the game throws inside
    // xpMelee.
    struct SkillRedirect
    {
        CharStats* st;
        float* saved;
        float* prevCap;
        float* prevDefence;
        SkillRedirect(CharStats* s, float* to, float* defence)
            : st(s), saved(s->pCurrentWeaponSkill), prevCap(t_capStat), prevDefence(t_capDefence)
        {
            if (to != NULL)
                s->pCurrentWeaponSkill = to;
            t_capStat = s->pCurrentWeaponSkill;
            t_capAmount = 0.0f;
            t_capDefence = defence;
            t_capDefenceAmount = 0.0f;
        }
        ~SkillRedirect()
        {
            st->pCurrentWeaponSkill = saved;
            t_capStat = prevCap;
            t_capDefence = prevDefence;
        }
    };

    // Notes `attacker` (NULL: none) and says whether two enemies reached `me` within the window.
    bool outnumbered(Character* me, Character* attacker, double now)
    {
        double window = cfg::d(cfg::AMB_XP_OUTNUMBERED_MS);
        Lock l;
        std::map<Character*, Foes>::iterator it = g_foes.find(me);
        if (it == g_foes.end())
        {
            if (attacker == NULL)
                return false;
            if (g_foes.size() > 4096)
                g_foes.clear();
            Foes z = { { NULL, NULL }, { 0.0, 0.0 } };
            it = g_foes.insert(std::make_pair(me, z)).first;
        }
        Foes& f = it->second;
        if (attacker != NULL)
        {
            int slot = f.who[0] == attacker ? 0 : f.who[1] == attacker ? 1 : f.who[0] == NULL ? 0
                : f.who[1] == NULL ? 1 : (f.t[0] <= f.t[1] ? 0 : 1);
            f.who[slot] = attacker;
            f.t[slot] = now;
        }
        for (int i = 0; i < 2; ++i)
            if (f.who[i] == NULL || now < f.t[i] || now - f.t[i] > window)
                return false;
        return true;
    }

    // A WEAPON record's `nitoryu dual wielding xp` (1 = none): a pair made for training.
    float weaponXp(Weapon* w)
    {
        float k = w != NULL ? kit::fcsFloat(w->data, "nitoryu dual wielding xp", 1.0f) : 1.0f;
        return finite(k) && k >= 0.0f ? k : 1.0f;
    }

    float atLeast0(float v) { return finite(v) && v > 0.0f ? v : 0.0f; }

    // ---- Dual Wielding grows from the weapon skills' own gains ---------------------------------------------
    // A plugin that hooks xpMelee after Nitoryu and does not call the original leaves our xpMelee_hook unused,
    // so nothing would grow Dual Wielding. The main loop therefore watches each dual fighter's weapon skills:
    // whatever they gained since last looked at, not already handled by our xpMelee_hook (it moves the mark
    // after the game's XP), trains Dual Wielding by the same share and curve.

    int pairStats(const stance::Info& s, int out[2])
    {
        int n = 0;
        Weapon* w[2] = { s.main, s.off };
        for (int i = 0; i < 2; ++i)
        {
            StatsEnumerated st = w[i] != NULL ? stance::statFor(w[i]->category) : STAT_NONE;
            if (st != STAT_NONE && (n == 0 || out[0] != (int)st))
                out[n++] = (int)st;
        }
        return n;
    }

    // The levels as they are now become the mark (after our own xpMelee_hook, and on a first look).
    void markSeen(Character* c, CharStats* st, const stance::Info& s, double next)
    {
        int stats[2] = { -1, -1 };
        int n = pairStats(s, stats);
        Lock l;
        WeaponSeen& w = g_seen[c];
        for (int i = 0; i < 2; ++i)
        {
            w.stat[i] = i < n ? stats[i] : -1;
            w.lvl[i] = i < n ? st->getStatRef((StatsEnumerated)stats[i]) : 0.0f;
        }
        if (next >= 0.0)
            w.next = next;
    }

    void (*xpMelee_orig)(CharStats*, CharStats::DeadTimeState, Character*, const Damages&) = NULL;
    void xpMelee_hook(CharStats* self, CharStats::DeadTimeState what, Character* target, const Damages& damage)
    {
        diag::HookCall hc(diag::HK_XP_MELEE);
        Character* me = self != NULL ? self->me : NULL;
        if (!g_enabled || me == NULL || target == NULL || self->pCurrentWeaponSkill == NULL)
        {
            xpMelee_orig(self, what, target, damage);
            return;
        }
        stance::Info s = stance::evaluate(me, NULL);
        bool leftHanded = !s.dual && lefty::active(me);
        if (!s.dual && !leftHanded)
        {
            xpMelee_orig(self, what, target, damage);
            return;
        }
        hc.ours();
        // Off-hand exchange: an off-hand impact by me, or an off-hand parry of mine.
        bool offAttack = (what == CharStats::ATTACK_HIT || what == CharStats::ATTACK_WAS_BLOCKED)
            && t_impactAttacker == me && t_impactOff;
        if (offAttack)
        {
            CombatClass* cc = me->getCombatClass();
            if (cc == NULL || cc->currentTechnique != t_impactTech)
                offAttack = false;
        }
        bool offParry = what == CharStats::BLOCKED_IT && defense::offParried(me);
        float* saved = self->pCurrentWeaponSkill;
        // Check that getStatRef names the same field setWeapon points at.
        if (s.main != NULL && stance::statFor(s.main->category) != STAT_NONE)
            diag::count(&self->getStatRef(stance::statFor(s.main->category)) == saved ? "xp_ptr_match" : "xp_ptr_mismatch");
        float* offSkill = NULL;
        if ((offAttack || offParry) && s.off != NULL && s.main != NULL && s.off->category != s.main->category)
        {
            StatsEnumerated st = stance::statFor(s.off->category);
            if (st != STAT_NONE)
                offSkill = &self->getStatRef(st);
        }
        if (offSkill != NULL)
            diag::count(offAttack ? "xp_off_attack" : "xp_off_parry");
        else if (offAttack || offParry)
            diag::count("xp_off_same_skill");
        bool hitTaken = what == CharStats::WAS_HIT;
        const float weaponLevel = offSkill != NULL ? *offSkill : *saved;     // the level the game's XP was scaled by
        {
            SkillRedirect redirect(self, offSkill, hitTaken ? &self->getStatRef(STAT_MELEE_DEFENCE) : NULL);
            xpMelee_orig(self, what, target, damage);
        }
        if (s.dual)
            markSeen(me, self, s, -1.0);        // this gain is handled here, not by watchWeaponXp
        // What was done decides how much it trains.
        float base = cfg::f(cfg::AMB_XP_SHARE) * t_capAmount;
        // The weapon XP the game gave is scaled by the weapon skill against the opponent
        // (getSkillDifferenceRatio, 0.1 at the floor), so once the weapon skill outgrows the opponents Dual
        // Wielding would stall with it, however low it is. It is rebased onto Dual Wielding's own level
        // against the same opponent skill (setting nitoryu dual wielding xp own level).
        if (!hitTaken && base > 0.0f && cfg::b(cfg::AMB_XP_OWN_RATIO))
        {
            CharStats* ts = target->getStats();
            if (ts != NULL)
            {
                float his = what == CharStats::BLOCKED_IT ? ts->getMeleeAttack() : ts->getMeleeDefence(false);
                float rw = self->getSkillDifferenceRatio(weaponLevel, his);
                float ra = self->getSkillDifferenceRatio(ambidex::stored(me), his);
                if (rw > 0.0f && finite(ra / rw))
                {
                    base *= ra / rw;
                    diag::count(ra > rw ? "ambi_xp_own_level_up" : ra < rw ? "ambi_xp_own_level_down" : "ambi_xp_own_level_same");
                }
            }
        }
        float deed = 1.0f;
        const char* deedName = "";
        if (hitTaken)
        {
            // The game's defence XP of a hit already carries the skill difference: a stronger attacker teaches more.
            if (!(t_capDefenceAmount > 0.0f))
                diag::count("ambi_hit_taken_no_defence_xp");
            base = atLeast0(cfg::f(cfg::AMB_XP_HIT_TAKEN)) * t_capDefenceAmount;
            deedName = "ambi_xp_hit_taken";
        }
        else if (offParry)
        {
            deed = atLeast0(cfg::f(cfg::AMB_XP_PARRY));
            deedName = "ambi_xp_parry";
        }
        else if ((what == CharStats::ATTACK_HIT || what == CharStats::ATTACK_WAS_BLOCKED) && t_impactAttacker == me)
        {
            CombatClass* cc = me->getCombatClass();
            if (cc != NULL && cc->currentTechnique == t_impactTech)
            {
                float twin = what == CharStats::ATTACK_HIT && combat::isTwinTechnique(t_impactTech)
                    ? atLeast0(cfg::f(cfg::AMB_XP_TWIN)) : 1.0f;
                float riposte = combat::isRiposte(me, t_impactTech) ? atLeast0(cfg::f(cfg::AMB_XP_RIPOSTE)) : 1.0f;
                if (riposte != 1.0f && riposte >= twin)
                {
                    deed = riposte;
                    deedName = "ambi_xp_riposte";
                }
                else if (twin != 1.0f)
                {
                    deed = twin;
                    deedName = "ambi_xp_twin";
                }
            }
        }
        if (!(base > 0.0f) || !finite(base))
            return;
        bool victim = hitTaken || what == CharStats::BLOCKED_IT;
        bool many = outnumbered(me, victim ? target : NULL, defense::clockMs());
        if (many)
            deed *= atLeast0(cfg::f(cfg::AMB_XP_OUTNUMBERED));
        if (deedName[0] != 0)
            diag::count(deedName);
        if (many)
            diag::count("ambi_xp_outnumbered");

        float sMin = s.skillMain < s.skillOff ? s.skillMain : s.skillOff;
        float byWeapon = weaponXp(s.main) * weaponXp(s.off);
        if (leftHanded)
        {
            Weapon* w = me->getCurrentWeapon();
            StatsEnumerated st = w != NULL ? stance::statFor(w->category) : STAT_NONE;
            sMin = st != STAT_NONE ? self->getStat(st, false) : 0.0f;
            byWeapon = weaponXp(w);
            diag::count("ambi_grow_lefty");
        }
        if (byWeapon != 1.0f)
            diag::count("ambi_xp_weapon_factor");
        // Grow the character's own A, never the [Test] override (which would be saved as the real value).
        float a = ambidex::stored(me);
        float k = 1.0f + (sMin - a) / 50.0f;
        k = k < 0.25f ? 0.25f : (k > 2.0f ? 2.0f : k);
        float next = a;
        increase_orig(next, base * k * deed * byWeapon, t_capLimit);
        if (!finite(next) || next < a)
            return;
        store(me, next);
        diag::count("ambi_grow");
        if (me->isPlayerCharacter() && takeDetail())
            kit::Diag("ambi").kv("who", me->displayName).kv("state", (int)what).kv("xp_amount", base)
                .kv("deed", deedName).kv("deed_mult", deed).kv("outnumbered", many).kv("weapon_mult", byWeapon)
                .kv("a_before", a).kv("a_after", next).kv("skill_min", sMin).kv("limit", t_capLimit)
                .kv("off_xp", offSkill != NULL).emit();
    }

    // ---- the stats line ----------------------------------------------------------

    // Hover: vanilla fills its description panels from a MyGUI event on its own
    // lines (CharacterStatsWindow::statMouseOver, keyed by currentStatIdDescription,
    // with the "Next Level:" bar refreshed by update() while statProgress != 0).
    // Our line has no such event, so update() hit-tests the mouse and fills the
    // same panels the same way; any vanilla line hovered afterwards differs from
    // our key, so vanilla repopulates as usual.
    bool mouseOver(DataPanelLine* line)
    {
        if (line == NULL)
            return false;
        MyGUI::IntPoint m = MyGUI::InputManager::getInstance().getMousePosition();
        MyGUI::Widget* ws[2] = { line->w1, line->w2 };
        for (int i = 0; i < 2; ++i)
            if (ws[i] != NULL && ws[i]->getInheritedVisible() && ws[i]->getAbsoluteRect().inside(m))
                return true;
        return false;
    }

    // The game's "Next Level:" line (a DPL_RESEARCH line from the float setLine, keyed "#767676" + the
    // translated label) is reused when the panel still holds it, and made by us only when there is none, in
    // the game's colour.
    // The game's bar line has 4 widgets: the frame, the fill (inset 7 left, 3 top, 9 shorter than the frame),
    // two text boxes. Our description panel scrolls, and its layout runs DataPanelLine::resize (the bar line
    // has none of its own), which stretches every widget of the line to the line's height: the fill would go
    // out of its frame. After the original, our bar line gets its fill back where the game's own code puts it.
    DataPanelLine* g_barLine = NULL;       // compared only; set by setProgress

    void fixBarFill(DataPanelLine* l)
    {
        if (l == NULL || l->widgets.size() < 2 || l->widgets[0] == NULL || l->widgets[1] == NULL)
            return;
        MyGUI::IntCoord frame = l->widgets[0]->getCoord();
        MyGUI::IntCoord fill = l->widgets[1]->getCoord();
        int top = frame.top + 3, height = frame.height - 9 > 1 ? frame.height - 9 : 1;
        if (fill.top != top || fill.height != height)
            l->widgets[1]->setCoord(fill.left, top, fill.width, height);
    }

    void (*lineResize_orig)(DataPanelLine*, int, int) = NULL;
    void lineResize_hook(DataPanelLine* self, int width, int height)
    {
        diag::HookCall hc(diag::HK_LINE_RESIZE);
        lineResize_orig(self, width, height);
        if (self != NULL && self == g_barLine)
        {
            hc.ours();
            fixBarFill(self);
        }
    }

    const std::string NEXT_LEVEL_COLOUR = "#767676";    // the game's, seen on its own line
    void setProgress(DatapanelGUI* right, float a)
    {
        float frac = a - (float)(int)a;
        char pct[16];
        sprintf_s(pct, "%d%%", (int)(frac * 100.0f));
        int cat = right->getCurrentCategory();
        std::string key;
        int n = right->getNumLines(cat);
        for (int i = 0; i < n; ++i)
        {
            DataPanelLine* l = right->getLineByNum(cat, i);
            if (l == NULL || l->getType() != DataPanelLine::DPL_RESEARCH)   // the float setLine makes a bar line of this type
                continue;
            if (key.empty())
                key = l->keyValue;
            else if (l->keyValue != key)
            {
                right->removeLine(l->keyValue, cat);       // a second bar (ours from before): one stays
                break;
            }
        }
        if (key.empty())
            key = NEXT_LEVEL_COLOUR + g_text.nextLevel;
        g_barLine = right->setLine(key, NEXT_LEVEL_COLOUR + pct, frac, cat);
        fixBarFill(g_barLine);
    }

    // A refill while hovered rebuilding both panels would make the game's bar anew 5 times a second (it
    // flickers). `rightToo` false (a refill) rebuilds only the left panel, the one that follows the gear;
    // the right one (texts, the bar) is built once per hover and its bar only updated.
    const char* const TITLE_SKIN = "Kenshi_WordWrapEmpty_StandardText_Large";

    // The colour tag ("#rrggbb") the game put on a group header of the skills column ("-Weapons-"), so our
    // heading has the colour of the game's, whatever recolour mod is loaded; "" when none is found.
    std::string titleColour(CharacterStatsWindow* win)
    {
        DatapanelGUI* panel = win != NULL ? win->skills1Datapanel : NULL;
        if (panel == NULL)
            return "";
        const int cat = panel->getCurrentCategory();
        const int n = panel->getNumLines(cat);
        for (int i = 0; i < n && i < 64; ++i)
        {
            DataPanelLine* l = panel->getLineByNum(cat, i);
            if (l == NULL || l->w1 == NULL || l->skinW1 != TITLE_SKIN)
                continue;
            const std::string cap = l->w1->getCaption().asUTF8();
            if (cap.size() > 7 && cap[0] == '#' && cap[1] != '#')
                return cap.substr(0, 7);
        }
        return "";
    }

    void fillDescription(CharacterStatsWindow* win, float a, bool rightToo)
    {
        DatapanelGUI* left = win->description1Datapanel;
        DatapanelGUI* right = win->description2Datapanel;
        if (left == NULL || right == NULL)
            return;
        win->currentStatIdDescription = UI_KEY;
        win->statProgress = STAT_NONE;          // vanilla update() must not redraw a stale bar
        left->clear();
        if (rightToo)
            right->clear();
        int lc = left->getCurrentCategory();
        // The skill's name is a heading like the game's: the large skin of the window's group headers, in
        // their colour.
        left->setLine(titleColour(win) + g_label, "", TITLE_SKIN, TITLE_SKIN, lc);
        left->setLine(heading(g_text.affectsTitle), "", lc, false, true);
        left->setLine(g_text.affects, "", lc, false, true);
        left->setLine(heading(g_text.trainTitle), "", lc, false, true);
        left->setLine(g_text.train, "", lc, false, true);
        // What counts in a fight now, why, and the Path (DP).
        ambidex::Effective e = ambidex::effective(win->character);
        char v[64];
        sprintf_s(v, "%d", (int)(e.eff + 0.0001f));
        left->setLine(g_text.inFight, v, lc, false, true);
        if (e.overloadMult < 0.999f)
        {
            sprintf_s(v, "%d%%: x%.2f", (int)(e.overload * 100.0f + 0.5f), e.overloadMult);
            left->setLine(g_text.overloaded, v, lc, false, true);
        }
        if (e.armourMult < 0.999f)
        {
            sprintf_s(v, "x%.2f", e.armourMult);
            left->setLine(g_text.heavyArmour, v, lc, false, true);
        }
        // The tier after the Dual Wielding gate, and what the next one needs.
        stance::Preview p = stance::preview(win->character);
        std::string prof = rankLabel(p.tier, (int)(p.dp + 0.0001f));
        if (p.nextAmbi > 0.0f)
        {
            std::string capped = g_text.tierCapped;     // "{amb} Dual Wielding for {next}"
            char n[16];
            sprintf_s(n, "%d", (int)p.nextAmbi);
            replaceAll(capped, "{amb}", n);
            replaceAll(capped, "{next}", rankName(p.tier + 1));
            prof += "; " + capped;
        }
        left->setLine(g_text.proficiency, (p.tier >= 1 ? rankColour(p.tier) : std::string()) + prof, lc, false, true);
        // The dual speed factor on attacks and blocks (before the caps).
        sprintf_s(v, "x%.2f", p.speedFactor);
        left->setLine(g_text.speed, v, lc, false, true);
        // Dexterity counts more in a fight with two blades (damage and speed).
        if (balance::dexBonus() > 0.0f)
        {
            sprintf_s(v, "+%d%%", (int)(balance::dexBonus() * 100.0f + 0.5f));
            left->setLine(g_text.dexBonus, v, lc, false, true);
        }
        // What the carried pair gives (long + short, equal blades, two weapon types).
        Weapon* pa = NULL;
        Weapon* pb = NULL;
        if (stance::carriedPair(win->character, pa, pb))
        {
            std::string what;
            blades::Pair kind = blades::pairOf(pa, pb);
            if (kind != blades::PAIR_PLAIN)
            {
                sprintf_s(v, "x%.2f", kind == blades::PAIR_LONG_SHORT ? blades::guardMult(pa, pb) : blades::twinMult(pa, pb));
                what = kind == blades::PAIR_LONG_SHORT ? g_text.pairLongShort : g_text.pairEqual;
                replaceAll(what, "{x}", v);
            }
            // one bonus per line (both on one line wrap over the label)
            if (!what.empty())
                left->setLine(g_text.pair, what, lc, false, true);
            // only when the second blade (the one not in hand; sheathed, the game draws pa) has a bonus
            bool paSecond = win->character->getCurrentWeapon() == pb;
            if (blades::mixedDefence(paSecond ? pb : pa, paSecond ? pa : pb))
                left->setLine(what.empty() ? g_text.pair : std::string(" "), g_text.pairMixed, lc, false, true);
        }
        // The five ranks with what each opens; reached ones in their colour, the others greyed with what
        // they need.
        left->setLine(" ", "", lc, false, true);
        left->setLine(heading(g_text.ranksTitle), "", lc, false, true);
        for (int n = 1; n <= tuning::TIERS; ++n)
        {
            std::string line = rankColour(p.tier >= n ? n : 0) + rankName(n);
            if (p.tier < n)
            {
                float needAmb = cfg::item(cfg::TIER_MIN_AMBI, n - 1);
                std::string needs = needAmb > 0.0f ? g_text.rankNeeds : g_text.rankNeedsDp;
                char num[16];
                sprintf_s(num, "%d", (int)(cfg::item(cfg::TIER_DP, n - 1) + 0.5f));
                replaceAll(needs, "{dp}", num);
                sprintf_s(num, "%d", (int)(needAmb + 0.5f));
                replaceAll(needs, "{amb}", num);
                line += " (" + needs + ")";
            }
            left->setLine(line + ": " + g_text.rankOpens[n], "", lc, false, true);
        }
        setProgress(right, a);
        if (!rightToo)
            return;
        int rc = right->getCurrentCategory();
        const char* const wrap = "Kenshi_WordWrapEmpty";
        int gaps = 0;
        setGap(right, rc, gaps);
        right->setLine(heading(g_text.descTitle), "", rc, false, true);
        right->setLine(g_text.desc, "", wrap, wrap, rc);
        // How the Path is counted, with a note that it is an effective number. The weights are the
        // settings', so a mod that changes them changes the text.
        {
            std::string f = g_text.pathFormula;
            replaceAll(f, "{skill}", number(cfg::f(cfg::W_SKILL) * 100.0f));
            replaceAll(f, "{dex}", number(cfg::f(cfg::W_DEX) * 100.0f));
            replaceAll(f, "{atk}", number(cfg::f(cfg::W_ATTACK) * 100.0f));
            replaceAll(f, "{def}", number(cfg::f(cfg::W_DEFENCE) * 100.0f));
            replaceAll(f, "{dw}", number(cfg::f(cfg::W_AMBI) * 100.0f));
            setGap(right, rc, gaps);
            right->setLine(heading(g_text.pathTitle), "", rc, false, true);
            setBlock(right, rc, f, NULL);
            right->setLine(g_text.pathNote, "", wrap, wrap, rc);
        }
        // What the marks mean: one paragraph per mark whose mechanic and mark are on, with the settings'
        // numbers.
        const bool mPressure = cfg::b(cfg::HUD_PRESSURE) && cfg::b(cfg::PRESSURE_ON);
        const bool mFlow = cfg::b(cfg::HUD_FLOW) && cfg::b(cfg::FLOW_ON);
        const bool mMugen = cfg::b(cfg::HUD_MUGEN) && cfg::b(cfg::MUGEN_ON);
        if (cfg::i(cfg::HUD_MARKS) != 0 && (mPressure || mFlow || mMugen))
        {
            setGap(right, rc, gaps);
            right->setLine(heading(g_text.marksTitle), "", rc, false, true);
            if (mPressure)
            {
                std::string t = g_text.markPressure;
                replaceAll(t, "{max}", number(cfg::f(cfg::PRESSURE_MAX_STACKS)));
                setBlock(right, rc, t, headColour(2));
            }
            if (mFlow)
            {
                std::string t = g_text.markFlow;
                replaceAll(t, "{attacks}", number(cfg::f(cfg::FLOW_ATTACKS)));
                replaceAll(t, "{window}", number(cfg::f(cfg::FLOW_WINDOW_MS) / 1000.0f));
                replaceAll(t, "{duration}", number(cfg::f(cfg::FLOW_DURATION_MS) / 1000.0f));
                setBlock(right, rc, t, headColour(2));
            }
            if (mMugen)
                setBlock(right, rc, g_text.markMugen, headColour(2));
        }
        diag::count("ambi_description");
    }

    // The line can sit in the Combat group ([Stats] group = 2). The window's second column holds two groups, Combat and Ranged
    // (CharacterStatsWindow::setupStats: a header is a line with the large skin and no value, a skill is a
    // line with a number), and a new line lands under the last one. So the line is added there, the Ranged
    // header and everything under it move down one row, and ours takes the row after the last Combat skill.
    // With one group in the column (a layout another mod changed) nothing moves: the bottom is the end of
    // the group.
    const char* const HEADER_SKIN = "Kenshi_WordWrapEmpty_StandardText_Large";

    bool topOf(DataPanelLine* l, int& top)
    {
        MyGUI::Widget* w = l->w1 != NULL ? (MyGUI::Widget*)l->w1 : (MyGUI::Widget*)l->w2;
        if (w == NULL)
            return false;
        top = w->getTop();
        return true;
    }

    bool isHeader(DataPanelLine* l)
    {
        return l->skinW1 == HEADER_SKIN || l->w2 == NULL || l->w2->getCaption().empty();
    }

    void moveLine(DataPanelLine* l, int dy)
    {
        MyGUI::Widget* own[2] = { l->w1, l->w2 };
        for (int i = 0; i < 2; ++i)
            if (own[i] != NULL)
                own[i]->setPosition(own[i]->getLeft(), own[i]->getTop() + dy);
        for (size_t i = 0; i < l->widgets.size(); ++i)
        {
            MyGUI::Widget* w = l->widgets[i];
            if (w != NULL && w != own[0] && w != own[1])
                w->setPosition(w->getLeft(), w->getTop() + dy);
        }
    }

    // Returns what it did, for the log: "moved", "placed" (already there), "one_group", "no_rows".
    const char* placeAfterFirstGroup(DatapanelGUI* panel, int cat, DataPanelLine* ours)
    {
        int oursTop = 0;
        if (ours == NULL || !topOf(ours, oursTop))
            return "no_rows";
        const int n = panel->getNumLines(cat);
        int firstHeader = INT_MAX;      // the column's own header
        for (int i = 0; i < n; ++i)
        {
            DataPanelLine* l = panel->getLineByNum(cat, i);
            int t = 0;
            if (l != NULL && l != ours && topOf(l, t) && isHeader(l) && t < firstHeader)
                firstHeader = t;
        }
        int nextHeader = INT_MAX;       // the second group's header
        int bottom = INT_MIN;           // the lowest row that is not ours
        DataPanelLine* bottomLine = NULL;
        for (int i = 0; i < n; ++i)
        {
            DataPanelLine* l = panel->getLineByNum(cat, i);
            int t = 0;
            if (l == NULL || l == ours || !topOf(l, t))
                continue;
            if (isHeader(l) && t > firstHeader && t < nextHeader)
                nextHeader = t;
            if (t > bottom)
            {
                bottom = t;
                bottomLine = l;
            }
        }
        if (nextHeader == INT_MAX || bottomLine == NULL)
            return "one_group";
        if (oursTop < nextHeader)
            return "placed";
        const int pitch = oursTop - bottom;
        if (pitch <= 0)
            return "no_rows";
        int above = INT_MIN;            // the last row of the first group
        for (int i = 0; i < n; ++i)
        {
            DataPanelLine* l = panel->getLineByNum(cat, i);
            int t = 0;
            if (l == NULL || l == ours || !topOf(l, t))
                continue;
            if (t < nextHeader && t > above)
                above = t;
            if (t >= nextHeader)
                moveLine(l, pitch);
        }
        moveLine(ours, (above != INT_MIN ? above + pitch : nextHeader) - oursTop);
        // the panel puts its next line under this one: keep it the lowest row, for a mod that adds a line after us
        if (panel->bottomLine.find(cat) != panel->bottomLine.end())
            panel->bottomLine[cat] = bottomLine;
        return "moved";
    }

    void setStatsLine(CharacterStatsWindow* win)
    {
        if (!g_showLine || win == NULL || win->character == NULL)
            return;
        // 1 = the end of the Weapons group (the default), 2 = the Combat group
        const bool inCombat = cfg::i(cfg::STATS_GROUP) != 1 && win->skills2Datapanel != NULL;
        DatapanelGUI* panel = inCombat ? win->skills2Datapanel : win->skills1Datapanel;
        if (panel == NULL)
            return;
        // Every character has the stat (seeded if it never fought dual), so the
        // line is always the one of the character the window shows now.
        float a = ambidex::get(win->character);
        // A penalty (overload, heavy armour) shows in red after the value.
        ambidex::Effective e = ambidex::effective(win->character);
        int loss = (int)(a + 0.0001f) - (int)(e.eff + 0.0001f);
        char num[48];
        if (loss > 0)
            sprintf_s(num, "%d #FF5040(-%d)", (int)(a + 0.0001f), loss);
        else
            sprintf_s(num, "%d", (int)(a + 0.0001f));
        std::string value(num);
        // The value is the number alone, so it does not shift the column other mods may add to; the
        // rank's name and colour live in the hover.
        int cat = panel->getCurrentCategory();
        DataPanelLine* line = NULL;
        if (panel->lineExists(UI_KEY, cat))
        {
            line = panel->getLine(UI_KEY, cat);
            if (line != NULL && line->w2 != NULL)
                line->w2->setCaption(value);
        }
        else
        {
            line = panel->setLine(UI_KEY, g_label, value, cat, false, true);
            diag::count("ambi_line_added");
            if (inCombat)
            {
                const char* did = placeAfterFirstGroup(panel, cat, line);
                static int logged = 0;
                if (logged < 3 && ++logged)
                    kit::Diag("stats_line").kv("group", "combat").kv("rows", panel->getNumLines(cat))
                        .kv("placed", did).emit();
            }
        }
        bool ours = win->currentStatIdDescription == UI_KEY;
        // Refill while hovered, so the penalty lines follow the gear (5 times a second).
        static DWORD lastFill = 0;
        if (mouseOver(line) && (!ours || GetTickCount() - lastFill > 200))
        {
            fillDescription(win, a, !ours);
            lastFill = GetTickCount();
        }
        else if (ours && win->description2Datapanel != NULL)
            setProgress(win->description2Datapanel, a);     // as vanilla update() does
    }

    // The rank's name in its colour next to the character's name in the stats window, in a small text.
    // The name is the window's caption; ours is a small text box inside the caption widget, right of the
    // name. Shown for a character who carries a pair and has a rank. Found by name every time, never kept;
    // the game owns it with the window.
    const char* const RANK_TAG_NAME = "Nitoryu_rank_tag";
    const char* const RANK_TAG_SKIN = "Kenshi_TextboxStandardText";
    const char* const RANK_TAG_FONT = "Kenshi_StandardFont_Small";
    const int RANK_TAG_GAP = 10;
    bool g_rankTag = false;             // [Stats] rank_in_title (off by default)
    bool g_rankTagFailed = false;

    void setRankTag(CharacterStatsWindow* win)
    {
        if (!g_rankTag || g_rankTagFailed || win == NULL || win->character == NULL || win->mMainWidget == NULL)
            return;
        // MyGUI throws; an exception costs the tag, not the window.
        try
        {
            MyGUI::Window* root = win->mMainWidget->castType<MyGUI::Window>(false);
            MyGUI::TextBox* cap = root != NULL ? root->getCaptionWidget() : NULL;
            if (cap == NULL)
            {
                static bool told = false;
                if (!told)
                    diag::count("rank_tag_no_caption");
                told = true;
                return;
            }
            MyGUI::TextBox* tag = NULL;
            for (size_t i = 0; i < cap->getChildCount() && tag == NULL; ++i)
                if (cap->getChildAt(i)->getName() == RANK_TAG_NAME)
                    tag = cap->getChildAt(i)->castType<MyGUI::TextBox>(false);
            stance::Preview p = stance::preview(win->character);
            const bool show = p.tier >= 1 && stance::hasPair(win->character);
            if (tag == NULL)
            {
                if (!show)
                    return;
                tag = cap->createWidget<MyGUI::TextBox>(RANK_TAG_SKIN, MyGUI::IntCoord(0, 0, 60, cap->getHeight()), MyGUI::Align::Default,
                                                        RANK_TAG_NAME);
                if (tag == NULL)
                    return;
                tag->setNeedMouseFocus(false);      // the caption still drags the window
                tag->setNeedKeyFocus(false);
                tag->setFontName(RANK_TAG_FONT);
                tag->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
                diag::count("rank_tag_created");
            }
            if (!show)
            {
                if (tag->getVisible())
                    tag->setVisible(false);
                return;
            }
            const int tier = p.tier > 5 ? 5 : p.tier;
            const std::string text = rankColour(tier) + rankName(tier);
            if (tag->getCaption().asUTF8() != text)
                tag->setCaption(text);
            // right of the name: the caption's text is centred or starts at the left of its region
            const MyGUI::IntSize name = cap->getTextSize();
            const MyGUI::IntCoord region = cap->getTextRegion();
            const int nameRight = cap->getTextAlign().isHCenter() ? region.left + (region.width + name.width) / 2
                                                                  : region.left + name.width;
            const int w = tag->getTextSize().width * 108 / 100 + 8;     // getTextSize is smaller than the drawn text
            tag->setCoord(nameRight + RANK_TAG_GAP, 0, w, cap->getHeight());
            if (!tag->getVisible())
                tag->setVisible(true);
        }
        catch (...)
        {
            g_rankTagFailed = true;
            diag::count("rank_tag_failed");
        }
    }

    void (*statsWindow_orig)(CharacterStatsWindow*) = NULL;
    void statsWindow_hook(CharacterStatsWindow* self)
    {
        diag::HookCall hc(diag::HK_STATS_WINDOW);
        statsWindow_orig(self);
        if (g_enabled)
        {
            hc.ours();
            setStatsLine(self);
            setRankTag(self);
        }
    }

    bool hook(KenshiLib::HookStatus s, const char* what)
    {
        if (s == KenshiLib::SUCCESS)
            return true;
        kit::log::error(std::string("Nitoryu: could not hook ") + what);
        return false;
    }
}

namespace ambidex
{
    void init()
    {
        InitializeCriticalSection(&g_lock);
        g_detailLeft = diag::budget(DETAIL_BUDGET);
        std::string path = stance::iniFile();
        g_showLine = GetPrivateProfileIntA("Stats", "show", 1, path.c_str()) != 0;
        char ta[32] = { 0 };
        GetPrivateProfileStringA("Test", "ambidexterity", "", ta, sizeof(ta), path.c_str());
        if (_stricmp(ta, "skill") == 0)
            g_testSkill = true;
        else if (ta[0] != 0)
            g_testA = (float)atof(ta);
        // the same for every NPC at once, a test aid ("skill" or a number)
        char tn[32] = { 0 };
        GetPrivateProfileStringA("Test", "npc_ambidexterity", "", tn, sizeof(tn), path.c_str());
        if (_stricmp(tn, "skill") == 0)
            g_testNpcSkill = true;
        else if (tn[0] != 0)
            g_testNpcA = (float)atof(tn);
        // [NPC] skill=<string id>:<value>, CHARACTER or FACTION ids, comma separated
        int bad = cfg::readIdValues(path, "NPC", "skill", g_iniSkill);
        if (!g_iniSkill.empty() || bad > 0 || g_testNpcSkill || g_testNpcA >= 0.0f)
            kit::Diag("npc_skill").kv("from_ini", (int)g_iniSkill.size()).kv("bad_entries", bad)
                .kv("test_npc", g_testNpcSkill ? std::string("skill") : g_testNpcA >= 0.0f ? std::string("set") : std::string())
                .emit();
        // Texts from lang\<code>.ini (Lang.cpp); a value set in Nitoryu.ini [Stats] still wins.
        struct { const char* ini; const char* key; std::string* out; } t[] = {
            { "label", "stats.label", &g_label },
            { "affects_title", "stats.affects_title", &g_text.affectsTitle },
            { "affects", "stats.affects", &g_text.affects },
            { "train_title", "stats.train_title", &g_text.trainTitle },
            { "train", "stats.train", &g_text.train },
            { "description_title", "stats.description_title", &g_text.descTitle },
            { "description", "stats.description", &g_text.desc },
            { "next_level", "stats.next_level", &g_text.nextLevel },
            { "in_fight", "stats.in_fight", &g_text.inFight },
            { "overloaded", "stats.overloaded", &g_text.overloaded },
            { "heavy_armour", "stats.heavy_armour", &g_text.heavyArmour },
            { "proficiency", "stats.proficiency", &g_text.proficiency },
            { "tier", "stats.tier", &g_text.tier },
            { "tier_capped", "stats.tier_capped", &g_text.tierCapped },
            { "speed", "stats.speed", &g_text.speed },
            { "dex_bonus", "stats.dex_bonus", &g_text.dexBonus },
            { "pair", "stats.pair", &g_text.pair },
            { "pair_long_short", "stats.pair_long_short", &g_text.pairLongShort },
            { "pair_equal", "stats.pair_equal", &g_text.pairEqual },
            { "pair_mixed", "stats.pair_mixed", &g_text.pairMixed },
            { "rank1", "stats.rank1", &g_text.rank[1] },
            { "rank2", "stats.rank2", &g_text.rank[2] },
            { "rank3", "stats.rank3", &g_text.rank[3] },
            { "rank4", "stats.rank4", &g_text.rank[4] },
            { "rank5", "stats.rank5", &g_text.rank[5] },
            { "ranks_title", "stats.ranks_title", &g_text.ranksTitle },
            { "rank1_opens", "stats.rank1_opens", &g_text.rankOpens[1] },
            { "rank2_opens", "stats.rank2_opens", &g_text.rankOpens[2] },
            { "rank3_opens", "stats.rank3_opens", &g_text.rankOpens[3] },
            { "rank4_opens", "stats.rank4_opens", &g_text.rankOpens[4] },
            { "rank5_opens", "stats.rank5_opens", &g_text.rankOpens[5] },
            { "rank_needs", "stats.rank_needs", &g_text.rankNeeds },
            { "rank_needs_dp", "stats.rank_needs_dp", &g_text.rankNeedsDp },
            { "rank_up", "stats.rank_up", &g_text.rankUp },
            { "rank_up_opens", "stats.rank_up_opens", &g_text.rankUpOpens },
            { "marks_title", "stats.marks_title", &g_text.marksTitle },
            { "mark_flow", "stats.mark_flow", &g_text.markFlow },
            { "mark_mugen", "stats.mark_mugen", &g_text.markMugen },
            { "mark_pressure", "stats.mark_pressure", &g_text.markPressure },
            { "path_title", "stats.path_title", &g_text.pathTitle },
            { "path_formula", "stats.path_formula", &g_text.pathFormula },
            { "path_note", "stats.path_note", &g_text.pathNote },
        };
        g_coloursOn = GetPrivateProfileIntA("Stats", "rank_colours", 1, path.c_str()) != 0;
        g_rankUpMessage = GetPrivateProfileIntA("Stats", "rank_up_message", 1, path.c_str()) != 0;
        g_rankTag = GetPrivateProfileIntA("Stats", "rank_in_title", 0, path.c_str()) != 0;
        for (int i = 0; i < (int)(sizeof(t) / sizeof(t[0])); ++i)
        {
            char v[512] = { 0 };
            GetPrivateProfileStringA("Stats", t[i].ini, "", v, sizeof(v), path.c_str());
            *t[i].out = v[0] != '\0' ? std::string(v) : lang::text(t[i].key);
        }
    }

    bool installHooks()
    {
        bool ok = true;
        ok &= hook(hooks::add("CharStats::serialise", "ambidex", KenshiLib::GetRealAddress(&CharStats::serialise), serialise_hook, &serialise_orig),
                   "CharStats::serialise");
        ok &= hook(hooks::add("CharStats::updateStats", "ambidex", KenshiLib::GetRealAddress(&CharStats::updateStats), updateStats_hook, &updateStats_orig),
                   "CharStats::updateStats");
        ok &= hook(hooks::add("CharStats::xpMelee", "ambidex", KenshiLib::GetRealAddress(&CharStats::xpMelee), xpMelee_hook, &xpMelee_orig),
                   "CharStats::xpMelee");
        ok &= hook(hooks::add("increaseStat", "ambidex", KenshiLib::GetRealAddress(&increaseStat), increase_hook, &increase_orig),
                   "increaseStat");
        ok &= hook(hooks::add("CharacterStatsWindow::update", "ambidex", KenshiLib::GetRealAddress(&CharacterStatsWindow::_NV_update), statsWindow_hook,
                                      &statsWindow_orig),
                   "CharacterStatsWindow::update");
        // the fill of our Next Level bar (only the look; a failure leaves the bar as it was)
        hook(hooks::add("DataPanelLine::resize", "ambidex", KenshiLib::GetRealAddress(&DataPanelLine::_NV_resize), lineResize_hook, &lineResize_orig),
             "DataPanelLine::resize");
        return ok;
    }

    // Core, not the Dual Wielding group: it clears every per-character record (stance, pressure,
    // Dual Wielding) before the pointer can be re-used.
    bool installLifetimeHook()
    {
        return hook(hooks::add("Character::~Character", "core", KenshiLib::GetRealAddress(&Character::_DESTRUCTOR), charDtor_hook, &charDtor_orig),
                    "Character destructor");
    }

    void setEnabled(bool on) { g_enabled = on; }

    // Main loop, for each drawn dual fighter. A player character whose rank is higher than any it had
    // says the rank's name, and the game shows one message line.
    void noteRank(Character* c)
    {
        if (!g_enabled || c == NULL || !c->isPlayerCharacter())
            return;
        stance::Info s = stance::evaluate(c, NULL);
        if (!s.dual || stance::forcedTier() > 0)
            return;
        int tier = s.tier > 5 ? 5 : s.tier;
        {
            Lock l;
            std::map<Character*, int>::iterator it = g_rankMax.find(c);
            if (it == g_rankMax.end())
            {
                g_rankMax[c] = tier;        // first sight (an older save, a new character): no moment
                return;
            }
            if (tier <= it->second)
                return;
            it->second = tier;
        }
        diag::count("rank_up");
        std::string name = rankName(tier);
        kit::Diag("rank_up").kv("who", c->displayName).kv("rank", tier).kv("name", name).kv("dp", s.dp).emit();
        if (!g_rankUpMessage)
            return;
        feedback::announce(c, (name + ".").c_str());
        std::string msg = g_text.rankUp;        // "{who} has reached the rank of {rank}."
        replaceAll(msg, "{who}", c->displayName);
        replaceAll(msg, "{rank}", name);
        if (ou != NULL && !msg.empty())
            ou->showPlayerAMessage(msg, true);
        // A second line with what the rank opens, the text the stats hover lists for it (not in the
        // rank's colour).
        std::string opens = g_text.rankUpOpens;     // "{rank} opens: {opens}."
        const std::string& what = g_text.rankOpens[tier];
        if (ou != NULL && !opens.empty() && !what.empty())
        {
            replaceAll(opens, "{who}", c->displayName);
            replaceAll(opens, "{rank}", name);
            replaceAll(opens, "{opens}", what);
            ou->showPlayerAMessage(opens, true);
            diag::count("rank_up_opens_shown");
        }
    }

    std::string testText()
    {
        if (g_testSkill)
            return "skill";
        if (g_testA < 0.0f)
            return "";
        char buf[32];
        sprintf_s(buf, "%.0f", g_testA);
        return buf;
    }

    void setListSkills(const std::map<std::string, float>& byRecord)
    {
        Lock l;
        g_listSkill = byRecord;
    }

    std::string testNpcText()
    {
        if (g_testNpcSkill)
            return "skill";
        if (g_testNpcA < 0.0f)
            return "";
        char buf[32];
        sprintf_s(buf, "%.0f", g_testNpcA);
        return buf;
    }

    float get(Character* c)
    {
        if (c == NULL)
            return 0.0f;
        // [Test] ambidexterity: a fixed value for the player's characters, for tests (other tools
        // cannot edit this stat). Never saved: the stored value is kept. `skill` = the character's weapon
        // skill, so an equal-stats test measures DP = the stats.
        if (g_testSkill && c->isPlayerCharacter())
            return bestSkill(c);
        if (g_testA >= 0.0f && c->isPlayerCharacter())
            return g_testA;
        // [Test] npc_ambidexterity, the same for everyone who is not the player's. Never saved.
        if ((g_testNpcSkill || g_testNpcA >= 0.0f) && !c->isPlayerCharacter())
        {
            diag::count("ambi_test_npc");
            return g_testNpcSkill ? bestSkill(c) : g_testNpcA;
        }
        return stored(c);
    }

    void setStored(Character* c, float a)
    {
        if (c != NULL && finite(a))
            store(c, a);
    }

    // Main loop, per dual fighter (see WeaponSeen above). Each SEEN_STEP_MS, what the pair's weapon skills
    // gained beyond the mark (that is, outside our xpMelee_hook: another plugin's XP code, a trainer) is turned
    // back into the XP that made it (increaseStat's curve, limit 101) and trains Dual Wielding by AMB_XP_SHARE
    // of it, with the same pull toward the lower weapon skill as xpMelee_hook.
    void watchWeaponXp(Character* c)
    {
        if (!g_enabled || c == NULL)
            return;
        CharStats* st = c->getStats();
        if (st == NULL)
            return;
        stance::Info s = stance::evaluate(c, NULL);
        if (!s.dual)
            return;
        const double now = defense::clockMs();
        int stats[2] = { -1, -1 };
        const int n = pairStats(s, stats);
        float xp = 0.0f, sMin = 1000.0f;
        {
            Lock l;
            std::map<Character*, WeaponSeen>::iterator it = g_seen.find(c);
            if (it == g_seen.end())
            {
                WeaponSeen w;
                for (int i = 0; i < 2; ++i)
                {
                    w.stat[i] = i < n ? stats[i] : -1;
                    w.lvl[i] = i < n ? st->getStatRef((StatsEnumerated)stats[i]) : 0.0f;
                }
                w.next = now + SEEN_STEP_MS;
                g_seen[c] = w;
                return;
            }
            WeaponSeen& w = it->second;
            if (now < w.next && w.next - now <= SEEN_STEP_MS * 2.0)
                return;                         // not yet (a clock that went back starts over)
            w.next = now + SEEN_STEP_MS;
            for (int i = 0; i < n; ++i)
            {
                const float cur = st->getStatRef((StatsEnumerated)stats[i]);
                sMin = std::min(sMin, cur);
                for (int j = 0; j < 2; ++j)
                {
                    if (w.stat[j] != stats[i] || !(cur > w.lvl[j]) || !finite(cur))
                        continue;
                    const float f = (101.0f - w.lvl[j]) / 101.0f;
                    if (f > 0.01f)
                        xp += (cur - w.lvl[j]) / (f * f);
                }
            }
            for (int i = 0; i < 2; ++i)
            {
                w.stat[i] = i < n ? stats[i] : -1;
                w.lvl[i] = i < n ? st->getStatRef((StatsEnumerated)stats[i]) : 0.0f;
            }
        }
        if (!(xp > 0.0f) || !finite(xp))
            return;
        float a = stored(c);
        float k = 1.0f + (sMin - a) / 50.0f;
        k = k < 0.25f ? 0.25f : (k > 2.0f ? 2.0f : k);
        float next = a;
        increase_orig(next, cfg::f(cfg::AMB_XP_SHARE) * xp * k, 101.0f);
        if (!finite(next) || !(next > a))
            return;
        store(c, next);
        diag::count("ambi_grow_watch");
        if (c->isPlayerCharacter() && takeDetail())
            kit::Diag("ambi_watch").kv("who", c->displayName).kv("weapon_xp", xp).kv("k", k).kv("a_before", a)
                .kv("a_after", next).kv("skill_min", sMin).emit();
    }

    float stored(Character* c)
    {
        if (c == NULL)
            return 0.0f;
        // A value set for the character (field or ini) is a floor: a new character starts at it, one from
        // an older save is raised to it, and both grow past it like anyone else.
        float a = 0.0f;
        bool has = lookup(c, a);
        const char* from = "";
        float floorA = authored(c, from);
        if (has && !(floorA > a))
            return a;
        float before = has ? a : seedFor(c);
        a = floorA > before ? floorA : before;
        {
            Lock l;
            std::map<Character*, float>::iterator it = g_amb.find(c);
            if (it != g_amb.end() && !(a > it->second))
                return it->second;
            g_amb[c] = a;
        }
        if (floorA > before)
        {
            diag::count(from[0] == 'i' ? "ambi_authored_ini" : from[0] == 'l' ? "ambi_authored_list" : "ambi_authored_field");
            if (takeDetail())
                kit::Diag("ambi_authored").kv("who", c->displayName).kv("from", from).kv("value", floorA)
                    .kv("was", before).kv("record", kit::recordId(c->getGameData())).emit();
        }
        return a;
    }

    // Armour cuts A per worn piece, by the ARMOUR record's FCS `class` (ArmourClass: 0 cloth, 1 light,
    // 2 medium, 3 heavy); the cuts add up. The record's `nitoryu dual wielding cut` (-1 = by class) sets one
    // piece's own. The worn pieces are the items of the equipped sections (not a container slot);
    // Inventory::getEquippedArmour is not used: the lektor it fills has no destructor, so every call would
    // leave a buffer behind, and this runs every frame of a fight.
    float armourCut(Character* c, int& pieces)
    {
        pieces = 0;
        Inventory* inv = c != NULL ? c->getInventory() : NULL;
        if (inv == NULL)
            return 0.0f;
        const float heavy = cfg::f(cfg::ARMOUR_HEAVY_CUT), medium = cfg::f(cfg::ARMOUR_MEDIUM_CUT);
        float cut = 0.0f;
        lektor<InventorySection*>& secs = inv->getAllSections();
        for (uint32_t i = 0; i < secs.size(); ++i)
        {
            InventorySection* s = secs[i];
            if (s == NULL || !s->isAnEquippedItemSection || s->containerSlot)
                continue;
            const Ogre::vector<InventorySection::SectionItem>::type& items = s->getItems();
            for (size_t k = 0; k < items.size(); ++k)
            {
                Item* it = items[k].item;
                if (it == NULL || it->isArmour() == NULL || it->data == NULL)
                    continue;
                float own = kit::fcsFloat(it->data, "nitoryu dual wielding cut", -1.0f);
                float pc = own >= 0.0f ? own : 0.0f;
                if (own < 0.0f)
                {
                    int cls = kit::fcsInt(it->data, "class", 0);
                    pc = cls == 3 ? heavy : cls == 2 ? medium : 0.0f;
                }
                if (pc > 0.0f)
                {
                    cut += pc;
                    ++pieces;
                }
            }
        }
        return cut;
    }

    // encumbranceMult m = clamp((cap - 0.05 carry) / (0.95 carry), 0, 1)
    // (CharStats::_calculateEncumberanceMult), so while m < 1 the
    // overload is carry / cap - 1 = 1 / (0.05 + 0.95 m) - 1.
    Effective effective(Character* c)
    {
        Effective e;
        e.a = get(c);
        e.eff = e.a;
        e.overload = 0.0f;
        e.overloadMult = 1.0f;
        e.armourMult = 1.0f;
        e.armourPieces = 0;
        CharStats* st = c != NULL ? c->getStats() : NULL;
        if (st == NULL)
            return e;
        // The factors are core's (core/Math.h); a non-finite game value leaves them at 1.
        e.overload = core::overloadOf(st->encumbranceMult);
        if (e.overload != 0.0f)
            e.overloadMult = core::overloadFactor(e.overload, cfg::f(cfg::OVERLOAD_FLOOR));
        e.armourMult = core::armourFactor(armourCut(c, e.armourPieces), cfg::f(cfg::ARMOUR_FLOOR));
        e.eff = e.a * e.overloadMult * e.armourMult;
        return e;
    }

    void noteImpact(Character* attacker, CombatTechniqueData* attack, bool offHand)
    {
        t_impactAttacker = attacker;
        t_impactTech = attack;
        t_impactOff = offHand;
    }
}
