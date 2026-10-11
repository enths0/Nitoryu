// The balance curves at run time.
//
// SettingsDef.h holds the design defaults; FCS (GLOBAL CONSTANTS, the "nitoryu curve ..." fields) or
// Nitoryu.ini [Balance] may replace any curve with its own anchors, "dp:value,dp:value,..." (straight
// lines between anchors, flat outside), and the divided-guard penalty per tier with "t0,t1,t2,t3,t4,t5".
// docs/BALANCE.md lists the keys. A malformed value keeps the default and shows as "(bad)" on the load line.
#pragma once

#include <string>

namespace balance
{
    enum CurveId
    {
        OFF_HAND_SHARE,     // cfg::OFF_HAND_SHARE, [Balance] off_hand_share
        TWIN_SHARE,         // twin_share
        OFF_HAND_DAMAGE,    // off_hand_damage
        OFF_HAND_GUARD,     // off_hand_guard
        COUNTER_CHANCE,     // counter_chance
        PRESSURE_POINTS,    // pressure_points
        TEMPO,              // tempo: initiative levels a dual fighter always has
        MUGEN_COOLDOWN,     // the double-hit cooldown at rank 5, ms ([Mugen] cooldown_ms)
        MUGEN_CHAIN,        // chance a landed twin chains another ([Mugen] chain)
        SCISSOR_CHANCE,     // chance the second blade of a twin takes the first one's body part ([Scissor] chance)
        SPLIT_CHANCE,       // chance the left blade of a twin goes to a flanker ([Split] chance)
        CURVE_COUNT
    };

    void applySettings();                           // from cfg; at start (after cfg::init) and after a change
    float at(CurveId id, float dp);
    float dividedGuard(int tier);                   // cfg::DIVIDED_GUARD
    float dexBonus();                               // cfg::DEX_BONUS
    std::string overridesText();                    // the values FCS or the ini replaced, for the load line
}
