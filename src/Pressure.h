// Pressure (docs/BALANCE.md).
//
// Each hit of a dual fighter of the pressure rank (the master) that a target blocks adds a stack on that
// target; one stack fades per few seconds of game time without a new one, and all go when the target lands
// a hit on the master. Kenshi decides who swings and who blocks by an initiative contest
// (CombatClassAI::whoAttacksYouOrMe compares attack chances; the loser blocks). Between the master and a
// pressured target each stack counts as [Pressure] points attack levels for the master, so the target ends
// up blocking more and attacking less.
#pragma once

#include <kenshi/Enums.h>
#include <kenshi/util/hand.h>

#include <vector>

class Character;

namespace pressure
{
    void init();                 // after cfg::init
    void applySettings();        // the pressure and tempo settings from cfg (init, and after a change)
    bool installHooks();         // CombatClassAI::whoAttacksYouOrMe
    void setEnabled(bool on);
    // _iHitYouAreYouHit (victim's side, any victim): r is the hit's result.
    void onImpact(Character* victim, Character* attacker, HitMaterialType r);
    void forget(Character* c);   // Character destructor
    // Stacks on `target` put there by `master` (0 if none), for poses and logs.
    int stacks(Character* target, Character* master);
    int maxStacks();             // [Pressure] max_stacks
    // For the marks (Hud.cpp): every target with stacks on it now, and who put them there.
    struct Shown
    {
        hand target, by;
        int stacks;
    };
    void shown(std::vector<Shown>& out);
}
