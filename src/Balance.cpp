#include "kit/input.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "Balance.h"
#include "Settings.h"
#include "Tuning.h"

namespace
{

    const int MAX_ANCHORS = 16;

    struct Curve
    {
        tuning::Anchor a[MAX_ANCHORS];
        int n;
    };

    Curve g_curve[balance::CURVE_COUNT];
    float g_guard[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float g_dexBonus = 0.12f;
    std::string g_overrides;

    const cfg::Id SETTING[balance::CURVE_COUNT] = { cfg::OFF_HAND_SHARE, cfg::TWIN_SHARE, cfg::OFF_HAND_DAMAGE,
                                                    cfg::OFF_HAND_GUARD, cfg::COUNTER_CHANCE, cfg::PRESSURE_POINTS,
                                                    cfg::TEMPO_POINTS, cfg::MUGEN_COOLDOWN_MS, cfg::MUGEN_CHAIN,
                                                    cfg::SCISSOR_CHANCE, cfg::SPLIT_CHANCE };
    const char* KEY[balance::CURVE_COUNT] = { "off_hand_share", "twin_share", "off_hand_damage", "off_hand_guard",
                                              "counter_chance", "pressure_points", "tempo", "mugen_cooldown_ms",
                                              "mugen_chain", "scissor_chance", "split_chance" };

    bool parse(const char* s, Curve& c)
    {
        Curve out;
        out.n = 0;
        const char* p = s;
        while (*p)
        {
            while (*p == ' ' || *p == ',')
                ++p;
            if (!*p)
                break;
            char* end = NULL;
            double dp = strtod(p, &end);
            if (end == p || *end != ':')
                return false;
            p = end + 1;
            double v = strtod(p, &end);
            if (end == p)
                return false;
            p = end;
            tuning::Anchor a = { (float)dp, (float)v };
            if (out.n >= MAX_ANCHORS || (out.n > 0 && a.dp <= out.a[out.n - 1].dp))
                return false;
            out.a[out.n++] = a;
        }
        if (out.n == 0)
            return false;

        for (int i = 0; i < out.n; ++i)
            c.a[i] = out.a[i];
        c.n = out.n;
        return true;
    }

    void note(const char* key, cfg::Id id, bool ok)
    {
        if (cfg::source(id) == cfg::DEFAULT && ok)
            return;
        g_overrides += (g_overrides.empty() ? "" : ",") + std::string(key)
                       + (cfg::source(id) == cfg::FCS ? "(fcs)" : "") + (ok ? "" : "(bad)");
    }
}

namespace balance
{
    void applySettings()
    {
        g_overrides.clear();
        for (int i = 0; i < CURVE_COUNT; ++i)
        {

            bool ok = parse(cfg::text(SETTING[i]).c_str(), g_curve[i]);
            if (!ok)
                parse(cfg::defaultText(SETTING[i]), g_curve[i]);
            note(KEY[i], SETTING[i], ok);
        }
        float bonus = cfg::f(cfg::DEX_BONUS);
        g_dexBonus = bonus < 0.0f ? 0.0f : bonus > 1.0f ? 1.0f : bonus;
        note("dex_bonus", cfg::DEX_BONUS, true);
        bool six = cfg::items(cfg::DIVIDED_GUARD) == 6;
        for (int i = 0; i < 6; ++i)
            if (six)
                g_guard[i] = cfg::item(cfg::DIVIDED_GUARD, i);
        if (!six)
        {

            const char* p = cfg::defaultText(cfg::DIVIDED_GUARD);
            for (int i = 0; i < 6 && *p; ++i)
            {
                char* end = NULL;
                g_guard[i] = (float)strtod(p, &end);
                p = *end == ',' ? end + 1 : end;
            }
        }
        note("divided_guard", cfg::DIVIDED_GUARD, six);
    }

    float at(CurveId id, float dp)
    {
        const Curve& c = g_curve[id];
        int n = c.n;
        if (n <= 0)
            return 0.0f;
        if (dp <= c.a[0].dp)
            return c.a[0].v;
        for (int i = 1; i < n; ++i)
            if (dp <= c.a[i].dp)
                return c.a[i - 1].v + (c.a[i].v - c.a[i - 1].v) * (dp - c.a[i - 1].dp) / (c.a[i].dp - c.a[i - 1].dp);
        return c.a[n - 1].v;
    }

    float dividedGuard(int tier) { return g_guard[tier < 0 ? 0 : tier > 5 ? 5 : tier]; }

    float dexBonus() { return g_dexBonus; }

    std::string overridesText() { return g_overrides; }
}
