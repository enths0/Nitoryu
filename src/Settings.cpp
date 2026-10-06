#include "kit/input.h"

#include "kit/log.h"

#include <kenshi/Kenshi.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/GameData.h>

#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "Settings.h"
#include "SettingsBounds.h"
#include "IniKeysGen.h"
#include "Diagnostics.h"
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

    struct Range
    {
        double lo, hi;
        bool set;
    };
    Range g_range[cfg::COUNT];

    void initRanges()
    {
        for (int id = 0; id < cfg::COUNT; ++id)
        {
            g_range[id].lo = -DBL_MAX;
            g_range[id].hi = DBL_MAX;
            g_range[id].set = false;
        }
#define NITORYU_SETTING_RANGE(id, LOW, HIGH) g_range[cfg::id].lo = (LOW); g_range[cfg::id].hi = (HIGH); g_range[cfg::id].set = true;
        NITORYU_SETTING_BOUNDS(NITORYU_SETTING_RANGE)
#undef NITORYU_SETTING_RANGE
    }

    bool finiteNum(double x) { return x == x && x <= DBL_MAX && x >= -DBL_MAX; }

    double clampTo(int id, double x, bool& clamped)
    {
        const Range& g = g_range[id];
        if (x < g.lo) { clamped = true; return g.lo; }
        if (x > g.hi) { clamped = true; return g.hi; }
        return x;
    }

    std::string numText(double x)
    {
        char b[48];
        sprintf_s(b, "%.6g", x);
        return b;
    }

    std::string rangeText(int id)
    {
        const Range& g = g_range[id];
        if (g.hi >= NITORYU_ANY)
            return numText(g.lo) + " or more";
        return numText(g.lo) + ".." + numText(g.hi);
    }

    std::set<std::string> g_said;

    void tell(const char* what, int id, const std::string& raw, const std::string& used, cfg::Source src)
    {
        std::string key = std::string(what) + "|" + ROWS[id].fcs + "|" + raw;
        if (!g_said.insert(key).second)
            return;
        diag::count(what);
        kit::Diag(what).kv("key", ROWS[id].fcs).kv("value", raw).kv("used", used)
            .kv("range", g_range[id].set ? rangeText(id) : std::string("-"))
            .kv("source", src == cfg::FCS ? "fcs" : src == cfg::INI ? "ini" : "default").emit();
    }

    bool parseNumber(const std::string& v, double& out)
    {
        const char* p = v.c_str();
        while (*p == ' ' || *p == '\t')
            ++p;
        char* end = NULL;
        out = strtod(p, &end);
        return end != p && finiteNum(out);
    }

    void put(State& s, int id, const std::string& v, cfg::Source src)
    {
        const Row& r = ROWS[id];
        s.src[id] = src;
        if (!isText(r.type))
        {
            if (r.type == 'B')
            {
                bool on = v == "1" || v == "true" || v == "True" || v == "TRUE";
                bool off = v == "0" || v == "false" || v == "False" || v == "FALSE";
                if (!on && !off)
                {
                    on = strcmp(r.def, "1") == 0;
                    tell("setting_not_a_switch", id, v, r.def, src);
                }
                s.num[id] = on ? 1.0 : 0.0;
                return;
            }
            double x = 0.0;
            if (!parseNumber(v, x))
            {
                x = atof(r.def);
                if (src != cfg::DEFAULT)
                    tell("setting_not_a_number", id, v, r.def, src);
            }
            bool clamped = false;
            x = clampTo(id, x, clamped);
            if (clamped)
                tell("setting_clamped", id, v, numText(x), src);
            s.num[id] = x;
            return;
        }
        s.text[id] = v;
        if (r.type == 'C')
        {

            std::string out;
            bool clamped = false, bad = false;
            const char* p = v.c_str();
            int pairs = 0;
            while (*p)
            {
                char* end = NULL;
                double dp = strtod(p, &end);
                if (end == p || *end != ':') { bad = true; break; }
                p = end + 1;
                double val = strtod(p, &end);
                if (end == p || !finiteNum(dp) || !finiteNum(val)) { bad = true; break; }
                p = end;
                val = clampTo(id, val, clamped);
                if (!out.empty())
                    out += ",";
                out += numText(dp) + ":" + numText(val);
                ++pairs;
                while (*p == ',' || *p == ' ' || *p == '\t')
                    ++p;
            }
            if (bad || pairs == 0)
            {
                s.text[id] = r.def;
                if (src != cfg::DEFAULT)
                    tell("setting_not_a_curve", id, v, r.def, src);
            }
            else if (clamped)
            {
                s.text[id] = out;
                tell("setting_clamped", id, v, out, src);
            }
            return;
        }
        if (r.type != 'L')
            return;
        int n = 0;
        bool clamped = false;
        const char* p = v.c_str();
        while (*p && n < cfg::LIST_MAX)
        {
            char* end = NULL;
            double x = strtod(p, &end);
            if (end == p) { ++p; continue; }
            if (!finiteNum(x))
                x = g_range[id].lo > -DBL_MAX ? g_range[id].lo : 0.0;
            x = clampTo(id, x, clamped);
            s.list[id][n++] = (float)x;
            p = end;
        }
        for (int k = n; k < cfg::LIST_MAX; ++k)
            s.list[id][k] = 0.0f;
        s.listN[id] = n;
        if (clamped)
        {
            std::string out;
            for (int k = 0; k < n; ++k)
                out += (k > 0 ? "," : "") + numText(s.list[id][k]);
            s.text[id] = out;
            tell("setting_clamped", id, v, out, src);
        }
    }

    bool knownKey(const std::string& full)
    {
        static const char* const KNOWN[] = {
#define NITORYU_INI_KEY(k) k,
            NITORYU_INI_KEYS(NITORYU_INI_KEY)
#undef NITORYU_INI_KEY
            NULL };
        for (int k = 0; KNOWN[k] != NULL; ++k)
            if (_stricmp(KNOWN[k], full.c_str()) == 0)
                return true;
        return false;
    }

    const char* replacedBy(const std::string& full)
    {
        static const char* const LEGACY[][2] = {
#define NITORYU_INI_OLD(k, now) { k, now },
            NITORYU_INI_LEGACY(NITORYU_INI_OLD)
#undef NITORYU_INI_OLD
            { NULL, NULL } };
        for (int k = 0; LEGACY[k][0] != NULL; ++k)
            if (_stricmp(LEGACY[k][0], full.c_str()) == 0)
                return LEGACY[k][1];
        return NULL;
    }

    std::string trim(const std::string& t)
    {
        size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
        return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
    }

    void checkIni(const std::string& path)
    {
        std::vector<char> names(8192), body(32768);
        DWORD got = GetPrivateProfileSectionNamesA(&names[0], (DWORD)names.size(), path.c_str());
        if (got == 0)
            return;
        int lines = 0;
        for (const char* sec = &names[0]; *sec != '\0'; sec += strlen(sec) + 1)
        {
            DWORD n = GetPrivateProfileSectionA(sec, &body[0], (DWORD)body.size(), path.c_str());
            if (n == 0)
                continue;
            std::set<std::string> seen;
            for (const char* e = &body[0]; *e != '\0'; e += strlen(e) + 1)
            {
                std::string entry(e);
                size_t eq = entry.find('=');
                std::string key = trim(eq == std::string::npos ? entry : entry.substr(0, eq));
                if (key.empty() || key[0] == ';' || key[0] == '#' || eq == std::string::npos)
                    continue;
                std::string full = std::string(sec) + "." + key;
                std::string lower(full);
                for (size_t k = 0; k < lower.size(); ++k)
                    lower[k] = (char)tolower((unsigned char)lower[k]);
                const char* what = NULL;
                const char* now = NULL;
                if (!seen.insert(lower).second)
                    what = "ini_key_duplicate";
                else if ((now = replacedBy(full)) != NULL)
                    what = "ini_key_retired";
                else if (!knownKey(full))
                    what = "ini_key_unknown";
                if (what == NULL)
                    continue;
                diag::count(what);
                if (lines++ < 20)
                {
                    kit::Diag d(what);
                    d.kv("section", std::string(sec)).kv("key", key);
                    if (now != NULL)
                        d.kv("use", std::string(now));
                    if (strcmp(what, "ini_key_duplicate") == 0)
                        d.kv("note", "the first one counts");
                    d.emit();
                }
            }
        }
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
        initRanges();
        defaults(g_state);
        layIni(g_state);
        checkIni(iniPath);
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

    int readIdValues(const std::string& iniPath, const char* section, const char* key, std::map<std::string, float>& out)
    {
        char ids[4096] = { 0 };
        GetPrivateProfileStringA(section, key, "", ids, sizeof(ids), iniPath.c_str());
        int bad = 0;
        std::string cur;
        for (const char* q = ids;; ++q)
        {
            if (*q != ',' && *q != '\0')
            {
                cur += *q;
                continue;
            }
            size_t a = cur.find_first_not_of(" \t"), colon = cur.rfind(':');
            if (a != std::string::npos)
            {
                size_t e = colon != std::string::npos && colon > a ? cur.find_last_not_of(" \t", colon - 1) : std::string::npos;
                float v = colon != std::string::npos ? (float)atof(cur.c_str() + colon + 1) : -1.0f;
                if (e == std::string::npos || e < a || !(v == v) || v < 0.0f || v > 1.0e6f)
                    ++bad;
                else
                    out[cur.substr(a, e - a + 1)] = v;
            }
            cur.clear();
            if (*q == '\0')
                break;
        }
        return bad;
    }
}
