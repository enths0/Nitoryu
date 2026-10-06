#pragma once

class Character;
class CombatTechniqueData;

namespace defense
{
    void init();
    void applySettings();
    bool installHooks();
    void setEnabled(bool on);
    void tick();

    double clockMs();
    float headingDot(Character* me, Character* who);
    double frameMs();

    struct Constants
    {
        bool found;
        float blockBase;
        float cut1, cut99;
        float blunt1, blunt99;
        float damageMult;
    };
    void readConstants();
    Constants constants();

    struct HitScope
    {
        HitScope(Character* victim, Character* attacker, CombatTechniqueData* attack, int comboID);
        ~HitScope();
    private:
        Character* prevVictim;
        Character* prevAttacker;
        CombatTechniqueData* prevAttack;
        int prevCombo;
        Character* prevParry;
    };

    bool takeCounter(Character* c);
    float counterChance(float dp);

    bool offParried(Character* victim);

    double lastParryMs(Character* c);

    double lastMainBlockMs(Character* c);

    bool lastParryBack(Character* c);
    bool lastMainBlockBack(Character* c);
    void forget(Character* c);
}
