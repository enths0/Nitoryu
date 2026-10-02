#pragma once

class Character;
class Weapon;

namespace lefty
{
    void init();
    void applySettings();
    bool installHooks();
    bool iniOn();
    void setEnabled(bool on);

    bool qualifies(Character* c);

    bool active(Character* c);

    float damageFactor(Character* c);
    float speedFactor(Character* c);
    float blockPenalty(Character* c);

    void takeNoted(void (*fn)(Character*));
    void forget(Character* c);
}
