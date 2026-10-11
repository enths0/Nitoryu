// What two blades do that one cannot (docs/BALANCE.md).
//
// Pair bonuses: the pair is read from the two weapons' own `length` (a WEAPON's `nitoryu pair length`
// replaces it): the shorter at most PAIR_SHORT_RATIO of the longer = long + short (the second blade's parry
// chance x PAIR_GUARD_MULT); at least PAIR_EQUAL_RATIO = equal blades (the twin share x PAIR_TWIN_MULT). Two
// weapon types in the pair (katana + sabre): the second weapon's defence bonus counts PAIR_MIXED_DEFENCE
// instead of the off weapon bonus share.
//
// Scissor: the game picks a hit's body part by a weighted roll inside MedicalSystem::addWound. When both
// blades of a twin land on one enemy, the later blade takes the part the earlier one hit, with the
// SCISSOR_CHANCE curve by DP: that part's hit chance is raised for the one call.
//
// Two enemies, two blades: the game hits only the enemies in a narrow box in front. From SPLIT_MIN_TIER the
// left blade of a twin lands, with the SPLIT_CHANCE curve, on an enemy who attacked the fighter within
// SPLIT_ATTACKER_MS, stands within the second blade's reach in the front SPLIT_ARC_DEG, and is outside that
// box; the main target then takes the right blade only.
#pragma once

#include <kenshi/Enums.h>

#include "Stance.h"

class Character;
class CombatTechniqueData;
class Weapon;

namespace blades
{
    void init();                 // the lock; after cfg::init
    void applySettings();        // from cfg (init, and after a change)
    bool installHooks();         // MedicalSystem::addWound; false: the scissor stays off
    void setEnabled(bool on);
    void forget(Character* c);   // Character destructor

    // ---- the pair ----
    enum Pair { PAIR_PLAIN, PAIR_LONG_SHORT, PAIR_EQUAL };
    Pair pairOf(Weapon* a, Weapon* b);
    const char* pairName(Pair p);
    bool mixedTypes(Weapon* a, Weapon* b);
    bool mixedDefence(Weapon* main, Weapon* off);   // two types, and the second weapon has a defence bonus to count in full
    float guardMult(Weapon* a, Weapon* b);          // x the second blade's parry chance; 1 = none
    float twinMult(Weapon* a, Weapon* b);           // x the twin share; 1 = none
    // The share of the second weapon's defence bonus that counts (`usual` = the off weapon bonus share).
    float offDefenceShare(const stance::Info& s, float bonus, float usual);

    // ---- the scissor. Around the game's hit of one blade of a twin (hitByMeleeAttack's thread) ----
    class Scissor
    {
    public:
        Scissor(Character* attacker, Character* victim, const stance::Info& s, bool offBlade, bool twin);
        ~Scissor();                         // the raised hit chance goes back
        void landed(HitMaterialType r);     // after the game's hit returned
    private:
        Character* attacker;
        Character* victim;
        bool offBlade, on, second;
        void* forced;                       // MedicalSystem::HealthPartStatus*
        float savedChance;
        void* wanted;                       // GameData* of the part the first blade hit
    };

    // ---- the second enemy ----
    // _iHitYouAreYouHit (the victim's side): `attacker` aimed an attack at `victim`.
    void noteAttacker(Character* victim, Character* attacker);
    // The left blade of `me`'s twin is about to hit `mainTarget`: the flanker it goes to instead, or NULL.
    Character* flanker(Character* me, Character* mainTarget, const stance::Info& s, CombatTechniqueData* attack,
                       int comboID);
}
