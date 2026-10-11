// The two top ranks' own mechanics (docs/BALANCE.md).
//
// Flow (rank 4): FLOW_ATTACKS enemy attacks aimed at a dual fighter (blocked, parried or landed) within
// FLOW_WINDOW_MS start flow; it lasts FLOW_DURATION_MS of game time, then FLOW_COOLDOWN_MS pass before attacks
// count again (none count during flow either). In flow: Dexterity counts more in the cut damage, the attack
// speed gets its own factor and a higher cap. A knockdown ends it.
//
// Mugen (rank 5): the double-hit cooldown shrinks by DP (the MUGEN_COOLDOWN curve), and a twin strike that
// lands may arm a chain: the fighter's next pick within MUGEN_CHAIN_WINDOW_MS is a twin again, whatever the
// cooldown.
#pragma once

#include <kenshi/util/hand.h>

#include <vector>

class Character;

namespace mastery
{
    void init();                 // the lock; after cfg::init
    void applySettings();        // from cfg (init, and after a change)
    void setEnabled(bool on);
    void forget(Character* c);   // Character destructor
}

namespace flow
{
    // _iHitYouAreYouHit (the victim's side), every impact, whatever its result.
    void onAttacked(Character* victim, Character* attacker);
    bool active(Character* c);
    float dexBonus(Character* c);       // added to balance::dexBonus() in the cut damage; 0 outside flow
    float speedFactor(Character* c);    // x the attack speed; 1 outside flow
    float capBonus(Character* c);       // + the attack speed cap; 0 outside flow
    // For the marks (Hud.cpp): every fighter in flow now, `left` = the share of it still to run (1..0).
    struct Shown
    {
        hand who;
        float left;
    };
    void shown(std::vector<Shown>& out);
}

namespace mugen
{
    // The fighter's double-hit cooldown: `base`, or the Mugen curve's when that is shorter.
    double doubleCooldownMs(Character* c, double base);
    void onTwinLanded(Character* attacker);         // hitByMeleeAttack: the left blade of a twin landed
    bool takeChain(Character* c, double now);       // chooseAttack: true (once) when a chain is armed and fresh
    // For the marks: every fighter with a chain armed and fresh, or taken a moment ago.
    void shown(std::vector<hand>& out);
}
