#pragma once

namespace core
{

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

    bool parseCurve(const char* text, Curve& out);
    float curveAt(const Curve& c, float dp);

    struct PathWeights
    {
        float skill, dex, attack, defence, ambi;
    };
    float pathOf(const PathWeights& w, float skillLow, float dex, float attack, float defence, float ambiEff);

    int tierOf(float dp, const float* thresholds, int count, int tiers);

    int capByAmbi(int t, float ambiEff, const float* minAmbi, int count);

    float speedFactor(float dp, float dex, float speed0, float fullDp, float from, float bonus);

    float clumsiness(float base, float slope, float dp);

    float overloadOf(float encumbranceMult);
    float overloadFactor(float overload, float floor);
    float armourFactor(float cut, float floor);
}
