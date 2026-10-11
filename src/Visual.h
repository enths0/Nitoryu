// The off-hand weapon's bare blade in the left hand while the stance is drawn, and only its empty sheath
// left on the back or hip.
//
// How vanilla shows a drawn weapon (1.0.65):
//   - AppearanceBase::slots maps slot name -> AttachmentData {bone, offset}; per
//     race it is `hands` (hard-coded, Bip01 Prop2) plus the points in the race's
//     "attachment points" .phs (back, back2, hip). There is no left-hand slot,
//     so Nitoryu adds one to the map at run time (bone Bip01 L Hand, offset =
//     the mirror of Prop2 in the right hand, from the bind pose).
//   - CharacterHuman::leaveSheathEquipped detaches the drawn weapon's back slot
//     and re-attaches the WEAPON "sheath" mesh there; Nitoryu does the same for
//     the off weapon and attaches its "bare sword" mesh to the new slot.
// Everything runs on the main loop (Ogre work); combat hooks only queue.
#pragma once

class Character;

namespace visual
{
    void init();                        // reads [OffHand] from Nitoryu.ini
    void setEnabled(bool on);           // false takes every blade back at the next tick and stops
    // False when the Ogre pose hook did not install: the left blade then hangs on the hand slot
    // (Bip01 L Hand with a fixed grip), and left-hand fighting shows no blade.
    void setPoseEngine(bool on);
    bool poseEngine();
    void noteDraw(Character* c);        // any thread: this character (re)drew
    void noteSheathe(Character* c);     // any thread: it sheathed (only tracked ones matter)
    void tick();                        // main loop only
    // Any thread. The left blade is attached and posed for this fighter; the Dash is not picked before,
    // or its first frames show the base track without the second blade.
    bool bladeReady(Character* c);
}
