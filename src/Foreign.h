// Another plugin drives the body.
//
// A direct-control plugin plays swings, blocks and dodges itself and switches weapons at once, while the
// game's own combat is over (CombatClass::combatState == COMBAT_FINISHED, not in combat mode). Nitoryu's clips
// and the sheathe hold follow the game's combat state, so without this they would keep the arms and the blades
// over that plugin's animation.
//
// A foreign action = a combat or block animation that STARTED while the game's combat was finished. The game's
// own swings and blocks always start inside combat mode; their tails may outlive it, which is why the start
// decides and not the frame.
//
// Settings (SettingsDef.h, "Nitoryu 14 other plugins", ini [Compat]): foreign_control off / auto / on,
// plugins (the DLL names auto looks for), hold_ms, run_ms.
#pragma once

#include <string>

class Character;
class CombatClass;

namespace foreign
{
    // What one fighter's entry keeps between frames (Visual.cpp owns it; main loop only).
    struct Watch
    {
        const void* anim;       // the combat or block animation seen last frame (compared, never read), NULL: none
        float time01;           // its progress then
        bool startedForeign;    // it started outside the game's combat
        double until;           // defense::clockMs() until which this fighter counts as driven by another plugin
        double runSince;        // ... since which it runs without a stop (0: not running)
        bool blockUp;           // the other plugin's block plays (mirrored into blockHeld for the combat threads)
        Watch() : anim(NULL), time01(0.0f), startedForeign(false), until(0.0), runSince(0.0), blockUp(false) {}
    };

    void applySettings();       // main loop: at start and after a settings change; logs `foreign_config`
    bool active();              // on, or auto with one of the plugins loaded

    // Main loop, every frame, for a fighter with an entry. True: another plugin's swing or block plays now.
    bool actionNow(Character* c, CombatClass* cc, Watch& w, double nowMs);
    // Another plugin's swing or block plays on this fighter right now (as of the last actionNow): a draw or a
    // sheathe that happens under it gets no clip and no hold, the blades follow at once.
    bool playing(const Watch& w);
    // The fighter was driven by another plugin within hold_ms (the run hold then waits for a steady run).
    bool driven(const Watch& w, double nowMs);
    // The run hold's input: `running` as the game says it, or false until the run is run_ms old while driven.
    bool running(bool running, Watch& w, double nowMs);

    // Any thread: the other plugin's block is up on this character (as of the last main-loop frame).
    bool blockHeld(Character* c);
    // Combat thread, Character::hitByMeleeAttack's result for a player character while foreign control is
    // active: counters by result and by whether the block was up, and one `incoming_hit` line per impact
    // (its own budget).
    void noteIncoming(Character* victim, const std::string& attacker, const std::string& tech, int combo,
                      int result, bool landed, float cutIn, float bluntIn, float cutLeft, float bluntLeft);
}
