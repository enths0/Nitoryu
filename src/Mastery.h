#pragma once

#include <kenshi/util/hand.h>

#include <vector>

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

    struct Shown
    {
        hand who;
        float left;
    };
    void shown(std::vector<Shown>& out);
}

namespace mugen
{

    double doubleCooldownMs(Character* c, double base);
    void onTwinLanded(Character* attacker);
    bool takeChain(Character* c, double now);

    void shown(std::vector<hand>& out);
}
