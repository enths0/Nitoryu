#pragma once

class CombatTechniqueData;
class Character;

namespace combat
{

    bool isOffHandTechnique(CombatTechniqueData* t);
    bool isTwinTechnique(CombatTechniqueData* t);

    bool isRiposte(Character* c, CombatTechniqueData* t);

    int moveKind(CombatTechniqueData* t);
    int armsModeOf(CombatTechniqueData* t);
    int attackClip(CombatTechniqueData* t);

    int tailClip(CombatTechniqueData* t);
    bool dashOn();
    void setDashOn(bool on);

    bool isForeignLongDash(CombatTechniqueData* t);
    bool twinsForeignDash(Character* c, CombatTechniqueData* t);
    bool installHooks();
    void applySettings();
    void setEnabled(bool on);
    bool enabled();

    void emitFight();

    void logTechniques();

    void enforceSpeed(Character* c);
    int missingTrackCount();

    void forget(Character* c);
}
