#pragma once

#include <string>

#include "ClipsGen.h"

class Character;
namespace Ogre { class Skeleton; }

namespace arms
{
    enum Mode { MIRROR, HOLD, SWAP, TWIN, BLOCK, LEFTY };

    bool installHook();
    void init(float squareTorso, float uncross, unsigned blendMs, bool swap);

    enum Owner { OWNER_DUAL = 1, OWNER_LEFTY = 2, OWNER_ALL = 3 };

    bool track(Character* c, Ogre::Skeleton* skeleton, unsigned owner);
    void release(Character* c, unsigned owner);
    void untrack(Character* c);

    Ogre::Skeleton* skeletonOf(Character* c);

    bool hasTrack(Character* c, const std::string& name, std::string* rigName);
    int unsupportedCount();
    void preload();
    void setMode(Character* c, Mode m, bool inCombat);

    void setParry(Character* c, double startMs);
    void initParry(unsigned durationMs, float strength);
    const char* modeName(Mode m);

#define NITORYU_CLIP_ENUM(e, track, name, ini, legacy, counter, full, base) e,
    enum Clip { NITORYU_L3_CLIPS(NITORYU_CLIP_ENUM) CLIP_COUNT };
#undef NITORYU_CLIP_ENUM

    void setClipOn(Clip k, bool on);
    void setOffBlockStyle(int style);
    void setAngledSwap(bool on);
    void setLimpSwing(bool on);
    bool clipOn(Clip k);

    void playClip(Character* c, Clip k, double startMs);

    bool clipPlaying(Character* c, Clip k);
    void stopClip(Character* c, Clip k);

    void clipHit(Character* c);

    void setAttackClip(Character* c, int k, float progress01);

    void setRunning(Character* c, bool running);

    struct FrameInput
    {
        Mode mode;
        bool inCombat;
        bool running;
        bool moving;
        bool foreign;
        int rank;
        int attackClip;
        float attackProgress;
        double parryStart;
        double blockHitStart;
        bool parryBack;
        bool blockHitBack;
        int blockDir;
        double nowMs;
        int reacting;
    };
    void setFrame(Character* c, const FrameInput& in);

    enum Reacting { REACT_NONE = 0, REACT_STAGGER = 1, REACT_DOWN = 2 };
    void setReacting(Character* c, int level);
    void emitClipLog();

    void takeCost(double& totalMs, double& maxMs, unsigned& calls, int& peak);
    const char* clipText();
}
