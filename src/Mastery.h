#pragma once

class Character;

namespace mastery
{
    void init();
    void applySettings();
    void setEnabled(bool on);
    void forget(Character* c);
}

namespace flow
{

    void onAttacked(Character* victim, Character* attacker);
    bool active(Character* c);
    float dexBonus(Character* c);
    float speedFactor(Character* c);
    float capBonus(Character* c);
}

namespace mugen
{

    double doubleCooldownMs(Character* c, double base);
    void onTwinLanded(Character* attacker);
    bool takeChain(Character* c, double now);
}
