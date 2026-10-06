#include "kit/input.h"

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

#include <kenshi/Character.h>

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

namespace
{
    using arms::Mode;
    using arms::MIRROR;
    using arms::HOLD;
    using arms::SWAP;
    using arms::TWIN;
    using arms::BLOCK;
    using arms::LEFTY;

    const int BLOCK_SNAPS = 4;
    const float MIN_BLOCK_DEV = 0.3f;

    enum Role { OTHER, RIGHT_ARM, LEFT_ARM, SPINE };

    const char* SPINE_BONES[3] = { "Bip01 Spine", "Bip01 Spine1", "Bip01 Spine2" };
    const float SPINE_SHARE[3] = { 0.34f, 0.5f, 1.0f };

    float g_square = 0.8f;
    float g_blendSec = 0.08f;
    bool g_swap = true;
    float g_uncross = 0.7f;
    double g_parryMs = 650.0;
    float g_parryPose = 1.0f;

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
    const float CLIP_IN_S = 0.06f;
    const float CLIP_OUT_S = 0.10f;
    const float CLIP_YIELD_S = 0.10f;
    bool g_clipFoundLogged[arms::CLIP_COUNT] = { false };
    bool g_clipMissLogged[arms::CLIP_COUNT] = { false };
    const float RUN_FADE_S = 0.15f;

    const float STANCE_FADE_S = 0.40f;
    const float STANCE_CROSS_S = 0.80f;
    const float STANCE_BODY_S = 0.20f;
    const float ROOT_EASE_S = 0.25f;

    const float HANG_GRAVITY = 109.0f;
    const float HANG_DAMP = 3.5f;
    const float HANG_SHOULDER_COS = 0.643f;
    const float HANG_ELBOW_COS = 0.5f;
    bool g_limpSwing = true;

    const float CLIP_SWAP_S = 0.10f;
    int g_offBlockStyle = 0;
    const float ATTACK_BLEND_S = 0.35f;

    struct ClipLog { int clip; bool found; int own; int linked; float length; int tracks; float scale; const char* source; int sex; };
    std::vector<ClipLog> g_clipLog;
    std::vector<std::string> g_retimeLog;

    const char* L3_FILE[2] = { "Nitoryu_L3_male.skeleton", "Nitoryu_L3_female.skeleton" };

    const char* L3_DIR[2] = { "\\male_skeleton", "\\female_skeleton" };
    Ogre::SkeletonPtr g_l3Skel[2];
    int g_l3State[2] = { 0, 0 };
    std::string g_l3Error[2];
    bool g_l3Logged[2] = { false, false };

    struct Rig
    {
        int n;
        std::vector<int> parent;
        std::vector<int> partner;
        std::vector<Ogre::Quaternion> K;
        std::vector<Ogre::Quaternion> KpInv;
        std::vector<unsigned char> role;
        int prop1, prop2;
        int spine[3];
        Ogre::Vector3 normal;
        int rc, ru, rf, rh;
        bool canUncross;
        Ogre::Vector3 up, rside, fwd;
        int lu, lh;
        bool canParry;
        bool prop1OnLeftArm;
        Ogre::Vector3 upM;
        int checkHandle;
        std::string checkName;
        std::vector<unsigned char> clipBone;
        std::vector<unsigned char> legBone;
        int root;
        int thigh[2];
        std::vector<Ogre::Quaternion> bindQ;
        std::vector<Ogre::Vector3> bindP;
    };

    struct State
    {
        Character* who;
        unsigned owners;
        Rig rig;
        Mode mode;
        bool inCombat;
        Mode appliedMode;
        bool appliedCombat;
        bool started;
        float blendT;
        std::vector<Ogre::Quaternion> src, written, tgt, from, out, guard;
        std::vector<Ogre::Vector3> srcP, writtenP, tgtP, fromP, outP, guardP;
        bool haveGuard;
        LARGE_INTEGER lastT;
        double parryStart;

        std::vector<Ogre::Quaternion> snap[BLOCK_SNAPS];
        std::vector<Ogre::Vector3> snapP[BLOCK_SNAPS];
        int snapCount, snapNext, parrySnap;
        std::vector<Ogre::Quaternion> cur;
        std::vector<Ogre::Vector3> curP;
        float curDev;

        int clip;
        double clipStart;
        bool clipCounted;
        Ogre::Animation* clipAnim[arms::CLIP_COUNT];
        float clipScale[arms::CLIP_COUNT];
        bool clipLooked[arms::CLIP_COUNT];
        int clipMode;
        float clipYield;
        bool clipHit;
        bool clipSwap;
        int blockKind;
        double blockHitStart;
        int attackClip;
        float attackProgress;
        bool attackCounted;
        signed char bodyOk[arms::CLIP_COUNT];
        bool rootDriven;
        Ogre::Vector3 rootCarry;
        float blendDur;
        int appliedAttack;
        bool running;
        bool foreign;
        int runClip;
        double runStart;
        float runW;
        double runLast;
        bool runCounted;
        Ogre::Animation* runBase;
        bool runBaseTried;
        float runPhase;
        bool runLocked;
        bool runPhaseCounted;

        bool moving;
        int stCur, stPrev;
        double stStart, stPrevStart, stLast;
        int rank;
        float stW, stX, stBody;

        Ogre::Vector3 hangE, hangH, hangVE, hangVH;
        double hangT;
        int sex;
        const std::set<std::string>* tracks;
    };

    CRITICAL_SECTION g_lock;
    std::map<Ogre::Skeleton*, State> g_states;

    std::map<std::string, std::set<std::string> > g_trackNames;
    std::set<std::string> g_trackUnknown;
    std::map<Character*, Ogre::Skeleton*> g_byChar;
    std::map<Character*, Ogre::Skeleton*> g_unsupported;
    volatile LONG g_count = 0;
    LARGE_INTEGER g_freq;

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

        Ogre::Vector3 across = P[lu] - P[ru];
        float armSpan = across.length();
        if (armSpan < 1e-4f)
            return false;
        rig.normal = across / armSpan;
        Ogre::Vector3 mid = (P[lu] + P[ru]) * 0.5f;

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
                rig.K[i] = Ogre::Quaternion::IDENTITY;
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

    float parryEnvelope(double age)
    {
        if (age < 0.0 || age >= g_parryMs || g_parryMs <= 0.0)
            return 0.0f;
        float x = (float)(age / g_parryMs);
        float t = x < 0.2f ? x / 0.2f : x < 0.6f ? 1.0f : 1.0f - (x - 0.6f) / 0.4f;
        return t * t * (3.0f - 2.0f * t);
    }

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

        Ogre::Vector3 goal = (rig.upM * 0.75f + rig.normal * 0.65f).normalisedCopy();
        Ogre::Quaternion d = Ogre::Quaternion::Slerp(amount, Ogre::Quaternion::IDENTITY, v.getRotationTo(goal), true);
        modelXf(rig, rig.parent[rig.lu], q, p, parQ, parP);
        q[rig.lu] = parQ.Inverse() * d * uq;
        if (rig.prop1OnLeftArm)
            return;

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
        float in = -d.dotProduct(outward);
        if (in > 0.0f)
            d = (d + outward * in).normalisedCopy();
        float c = d.dotProduct(axis);
        if (c < maxCos)
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

    void hangRight(State& s, std::vector<Ogre::Quaternion>& q, std::vector<Ogre::Vector3>& p)
    {
        const Rig& rig = s.rig;
        if (!rig.canUncross)
            return;

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
        if (dt < 0.0f || dt > 0.5f)
        {
            s.hangE = sh + rest * l1;
            s.hangH = s.hangE + rest * l2;
            s.hangVE = s.hangVH = Ogre::Vector3::ZERO;
            diag::count("arms_limp_reset");
        }
        else if (dt > 0.0f)
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

    bool blockClip(int k)
    {
        return k == arms::CLIP_SCISSOR || k == arms::CLIP_BLOCK_LEFT || k == arms::CLIP_OFF_BLOCK
            || k == arms::CLIP_OFF_BLOCK_FWD || k == arms::CLIP_DUAL_IMPACT_L || k == arms::CLIP_DUAL_IMPACT_R

            || k == arms::CLIP_ANGLED_L || k == arms::CLIP_ANGLED_R || k == arms::CLIP_ANGLED_L_IMPACT
            || k == arms::CLIP_ANGLED_R_IMPACT || k == arms::CLIP_MAIN_OVERHEAD || k == arms::CLIP_MAIN_OVERHEAD_IMPACT

            || k == arms::CLIP_MAIN_BLOCK || k == arms::CLIP_MAIN_IMPACT;
    }

    bool reactionClip(int k)
    {
        return k == arms::CLIP_BLOCK_LEFT || k == arms::CLIP_OFF_BLOCK || k == arms::CLIP_OFF_BLOCK_FWD
            || k == arms::CLIP_DUAL_IMPACT_L || k == arms::CLIP_DUAL_IMPACT_R
            || k == arms::CLIP_ANGLED_L_IMPACT || k == arms::CLIP_ANGLED_R_IMPACT || k == arms::CLIP_MAIN_OVERHEAD_IMPACT
            || k == arms::CLIP_BROKEN_ANGLED_L || k == arms::CLIP_BROKEN_ANGLED_R || k == arms::CLIP_BROKEN_SCISSOR
            || k == arms::CLIP_MAIN_IMPACT || k == arms::CLIP_HIT_TAKEN;
    }

    bool g_angledSwap = false;

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

        if (g_clipOn[arms::CLIP_MAIN_BLOCK] && (!g_clipOn[arms::CLIP_SCISSOR] || (rand() & 1)))
            return arms::CLIP_MAIN_BLOCK;
        return g_clipOn[arms::CLIP_SCISSOR] ? (int)arms::CLIP_SCISSOR : -1;
    }

    int parryClip()
    {
        if (g_clipOn[arms::CLIP_OFF_BLOCK])
        {
            if (g_offBlockStyle == 1)
                return arms::CLIP_OFF_BLOCK;
            if (g_offBlockStyle == 2)
                return arms::CLIP_OFF_BLOCK_FWD;
            return (rand() & 1) ? arms::CLIP_OFF_BLOCK_FWD : arms::CLIP_OFF_BLOCK;
        }
        return g_clipOn[arms::CLIP_BLOCK_LEFT] ? (int)arms::CLIP_BLOCK_LEFT : -1;
    }

    bool applyClip(Ogre::Skeleton* sk, State& s, Mode mode)
    {
        if (s.clip < 0)
            return false;
        int k = s.clip;
        if (mode == SWAP || mode == TWIN || mode == LEFTY)
        {
            s.clip = -1;
            diag::count("l3_clip_cut_by_strike");
            return false;
        }
        Ogre::Animation* a = resolveClip(sk, s, k);
        if (a == NULL)
        {
            s.clip = -1;
            return false;
        }
        float len = a->getLength();
        float age = (float)((defense::frameMs() - s.clipStart) / 1000.0);
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

        if (s.clipYield < 0.0f && s.foreign)
        {
            s.clipYield = age;
            diag::count("l3_clip_yield_foreign");
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

        bool body = moves::fullBody(k);
        for (Ogre::Animation::OldNodeTrackList::const_iterator it = tl.begin(); it != tl.end(); ++it)
        {
            int h = (int)it->first;
            if (h >= rig.n || !(rig.clipBone[h] || (body && rig.legBone[h])) || it->second == NULL)
                continue;
            it->second->getInterpolatedKeyFrame(ti, &kf);
            Ogre::Quaternion q = rig.bindQ[h] * kf.getRotation();
            s.tgt[h] = Ogre::Quaternion::Slerp(env, s.tgt[h], q, true);
            if (h == rig.prop1 || h == rig.prop2)
            {
                Ogre::Vector3 p = rig.bindP[h] + kf.getTranslate() * s.clipScale[k];
                s.tgtP[h] = s.tgtP[h] + (p - s.tgtP[h]) * env;
            }
            else if (body && h == rig.root)
            {
                float y = rig.bindP[h].y + kf.getTranslate().y * s.clipScale[k];
                s.tgtP[h].y += (y - s.tgtP[h].y) * env;
                s.rootDriven = true;
            }
        }
        if (body && diag::stage())
            diag::count("l3_full_body_reaction_frame");

        if (k == arms::CLIP_SCISSOR || k == arms::CLIP_BLOCK_LEFT)
            squareChest(rig, s.tgt, env);
        return true;
    }

    bool applyAttackClip(Ogre::Skeleton* sk, State& s)
    {
        if (s.attackClip < 0)
            return false;
        int k = s.attackClip;
        Ogre::Animation* a = resolveClip(sk, s, k);
        if (a == NULL)
            return false;
        if (!s.attackCounted)
        {
            s.attackCounted = true;
            diag::count(CLIP_PLAYED[k]);
        }
        float x = std::max(0.0f, std::min(1.0f, s.attackProgress));
        float in = std::min(1.0f, x / 0.05f), out = std::min(1.0f, (1.0f - x) / 0.06f);

        if (k == arms::CLIP_DASH || k == arms::CLIP_OFF_RISING || k == arms::CLIP_TWIN_SWEEP
            || k == arms::CLIP_SPIN)
            in = out = 1.0f;
        float env = in * in * (3.0f - 2.0f * in) * out * out * (3.0f - 2.0f * out);
        const Rig& rig = s.rig;
        const Ogre::Animation::OldNodeTrackList& tl = a->_getOldNodeTrackList();
        Ogre::TimeIndex ti(x * a->getLength());
        Ogre::TransformKeyFrame kf(NULL, 0.0f);

        bool body = moves::fullBody(k) && bodyAllowed(sk, s, k, a);
        for (Ogre::Animation::OldNodeTrackList::const_iterator it = tl.begin(); it != tl.end(); ++it)
        {
            int h = (int)it->first;
            if (h >= rig.n || !(rig.clipBone[h] || (body && rig.legBone[h])) || it->second == NULL)
                continue;
            it->second->getInterpolatedKeyFrame(ti, &kf);
            s.tgt[h] = Ogre::Quaternion::Slerp(env, s.tgt[h], rig.bindQ[h] * kf.getRotation(), true);
            if (h == rig.prop1 || h == rig.prop2)
            {
                Ogre::Vector3 p = rig.bindP[h] + kf.getTranslate() * s.clipScale[k];
                s.tgtP[h] = s.tgtP[h] + (p - s.tgtP[h]) * env;
            }
            else if (h == rig.root)
            {
                float y = rig.bindP[h].y + kf.getTranslate().y * s.clipScale[k];
                s.tgtP[h].y += (y - s.tgtP[h].y) * env;

                if (moves::longTail(k))
                    s.tgtP[h].z += kf.getTranslate().z * s.clipScale[k] * env;
                if (moves::sideTravel(k))
                    s.tgtP[h].x += kf.getTranslate().x * s.clipScale[k] * env;
                s.rootDriven = true;
            }
        }
        if (body && diag::stage())
            diag::count("l3_full_body_frame");
        return true;
    }

    const char* RUN_BASE_TRACK = "run lower";
    const float RUN_MATCH_MIN = 0.985f;
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

    bool legPhase(Ogre::Skeleton* sk, State& s, float& phase, float& baseLen)
    {
        const Rig& rig = s.rig;
        if (rig.thigh[0] < 0 || rig.thigh[1] < 0 || rig.thigh[0] >= rig.n || rig.thigh[1] >= rig.n)
            return false;
        if (!s.runBaseTried)
        {
            s.runBaseTried = true;
            s.runBase = findAnimation(sk, RUN_BASE_TRACK);
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
        if (best < RUN_MATCH_MIN)
            return false;
        float lo = bt - step, hi = bt + step;
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

    void applyRunClip(Ogre::Skeleton* sk, State& s, Mode mode, bool other)
    {
        bool want = s.running && mode == MIRROR && !other && !s.foreign && g_clipOn[arms::CLIP_RUN_TRAIL];
        double now = defense::frameMs();
        float dt = (float)std::max(0.0, std::min(250.0, now - s.runLast)) / 1000.0f;
        s.runLast = now;
        if (want && s.runClip < 0)
        {

            s.runClip = arms::CLIP_RUN_TRAIL;
            s.runStart = now;
            s.runW = 0.0f;
            s.runCounted = false;
            s.runPhaseCounted = false;
        }
        if (s.runClip < 0)
            return;

        bool attack = mode == HOLD || mode == SWAP || mode == TWIN || mode == BLOCK || s.foreign;
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

        float phase = 0.0f, baseLen = a->getLength();
        bool locked = false;
        try { locked = legPhase(sk, s, phase, baseLen); }
        catch (...) { locked = false; }
        if (locked)
            s.runPhase = phase;
        else
        {
            s.runPhase += dt / baseLen;
            s.runPhase -= std::floor(s.runPhase);
        }
        if (!s.runPhaseCounted || locked != s.runLocked)
        {
            s.runPhaseCounted = true;
            s.runLocked = locked;
            diag::count(locked ? "run_hold_phase_locked" : "run_hold_phase_free");
        }
        Ogre::TimeIndex ti(std::min(s.runPhase * baseLen, a->getLength() - 0.001f));
        Ogre::TransformKeyFrame kf(NULL, 0.0f);
        for (Ogre::Animation::OldNodeTrackList::const_iterator it = tl.begin(); it != tl.end(); ++it)
        {
            int h = (int)it->first;
            if (h >= rig.n || !rig.clipBone[h] || it->second == NULL)
                continue;
            it->second->getInterpolatedKeyFrame(ti, &kf);
            s.tgt[h] = Ogre::Quaternion::Slerp(env, s.tgt[h], rig.bindQ[h] * kf.getRotation(), true);
            if (h == rig.prop1 || h == rig.prop2)
            {
                Ogre::Vector3 p = rig.bindP[h] + kf.getTranslate() * s.clipScale[s.runClip];
                s.tgtP[h] = s.tgtP[h] + (p - s.tgtP[h]) * env;
            }
        }
    }

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
            s.tgt[h] = Ogre::Quaternion::Slerp(e, s.tgt[h], rig.bindQ[h] * kf.getRotation(), true);
            if (h == rig.prop1 || h == rig.prop2)
            {
                Ogre::Vector3 p = rig.bindP[h] + kf.getTranslate() * s.clipScale[k];
                s.tgtP[h] = s.tgtP[h] + (p - s.tgtP[h]) * e;
            }
            else if (h == rig.root)
            {
                float y = rig.bindP[h].y + kf.getTranslate().y * s.clipScale[k];
                s.tgtP[h].y += (y - s.tgtP[h].y) * e;
                s.rootDriven = true;
            }
        }
    }

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

    void applyStanceClip(Ogre::Skeleton* sk, State& s, Mode mode, bool other)
    {
        int byRank = g_clipOn[arms::CLIP_STANCE_DESHI] ? stanceForRank(s.rank) : -1;
        bool want = s.inCombat && mode == MIRROR && !other && !s.running && !s.foreign && byRank >= 0;
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
        bool action = mode != MIRROR || other || s.foreign;
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

            for (int i = 0; i < rig.n; ++i)
                if (rig.role[i] == LEFT_ARM)
                    s.tgt[i] = mirrorRot(rig, i, s.src);
            s.tgtP[rig.prop1] = mirrorPos(rig, rig.prop1, s.srcP);
            return;
        }
        if (mode == LEFTY)
        {

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

        if (mode == MIRROR && inCombat && g_square > 0.0f)
            squareChest(rig, s.tgt, g_square);
        if (mode == MIRROR)
            uncrossRight(rig, s.tgt, s.srcP, g_uncross);
        for (int i = 0; i < rig.n; ++i)
            if (rig.role[i] == LEFT_ARM)
                s.tgt[i] = mirrorRot(rig, i, s.tgt);
        s.tgtP[rig.prop1] = mirrorPos(rig, rig.prop1, s.tgtP);

        if (mode == MIRROR && inCombat)
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
            s.rig.n = 0;
            diag::count("arms_rig_changed");
            return;
        }
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = s.started ? (float)(now.QuadPart - s.lastT.QuadPart) / (float)g_freq.QuadPart : 0.0f;
        s.lastT = now;
        if (dt < 0.0f || dt > 0.25f)
            dt = 0.25f;

        readSource(sk, s);
        Mode mode = s.mode;
        if (mode == SWAP && !g_swap)
            mode = HOLD;
        int attackNow = (mode == TWIN || mode == SWAP) ? s.attackClip : -1;
        if (!s.started || mode != s.appliedMode || s.inCombat != s.appliedCombat || attackNow != s.appliedAttack)
        {

            s.from = s.started ? s.out : s.src;
            s.fromP = s.started ? s.outP : s.srcP;
            s.blendT = 0.0f;
            s.blendDur = (attackNow >= 0 || s.appliedAttack >= 0) ? ATTACK_BLEND_S : g_blendSec;
            if (attackNow >= 0 || s.appliedAttack >= 0)
                diag::count("attack_blend_started");
            s.appliedMode = mode;
            s.appliedCombat = s.inCombat;
            s.appliedAttack = attackNow;
        }
        if (s.clipSwap)
        {
            s.clipSwap = false;
            if (s.started)
            {
                float left = s.blendDur * (1.0f - s.blendT);
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
        if (!clipPlays && s.parryStart >= 0.0 && g_parryPose > 0.0f && mode != SWAP && mode != TWIN && mode != LEFTY)
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
        float t = s.blendT * s.blendT * (3.0f - 2.0f * s.blendT);
        for (int i = 0; i < rig.n; ++i)
        {
            Ogre::OldBone* b = sk->getBone((unsigned short)i);
            s.out[i] = t >= 1.0f ? s.tgt[i] : Ogre::Quaternion::Slerp(t, s.from[i], s.tgt[i], true);
            b->setOrientation(s.out[i]);
            s.written[i] = b->getOrientation();
            if (i == rig.prop1 || i == rig.prop2)
            {
                s.outP[i] = s.fromP[i] + (s.tgtP[i] - s.fromP[i]) * t;
                b->setPosition(s.outP[i]);
            }
            else if (i == rig.root && s.rootDriven)
            {
                s.outP[i] = s.tgtP[i];
                s.rootCarry = s.tgtP[i] - s.srcP[i];
                b->setPosition(s.outP[i]);
            }
            else if (i == rig.root && s.rootCarry.squaredLength() > 1e-6f)
            {

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
        s.started = true;

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

    LONGLONG g_costTicks = 0, g_costMaxTicks = 0;
    unsigned g_costCalls = 0;
    int g_costPeak = 0;

    void updateTransforms_hook(Ogre::Skeleton* self)
    {
        if (g_count > 0)
        {
            EnterCriticalSection(&g_lock);
            std::map<Ogre::Skeleton*, State>::iterator it = g_states.find(self);
            if (it != g_states.end())
            {
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
        s.lastT.QuadPart = 0;
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

    void startClip(State& s, int k, double startMs)
    {
        loadL3Files();
        if (s.clip >= 0 && !reactionClip(k))
            return;
        if (s.clip >= 0)
        {
            s.clipSwap = true;
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

        if (KenshiLib::AddHook(reinterpret_cast<void*>(target), reinterpret_cast<void*>(updateTransforms_hook),
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
            g_byChar.erase(it);
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
            g_states.erase(st);
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
                g_byChar.erase(it);
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

    void setOffBlockStyle(int style) { g_offBlockStyle = style; }

    void setAngledSwap(bool on) { g_angledSwap = on; }
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

            if (s.mode != in.mode)
                diag::count(in.mode == TWIN ? "arms_mode_twin" : (in.mode == SWAP ? "arms_mode_swap" : (in.mode == HOLD ? "arms_mode_hold"
                            : (in.mode == BLOCK ? "arms_mode_block" : (in.mode == LEFTY ? "arms_mode_lefty" : "arms_mode_mirror")))));
            if (in.mode == BLOCK && s.mode != BLOCK)
            {

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

            if (in.running && !s.running && g_clipOn[CLIP_RUN_TRAIL])
                loadL3Files();
            s.running = in.running;

            if (in.inCombat && g_clipOn[CLIP_STANCE_DESHI])
                loadL3Files();
            s.moving = in.moving;
            s.foreign = in.foreign;
            s.rank = in.rank;

            if (k >= 0)
                loadL3Files();
            if (s.attackClip != k)
                s.attackCounted = false;
            s.attackClip = k;
            s.attackProgress = in.attackProgress;

            if (in.parryStart >= 0.0 && s.parryStart != in.parryStart)
            {
                s.parryStart = in.parryStart;
                s.parrySnap = s.snapCount > 0 ? (int)((unsigned)(in.parryStart / 7.0) % (unsigned)s.snapCount) : -1;
                diag::count(s.parrySnap >= 0 ? "arms_parry_block_pose" : "arms_parry_swing");
                int pk = parryClip();
                if (pk >= 0)
                    startClip(s, pk, in.parryStart);
            }

            if (in.blockHitStart >= 0.0 && s.blockHitStart != in.blockHitStart)
            {
                s.blockHitStart = in.blockHitStart;

                if (s.blockKind == CLIP_ANGLED_L && g_clipOn[CLIP_ANGLED_L_IMPACT])
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
            s->clipHit = true;
        LeaveCriticalSection(&g_lock);
    }

    void clipHit(Character* c)
    {
        if (g_count == 0 || c == NULL)
            return;
        EnterCriticalSection(&g_lock);
        State* s = stateOf(c);

        if (s != NULL && (s->mode == BLOCK || (s->clip >= 0 && blockClip(s->clip))) && s->blockKind >= 0
            && g_clipOn[CLIP_BROKEN_SCISSOR])
        {
            int k = s->blockKind == CLIP_ANGLED_L ? (int)CLIP_BROKEN_ANGLED_L
                  : s->blockKind == CLIP_ANGLED_R ? (int)CLIP_BROKEN_ANGLED_R : (int)CLIP_BROKEN_SCISSOR;
            startClip(*s, k, defense::frameMs());
            s->blockKind = -1;
            diag::count("l3_guard_broken");
        }

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
