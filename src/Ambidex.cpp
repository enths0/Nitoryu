#include "kit/input.h"

#include <Debug.h>
#include <core/Functions.h>

#include <kenshi/Kenshi.h>
#include <kenshi/Globals.h>
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/Damages.h>
#include <kenshi/Gear.h>
#include <kenshi/GameData.h>
#include <kenshi/gui/CharacterStatsWindow.h>
#include <kenshi/gui/DatapanelGUI.h>
#include <kenshi/gui/DataPanelLine.h>
#include <kenshi/combat/CombatClass.h>
#include <kenshi/combat/CombatTechniqueData.h>

#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Widget.h>
#include <mygui/MyGUI_InputManager.h>
#include <OgreResourceGroupManager.h>
#include <OgreDataStream.h>
#include <kenshi/GameWorld.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

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

namespace
{
    const char* const SAVE_KEY = "NIT_ambidexterity";
    const char* const DUAL_OFF_KEY = "NIT_dual_off";
    const char* const RANK_KEY = "NIT_rank";
    const char* const UI_KEY = "nitoryu_ambidexterity";
    const int DETAIL_BUDGET = 60;

    volatile bool g_enabled = true;
    bool g_showLine = true;
    float g_testA = -1.0f;
    bool g_testSkill = false;
    std::string g_label = "Dual Wielding";

    struct Texts
    {
        std::string affectsTitle, affects, trainTitle, train, descTitle, desc, nextLevel;
        std::string inFight, overloaded, heavyArmour, proficiency, tier;
        std::string tierCapped, speed;
        std::string dexBonus;
        std::string pair, pairLongShort, pairEqual, pairMixed;
        std::string rank[6];
        std::string ranksTitle, rankOpens[6], rankNeeds, rankNeedsDp;
        std::string rankUp;
    };
    Texts g_text;

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

    void replaceAll(std::string& s, const char* what, const std::string& with)
    {
        size_t n = strlen(what);
        for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + with.size()))
            s.replace(p, n, with);
    }

    const char* const RANK_COLOUR_KEY[6] = { "Greyed", "ItemGrade1", "ItemGrade2", "ItemGrade3", "ItemGrade4", "ItemGrade5" };
    const char* const RANK_COLOUR_DEFAULT[6] = { "#444444", "#d9ccbf", "#4099b3", "#66fc80", "#fceb0d", "#e60000" };
    std::string g_rankColour[6];
    bool g_coloursRead = false;
    bool g_coloursOn = true;
    bool g_rankUpMessage = true;

    bool hexDigit(char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'); }

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

    void readColours()
    {
        if (g_coloursRead)
            return;
        g_coloursRead = true;
        for (int i = 0; i < 6; ++i)
            g_rankColour[i] = RANK_COLOUR_DEFAULT[i];
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
        kit::Diag("rank_colours").kv("source", source).kv("keys_found", found).kv("locked", g_rankColour[0])
            .kv("r1", g_rankColour[1]).kv("r2", g_rankColour[2]).kv("r3", g_rankColour[3]).kv("r4", g_rankColour[4])
            .kv("r5", g_rankColour[5]).emit();
    }

    std::string rankColour(int tier)
    {
        if (!g_coloursOn)
            return "";
        readColours();
        return g_rankColour[tier >= 1 && tier <= 5 ? tier : 0];
    }

    CRITICAL_SECTION g_lock;
    std::map<Character*, float> g_amb;

    std::map<Character*, int> g_rankMax;
    int g_detailLeft = DETAIL_BUDGET;

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

    void store(Character* c, float a)
    {
        Lock l;
        g_amb[c] = a < 0.0f ? 0.0f : a;
    }

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

    bool takeDetail()
    {
        Lock l;
        if (g_detailLeft <= 0)
            return false;
        --g_detailLeft;
        return true;
    }

    __declspec(thread) Character* t_impactAttacker = NULL;
    __declspec(thread) CombatTechniqueData* t_impactTech = NULL;
    __declspec(thread) bool t_impactOff = false;

    void (*serialise_orig)(CharStats*, GameData*) = NULL;
    void serialise_hook(CharStats* self, GameData* data)
    {
        serialise_orig(self, data);
        if (!g_enabled || self == NULL || self->me == NULL || data == NULL)
            return;
        float a = 0.0f;
        if (lookup(self->me, a) && finite(a))
        {

            std::string key(SAVE_KEY);
            if (data->fdata.find(key) != data->fdata.end())
                data->fdata.find(key)->second = a;
            else
                data->fdata.insert(std::make_pair(key, a));
            diag::count("ambi_saved");
        }

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

        bool off = self->me->isPlayerCharacter() && !stance::playerAllowed(self->me);
        std::string dk(DUAL_OFF_KEY);
        if (data->fdata.find(dk) != data->fdata.end())
            data->fdata.find(dk)->second = off ? 1.0f : 0.0f;
        else if (off)
            data->fdata.insert(std::make_pair(dk, 1.0f));
        if (off)
            diag::count("toggle_saved_off");
    }

    void (*updateStats_orig)(CharStats*, GameData*) = NULL;
    void updateStats_hook(CharStats* self, GameData* data)
    {

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
        int rankMax = -1;
        if (g_enabled && data != NULL && data->fdata.count(RANK_KEY) != 0)
        {
            float r = data->fdata.find(RANK_KEY)->second;
            rankMax = finite(r) && r >= 0.0f ? (int)(r + 0.5f) : -1;
        }
        updateStats_orig(self, data);
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

        if (has && a >= 0.0f && self != NULL && self->me != NULL)
        {
            store(self->me, a);
            diag::count("ambi_loaded");
        }
    }

    void (*charDtor_orig)(Character*) = NULL;
    void charDtor_hook(Character* self)
    {
        {
            Lock l;
            g_amb.erase(self);
            g_rankMax.erase(self);
        }

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

    __declspec(thread) float* t_capStat = NULL;
    __declspec(thread) float t_capAmount = 0.0f;
    __declspec(thread) float t_capLimit = 101.0f;

    void (*increase_orig)(float&, float, float) = NULL;
    void increase_hook(float& stat, float amount, float limit)
    {
        if (t_capStat != NULL && &stat == t_capStat)
        {
            t_capAmount += amount;
            t_capLimit = limit;
        }
        increase_orig(stat, amount, limit);
    }

    struct SkillRedirect
    {
        CharStats* st;
        float* saved;
        float* prevCap;
        SkillRedirect(CharStats* s, float* to) : st(s), saved(s->pCurrentWeaponSkill), prevCap(t_capStat)
        {
            if (to != NULL)
                s->pCurrentWeaponSkill = to;
            t_capStat = s->pCurrentWeaponSkill;
            t_capAmount = 0.0f;
        }
        ~SkillRedirect()
        {
            st->pCurrentWeaponSkill = saved;
            t_capStat = prevCap;
        }
    };

    void (*xpMelee_orig)(CharStats*, CharStats::DeadTimeState, Character*, const Damages&) = NULL;
    void xpMelee_hook(CharStats* self, CharStats::DeadTimeState what, Character* target, const Damages& damage)
    {
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
        {
            SkillRedirect redirect(self, offSkill);
            xpMelee_orig(self, what, target, damage);
        }
        float amount = t_capAmount;
        if (!(amount > 0.0f) || !finite(amount))
            return;

        float sMin = s.skillMain < s.skillOff ? s.skillMain : s.skillOff;
        if (leftHanded)
        {
            Weapon* w = me->getCurrentWeapon();
            StatsEnumerated st = w != NULL ? stance::statFor(w->category) : STAT_NONE;
            sMin = st != STAT_NONE ? self->getStat(st, false) : 0.0f;
            diag::count("ambi_grow_lefty");
        }

        float a = ambidex::stored(me);
        float k = 1.0f + (sMin - a) / 50.0f;
        k = k < 0.25f ? 0.25f : (k > 2.0f ? 2.0f : k);
        float next = a;
        increase_orig(next, cfg::f(cfg::AMB_XP_SHARE) * amount * k, t_capLimit);
        if (!finite(next) || next < a)
            return;
        store(me, next);
        diag::count("ambi_grow");
        if (me->isPlayerCharacter() && takeDetail())
            kit::Diag("ambi").kv("who", me->displayName).kv("state", (int)what).kv("xp_amount", amount)
                .kv("a_before", a).kv("a_after", next).kv("skill_min", sMin).kv("limit", t_capLimit)
                .kv("off_xp", offSkill != NULL).emit();
    }

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

    void setProgress(DatapanelGUI* right, float a)
    {
        float frac = a - (float)(int)a;
        char pct[16];
        sprintf_s(pct, "%d%%", (int)(frac * 100.0f));
        right->setLine(g_text.nextLevel, pct, frac, right->getCurrentCategory());
    }

    void fillDescription(CharacterStatsWindow* win, float a)
    {
        DatapanelGUI* left = win->description1Datapanel;
        DatapanelGUI* right = win->description2Datapanel;
        if (left == NULL || right == NULL)
            return;
        win->currentStatIdDescription = UI_KEY;
        win->statProgress = STAT_NONE;
        left->clear();
        right->clear();
        int lc = left->getCurrentCategory();
        left->setLine(g_label, "", lc, false, true);
        left->setLine(g_text.affectsTitle, "", lc, false, true);
        left->setLine(g_text.affects, "", lc, false, true);
        left->setLine(g_text.trainTitle, "", lc, false, true);
        left->setLine(g_text.train, "", lc, false, true);

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

        stance::Preview p = stance::preview(win->character);
        std::string prof = rankLabel(p.tier, (int)(p.dp + 0.0001f));
        if (p.nextAmbi > 0.0f)
        {
            std::string capped = g_text.tierCapped;
            char n[16];
            sprintf_s(n, "%d", (int)p.nextAmbi);
            replaceAll(capped, "{amb}", n);
            replaceAll(capped, "{next}", rankName(p.tier + 1));
            prof += "; " + capped;
        }
        left->setLine(g_text.proficiency, (p.tier >= 1 ? rankColour(p.tier) : std::string()) + prof, lc, false, true);

        sprintf_s(v, "x%.2f", p.speedFactor);
        left->setLine(g_text.speed, v, lc, false, true);

        if (balance::dexBonus() > 0.0f)
        {
            sprintf_s(v, "+%d%%", (int)(balance::dexBonus() * 100.0f + 0.5f));
            left->setLine(g_text.dexBonus, v, lc, false, true);
        }

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

            if (!what.empty())
                left->setLine(g_text.pair, what, lc, false, true);
            if (blades::mixedTypes(pa, pb))
                left->setLine(what.empty() ? g_text.pair : std::string(" "), g_text.pairMixed, lc, false, true);
        }

        left->setLine(g_text.ranksTitle, "", lc, false, true);
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
        int rc = right->getCurrentCategory();
        right->setLine(g_text.descTitle, "", rc, false, true);
        right->setLine(g_text.desc, "", "Kenshi_WordWrapEmpty", "Kenshi_WordWrapEmpty", rc);
        diag::count("ambi_description");
    }

    void setStatsLine(CharacterStatsWindow* win)
    {
        if (!g_showLine || win == NULL || win->character == NULL || win->skills1Datapanel == NULL)
            return;

        float a = ambidex::get(win->character);

        ambidex::Effective e = ambidex::effective(win->character);
        int loss = (int)(a + 0.0001f) - (int)(e.eff + 0.0001f);
        char num[48];
        if (loss > 0)
            sprintf_s(num, "%d #FF5040(-%d)", (int)(a + 0.0001f), loss);
        else
            sprintf_s(num, "%d", (int)(a + 0.0001f));
        std::string value(num);

        DatapanelGUI* panel = win->skills1Datapanel;
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
        }
        bool ours = win->currentStatIdDescription == UI_KEY;

        static DWORD lastFill = 0;
        if (mouseOver(line) && (!ours || GetTickCount() - lastFill > 200))
        {
            fillDescription(win, a);
            lastFill = GetTickCount();
        }
        else if (ours && win->description2Datapanel != NULL)
            setProgress(win->description2Datapanel, a);
    }

    void (*statsWindow_orig)(CharacterStatsWindow*) = NULL;
    void statsWindow_hook(CharacterStatsWindow* self)
    {
        statsWindow_orig(self);
        if (g_enabled)
            setStatsLine(self);
    }

    bool hook(KenshiLib::HookStatus s, const char* what)
    {
        if (s == KenshiLib::SUCCESS)
            return true;
        ErrorLog(std::string("Nitoryu: could not hook ") + what);
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
        };
        g_coloursOn = GetPrivateProfileIntA("Stats", "rank_colours", 1, path.c_str()) != 0;
        g_rankUpMessage = GetPrivateProfileIntA("Stats", "rank_up_message", 1, path.c_str()) != 0;
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
        ok &= hook(KenshiLib::AddHook(KenshiLib::GetRealAddress(&CharStats::serialise), serialise_hook, &serialise_orig),
                   "CharStats::serialise");
        ok &= hook(KenshiLib::AddHook(KenshiLib::GetRealAddress(&CharStats::updateStats), updateStats_hook, &updateStats_orig),
                   "CharStats::updateStats");
        ok &= hook(KenshiLib::AddHook(KenshiLib::GetRealAddress(&CharStats::xpMelee), xpMelee_hook, &xpMelee_orig),
                   "CharStats::xpMelee");
        ok &= hook(KenshiLib::AddHook(KenshiLib::GetRealAddress(&increaseStat), increase_hook, &increase_orig),
                   "increaseStat");
        ok &= hook(KenshiLib::AddHook(KenshiLib::GetRealAddress(&CharacterStatsWindow::_NV_update), statsWindow_hook,
                                      &statsWindow_orig),
                   "CharacterStatsWindow::update");
        return ok;
    }

    bool installLifetimeHook()
    {
        return hook(KenshiLib::AddHook(KenshiLib::GetRealAddress(&Character::_DESTRUCTOR), charDtor_hook, &charDtor_orig),
                    "Character destructor");
    }

    void setEnabled(bool on) { g_enabled = on; }

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
                g_rankMax[c] = tier;
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
        std::string msg = g_text.rankUp;
        replaceAll(msg, "{who}", c->displayName);
        replaceAll(msg, "{rank}", name);
        if (ou != NULL && !msg.empty())
            ou->showPlayerAMessage(msg, true);
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

    float get(Character* c)
    {
        if (c == NULL)
            return 0.0f;

        if (g_testSkill && c->isPlayerCharacter())
            return bestSkill(c);
        if (g_testA >= 0.0f && c->isPlayerCharacter())
            return g_testA;
        return stored(c);
    }

    void setStored(Character* c, float a)
    {
        if (c != NULL && finite(a))
            store(c, a);
    }

    float stored(Character* c)
    {
        if (c == NULL)
            return 0.0f;
        {
            Lock l;
            std::map<Character*, float>::iterator it = g_amb.find(c);
            if (it != g_amb.end())
                return it->second;
        }
        float seed = seedFor(c);
        Lock l;
        std::map<Character*, float>::iterator it = g_amb.find(c);
        if (it != g_amb.end())
            return it->second;
        g_amb[c] = seed;
        return seed;
    }

    Effective effective(Character* c)
    {
        Effective e;
        e.a = get(c);
        e.eff = e.a;
        e.overload = 0.0f;
        e.overloadMult = 1.0f;
        e.armourMult = 1.0f;
        CharStats* st = c != NULL ? c->getStats() : NULL;
        if (st == NULL)
            return e;
        float m = st->encumbranceMult;
        if (finite(m) && m < 0.999f)
        {
            if (m < 0.0f)
                m = 0.0f;
            e.overload = 1.0f / (0.05f + 0.95f * m) - 1.0f;
            float f = e.overload <= 0.05f ? 1.0f - 5.5f * e.overload : 0.75f - 0.5f * e.overload;
            float lo = cfg::f(cfg::OVERLOAD_FLOOR);
            e.overloadMult = f < lo ? lo : f;
        }
        float csm = st->combatSpeedMultiplier;
        if (finite(csm) && csm < 1.0f)
        {
            float f = 1.0f - cfg::f(cfg::ARMOUR_PENALTY) * (1.0f - csm);
            float lo = cfg::f(cfg::ARMOUR_FLOOR);
            e.armourMult = f < lo ? lo : f;
        }
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
