// The design numbers that are NOT settings.
//
// Every number a mod or a player may change is in SettingsDef.h (read through cfg::, Settings.h; FCS and
// Nitoryu.ini can replace it); docs/BALANCE.md explains them. The per-move numbers of the Cross, the Dash and
// the spin are fields of the moves' own COMBAT_TECHNIQUE records (`nitoryu roll`, `nitoryu cooldown ms`,
// `nitoryu min gap` / `max gap`). The constants below only fill Moves.h's fallback table, for one of our
// records that came without its fields (an older Nitoryu.mod).
#pragma once

namespace tuning
{
    // A curve: straight lines between (DP, value) anchors, flat outside.
    struct Anchor { float dp; float v; };

    // ---- special moves ----
    const float CROSS_CHANCE = 0.6f;            // Cross finisher when the target staggers
    // One finisher per stagger: without it the Cross chains on one staggering target.
    const double CROSS_COOLDOWN_MS = 3000.0;    // game time, per attacker
    // Dash slash. The AI keeps a distance from CharStats::longestReachingAttack (CombatClass::getMeiMax) and
    // only attacks from there, so a fighter who has the dash gets that raised to DASH_DISTANCE; an attack
    // chosen that far out is the dash.
    const float DASH_DISTANCE = 35.0f;          // the technique's attack distance
    const float DASH_CHANCE = 0.85f;
    // Per fighter, game time, for our Dash and for another mod's dash swapped for ours.
    const double DASH_COOLDOWN_MS = 12000.0;    // [Moves] dash_cooldown_ms
    const float DASH_MIN_GAP = 6.0f;            // range - reach: beyond a normal attack
    // The spin. Skill = DP (the pool's skill, the record's `min skill`). Its first cut lands after ~22 units
    // of the run, so it starts 6-24 beyond reach.
    const float SPIN_MIN_SKILL = 75.0f;          // [Moves] spin_min_skill
    const int SPIN_MIN_TIER = 4;                 // the capped tier: DP 75 and effective Dual Wielding 60
    const float SPIN_CHANCE = 1.0f;              // per pick in its window, when ready
    const double SPIN_COOLDOWN_MS = 20000.0;     // [Moves] spin_cooldown_ms, game time, per fighter
    const float SPIN_MIN_GAP = 6.0f;             // range - reach
    const float SPIN_MAX_GAP = 25.0f;            // = vanilla's longest attack (the engage distance while it is up)
    // The order of the two distance specials is the FCS field `chance` of their COMBAT_TECHNIQUE records
    // (docs/FCS.md): the one with the higher `chance` goes first, and while it is up (on, its skill, the blade
    // on, off its cooldown) the other one waits and the fighter engages from the first one's distance. Equal:
    // both are tried, the spin first. These stand in until the records are read.
    const float SPIN_PRIORITY = 4.0f;            // = the record's `chance` of the spin
    const float DASH_PRIORITY = 3.0f;            // = `chance` of the Dash slash
    // ---- internals ------------------------------------------------------------
    // Calls of the game's chooseAttack one dual pick may make (Cross 10, Dash 10, off hand 6, the repick
    // of a long two-handed move 6); the game's own pick when nothing of ours was taken is always made,
    // outside the budget.
    const int PICK_BUDGET = 32;
    const unsigned STANCE_CACHE_MS = 500;
    const int TIERS = 5;                         // T1..T5: the length of the tier lists (cfg::TIER_DP, cfg::TIER_MIN_AMBI)
}
