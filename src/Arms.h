// The dual-wield pose engine. Every frame, just before
// Ogre::Skeleton::_updateTransforms (an OgreMain_x64.dll export), after the
// game's animations have set the bones, it rewrites a tracked fighter's bones:
//
//   MIRROR  stance, guard, block, walk, run: the left arm is the mirror of the
//           right arm, and Bip01 Prop1 (the left blade's mount) the mirror of
//           Bip01 Prop2 (the right weapon's mount, keyed in every sword track).
//           In combat the chest is also turned square to the facing, so the two
//           blades form a symmetric guard instead of pointing sideways, and a
//           right hand that reaches past the chest's midline is swung back to
//           its own side (uncross), so the arms never cross (the blades may).
//   HOLD    a main-hand technique plays: the left arm keeps its last guard.
//   BLOCK   the main hand blocks (combat state BLOCK / REACTION_BLOCK): one-handed,
//           so the left arm keeps its guard too; the right arm's block pose at
//           its widest is recorded (the last few) for the off-hand parry.
//   SWAP    a Nitoryu (off-hand) technique plays a right-hand sword track: the
//           whole body is mirrored, so it becomes a left-hand swing, while the
//           right arm holds its guard.
//   LEFTY   left-hand fighting (right arm crippled, Lefty.cpp): the whole body
//           is the mirror of the animation in every state, so the left arm does
//           what the right arm is animated to do, and Prop1 carries the weapon.
//   PARRY   (an overlay) the second blade just parried: the left arm plays the
//           mirror of a recorded right-arm block pose and returns, [Guard]
//           parry_ms of game time (before any block was seen: a swing up and out).
//   TWIN    a Nitoryu twin technique (RIGHT_ARM + LEFT_ARM impacts) plays a
//           right-hand track: the right arm plays it, the left arm mirrors it
//           in the same frame, so both blades strike at once (overhead double
//           cut, cross-slash).
//
// The mirror is general: at track time the rig's bind pose gives, per bone, a
// fixed correction K_b so that for any animated pose
//   local'(b) = K_parent^-1 * reflect(local(partner(b))) * K_b
// (partner: L <-> R by name, Prop1 <-> Prop2; reflection across the rig's
// sagittal plane, found from the upper arms). The mode is set by the main loop
// every frame from the fighter's combat state, never from timers. Blends between
// modes are exponential, ~[OffHand] blend_ms.
#pragma once

#include <string>

#include "ClipsGen.h"   // the clip table (generated)

class Character;
namespace Ogre { class Skeleton; }

namespace arms
{
    enum Mode { MIRROR, HOLD, SWAP, TWIN, BLOCK, LEFTY };

    bool installHook();
    void init(float squareTorso, float uncross, unsigned blendMs, bool swap);   // [OffHand] square_torso / uncross / blend_ms / swap_strikes
    // Who holds a fighter in the pose engine. The State lives while any owner holds it; each owner
    // releases on every exit path (a failed attach, a detach, an erased entry), so no mirrored arms
    // outlive the blade.
    enum Owner { OWNER_DUAL = 1, OWNER_LEFTY = 2, OWNER_ALL = 3 };
    // Main loop: pose this skeleton for `owner`. False if it is not a mirrorable biped rig
    // (no Prop1/Prop2 or not left-right symmetric); then nothing is changed.
    bool track(Character* c, Ogre::Skeleton* skeleton, unsigned owner);
    void release(Character* c, unsigned owner);                  // main loop; the State goes with its last owner
    void untrack(Character* c);                                  // main loop: every owner (the character is gone)
    // Main loop: the skeleton this fighter is posed on, or NULL; a different
    // AppearanceBase::getSkeleton() means the appearance was rebuilt.
    Ogre::Skeleton* skeletonOf(Character* c);
    // Does this fighter's rig (or its linked animation sources) have a track of this name? True when
    // unknown (not posed, or the rig's list could not be read). rigName receives the skeleton's name
    // when the answer is no.
    bool hasTrack(Character* c, const std::string& name, std::string* rigName);
    int unsupportedCount();         // characters whose rig the mirror cannot build (compatibility line)
    void preload();                 // main loop, after a game load: the clip files, outside the pose lock
    void setMode(Character* c, Mode m, bool inCombat);           // main loop, every frame
    // Main loop: an off-hand parry started at this game time (defense::clockMs):
    // the left arm snaps its blade up and out for [Guard] parry_ms, over any mode
    // but SWAP/TWIN (the off blade is swinging then).
    void setParry(Character* c, double startMs);
    void initParry(unsigned durationMs, float strength);         // [Guard] parry_ms / parry_pose
    const char* modeName(Mode m);

    // Clips: Nitoryu's own tracks (Nitoryu_L3_<sex>.skeleton, loaded by the plugin itself), played by
    // name over the upper body (spine, head, arms, blade mounts) on top of whatever mode is active,
    // with short blends in and out. A track the skeleton does not have is logged once and skipped, so
    // the fighter keeps the mirrored game pose. A full-body clip also drives the legs, the pelvis and
    // the root's turn and height (the base track keeps the ground travel); the run holds loop while a
    // fighter runs. The table is ClipsGen.h.
#define NITORYU_CLIP_ENUM(e, track, name, ini, legacy, counter, full, base) e,
    enum Clip { NITORYU_L3_CLIPS(NITORYU_CLIP_ENUM) CLIP_COUNT };
#undef NITORYU_CLIP_ENUM
    // [Animations] switches, one per clip (Moves.h CLIPS: the two draws and the two run holds share one).
    void setClipOn(Clip k, bool on);
    void setAngledSwap(bool on);            // [Animations] angled_block_swap
    void setLimpSwing(bool on);             // [Animations] limp_arm_swing: the crippled arm swings (0: one fixed pose)
    bool clipOn(Clip k);
    // Main loop: play clip k from this game time (defense::clockMs). A running
    // clip is not restarted; only CLIP_BLOCK_LEFT (a reaction) replaces another.
    void playClip(Character* c, Clip k, double startMs);
    // Is clip k this fighter's playing triggered clip; and make it give way (the same 0.1 s fade as a
    // hit) when what it shows is no longer true (the blades were drawn again).
    bool clipPlaying(Character* c, Clip k);
    void stopClip(Character* c, Clip k);
    // A hit landed on this fighter (combat thread): a playing clip gives way.
    // So do swings and blocks that start during a clip (checked by the pose hook).
    void clipHit(Character* c);
    // Main loop, every frame: an attack clip follows the playing technique's own progress (0-1,
    // AnimationClass::getAnimationProgress), so attack speed and pause keep the impacts in sync.
    // k < 0: no attack clip.
    void setAttackClip(Character* c, int k, float progress01);
    // Main loop, every frame: the fighter is running (CharMovement::isRunning). While it
    // runs in the plain guard (MIRROR) a run hold loops over the arms and spine, one of
    // two picked at random each time the run starts; it fades in and out in 0.15 s.
    // It plays in step with the legs when they run the rig's `run lower` (its own track or a linked file's);
    // with any other run the torso takes only [Animations] run_hold_free_weight of it.
    void setRunning(Character* c, bool running);
    // The four per-frame calls above in one (one lock, one lookup).
    struct FrameInput
    {
        Mode mode;
        bool inCombat;
        bool running;
        bool moving;            // Character::getMovementSpeed > 1 (walking up counts)
        bool foreign;           // another plugin's swing or block plays on the body (Foreign.h)
        int rank;               // the fighter's rank (stance::Info::tier, 0-5): its one idle stance
        int attackClip;         // -1: none
        float attackProgress;
        double parryStart;      // < 0: no parry this frame
        double blockHitStart;   // the last hit the main hand blocked (defense::lastMainBlockMs), < 0: none
        bool parryBack;         // that parry was against an attacker behind: the second blade's back guard
        bool blockHitBack;      // that blocked hit came from behind: the main blade's back guard
        int blockDir;           // the blow's CutDirection while blocking (block technique, else the attacker's), -1: unknown
        double nowMs;           // defense::clockMs() of this frame
        int reacting;           // REACT_NONE, REACT_STAGGER, REACT_DOWN (Visual.cpp reactingOf)
    };
    void setFrame(Character* c, const FrameInput& in);
    // The game plays a reaction on the body. STAGGER (combatState STUMBLE): the stance and the run hold give
    // way in 0.1 s and the chest is not squared. DOWN (down, getting up, KO, playing dead, ragdoll, carried, dead): also
    // every clip gives way and only the plain mirror of the left arm and Prop1 is written (LEFTY: no whole-body mirror),
    // so the second blade stays in the left hand while the game (or physics) moves the body.
    enum Reacting { REACT_NONE = 0, REACT_STAGGER = 1, REACT_DOWN = 2 };
    void setReacting(Character* c, int level);              // main loop: the left-handers (LEFTY goes through setMode)
    void emitClipLog();                                    // main loop: the lines the pose hook queued
    // The pose engine's time since the last call (ms), its longest single update, how many updates
    // ran, and the most fighters it held at once; then reset.
    void takeCost(double& totalMs, double& maxMs, unsigned& calls, int& peak);
    const char* clipText();                                // "block,scissor,draw" or ""
}
