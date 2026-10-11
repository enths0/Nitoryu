// Nitoryu's API for other plugins (api/NitoryuApi.h, docs/API.md): the function table, the addons that take it,
// and the events they subscribe to.
//
// Two ways in. An addon exports NitoryuAddon_Init and Nitoryu calls it with the table, once, on the main loop;
// a plugin that only reads finds the export Nitoryu_GetApi. Both stay closed until the plugin started and the
// setting `nitoryu addons` ([Compat] addons) was read.
//
// With no subscriber every call here returns at once: a game without an addon runs none of this.
#pragma once

#include "Stance.h"

class Character;
class Weapon;

namespace api
{
    void init();                        // the lock; before the first hook goes in
    void start(const char* version);    // the plugin started with its core hooks: the table may open
    // Main loop, every frame: opens the table once the settings are read, calls the addons' init, and hands
    // the queued events to their subscribers.
    void tick();

    // Any thread. A fresh evaluation of a character's stance (stance::evaluate, never a cached one): the
    // changes since the last one become events (dual on / off, rank, Dual Wielding).
    void observe(Character* c, const stance::Info& s);
    // Any thread. The moments the combat hooks see. Queued; the tick delivers them.
    void offHandHit(Character* attacker, Character* victim, Weapon* off, int moveKind, const char* track);
    void offHandParry(Character* defender, Character* attacker);
    void special(Character* c, int moveKind, const char* tag);
    void forget(Character* c);          // the character is being destroyed

    // The plugins' pair rules. Any thread, inline: stance::pairVerdict asks them about a pair that Nitoryu's
    // own rule answered with `own`. Returns a core::PluginAnswer; `t` gets the numbers of the rule that let
    // the pair in. A rule is never asked from inside a rule.
    // The plugins' move filters. Any thread, inline: the move picker asks them about a record that came up for
    // a pair. Returns a NitoryuFilterVerdict (0 no opinion, 1 allow, 2 refuse).
    struct MoveAsk
    {
        Character* c;
        Weapon* main;
        Weapon* off;
        void* record;               // GameData*
        const char* recordId;
        const char* track;
        int kind, categoryMain, categoryOff;
        bool mainHand, twoHandedPair, listed;
    };
    bool hasMoveFilters();
    int moveFilter(const MoveAsk& m);

    bool hasPairRules();
    int pairRules(Character* c, Weapon* a, Weapon* b, const core::PairFacts& f, const core::PairVerdict& own,
                  bool player, stance::PairTuning& t);
}
