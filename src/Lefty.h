// Left-hand fighting. Vanilla never lets a character with a crippled right arm keep a weapon:
// Task_MeleeAttack's weapon choice (vtable slot 5, found by RTTI name at load) sheathes it and goes
// unarmed. For a character with a working left arm, enough Dual Wielding ([LeftHand] min_ambidexterity)
// and an allowed one-handed weapon, the hook runs the choice as if the right arm worked, so the weapon is
// drawn; the pose engine mirrors the whole body (LEFTY) and the weapon is shown in the left hand. It works
// for a single weapon and for a pair (then only one blade is used). Penalties fall with Dual Wielding.
#pragma once

class Character;
class Weapon;

namespace lefty
{
    void init();                 // after cfg::init
    void applySettings();        // the left-hand settings from cfg (init, and after a change)
    bool installHooks();         // false (logged) if the class or its check is not found
    bool iniOn();                // [LeftHand] enabled
    void setEnabled(bool on);

    // The character may fight left-handed now: right arm out, left arm working,
    // Dual Wielding high enough, a one-handed allowed weapon carried (cached 500 ms).
    bool qualifies(Character* c);
    // It does: it qualifies and holds an allowed one-handed weapon.
    bool active(Character* c);

    // Penalties for an active left-hander, 1 = none (they fall off with Dual Wielding).
    float damageFactor(Character* c);
    float speedFactor(Character* c);
    float blockPenalty(Character* c);      // added to the attacker's attack in the block roll

    // Characters that went left-handed recently (main loop, for the visuals).
    void takeNoted(void (*fn)(Character*));
    void forget(Character* c);      // Character destructor
}
