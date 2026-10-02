#pragma once

#include <cstring>

#include "Arms.h"
#include "Tuning.h"

namespace moves
{

    enum Kind { OTHER, SINGLE, TWIN, FINISHER, OPENER, KIND_COUNT };

    struct KindDef
    {
        const char* name;
        bool twin;
        bool special;
    };

    static const KindDef KINDS[KIND_COUNT] = {
        { "other",    false, false },
        { "single",   false, false },
        { "twin",     true,  false },
        { "finisher", true,  true  },
        { "opener",   true,  true  },
    };

    enum ArmsBy { ARMS_BY_KIND = 0, ARMS_MIRRORED = 1, ARMS_BOTH = 2, ARMS_MAIN_ONLY = 3 };

    struct Def
    {
        Kind kind;
        int arms;
        int clip;
        bool needsClip;
        bool echo;
        float roll;
        double cooldownMs;
        float minGap;
        float maxGap;
    };

    inline Def noMove()
    {
        Def d = { OTHER, ARMS_BY_KIND, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f };
        return d;
    }

    inline arms::Mode armsMode(const Def& d)
    {
        switch (d.arms)
        {
        case ARMS_MIRRORED:  return arms::SWAP;
        case ARMS_BOTH:      return arms::TWIN;
        case ARMS_MAIN_ONLY: return arms::HOLD;
        default:             return KINDS[d.kind].twin ? arms::TWIN : arms::SWAP;
        }
    }

    struct ClipDef
    {
        arms::Clip clip;
        const char* name;
        const char* iniKey;
        const char* legacyKey;
        bool fullBody;
        const char* baseTrack;
    };

#define NITORYU_CLIP_DEF(e, track, name, ini, legacy, counter, full, base) { arms::e, name, ini, legacy, full, base },
    static const ClipDef CLIPS[arms::CLIP_COUNT] = { NITORYU_L3_CLIPS(NITORYU_CLIP_DEF) };
#undef NITORYU_CLIP_DEF

    inline bool fullBody(int clip) { return clip >= 0 && clip < arms::CLIP_COUNT && CLIPS[clip].fullBody; }

    inline int clipByName(const char* name)
    {
        if (name == NULL || name[0] == '\0')
            return -1;
        for (int k = 0; k < arms::CLIP_COUNT; ++k)
            if (strcmp(CLIPS[k].name, name) == 0)
                return k;
        return -1;
    }

    inline bool twinClip(int clip)
    {
        return clip == arms::CLIP_DASH || clip == arms::CLIP_TWIN_SWEEP || clip == arms::CLIP_SPIN;
    }

    inline bool longTail(int clip) { return clip == arms::CLIP_DASH || clip == arms::CLIP_SPIN; }

    inline bool sideTravel(int clip) { return clip == arms::CLIP_SPIN; }

    struct SheatheDef
    {
        float mainHomeS;
        float offHomeS;
        float lengthS;
    };

    inline SheatheDef sheathe(int clip)
    {
        SheatheDef back = { 38.0f / 30.0f, 48.0f / 30.0f, 53.0f / 30.0f };
        SheatheDef onHip = { 38.0f / 30.0f, 41.0f / 30.0f, 45.0f / 30.0f };
        SheatheDef chiburi = { 50.0f / 30.0f, 60.0f / 30.0f, 65.0f / 30.0f };
        return clip == arms::CLIP_SHEATHE_HIP ? onHip
            : clip == arms::CLIP_CHIBURI || clip == arms::CLIP_CHIBURI_HIP ? chiburi : back;
    }

    struct Record
    {
        int number;
        const char* key;
        const char* tag;
        const char* source;
        Def def;
    };

    static const Record RECORDS[] = {
        { 1,  "nitoryu-offhand-strike",      "", "40-gamedata.quack",  { SINGLE, 0, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f } },
        { 2,  "nitoryu-twin-strike",         "", "38-gamedata.quack",  { SINGLE, 0, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f } },
        { 3,  "nitoryu-twin-strike-mid",     "", "45-gamedata.quack",  { SINGLE, 0, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f } },
        { 4,  "nitoryu-offhand-cut-static",  "", "196-gamedata.base",  { SINGLE, 0, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f } },
        { 5,  "nitoryu-offhand-down-static", "", "128-gamedata.quack", { SINGLE, 0, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f } },
        { 6,  "nitoryu-twin-overhead",       "", NULL,                 { TWIN,   0, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f } },
        { 7,  "nitoryu-cross-slash",         "", NULL,                 { TWIN,   0, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f } },
        { 9,  "nitoryu-cross-finisher",      "cross", NULL,
          { FINISHER, 0, arms::CLIP_CROSS, false, false, tuning::CROSS_CHANCE, tuning::CROSS_COOLDOWN_MS, 0.0f, 0.0f } },
        { 10, "nitoryu-dash-slash",          "dash", NULL,
          { OPENER, 0, arms::CLIP_DASH, false, false, tuning::DASH_CHANCE, tuning::DASH_COOLDOWN_MS, tuning::DASH_MIN_GAP,
            tuning::DASH_DISTANCE } },

        { 13, "nitoryu-offhand-rising-cut",  "", NULL,                 { SINGLE, 0, arms::CLIP_OFF_RISING, true, false, 1.0f, 0.0, 0.0f, 0.0f } },

        { 16, "nitoryu-twin-sweep",          "", NULL,                 { TWIN,   0, arms::CLIP_TWIN_SWEEP, true, false, 1.0f, 0.0, 0.0f, 0.0f } },

        { 12, "nitoryu-spin",                "spin", NULL,
          { OPENER, 0, arms::CLIP_SPIN, true, true, tuning::SPIN_CHANCE, tuning::SPIN_COOLDOWN_MS, tuning::SPIN_MIN_GAP,
            tuning::SPIN_MAX_GAP } },
    };
    static const int RECORD_COUNT = sizeof(RECORDS) / sizeof(RECORDS[0]);

    inline const Record* recordOfNumber(int number)
    {
        for (int i = 0; i < RECORD_COUNT; ++i)
            if (RECORDS[i].number == number)
                return &RECORDS[i];
        return NULL;
    }
}
