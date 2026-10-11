// Nitoryu's pure math (Math.h). No game header here, ever: this file also builds on the host for the unit tests.
#include "Math.h"

#include <cstdlib>

namespace
{
    float clampTo(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    float cap100(float v) { return clampTo(v, 0.0f, 100.0f); }
}

namespace core
{
    bool parseCurve(const char* text, Curve& c)
    {
        Curve out;
        out.n = 0;
        const char* p = text != NULL ? text : "";
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
            Anchor a = { (float)dp, (float)v };
            if (out.n >= CURVE_MAX || (out.n > 0 && a.dp <= out.a[out.n - 1].dp))
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

    float curveAt(const Curve& c, float dp)
    {
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

    float pathOf(const PathWeights& w, float skillLow, float dex, float attack, float defence, float ambiEff)
    {
        return clampTo(w.skill * cap100(skillLow) + w.dex * cap100(dex) + w.attack * cap100(attack)
                       + w.defence * cap100(defence) + w.ambi * cap100(ambiEff), 0.0f, 100.0f);
    }

    int tierOf(float dp, const float* thresholds, int count, int tiers)
    {
        int t = 0;
        for (int i = 0; i < tiers; ++i)
            if (dp >= (i < count ? thresholds[i] : 1000.0f))
                t = i + 1;
        return t;
    }

    int capByAmbi(int t, float ambiEff, const float* minAmbi, int count)
    {
        while (t > 0 && ambiEff < (t - 1 < count ? minAmbi[t - 1] : 0.0f))
            --t;
        return t;
    }

    float speedFactor(float dp, float dex, float speed0, float fullDp, float from, float bonus)
    {
        if (dp < fullDp)
            return speed0 + (1.0f - speed0) * dp / fullDp;
        float span = 100.0f - from;
        if (span <= 0.0f)
            return 1.0f + bonus;
        float dexN = clampTo((dex - from) / span, 0.0f, 1.0f);
        float dpN = clampTo((dp - from) / span, 0.0f, 1.0f);
        return 1.0f + bonus * dexN * dpN;
    }

    float clumsiness(float base, float slope, float dp)
    {
        float c = base - slope * dp;
        return c > 0.0f ? c : 0.0f;
    }

    bool finiteF(float v) { return v == v && v < 3.0e38f && v > -3.0e38f; }

    float overloadOf(float m)
    {
        if (!finiteF(m) || !(m < 0.999f))
            return 0.0f;
        if (m < 0.0f)
            m = 0.0f;
        return 1.0f / (0.05f + 0.95f * m) - 1.0f;
    }

    float overloadFactor(float overload, float floor)
    {
        if (overload <= 0.0f)
            return 1.0f;
        float f = overload <= 0.05f ? 1.0f - 5.5f * overload : 0.75f - 0.5f * overload;
        return f < floor ? floor : f;
    }

    float armourFactor(float cut, float floor)
    {
        if (!finiteF(cut) || !(cut > 0.0f))
            return 1.0f;
        float f = 1.0f - cut;
        return f < floor ? floor : f;
    }
}
