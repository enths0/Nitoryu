#pragma once

#include <kenshi/Enums.h>

#include <string>

class Character;
class Weapon;

namespace stance
{
    struct Info
    {
        bool dual;
        const char* reason;
        Weapon* main;
        Weapon* off;
        float skillMain, skillOff;
        float dp;
        int tier;
        int tierByDp;
        const char* tierCap;
        bool optedIn;
        float clumsy;
        float guardPenalty;
        float speedFactor;
        float weightDual;
        float ambi, ambiEff;
        float overload;
        float dex;
        bool offFromHip;
    };

    void init();
    void applySettings();
    std::string iniFile();

    float applySpeed(float gameResult, float factor, bool isBlock, float capBonus = 0.0f);
    bool allowed(WeaponCategory c);
    std::string allowListText();

    bool weaponAllowed(Weapon* w);
    bool weaponRefused(Weapon* w);

    enum Rig { RIG_AUTO = 0, RIG_FIXED_GRIP = 1, RIG_NEVER = 2 };
    int rigOf(Character* c);

    Info evaluate(Character* c, Weapon* mainOverride);

    void invalidate(Character* c);

    bool togglePlayer(Character* c);
    bool playerAllowed(Character* c);
    void setPlayerAllowed(Character* c, bool on);
    void forget(Character* c);

    bool hasPair(Character* c);

    bool carriedPair(Character* c, Weapon*& a, Weapon*& b);

    float previewDP(Character* c);

    struct Preview
    {
        float dp;
        int tier, tierByDp;
        float nextAmbi;
        float speedFactor;
    };
    Preview preview(Character* c);

    float poolSkill(const Info& s);

    void refreshOptIn();

    void prune(unsigned olderThanMs);

    float offHandShare(float dp);
    bool testShareSet();

    int forcedTier();
    int tierOf(float dp);

    bool knownLayout();
    std::string gameVersion();
    StatsEnumerated statFor(WeaponCategory c);
}
