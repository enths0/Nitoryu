// May these two weapons fight as a pair. One decision for every caller: the stance, the carried pair behind the
// inventory button, the stats window, the any-weapon button, left-hand fighting. No game header: the game side
// reads the facts (Stance.cpp) and calls this; it is unit-tested on the host.
//
// What it does not know: whether the weapons are drawn, the second weapon's mesh, the arms, the race's rig, the
// player's own stance switch. Those need the game and stay with the caller.
#pragma once

namespace core
{
    // The game's weapon categories the rule names (kenshi/Enums.h WeaponCategory; Stance.cpp asserts they match).
    const int CAT_BLUNT = 2;
    const int CAT_HACKERS = 4;

    // The WEAPON field `nitoryu dual wield`.
    enum WeaponRecord { RECORD_BY_CATEGORY = 0, RECORD_ALLOWED = 1, RECORD_REFUSED = 2 };

    struct WeaponFacts
    {
        int category;
        int record;                 // WeaponRecord; any other value counts as RECORD_BY_CATEGORY
        bool twoHandedOnly;         // Weapon::is2HandedOnly: heavy weapons, bows, polearms
        bool categoryAllowed;       // its category is on the allow-list (the setting `nitoryu weapon categories`)
    };

    // What a plugin's pair rule said about this pair.
    enum PluginAnswer { ANSWER_DEFAULT = 0, ANSWER_ALLOW = 1, ANSWER_REFUSE = 2, ANSWER_ALLOW_TWO_HANDED = 3 };

    struct PairFacts
    {
        WeaponFacts a, b;
        bool button;                // a player character with the any-weapon switch on
        int plugin;                 // PluginAnswer
    };

    enum PairReason
    {
        PAIR_OK,
        PAIR_WEAPON_REFUSED,        // a record says RECORD_REFUSED
        PAIR_PLUGIN_REFUSED,
        PAIR_TYPE_NOT_ALLOWED,      // a category off the list, not unlocked by its record, the button or a plugin
        PAIR_TWO_HANDED_ONLY
    };

    struct PairVerdict
    {
        bool allowed;
        PairReason reason;
        bool needsButton;           // refused with the any-weapon switch off, allowed with it on
    };

    // The order, first match decides:
    //   1. a weapon's own record refuses it: nothing below lifts that;
    //   2. a plugin refuses the pair;
    //   3. each weapon is let in by a plugin's answer, its record, its category, or the any-weapon switch
    //      (blunt weapons and hackers only);
    //   4. a two-handed-only weapon is refused unless a plugin answered ANSWER_ALLOW_TWO_HANDED.
    // The any-weapon switch never lets in a two-handed weapon, and a pair a plugin let in never needs it.
    PairVerdict pairVerdict(const PairFacts& f);

    // One weapon on its own, for left-hand fighting: let in by its record or its category, and one-handed.
    bool oneHandedAllowed(const WeaponFacts& w);

    // The log's name of a reason ("dual" for PAIR_OK). A string literal.
    const char* pairReasonName(PairReason r);
}
