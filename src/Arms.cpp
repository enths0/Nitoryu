// Threads:
//   updateTransforms_hook - wherever Ogre updates a skeleton (its worker threads); the main loop writes the States.
//   g_lock guards g_states, g_trackNames, g_trackUnknown, g_byChar, g_unsupported and the cost counters. It is
//   held across apply() (our pose maths on Ogre bones of that skeleton), never across the game's original.

#include "kit/input.h"   // Windows.h with NOMINMAX first

#include "kit/log.h"
#include <core/Functions.h>

#include <OgreSkeleton.h>
#include <OgreAnimation.h>
#include <OgreAnimationTrack.h>
#include <OgreKeyFrame.h>
#include <OgreOldSkeletonManager.h>
#include <OgreResourceGroupManager.h>
#include <OgreOldBone.h>
#include <OgreQuaternion.h>
#include <OgreVector3.h>

#include <kenshi/Character.h>      // isFemale

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Arms.h"
#include "Moves.h"
#include "Diagnostics.h"
#include "Defense.h"
#include "Feedback.h"
#include "kit/diag.h"
#include "Hooks.h"
#include "Settings.h"

namespace
{
    using arms::Mode;
    using arms::MIRROR;
    using arms::HOLD;
    using arms::SWAP;
    using arms::TWIN;
    using arms::BLOCK;
    using arms::LEFTY;

    const int BLOCK_SNAPS = 4;        // right-arm block poses kept per fighter
    const float MIN_BLOCK_DEV = 0.3f; // radians: smaller deviations from the guard are not a block pose

    enum Role { OTHER, RIGHT_ARM, LEFT_ARM, SPINE };

    const char* SPINE_BONES[3] = { "Bip01 Spine", "Bip01 Spine1", "Bip01 Spine2" };
    const float SPINE_SHARE[3] = { 0.34f, 0.5f, 1.0f };     // squaring spread down the spine

    float g_square = 0.8f;      // [OffHand] square_torso: 0 = vanilla chest, 1 = fully square in combat
    float g_blendSec = 0.08f;   // [OffHand] blend_ms
    bool g_swap = true;         // [OffHand] swap_strikes: mirror the body during off-hand strikes
    float g_uncross = 0.7f;     // [OffHand] uncross: keep each hand this share of the shoulder's offset on its own side
    double g_parryMs = 650.0;   // [Guard] parry_ms: length of the parry pose (game time)
    float g_parryPose = 1.0f;   // [Guard] parry_pose: how far the arm goes toward the deflect (0 = off)

    // The clips (arms::Clip order): track names and counters from ClipsGen.h.
#define NITORYU_CLIP_TRACK(e, track, name, ini, legacy, counter, full, base) track,
#define NITORYU_CLIP_PLAYED(e, track, name, ini, legacy, counter, full, base) counter "_played",
#define NITORYU_CLIP_MISSING(e, track, name, ini, legacy, counter, full, base) counter "_missing",
    const char* CLIP_TRACK[arms::CLIP_COUNT] = { NITORYU_L3_CLIPS(NITORYU_CLIP_TRACK) };
    const char* CLIP_PLAYED[arms::CLIP_COUNT] = { NITORYU_L3_CLIPS(NITORYU_CLIP_PLAYED) };
    const char* CLIP_MISSING[arms::CLIP_COUNT] = { NITORYU_L3_CLIPS(NITORYU_CLIP_MISSING) };
#undef NITORYU_CLIP_TRACK
#undef NITORYU_CLIP_PLAYED
#undef NITORYU_CLIP_MISSING
    bool g_clipOn[arms::CLIP_COUNT] = { false };
    const float CLIP_IN_S = 0.06f;          // blend into the clip
    const float CLIP_OUT_S = 0.10f;         // and back to the game's pose at its end
    const float CLIP_YIELD_S = 0.10f;       // a clip gives way this fast to a swing, a block or a hit
    bool g_clipFoundLogged[arms::CLIP_COUNT] = { false };
    bool g_clipMissLogged[arms::CLIP_COUNT] = { false };
    const float RUN_FADE_S = 0.15f;         // a run hold fades in and out this fast
    // The run hold's lock on the legs' stride. Locked at RUN_MATCH_MIN, kept down to RUN_UNLOCK_MIN (no
    // flip-flop at the threshold); a small difference to the legs is taken at once, a big one (back from free
    // running) is eased in over RUN_PHASE_EASE_S instead of snapping.
    const float RUN_UNLOCK_MIN = 0.96f;
    const float RUN_PHASE_SNAP = 0.08f;
    const float RUN_PHASE_EASE_S = 0.10f;
    const float RUN_STANCE_FADE_S = 0.25f;  // a full-body run stance takes over from the trail (and gives way) this fast
    // the combat idle stances
    const float STANCE_FADE_S = 0.40f;      // into a stance from the guard and back out of it (to the guard)
    const float STANCE_CROSS_S = 0.80f;     // one rank's stance into another's (the rank changed mid-fight)
    const float STANCE_BODY_S = 0.20f;      // standing (our legs) <-> walking (the game's legs, our arms)
    const float ROOT_EASE_S = 0.25f;        // a clip's root offset eases out this fast
    // The limp arm's pendulum (LEFTY). A model unit is about 9 cm, so g is ~109; the damping settles a
    // swing in about a second; the upper arm stays within 50 deg of hanging, the forearm within 60 of the upper arm.
    const float HANG_GRAVITY = 109.0f;
    const float HANG_DAMP = 3.5f;
    const float HANG_SHOULDER_COS = 0.643f;
    const float HANG_ELBOW_COS = 0.5f;
    bool g_limpSwing = true;                // [Animations] limp_arm_swing
    // Into and out of an attack clip the pose blends from what is on screen over this long (other mode
    // changes keep blend_ms); a one-frame switch would shift the hands visibly.
    const float CLIP_SWAP_S = 0.10f;           // a reaction clip replacing another fades from what was on screen
    const float ATTACK_BLEND_S = 0.35f;
    const float RETURN_BLEND_S = 0.25f;        // a block, a cut-off clip or a vanilla swing back to the guard
    const float POSE_JUMP_DEG_S = 1800.0f;     // a bone turning faster than this outside a clip's own swing is logged (pose_jump)
    const float POSE_JUMP_ROOT_S = 80.0f;      // ... OUR offset over the game's root changing faster than this (units a second)
    const int POSE_JUMP_DETAILS = 300;         // the pose_jump lines' own budget (the shared one is spent before the fights)
    volatile LONG g_poseJumpLines = 0;
    struct ClipLog { int clip; bool found; int own; int linked; float length; int tracks; float scale; const char* source; int sex; };
    std::vector<ClipLog> g_clipLog;         // under g_lock; emitted by the main loop
    std::vector<std::string> g_retimeLog;   // `base_track_retimed` lines, under g_lock

    // Nitoryu's own track files, loaded by the plugin (main loop) and read directly. The game links
    // an ANIMATION_FILE's skeleton only through the character's race, and a fighter's skeleton may
    // have no linked source at all. Reading our own file works for any race.
    const char* L3_FILE[2] = { "Nitoryu_L3_male.skeleton", "Nitoryu_L3_female.skeleton" };
    // the folders animation mods use, under mods\Nitoryu\character\meshes
    const char* L3_DIR[2] = { "\\male_skeleton", "\\female_skeleton" };
    Ogre::SkeletonPtr g_l3Skel[2];
    int g_l3State[2] = { 0, 0 };            // 0 not tried, 1 loaded, -1 failed
    std::string g_l3Error[2];
    bool g_l3Logged[2] = { false, false };

    // What a rig needs for the mirror, built once per skeleton from its bind pose.
    struct Rig
    {
        int n;
        std::vector<int> parent;                // -1 for a root bone
        std::vector<int> partner;               // L <-> R, Prop1 <-> Prop2, else itself
        std::vector<Ogre::Quaternion> K;        // per-bone mirror correction
        std::vector<Ogre::Quaternion> KpInv;    // K of the parent, inverted
        std::vector<unsigned char> role;
        int prop1, prop2;
        int spine[3];
        Ogre::Vector3 normal;                   // the sagittal plane's normal, model space
        int rc, ru, rf, rh;                     // right arm chain, for uncrossing
        bool canUncross;
        Ogre::Vector3 up, rside, fwd;           // chest-frame axes (clavicle's parent)
        int lu, lh;                             // left upper arm, left hand (parry pose)
        bool canParry;
        bool prop1OnLeftArm;                    // Prop1 hangs under the left arm (moves with it)
        Ogre::Vector3 upM;                      // model-space up, orthogonal to `normal`
        int checkHandle;                        // cheap identity check each frame
        std::string checkName;
        std::vector<unsigned char> clipBone;    // bones a clip drives: spine, neck, head, arms, props
        std::vector<unsigned char> legBone;     // and a full-body clip also: root, pelvis, legs
        std::vector<unsigned char> torso;       // spine, neck, head, clavicles (what a free-running run hold fights)
        int root;                               // Bip01 (-1: none): its height comes from a full-body clip
        int thigh[2];                           // Bip01 L/R Thigh (-1: none): the run hold reads the stride from them
        std::vector<Ogre::Quaternion> bindQ;    // local bind pose (clip keys are relative to it)
        std::vector<Ogre::Vector3> bindP;
    };

    // The layers that weigh a clip over the pose under it (layerSlerp).
    enum Layer { LY_TRIGGERED, LY_SPECIAL, LY_RUN, LY_RUN_STANCE, LY_STANCE, LAYER_COUNT };

    struct State
    {
        Character* who;
        unsigned owners;                            // arms::Owner bits
        Rig rig;
        Mode mode;
        bool inCombat;
        Mode appliedMode;
        bool appliedCombat;
        bool started;
        float blendT;                               // 0..1 from `from` toward the target
        std::vector<Ogre::Quaternion> src, written, tgt, from, out, guard;
        std::vector<Ogre::Vector3> srcP, writtenP, tgtP, fromP, outP, guardP;
        // each layer's result per bone last frame and the frame it was written (layerSlerp)
        std::vector<Ogre::Quaternion> layerPrev[LAYER_COUNT];
        std::vector<unsigned> layerFrame[LAYER_COUNT];
        unsigned frameNo;                           // apply() calls, for layerFrame
        bool haveGuard;
        double lastMs;                              // defense::frameMs() of the last update (< 0: none); blends run on game time
        int reacting;                               // arms::Reacting, set by the main loop
        int appliedReacting;                        // the level the current transition was started for
        double parryStart;                          // game ms, < 0: none
        // Block poses of the right arm (full local pose; only the right arm and
        // Prop2 are read), the widest frame of each recent main-hand block.
        std::vector<Ogre::Quaternion> snap[BLOCK_SNAPS];
        std::vector<Ogre::Vector3> snapP[BLOCK_SNAPS];
        int snapCount, snapNext, parrySnap;
        std::vector<Ogre::Quaternion> cur;
        std::vector<Ogre::Vector3> curP;
        float curDev;
        // The clip playing (arms::Clip) or -1, from game time clipStart.
        int clip;
        double clipStart;
        bool clipCounted;
        Ogre::Animation* clipAnim[arms::CLIP_COUNT];    // resolved once per skeleton; NULL: not there
        float clipScale[arms::CLIP_COUNT];
        bool clipLooked[arms::CLIP_COUNT];
        int clipMode;                                   // the mode when the clip started
        float clipYield;                                // clip age when it began to give way, < 0: not
        bool clipHit;                                   // a hit landed on this fighter (combat thread)
        bool clipSwap;                                  // a reaction replaced a playing clip: blend from the screen
        int blockKind;                                  // the held guard of the current main-hand block, -1: none
        double blockHitStart;                           // the last main-hand blocked hit (game ms), < 0: none
        int attackClip;                                 // the attack clip while that technique plays, else -1
        float attackProgress;                           // its 0-1 progress (main loop)
        bool attackCounted;
        signed char bodyOk[arms::CLIP_COUNT];           // -1 unchecked, 0 base retimed (upper body only), 1 full body
        bool rootDriven;                                // this frame writes the root's height
        Ogre::Vector3 rootCarry;                        // the clip's last root offset, fading out
        float blendDur;                                 // this transition's length (s)
        int appliedAttack;                              // the attack clip the transition was for
        bool running;                                   // main loop: CharMovement::isRunning
        bool foreign;                                   // main loop: another plugin's swing or block plays
        int runClip;                                    // the run hold looping (or fading out), -1: none
        double runStart;                                // game ms
        float runW;                                     // its weight, 0-1
        double runLast;                                 // game ms of the last frame (the fade's step)
        bool runCounted;
        Ogre::Animation* runBase;                       // the rig's `run lower` (NULL: none), looked up once
        bool runBaseTried;
        float runPhase;                                 // 0-1 along the stride pair
        bool runLocked;                                 // the phase was read from the legs this frame
        bool runPhaseCounted;                           // this run's locked/free state was counted
        int runStance;                                  // this run's full-body stance (CLIP_RUN_STANCE_*), -1: the trail alone
        int runMix;                                     // run_style 4: this fighter's stance (A or C), -1: not drawn yet
        float runFullW;                                 // the stance's share over the trail, 0-1 (locked legs: 1)
        bool runStanceOn, runStanceCounted, runStanceCounted2;
        // The combat idle stance looping (or fading out), -1: none; the one it is crossing from; its
        // overall weight, the cross-fade, and how much of it is standing (legs).
        bool moving;                                    // main loop: Character::getMovementSpeed > 1
        int stCur, stPrev;
        double stStart, stPrevStart, stLast;
        int rank;                       // the fighter's rank (stance tier 0-5) picks its idle stance
        float stW, stX, stBody;
        // The crippled right arm's pendulum (LEFTY): the elbow and the hand as points in model space
        // with their velocities, and the game time of the last step (< 0: not started).
        Ogre::Vector3 hangE, hangH, hangVE, hangVH;
        double hangT;
        int sex;                                        // 0 male, 1 female skeleton: which track file
        const std::set<std::string>* tracks;            // every track name the rig has, NULL: unknown
    };

    CRITICAL_SECTION g_lock;
    std::map<Ogre::Skeleton*, State> g_states;
    // Per skeleton name, the names of every track in it and its linked animation sources (under
    // g_lock; std::map nodes stay put, so States keep pointers).
    std::map<std::string, std::set<std::string> > g_trackNames;
    std::set<std::string> g_trackUnknown;                   // rigs whose list looked empty
    std::map<Character*, Ogre::Skeleton*> g_byChar;
    std::map<Character*, Ogre::Skeleton*> g_unsupported;    // rigs that failed buildRig, not retried
    volatile LONG g_count = 0;          // lock-free "anything to do?" for the hot path
    LARGE_INTEGER g_freq;

    // Slerp(w, a, b, shortestPath) takes the shorter way from a to b, and when the clip's pose and the pose
    // under it come to stand ~180 degrees apart (a blade turned over) the shorter way switches sides from one
    // frame to the next and the bone jumps across. Of the two ways (b and -b, the same rotation) this keeps
    // the one nearer to what the layer gave last frame; a layer that was not on last frame, or at weight 0 or
    // 1 (both ways give the same pose), takes the shorter way.
    Ogre::Quaternion keptWay(float w, const Ogre::Quaternion& a, const Ogre::Quaternion& b, const Ogre::Quaternion* prev,
                             bool* longer = NULL)
    {
        float d = a.Dot(b);
        if (prev == NULL || w <= 0.0f || w >= 1.0f || std::fabs(d) > 0.9999f)
            return Ogre::Quaternion::Slerp(w, a, b, true);
        Ogre::Quaternion nb = -b;
        Ogre::Quaternion one = Ogre::Quaternion::Slerp(w, a, d >= 0.0f ? b : nb, false);       // the shorter way
        Ogre::Quaternion other = Ogre::Quaternion::Slerp(w, a, d >= 0.0f ? nb : b, false);
        if (!(other.w == other.w) || !(other.x == other.x))
            return one;
        if (std::fabs(prev->Dot(other)) <= std::fabs(prev->Dot(one)) + 1e-4f)
            return one;
        if (longer != NULL)
            *longer = true;
        return other;
    }

    Ogre::Quaternion layerSlerp(State& s, Layer ly, int h, float w, const Ogre::Quaternion& a, const Ogre::Quaternion& b)
    {
        std::vector<Ogre::Quaternion>& prev = s.layerPrev[ly];
        std::vector<unsigned>& at = s.layerFrame[ly];
        if (h < 0 || h >= (int)prev.size() || h >= (int)at.size())
            return Ogre::Quaternion::Slerp(w, a, b, true);
        bool longer = false;
        Ogre::Quaternion r = keptWay(w, a, b, at[h] + 1 == s.frameNo ? &prev[h] : NULL, &longer);
        if (longer)
            diag::count("layer_kept_way");          // the longer way kept this frame (a jump avoided)
        prev[h] = r;
        at[h] = s.frameNo;
        return r;
    }

    // Reflection across the plane with unit normal n, applied to a rotation:
    // M R M = rotation about M(axis) by -angle.
    Ogre::Quaternion reflect(const Ogre::Quaternion& q, const Ogre::Vector3& n)
    {
        Ogre::Vector3 v(q.x, q.y, q.z);
        Ogre::Vector3 r = n * (2.0f * v.dotProduct(n)) - v;
        return Ogre::Quaternion(q.w, r.x, r.y, r.z);
    }

    Ogre::Vector3 reflect(const Ogre::Vector3& p, const Ogre::Vector3& n) { return p - n * (2.0f * p.dotProduct(n)); }

    std::string partnerName(const std::string& name)
    {
        if (name == "Bip01 Prop1") return "Bip01 Prop2";
        if (name == "Bip01 Prop2") return "Bip01 Prop1";
        std::string s = name;
        size_t l = s.find(" L ");
        size_t r = s.find(" R ");
        if (l != std::string::npos) s[l + 1] = 'R';
        else if (r != std::string::npos) s[r + 1] = 'L';
        return s;
    }

    int handleOf(Ogre::Skeleton* sk, const char* name)
    {
        return sk->hasBone(name) ? (int)sk->getBone(name)->getHandle() : -1;
    }

    bool descendsFrom(const Rig& rig, int i, int ancestor)
    {
        for (int p = i; p >= 0; p = rig.parent[p])
            if (p == ancestor)
                return true;
        return false;
    }

    // Builds the rig from the bind pose. False (with a counter) if the skeleton
    // is not a left-right symmetric biped with both weapon mounts.
    bool buildRig(Ogre::Skeleton* sk, Rig& rig)
    {
        int n = (int)sk->getNumBones();
        if (n <= 0 || n > 256)
            return false;
        rig.n = n;
        rig.parent.assign(n, -1);
        rig.partner.assign(n, -1);
        rig.role.assign(n, OTHER);
        std::vector<std::string> names(n);
        std::map<std::string, int> byName;
        for (int i = 0; i < n; ++i)
        {
            Ogre::OldBone* b = sk->getBone((unsigned short)i);
            if (b == NULL)
                return false;
            names[i] = b->getName();
            byName[names[i]] = i;
            Ogre::OldNode* p = b->getParent();
            rig.parent[i] = p != NULL ? (int)static_cast<Ogre::OldBone*>(p)->getHandle() : -1;
            if (rig.parent[i] >= n)
                return false;
        }
        rig.prop1 = handleOf(sk, "Bip01 Prop1");
        rig.prop2 = handleOf(sk, "Bip01 Prop2");
        int lc = handleOf(sk, "Bip01 L Clavicle"), rc = handleOf(sk, "Bip01 R Clavicle");
        int lu = handleOf(sk, "Bip01 L UpperArm"), ru = handleOf(sk, "Bip01 R UpperArm");
        int lh = handleOf(sk, "Bip01 L Hand"), rh = handleOf(sk, "Bip01 R Hand");
        if (rig.prop1 < 0 || rig.prop2 < 0 || lc < 0 || rc < 0 || lu < 0 || ru < 0 || lh < 0 || rh < 0)
        {
            diag::count("arms_rig_missing_bones");
            return false;
        }
        for (int k = 0; k < 3; ++k)
            rig.spine[k] = handleOf(sk, SPINE_BONES[k]);
        for (int i = 0; i < n; ++i)
        {
            std::map<std::string, int>::iterator it = byName.find(partnerName(names[i]));
            rig.partner[i] = it != byName.end() ? it->second : i;
        }

        // Bind pose in model space.
        std::vector<Ogre::Quaternion> W(n);
        std::vector<Ogre::Vector3> P(n);
        std::vector<bool> done(n, false);
        for (int pass = 0; pass < n; ++pass)
        {
            bool progress = false;
            for (int i = 0; i < n; ++i)
            {
                if (done[i])
                    continue;
                Ogre::OldBone* b = sk->getBone((unsigned short)i);
                int p = rig.parent[i];
                if (p < 0)
                {
                    W[i] = b->getInitialOrientation();
                    P[i] = b->getInitialPosition();
                }
                else if (done[p])
                {
                    W[i] = W[p] * b->getInitialOrientation();
                    P[i] = P[p] + W[p] * b->getInitialPosition();
                }
                else
                    continue;
                done[i] = progress = true;
            }
            if (!progress)
                break;
        }
        for (int i = 0; i < n; ++i)
            if (!done[i])
                return false;

        // The sagittal plane: through the midpoint of the upper arms, normal along them.
        Ogre::Vector3 across = P[lu] - P[ru];
        float armSpan = across.length();
        if (armSpan < 1e-4f)
            return false;
        rig.normal = across / armSpan;
        Ogre::Vector3 mid = (P[lu] + P[ru]) * 0.5f;
        // Symmetry check: each paired bone sits at the reflection of its partner.
        float worst = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            if (rig.partner[i] == i || i == rig.prop1 || i == rig.prop2)
                continue;
            Ogre::Vector3 m = mid + reflect(P[rig.partner[i]] - mid, rig.normal);
            worst = std::max(worst, (m - P[i]).length());
        }
        if (worst > 0.1f * armSpan)
        {
            diag::count("arms_rig_asymmetric");
            kit::Diag("arms_rig").kv("bones", n).kv("asymmetry", worst).kv("span", armSpan).emit();
            return false;
        }

        rig.K.resize(n);
        rig.KpInv.resize(n);
        for (int i = 0; i < n; ++i)
        {
            if (i == rig.prop1 || i == rig.prop2)
                rig.K[i] = Ogre::Quaternion::IDENTITY;   // blade +Y and edge +Z mirror, thickness flips
            else
                rig.K[i] = reflect(W[rig.partner[i]].Inverse(), rig.normal) * W[i];
        }
        for (int i = 0; i < n; ++i)
            rig.KpInv[i] = rig.parent[i] >= 0 ? rig.K[rig.parent[i]].Inverse() : Ogre::Quaternion::IDENTITY;

        for (int i = 0; i < n; ++i)
        {
            if (descendsFrom(rig, i, rc))
                rig.role[i] = RIGHT_ARM;
            else if (descendsFrom(rig, i, lc))
                rig.role[i] = LEFT_ARM;
        }
        for (int k = 0; k < 3; ++k)
            if (rig.spine[k] >= 0)
                rig.role[rig.spine[k]] = SPINE;
        rig.clipBone.assign(n, 0);
        rig.legBone.assign(n, 0);
        rig.torso.assign(n, 0);
        rig.root = -1;
        rig.thigh[0] = handleOf(sk, "Bip01 L Thigh");
        rig.thigh[1] = handleOf(sk, "Bip01 R Thigh");
        rig.bindQ.resize(n);
        rig.bindP.resize(n);
        for (int i = 0; i < n; ++i)
        {
            Ogre::OldBone* b = sk->getBone((unsigned short)i);
            rig.bindQ[i] = b->getInitialOrientation();
            rig.bindP[i] = b->getInitialPosition();
            rig.clipBone[i] = rig.role[i] != OTHER || names[i] == "Bip01 Neck" || names[i] == "Bip01 Head";
            const std::string& nm = names[i];
            rig.legBone[i] = nm == "Bip01" || nm == "Bip01 Pelvis"
                || ((nm.compare(0, 8, "Bip01 L ") == 0 || nm.compare(0, 8, "Bip01 R ") == 0)
                    && (nm.compare(8, std::string::npos, "Thigh") == 0 || nm.compare(8, std::string::npos, "Calf") == 0
                        || nm.compare(8, std::string::npos, "Foot") == 0 || nm.compare(8, std::string::npos, "Toe0") == 0));
            if (nm == "Bip01")
                rig.root = i;
            rig.torso[i] = rig.role[i] == SPINE || i == lc || i == rc || nm == "Bip01 Neck" || nm == "Bip01 Head";
        }
        rig.checkHandle = lh;
        rig.checkName = names[lh];
        rig.lu = lu;
        rig.lh = lh;
        rig.canParry = rig.parent[lu] >= 0 && descendsFrom(rig, lh, lu);
        rig.prop1OnLeftArm = descendsFrom(rig, rig.prop1, lu);
        {
            int hd = handleOf(sk, "Bip01 Head"), pv = handleOf(sk, "Bip01 Pelvis");
            Ogre::Vector3 u = hd >= 0 && pv >= 0 ? (P[hd] - P[pv]) : Ogre::Vector3::UNIT_Y;
            u = u - rig.normal * u.dotProduct(rig.normal);
            rig.upM = u.length() > 1e-4f ? u.normalisedCopy() : Ogre::Vector3::UNIT_Y;
        }

        // Uncross support: the right arm chain and the chest's axes (the frame
        // the right clavicle hangs from), from the bind pose.
        int rf = handleOf(sk, "Bip01 R Forearm");
        int head = handleOf(sk, "Bip01 Head"), pelvis = handleOf(sk, "Bip01 Pelvis");
        rig.rc = rc; rig.ru = ru; rig.rf = rf; rig.rh = rh;
        rig.canUncross = rf >= 0 && rig.parent[rc] >= 0 && rig.parent[ru] == rc && rig.parent[rf] == ru
                         && rig.parent[rh] == rf;
        if (rig.canUncross)
        {
            Ogre::Quaternion chestInv = W[rig.parent[rc]].Inverse();
            Ogre::Vector3 lat = chestInv * rig.normal;
            Ogre::Vector3 upModel = head >= 0 && pelvis >= 0 ? (P[head] - P[pelvis]) : Ogre::Vector3::UNIT_Y;
            Ogre::Vector3 up = chestInv * upModel.normalisedCopy();
            up = (up - lat * up.dotProduct(lat)).normalisedCopy();
            float side = (chestInv * (P[ru] - P[rig.parent[rc]])).dotProduct(lat) > 0.0f ? 1.0f : -1.0f;
            rig.up = up;
            rig.rside = lat * side;
            rig.fwd = up.crossProduct(rig.rside);
        }
        return true;
    }

    // Keeps the right hand on its own side of the chest, so its mirror (the left
    // hand) does not cross it: when the hand is less than `amount` x the
    // shoulder's offset out from the midline, the upper arm yaws outward about
    // the chest's up axis at the shoulder until it is (the blade turns with it).
    // Works in the frame of the clavicle's parent; bone positions are local.
    void uncrossRight(const Rig& rig, std::vector<Ogre::Quaternion>& local, const std::vector<Ogre::Vector3>& pos,
                      float amount)
    {
        if (!rig.canUncross || amount <= 0.0f)
            return;
        Ogre::Quaternion qc = local[rig.rc];
        Ogre::Vector3 pu = pos[rig.rc] + qc * pos[rig.ru];
        Ogre::Quaternion qu = qc * local[rig.ru];
        Ogre::Vector3 pf = pu + qu * pos[rig.rf];
        Ogre::Quaternion qf = qu * local[rig.rf];
        Ogre::Vector3 ph = pf + qf * pos[rig.rh];
        float m = amount * pu.dotProduct(rig.rside);
        if (ph.dotProduct(rig.rside) >= m)
            return;
        Ogre::Vector3 rel = ph - pu;
        Ogre::Vector3 relUp = rig.up * rel.dotProduct(rig.up);
        Ogre::Vector3 flat = rel - relUp;
        float a = flat.dotProduct(rig.rside), b = flat.dotProduct(rig.fwd);
        float r = std::sqrt(a * a + b * b);
        if (r < 1e-4f)
            return;
        float a2 = std::max(-r, std::min(r, m - pu.dotProduct(rig.rside) - relUp.dotProduct(rig.rside)));
        float b2 = std::sqrt(std::max(0.0f, r * r - a2 * a2));
        if (b < 0.0f)
            b2 = -b2;
        Ogre::Quaternion d = flat.getRotationTo(rig.rside * a2 + rig.fwd * b2);
        local[rig.ru] = qc.Inverse() * d * qu;
    }

    Ogre::Quaternion mirrorRot(const Rig& rig, int i, const std::vector<Ogre::Quaternion>& local)
    {
        return rig.KpInv[i] * reflect(local[rig.partner[i]], rig.normal) * rig.K[i];
    }

    Ogre::Vector3 mirrorPos(const Rig& rig, int i, const std::vector<Ogre::Vector3>& local)
    {
        return rig.KpInv[i] * reflect(local[rig.partner[i]], rig.normal);
    }

    Ogre::Quaternion worldRot(const Rig& rig, int i, const std::vector<Ogre::Quaternion>& local)
    {
        Ogre::Quaternion w = Ogre::Quaternion::IDENTITY;
        for (int p = i; p >= 0; p = rig.parent[p])
            w = local[p] * w;
        return w;
    }

    // Turns the chest toward the model's facing: each spine bone's model-space
    // frame moves toward the halfway point between itself and its mirror image
    // (a frame symmetric about the sagittal plane), more toward the top.
    void squareChest(const Rig& rig, std::vector<Ogre::Quaternion>& local, float amount)
    {
        for (int k = 0; k < 3; ++k)
        {
            int i = rig.spine[k];
            if (i < 0)
                continue;
            Ogre::Quaternion wp = rig.parent[i] >= 0 ? worldRot(rig, rig.parent[i], local) : Ogre::Quaternion::IDENTITY;
            Ogre::Quaternion wb = wp * local[i];
            Ogre::Quaternion mirrored = reflect(wb, rig.normal) * rig.K[i];
            Ogre::Quaternion symmetric = Ogre::Quaternion::Slerp(0.5f, wb, mirrored, true);
            Ogre::Quaternion goal = Ogre::Quaternion::Slerp(amount * SPINE_SHARE[k], wb, symmetric, true);
            local[i] = wp.Inverse() * goal;
        }
    }

    // Reads the animated pose. If the animation did not run since our last write
    // (the bone still holds what we wrote), reuse the last animated value, so a
    // pose is never mirrored twice.
    void readSource(Ogre::Skeleton* sk, State& s)
    {
        const Rig& rig = s.rig;
        for (int i = 0; i < rig.n; ++i)
        {
            Ogre::OldBone* b = sk->getBone((unsigned short)i);
            const Ogre::Quaternion& q = b->getOrientation();
            if (!s.started || !(q == s.written[i]))
                s.src[i] = q;
            const Ogre::Vector3& p = b->getPosition();
            if (!s.started || !(p == s.writtenP[i]))
                s.srcP[i] = p;
        }
    }

    float angleBetween(const Ogre::Quaternion& a, const Ogre::Quaternion& b)
    {
        float d = std::fabs(a.Dot(b));
        return d >= 1.0f ? 0.0f : 2.0f * std::acos(d);
    }

    // During a main-hand block, keep the frame where the right arm is furthest
    // from its guard; when the block ends, store it as a block pose.
    void trackBlock(State& s, Mode mode)
    {
        const Rig& rig = s.rig;
        if (!rig.canUncross || !s.haveGuard)
            return;
        if (mode == BLOCK)
        {
            float dev = angleBetween(s.src[rig.ru], s.guard[rig.ru]) + angleBetween(s.src[rig.rf], s.guard[rig.rf])
                        + angleBetween(s.src[rig.rh], s.guard[rig.rh]);
            if (dev > s.curDev)
            {
                s.curDev = dev;
                s.cur = s.src;
                s.curP = s.srcP;
            }
            return;
        }
        if (s.curDev > MIN_BLOCK_DEV)
        {
            s.snap[s.snapNext] = s.cur;
            s.snapP[s.snapNext] = s.curP;
            s.snapNext = (s.snapNext + 1) % BLOCK_SNAPS;
            if (s.snapCount < BLOCK_SNAPS)
                ++s.snapCount;
            diag::count("arms_block_pose_kept");
        }
        s.curDev = 0.0f;
    }

    // The parry from a recorded block: the left arm and Prop1 move toward the
    // mirror of the right arm's block pose.
    void applyParryFromBlock(const State& s, std::vector<Ogre::Quaternion>& q, std::vector<Ogre::Vector3>& p, float amount)
    {
        const Rig& rig = s.rig;
        const std::vector<Ogre::Quaternion>& sq = s.snap[s.parrySnap];
        const std::vector<Ogre::Vector3>& sp = s.snapP[s.parrySnap];
        for (int i = 0; i < rig.n; ++i)
            if (rig.role[i] == LEFT_ARM)
                q[i] = Ogre::Quaternion::Slerp(amount, q[i], mirrorRot(rig, i, sq), true);
        q[rig.prop1] = Ogre::Quaternion::Slerp(amount, q[rig.prop1], mirrorRot(rig, rig.prop1, sq), true);
        p[rig.prop1] = p[rig.prop1] + (mirrorPos(rig, rig.prop1, sp) - p[rig.prop1]) * amount;
    }

    // Model-space transform of bone i from local rotations and positions.
    void modelXf(const Rig& rig, int i, const std::vector<Ogre::Quaternion>& q, const std::vector<Ogre::Vector3>& p,
                 Ogre::Quaternion& outQ, Ogre::Vector3& outP)
    {
        outQ = Ogre::Quaternion::IDENTITY;
        outP = Ogre::Vector3::ZERO;
        for (int b = i; b >= 0; b = rig.parent[b])
        {
            outP = q[b] * outP + p[b];
            outQ = q[b] * outQ;
        }
    }

    // 0 -> 1 -> 0 over the parry: a fast rise (first 20%), held in the block until 60% so the eye
    // catches it, then the return.
    float parryEnvelope(double age)
    {
        if (age < 0.0 || age >= g_parryMs || g_parryMs <= 0.0)
            return 0.0f;
        float x = (float)(age / g_parryMs);
        float t = x < 0.2f ? x / 0.2f : x < 0.6f ? 1.0f : 1.0f - (x - 0.6f) / 0.4f;
        return t * t * (3.0f - 2.0f * t);
    }

    // The parry: the left upper arm swings (at the shoulder) so the hand moves
    // up and outward, the blade with it. Model space only needs `up` and the
    // lateral axis, so no forward-axis sign is assumed.
    void applyParry(const Rig& rig, std::vector<Ogre::Quaternion>& q, std::vector<Ogre::Vector3>& p, float amount)
    {
        if (!rig.canParry || amount <= 0.0f)
            return;
        Ogre::Quaternion uq, hq, pq, parQ, handQ2;
        Ogre::Vector3 up_, hp, pp, parP, handP2;
        modelXf(rig, rig.lu, q, p, uq, up_);
        modelXf(rig, rig.lh, q, p, hq, hp);
        modelXf(rig, rig.prop1, q, p, pq, pp);
        Ogre::Vector3 v = hp - up_;
        if (v.length() < 1e-4f)
            return;
        // rig.normal points from the right upper arm to the left one: outward for the left arm.
        Ogre::Vector3 goal = (rig.upM * 0.75f + rig.normal * 0.65f).normalisedCopy();
        Ogre::Quaternion d = Ogre::Quaternion::Slerp(amount, Ogre::Quaternion::IDENTITY, v.getRotationTo(goal), true);
        modelXf(rig, rig.parent[rig.lu], q, p, parQ, parP);
        q[rig.lu] = parQ.Inverse() * d * uq;
        if (rig.prop1OnLeftArm)
            return;                          // the mount already follows the arm
        // Carry the blade mount by the hand's motion.
        modelXf(rig, rig.lh, q, p, handQ2, handP2);
        Ogre::Quaternion dq = handQ2 * hq.Inverse();
        Ogre::Quaternion newQ = dq * pq;
        Ogre::Vector3 newP = handP2 + dq * (pp - hp);
        int pr = rig.parent[rig.prop1];
        Ogre::Quaternion prQ = Ogre::Quaternion::IDENTITY;
        Ogre::Vector3 prP = Ogre::Vector3::ZERO;
        if (pr >= 0)
            modelXf(rig, pr, q, p, prQ, prP);
        Ogre::Quaternion prInv = prQ.Inverse();
        q[rig.prop1] = prInv * newQ;
        p[rig.prop1] = prInv * (newP - prP);
    }

    // Swings one segment (bone `a`, ending at bone `b`) at its root so it points
    // along the model-space direction `dir`.
    void aimSegment(const Rig& rig, int a, int b, const Ogre::Vector3& dir,
                    std::vector<Ogre::Quaternion>& q, std::vector<Ogre::Vector3>& p)
    {
        Ogre::Quaternion aq, bq, parQ;
        Ogre::Vector3 ap, bp, parP;
        modelXf(rig, a, q, p, aq, ap);
        modelXf(rig, b, q, p, bq, bp);
        Ogre::Vector3 v = bp - ap;
        if (v.length() < 1e-4f || rig.parent[a] < 0)
            return;
        modelXf(rig, rig.parent[a], q, p, parQ, parP);
        q[a] = parQ.Inverse() * v.getRotationTo(dir) * aq;
    }

    // One point of the limp arm's chain: gravity, damping, then back onto its link (length `len` from `pivot`),
    // out of the body (never across the shoulder's plane toward the left) and inside `maxCos` of `axis`. The
    // velocity is what the step actually moved it by, so a pivot that moves drags the point along and it swings.
    void hangPoint(Ogre::Vector3& x, Ogre::Vector3& v, const Ogre::Vector3& pivot, float len, const Ogre::Vector3& g,
                   const Ogre::Vector3& outward, const Ogre::Vector3& axis, float maxCos, float keep, float h)
    {
        Ogre::Vector3 old = x;
        v = (v + g * h) * keep;
        x += v * h;
        Ogre::Vector3 d = x - pivot;
        if (d.length() < 1e-5f)
            d = axis;
        d.normalise();
        float in = -d.dotProduct(outward);            // > 0: pointing across the body
        if (in > 0.0f)
            d = (d + outward * in).normalisedCopy();
        float c = d.dotProduct(axis);
        if (c < maxCos)                               // past the cone: back onto its rim
        {
            Ogre::Vector3 side = d - axis * c;
            if (side.length() < 1e-5f)
                d = axis;
            else
                d = (axis * maxCos + side.normalisedCopy() * std::sqrt(1.0f - maxCos * maxCos)).normalisedCopy();
        }
        x = pivot + d * len;
        v = (x - old) / h;
    }

    // LEFTY: the crippled right arm hangs limp beside the body and swings: the elbow and the hand are two
    // points on rigid links under gravity (model space), pulled along by the shoulder as the body walks, turns
    // and cuts. Vanilla's own partial ragdoll is not used: switching it on makes the game sheathe the weapon,
    // and the mirror would overwrite it. `[Animations] limp_arm_swing=0` gives one fixed pose, straight down.
    void hangRight(State& s, std::vector<Ogre::Quaternion>& q, std::vector<Ogre::Vector3>& p)
    {
        const Rig& rig = s.rig;
        if (!rig.canUncross)
            return;
        // rig.normal points from the right arm to the left: outward is -normal.
        Ogre::Vector3 rest = (-rig.upM - rig.normal * 0.12f).normalisedCopy();
        Ogre::Quaternion tq;
        Ogre::Vector3 sh, el, ha;
        modelXf(rig, rig.ru, q, p, tq, sh);
        modelXf(rig, rig.rf, q, p, tq, el);
        modelXf(rig, rig.rh, q, p, tq, ha);
        float l1 = (el - sh).length(), l2 = (ha - el).length();
        if (!g_limpSwing || l1 < 1e-4f || l2 < 1e-4f)
        {
            aimSegment(rig, rig.ru, rig.rf, rest, q, p);
            aimSegment(rig, rig.rf, rig.rh, rest, q, p);
            return;
        }
        double now = defense::frameMs();
        float dt = s.hangT < 0.0 ? -1.0f : (float)(now - s.hangT) / 1000.0f;
        s.hangT = now;
        if (dt < 0.0f || dt > 0.5f)                   // the first frame, or back after a gap: at rest
        {
            s.hangE = sh + rest * l1;
            s.hangH = s.hangE + rest * l2;
            s.hangVE = s.hangVH = Ogre::Vector3::ZERO;
            diag::count("arms_limp_reset");
        }
        else if (dt > 0.0f)                           // a paused game: the arm holds where it is
        {
            int steps = std::min(8, std::max(1, (int)std::ceil(dt * 90.0f)));
            float h = dt / (float)steps;
            float keep = std::max(0.0f, 1.0f - HANG_DAMP * h);
            Ogre::Vector3 g = -rig.upM * HANG_GRAVITY;
            for (int k = 0; k < steps; ++k)
            {
                hangPoint(s.hangE, s.hangVE, sh, l1, g, -rig.normal, rest, HANG_SHOULDER_COS, keep, h);
                Ogre::Vector3 upper = (s.hangE - sh).normalisedCopy();
                hangPoint(s.hangH, s.hangVH, s.hangE, l2, g, -rig.normal, upper, HANG_ELBOW_COS, keep, h);
            }
            if (diag::stage())
                diag::count("arms_limp_swing_frame");
        }
        aimSegment(rig, rig.ru, rig.rf, (s.hangE - sh).normalisedCopy(), q, p);
        aimSegment(rig, rig.rf, rig.rh, (s.hangH - s.hangE).normalisedCopy(), q, p);
    }

    // An animation by name, by walking the skeleton's own list (see resolveClip).
    Ogre::Animation* findAnimation(Ogre::Skeleton* sk, const char* name)
    {
        unsigned short n = sk->getNumAnimations();
        for (unsigned short i = 0; i < n; ++i)
        {
            Ogre::Animation* a = sk->getAnimation(i);
            if (a != NULL && a->getName() == name)
                return a;
        }
        return NULL;
    }

    // The track names of a rig (own and linked sources, by index: names are hashed in Kenshi's
    // OgreMain, see resolveClip), once per skeleton name. Caller holds g_lock. NULL when the walk
    // found almost nothing (then every track counts as present).
    const std::set<std::string>* trackNamesOf(Ogre::Skeleton* sk)
    {
        const std::string& key = sk->getName();
        std::map<std::string, std::set<std::string> >::iterator it = g_trackNames.find(key);
        if (it != g_trackNames.end())
            return &it->second;
        if (g_trackUnknown.count(key) > 0)
            return NULL;
        std::set<std::string> names;
        try
        {
            unsigned short n = sk->getNumAnimations();
            for (unsigned short i = 0; i < n; ++i)
            {
                Ogre::Animation* a = sk->getAnimation(i);
                if (a != NULL)
                    names.insert(a->getName());
            }
            Ogre::Skeleton::LinkedSkeletonAnimSourceIterator li = sk->getLinkedSkeletonAnimationSourceIterator();
            while (li.hasMoreElements())
            {
                const Ogre::LinkedSkeletonAnimationSource& src = li.getNext();
                Ogre::Skeleton* ls = src.pSkeleton.get();
                if (ls == NULL)
                    continue;
                unsigned short m = ls->getNumAnimations();
                for (unsigned short i = 0; i < m; ++i)
                {
                    Ogre::Animation* a = ls->getAnimation(i);
                    if (a != NULL)
                        names.insert(a->getName());
                }
            }
        }
        catch (...)
        {
            names.clear();
        }
        if (names.size() < 20)
        {
            if (g_trackUnknown.size() < 256)
                g_trackUnknown.insert(key);
            diag::count("base_tracks_unknown_rig");
            return NULL;
        }
        if (g_trackNames.size() > 256)
            return NULL;
        return &(g_trackNames[key] = names);
    }

    // The length of track `name` as Ogre would find it (own tracks, then the linked sources in
    // order), by index (names are hashed, see resolveClip); < 0: not found.
    float trackLength(Ogre::Skeleton* sk, const std::string& name)
    {
        try
        {
            unsigned short n = sk->getNumAnimations();
            for (unsigned short i = 0; i < n; ++i)
            {
                Ogre::Animation* a = sk->getAnimation(i);
                if (a != NULL && a->getName() == name)
                    return a->getLength();
            }
            Ogre::Skeleton::LinkedSkeletonAnimSourceIterator li = sk->getLinkedSkeletonAnimationSourceIterator();
            while (li.hasMoreElements())
            {
                Ogre::Skeleton* ls = li.getNext().pSkeleton.get();
                if (ls == NULL)
                    continue;
                unsigned short m = ls->getNumAnimations();
                for (unsigned short i = 0; i < m; ++i)
                {
                    Ogre::Animation* a = ls->getAnimation(i);
                    if (a != NULL && a->getName() == name)
                        return a->getLength();
                }
            }
        }
        catch (...)
        {
        }
        return -1.0f;
    }

    // A full-body clip is authored on its base track's timeline (same length). If an animation pack
    // retimed the base, the legs and root would ride a different motion: then the clip drives only the
    // upper body. Once per fighter and clip; one line per rig and track. Caller holds g_lock.
    bool bodyAllowed(Ogre::Skeleton* sk, State& s, int k, Ogre::Animation* clip)
    {
        if (s.bodyOk[k] >= 0)
            return s.bodyOk[k] == 1;
        s.bodyOk[k] = 1;
        const char* base = moves::CLIPS[k].baseTrack;
        if (base == NULL || clip == NULL)
            return true;
        float baseLen = trackLength(sk, base), clipLen = clip->getLength();
        if (baseLen <= 0.0f || clipLen <= 0.0f || fabs(baseLen - clipLen) <= 0.05f * clipLen)
            return true;
        s.bodyOk[k] = 0;
        diag::count("attack_clip_upper_only");
        static std::set<std::string> logged;
        std::string key = sk->getName() + "|" + base;
        if (logged.size() < 64 && logged.insert(key).second && g_retimeLog.size() < 64)
        {
            char buf[256];
            sprintf_s(buf, "rig=%s track=%s base_s=%.3f clip_s=%.3f", sk->getName().c_str(), base, baseLen, clipLen);
            g_retimeLog.push_back(buf);
        }
        return false;
    }

    // Finds clip k for this skeleton once (the skeleton's own tracks, else our file).
    Ogre::Animation* resolveClip(Ogre::Skeleton* sk, State& s, int k)
    {
        if (s.clipLooked[k])
            return s.clipAnim[k];
        s.clipLooked[k] = true;
        s.clipAnim[k] = NULL;
        s.clipScale[k] = 1.0f;
        int own = -1, linked = -1;
        const char* source = "none";
        try
        {
            own = (int)sk->getNumAnimations();
            linked = 0;
            Ogre::Skeleton::LinkedSkeletonAnimSourceIterator li = sk->getLinkedSkeletonAnimationSourceIterator();
            while (li.hasMoreElements())
            {
                li.getNext();
                ++linked;
            }
            // Kenshi's OgreMain takes animation NAMES AS A HASH: its exports are
            // Skeleton::hasAnimation(unsigned int) / getAnimation(unsigned int[, linker]),
            // while KenshiLib's OgreSkeleton.h declares them with `const String&`, so
            // a call by name passes a pointer as the hash and never matches. Look tracks
            // up by index and Animation::getName() instead.
            // 1. The game linked or merged our track into this skeleton.
            // 2. Our own file, loaded by playClip.
            Ogre::Skeleton* own3 = g_l3State[s.sex] == 1 ? g_l3Skel[s.sex].get() : NULL;
            Ogre::Skeleton* where[2] = { sk, own3 };
            const char* whereName[2] = { "rig", "file" };
            for (int w = 0; w < 2 && s.clipAnim[k] == NULL; ++w)
            {
                Ogre::Skeleton* src = where[w];
                if (src == NULL)
                    continue;
                Ogre::Animation* a = findAnimation(src, CLIP_TRACK[k]);
                if (a == NULL)
                    continue;
                // Tracks address bones by handle: every keyed handle must name the
                // same bone on this character's skeleton.
                bool fits = true;
                const Ogre::Animation::OldNodeTrackList& tl = a->_getOldNodeTrackList();
                for (Ogre::Animation::OldNodeTrackList::const_iterator it = tl.begin(); it != tl.end(); ++it)
                {
                    unsigned short h = it->first;
                    if ((int)h >= s.rig.n || h >= src->getNumBones()
                        || src->getBone(h)->getName() != sk->getBone(h)->getName())
                        fits = false;
                }
                if (fits)
                {
                    s.clipAnim[k] = a;
                    s.clipScale[k] = 1.0f;
                    source = whereName[w];
                }
                else
                    diag::count("l3_track_bad_bones");
            }
        }
        catch (...)
        {
            s.clipAnim[k] = NULL;
            diag::count("l3_lookup_fault");
        }
        bool found = s.clipAnim[k] != NULL;
        if (!found)
            diag::count(CLIP_MISSING[k]);
        bool& logged = found ? g_clipFoundLogged[k] : g_clipMissLogged[k];
        if (!logged)
        {
            logged = true;
            ClipLog e = { k, found, own, linked, found ? s.clipAnim[k]->getLength() : 0.0f,
                          found ? (int)s.clipAnim[k]->_getOldNodeTrackList().size() : 0, s.clipScale[k], source, s.sex };
            g_clipLog.push_back(e);
        }
        return s.clipAnim[k];
    }

    // Clips a block starts or plays through (a BLOCK mode is no new action for them).
    bool blockClip(int k)
    {
        return k == arms::CLIP_SCISSOR || k == arms::CLIP_BLOCK_LEFT || k == arms::CLIP_OFF_BLOCK
            || k == arms::CLIP_DUAL_IMPACT_L || k == arms::CLIP_DUAL_IMPACT_R
            // the angled and overhead guards and their blocked hits
            || k == arms::CLIP_ANGLED_L || k == arms::CLIP_ANGLED_R || k == arms::CLIP_ANGLED_L_IMPACT
            || k == arms::CLIP_ANGLED_R_IMPACT || k == arms::CLIP_MAIN_OVERHEAD || k == arms::CLIP_MAIN_OVERHEAD_IMPACT
            // the one-blade main-hand block and its blocked hit
            || k == arms::CLIP_MAIN_BLOCK || k == arms::CLIP_MAIN_IMPACT
            // the back guards
            || k == arms::CLIP_BACK_LOW || k == arms::CLIP_BACK_LOW_MAIN || k == arms::CLIP_BACK_HIGH
            || k == arms::CLIP_BACK_HIGH_MAIN;
    }

    // Reactions to a hit: they replace whatever clip plays.
    bool reactionClip(int k)
    {
        return k == arms::CLIP_BLOCK_LEFT || k == arms::CLIP_OFF_BLOCK
            || k == arms::CLIP_DUAL_IMPACT_L || k == arms::CLIP_DUAL_IMPACT_R
            || k == arms::CLIP_ANGLED_L_IMPACT || k == arms::CLIP_ANGLED_R_IMPACT || k == arms::CLIP_MAIN_OVERHEAD_IMPACT
            || k == arms::CLIP_MAIN_IMPACT || k == arms::CLIP_HIT_TAKEN
            || k == arms::CLIP_BACK_LOW || k == arms::CLIP_BACK_LOW_MAIN || k == arms::CLIP_BACK_HIGH
            || k == arms::CLIP_BACK_HIGH_MAIN;
    }

    bool g_angledSwap = false;              // [Animations] angled_block_swap (if the sides show mirrored in game)

    // The held guard of a dual fighter's main-hand block, by the blow's direction (the block technique's
    // impact direction, else the attacker's): overhead for a downward blow, angled left / right for a blow
    // from that side, the scissor X for the rest (and unknown).
    int blockClipFor(int dir)
    {
        bool ang = g_clipOn[arms::CLIP_ANGLED_L], over = g_clipOn[arms::CLIP_MAIN_OVERHEAD];
        int left = g_angledSwap ? arms::CLIP_ANGLED_R : arms::CLIP_ANGLED_L;
        int right = g_angledSwap ? arms::CLIP_ANGLED_L : arms::CLIP_ANGLED_R;
        switch (dir)
        {
        case CUT_DOWNWARD: case CUT_REAR_DOWNWARD:
            if (over) return arms::CLIP_MAIN_OVERHEAD;
            break;
        case CUT_LEFT: case CUT_REAR_LEFT:
            if (ang) return left;
            break;
        case CUT_RIGHT: case CUT_REAR_RIGHT:
            if (ang) return right;
            break;
        default:
            break;
        }
        // from the front (or unknown), the one-blade main-hand block at random instead of the scissor X
        // ([Animations] main_block_single)
        if (g_clipOn[arms::CLIP_MAIN_BLOCK] && (!g_clipOn[arms::CLIP_SCISSOR] || (rand() & 1)))
            return arms::CLIP_MAIN_BLOCK;
        return g_clipOn[arms::CLIP_SCISSOR] ? (int)arms::CLIP_SCISSOR : -1;
    }

    // The guard of a blow from behind, for the second blade (a parry) or the main blade (a hit it blocked):
    // high for a downward blow, low for the others, either at random when the blow's direction is unknown.
    // -1: [Animations] back_guard off.
    int backGuardClip(bool mainBlade, int dir)
    {
        if (!g_clipOn[arms::CLIP_BACK_LOW])
            return -1;
        bool high = dir == CUT_DOWNWARD || dir == CUT_REAR_DOWNWARD || (dir < 0 && (rand() & 1));
        return mainBlade ? (high ? arms::CLIP_BACK_HIGH_MAIN : arms::CLIP_BACK_LOW_MAIN)
                         : (high ? arms::CLIP_BACK_HIGH : arms::CLIP_BACK_LOW);
    }

    // The off-hand parry's clip: the one-blade block when [Animations] offhand_block is on, else the high
    // block, else none (the parry pose).
    int parryClip()
    {
        if (g_clipOn[arms::CLIP_OFF_BLOCK])
            return arms::CLIP_OFF_BLOCK;
        return g_clipOn[arms::CLIP_BLOCK_LEFT] ? (int)arms::CLIP_BLOCK_LEFT : -1;
    }

    // Overlays the playing clip on the target pose (spine, neck, head, arms,
    // blade mounts). False when no clip plays (none asked, ended, or missing).
    bool applyClip(Ogre::Skeleton* sk, State& s, Mode mode)
    {
        if (s.clip < 0)
            return false;
        int k = s.clip;
        if (mode == SWAP || mode == TWIN || mode == LEFTY)
        {
            s.clip = -1;                    // a swing of the off blade owns the arms
            diag::count("l3_clip_cut_by_strike");
            return false;
        }
        Ogre::Animation* a = resolveClip(sk, s, k);
        if (a == NULL)
        {
            s.clip = -1;                    // fall back to the mirrored game pose
            return false;
        }
        float len = a->getLength();
        float age = (float)((defense::frameMs() - s.clipStart) / 1000.0);   // frame clock, no lock
        if (age >= len || age < -0.5f)
        {
            s.clip = -1;
            return false;
        }
        if (age < 0.0f)
            age = 0.0f;
        if (!s.clipCounted)
        {
            s.clipCounted = true;
            s.clipMode = (int)mode;
            diag::count(CLIP_PLAYED[k]);
        }
        // A clip gives way to combat (a hit mid-draw would leave two animations fighting over the
        // arms): a swing or a block that starts during it, or a hit that lands, fades it out in
        // CLIP_YIELD_S. A new action = the mode changes to anything but the plain guard (a block
        // during the scissor block is that move's own trigger and keeps it).
        bool action = (int)mode != s.clipMode && mode != MIRROR && !(blockClip(k) && mode == BLOCK);
        s.clipMode = (int)mode;
        if (s.clipYield < 0.0f && action)
        {
            s.clipYield = age;
            diag::count("l3_clip_yield_action");
        }
        if (s.clipYield < 0.0f && s.clipHit)
        {
            s.clipYield = age;
            diag::count("l3_clip_yield_hit");
        }
        // a swing or a block another plugin plays outside the game's combat never changes the mode;
        // it takes the body the same way
        if (s.clipYield < 0.0f && s.foreign)
        {
            s.clipYield = age;
            diag::count("l3_clip_yield_foreign");
        }
        // The fighter went down (a knockdown, a KO, a ragdoll, carried): the game owns the body. A stagger
        // keeps the clip: `hit taken` and the block impacts are made to play over the game's reaction.
        if (s.clipYield < 0.0f && s.reacting == arms::REACT_DOWN)
        {
            s.clipYield = age;
            diag::count("l3_clip_yield_down");
        }
        float give = 1.0f;
        if (s.clipYield >= 0.0f)
        {
            give = 1.0f - (age - s.clipYield) / CLIP_YIELD_S;
            if (give <= 0.0f)
            {
                s.clip = -1;
                return false;
            }
        }
        float in = std::min(1.0f, age / CLIP_IN_S), out = std::min(1.0f, (len - age) / CLIP_OUT_S);
        float env = in * in * (3.0f - 2.0f * in) * out * out * (3.0f - 2.0f * out) * give * give * (3.0f - 2.0f * give);
        const Rig& rig = s.rig;
        const Ogre::Animation::OldNodeTrackList& tl = a->_getOldNodeTrackList();
        Ogre::TimeIndex ti(age);
        Ogre::TransformKeyFrame kf(NULL, 0.0f);
        // A full-body triggered clip (the dual block impacts, the one-blade blocks) also drives the
        // root, the pelvis and the legs, and the root's height only: the game keeps the ground position.
        bool body = moves::fullBody(k);
        for (Ogre::Animation::OldNodeTrackList::const_iterator it = tl.begin(); it != tl.end(); ++it)
        {
            int h = (int)it->first;
            if (h >= rig.n || !(rig.clipBone[h] || (body && rig.legBone[h])) || it->second == NULL)
                continue;
            it->second->getInterpolatedKeyFrame(ti, &kf);
            Ogre::Quaternion q = rig.bindQ[h] * kf.getRotation();
            s.tgt[h] = layerSlerp(s, LY_TRIGGERED, h, env, s.tgt[h], q);
            if (h == rig.prop1 || h == rig.prop2)
            {
                Ogre::Vector3 p = rig.bindP[h] + kf.getTranslate() * s.clipScale[k];
                s.tgtP[h] = s.tgtP[h] + (p - s.tgtP[h]) * env;
            }
            else if (body && h == rig.root)
            {
                float y = rig.bindP[h].y + kf.getTranslate().y * s.clipScale[k];     // model up (Ogre Y)
                s.tgtP[h].y += (y - s.tgtP[h].y) * env;
                s.rootDriven = true;
            }
        }
        if (body && diag::stage())
            diag::count("l3_full_body_reaction_frame");
        // The block clips are authored facing forward on the vanilla block's body. Another mod's
        // block may stand side-on, and the X would follow the turned pelvis and point past the enemy.
        // Square the chest to the fighter's facing (the enemy he blocks) while the clip plays.
        if (k == arms::CLIP_SCISSOR || k == arms::CLIP_BLOCK_LEFT)
            squareChest(rig, s.tgt, env);
        return true;
    }

    // An attack clip over the TWIN pose, sampled at the technique's progress (the clip is
    // authored in the base track's frames). Short fades at both ends.
    bool applyAttackClip(Ogre::Skeleton* sk, State& s)
    {
        if (s.attackClip < 0)
            return false;
        int k = s.attackClip;
        Ogre::Animation* a = resolveClip(sk, s, k);
        if (a == NULL)
            return false;                   // the TWIN mirror stays
        if (!s.attackCounted)
        {
            s.attackCounted = true;
            diag::count(CLIP_PLAYED[k]);
        }
        float x = std::max(0.0f, std::min(1.0f, s.attackProgress));
        float in = std::min(1.0f, x / 0.05f), out = std::min(1.0f, (1.0f - x) / 0.06f);
        // The long clips fade in and out through the mode blend (ATTACK_BLEND_S, from what was on
        // screen); their own envelope would fade toward the base track's poses instead.
        if (k == arms::CLIP_DASH || k == arms::CLIP_TWIN_SWEEP
            || k == arms::CLIP_SPIN)
            in = out = 1.0f;
        float env = in * in * (3.0f - 2.0f * in) * out * out * (3.0f - 2.0f * out);
        const Rig& rig = s.rig;
        const Ogre::Animation::OldNodeTrackList& tl = a->_getOldNodeTrackList();
        Ogre::TimeIndex ti(x * a->getLength());
        Ogre::TransformKeyFrame kf(NULL, 0.0f);
        // The special moves are full-body clips: they also drive the pelvis, the legs and the root's
        // turn and height (the lunge and the crouched run). The root's ground position stays the
        // game's, so the technique's travel is unchanged.
        bool body = moves::fullBody(k) && bodyAllowed(sk, s, k, a);
        for (Ogre::Animation::OldNodeTrackList::const_iterator it = tl.begin(); it != tl.end(); ++it)
        {
            int h = (int)it->first;
            if (h >= rig.n || !(rig.clipBone[h] || (body && rig.legBone[h])) || it->second == NULL)
                continue;
            it->second->getInterpolatedKeyFrame(ti, &kf);
            s.tgt[h] = layerSlerp(s, LY_SPECIAL, h, env, s.tgt[h], rig.bindQ[h] * kf.getRotation());
            if (h == rig.prop1 || h == rig.prop2)
            {
                Ogre::Vector3 p = rig.bindP[h] + kf.getTranslate() * s.clipScale[k];
                s.tgtP[h] = s.tgtP[h] + (p - s.tgtP[h]) * env;
            }
            else if (h == rig.root)
            {
                float y = rig.bindP[h].y + kf.getTranslate().y * s.clipScale[k];     // model up (Ogre Y)
                s.tgtP[h].y += (y - s.tgtP[h].y) * env;
                // The clip's own speed curve. Its exported root travel is only the offset from the
                // base track's, so it adds to the game's ground position: the body runs ahead of or
                // behind the fighter by it, zero at both ends. Model forward is Ogre +Z, model left
                // Ogre +X (the sideways drift, and the hips' shift over planted feet).
                if (moves::rootTravel(k))
                    s.tgtP[h].z += kf.getTranslate().z * s.clipScale[k] * env;
                if (moves::sideTravel(k))
                    s.tgtP[h].x += kf.getTranslate().x * s.clipScale[k] * env;
                s.rootDriven = true;
            }
        }
        if (body && diag::stage())      // per-frame counter, stage only
            diag::count("l3_full_body_frame");
        return true;
    }

    // The run hold is authored on the game's own run cycle (`run lower`: the pelvis swings 42 deg a
    // stride pair and the clip's spine takes that swing), so it must play in step with the legs, or the
    // head wobbles. The game's time in that track is not readable from here; the legs' pose is.
    // legMatch: how close the two thighs of the animated pose are to `run lower` at time t (1: the same).
    const char* RUN_BASE_TRACK = "run lower";
    const float RUN_MATCH_MIN = 0.985f;        // mean |dot| of the two thighs: ~20 deg off
    float legMatch(const State& s, Ogre::OldNodeAnimationTrack* const tr[2], float t)
    {
        const Rig& rig = s.rig;
        Ogre::TransformKeyFrame kf(NULL, 0.0f);
        Ogre::TimeIndex ti(t);
        float sum = 0.0f;
        for (int k = 0; k < 2; ++k)
        {
            int h = rig.thigh[k];
            tr[k]->getInterpolatedKeyFrame(ti, &kf);
            sum += std::fabs((rig.bindQ[h] * kf.getRotation()).Dot(s.src[h]));
        }
        return sum * 0.5f;
    }

    // An animation by name in the skeleton's own list, else in its linked sources in order (as Ogre
    // finds it): a run may come from a linked animation file.
    Ogre::Animation* findAnimationLinked(Ogre::Skeleton* sk, const char* name)
    {
        Ogre::Animation* a = findAnimation(sk, name);
        if (a != NULL)
            return a;
        Ogre::Skeleton::LinkedSkeletonAnimSourceIterator li = sk->getLinkedSkeletonAnimationSourceIterator();
        while (li.hasMoreElements())
        {
            Ogre::Skeleton* ls = li.getNext().pSkeleton.get();
            if (ls != NULL && (a = findAnimation(ls, name)) != NULL)
            {
                diag::count("run_base_from_linked");
                return a;
            }
        }
        return NULL;
    }

    // The stride's phase (0-1 along `run lower`) read from the legs; false: no such track on this
    // rig, or the legs are not in it closer than minMatch (another mod's run, a blend into a walk).
    bool legPhase(Ogre::Skeleton* sk, State& s, float minMatch, float& phase, float& baseLen)
    {
        const Rig& rig = s.rig;
        if (rig.thigh[0] < 0 || rig.thigh[1] < 0 || rig.thigh[0] >= rig.n || rig.thigh[1] >= rig.n)
            return false;
        if (!s.runBaseTried)
        {
            s.runBaseTried = true;
            s.runBase = findAnimationLinked(sk, RUN_BASE_TRACK);
        }
        Ogre::Animation* base = s.runBase;
        if (base == NULL || base->getLength() <= 0.0f)
            return false;
        Ogre::OldNodeAnimationTrack* tr[2] = { NULL, NULL };
        const Ogre::Animation::OldNodeTrackList& tl = base->_getOldNodeTrackList();
        for (Ogre::Animation::OldNodeTrackList::const_iterator it = tl.begin(); it != tl.end(); ++it)
            for (int k = 0; k < 2; ++k)
                if ((int)it->first == rig.thigh[k])
                    tr[k] = it->second;
        if (tr[0] == NULL || tr[1] == NULL)
            return false;
        float len = base->getLength();
        const int N = 28;
        float step = len / (float)N, best = -1.0f, bt = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            float m = legMatch(s, tr, step * (float)i);
            if (m > best)
            {
                best = m;
                bt = step * (float)i;
            }
        }
        if (best < minMatch)
            return false;
        float lo = bt - step, hi = bt + step;       // the match is smooth round its top: narrow in on it
        for (int i = 0; i < 6; ++i)
        {
            float a = lo + (hi - lo) / 3.0f, b = hi - (hi - lo) / 3.0f;
            float ma = legMatch(s, tr, a - len * std::floor(a / len));
            float mb = legMatch(s, tr, b - len * std::floor(b / len));
            if (ma < mb)
                lo = a;
            else
                hi = b;
        }
        float t = 0.5f * (lo + hi);
        phase = t / len - std::floor(t / len);
        baseLen = len;
        return true;
    }

    // The full-body stance of a run by [Animations] run_style (nitoryu run style): 0 none (the trail alone),
    // 1-3 stance 1-3, 4 stance 1 or 3, drawn once per fighter. A stance whose track is missing leaves the
    // trail alone.
    int runStanceFor(State& s)
    {
        int style = cfg::i(cfg::RUN_STYLE);
        if (style == 4)
        {
            if (s.runMix < 0)
                s.runMix = (rand() & 1) ? 3 : 1;
            style = s.runMix;
        }
        diag::count(style == 1 ? "run_style_stance_1" : style == 2 ? "run_style_stance_2" : style == 3 ? "run_style_stance_3"
                    : "run_style_trail");
        switch (style)
        {
        case 1:  return arms::CLIP_RUN_STANCE_A;
        case 2:  return arms::CLIP_RUN_STANCE_B;
        case 3:  return arms::CLIP_RUN_STANCE_C;
        default: return -1;
        }
    }

    // A run hold (CLIP_RUN_*): loops over the upper body while the fighter runs in the plain
    // guard, in step with the legs (legPhase), fading in and out. The legs, pelvis and bob stay
    // the game's, unless a full-body stance (runStanceFor) is laid over it while the legs are locked.
    void applyRunClip(Ogre::Skeleton* sk, State& s, Mode mode, bool other)
    {
        bool want = s.running && mode == MIRROR && !other && !s.foreign && s.reacting == arms::REACT_NONE
                    && g_clipOn[arms::CLIP_RUN_TRAIL];
        double now = defense::frameMs();       // frame clock, no lock
        float dt = (float)std::max(0.0, std::min(250.0, now - s.runLast)) / 1000.0f;
        s.runLast = now;
        if (want && s.runClip < 0)
        {
            // always the trail
            s.runClip = arms::CLIP_RUN_TRAIL;
            s.runStart = now;
            s.runW = 0.0f;
            s.runCounted = false;
            s.runPhaseCounted = false;
            s.runStance = runStanceFor(s);
            s.runFullW = 0.0f;
            s.runStanceCounted = false;
            s.runStanceCounted2 = false;
        }
        if (s.runClip < 0)
            return;
        // A swing or a block cuts the run hold in CLIP_YIELD_S instead of the slower run fade, so no
        // attack (ours or another mod's) starts under it; the frames that still overlap are counted.
        // A stagger or a fall cuts it as fast.
        bool attack = mode == HOLD || mode == SWAP || mode == TWIN || mode == BLOCK || s.foreign
                      || s.reacting != arms::REACT_NONE;
        if (attack && s.runW > 0.0f)
            diag::count("run_hold_overlaps_attack");
        s.runW += (want ? dt : -dt) / (attack ? CLIP_YIELD_S : RUN_FADE_S);
        if (s.runW <= 0.0f && !want)
        {
            s.runClip = -1;
            s.runW = 0.0f;
            return;
        }
        s.runW = std::min(1.0f, std::max(0.0f, s.runW));
        Ogre::Animation* a = resolveClip(sk, s, s.runClip);
        if (a == NULL || a->getLength() <= 0.0f)
            return;
        if (!s.runCounted)
        {
            s.runCounted = true;
            diag::count(CLIP_PLAYED[s.runClip]);
        }
        float env = s.runW * s.runW * (3.0f - 2.0f * s.runW);
        const Rig& rig = s.rig;
        const Ogre::Animation::OldNodeTrackList& tl = a->_getOldNodeTrackList();
        // the clip's time is the run track's time (17 frames at 30 fps over the track's 14 at 25);
        // legs that are not in that track: the phase runs on at the track's own pace
        float phase = 0.0f, baseLen = a->getLength();
        bool locked = false;
        // a lock already held is kept down to RUN_UNLOCK_MIN, so a turn or the start of a run near the
        // threshold does not flip it every frame
        const float minMatch = s.runPhaseCounted && s.runLocked ? RUN_UNLOCK_MIN : RUN_MATCH_MIN;
        try { locked = legPhase(sk, s, minMatch, phase, baseLen); }
        catch (...) { locked = false; }
        if (locked)
        {
            // a small difference to the legs is taken as it is; a big one (back from free running) eases in
            float diff = phase - s.runPhase;
            diff -= std::floor(diff + 0.5f);                    // the shorter way round, -0.5..0.5
            if (!s.runPhaseCounted || std::fabs(diff) <= RUN_PHASE_SNAP)
                s.runPhase = phase;
            else
            {
                s.runPhase += dt / baseLen + diff * std::min(1.0f, dt / RUN_PHASE_EASE_S);
                s.runPhase -= std::floor(s.runPhase);
            }
        }
        else
        {
            s.runPhase += dt / baseLen;
            s.runPhase -= std::floor(s.runPhase);
        }
        if (!s.runPhaseCounted || locked != s.runLocked)
        {
            if (s.runPhaseCounted)
                diag::count("run_hold_lock_flips");
            s.runPhaseCounted = true;
            s.runLocked = locked;
            diag::count(locked ? "run_hold_phase_locked" : "run_hold_phase_free");
        }
        // A full-body stance takes over from the trail while the legs are the vanilla run's (locked) and gives
        // way to it when they are not (another mod's run, a blend into a walk), over RUN_STANCE_FADE_S.
        if (s.runStance >= 0)
        {
            s.runFullW += (locked ? dt : -dt) / RUN_STANCE_FADE_S;
            s.runFullW = std::min(1.0f, std::max(0.0f, s.runFullW));
            if (!s.runStanceCounted || locked != s.runStanceOn)
            {
                diag::count(locked ? "run_stance_on" : "run_stance_gives_way");
                s.runStanceOn = locked;
                s.runStanceCounted = true;
            }
        }
        // Legs that are not in the authored run: the torso's swing would fight them, so it takes only a share
        // (the setting nitoryu run hold free weight, [Animations] run_hold_free_weight; its range is 0-1).
        const float torsoEnv = locked ? env : env * cfg::f(cfg::RUN_HOLD_FREE_WEIGHT);
        Ogre::TimeIndex ti(std::min(s.runPhase * baseLen, a->getLength() - 0.001f));
        Ogre::TransformKeyFrame kf(NULL, 0.0f);
        for (Ogre::Animation::OldNodeTrackList::const_iterator it = tl.begin(); it != tl.end(); ++it)
        {
            int h = (int)it->first;
            if (h >= rig.n || !rig.clipBone[h] || it->second == NULL)
                continue;
            it->second->getInterpolatedKeyFrame(ti, &kf);
            s.tgt[h] = layerSlerp(s, LY_RUN, h, rig.torso[h] ? torsoEnv : env, s.tgt[h], rig.bindQ[h] * kf.getRotation());
            if (h == rig.prop1 || h == rig.prop2)
            {
                Ogre::Vector3 p = rig.bindP[h] + kf.getTranslate() * s.clipScale[s.runClip];
                s.tgtP[h] = s.tgtP[h] + (p - s.tgtP[h]) * env;
            }
        }
        // The stance over the trail, the whole body (pelvis, legs, the root's turn and height: its own lean and
        // stride, authored on run lower's phase, so the feet keep the game's ground speed).
        const float fw = env * s.runFullW * s.runFullW * (3.0f - 2.0f * s.runFullW);
        Ogre::Animation* st = s.runStance >= 0 && fw > 0.0f ? resolveClip(sk, s, s.runStance) : NULL;
        if (st == NULL || st->getLength() <= 0.0f)
            return;
        if (!s.runStanceCounted2)
        {
            s.runStanceCounted2 = true;
            diag::count(CLIP_PLAYED[s.runStance]);
        }
        Ogre::TimeIndex ts(std::min(s.runPhase * st->getLength(), st->getLength() - 0.001f));
        const Ogre::Animation::OldNodeTrackList& sl = st->_getOldNodeTrackList();
        for (Ogre::Animation::OldNodeTrackList::const_iterator it = sl.begin(); it != sl.end(); ++it)
        {
            int h = (int)it->first;
            if (h >= rig.n || !(rig.clipBone[h] || rig.legBone[h]) || it->second == NULL)
                continue;
            it->second->getInterpolatedKeyFrame(ts, &kf);
            s.tgt[h] = layerSlerp(s, LY_RUN_STANCE, h, fw, s.tgt[h], rig.bindQ[h] * kf.getRotation());
            if (h == rig.prop1 || h == rig.prop2)
            {
                Ogre::Vector3 p = rig.bindP[h] + kf.getTranslate() * s.clipScale[s.runStance];
                s.tgtP[h] = s.tgtP[h] + (p - s.tgtP[h]) * fw;
            }
            else if (h == rig.root)
            {
                float y = rig.bindP[h].y + kf.getTranslate().y * s.clipScale[s.runStance];     // model up (Ogre Y)
                s.tgtP[h].y += (y - s.tgtP[h].y) * fw;
                s.rootDriven = true;
            }
        }
    }

    // One stance clip k at game time `now` (looping from `start`) over the target, weight w on the
    // upper body (spine, neck, head, arms, blade mounts) and w * body on the legs and the root's
    // height (standing). Walking keeps the stance's arms over the game's legs.
    void applyStanceOne(Ogre::Skeleton* sk, State& s, int k, double start, double now, float w, float body)
    {
        const Rig& rig = s.rig;
        Ogre::TransformKeyFrame kf(NULL, 0.0f);
        float wl = w * body;
        Ogre::Animation* a = resolveClip(sk, s, k);
        if (w <= 0.0f || a == NULL || a->getLength() <= 0.0f)
            return;
        Ogre::TimeIndex ti(std::fmod((float)((now - start) / 1000.0), a->getLength()));
        const Ogre::Animation::OldNodeTrackList& tl = a->_getOldNodeTrackList();
        for (Ogre::Animation::OldNodeTrackList::const_iterator it = tl.begin(); it != tl.end(); ++it)
        {
            int h = (int)it->first;
            if (h >= rig.n || it->second == NULL)
                continue;
            float e = rig.clipBone[h] ? w : (rig.legBone[h] ? wl : 0.0f);
            if (e <= 0.0f)
                continue;
            it->second->getInterpolatedKeyFrame(ti, &kf);
            s.tgt[h] = layerSlerp(s, LY_STANCE, h, e, s.tgt[h], rig.bindQ[h] * kf.getRotation());
            if (h == rig.prop1 || h == rig.prop2)
            {
                Ogre::Vector3 p = rig.bindP[h] + kf.getTranslate() * s.clipScale[k];
                s.tgtP[h] = s.tgtP[h] + (p - s.tgtP[h]) * e;
            }
            else if (h == rig.root)
            {
                float y = rig.bindP[h].y + kf.getTranslate().y * s.clipScale[k];     // model up (Ogre Y)
                s.tgtP[h].y += (y - s.tgtP[h].y) * e;
                s.rootDriven = true;
            }
        }
    }

    // The one stance of the fighter's rank (a rank has one stance and never cycles); rank 0 keeps the
    // game's mirrored guard.
    int stanceForRank(int rank)
    {
        switch (rank)
        {
        case 1: return arms::CLIP_STANCE_DESHI;
        case 2: return arms::CLIP_STANCE_BUSHI;
        case 3: return arms::CLIP_STANCE_RONIN;
        case 4: return arms::CLIP_STANCE_KENSHI;
        case 5: return arms::CLIP_STANCE_KENSEI;
        }
        return -1;
    }

    // In combat, in the plain dual guard, not swinging, blocking, running or playing another clip: loop
    // the rank's idle stance; a rank that changes mid-fight (injuries, armour) cross-fades into its
    // own. Any action cuts it in CLIP_YIELD_S (as the clips yield); walking keeps its arms, the legs
    // the game's.
    void applyStanceClip(Ogre::Skeleton* sk, State& s, Mode mode, bool other)
    {
        int byRank = g_clipOn[arms::CLIP_STANCE_DESHI] ? stanceForRank(s.rank) : -1;
        // never over the game's stagger, fall or get-up
        bool want = s.inCombat && mode == MIRROR && !other && !s.running && !s.foreign && s.reacting == arms::REACT_NONE
                    && byRank >= 0;
        double now = defense::frameMs();
        float dt = (float)std::max(0.0, std::min(250.0, now - s.stLast)) / 1000.0f;
        s.stLast = now;
        if (want && s.stCur < 0)
        {
            s.stCur = byRank;
            s.stStart = now;
            s.stPrev = -1;
            s.stX = 1.0f;
            s.stW = 0.0f;
            s.stBody = s.moving ? 0.0f : 1.0f;
            diag::count(CLIP_PLAYED[s.stCur]);
        }
        if (s.stCur < 0)
            return;
        if (want && s.stX >= 1.0f && s.stCur != byRank)
        {
            s.stPrev = s.stCur;
            s.stPrevStart = s.stStart;
            s.stCur = byRank;
            s.stStart = now;
            s.stX = 0.0f;
            diag::count(CLIP_PLAYED[s.stCur]);
            diag::count("l3_stance_rank_changed");
        }
        bool action = mode != MIRROR || other || s.foreign || s.reacting != arms::REACT_NONE;
        if (s.reacting != arms::REACT_NONE && s.stW > 0.0f && diag::stage())
            diag::count("l3_stance_yield_reaction_frame");
        s.stW += (want ? dt : -dt) / (action ? CLIP_YIELD_S : STANCE_FADE_S);
        if (s.stW <= 0.0f && !want)
        {
            s.stCur = s.stPrev = -1;
            s.stW = 0.0f;
            return;
        }
        s.stW = std::min(1.0f, std::max(0.0f, s.stW));
        s.stX = std::min(1.0f, s.stX + dt / STANCE_CROSS_S);
        s.stBody = std::min(1.0f, std::max(0.0f, s.stBody + (s.moving ? -dt : dt) / STANCE_BODY_S));
        if (s.moving && diag::stage())
            diag::count("l3_stance_walk_frame");
        float env = s.stW * s.stW * (3.0f - 2.0f * s.stW);
        float body = s.stBody * s.stBody * (3.0f - 2.0f * s.stBody);
        if (s.stPrev >= 0 && s.stX < 1.0f)
            applyStanceOne(sk, s, s.stPrev, s.stPrevStart, now, env, body);
        float x = s.stX * s.stX * (3.0f - 2.0f * s.stX);
        applyStanceOne(sk, s, s.stCur, s.stStart, now, env * (s.stPrev >= 0 ? x : 1.0f), body);
    }

    void computeTarget(State& s, Mode mode, bool inCombat)
    {
        const Rig& rig = s.rig;
        s.tgt = s.src;
        s.tgtP = s.srcP;
        if (mode == TWIN)
        {
            // Both blades swing: the right arm plays the track as it is, the
            // left arm (and its blade mount) mirrors it in the same frame. No
            // squaring or uncrossing: a cross-slash is meant to cross.
            for (int i = 0; i < rig.n; ++i)
                if (rig.role[i] == LEFT_ARM)
                    s.tgt[i] = mirrorRot(rig, i, s.src);
            s.tgtP[rig.prop1] = mirrorPos(rig, rig.prop1, s.srcP);
            return;
        }
        if (mode == LEFTY && s.reacting == arms::REACT_DOWN)
        {
            // A left-hander who is down, knocked out or ragdolled: no whole-body mirror over the game's (or
            // physics') body; only the left arm and Prop1, so the weapon stays in the left hand.
            for (int i = 0; i < rig.n; ++i)
                if (rig.role[i] == LEFT_ARM)
                    s.tgt[i] = mirrorRot(rig, i, s.src);
            s.tgtP[rig.prop1] = mirrorPos(rig, rig.prop1, s.srcP);
            return;
        }
        if (mode == LEFTY)
        {
            // Everything mirrored, both arms included: the crippled right arm
            // takes the mirror of the free left arm, the left arm fights.
            for (int i = 0; i < rig.n; ++i)
                s.tgt[i] = mirrorRot(rig, i, s.src);
            s.tgtP[rig.prop1] = mirrorPos(rig, rig.prop1, s.srcP);
            hangRight(s, s.tgt, s.tgtP);
            return;
        }
        if (mode == SWAP)
        {
            for (int i = 0; i < rig.n; ++i)
            {
                if (rig.role[i] == RIGHT_ARM)
                    s.tgt[i] = s.haveGuard ? s.guard[i] : s.src[i];
                else
                    s.tgt[i] = mirrorRot(rig, i, s.src);
            }
            s.tgtP[rig.prop1] = mirrorPos(rig, rig.prop1, s.srcP);
            if (s.haveGuard)
                s.tgtP[rig.prop2] = s.guardP[rig.prop2];
            return;
        }
        if ((mode == HOLD || mode == BLOCK) && s.haveGuard)
        {
            for (int i = 0; i < rig.n; ++i)
                if (rig.role[i] == LEFT_ARM)
                    s.tgt[i] = s.guard[i];
            s.tgtP[rig.prop1] = s.guardP[rig.prop1];
            return;
        }
        // MIRROR (and HOLD before any guard was seen).
        // While the game plays a reaction (a stagger, a fall, a get-up) the chest and the right arm stay the
        // game's: only the left arm mirrors, and that pose is no guard to keep.
        const bool reacting = s.reacting != arms::REACT_NONE;
        if (mode == MIRROR && inCombat && g_square > 0.0f && !reacting)
            squareChest(rig, s.tgt, g_square);
        if (mode == MIRROR && !reacting)
            uncrossRight(rig, s.tgt, s.srcP, g_uncross);
        for (int i = 0; i < rig.n; ++i)
            if (rig.role[i] == LEFT_ARM)
                s.tgt[i] = mirrorRot(rig, i, s.tgt);
        s.tgtP[rig.prop1] = mirrorPos(rig, rig.prop1, s.tgtP);
        // The guard that HOLD / BLOCK keep is the combat stance only: the relaxed pose before a
        // fight would hold the left blade upright by the head in the first block. Until a combat
        // guard exists, HOLD and BLOCK fall back to the plain mirror (above).
        if (mode == MIRROR && inCombat && !reacting)
        {
            for (int i = 0; i < rig.n; ++i)
                if (rig.role[i] == LEFT_ARM || rig.role[i] == RIGHT_ARM)
                    s.guard[i] = s.tgt[i];
            s.guardP[rig.prop1] = s.tgtP[rig.prop1];
            s.guardP[rig.prop2] = s.tgtP[rig.prop2];
            s.haveGuard = true;
        }
    }

    void initState(State& s, Character* c);

    bool sane(float x) { return x == x && x < 1.0e5f && x > -1.0e5f; }

    void apply(Ogre::Skeleton* sk, State& s)
    {
        const Rig& rig = s.rig;
        if (rig.n == 0)
            return;
        if ((int)sk->getNumBones() != rig.n || sk->getBone((unsigned short)rig.checkHandle)->getName() != rig.checkName)
        {
            s.rig.n = 0;        // another skeleton now lives at this address; re-tracked by the tick
            diag::count("arms_rig_changed");
            return;
        }
        // The blends step on game time, as the clips, the stance and the run hold do: on wall time an
        // attack blend would cover most of a twin strike at x3. A paused game holds a blend too.
        const double nowMs = defense::frameMs();
        float dt = s.started && s.lastMs >= 0.0 ? (float)((nowMs - s.lastMs) / 1000.0) : 0.0f;
        s.lastMs = nowMs;
        ++s.frameNo;
        if (dt < 0.0f)
            dt = 0.0f;
        else if (dt > 0.25f)
            dt = 0.25f;

        // the game's root of the last frame: its own motion (a stagger, a knock-back) is not a jump of ours
        const bool rootOk = rig.root >= 0 && rig.root < rig.n;
        const Ogre::Vector3 srcRootBefore = rootOk ? s.srcP[rig.root] : Ogre::Vector3::ZERO;
        readSource(sk, s);
        Mode mode = s.mode;
        if (mode == SWAP && !g_swap)
            mode = HOLD;
        int attackNow = (mode == TWIN || mode == SWAP) ? s.attackClip : -1;
        if (s.reacting != s.appliedReacting && s.started)
            diag::count(s.reacting == arms::REACT_DOWN ? "pose_reacting_down" : s.reacting == arms::REACT_STAGGER
                        ? "pose_reacting_stagger" : "pose_reacting_end");
        if (!s.started || mode != s.appliedMode || s.inCombat != s.appliedCombat || attackNow != s.appliedAttack
            || s.reacting != s.appliedReacting)
        {
            // Start a transition from whatever is on screen now.
            s.from = s.started ? s.out : s.src;
            s.fromP = s.started ? s.outP : s.srcP;
            s.blendT = 0.0f;
            s.blendDur = (attackNow >= 0 || s.appliedAttack >= 0) ? ATTACK_BLEND_S : g_blendSec;
            // Going BACK to the guard (from a block, a cut-off clip, or the first blend after a State
            // starts) takes RETURN_BLEND_S: at blend_ms it is 15-20 deg a frame, a visible twitch. A
            // block and a swing still start at blend_ms, they must not lag the game's own timing.
            if (mode == MIRROR && (!s.started || s.appliedMode != MIRROR || s.inCombat != s.appliedCombat
                                   || s.reacting != s.appliedReacting))
                s.blendDur = std::max(s.blendDur, RETURN_BLEND_S);
            if (!s.started)
                diag::count(mode == MIRROR ? "first_blend_guard" : "first_blend_other");
            if (attackNow >= 0 || s.appliedAttack >= 0)
                diag::count("attack_blend_started");
            s.appliedMode = mode;
            s.appliedCombat = s.inCombat;
            s.appliedAttack = attackNow;
            s.appliedReacting = s.reacting;
        }
        if (s.clipSwap)
        {
            s.clipSwap = false;
            if (s.started)
            {
                float left = s.blendDur * (1.0f - s.blendT);     // a mode blend under way keeps its pace
                s.from = s.out;
                s.fromP = s.outP;
                s.blendT = 0.0f;
                s.blendDur = std::max(left, CLIP_SWAP_S);
            }
        }
        trackBlock(s, mode);
        computeTarget(s, mode, s.inCombat);
        s.rootDriven = false;
        bool clipPlays = ((mode == TWIN || mode == SWAP) && applyAttackClip(sk, s)) || applyClip(sk, s, mode);
        applyRunClip(sk, s, mode, clipPlays);
        applyStanceClip(sk, s, mode, clipPlays || s.runClip >= 0);
        if (!clipPlays && s.parryStart >= 0.0 && g_parryPose > 0.0f && mode != SWAP && mode != TWIN && mode != LEFTY
            && s.reacting != arms::REACT_DOWN)
        {
            float env = parryEnvelope(defense::frameMs() - s.parryStart);
            if (env > 0.0f)
            {
                if (s.parrySnap >= 0)
                    applyParryFromBlock(s, s.tgt, s.tgtP, env * g_parryPose);
                else
                    applyParry(rig, s.tgt, s.tgtP, env * g_parryPose);
            }
        }
        s.blendT = s.blendDur > 0.0f ? std::min(1.0f, s.blendT + dt / s.blendDur) : 1.0f;
        float t = s.blendT * s.blendT * (3.0f - 2.0f * s.blendT);      // smoothstep
        // The fastest bone turn of this frame, for the `pose_jump` lines (the bone, the speed and what was
        // switching): they name the change behind a visible jerk.
        float jumpDeg = 0.0f;
        int jumpBone = -1;
        const Ogre::Vector3 rootBefore = rootOk ? s.outP[rig.root] : Ogre::Vector3::ZERO;
        for (int i = 0; i < rig.n; ++i)
        {
            Ogre::OldBone* b = sk->getBone((unsigned short)i);
            const Ogre::Quaternion before = s.out[i];
            bool longer = false;
            s.out[i] = t >= 1.0f ? s.tgt[i] : keptWay(t, s.from[i], s.tgt[i], s.started ? &before : NULL, &longer);
            if (longer)
                diag::count("blend_kept_way");
            if (s.started && (rig.clipBone[i] || rig.legBone[i]))
            {
                float d = std::fabs(before.Dot(s.out[i]));
                float deg = d >= 1.0f ? 0.0f : 2.0f * std::acos(d) * 57.2958f;
                if (deg > jumpDeg)
                {
                    jumpDeg = deg;
                    jumpBone = i;
                }
            }
            b->setOrientation(s.out[i]);
            s.written[i] = b->getOrientation();
            if (i == rig.prop1 || i == rig.prop2)
            {
                s.outP[i] = s.fromP[i] + (s.tgtP[i] - s.fromP[i]) * t;
                b->setPosition(s.outP[i]);
            }
            else if (i == rig.root && s.rootDriven)
            {
                s.outP[i] = s.tgtP[i];          // the clip's height, faded in and out by its own envelope
                s.rootCarry = s.tgtP[i] - s.srcP[i];
                b->setPosition(s.outP[i]);
            }
            else if (i == rig.root && s.rootCarry.squaredLength() > 1e-6f)
            {
                // A clip that stops early (not through its own fade) leaves the body where it was and
                // eases it back onto the game's root in ROOT_EASE_S (no jump).
                float k = std::max(0.0f, 1.0f - dt / ROOT_EASE_S);
                s.rootCarry *= k;
                if (k <= 0.0f)
                    diag::count("root_carry_eased");
                s.outP[i] = s.srcP[i] + s.rootCarry;
                b->setPosition(s.outP[i]);
            }
            else
                s.outP[i] = s.srcP[i];
            s.writtenP[i] = b->getPosition();
        }
        if (s.started && dt > 0.004f && jumpBone >= 0)
        {
            const float degPerS = jumpDeg / dt;
            // root_s is the speed of OUR offset over the game's root (out - src), src_s the game's root
            // itself (it moves a lot under staggers); only ours counts as pose_jump_root, the game's as
            // root_moved_by_game.
            float rootPerS = 0.0f, srcPerS = 0.0f;
            if (rootOk)
            {
                const Ogre::Vector3 src = s.srcP[rig.root];
                rootPerS = ((s.outP[rig.root] - src) - (rootBefore - srcRootBefore)).length() / dt;
                srcPerS = (src - srcRootBefore).length() / dt;
            }
            if (srcPerS > POSE_JUMP_ROOT_S)
                diag::count("root_moved_by_game");
            if ((s.appliedAttack < 0 || s.blendT < 1.0f) && (degPerS > POSE_JUMP_DEG_S || rootPerS > POSE_JUMP_ROOT_S))
            {
                diag::count(rootPerS > POSE_JUMP_ROOT_S ? "pose_jump_root" : "pose_jump");
                if (InterlockedIncrement(&g_poseJumpLines) <= diag::budget(POSE_JUMP_DETAILS))
                    kit::Diag("pose_jump").kv("who", s.who != NULL ? std::string(s.who->displayName) : std::string("?"))
                        .kv("bone", jumpBone).kv("deg", jumpDeg).kv("deg_s", degPerS).kv("root_s", rootPerS)
                        .kv("src_s", srcPerS).kv("root_driven", s.rootDriven).kv("root_carry", s.rootCarry.length())
                        .kv("dt_ms", dt * 1000.0f).kv("mode", (int)mode).kv("blend_t", s.blendT).kv("blend_s", s.blendDur)
                        .kv("attack", s.appliedAttack).kv("clip", s.clip).kv("stance", s.stCur).kv("stance_w", s.stW)
                        .kv("run", s.runClip).kv("reacting", s.reacting).emit();
            }
        }
        s.started = true;
        // A bad value in this State (a non-number, or the blade mount thrown far away) would hide the
        // second blade until the stance is toggled. Check what was written; reset the State if so.
        {
            bool bad = false;
            int badBone = -1;
            for (int i = 0; i < rig.n && !bad; ++i)
            {
                const Ogre::Quaternion& q = s.out[i];
                if (!sane(q.w) || !sane(q.x) || !sane(q.y) || !sane(q.z))
                    bad = true, badBone = i;
            }
            const int props[2] = { rig.prop1, rig.prop2 };
            for (int j = 0; j < 2 && !bad; ++j)
            {
                const Ogre::Vector3& p = s.outP[props[j]];
                if (!sane(p.x) || !sane(p.y) || !sane(p.z)
                    || (p - rig.bindP[props[j]]).squaredLength() > 40.0f * 40.0f)
                    bad = true, badBone = props[j];
            }
            if (bad)
            {
                diag::count("arms_state_reset");
                if (diag::takeDetail())
                {
                    const Ogre::Vector3& p = s.outP[badBone];
                    kit::Diag("arms_state_reset").kv("bone", sk->getBone((unsigned short)badBone)->getName())
                        .kv("mode", arms::modeName(s.appliedMode)).kv("attack", s.attackClip).kv("clip", s.clip)
                        .kv("px", p.x).kv("py", p.y).kv("pz", p.z).kv("qw", s.out[badBone].w).emit();
                }
                // this frame shows the game's own pose (read this frame), not a bind-pose flash
                std::vector<Ogre::Quaternion> src = s.src;
                std::vector<Ogre::Vector3> srcP = s.srcP;
                Character* who = s.who;
                initState(s, who);
                for (int i = 0; i < rig.n; ++i)
                {
                    Ogre::OldBone* b = sk->getBone((unsigned short)i);
                    b->setOrientation(src[i]);
                    b->setPosition(srcP[i]);
                }
                return;
            }
        }
    }

    void (*updateTransforms_orig)(Ogre::Skeleton*) = NULL;
    // What the pose engine costs, measured in place (under g_lock).
    LONGLONG g_costTicks = 0, g_costMaxTicks = 0;
    unsigned g_costCalls = 0;
    int g_costPeak = 0;

    void updateTransforms_hook(Ogre::Skeleton* self)
    {
        diag::HookCall hc(diag::HK_UPDATE_TRANSFORMS);
        if (g_count > 0)
        {
            EnterCriticalSection(&g_lock);
            std::map<Ogre::Skeleton*, State>::iterator it = g_states.find(self);
            if (it != g_states.end())
            {
                hc.ours();
                LARGE_INTEGER t0, t1;
                QueryPerformanceCounter(&t0);
                apply(self, it->second);
                QueryPerformanceCounter(&t1);
                LONGLONG d = t1.QuadPart - t0.QuadPart;
                g_costTicks += d;
                if (d > g_costMaxTicks)
                    g_costMaxTicks = d;
                ++g_costCalls;
                if ((int)g_states.size() > g_costPeak)
                    g_costPeak = (int)g_states.size();
            }
            LeaveCriticalSection(&g_lock);
        }
        updateTransforms_orig(self);
    }

    void initState(State& s, Character* c)
    {
        int n = s.rig.n;
        s.who = c;
        s.mode = MIRROR;
        s.inCombat = false;
        s.appliedMode = MIRROR;
        s.appliedCombat = false;
        s.started = false;
        s.blendT = 0.0f;
        s.haveGuard = false;
        s.src.assign(n, Ogre::Quaternion::IDENTITY);
        s.written = s.tgt = s.from = s.out = s.guard = s.src;
        s.srcP.assign(n, Ogre::Vector3::ZERO);
        s.writtenP = s.tgtP = s.fromP = s.outP = s.guardP = s.srcP;
        for (int ly = 0; ly < LAYER_COUNT; ++ly)
        {
            s.layerPrev[ly] = s.src;
            s.layerFrame[ly].assign(n, 0u);
        }
        s.frameNo = 1;              // layerFrame 0 = never written
        s.lastMs = -1.0;
        s.reacting = arms::REACT_NONE;
        s.appliedReacting = arms::REACT_NONE;
        s.hangT = -1.0;
        s.parryStart = -1.0;
        s.snapCount = s.snapNext = 0;
        s.parrySnap = -1;
        s.curDev = 0.0f;
        s.clip = -1;
        s.clipStart = 0.0;
        s.clipCounted = false;
        s.clipMode = -1;
        s.clipYield = -1.0f;
        s.clipHit = false;
        s.clipSwap = false;
        s.blockKind = -1;
        s.blockHitStart = -1.0;
        s.attackClip = -1;
        s.attackProgress = 0.0f;
        s.attackCounted = false;
        for (int k = 0; k < arms::CLIP_COUNT; ++k)
            s.bodyOk[k] = -1;
        s.rootDriven = false;
        s.rootCarry = Ogre::Vector3::ZERO;
        s.blendDur = g_blendSec;
        s.appliedAttack = -1;
        s.running = false;
        s.foreign = false;
        s.runClip = -1;
        s.runStart = 0.0;
        s.runW = 0.0f;
        s.runBase = NULL;
        s.runBaseTried = false;
        s.runPhase = 0.0f;
        s.runLocked = false;
        s.runPhaseCounted = false;
        s.runLast = 0.0;
        s.runStance = -1;
        s.runMix = -1;
        s.runFullW = 0.0f;
        s.runStanceOn = s.runStanceCounted = s.runStanceCounted2 = false;
        s.runCounted = false;
        s.moving = false;
        s.stCur = s.stPrev = -1;
        s.stStart = s.stPrevStart = s.stLast = 0.0;
        s.rank = 0;
        s.stW = 0.0f;
        s.stX = 1.0f;
        s.stBody = 1.0f;
        for (int k = 0; k < arms::CLIP_COUNT; ++k)
        {
            s.clipAnim[k] = NULL;
            s.clipScale[k] = 1.0f;
            s.clipLooked[k] = false;
        }
        s.sex = 0;
        s.tracks = NULL;
    }

    // mods\Nitoryu\character\meshes\ (next to the DLL). No RACE record links the files, so the folder
    // is made an Ogre location here.
    std::string animDir()
    {
        HMODULE self = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&animDir), &self);
        char path[MAX_PATH] = { 0 };
        GetModuleFileNameA(self, path, MAX_PATH);
        std::string p(path);
        size_t slash = p.find_last_of("\\/");
        return (slash == std::string::npos ? std::string(".") : p.substr(0, slash)) + "\\character\\meshes";
    }

    // Both folders become Ogre locations, once.
    void addL3Locations()
    {
        try
        {
            Ogre::ResourceGroupManager& rgm = Ogre::ResourceGroupManager::getSingleton();
            for (int sex = 0; sex < 2; ++sex)
            {
                std::string dir = animDir() + L3_DIR[sex];
                if (!rgm.resourceLocationExists(dir, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
                    rgm.addResourceLocation(dir, "FileSystem", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
            }
        }
        catch (...)
        {
            diag::count("l3_location_failed");
        }
    }

    // Main loop: load both track files once. Failure is logged by emitClipLog; clips then fall back.
    void loadL3Files()
    {
        static bool located = false;
        if (!located)
        {
            located = true;
            addL3Locations();
        }
        for (int sex = 0; sex < 2; ++sex)
        {
            if (g_l3State[sex] != 0)
                continue;
            try
            {
                Ogre::ResourcePtr r = Ogre::OldSkeletonManager::getSingleton().load(
                    L3_FILE[sex], Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
                g_l3Skel[sex] = r.staticCast<Ogre::Skeleton>();
                g_l3State[sex] = g_l3Skel[sex].isNull() ? -1 : 1;
            }
            catch (const std::exception& e)
            {
                g_l3State[sex] = -1;
                g_l3Error[sex] = e.what();
            }
            catch (...)
            {
                g_l3State[sex] = -1;
                g_l3Error[sex] = "unknown exception";
            }
            diag::count(g_l3State[sex] == 1 ? "l3_file_loaded" : "l3_file_failed");
        }
    }

    // The same load from the main loop after a game load, OUTSIDE the pose lock, so the first draw
    // does not wait on disk I/O while the pose hook is blocked. The result is published under the
    // lock; the in-lock loadL3Files calls then find it done.
    void preloadL3Files()
    {
        EnterCriticalSection(&g_lock);
        bool need = g_l3State[0] == 0 || g_l3State[1] == 0;
        LeaveCriticalSection(&g_lock);
        if (!need)
            return;
        addL3Locations();
        for (int sex = 0; sex < 2; ++sex)
        {
            Ogre::SkeletonPtr sk;
            int state = -1;
            std::string err;
            try
            {
                Ogre::ResourcePtr r = Ogre::OldSkeletonManager::getSingleton().load(
                    L3_FILE[sex], Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
                sk = r.staticCast<Ogre::Skeleton>();
                state = sk.isNull() ? -1 : 1;
            }
            catch (const std::exception& e)
            {
                err = e.what();
            }
            catch (...)
            {
                err = "unknown exception";
            }
            EnterCriticalSection(&g_lock);
            if (g_l3State[sex] == 0)
            {
                g_l3Skel[sex] = sk;
                g_l3State[sex] = state;
                g_l3Error[sex] = err;
                diag::count(state == 1 ? "l3_file_preloaded" : "l3_file_failed");
            }
            LeaveCriticalSection(&g_lock);
        }
    }

    // Under g_lock.
    void startClip(State& s, int k, double startMs)
    {
        loadL3Files();                      // once; main loop (setMode, setParry, playClip)
        if (s.clip >= 0 && !reactionClip(k))
            return;                         // let the running clip finish
        if (s.clip >= 0)
        {
            s.clipSwap = true;              // from the playing clip to this one, not a snap
            diag::count("l3_clip_replaced");
        }
        s.clip = k;
        s.clipStart = startMs;
        s.clipCounted = false;
        s.clipMode = -1;
        s.clipYield = -1.0f;
        s.clipHit = false;
    }

    State* stateOf(Character* c)
    {
        std::map<Character*, Ogre::Skeleton*>::iterator it = g_byChar.find(c);
        if (it == g_byChar.end())
            return NULL;
        std::map<Ogre::Skeleton*, State>::iterator st = g_states.find(it->second);
        return st != g_states.end() ? &st->second : NULL;
    }
}

namespace arms
{
    void init(float squareTorso, float uncross, unsigned blendMs, bool swap)
    {
        InitializeCriticalSection(&g_lock);
        QueryPerformanceFrequency(&g_freq);
        g_square = std::max(0.0f, std::min(1.0f, squareTorso));
        g_blendSec = (float)blendMs / 1000.0f;
        g_swap = swap;
        g_uncross = std::max(0.0f, std::min(1.5f, uncross));
    }

    bool installHook()
    {
        HMODULE ogre = GetModuleHandleA("OgreMain_x64.dll");
        FARPROC target = ogre != NULL ? GetProcAddress(ogre, "?_updateTransforms@Skeleton@Ogre@@UEAAXXZ") : NULL;
        if (target == NULL)
        {
            kit::log::error("Nitoryu: Ogre::Skeleton::_updateTransforms not found; no dual stance pose");
            return false;
        }
        // hook: Ogre::Skeleton::_updateTransforms
        if (hooks::add("Ogre::Skeleton::_updateTransforms", "pose", reinterpret_cast<void*>(target), reinterpret_cast<void*>(updateTransforms_hook),
                               reinterpret_cast<void**>(&updateTransforms_orig)) != KenshiLib::SUCCESS)
        {
            kit::log::error("Nitoryu: could not hook Ogre::Skeleton::_updateTransforms");
            return false;
        }
        return true;
    }

    bool track(Character* c, Ogre::Skeleton* sk, unsigned owner)
    {
        if (c == NULL || sk == NULL || owner == 0)
            return false;
        EnterCriticalSection(&g_lock);
        bool ok = true;
        std::map<Character*, Ogre::Skeleton*>::iterator it = g_byChar.find(c);
        std::map<Ogre::Skeleton*, State>::iterator st = it != g_byChar.end() ? g_states.find(it->second) : g_states.end();
        if (it != g_byChar.end() && (st == g_states.end() || st->second.who != c))
        {
            g_byChar.erase(it);                 // its State went with another character: stale
            it = g_byChar.end();
            st = g_states.end();
        }
        bool fresh = it == g_byChar.end() || it->second != sk || st->second.rig.n == 0;
        if (!fresh)
        {
            st->second.owners |= owner;
            LeaveCriticalSection(&g_lock);
            return true;
        }
        bool had = st != g_states.end();
        unsigned owners = owner | (had ? st->second.owners : 0u);
        if (had)
        {
            g_states.erase(st);                 // the appearance was rebuilt (or the rig check failed)
            g_byChar.erase(it);
        }
        std::map<Character*, Ogre::Skeleton*>::iterator bad = g_unsupported.find(c);
        if (bad != g_unsupported.end() && bad->second == sk)
            ok = false;
        State s;
        if (ok)
            ok = buildRig(sk, s.rig);
        if (ok)
        {
            // A skeleton belongs to one character: a State still keyed at this address is a dead
            // character's (a freed skeleton re-allocated here).
            std::map<Ogre::Skeleton*, State>::iterator old = g_states.find(sk);
            if (old != g_states.end())
            {
                g_byChar.erase(old->second.who);
                g_states.erase(old);
                diag::count("arms_skeleton_reused");
                diag::count("arms_untracked");
            }
            initState(s, c);
            s.owners = owners;
            // The character's own sex picks the track file, not the rig's name.
            int byName = sk->getName().find("female") != std::string::npos ? 1 : 0;
            s.sex = c->isFemale() ? 1 : 0;
            if (s.sex != byName)
                diag::count("arms_sex_name_differs");
            s.tracks = trackNamesOf(sk);
            g_states[sk] = s;
            g_byChar[c] = sk;
            diag::count(had ? "arms_retracked" : "arms_tracked");
        }
        else
        {
            if (had)
                diag::count("arms_untracked");
            if (bad == g_unsupported.end() || bad->second != sk)
            {
                if (g_unsupported.size() > 256)
                    g_unsupported.clear();
                g_unsupported[c] = sk;
            }
        }
        g_count = (LONG)g_states.size();
        LeaveCriticalSection(&g_lock);
        return ok;
    }

    void release(Character* c, unsigned owner)
    {
        EnterCriticalSection(&g_lock);
        std::map<Character*, Ogre::Skeleton*>::iterator it = g_byChar.find(c);
        if (it != g_byChar.end())
        {
            std::map<Ogre::Skeleton*, State>::iterator st = g_states.find(it->second);
            if (st == g_states.end() || st->second.who != c)
                g_byChar.erase(it);             // stale link, no State of ours
            else
            {
                st->second.owners &= ~owner;
                if (st->second.owners == 0)
                {
                    g_states.erase(st);
                    g_byChar.erase(it);
                    diag::count("arms_untracked");
                }
            }
        }
        g_count = (LONG)g_states.size();
        LeaveCriticalSection(&g_lock);
    }

    void untrack(Character* c) { release(c, OWNER_ALL); }

    Ogre::Skeleton* skeletonOf(Character* c)
    {
        EnterCriticalSection(&g_lock);
        std::map<Character*, Ogre::Skeleton*>::iterator it = g_byChar.find(c);
        Ogre::Skeleton* r = it != g_byChar.end() ? it->second : NULL;
        LeaveCriticalSection(&g_lock);
        return r;
    }

    void preload() { preloadL3Files(); }

    int unsupportedCount()
    {
        EnterCriticalSection(&g_lock);
        int n = (int)g_unsupported.size();
        LeaveCriticalSection(&g_lock);
        return n;
    }

    bool hasTrack(Character* c, const std::string& name, std::string* rigName)
    {
        bool r = true;
        EnterCriticalSection(&g_lock);
        std::map<Character*, Ogre::Skeleton*>::iterator it = g_byChar.find(c);
        std::map<Ogre::Skeleton*, State>::iterator st = it != g_byChar.end() ? g_states.find(it->second) : g_states.end();
        if (st != g_states.end() && st->second.tracks != NULL)
        {
            r = st->second.tracks->count(name) > 0;
            if (!r && rigName != NULL)
                *rigName = st->first->getName();
        }
        LeaveCriticalSection(&g_lock);
        return r;
    }

    void setMode(Character* c, Mode m, bool inCombat)
    {
        if (g_count == 0 || c == NULL)
            return;
        EnterCriticalSection(&g_lock);
        std::map<Character*, Ogre::Skeleton*>::iterator it = g_byChar.find(c);
        if (it != g_byChar.end())
        {
            std::map<Ogre::Skeleton*, State>::iterator st = g_states.find(it->second);
            if (st != g_states.end())
            {
                if (st->second.mode != m)
                    diag::count(m == TWIN ? "arms_mode_twin" : (m == SWAP ? "arms_mode_swap" : (m == HOLD ? "arms_mode_hold"
                                : (m == BLOCK ? "arms_mode_block" : (m == LEFTY ? "arms_mode_lefty" : "arms_mode_mirror")))));
                // every main-hand block of a dual fighter plays the scissor block
                if (m == BLOCK && st->second.mode != BLOCK && g_clipOn[CLIP_SCISSOR])
                    startClip(st->second, CLIP_SCISSOR, defense::clockMs());
                st->second.mode = m;
                st->second.inCombat = inCombat;
            }
        }
        LeaveCriticalSection(&g_lock);
    }

    void initParry(unsigned durationMs, float strength)
    {
        g_parryMs = (double)durationMs;
        g_parryPose = std::max(0.0f, std::min(1.0f, strength));
    }

    void setParry(Character* c, double startMs)
    {
        if (g_count == 0 || c == NULL)
            return;
        EnterCriticalSection(&g_lock);
        std::map<Character*, Ogre::Skeleton*>::iterator it = g_byChar.find(c);
        if (it != g_byChar.end())
        {
            std::map<Ogre::Skeleton*, State>::iterator st = g_states.find(it->second);
            if (st != g_states.end() && st->second.parryStart != startMs)
            {
                State& s = st->second;
                s.parryStart = startMs;
                s.parrySnap = s.snapCount > 0 ? (int)((unsigned)(startMs / 7.0) % (unsigned)s.snapCount) : -1;
                diag::count(s.parrySnap >= 0 ? "arms_parry_block_pose" : "arms_parry_swing");
                // the off-hand parry plays its clip (the parry pose above is the fallback if the track
                // is missing)
                int pk = parryClip();
                if (pk >= 0)
                    startClip(s, pk, startMs);
            }
        }
        LeaveCriticalSection(&g_lock);
    }

    const char* modeName(Mode m)
    {
        return m == TWIN ? "twin" : (m == SWAP ? "swap" : (m == HOLD ? "hold" : (m == BLOCK ? "block" : (m == LEFTY ? "lefty" : "mirror"))));
    }

    void setClipOn(Clip k, bool on)
    {
        if (k >= 0 && k < CLIP_COUNT)
            g_clipOn[k] = on;
    }

    void setAngledSwap(bool on) { g_angledSwap = on; }

    void setReacting(Character* c, int level)
    {
        if (g_count == 0 || c == NULL)
            return;
        EnterCriticalSection(&g_lock);
        State* s = stateOf(c);
        if (s != NULL)
            s->reacting = level;
        LeaveCriticalSection(&g_lock);
    }
    void setLimpSwing(bool on) { g_limpSwing = on; }

    void setAttackClip(Character* c, int k, float progress01)
    {
        if (g_count == 0 || c == NULL)
            return;
        if (k >= 0 && !clipOn((Clip)k))
            k = -1;
        EnterCriticalSection(&g_lock);
        State* s = stateOf(c);
        if (s != NULL)
        {
            if (k >= 0)
                loadL3Files();
            if (s->attackClip != k)
                s->attackCounted = false;
            s->attackClip = k;
            s->attackProgress = progress01;
        }
        LeaveCriticalSection(&g_lock);
    }

    void setRunning(Character* c, bool running)
    {
        if (g_count == 0 || c == NULL)
            return;
        EnterCriticalSection(&g_lock);
        State* s = stateOf(c);
        if (s != NULL)
        {
            if (running && !s->running && g_clipOn[CLIP_RUN_TRAIL])
                loadL3Files();
            s->running = running;
        }
        LeaveCriticalSection(&g_lock);
    }

    bool clipOn(Clip k) { return k >= 0 && k < CLIP_COUNT && g_clipOn[k]; }

    // One lock and one lookup per fighter per frame for what setMode, setRunning, setAttackClip and
    // setParry each do on their own.
    void setFrame(Character* c, const FrameInput& in)
    {
        if (g_count == 0 || c == NULL)
            return;
        int k = in.attackClip;
        if (k >= 0 && !clipOn((Clip)k))
            k = -1;
        EnterCriticalSection(&g_lock);
        State* sp = stateOf(c);
        if (sp != NULL)
        {
            State& s = *sp;
            // setMode
            if (s.mode != in.mode)
                diag::count(in.mode == TWIN ? "arms_mode_twin" : (in.mode == SWAP ? "arms_mode_swap" : (in.mode == HOLD ? "arms_mode_hold"
                            : (in.mode == BLOCK ? "arms_mode_block" : (in.mode == LEFTY ? "arms_mode_lefty" : "arms_mode_mirror")))));
            if (in.mode == BLOCK && s.mode != BLOCK)
            {
                // the held guard by the blow's direction (the counters tell which directions come in)
                diag::count(in.blockDir == CUT_DOWNWARD || in.blockDir == CUT_REAR_DOWNWARD ? "block_dir_down"
                            : in.blockDir == CUT_LEFT || in.blockDir == CUT_REAR_LEFT ? "block_dir_left"
                            : in.blockDir == CUT_RIGHT || in.blockDir == CUT_REAR_RIGHT ? "block_dir_right"
                            : in.blockDir < 0 ? "block_dir_unknown" : "block_dir_other");
                int bk = blockClipFor(in.blockDir);
                s.blockKind = bk;
                if (bk >= 0)
                    startClip(s, bk, in.nowMs);
            }
            s.mode = in.mode;
            s.inCombat = in.inCombat;
            // setRunning
            if (in.running && !s.running && g_clipOn[CLIP_RUN_TRAIL])
                loadL3Files();
            s.running = in.running;
            // the idle stances need the track file from the first combat frame
            if (in.inCombat && g_clipOn[CLIP_STANCE_DESHI])
                loadL3Files();
            s.moving = in.moving;
            s.foreign = in.foreign;
            s.rank = in.rank;
            s.reacting = in.reacting;
            // setAttackClip
            if (k >= 0)
                loadL3Files();
            if (s.attackClip != k)
                s.attackCounted = false;
            s.attackClip = k;
            s.attackProgress = in.attackProgress;
            // setParry
            if (in.parryStart >= 0.0 && s.parryStart != in.parryStart)
            {
                s.parryStart = in.parryStart;
                s.parrySnap = s.snapCount > 0 ? (int)((unsigned)(in.parryStart / 7.0) % (unsigned)s.snapCount) : -1;
                diag::count(s.parrySnap >= 0 ? "arms_parry_block_pose" : "arms_parry_swing");
                int pk = in.parryBack ? backGuardClip(false, in.blockDir) : -1;
                if (pk >= 0)
                    diag::count("back_guard_second_blade");
                else
                    pk = parryClip();
                if (pk >= 0)
                    startClip(s, pk, in.parryStart);
            }
            // A hit the main hand blocked (dual fighter) plays the dual block impact, left or right
            // blade high at random; over the scissor block it replaces the X with a CLIP_SWAP_S fade.
            // [Animations] dual_block=0 removes it.
            if (in.blockHitStart >= 0.0 && s.blockHitStart != in.blockHitStart)
            {
                s.blockHitStart = in.blockHitStart;
                // from behind: the main blade's back guard instead
                int bk = in.blockHitBack ? backGuardClip(true, in.blockDir) : -1;
                if (bk >= 0)
                {
                    diag::count("back_guard_main_blade");
                    startClip(s, bk, in.blockHitStart);
                }
                // the blocked hit of the guard that is up (the angled and overhead ones have their own)
                else if (s.blockKind == CLIP_ANGLED_L && g_clipOn[CLIP_ANGLED_L_IMPACT])
                    startClip(s, CLIP_ANGLED_L_IMPACT, in.blockHitStart);
                else if (s.blockKind == CLIP_ANGLED_R && g_clipOn[CLIP_ANGLED_R_IMPACT])
                    startClip(s, CLIP_ANGLED_R_IMPACT, in.blockHitStart);
                else if (s.blockKind == CLIP_MAIN_OVERHEAD && g_clipOn[CLIP_MAIN_OVERHEAD_IMPACT])
                    startClip(s, CLIP_MAIN_OVERHEAD_IMPACT, in.blockHitStart);
                else if (s.blockKind == CLIP_MAIN_BLOCK && g_clipOn[CLIP_MAIN_IMPACT])
                    startClip(s, CLIP_MAIN_IMPACT, in.blockHitStart);
                else if (g_clipOn[CLIP_DUAL_IMPACT_L])
                    startClip(s, (rand() & 1) ? CLIP_DUAL_IMPACT_R : CLIP_DUAL_IMPACT_L, in.blockHitStart);
            }
        }
        LeaveCriticalSection(&g_lock);
    }

    void playClip(Character* c, Clip k, double startMs)
    {
        if (g_count == 0 || c == NULL || !clipOn(k))
            return;
        EnterCriticalSection(&g_lock);
        State* s = stateOf(c);
        if (s != NULL)
            startClip(*s, k, startMs);
        else
            diag::count("l3_clip_untracked");
        LeaveCriticalSection(&g_lock);
    }

    bool clipPlaying(Character* c, Clip k)
    {
        if (g_count == 0 || c == NULL)
            return false;
        EnterCriticalSection(&g_lock);
        State* s = stateOf(c);
        bool on = s != NULL && s->clip == (int)k;
        LeaveCriticalSection(&g_lock);
        return on;
    }

    void stopClip(Character* c, Clip k)
    {
        if (g_count == 0 || c == NULL)
            return;
        EnterCriticalSection(&g_lock);
        State* s = stateOf(c);
        if (s != NULL && s->clip == (int)k)
            s->clipHit = true;              // applyClip fades it out in CLIP_YIELD_S
        LeaveCriticalSection(&g_lock);
    }

    void clipHit(Character* c)
    {
        if (g_count == 0 || c == NULL)
            return;
        EnterCriticalSection(&g_lock);
        State* s = stateOf(c);
        // A flesh hit through a block: the block clip gives way to the game's own hit reaction.
        if (s != NULL && (s->mode == BLOCK || (s->clip >= 0 && blockClip(s->clip))) && s->blockKind >= 0)
        {
            s->blockKind = -1;
            if (s->clip >= 0)
                s->clipHit = true;
            diag::count("l3_block_hit_through");
        }
        // A hit that gets through to a dual fighter who is not blocking: one flinch over the game's own
        // reaction, back to the guard; never while down, out or ragdolled.
        else if (s != NULL && s->mode != BLOCK && g_clipOn[CLIP_HIT_TAKEN] && !c->isDown() && !c->isUnconcious()
                 && !c->isRagdoll())
        {
            startClip(*s, CLIP_HIT_TAKEN, defense::frameMs());
            diag::count("l3_hit_taken_start");
        }
        else if (s != NULL && s->clip >= 0)
            s->clipHit = true;
        LeaveCriticalSection(&g_lock);
    }

    void emitClipLog()
    {
        std::vector<ClipLog> lines;
        std::vector<std::string> retimed;
        EnterCriticalSection(&g_lock);
        lines.swap(g_clipLog);
        retimed.swap(g_retimeLog);
        LeaveCriticalSection(&g_lock);
        for (size_t i = 0; i < retimed.size(); ++i)
            kit::log::line(std::string("base_track_retimed ") + retimed[i] + " (the clip drives the upper body only)");
        for (int sex = 0; sex < 2; ++sex)
            if (g_l3State[sex] != 0 && !g_l3Logged[sex])
            {
                g_l3Logged[sex] = true;
                kit::Diag d(g_l3State[sex] == 1 ? "l3_file" : "l3_file_failed");
                d.kv("file", L3_FILE[sex]);
                if (g_l3State[sex] == 1)
                    d.kv("animations", (int)g_l3Skel[sex]->getNumAnimations()).kv("bones", (int)g_l3Skel[sex]->getNumBones());
                else
                    d.kv("error", g_l3Error[sex]);
                d.emit();
            }
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const ClipLog& e = lines[i];
            kit::Diag d(e.found ? "l3_track" : "l3_track_missing");
            d.kv("track", CLIP_TRACK[e.clip]).kv("source", e.source).kv("sex", e.sex == 1 ? "female" : "male")
                .kv("own_anims", e.own).kv("linked_skeletons", e.linked);
            if (e.found)
                d.kv("length", e.length).kv("bone_tracks", e.tracks).kv("scale", e.scale);
            else
                d.kv("fallback", e.clip == CLIP_BLOCK_LEFT ? "parry pose" : "vanilla pose");
            d.emit();
        }
    }

    void takeCost(double& totalMs, double& maxMs, unsigned& calls, int& peak)
    {
        EnterCriticalSection(&g_lock);
        double f = g_freq.QuadPart > 0 ? 1000.0 / (double)g_freq.QuadPart : 0.0;
        totalMs = g_costTicks * f;
        maxMs = g_costMaxTicks * f;
        calls = g_costCalls;
        peak = g_costPeak;
        g_costTicks = g_costMaxTicks = 0;
        g_costCalls = 0;
        g_costPeak = (int)g_states.size();
        LeaveCriticalSection(&g_lock);
    }

    const char* clipText()
    {
        static std::string text;
        text.clear();
        for (int k = 0; k < CLIP_COUNT; ++k)
            if (g_clipOn[k])
                text += (text.empty() ? "" : ",") + std::string(moves::CLIPS[k].name);
        return text.c_str();
    }
}
