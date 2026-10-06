#pragma once

#include <string>

class Character;

namespace feedback
{
    void init();
    bool stage();

    void say(Character* c, const char* text, unsigned minGapMs);

    void announce(Character* c, const char* text);
    void flush();
    void forget(Character* c);

    void featureOff(const char* feature, const char* why);
}
