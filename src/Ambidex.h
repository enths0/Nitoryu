// Dual Wielding (called Ambidexterity, A, in the code), off-hand XP and the stats line.
//
// A is per character. It is seeded at AMB_SEED x the best allowed weapon skill
// the first time it is needed (stats window, dual stance), grows only in dual
// stance: 0.5 x the raw XP amount vanilla gives the weapon skill in each xpMelee,
// x clamp(1 + (S - A)/50, 0.25, 2), applied through vanilla's increaseStat at A's
// own level (each level slower than the last, as for any skill),
// and is saved in the character's STATS GameData (fdata "NIT_ambidexterity")
// through CharStats::serialise / updateStats, so saves, loads and Import keep it.
// Off-hand exchanges (an off-hand impact, or an off-hand parry) give their
// weapon XP to the off weapon's skill instead of the main one.
#pragma once

#include <map>
#include <string>

class Character;
class CombatTechniqueData;

namespace ambidex
{
    void init();                 // lock, [Stats] from Nitoryu.ini
    bool installHooks();         // the Ambidexterity group: save, load, growth, stats window
    bool installLifetimeHook();  // Character destructor (core): forgets every per-character record
    void setEnabled(bool on);

    // A for this character; the first call stores the seed (AMB_SEED x its best
    // allowed weapon skill) if the save held none.
    float get(Character* c);
    // The character's own A (saved, or the seed), ignoring [Test] ambidexterity: what grows and is saved.
    float stored(Character* c);
    // The pair dummy raised it (the game's own training XP, applied to this stat).
    void setStored(Character* c, float a);
    // [Test] ambidexterity for the load line: "" (off), "skill" or the fixed value.
    std::string testText();
    // The Dual Wielding the lists of Nitoryu.mod give a CHARACTER or FACTION record (string id -> value),
    // read by stance::refreshOptIn after every load.
    void setListSkills(const std::map<std::string, float>& byRecord);
    // [Test] npc_ambidexterity (every NPC), the same way.
    std::string testNpcText();

    // A as it counts in a fight, x the overload and heavy-armour factors (docs/BALANCE.md).
    // Optional outputs for the log.
    struct Effective
    {
        float a, eff;           // stored A, effective A
        float overload;         // carried / capacity - 1 (0 when not overloaded)
        float overloadMult, armourMult;
        int armourPieces;       // worn pieces that cut A (heavy, medium, or a record's own cut)
    };
    Effective effective(Character* c);

    // hitByMeleeAttack: the attacker's xpMelee follows the victim's hit on the
    // same thread; remember whether that impact was the off hand's.
    void noteImpact(Character* attacker, CombatTechniqueData* attack, bool offHand);

    // Main loop, per drawn dual fighter: the rank-up moment of a player character.
    void noteRank(Character* c);

    // Main loop, per dual fighter: the weapon skills' gains made outside our xpMelee hook (another plugin
    // that replaced xpMelee) train Dual Wielding too.
    void watchWeaponXp(Character* c);
}
