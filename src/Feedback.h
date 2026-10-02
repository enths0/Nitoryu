#pragma once

class Character;

namespace feedback
{
    void init();
    bool stage();

    void say(Character* c, const char* text, unsigned minGapMs);

    void announce(Character* c, const char* text);
    void flush();
    void forget(Character* c);
}
