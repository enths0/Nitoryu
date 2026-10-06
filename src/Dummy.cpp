#include "kit/input.h"

#include "kit/log.h"
#include <core/Functions.h>

#include <kenshi/Kenshi.h>
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/GameData.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/GameDataManager.h>
#include <kenshi/Building/UseableStuff.h>

#include <string>

#include "Dummy.h"
#include "Ambidex.h"
#include "Diagnostics.h"
#include "Lang.h"
#include "Settings.h"
#include "Stance.h"
#include "kit/diag.h"
#include "kit/fcs.h"

namespace
{
    volatile bool g_enabled = true;
    bool g_on = true;

    __declspec(thread) Character* t_trainee = NULL;

    bool finite(float v) { return v == v && v > -1.0e6f && v < 1.0e6f; }

    void (*xpTraining_orig)(CharStats*, float, float, float&, float, StatsEnumerated) = NULL;
    void xpTraining_hook(CharStats* self, float time, float mult, float& stat, float upperLimit, StatsEnumerated which)
    {
        Character* c = t_trainee;
        if (c == NULL || self == NULL || self != c->getStats())
        {
            xpTraining_orig(self, time, mult, stat, upperLimit, which);
            return;
        }
        if (!stance::hasPair(c))
        {
            diag::count("dummy_no_pair");
            return;
        }

        float a = ambidex::stored(c);
        float next = a;
        xpTraining_orig(self, time, mult, next, upperLimit, which);
        if (!finite(next) || !(next > a))
        {
            diag::count(a >= upperLimit ? "dummy_at_cap" : "dummy_no_gain");

            static Character* said = NULL;
            if (said != c && diag::takeDetail())
            {
                said = c;
                kit::Diag("dummy_check").kv("who", c->displayName).kv("stored", a).kv("shown", ambidex::get(c))
                    .kv("cap", upperLimit).kv("time", time).kv("mult", mult).kv("after", next).emit();
            }
            return;
        }
        ambidex::setStored(c, next);
        diag::count("dummy_trained");
        if ((int)next != (int)a && diag::takeDetail())
            kit::Diag("dummy").kv("who", c->displayName).kv("a_before", a).kv("a_after", next).kv("cap", upperLimit).emit();
    }

    void (*operate_orig)(UseableStuff*, Character*, float) = NULL;
    void operate_hook(UseableStuff* self, Character* who, float t)
    {
        if (!g_enabled || !g_on || self == NULL || who == NULL || self->specialFunction != BF_TRAINING
            || !kit::fcsBool(self->getGameData(), "nitoryu pair dummy", false))
        {
            operate_orig(self, who, t);
            return;
        }
        Character* prev = t_trainee;
        t_trainee = who;
        operate_orig(self, who, t);
        t_trainee = prev;

        float a = stance::hasPair(who) ? ambidex::stored(who) : 0.0f;
        self->progressBarLevel = finite(a) && a > 0.0f ? a - (float)(int)a : 0.0f;
    }

    void setText(GameData* d, const char* field, const std::string& v)
    {
        boost::unordered_map<std::string, std::string>::iterator it = d->sdata.find(field);
        if (it != d->sdata.end())
            it->second = v;
    }

    void localise()
    {
        static const char* const BUILDING[3] = { "19-Nitoryu.mod", "18-Nitoryu.mod", "17-Nitoryu.mod" };
        static const char* const RESEARCH[3] = { "20-Nitoryu.mod", "21-Nitoryu.mod", "22-Nitoryu.mod" };
        static const char* const KEYS[3][4] = {
            { "dummy.name1", "dummy.desc1", "dummy.research1", "dummy.research_desc1" },
            { "dummy.name2", "dummy.desc2", "dummy.research2", "dummy.research_desc2" },
            { "dummy.name3", "dummy.desc3", "dummy.research3", "dummy.research_desc3" } };
        if (ou == NULL)
            return;
        int done = 0;
        for (int i = 0; i < 3; ++i)
        {
            GameData* b = ou->gamedata.getData(BUILDING[i]);
            if (b != NULL)
            {
                b->name = lang::text(KEYS[i][0]);
                setText(b, "Description", lang::text(KEYS[i][1]));
                setText(b, "building group", lang::text("dummy.group"));
                ++done;
            }
            GameData* r = ou->gamedata.getData(RESEARCH[i]);
            if (r != NULL)
            {
                r->name = lang::text(KEYS[i][2]);
                setText(r, "description", lang::text(KEYS[i][3]));
                setText(r, "category", lang::text("dummy.research_category"));
                ++done;
            }
        }
        static bool logged = false;
        if (!logged)
        {
            logged = true;
            kit::Diag("dummy_texts").kv("language", lang::code()).kv("records", done).emit();
        }
    }
}

namespace dummy
{
    void applySettings()
    {
        g_on = cfg::b(cfg::DUMMY_ON);
        localise();
    }

    bool installHooks()
    {
        applySettings();
        bool ok = KenshiLib::AddHook(KenshiLib::GetRealAddress(&CharStats::xpTraining), xpTraining_hook,
                                     &xpTraining_orig) == KenshiLib::SUCCESS;
        if (!ok)
            kit::log::error("Nitoryu: could not hook CharStats::xpTraining");

        ok = ok && KenshiLib::AddHook(KenshiLib::GetRealAddress(&UseableStuff::_NV_operate), operate_hook,
                                      &operate_orig) == KenshiLib::SUCCESS;
        if (!ok)
            kit::log::error("Nitoryu: could not hook UseableStuff::operate; the pair dummy trains nothing");
        return ok;
    }

    void setEnabled(bool on) { g_enabled = on; }
}
