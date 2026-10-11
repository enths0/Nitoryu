// The pair dummy: a training building for Dual Wielding.
//
// A training dummy gives XP in UseableStuff::operate -> CharStats::xpTraining(time, 2, stat, limit = the
// building's `output rate`). For a BUILDING record ticked `nitoryu pair dummy` the stat that call trains is
// swapped for the character's Dual Wielding, so the amount, the XP rate and the cap are the game's own; a
// character without an allowed pair in its weapon slots trains nothing there.
#pragma once

namespace dummy
{
    void applySettings();        // from cfg (start, and after a change)
    bool installHooks();         // UseableStuff::operate, CharStats::xpTraining
    void setEnabled(bool on);
}
