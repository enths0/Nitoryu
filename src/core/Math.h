// Nitoryu's pure math, with no game, KenshiLib, Ogre or Windows header, so it compiles on the host and is
// unit-tested there. The game side reads its inputs (stats, settings) and calls these; the numbers live in
// SettingsDef.h.
#pragma once

namespace core
{
    // A curve "dp:value,dp:value,..." in rising DP: straight lines between the anchors, flat outside.
    const int CURVE_MAX = 16;
    struct Anchor
    {
        float dp, v;
    };
    struct Curve
    {
        Anchor a[CURVE_MAX];
        int n;
    };
    // False, and `out` untouched, on anything malformed (a missing ':', a DP not rising, no anchor, too many).
    // The count is written last, so a reader on another thread mid-update sees a valid prefix.
    bool parseCurve(const char* text, Curve& out);
    float curveAt(const Curve& c, float dp);

    // The Path (DP, 0-100): a weighted average of five inputs, each capped at 100 (docs/BALANCE.md).
    struct PathWeights
    {
        float skill, dex, attack, defence, ambi;
    };
    float pathOf(const PathWeights& w, float skillLow, float dex, float attack, float defence, float ambiEff);

    // The rank by Path: the highest tier whose threshold the Path reaches (thresholds[0] = T1); a list shorter
    // than `tiers` leaves the missing tiers out of reach.
    int tierOf(float dp, const float* thresholds, int count, int tiers);
    // The highest tier at most `t` that the effective Ambidexterity allows (minAmbi[0] = T1).
    int capByAmbi(int t, float ambiEff, const float* minAmbi, int count);

    // Attack and block speed factor: a beginner is slower (speed0 at DP 0, 1 at fullDp), a master with high
    // Dexterity faster, up to 1 + bonus at Dexterity and DP 100, growing from `from`.
    float speedFactor(float dp, float dex, float speed0, float fullDp, float from, float bonus);
    // Melee Attack and Defence points a dual fighter loses: base - slope x DP, never below 0.
    float clumsiness(float base, float slope, float dp);

    // Effective Ambidexterity: the overload and armour factors (each with its floor).
    // encumbranceMult: the game's (1 = not overloaded). cut: the sum of the worn armour pieces' cuts;
    // the factor is 1 - cut, never below floor nor above 1.
    float overloadOf(float encumbranceMult);
    float overloadFactor(float overload, float floor);
    float armourFactor(float cut, float floor);
}
