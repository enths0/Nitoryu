#pragma once

#include <map>
#include <string>

class Character;
class CombatTechniqueData;

namespace ambidex
{
    void init();
    bool installHooks();
    bool installLifetimeHook();
    void setEnabled(bool on);

    float get(Character* c);

    float stored(Character* c);

    void setStored(Character* c, float a);

    std::string testText();

    void setListSkills(const std::map<std::string, float>& byRecord);

    std::string testNpcText();

    struct Effective
    {
        float a, eff;
        float overload;
        float overloadMult, armourMult;
        int armourPieces;
    };
    Effective effective(Character* c);

    void noteImpact(Character* attacker, CombatTechniqueData* attack, bool offHand);

    void noteRank(Character* c);

    void watchWeaponXp(Character* c);
}
