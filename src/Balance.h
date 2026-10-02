#pragma once

#include <string>

namespace balance
{
    enum CurveId
    {
        OFF_HAND_SHARE,
        TWIN_SHARE,
        OFF_HAND_DAMAGE,
        OFF_HAND_GUARD,
        COUNTER_CHANCE,
        PRESSURE_POINTS,
        TEMPO,
        MUGEN_COOLDOWN,
        MUGEN_CHAIN,
        SCISSOR_CHANCE,
        SPLIT_CHANCE,
        CURVE_COUNT
    };

    void applySettings();
    float at(CurveId id, float dp);
    float dividedGuard(int tier);
    float dexBonus();
    std::string overridesText();
}
