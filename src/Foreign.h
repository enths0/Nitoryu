#pragma once

#include <string>

class Character;
class CombatClass;

namespace foreign
{

    struct Watch
    {
        const void* anim;
        float time01;
        bool startedForeign;
        double until;
        double runSince;
        bool blockUp;
        Watch() : anim(NULL), time01(0.0f), startedForeign(false), until(0.0), runSince(0.0), blockUp(false) {}
    };

    void applySettings();
    bool active();

    bool actionNow(Character* c, CombatClass* cc, Watch& w, double nowMs);

    bool playing(const Watch& w);

    bool driven(const Watch& w, double nowMs);

    bool running(bool running, Watch& w, double nowMs);

    bool blockHeld(Character* c);

    void noteIncoming(Character* victim, const std::string& attacker, const std::string& tech, int combo,
                      int result, bool landed, float cutIn, float bluntIn, float cutLeft, float bluntLeft);
}
