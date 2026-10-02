#pragma once

#include <kenshi/Enums.h>

#include "Stance.h"

class Character;
class CombatTechniqueData;
class Weapon;

namespace blades
{
    void init();
    void applySettings();
    bool installHooks();
    void setEnabled(bool on);
    void forget(Character* c);

    enum Pair { PAIR_PLAIN, PAIR_LONG_SHORT, PAIR_EQUAL };
    Pair pairOf(Weapon* a, Weapon* b);
    const char* pairName(Pair p);
    bool mixedTypes(Weapon* a, Weapon* b);
    float guardMult(Weapon* a, Weapon* b);
    float twinMult(Weapon* a, Weapon* b);

    float offDefenceShare(const stance::Info& s, float bonus, float usual);

    class Scissor
    {
    public:
        Scissor(Character* attacker, Character* victim, const stance::Info& s, bool offBlade, bool twin);
        ~Scissor();
        void landed(HitMaterialType r);
    private:
        Character* attacker;
        Character* victim;
        bool offBlade, on, second;
        void* forced;
        float savedChance;
        void* wanted;
    };

    void noteAttacker(Character* victim, Character* attacker);

    Character* flanker(Character* me, Character* mainTarget, const stance::Info& s, CombatTechniqueData* attack,
                       int comboID);
}
