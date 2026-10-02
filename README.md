# Nitoryu: Dual Wielding Combat System

*Русская версия: `README_RU.md`.*

hi all! this is a mod for fighting with paired blades in Kenshi. a fighter carrying two blades draws both and fights with one in each hand: the main hand swings as usual, the second blade adds its own strikes, twin strikes, parries and a few special moves.

I play with it myself, full time, on a Second Life of Kenshi build + 200 mods on top. I'm not an animator, so go easy on the animations (I made 30+ of my own: attacks, blocks, block impacts, running, draws and sheathes, a combat stance per rank and the moves that come with skill).

at the start you'll be weaker than with one blade. as you level, new strikes, blocks and moves open up, and at stats ~80 a dual fighter is 25-30% stronger than an equal opponent with one sword and holds off several enemies.

the mod is playable, but the work goes on: more moves, more animations, and I'll keep tuning the top ranks. with big animation packs some moves look different. how to send a bug is at the end.

## Requirements

- **RE_Kenshi 0.3.5 or newer** (it brings KenshiLib 0.5.0): [Nexus](https://www.nexusmods.com/kenshi/mods/847) or [GitHub](https://github.com/BFrizzleFoShizzle/RE_Kenshi).
- **Kenshi 1.0.65** (Steam tested, GOG should work). On other game versions the dual stance stays off (one line in the log), left-hand fighting still works.
- Don't use it together with DualWieldProbe: if both are installed, Nitoryu turns itself off.

## Install

1. Subscribe on the Steam Workshop (or put the `Nitoryu` folder into `Kenshi\mods\`).
2. Enable **Nitoryu** and put it at the very end of your load order, after all your main mods.
3. Start the game through RE_Kenshi as usual.

to remove it, just disable it. saves stay loadable, characters only lose their Dual Wielding number (it comes back if you enable the mod again). if you still have a test build of Nitoryu in `Kenshi\mods\`, delete that folder: only one copy runs.

## How to use it

- Put **two weapons on a character's back** (or one on the back, one on the hip) and draw. The drawn one goes to the right hand, the other to the left. A weapon that takes a full slot can be part of a pair too.
- **Katanas and sabres** pair, also one of each (blunt can be switched on in `[Weapons] categories`). Heavy weapons, hackers, polearms and bows don't. A weapon without a left-hand mesh (`bare sword`) can't be the second blade, that pair fights with one.
- **The button** on the back weapon slot in the inventory (crossed blades = on, one grey blade = off) turns the dual stance on and off for that character. The choice is saved. No hotkey by default, `[UI] hotkey=alt+n` adds one.
- Everything else happens by itself in a fight: the fighter picks its strikes, parries and moves by its rank.

## The skill: Dual Wielding and the Path

**Dual Wielding** is a new skill in the stats window (weapons column) for every character.

- It levels like every other skill, just by fighting, but only with two blades or left-handed: half of the weapon XP of each such hit also goes into it. It grows faster while it is below your weapon skill and slower once it is above.
- A new character starts at a quarter of its best katana or sabre skill.
- The first levels can be trained on the **Pair Training Dummy** (below).
- Heavy armour and overload lower it in a fight, since the idea of dual wielding is an agile fighter. The minus shows in red next to the skill, and the hover says why and how much.
- It is saved with your game.

**The Path** is your overall skill with two blades, a number from 0 to 100 built from five things: the lower of your two weapon skills (30%), Dual Wielding (30%), Dexterity (20%), melee attack (10%) and melee defence (10%). All of them at X give Path X. The stats count as they are in the fight, so wounds, armour and overload pull the Path down, and a rank can drop in the middle of a fight.

## The ranks

the Path opens five ranks. ranks 3-5 also need Dual Wielding itself.

| Rank | Path | Dual Wielding | What it gives |
|---|---|---|---|
| 1 Deshi | 20 | - | The second blade starts striking on its own: the off-hand cut and the off-hand rising cut |
| 2 Bushi | 40 | - | The second blade parries hits the main one missed. The off-hand downward cut |
| 3 Ronin | 60 | 40 | Twin strikes, ripostes after a block, pressure, both blades into the same body part, the special moves (Cross finisher, Dash slash), other mods' long moves with both blades |
| 4 Kenshi | 75 | 60 | Flow, the Spin, two enemies at once |
| 5 Kensei | 90 | 80 | Mugen: moves with both blades come back much sooner, and a twin strike that lands can chain the next one |

every rank also has its own combat stance (see the animations). below rank 1 a fighter just holds two blades and pays the beginner's penalties.

### Rank 1, Deshi (Path 20)

- **Second-blade strikes.** After a main-hand attack the left blade strikes on its own: the off-hand cut and the **off-hand rising cut** (my own animation, the whole body in it).
- How often: 3% of attacks at Path 20, 10% at 40, 18% at 60, 30% at 75, 40% at 85, 50% at 90, 55% at 100.
- A second-blade strike does the second weapon's own damage, bleeding and armour penetration, x1.04 at Path 20, x1.14 at 40, x1.30 from 70.

### Rank 2, Bushi (Path 40)

- **Second-blade parry.** The second blade parries a hit the main blade missed: a hit from a second enemy (the full chance), or a later hit of one enemy's combo (half of it).
- The chance: 20% at Path 40, 30% at 60, 40% at 75, 50% at 90, 55% at 100.
- Only from the front and the flanks (110 degrees to each side), never while your fighter swings, staggers or is down, at most once per game second.
- A new strike: the **off-hand downward cut**.

### Rank 3, Ronin (Path 60, Dual Wielding 40)

- **Twin strikes.** Both blades in one swing, two wounds at once: the twin overhead cut, the cross slash and the **twin sweep** (my own animation). 25% of second-blade attacks at Path 60, 45% at 100. A new second-blade strike too, the off-hand combo.
- **Ripostes.** After you block, your next attack within 1.5 s can be a riposte with the second blade or both: 30% at Path 60, 60% at 100.
- **The parry in a duel.** The second blade now also parries the first hit of the enemy you're facing, at half the chance.
- **Pressure.** Every hit of yours an enemy blocks puts a stack of pressure on them (up to 5, one fades every 3 s). Each stack counts against them when the game decides who swings next, so they block more and attack less. A hit they land on you clears it.
- **The same body part.** When both blades of a twin strike land on one enemy, the second goes for the body part the first one hit: 80% at Path 60, always from 75. Limbs come off faster.
- **Special moves** (they also need Dual Wielding 50):
  - **Cross finisher:** the right blade down, then the left, an X. It comes on a staggering enemy, on an enemy under full pressure (5 stacks), or within 1.5 s after your block or parry. At most once per 3 s.
  - **Dash slash:** a sprint from up to 35 units away, the blades cross in an X and split out on the hit. At most once per 12 s per fighter.
- **Other mods' long moves** (like issen from More Combat Animations) are played with both blades, and the second blade hits too. They share the Dash's 12 s cooldown.
- Moves that hit with both blades (twin strikes, the Cross, the Dash) share a **5 s cooldown**, so double hits don't come back to back; the second blade strikes alone meanwhile. It also doesn't use the same move twice in a row.
- The beginner's split guard is gone from this rank.

### Rank 4, Kenshi (Path 75, Dual Wielding 60)

- **Flow.** After 3 enemy attacks aimed at your fighter within 5 s (blocked, parried or landed, all count) it concentrates for 4 s: Dexterity counts another +12% in the cut damage, the attack speed is x1.04 and its cap goes from 1.5 to 1.8. Then 5 s before attacks count again. A knockdown ends it.
- **The Spin.** A rush, a sweep with both blades, a side step, a turn rising into an X over the head and a cross-split: both blades hit on the sweep and on the split, 4 hits in one move. From a distance (6-25 units beyond reach), at most once per 20 s. While the Spin is ready it goes before the Dash.
- **Two enemies, two blades.** When a second enemy who attacked you in the last 8 s stands at your flank, the left blade of a twin strike or of the Cross lands on him, and the right one stays on your target.

### Rank 5, Kensei (Path 90, Dual Wielding 80)

- **Mugen**, "endless". The 5 s cooldown on moves that hit with both blades shrinks to 3.5 s at Path 90 and down to 2 s at Path 100.
- A twin strike that lands can pull the next twin right after it, whatever the cooldown: a 25% chance at Path 90, 50% at 100. A chained twin can chain again.

## Learning is hard

below rank 3 two blades are worse than one. it all goes away as the Path grows:

- **Clumsiness:** -10 melee attack and defence at Path 0, gone at Path 40.
- **Split guard:** your block is weaker at ranks 0-1, half as much at rank 2, not at all from rank 3.
- **Speed:** attack and block speed x0.85 at Path 0, normal from Path 30.
- **Strength:** a pair counts as 1.25 x both weapons' weight, so two equal blades need 2.5x the strength of one.
- **Heavy armour and overload** lower Dual Wielding in a fight, and with it the Path and the rank.

## What you pair

- **The second weapon's bonuses** count at half: its attack, defence and indoor bonuses. Its own strikes use its own damage, bleeding, armour penetration and its bonuses against races.
- **Long + short** (the shorter blade at most 80% of the longer, like katana + wakizashi): the second blade's parry chance x1.30.
- **Equal blades** (within 10% of each other): x1.30 twin strikes.
- **Two types** (katana + sabre): the second weapon's defence bonus counts in full.
- **Off-hand XP:** hits and parries by the second blade train the second weapon's skill.

hover Dual Wielding to see which pair you carry and what it gives.

## Attack speed: how it works and where you see it

Nitoryu doesn't replace the game's speed formula. the game computes attack and block speed as usual (Dexterity, weapon skill, weapon weight vs strength, armour, attack speed stops at 1.2), and Nitoryu multiplies that by the **dual speed factor**:

- A beginner is slower: x0.85 at Path 0, x1 at Path 30.
- A master is faster: up to x1.3 at Dexterity 100 and Path 100. Only both together give the big bonus (Dexterity 60 + Path 60 about x1.06, Dexterity 100 + Path 60 about x1.13).
- Dexterity counts as it is in the fight, after injuries and armour.
- The dual attack speed cap is 1.5 (the game's own is 1.2), blocks 4.0, but you still have to level into it. In flow the cap is 1.8.
- A dual fighter's Dexterity also counts **+12% in a fight**, in the cut damage and the speed formula, at any rank.

## Where you see it in game

hover **Dual Wielding** in the stats window:

- "In a fight now": the Dual Wielding that counts right now, with "-Overloaded" and "-Heavy armour" and their factors when they apply.
- "Path: Ronin (62)": your rank and Path, and what the next rank still needs, e.g. "40 Dual Wielding for Ronin".
- "Ranks": all five with what each opens. The ones you have are coloured like the game's item grades (a recolour mod changes them too), the rest are grey with what they need.
- "Dual speed bonus: x1.04": Nitoryu's own multiplier on attack and block speed.
- "Dexterity in a fight: +12%".
- "Pair": what your pair gives.

hover **Dexterity** while you're in a fight with the weapons drawn: "Attack speed" and "Block speed" show the final numbers.

when a character reaches a new rank it says the rank's name and the game shows a message.

## Pair Training Dummy

three buildings, mkI, mkII and mkIII, that train Dual Wielding up to 10 / 20 / 30. each has its own research (Pair Training Dummies, Better, Advanced; the Training category). the character needs two one-handed blades in the weapon slots, they can stay sheathed. the ranks from 3 up are still earned in fights.

## Left-hand fighting

in vanilla a character with a crippled right arm drops to martial arts. with Dual Wielding 50+ and a working left arm it keeps fighting with a one-handed katana or sabre in the **left hand**, also with the right arm cut off. it works with one weapon or a pair (one blade is used then).

- At Dual Wielding 50: 70% damage, 85% attack and block speed, -10 defence when blocking. The penalties fade to nothing at 100.
- Every move is mirrored, the crippled arm hangs down.
- It trains Dual Wielding.

## NPCs

- An NPC carrying an allowed pair fights with both blades if it has Dexterity 30, both weapon skills 30 and strength of at least half the pair's weight (`[NPC] dual=all`).
- NPCs don't go above rank 3 (`[NPC] max_tier`).
- `[NPC] dual=authored` leaves only the NPCs another mod opted in, `off` none.

## Other mods' animations

- The left arm mirrors the right frame by frame (guard, walking, running, strikes), so the dual stance takes on the style of your animation mods.
- Another mod's long move (attack distance 20+) is played with both blades by a fighter with the special moves. A fighter without them never plays a long move that needs both hands on one hilt, the game picks another.
- My own animations play over the game's tracks and give way at once to a swing, a block or a hit.

## My animations

all made for two blades, each can be turned off in `[Animations]` (then the mirrored game animation plays).

1. **Dual draw:** both blades over the shoulders in one motion, or the second one from the hip if it's worn there. `dual_draw`.
2. **Dual sheathe:** a flick, the right blade home first, then the left (to the back or the hip). Only the look takes a moment, the game sheathes at once, so first aid and carrying start as in vanilla. `dual_sheathe`.
3. **Blood flick:** a fighter who sheathes right after a won fight (its enemy is dead, down or knocked out) first flicks both blades down and out, pauses, then sheathes. `dual_chiburi`.
4. **A stance per rank:** in a fight, when the fighter isn't attacking, blocking or running, it holds the stance of its rank, no switching: Deshi hides behind the swords, Bushi holds a drilled guard, Ronin the upper guard, Kenshi keeps one blade hidden, Kensei rests a blade on the shoulder. Below rank 1 it's the game's guard. Walking up to the enemy keeps the stance's arms. `idle_stances`.
5. **Running with the blades:** both trailing behind, or the left one forward. `run_hold`.
6. **Main-hand blocks by direction:** the guard follows the blow. From the left or the right both blades together, leaning to that side (**angled block**), from above the right blade flat over the head (**overhead block**), from the front the **scissor X** or a one-blade block (the right blade high over the head, the left one low), at random. `angled_block`, `main_block`, `scissor_block`, `main_block_single`.
7. **A hit taken on the block:** when a blow lands on the blades, the guard gets pushed back once and comes back. `dual_block` for the X.
8. **Guard broken:** when a blow gets through a block, the blades get knocked apart, the body takes it and steps back. `guard_broken`.
9. **A hit that gets through:** if a blow lands while the fighter wasn't blocking, it rocks back once and keeps both blades. `hit_taken`.
10. **Second-blade parry:** a block with the left blade alone, the free blade low behind or held forward (`offhand_block_style`: mix, low, forward). `offhand_block`; with it off, the left blade is lifted over the head (`high_block`).
11. **Off-hand rising cut** and **twin sweep:** the two strikes with their own animations. `off_rising_cut`, `twin_sweep` (0 = the move is not used).
12. **Cross finisher**, **Dash slash** and **Spin:** whole-body animations for the three specials. `cross_finisher`, `dash_slash`, `spin`.

if the angled block leans away from the blow in your game, set `[Animations] angled_block_swap=1`.

## For mod authors: Nitoryu in FCS

a lot of you asked for levers in FCS, so most of Nitoryu is data. no C++ needed for any of this, just a mod loaded after Nitoryu.

1. **Balance.** About 100 settings sit on GLOBAL CONSTANTS as `nitoryu ...` fields: the Path weights, ranks, every curve, penalties, speed, parries, pressure, flow, Mugen, pair bonuses, NPC rules. Change a field in your mod and it applies to everyone who runs it. A player's own `Nitoryu.ini` still wins if they set the same thing there.
2. **Moves.** A dual-wield move is a COMBAT_TECHNIQUE record with a few `nitoryu` fields: what it is (off-hand strike, twin strike, finisher, a move from distance), cooldown, distance, which of my clips goes over it. My twelve moves are made exactly this way, so copy one and change it.
3. **Weapons.** `nitoryu dual wield` on a WEAPON lets it into a pair or keeps it out, whatever its category. `nitoryu off hand damage` changes its damage as the second blade, `nitoryu pair length` sets its role in a pair (long or short).
4. **NPCs.** Tick `nitoryu dual wielder` on your CHARACTER or FACTION and they fight with two blades when they carry a pair, with no stat minimums and no rank cap, even when the player has `[NPC] dual=authored`. The older ways still work: add them to the squad "Nitoryu dual wielders (opt-in list)" (it never spawns), or `[NPC] opt_in=<string id>,<string id>` in the ini.
5. **Races.** `nitoryu rig` on a RACE, if my mirroring looks wrong on your skeleton: 1 puts the second blade in a fixed left-hand grip, 2 turns Nitoryu off for that race.
6. **Training.** `nitoryu pair dummy` on a training BUILDING makes it train Dual Wielding up to its `output rate`.

to see the fields with tooltips in FCS you need FCS Extended (it loads `fcs.def` from the mod folder, the same way Second Life of Kenshi's own fields show up). players don't need it. every field with its description is in `fcs.def` next to this file; in the game data the skill is still called Ambidexterity and the Path "DP".

one thing isn't there yet: a track that animates both arms itself and plays as authored. for now a move is a right-hand track that I mirror or put on both blades.

## Settings

`Nitoryu.ini` in the mod folder, read when the game starts. it's short: the switches on top, then the balance keys with their defaults. the balance keys are shipped commented out (with a `;`), because they also live in the game data and a mod can change them. remove the `;` and your value wins. the ones you'll most likely want:

- `[General] language`: `auto`, `en`, `ru`, ...
- `[General] diag`: how much goes into `RE_Kenshi_log.txt`: `off`, `normal` (default), `stage`, `verbose`.
- `[UI] toggle_button`, `button_size`, `hotkey`.
- `[Stats] rank_colours`, `rank_up_message`.
- `[Animations]`: each of my animations on or off.
- `[NPC] dual` (`all` / `authored` / `off`) and `max_tier`.
- `[Weapons] categories`: what can pair.
- `[Guard] cooldown_ms` (one parry per game second) and `arc_deg` (110 = front and flanks).
- `[Moves] dash_cooldown_ms` (12000), `spin_cooldown_ms` (20000), `double_hit_cooldown_ms` (5000), `no_repeat`.
- `[Speed] dual_speed_bonus`, `dual_attack_cap`.
- `[Flow]`, `[Mugen]`, `[Pair]`, `[Scissor]` (the same body part), `[Split]` (two enemies), `[Training]`, `[LeftHand]`: each mechanic's switch and numbers.
- `[Balance]`: every curve by the Path.

## For plugin authors: what Nitoryu hooks

if your plugin hooks one of these too, both run (hooks chain), but tell me if something breaks. at start Nitoryu logs `shared_hook target=...` for every target someone hooked first, and after 60 s of play a `compat` line.

- `GameWorld::mainLoop_GPUSensitiveStuff` (per frame work)
- `CharStats::setWeapon`, `_calculateWeaponWeightSpeedMultiplier`, `calculateAttackOrBlockSpeed`, `getMeleeAttack`, `_calculateBlockChance`, `getEquippedWeaponSkill`, `chooseAttack` (the stance, speed, guard, move picks)
- `CharStats::serialise`, `updateStats`, `xpMelee`, `xpTraining`, `increaseStat` (Dual Wielding: save, load, XP)
- `Character::hitByMeleeAttack` (off-hand damage, the broken guard), `Character::~Character` (cleanup)
- `CombatClass::_iHitYouAreYouHit`, `initialiseBlock` (the second blade's parry)
- `CombatClassAI::whoAttacksYouOrMe` (pressure)
- `CombatTechniqueData::CombatTechniqueData(GameData*)` (knows our own moves)
- `MedicalSystem::addWound` (the same body part), `UseableStuff::operate` (the training dummy)
- `CharacterStatsWindow::update`, `InventoryGUI::refreshSection` (the stats line, the button)
- the melee task's weapon choice (a vtable slot, found by name: left-hand fighting)
- `Ogre::Skeleton::_updateTransforms` in OgreMain (the pose engine and the animations)

## Using the mod

you can use the mod in other modpacks and collections. the source code is open under GPL-3.0 (the mod is built on KenshiLib and RE_Kenshi, they're GPL too), so if you make your own version of it, keep it open under the same license and credit the original. for anything else, message me directly.

## What I'd love feedback on

1. How a dual fighter does against an opponent with one blade at around the same stats, and in group fights. Any notes on the balance are welcome.
2. The blocks: does the guard match the blow's side, do blocked hits and broken guards read right.
3. The stances: is there a stance a swing starts from with a weird jerk.
4. Anything that looks broken with your animation mods (a screenshot helps a lot).

## If something broke

1. **Right after the session**, before starting the game again (the log is rewritten on every start), take `Kenshi\RE_Kenshi_log.txt`.
2. Tell me what the bug is, how to reproduce it, which character and roughly when. Add your mod list and a screenshot or a short video if something looked wrong.
3. Post it in the Bug Reports discussion on the mod's Workshop page.

thanks!
