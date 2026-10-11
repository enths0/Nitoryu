// Speech-bubble feedback. Combat hooks run off the main-loop thread, so lines are queued there and
// said from the main loop, re-resolving each character through its handle.
//
// A test aid: say() speaks only with Nitoryu.ini [General] env=stage; env=prod (the default, and the
// release) says nothing. Other test-only output can check stage() too.
#pragma once

#include <string>

class Character;

namespace feedback
{
    void init();        // after stance::init (reads [General] env)
    bool stage();       // [General] env=stage
    // Queue `text` for `c`; the same character says a line at most once per `minGapMs`.
    void say(Character* c, const char* text, unsigned minGapMs);
    // A line that is part of the game, not a test aid: queued in prod too.
    void announce(Character* c, const char* text);
    void flush();       // main loop only
    void forget(Character* c);      // Character destructor

    // A feature group that turned itself off ("pose", "ui"...): one error line in the log, once per feature,
    // nothing in game. Any thread, also before init().
    void featureOff(const char* feature, const char* why);
}
