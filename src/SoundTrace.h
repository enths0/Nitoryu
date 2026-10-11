// Which sounds a fight makes: a test switch for finding impacts that play no sound.
//
// Off in the shipped ini. `[Test] sound_trace=1` hooks Character::audioEvent (the original is called first,
// nothing is changed) and writes
//   sound  t=<ms> who=... name=<the event's name> range=... ok=<what the game returned>
//   impact t=<ms> attacker=... victim=... result=<HitMaterialType> combo=... move=... ours=<our move or a dual attacker>
// so an impact with no `sound` line of its attacker or victim at the same t is a silent one. Counters:
// `sound_calls`, `sound_steps_and_idle` (footsteps and idle creature voices: counted, no line), `sound_impacts`.
// `ok` is nearly always 0, so it does not mean "not played".
#pragma once

#include <string>

class Character;

namespace soundtrace
{
    void init(const std::string& iniFile);
    bool on();
    bool installHooks();        // nothing when off
    // Combat thread, from the hit hook after the original: one impact and its result.
    void impact(Character* victim, Character* attacker, int result, int comboID, const std::string& move, bool ours);
}
