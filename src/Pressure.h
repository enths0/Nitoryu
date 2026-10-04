#pragma once

#include <kenshi/Enums.h>
#include <kenshi/util/hand.h>

#include <vector>

class Character;

namespace pressure
{
    void init();
    void applySettings();
    bool installHooks();
    void setEnabled(bool on);

    void onImpact(Character* victim, Character* attacker, HitMaterialType r);
    void forget(Character* c);

    int stacks(Character* target, Character* master);
    int maxStacks();

    struct Shown
    {
        hand target, by;
        int stacks;
    };
    void shown(std::vector<Shown>& out);
}
