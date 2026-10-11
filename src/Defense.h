// The off-hand guard and the counter.
//
// Off-hand guard: when vanilla lets a hit land (_iHitYouAreYouHit returns HIT_FLESH), the second blade may
// still parry it and the hook returns HIT_SWORD, so hitByMeleeAttack does the full vanilla block. Two triggers:
//   combo guard - a later impact (comboID >= 1) of a technique whose earlier
//                 impact already reached this defender; 0.5 x G x bc / base
//   group guard - an attacker other than the main guard's foe;  G x bc / base
// At most one off-hand parry per defender per [Guard] cooldown_ms of game time.
// Counter: after any successful block, the next attack pick within [Guard] counter_window_ms is an
// off-hand riposte.
#pragma once

class Character;
class CombatTechniqueData;

namespace defense
{
    void init();                 // after cfg::init
    void applySettings();        // the guard and counter settings from cfg (init, and after a change)
    bool installHooks();         // _iHitYouAreYouHit, initialiseBlock
    void setEnabled(bool on);
    void tick();                 // main loop: keeps the game clock fine-grained

    // Game time in ms: wall time x the game speed, stopped while paused.
    double clockMs();
    float headingDot(Character* me, Character* who);   // cos(game heading, direction to who); 2 = unknown
    double frameMs();       // this frame's game clock (set by tick), lock-free, for the pose hook

    // CONSTANTS from the `GLOBAL CONSTANTS` record (any build), read on the main loop; damage values
    // already x `damage multiplier`, as the game's own globals are.
    struct Constants
    {
        bool found;
        float blockBase;
        float cut1, cut99;
        float blunt1, blunt99;
        float damageMult;
    };
    void readConstants();       // main loop (every counts interval)
    Constants constants();

    // hitByMeleeAttack wraps its original in this, so the _iHitYouAreYouHit hook
    // (called inside it, on the same thread) knows the technique and comboID.
    struct HitScope
    {
        HitScope(Character* victim, Character* attacker, CombatTechniqueData* attack, int comboID);
        ~HitScope();
    private:
        Character* prevVictim;
        Character* prevAttacker;
        CombatTechniqueData* prevAttack;
        int prevCombo;
        Character* prevParry;
    };

    // chooseAttack: true (once) when a counter is armed for this fighter.
    bool takeCounter(Character* c);
    float counterChance(float dp);   // [Guard] counter_chance, else the curve by DP
    // Inside hitByMeleeAttack: true if this victim's hit was just parried by the off hand.
    bool offParried(Character* victim);
    // Game time (clockMs) of this fighter's last off-hand parry, or < 0 (main loop, for the parry pose).
    double lastParryMs(Character* c);
    // Game time (clockMs) of the last hit this dual fighter's main hand blocked, or < 0.
    double lastMainBlockMs(Character* c);
    // That parry / that blocked hit was against an attacker behind the fighter, of a fighter whose rank
    // guards the back (the back guard clips, Arms.cpp).
    bool lastParryBack(Character* c);
    bool lastMainBlockBack(Character* c);
    void forget(Character* c);      // Character destructor
}
