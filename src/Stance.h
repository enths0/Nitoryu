// The stance: which characters fight with two weapons right now, and how well.
//
// A character is dual when:
//   - its inventory section "back" holds two weapons, or "back" + "hip" one each
//     (both back weapons share the section "back");
//   - both are on the allow-list (default katanas, sabres, blunt) and not two-handed only;
//   - the weapon in its hands is one of the pair (that one is the main hand);
//   - both arms work, ranged mode is off, and the player has not toggled it off;
//   - NPCs also pass the gate (Dex, both skills, half the dual strength requirement).
// Combat hooks run off the main-loop thread, so every access goes through one lock.
#pragma once

#include <kenshi/Enums.h>

#include <string>

class Character;
class Weapon;

namespace stance
{
    struct Info
    {
        bool dual;              // the stance is active now
        const char* reason;     // why not, for the per-reason counters ("dual" when active)
        Weapon* main;
        Weapon* off;
        float skillMain, skillOff;
        float dp;               // dual proficiency 0..100
        int tier;               // 0..5, after the caps below
        int tierByDp;           // the tier DP alone gives
        const char* tierCap;    // "ambi", "npc" ([NPC] max_tier) or "" (none)
        bool optedIn;           // an NPC another mod opted in
        float clumsy;           // off melee attack and defence
        float guardPenalty;     // extra off melee defence (divided guard)
        float speedFactor;      // x attack and block speed
        float weightDual;       // W_dual fed to the weight-speed formula
        float ambi, ambiEff;    // stored and effective Dual Wielding (after overload and armour)
        float overload;         // carried / capacity - 1 (0 = not overloaded)
        float dex;              // Dexterity, capped at 100
        bool offFromHip;        // the second blade is drawn from the hip (back + hip layout)
    };

    void init();                                   // lock, [Test] and [NPC] opt_in from Nitoryu.ini; after cfg::init
    void applySettings();                          // allow-list, speed caps, NPC mode from cfg (init, and after a change)
    std::string iniFile();                         // Nitoryu.ini next to the DLL
    // The game's attack/block speed x the dual factor, with bonuses capped at
    // [Speed] dual_attack_cap / dual_block_cap (never below the game's own result).
    // capBonus: added to the cap while the fighter is in flow.
    float applySpeed(float gameResult, float factor, bool isBlock, float capBonus = 0.0f);
    bool allowed(WeaponCategory c);
    std::string allowListText();
    // Per-record levers (docs/FCS.md). A weapon's own WEAPON field `nitoryu dual wield` (0 = by its
    // category, 1 = allowed whatever the category, 2 = refused) decides before the category list.
    bool weaponAllowed(Weapon* w);
    bool weaponRefused(Weapon* w);                 // its record says 2
    // A player character's own switch (the second inventory button, saved as NIT_any_weapon):
    // blunt weapons and hackers pair for this character whatever the category list says. Heavy weapons,
    // polearms, bows, two-handed-only weapons and a record that says 2 stay out.
    bool anyWeapon(Character* c);
    bool toggleAnyWeapon(Character* c);            // returns the new state
    void setAnyWeapon(Character* c, bool on);      // from the save (Ambidex.cpp)
    bool categoryAllowedFor(Character* c, WeaponCategory cat);
    bool weaponAllowedFor(Character* c, Weapon* w);
    // The two carried weapons pair only with the switch on: the second button shows then (Ui.cpp).
    bool needsAnyWeapon(Character* c);
    // The character's RACE field `nitoryu rig`: 0 = the pose engine decides (mirror when the rig allows),
    // 1 = no mirroring (the second blade in a fixed left-hand grip, no left-hand fighting), 2 = no Nitoryu.
    enum Rig { RIG_AUTO = 0, RIG_FIXED_GRIP = 1, RIG_NEVER = 2 };
    int rigOf(Character* c);

    // Evaluate (cached for STANCE_CACHE_MS unless mainOverride is given: setWeapon
    // passes the weapon it is about to put in the hands).
    Info evaluate(Character* c, Weapon* mainOverride);

    void invalidate(Character* c);                 // drop the cached evaluation (weapon changed)

    // Player toggle; returns the new state (true = dual allowed).
    bool togglePlayer(Character* c);
    bool playerAllowed(Character* c);
    void setPlayerAllowed(Character* c, bool on);  // from the save (Ambidex.cpp)
    void forget(Character* c);                     // the character is being destroyed
    // Two allowed one-handed weapons in back/back or back/hip (drawn or not):
    // the inventory button shows only then (Ui.cpp).
    bool hasPair(Character* c);
    // The same pair's two weapons (the first back weapon, then the second back or the hip one).
    bool carriedPair(Character* c, Weapon*& a, Weapon*& b);
    // DP from the character's stats now, drawn or not (the stats window's hover).
    float previewDP(Character* c);
    // The stats window's hover: DP, the tier with its caps, the Dual Wielding the next tier needs
    // (0 = none), and the speed factor.
    struct Preview
    {
        float dp;
        int tier, tierByDp;
        float nextAmbi;
        float speedFactor;
    };
    Preview preview(Character* c);
    // The skill passed to the technique pool (min skill = DP gate): no more than the capped tier allows.
    float poolSkill(const Info& s);
    // Re-read the NPC opt-in list (ini + the FCS record) after a game load.
    void refreshOptIn();

    void prune(unsigned olderThanMs);              // forget characters not evaluated lately

    // Share of attacks that use the off hand at this DP (the OFF_HAND_SHARE curve), or
    // [Test] follow_up_chance when set.
    float offHandShare(float dp);
    bool testShareSet();
    // [Test] force_tier from Nitoryu.ini (0 = off), for seeing the moves without a skilled fighter.
    int forcedTier();
    int tierOf(float dp);
    // The game's memory layouts we read raw (AttachmentData, AttachedEntity, InventoryGUI) are known
    // for game version 1.0.65 only (any store). Elsewhere the dual visuals cannot be built, so the
    // stance, its visuals and the button stay off.
    bool knownLayout();
    std::string gameVersion();
    StatsEnumerated statFor(WeaponCategory c);
}
