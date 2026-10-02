#pragma once

#include <kenshi/Enums.h>

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
}
