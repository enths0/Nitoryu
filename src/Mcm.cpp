// Nitoryu's page in a mod settings menu (MCM).
//
// A settings menu plugin (a tab in the game's Options window) finds its mods by an export: when the game first
// builds the Options window it walks the loaded DLLs and calls `MCM_Describe(api)` in each one that has it. We
// describe our pages through the function table it hands us. Nitoryu never calls the menu and does not need it:
// without one installed nobody calls the export, and the ini works as before.
//
// The rows are src/McmDef.h. Each is a "row on functions": the menu asks us for the value and gives us the
// player's choice, so the three sources keep their order (default < FCS < an ini key that is present):
//   shown    the ini key if it is present, else what FCS or the default gives;
//   written  into Nitoryu.ini; a choice equal to what FCS or the default gives removes the key instead, so the
//            menu's "reset" never masks a modpack's values with ours.
// A key of the settings table (SettingsDef.h) is read again by cfg::refresh on the main loop's slow tick; any
// other key is read once at start and its row is marked "after a restart".
//
// Thread: the menu calls all of this from the Options window (the main thread). Nothing may throw across the DLL
// boundary, so every entry point catches.
#include "kit/log.h"    // Windows.h with NOMINMAX first

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Lang.h"
#include "McmDef.h"
#include "Settings.h"
#include "Combat.h"
#include "Feedback.h"

namespace
{
    // The menu's function table, as its API document lays it out (version 5). Members we do not call are kept as
    // plain pointers so the offsets hold. New members are only ever added at the end; `version` says how far it goes.
    struct McmApi;
    typedef int (__cdecl *GetIntFn)(void* ud, int* out);
    typedef int (__cdecl *SetIntFn)(void* ud, int value, char* err, unsigned errSize);
    typedef int (__cdecl *GetFloatFn)(void* ud, float* out);
    typedef int (__cdecl *SetFloatFn)(void* ud, float value, char* err, unsigned errSize);
    typedef int (__cdecl *GetTextFn)(void* ud, char* out, unsigned outSize);
    typedef int (__cdecl *SetTextFn)(void* ud, const char* value, char* err, unsigned errSize);

    struct McmApi
    {
        int version;
        void* impl;
        void (__cdecl *beginMod)(McmApi* api, const char* id, const char* title, const char* iniPath, void (__cdecl *apply)());
        void (__cdecl *section)(McmApi* api, const char* title);
        void* iniRows[7];       // rows the menu reads and writes in an ini itself: not used (see the header)
        // version 2
        void (__cdecl *info)(McmApi* api, const char* text);
        // version 3: rows on functions
        void (__cdecl *toggleFn)(McmApi* api, const char* label, const char* tip, GetIntFn get, SetIntFn set, void* ud, unsigned flags);
        void (__cdecl *integerFn)(McmApi* api, const char* label, const char* tip, GetIntFn get, SetIntFn set, void* ud,
                                  int lowest, int highest, unsigned flags);
        void (__cdecl *numberFn)(McmApi* api, const char* label, const char* tip, GetFloatFn get, SetFloatFn set, void* ud,
                                 float lowest, float highest, int decimals, unsigned flags);
        void (__cdecl *choiceFn)(McmApi* api, const char* label, const char* tip, GetIntFn get, SetIntFn set, void* ud,
                                 const int* values, const char* const* labels, int count, unsigned flags);
        void (__cdecl *textFn)(McmApi* api, const char* label, const char* tip, GetTextFn get, SetTextFn set, void* ud, unsigned flags);
        void* colourFn;
        void* hotkeyFn;
        void* action;
        // version 4
        void* customArea;
        // version 5: the default of the row just added, as text; gives the page its reset button
        void (__cdecl *defaultValue)(McmApi* api, const char* value);
    };

    const unsigned FLAG_RESTART = 0x1u;

    struct Page
    {
        int page;
        const char* id;
        const char* title;
        const char* info;
    };

    struct Row
    {
        int page;
        bool heading;
        const char* section;    // a heading: its id
        const char* key;
        char kind;              // T I N C S
        const char* def;
        double lowest, highest;
        int decimals;
        bool restart;
        const char* choices;
        const char* label;
        const char* tip;
    };

#define NITORYU_MCM_PAGE(page, id, title, info) { page, id, title, info },
    const Page PAGES[] = { NITORYU_MCM_PAGES(NITORYU_MCM_PAGE) };
#undef NITORYU_MCM_PAGE

#define NITORYU_MCM_H(page, id, text) { page, true, id, "", 'H', "", 0, 0, 0, false, "", text, "" },
#define NITORYU_MCM_X(page, section, key, kind, def, lowest, highest, decimals, restart, choices, label, tip) \
    { page, false, section, key, #kind[0], def, lowest, highest, decimals, restart != 0, choices, label, tip },
    // devRow: a row shown only with env=stage (none today).
    const Row ROWS[] = { NITORYU_MCM_ROWS(NITORYU_MCM_H, NITORYU_MCM_X) };
    bool devRow(const Row& r) { return strcmp(r.section, "Test") == 0 || strcmp(r.section, "dev") == 0; }
#undef NITORYU_MCM_H
#undef NITORYU_MCM_X
    const int ROW_COUNT = sizeof(ROWS) / sizeof(ROWS[0]);

    // A choice row's values and labels: the menu works with numbers (here the index), the ini holds the value.
    struct Choice
    {
        std::vector<std::string> values;
        std::vector<std::string> labels;
        std::vector<int> numbers;
        std::vector<const char*> labelPtrs;
    };
    std::vector<Choice> g_choices;      // one per row, sized once, so the pointers handed to the menu stay put

    std::string lower(const std::string& s)
    {
        std::string out(s);
        for (size_t k = 0; k < out.size(); ++k)
            if (out[k] >= 'A' && out[k] <= 'Z')
                out[k] = (char)(out[k] - 'A' + 'a');
        return out;
    }

    std::string trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos)
            return std::string();
        size_t b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    }

    // The text in the player's language (lang/<code>.ini, then en.ini), else the English of the table.
    const char* text(const std::string& key, const char* english)
    {
        const std::string* found = lang::find(key.c_str());
        return found != NULL && !found->empty() ? found->c_str() : english;
    }

    std::string rowKey(const Row& r) { return "mcm." + lower(r.section) + "." + r.key; }

    std::string iniValue(const Row& r)
    {
        char buf[512] = { 0 };
        GetPrivateProfileStringA(r.section, r.key, "", buf, sizeof(buf), cfg::iniPath().c_str());
        return trim(buf);
    }

    // What the value is with no ini key: FCS (or the table's default) for a setting, else this row's default.
    std::string baseValue(const Row& r)
    {
        cfg::Id id = cfg::byIni(r.section, r.key);
        return trim(id != cfg::COUNT ? cfg::baseText(id) : std::string(r.def));
    }

    std::string effective(const Row& r)
    {
        std::string v = iniValue(r);
        return v.empty() ? baseValue(r) : v;
    }

    bool restartRow(const Row& r) { return !devRow(r) && (r.restart || cfg::byIni(r.section, r.key) == cfg::COUNT); }

    std::string numberText(const Row& r, double v)
    {
        char buf[64];
        if (r.kind == 'N')
            sprintf_s(buf, "%.*f", r.decimals, v);
        else
            sprintf_s(buf, "%d", (int)floor(v + 0.5));
        return buf;
    }

    bool sameValue(const Row& r, const std::string& a, const std::string& b)
    {
        if (r.kind == 'T')
            return (atof(a.c_str()) != 0.0) == (atof(b.c_str()) != 0.0);
        if (r.kind == 'I' || r.kind == 'N')
            return numberText(r, atof(a.c_str())) == numberText(r, atof(b.c_str()));
        return lower(a) == lower(b);
    }

    // 0 = written. The player's choice goes into the ini; the value FCS or the default gives removes the key.
    int store(const Row& r, const std::string& value, char* err, unsigned errSize)
    {
        const std::string& ini = cfg::iniPath();
        bool back = sameValue(r, value, baseValue(r));
        if (back && iniValue(r).empty())
            return 0;
        if (WritePrivateProfileStringA(r.section, r.key, back ? NULL : value.c_str(), ini.c_str()))
        {
            kit::log::line(std::string("settings page: [") + r.section + "] " + r.key + (back ? " removed from the ini" : "=" + value));
            return 0;
        }
        if (err != NULL && errSize > 0)
            strncpy_s(err, errSize, "Nitoryu.ini could not be written", _TRUNCATE);
        kit::log::error(std::string("Nitoryu: the settings page could not write [") + r.section + "] " + r.key + " to " + ini);
        return 1;
    }

    int choiceIndex(const Row& r, const std::string& value)
    {
        const Choice& c = g_choices[&r - ROWS];
        for (size_t k = 0; k < c.values.size(); ++k)
            if (sameValue(r, c.values[k], value))
                return (int)k;
        return -1;
    }

    int __cdecl getInt(void* ud, int* out)
    {
        try
        {
            const Row& r = *static_cast<const Row*>(ud);
            std::string v = effective(r);
            if (r.kind == 'C')
            {
                int at = choiceIndex(r, v);
                if (at < 0)
                    at = choiceIndex(r, r.def);
                *out = at < 0 ? 0 : at;
            }
            else if (r.kind == 'T')
                *out = atof(v.c_str()) != 0.0 ? 1 : 0;
            else
                *out = (int)floor(atof(v.c_str()) + 0.5);
            return 0;
        }
        catch (...)
        {
            return 1;
        }
    }

    int __cdecl setInt(void* ud, int value, char* err, unsigned errSize)
    {
        try
        {
            const Row& r = *static_cast<const Row*>(ud);
            if (r.kind == 'C')
            {
                const Choice& c = g_choices[&r - ROWS];
                if (value < 0 || value >= (int)c.values.size())
                    return 1;
                return store(r, c.values[value], err, errSize);
            }
            if (r.kind == 'T')
                return store(r, value != 0 ? "1" : "0", err, errSize);
            return store(r, numberText(r, value), err, errSize);
        }
        catch (...)
        {
            return 1;
        }
    }

    int __cdecl getFloat(void* ud, float* out)
    {
        try
        {
            *out = (float)atof(effective(*static_cast<const Row*>(ud)).c_str());
            return 0;
        }
        catch (...)
        {
            return 1;
        }
    }

    int __cdecl setFloat(void* ud, float value, char* err, unsigned errSize)
    {
        try
        {
            if (!(value == value))
                return 1;
            const Row& r = *static_cast<const Row*>(ud);
            return store(r, numberText(r, value), err, errSize);
        }
        catch (...)
        {
            return 1;
        }
    }

    int __cdecl getText(void* ud, char* out, unsigned outSize)
    {
        try
        {
            if (out == NULL || outSize == 0)
                return 1;
            strncpy_s(out, outSize, effective(*static_cast<const Row*>(ud)).c_str(), _TRUNCATE);
            return 0;
        }
        catch (...)
        {
            return 1;
        }
    }

    int __cdecl setText(void* ud, const char* value, char* err, unsigned errSize)
    {
        try
        {
            return store(*static_cast<const Row*>(ud), trim(value != NULL ? value : ""), err, errSize);
        }
        catch (...)
        {
            return 1;
        }
    }

    void splitChoices(const Row& r, Choice& c)
    {
        std::string all(r.choices);
        size_t at = 0;
        while (at <= all.size())
        {
            size_t bar = all.find('|', at);
            if (bar == std::string::npos)
                bar = all.size();
            std::string one = all.substr(at, bar - at);
            size_t eq = one.find('=');
            if (eq != std::string::npos)
            {
                std::string value = one.substr(0, eq);
                c.values.push_back(value);
                c.labels.push_back(text(rowKey(r) + "." + lower(value), one.c_str() + eq + 1));
            }
            at = bar + 1;
        }
        for (size_t k = 0; k < c.values.size(); ++k)
        {
            c.numbers.push_back((int)k);
            c.labelPtrs.push_back(c.labels[k].c_str());
        }
    }

    void addRow(McmApi* api, const Row& r)
    {
        const char* label = text(rowKey(r), r.label);
        const char* tip = text(rowKey(r) + ".tip", r.tip);
        void* ud = const_cast<Row*>(&r);
        unsigned flags = restartRow(r) ? FLAG_RESTART : 0u;
        std::string def = baseValue(r);
        char buf[32];
        switch (r.kind)
        {
        case 'T':
            api->toggleFn(api, label, tip, &getInt, &setInt, ud, flags);
            def = atof(def.c_str()) != 0.0 ? "1" : "0";
            break;
        case 'I':
            api->integerFn(api, label, tip, &getInt, &setInt, ud, (int)r.lowest, (int)r.highest, flags);
            def = numberText(r, atof(def.c_str()));
            break;
        case 'N':
            api->numberFn(api, label, tip, &getFloat, &setFloat, ud, (float)r.lowest, (float)r.highest, r.decimals, flags);
            def = numberText(r, atof(def.c_str()));
            break;
        case 'C':
        {
            Choice& c = g_choices[&r - ROWS];
            if (c.values.empty())
                splitChoices(r, c);
            if (c.values.empty())
                return;
            api->choiceFn(api, label, tip, &getInt, &setInt, ud, &c.numbers[0], &c.labelPtrs[0], (int)c.values.size(), flags);
            int at = choiceIndex(r, def);
            sprintf_s(buf, "%d", at < 0 ? 0 : at);
            def = buf;
            break;
        }
        default:
            api->textFn(api, label, tip, &getText, &setText, ud, flags);
            break;
        }
        if (api->version >= 5 && api->defaultValue != NULL)
            api->defaultValue(api, def.c_str());
    }

    void describe(McmApi* api)
    {
        if (g_choices.empty())
            g_choices.resize(ROW_COUNT);
        int rows = 0;
        for (int p = 0; p < (int)(sizeof(PAGES) / sizeof(PAGES[0])); ++p)
        {
            const Page& page = PAGES[p];
            api->beginMod(api, page.id, page.title, NULL, NULL);
            api->info(api, text(std::string("mcm.page.") + lower(page.id), page.info));
            for (int k = 0; k < ROW_COUNT; ++k)
            {
                const Row& r = ROWS[k];
                if (r.page != page.page || (devRow(r) && !feedback::stage()))
                    continue;
                if (r.heading)
                {
                    api->section(api, text(std::string("mcm.h.") + r.section, r.label));
                    continue;
                }
                addRow(api, r);
                ++rows;
            }
        }
        char buf[96];
        sprintf_s(buf, "settings page: %d rows described (menu API %d)", rows, api->version);
        kit::log::line(buf);
    }
}

// Called by a settings menu plugin, if one is installed; never by us.
extern "C" __declspec(dllexport) void MCM_Describe(McmApi* api)
{
    try
    {
        if (api == NULL)
            return;
        if (api->version < 3 || api->beginMod == NULL || api->section == NULL || api->info == NULL || api->toggleFn == NULL
            || api->integerFn == NULL || api->numberFn == NULL || api->choiceFn == NULL || api->textFn == NULL)
        {
            kit::log::line("settings page: the installed settings menu is older than its API 3; the page is not shown (Nitoryu.ini works as before)");
            return;
        }
        if (cfg::iniPath().empty())
            return;     // the plugin did not get as far as its settings
        describe(api);
    }
    catch (...)
    {
        kit::log::error("Nitoryu: the settings page could not be described");
    }
}
