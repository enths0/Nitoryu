#include "kit/input.h"   // Windows.h with NOMINMAX first

#include <cstdio>
#include <cstdlib>
#include <string>

#include "Balance.h"
#include "core/Math.h"
#include "Settings.h"
#include "Tuning.h"

namespace
{
    // Fixed storage: the combat threads read a curve while the main loop may replace it after a load;
    // a short mix of old and new anchors is harmless, a moved vector is not.
    typedef core::Curve Curve;

    Curve g_curve[balance::CURVE_COUNT];
    float g_guard[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float g_dexBonus = 0.12f;       // cfg::DEX_BONUS
    std::string g_overrides;

    const cfg::Id SETTING[balance::CURVE_COUNT] = { cfg::OFF_HAND_SHARE, cfg::TWIN_SHARE, cfg::OFF_HAND_DAMAGE,
                                                    cfg::OFF_HAND_GUARD, cfg::COUNTER_CHANCE, cfg::PRESSURE_POINTS,
                                                    cfg::TEMPO_POINTS, cfg::MUGEN_COOLDOWN_MS, cfg::MUGEN_CHAIN,
                                                    cfg::SCISSOR_CHANCE, cfg::SPLIT_CHANCE };
    const char* KEY[balance::CURVE_COUNT] = { "off_hand_share", "twin_share", "off_hand_damage", "off_hand_guard",
                                              "counter_chance", "pressure_points", "tempo", "mugen_cooldown_ms",
                                              "mugen_chain", "scissor_chance", "split_chance" };

    bool parse(const char* s, Curve& c) { return core::parseCurve(s, c); }

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
            // A malformed curve (FCS or the ini) keeps the table's own.
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
            if (six)    // six numbers, tiers T0..T5
                g_guard[i] = cfg::item(cfg::DIVIDED_GUARD, i);
        if (!six)
        {
            // not six numbers: the table's own
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

    float at(CurveId id, float dp) { return core::curveAt(g_curve[id], dp); }

    float dividedGuard(int tier) { return g_guard[tier < 0 ? 0 : tier > 5 ? 5 : tier]; }

    float dexBonus() { return g_dexBonus; }

    std::string overridesText() { return g_overrides; }
}
