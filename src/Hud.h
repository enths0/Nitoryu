// Marks that show the rank skills at work without a word on screen.
//
//   flow     a wave, and a bar under it that runs out with the flow
//   Mugen    a loop while a chain waits for its twin (and a moment after)
//   pressure one pip per stack; the pips change colour at full pressure
//
// Where (cfg::HUD_PLACE): above the heads (the default), on the squad portraits, or both. Whose marks show is
// cfg::HUD_MARKS (the default: the selected characters; marks over every head get crowded in a big fight).
//   portraits: children of the portrait cell's widget, painted in a post-hook of PortraitMainCellView::update;
//              the pips are the most stacks this fighter holds on one enemy.
//   heads:     our own MyGUI images in one container of ours on the layer under the game's windows, placed
//              from the head's world position the way UtilityT::worldToScreenRel does it; the pips sit on
//              the pressured enemy.
// Main loop only.
#pragma once

namespace hud
{
    void init();
    void applySettings();       // from cfg (init, and after a change)
    bool installHooks();        // PortraitMainCellView::update post-hook; false: no marks on the portraits
    void setEnabled(bool on);   // false hides every mark at the next tick
    void tick();                // main loop only
}
