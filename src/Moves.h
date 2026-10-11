// Nitoryu's moves in one place. A move is DATA: a COMBAT_TECHNIQUE record with `nitoryu ...` fields
// (docs/FCS.md, declared in mod/fcs.def), ours or another mod's. Combat (the registry, the picks), Arms (the
// clips) and Visual (the switches, the attack clip of a playing technique) read a moves::Def. The RECORDS table
// below is only the fallback for one of OUR records that came without the fields (an older Nitoryu.mod under a
// newer DLL).
#pragma once

#include <cstring>

#include "Arms.h"
#include "Tuning.h"

namespace moves
{
    // What a technique is to Nitoryu (the FCS field `nitoryu move`: 1 strike, 2 twin, 3 finisher, 4 opener).
    // OTHER: not ours (vanilla's, another mod's plain technique).
    enum Kind { OTHER, SINGLE, TWIN, FINISHER, OPENER, KIND_COUNT };

    struct KindDef
    {
        const char* name;           // in `draw_miss` / `offhand_pick` lines
        bool twin;                  // both blades hit in one swing (a double hit: the shared cooldown)
        bool special;               // rank and Dual Wielding gated, never a plain off-hand pick
    };

    static const KindDef KINDS[KIND_COUNT] = {
        { "other",    false, false },
        { "single",   false, false },
        { "twin",     true,  false },
        { "finisher", true,  true  },   // on a staggering or fully pressured target (the Cross)
        { "opener",   true,  true  },   // from a distance window (the Dash, the spin)
    };

    // The FCS field `nitoryu arms`: how the technique's track is played.
    enum ArmsBy { ARMS_BY_KIND = 0, ARMS_MIRRORED = 1, ARMS_BOTH = 2, ARMS_MAIN_ONLY = 3 };

    // One move, as its record describes it.
    struct Def
    {
        Kind kind;
        int arms;                   // ArmsBy
        int clip;                   // arms::Clip laid over the track while it swings, -1: none
        bool needsClip;             // not drawn while that clip's [Animations] switch is off
        bool echo;                  // each impact is followed by the second blade's (the spin: 2 impacts, 4 hits)
        float roll;                 // finisher / opener: chance per pick when everything else fits
        double cooldownMs;          // any move, a plain strike too: per fighter, game time
        float minGap;               // opener: range - reach window
        float maxGap;               // ... and the engage distance while it is ready
    };

    inline Def noMove()
    {
        Def d = { OTHER, ARMS_BY_KIND, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f };
        return d;
    }

    // The arm mode a move plays in (Arms.h): a strike mirrors the whole body (SWAP), the others play on both
    // blades (TWIN), unless the record says otherwise.
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

    // The clips (Nitoryu_L3_<sex>.skeleton), in arms::Clip order. Each has an [Animations] switch (on by
    // default); an older ini may still hold [Test] <legacy>.
    struct ClipDef
    {
        arms::Clip clip;
        const char* name;           // the load line's "L3 TEST ON" list; the FCS field `nitoryu clip` names a clip by it
        const char* iniKey;         // [Animations] key (shared: the two draws, the two run holds)
        const char* legacyKey;      // the older [Test] key
        bool fullBody;              // also drives the pelvis, the legs and the root's turn and height
        const char* baseTrack;      // the vanilla track the clip was authored on (same length), or NULL
    };

    // One row per clip, generated into ClipsGen.h.
#define NITORYU_CLIP_DEF(e, track, name, ini, legacy, counter, full, base) { arms::e, name, ini, legacy, full, base },
    static const ClipDef CLIPS[arms::CLIP_COUNT] = { NITORYU_L3_CLIPS(NITORYU_CLIP_DEF) };
#undef NITORYU_CLIP_DEF

    inline bool fullBody(int clip) { return clip >= 0 && clip < arms::CLIP_COUNT && CLIPS[clip].fullBody; }

    inline int clipByName(const char* name)     // -1: no such clip (or empty)
    {
        if (name == NULL || name[0] == '\0')
            return -1;
        for (int k = 0; k < arms::CLIP_COUNT; ++k)
            if (strcmp(CLIPS[k].name, name) == 0)
                return k;
        return -1;
    }

    // A tailed attack clip played over both arms (TWIN mode); the others play over the off blade's
    // swing (SWAP).
    inline bool twinClip(int clip)
    {
        return clip == arms::CLIP_DASH || clip == arms::CLIP_TWIN_SWEEP || clip == arms::CLIP_SPIN
            || clip == arms::CLIP_TWIN_SHEAR || clip == arms::CLIP_HEAD_WRAP
            || clip == arms::CLIP_FIGURE_EIGHT || clip == arms::CLIP_THREE_CUTS
            // both blades cutting over a stepping base: no spotShift, the travel is the base's own
            || clip == arms::CLIP_TWIN_BODY_CUT || clip == arms::CLIP_TWO_STEP;
    }

    // The clips on the running base (the Dash, the spin): their tail wins over anything but a block (Visual.cpp)
    // and their root carries our own forward travel as an offset from the base's (Arms.cpp).
    inline bool longTail(int clip) { return clip == arms::CLIP_DASH || clip == arms::CLIP_SPIN; }
    // The clips whose root carries an offset from the base track's ground position: the long-tailed ones, and
    // the moves "from the spot", whose hips move over the planted feet (the weight from leg to leg, about 2
    // units). Without the offset the legs, solved for the moved hips, would slide the feet.
    inline bool spotShift(int clip)
    {
        return clip == arms::CLIP_ONE_TWO || clip == arms::CLIP_OFF_BACKHAND || clip == arms::CLIP_OFF_BELLY_CUT
            || clip == arms::CLIP_THREE_CUTS || clip == arms::CLIP_DOUBLE_CHOP
            || clip == arms::CLIP_MAIN_CHOP || clip == arms::CLIP_OFF_SHOULDER_CUT || clip == arms::CLIP_MAIN_LEVEL_CUT
            || clip == arms::CLIP_OFF_HIGH_CUT || clip == arms::CLIP_MAIN_SHORT_CUT
            || clip == arms::CLIP_OFF_LEG_CUT || clip == arms::CLIP_X_CUT || clip == arms::CLIP_TWIN_SWEEP
            || clip == arms::CLIP_TWIN_SHEAR;
    }
    inline bool rootTravel(int clip) { return longTail(clip) || spotShift(clip); }
    // ... and the root's sideways offset too: the spin's drift (the side step and the curve back), the shift over the
    // feet of the clips from the spot; 0 at both ends
    inline bool sideTravel(int clip) { return clip == arms::CLIP_SPIN || spotShift(clip); }

    // The dual sheathes: when each blade is home in its sheath and the clip's length, seconds of game time
    // from the clip's start. The chiburi is the flick plus the whole sheathe. The blades go home on the
    // game's own sheaths and the clips play on until the arms are down. A re-timed clip needs its numbers
    // changed here too.
    struct SheatheDef
    {
        float mainHomeS;
        float offHomeS;
        float lengthS;
    };

    inline SheatheDef sheathe(int clip)
    {
        SheatheDef back = { 35.0f / 30.0f, 45.0f / 30.0f, 66.0f / 30.0f };
        SheatheDef onHip = { 34.0f / 30.0f, 38.0f / 30.0f, 62.0f / 30.0f };
        SheatheDef chiburi = { 49.0f / 30.0f, 59.0f / 30.0f, 80.0f / 30.0f };
        SheatheDef chiburiHip = { 49.0f / 30.0f, 60.0f / 30.0f, 74.0f / 30.0f };
        return clip == arms::CLIP_SHEATHE_HIP ? onHip : clip == arms::CLIP_CHIBURI ? chiburi
            : clip == arms::CLIP_CHIBURI_HIP ? chiburiHip : back;
    }

    // The dual draw's moment "blades in the hands" (frame 19 of 44 at 30 fps): until then both blades hang
    // on the back (Visual.cpp, the dual draw's hold). The hip draw has the same grip frame. A re-timed clip
    // needs its number changed here too.
    inline float drawGripS() { return 19.0f / 30.0f; }

    // ---- the fallback: our records "<number>-Nitoryu.mod" ----
    // 8 (the ANIMATION_FILE) and 11 (the NPC opt-in squad) are not techniques.
    struct Record
    {
        int number;
        const char* key;            // the record's ID-lock key
        const char* tag;            // counters and log lines: special_<tag>, <tag>_off_landed ... ("" = the kind's name)
        const char* source;         // a single's vanilla source record: its timing follows it live
        Def def;
    };

    static const Record RECORDS[] = {
        { 1,  "nitoryu-offhand-strike",      "", "40-gamedata.quack",  { SINGLE, 0, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f } },
        { 2,  "nitoryu-twin-strike",         "", "38-gamedata.quack",  { SINGLE, 0, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f } },   // the off-hand downward cut
        { 3,  "nitoryu-twin-strike-mid",     "", "45-gamedata.quack",  { SINGLE, 0, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f } },   // the off-hand combo
        { 4,  "nitoryu-offhand-cut-static",  "", "196-gamedata.base",  { SINGLE, 0, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f } },
        { 5,  "nitoryu-offhand-down-static", "", "128-gamedata.quack", { SINGLE, 0, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f } },
        { 6,  "nitoryu-twin-overhead",       "", NULL,                 { TWIN,   0, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f } },   // twins set their own timing
        { 7,  "nitoryu-cross-slash",         "", NULL,                 { TWIN,   0, -1, false, false, 1.0f, 0.0, 0.0f, 0.0f } },
        { 9,  "nitoryu-cross-finisher",      "cross", NULL,
          { FINISHER, 0, arms::CLIP_X_CUT, false, false, tuning::CROSS_CHANCE, tuning::CROSS_COOLDOWN_MS, 0.0f, 0.0f } },
        { 10, "nitoryu-dash-slash",          "dash", NULL,
          { OPENER, 0, arms::CLIP_DASH, false, false, tuning::DASH_CHANCE, tuning::DASH_COOLDOWN_MS, tuning::DASH_MIN_GAP,
            tuning::DASH_DISTANCE } },
        // the twin sweep, a twin with its own full-body clip
        { 16, "nitoryu-twin-sweep",          "", NULL,                 { TWIN,   0, arms::CLIP_TWIN_SWEEP, true, false, 1.0f, 0.0, 0.0f, 0.0f } },
        // the spin, a full-body clip; two impacts of the right blade, each followed by the left's (echo)
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
