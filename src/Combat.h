// The combat hooks. Each calls the original and adjusts its input or output for dual characters only;
// everyone else passes through.
#pragma once

class CombatTechniqueData;
class Character;

namespace combat
{
    // A Nitoryu move is a COMBAT_TECHNIQUE record with `nitoryu ...` fields (docs/FCS.md), ours or
    // another mod's; everything below reads the record's moves::Def (Moves.h).
    bool isOffHandTechnique(CombatTechniqueData* t);   // a Nitoryu move (the private pool, category 21)
    bool isTwinTechnique(CombatTechniqueData* t);      // ... hitting with both blades (a twin, a finisher, an opener)
    // `t` is the riposte this fighter's second blade picked last (cleared by its next pick).
    bool isRiposte(Character* c, CombatTechniqueData* t);
    // A technique's moves::Kind (Moves.h) and the clip it plays (-1: none).
    int moveKind(CombatTechniqueData* t);
    int armsModeOf(CombatTechniqueData* t);            // the arms::Mode it plays in (`nitoryu arms`, else by kind); -1: not ours
    int attackClip(CombatTechniqueData* t);            // the record's `nitoryu clip`
    // An attack clip kept on until its base track ends (the combat state ends before the track does):
    // the Dash, and a move's own full-body clip while its switch is on. -1: none.
    int tailClip(CombatTechniqueData* t);
    bool dashOn();                                     // [Moves] dash_slash
    void setDashOn(bool on);
    // Another mod's long dash (attack distance >= LONG_DASH_MIN, not ours) played by a dual fighter who
    // has the specials: the pose engine mirrors it onto the left arm (TWIN) and the off blade hits a
    // second time. [Moves] mirror_long_dash.
    bool isForeignLongDash(CombatTechniqueData* t);
    bool twinsForeignDash(Character* c, CombatTechniqueData* t);
    bool installHooks();            // false if any hook failed (logged)
    void applySettings();           // the [Moves] settings from cfg (installHooks, and after a change)
    void setEnabled(bool on);       // false: every hook passes through
    bool enabled();
    // One `fight` line per character in the last interval (stage only): impacts landed/blocked/missed,
    // damage after armour dealt and taken. Main loop, with the counts.
    void emitFight();
    // One `techniques total=N ours=M` line once the game built its pool.
    void logTechniques();
    // Main loop, each drawn dual fighter: keep our speed factor in the stored attack/block speed if
    // another writer put the game's plain value back.
    void enforceSpeed(Character* c);
    int missingTrackCount();        // rig|track pairs our moves skipped (compatibility line)
    // The character is being destroyed: its Cross/Dash cooldowns, carry-over, ledger, and its stats'
    // speed and reach records.
    void forget(Character* c);
}
