#include "kit/input.h"

#include <Debug.h>

#include <kenshi/Kenshi.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/GameData.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Settings.h"
#include "kit/diag.h"

namespace
{
    struct Row
    {
        const char* name;
        char type;
        const char* def;
        const char* fcs;
        const char* iniSection;
        const char* iniKey;
    };

#define NITORYU_SETTING_ROW(id, type, def, fcs, section, iniSection, iniKey, tip) { #id, #type[0], def, fcs, iniSection, iniKey },
    const Row ROWS[cfg::COUNT] = { NITORYU_SETTINGS(NITORYU_SETTING_ROW) };
#undef NITORYU_SETTING_ROW

    const char* RECORD = "GLOBAL CONSTANTS";

    struct State
    {
        double num[cfg::COUNT];
        float list[cfg::COUNT][cfg::LIST_MAX];
        int listN[cfg::COUNT];
        std::string text[cfg::COUNT];
        cfg::Source src[cfg::COUNT];
    };

    State g_state;
    std::string g_ini;
    bool g_found = false;
    bool g_logged = false;

    bool isText(char type) { return type == 'C' || type == 'L' || type == 'S'; }

    void put(State& s, int id, const std::string& v, cfg::Source src)
    {
        const Row& r = ROWS[id];
        s.src[id] = src;
        if (!isText(r.type))
        {
            if (r.type == 'B')
                s.num[id] = (v == "1" || v == "true" || v == "True") ? 1.0 : 0.0;
            else
                s.num[id] = atof(v.c_str());
            return;
        }
        s.text[id] = v;
        if (r.type != 'L')
            return;
        int n = 0;
        const char* p = v.c_str();
        while (*p && n < cfg::LIST_MAX)
        {
            char* end = NULL;
            double x = strtod(p, &end);
            if (end == p) { ++p; continue; }
            s.list[id][n++] = (float)x;
            p = end;
        }
        for (int k = n; k < cfg::LIST_MAX; ++k)
            s.list[id][k] = 0.0f;
        s.listN[id] = n;
    }

    void defaults(State& s)
    {
        for (int id = 0; id < cfg::COUNT; ++id)
        {
            s.num[id] = 0.0;
            s.listN[id] = 0;
            for (int k = 0; k < cfg::LIST_MAX; ++k)
                s.list[id][k] = 0.0f;
            put(s, id, ROWS[id].def, cfg::DEFAULT);
        }
    }

    bool fromRecord(const GameData* d, const Row& r, std::string& out)
    {
        std::string key(r.fcs);
        char b[64];
        if (isText(r.type))
        {
            auto it = d->sdata.find(key);
            if (it == d->sdata.end())
                return false;
            out = it->second;
            return true;
        }
        if (r.type == 'B')
        {
            auto it = d->bdata.find(key);
            if (it == d->bdata.end())
                return false;
            out = it->second ? "1" : "0";
            return true;
        }
        auto fi = d->fdata.find(key);
        if (fi != d->fdata.end())
        {
            sprintf_s(b, "%.9g", (double)fi->second);
            out = b;
            return true;
        }
        auto ii = d->idata.find(key);
        if (ii != d->idata.end())
        {
            sprintf_s(b, "%d", ii->second);
            out = b;
            return true;
        }
        return false;
    }

    void layIni(State& s)
    {
        char buf[512];
        for (int id = 0; id < cfg::COUNT; ++id)
        {
            const Row& r = ROWS[id];
            if (r.iniKey[0] == '\0')
                continue;
            buf[0] = '\0';
            GetPrivateProfileStringA(r.iniSection, r.iniKey, "", buf, sizeof(buf), g_ini.c_str());
            if (buf[0] != '\0')
                put(s, id, buf, cfg::INI);
        }
    }

    bool same(const State& a, const State& b, int id)
    {
        if (isText(ROWS[id].type))
            return a.text[id] == b.text[id];
        return a.num[id] == b.num[id];
    }

    void commit(const State& s, int id)
    {
        g_state.num[id] = s.num[id];
        for (int k = 0; k < cfg::LIST_MAX; ++k)
            g_state.list[id][k] = s.list[id][k];
        g_state.listN[id] = s.listN[id];
        g_state.text[id] = s.text[id];
        g_state.src[id] = s.src[id];
    }

    bool valid(cfg::Id id) { return id >= 0 && id < cfg::COUNT; }
}

namespace cfg
{
    void init(const std::string& iniPath)
    {
        g_ini = iniPath;
        defaults(g_state);
        layIni(g_state);
    }

    bool refresh()
    {
        if (ou == NULL)
            return false;
        GameData* d = ou->gamedata.getDataByName(RECORD, CONSTANTS);
        if (d == NULL)
            return false;
        static State next;
        defaults(next);
        std::string v;
        std::string base;
        for (int id = 0; id < COUNT; ++id)
        {
            if (!fromRecord(d, ROWS[id], v))
                continue;

            base = next.text[id];
            double num = next.num[id];
            put(next, id, v, FCS);
            bool changed = isText(ROWS[id].type) ? next.text[id] != base
                                                 : (float)next.num[id] != (float)num;
            if (!changed)
                next.src[id] = DEFAULT;
        }
        layIni(next);
        bool any = !g_found;
        g_found = true;
        for (int id = 0; id < COUNT; ++id)
        {
            if (same(next, g_state, id) && next.src[id] == g_state.src[id])
                continue;
            commit(next, id);
            any = true;
        }
        if (any)
            g_logged = false;
        return any;
    }

    bool recordFound() { return g_found; }

    float f(Id id) { return valid(id) ? (float)g_state.num[id] : 0.0f; }
    double d(Id id) { return valid(id) ? g_state.num[id] : 0.0; }
    int i(Id id) { return valid(id) ? (int)(g_state.num[id] < 0.0 ? g_state.num[id] - 0.5 : g_state.num[id] + 0.5) : 0; }
    bool b(Id id) { return valid(id) && g_state.num[id] != 0.0; }

    float item(Id id, int index)
    {
        return valid(id) && index >= 0 && index < g_state.listN[id] ? g_state.list[id][index] : 0.0f;
    }

    int items(Id id) { return valid(id) ? g_state.listN[id] : 0; }

    const std::string& text(Id id)
    {
        static const std::string none;
        return valid(id) ? g_state.text[id] : none;
    }

    const char* defaultText(Id id) { return valid(id) ? ROWS[id].def : ""; }

    Source source(Id id) { return valid(id) ? g_state.src[id] : DEFAULT; }
    const char* fcsKey(Id id) { return valid(id) ? ROWS[id].fcs : ""; }

    void log()
    {
        if (g_logged)
            return;
        g_logged = true;
        int fromFcs = 0, fromIni = 0;
        char b[64];
        for (int id = 0; id < COUNT; ++id)
        {
            Source s = g_state.src[id];
            if (s == DEFAULT)
                continue;
            ++(s == FCS ? fromFcs : fromIni);
            std::string v;
            if (isText(ROWS[id].type))
                v = g_state.text[id];
            else
            {
                sprintf_s(b, "%.6g", g_state.num[id]);
                v = b;
            }
            kit::Diag("setting").kv("key", ROWS[id].fcs).kv("value", v).kv("source", s == FCS ? "fcs" : "ini")
                .kv("default", ROWS[id].def).emit();
        }
        kit::Diag("settings").kv("total", (int)COUNT).kv("record_found", g_found).kv("from_fcs", fromFcs)
            .kv("from_ini", fromIni).emit();
    }
}
