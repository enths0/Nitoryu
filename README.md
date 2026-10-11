# Nitoryu: Dual Wielding Combat System

*Русская версия: `README_RU.md`. 中文: `README_ZH.md`. 日本語: `README_JA.md`.*

hi all! this is a mod for fighting with paired blades in Kenshi. a fighter carrying two blades draws both and fights with one in each hand: the main hand swings as usual, the second blade adds its own strikes, twin strikes, parries and a few special moves.

I play with it myself, full time, on a Second Life of Kenshi build + 200 mods on top. I'm not an animator, so go easy on the animations. there are 50+ of my own now (attacks, blocks, block impacts, runs, draws and sheathes, a combat stance per rank and the moves that come with skill), and in 1.1.0 I redid most of them, so they finally look like proper sword work. still far from perfect, but a level above what was there.

at the start you'll be weaker than with one blade. as you level, new strikes, blocks and moves open up, and at stats ~80 a dual fighter is 25-30% stronger than an equal opponent with one sword and holds off several enemies.

the mod is playable, but the work goes on: more moves, more animations, and I'll keep tuning the top ranks. by default a dual fighter uses only my strikes, so other animation mods don't mix into the pair (see "Other mods' animations"). how to send a bug is at the end.

## Requirements

- **RE_Kenshi 0.3.5 or newer** (it brings KenshiLib 0.5.0): [Nexus](https://www.nexusmods.com/kenshi/mods/847) or [GitHub](https://github.com/BFrizzleFoShizzle/RE_Kenshi).
- **Kenshi 1.0.65** (Steam tested, GOG should work). On other game versions the dual stance stays off (one line in the log), left-hand fighting still works.
- Don't use it together with DualWieldProbe: both change the same combat code. Nitoryu doesn't turn itself off anymore, it only leaves a line in the log, so what happens in fights is on you.
- **[Mod Config Menu](https://steamcommunity.com/sharedfiles/filedetails/?id=3813761584)** is optional, but highly recommended: every setting of Nitoryu in the game, switched in a click.

## Install

1. Subscribe on the Steam Workshop (or put the `Nitoryu` folder into `Kenshi\mods\`).
2. Enable **Nitoryu** and put it at the very end of your load order, after all your main mods. If `RE_Kenshi_log.txt` says another plugin hooks something on top of Nitoryu, move Nitoryu below that mod: some plugins (KEP's melee XP fix, for one) replace a game function without passing it on, and then Dual Wielding stops growing.
3. Start the game through RE_Kenshi as usual.

to remove it, just disable it. saves stay loadable, characters only lose their Dual Wielding number (it comes back if you enable the mod again). if you still have a test build of Nitoryu in `Kenshi\mods\`, delete that folder: only one copy runs.

## How to use it

- Put **two weapons on a character's back** (or one on the back, one on the hip) and draw. The drawn one goes to the right hand, the other to the left. A weapon that takes a full slot can be part of a pair too.
- **Katanas and sabres** pair, also one of each. Blunt and hackers can be switched on (`[Weapons] categories=0,1,2,4`), but I haven't tested them and they have no moves of their own. Heavy weapons, polearms and bows never pair. A weapon without a left-hand mesh (`bare sword`) can't be the second blade, that pair fights with one.
- **The button** on the back weapon slot in the inventory (crossed blades = on, one grey blade = off) turns the dual stance on and off for that character. The choice is saved. No hotkey by default, `[UI] hotkey=alt+n` adds one.
- **The second button** next to it (a blade crossed with a club) shows up on a character who carries two blunt weapons or hackers, or one of them with a katana or a sabre. Click it and this character fights with them as a pair, no ini editing. Saved with the character. Same as the `categories` switch: I haven't tested them and they have no moves of their own. `[UI] any_weapon_button=0` hides it.
- Hold the mouse over either button and it says what it does. Nothing beyond blunt and hackers is supported: heavy weapons, polearms and bows are two-handed. `[UI] button_tips=0` turns the text off.
- Everything else happens by itself in a fight: the fighter picks its strikes, parries and moves by its rank.
- **Settings in the game:** with [Mod Config Menu](https://steamcommunity.com/sharedfiles/filedetails/?id=3813761584) installed, the game's Options window has an MCM tab with two Nitoryu pages: "Nitoryu" (only Nitoryu strikes, the run, each of my animations, the inventory buttons, which NPCs fight with two blades, the speed caps, the left arm) and "Nitoryu: extra" (the marks and the stats window line). A hint on every row, in English, Russian, Chinese and Japanese. A change is written to `Nitoryu.ini`. Without the menu mod nothing changes, the ini works as before.
- **Marks above the head** show the rank skills at work, no text: a wave with a bar that runs out is flow, a loop is a Mugen chain waiting for its twin strike, diamonds over an enemy are pressure stacks (red at full pressure). They show for the characters you have selected. What each one means and gives is in the Dual Wielding description in the stats window, next to the Path's formula. `[Marks] marks=2` shows them for all your characters, `marks=0` turns them off.

## The skill: Dual Wielding and the Path

**Dual Wielding** is a new skill in the stats window (weapons column) for every character.

- It levels like every other skill, just by fighting, but only with two blades or left-handed: half of the weapon XP of each such hit also goes into it. It grows faster while it is below your weapon skill and slower once it is above.
- A new character starts at a quarter of its best katana or sabre skill.
- The first levels can be trained on the **Pair Training Dummy** (below).
- Armour and overload lower it in a fight, since the idea of dual wielding is an agile fighter: each heavy piece you wear takes 7%, each medium piece 4% (light armour and clothes nothing). The minus shows in red next to the skill, and the hover says why and how much.
- It is saved with your game.

**The Path** is your overall skill with two blades, a number from 0 to 100 built from five things: Dual Wielding (60%), the lower of your two weapon skills (10%), Dexterity (10%), melee attack (10%) and melee defence (10%). All of them at X give Path X. The stats count as they are in the fight, so wounds, armour and overload pull the Path down, and a rank can drop in the middle of a fight.

## The ranks

the Path opens five ranks. ranks 3-5 also need Dual Wielding itself.

| Rank | Path | Dual Wielding | What it gives |
|---|---|---|---|
| 1 Deshi | 20 | - | The second blade starts striking on its own, the first strikes with my animations for each hand, the big chop |
| 2 Bushi | 40 | - | The second blade parries hits the main one missed. The off-hand downward cut |
| 3 Ronin | 60 | 40 | Twin strikes, ripostes after a block, pressure, both blades into the same body part, the back guard, the special moves (X cut, Dash slash), other mods' long moves with both blades |
| 4 Kenshi | 75 | 60 | Flow, the Spin, two enemies at once |
| 5 Kensei | 90 | 80 | Mugen: moves with both blades come back much sooner, and a twin strike that lands can chain the next one |

every rank also has its own combat stance (see the animations). below rank 1 a fighter just holds two blades and pays the beginner's penalties.

### Rank 1, Deshi (Path 20)

- **Second-blade strikes.** The left blade strikes on its own: the off-hand cut, the **off-hand backhand**, the **off-hand belly cut**, the **off-hand shoulder cut** and the **off-hand high cut** (all my own animations, cut from the spot).
- **Main-hand strikes.** The right blade gets its own too: the **main-hand chop**, the **main-hand level cut** and the **main-hand backhand**.
- **The big chop.** A strike for an enemy out of reach: three steps in and the right blade comes down the diagonal from the neck to the hip.
- How often the second blade strikes: 20% of attacks at Path 20, 23% at 40, 26% at 60, 30% at 75, 40% at 85, 50% at 90, 55% at 100. The big chop comes at most once every 7 seconds; in between a fighter out of reach walks in.
- A second-blade strike does the second weapon's own damage, bleeding and armour penetration, x1.04 at Path 20, x1.14 at 40, x1.30 from 70.

### Rank 2, Bushi (Path 40)

- **Second-blade parry.** The second blade parries a hit the main blade missed: a hit from a second enemy (the full chance), or a later hit of one enemy's combo (half of it).
- The chance: 20% at Path 40, 30% at 60, 40% at 75, 50% at 90, 55% at 100.
- Only from the front and the flanks (110 degrees to each side), never while your fighter swings, staggers or is down, at most once per game second.
- From Ronin (rank 3) the second blade also tries a blow from behind, at half the chance: the back guard.
- New strikes: the **off-hand downward cut**, and five with my own animations: the **double chop**, the **leg cut**, the **dual cut** and a **chest cut** for each hand. And the **twin body cut**: one step in, both blades come in level from both sides, the left through the belly and the right through the chest.

### Rank 3, Ronin (Path 60, Dual Wielding 40)

- **Twin strikes.** Both blades in one swing, two wounds at once: the twin overhead cut, the cross slash and the **twin sweep** (my own animation). 25% of second-blade attacks at Path 60, 45% at 100. Three more twin strikes with my own animations: the **one-two**, the **twin shear** and the **figure eight**, and the **two step** for an enemy out of reach (the one-two walking in). A new second-blade strike too, the off-hand combo.
- **Ripostes.** After you block, your next attack within 1.5 s can be a riposte with the second blade or both: 30% at Path 60, 60% at 100.
- **The parry in a duel.** The second blade now also parries the first hit of the enemy you're facing, at half the chance.
- **Pressure.** Every hit of yours an enemy blocks puts a stack of pressure on them (up to 5, one fades every 3 s). Each stack counts against them when the game decides who swings next, so they block more and attack less. A hit they land on you clears it.
- **The same body part.** When both blades of a twin strike land on one enemy, the second goes for the body part the first one hit: 80% at Path 60, always from 75. Limbs come off faster.
- **Special moves** (they also need Dual Wielding 50):
  - **X cut** (was the Cross finisher): the right blade down, then the left, an X. It comes on a staggering enemy, on an enemy under full pressure (5 stacks), or, one time in four, within 1.5 s after your block or parry. At most once per 3 s.
  - **Dash slash:** a sprint from up to 35 units away, the blades cross in an X and split out on the hit. At most once per 12 s per fighter.
- **Other mods' long moves** (like issen from More Combat Animations) are played with both blades, and the second blade hits too. They share the Dash's 12 s cooldown. Only with `[Main] only_my_strikes=0`: by default a dual fighter uses only my strikes.
- Moves that hit with both blades (twin strikes, the X cut, the Dash) share a **5 s cooldown**, so double hits don't come back to back; the second blade strikes alone meanwhile. It also doesn't use the same move twice in a row.
- The beginner's split guard is gone from this rank.

### Rank 4, Kenshi (Path 75, Dual Wielding 60)

- **Flow.** After 3 enemy attacks aimed at your fighter within 5 s (blocked, parried or landed, all count) it concentrates for 4 s: Dexterity counts another +12% in the cut damage, the attack speed is x1.04 and its cap goes from 1.5 to 1.8. Then 5 s before attacks count again. A knockdown ends it.
- Two more twin strikes: the **head wrap** and the **three cuts**.
- **The Spin.** A rush, a sweep with both blades, a side step, a turn rising into an X over the head and a cross-split: both blades hit on the sweep and on the split, 4 hits in one move. From a distance (6-25 units beyond reach), at most once per 20 s. While the Spin is ready it goes before the Dash.
- **Two enemies, two blades.** When a second enemy who attacked you in the last 8 s stands at your flank, the left blade of a twin strike or of the X cut lands on him, and the right one stays on your target.

### Rank 5, Kensei (Path 90, Dual Wielding 80)

- **Mugen**, "endless". The 5 s cooldown on moves that hit with both blades shrinks to 3.5 s at Path 90 and down to 2 s at Path 100.
- A twin strike that lands can pull the next twin right after it, whatever the cooldown: a 25% chance at Path 90, 50% at 100. A chained twin can chain again.

## Learning is hard

below rank 3 two blades are worse than one. it all goes away as the Path grows:

- **Clumsiness:** -10 melee attack and defence at Path 0, gone at Path 40.
- **Split guard:** your block is weaker at ranks 0-1, half as much at rank 2, not at all from rank 3.
- **Speed:** attack and block speed x0.85 at Path 0, normal from Path 30.
- **Strength:** a pair counts as 1.25 x both weapons' weight, so two equal blades need 2.5x the strength of one.
- **Armour and overload** lower Dual Wielding in a fight (heavy -7% a piece, medium -4%), and with it the Path and the rank.

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

- "In a fight now": the Dual Wielding that counts right now, with "-Overloaded" and "-Armour" and their factors when they apply.
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
- Every move is mirrored, the crippled arm hangs down and swings as the body moves (`[Animations] limp_arm_swing=0` keeps it still).
- It trains Dual Wielding.

## NPCs

- An NPC carrying an allowed pair fights with both blades if it has Dexterity 30, both weapon skills 30 and strength of at least half the pair's weight (`[NPC] dual=all`).
- NPCs don't go above rank 3 (`[NPC] max_tier`).
- `[NPC] dual=authored` leaves only the NPCs another mod opted in, `off` none.
- An NPC starts with Dual Wielding at a quarter of its best weapon skill. To give one more, `[NPC] skill=<string id>:<value>` in the ini: the id of a CHARACTER or of a whole FACTION, comma separated. It starts at that value (one already in your save is raised to it) and grows from there.

## Other mods' animations

- **By default a dual fighter uses only my strikes** (1.1.0): no vanilla swings and no other mods' moves mixed into the pair. Out of reach it walks in instead of taking another mod's long dash. To get the old mix back: `[Main] only_my_strikes=0`, the first line of `Nitoryu.ini`, or the first row of the settings menu. Your animation mods still show in everything that isn't a strike: walking, the blocks the game plays, reactions.
- The left arm mirrors the right frame by frame (guard, walking, running, strikes), so the dual stance takes on the style of your animation mods.
- Another mod's long move (attack distance 20+) is played with both blades by a fighter with the special moves. A fighter without them never plays a long move that needs both hands on one hilt, the game picks another.
- My own animations play over the game's tracks and give way at once to a swing, a block or a hit. When a fighter is staggered, knocked down, knocked out or getting up, they step aside and the game's own animation plays (`[Animations] yield_to_reactions=0` brings the old way back).

## My animations

all made for two blades, each can be turned off in `[Animations]` (then the mirrored game animation plays).

1. **Dual draw:** the hands go up to the hilts behind the head, pull both blades and swing them out wide into the guard; the second one from the hip if it's worn there. `dual_draw`. The blades stay on the back until the hands take them (`dual_draw_hold`, 0 = in the hands from the first frame).
2. **Dual sheathe:** the body bows into the reach, the right blade home first, then the left (to the back or the hip, where the game wears them). Only the look takes a moment, the game sheathes at once, so first aid and carrying start as in vanilla. `dual_sheathe`.
3. **Blood flick:** a fighter who sheathes right after a won fight (its enemy is dead, down or knocked out) first flicks both blades down and out, pauses, then sheathes. `dual_chiburi`. Neither the flick nor the sheathe plays when the fighter goes straight on to first aid, a splint, looting or lifting someone: the blades just go home (`sheathe_yields_to_jobs=0` brings the old way back).
4. **A stance per rank:** in a fight, when the fighter isn't attacking, blocking or running, it holds the stance of its rank, no switching: Deshi holds one sword in a plain guard and forgets the other by the thigh, Bushi holds a drilled guard, Ronin keeps one blade hidden behind the hip, Kenshi aims a level blade from the temple, Kensei rests a blade on the shoulder. Below rank 1 it's the game's guard. Walking up to the enemy keeps the stance's arms. `idle_stances`.
5. **Running with the blades:** four runs, `run_style`: 3 (the default) the right blade reversed along the back and the left out to the right, 2 the right blade dragging behind and the left at the shoulder, 1 the left arm thrown back with its blade trailing low, 0 the old run (both blades trailing behind over the game's legs); 4 = 1 or 3, picked once per fighter. 1-3 lean deep and move the whole body, legs included. `run_hold` turns them all off. Over another mod's run they give way to 0, and the upper body only half-follows it (`run_hold_free_weight`, 0 to 1, default 0.5).
6. **Main-hand blocks by direction:** the guard follows the blow. From the left or the right both blades together, leaning to that side (**angled block**), from above the right blade flat over the head (**overhead block**), from the front the **scissor X** or a one-blade block (the right blade up across the front, the left one back at the hip), at random. `angled_block`, `main_block`, `scissor_block`, `main_block_single`.
7. **A hit taken on the block:** when a blow lands on the blades, the guard gets pushed back once and comes back. `dual_block` for the X.
8. **A hit that gets through:** if a blow lands while the fighter wasn't blocking, it rocks back once and keeps both blades. `hit_taken`.
9. **Second-blade parry:** a block with the left blade alone: it goes up across the front and the turn of the body throws the blow off past the shoulder, the right blade drawn back to the hip. `offhand_block`; with it off, the left blade is lifted over the head (`high_block`).
10. **Twin sweep**, **one-two**, **off-hand backhand**, **double chop**, **leg cut**, **dual cut**, **off-hand chest cut**, **main-hand chest cut**, **off-hand belly cut**, **main-hand chop**, **off-hand shoulder cut**, **main-hand level cut**, **off-hand high cut** and **main-hand backhand:** the fourteen strikes with their own animations. `twin_sweep`, `one_two`, `off_backhand`, `double_chop`, `off_leg_cut`, `dual_cut`, `off_hand_cut`, `main_hand_cut`, `off_belly_cut`, `main_chop`, `off_shoulder_cut`, `main_level_cut`, `off_high_cut`, `main_short_cut` (0 = the move is not used).
11. **Twin shear**, **figure eight** (Ronin), **head wrap** and **three cuts** (Kenshi): four twin strikes for the higher ranks. in the twin shear the blades cross going opposite ways, the right through the neck and the left through the thigh, the figure eight wheels each blade round into its own diagonal without a stop, the head wrap swings the right blade round behind the head and finishes in a long lunge, the three cuts are cut from the spot: the right blade rises from the left hip through the ribs, the left rises the mirror way under it, the right falls from the shoulder. `twin_shear`, `figure_eight`, `head_wrap`, `three_cuts`.
12. **Big chop** (Deshi), **twin body cut** (Bushi) and **two step** (Ronin): three strikes that step in. the big chop takes three steps and brings the right blade down the diagonal, the twin body cut takes one step and both blades come in level from both sides, the two step is the one-two walking in. the big chop and the two step are for an enemy out of reach. `big_chop`, `twin_body_cut`, `two_step`.
13. **X cut** (was the Cross finisher), **Dash slash** and **Spin:** whole-body animations for the three specials. `x_cut` (an old `cross_finisher=` still works), `dash_slash`, `spin`.
14. **Back guard** (Ronin and up): a blow from behind caught without turning round, the blade laid across the back, low behind the hip or high over the shoulder. the second blade does it when it parries an attacker behind you, the main blade when it blocked one. `back_guard`.

if the angled block leans away from the blow in your game, set `[Animations] angled_block_swap=1`.

## For mod authors: Nitoryu in FCS

a lot of you asked for levers in FCS, so most of Nitoryu is data. no C++ needed for any of this, just a mod loaded after Nitoryu.

1. **Balance.** About 100 settings sit on GLOBAL CONSTANTS as `nitoryu ...` fields: the Path weights, ranks, every curve, penalties, speed, parries, pressure, flow, Mugen, pair bonuses, NPC rules. Change a field in your mod and it applies to everyone who runs it. A player's own `Nitoryu.ini` still wins if they set the same thing there.
2. **Moves.** A dual-wield move is a COMBAT_TECHNIQUE record with a few `nitoryu` fields: what it is (off-hand strike, twin strike, finisher, a move from distance), cooldown, distance, which of my clips goes over it. My own moves are made exactly this way, so copy one and change it.
3. **Weapons.** `nitoryu dual wield` on a WEAPON lets it into a pair or keeps it out, whatever its category. `nitoryu off hand damage` changes its damage as the second blade, `nitoryu pair length` sets its role in a pair (long or short). Without FCS Extended: add the WEAPON to the `weapons` list of "Nitoryu weapons allowed in a pair" or "Nitoryu weapons refused in a pair" (two vendor lists no shop uses); players get `[Weapons] allow=` / `refuse=` in the ini. `nitoryu move weapons` on a move says which weapon categories it's for.
4. **NPCs.** Tick `nitoryu dual wielder` on your CHARACTER or FACTION and they fight with two blades when they carry a pair, with no stat minimums and no rank cap, even when the player has `[NPC] dual=authored`. The older ways still work: add them to the squad "Nitoryu dual wielders (opt-in list)" (it never spawns), or `[NPC] opt_in=<string id>,<string id>` in the ini. To set their Dual Wielding skill (the number in the stats window) in plain FCS, add the character or the faction to one of the squads "Nitoryu dual wielders (Dual Wielding 40 / 60 / 80 / 100)", or type an exact number as the character's first value there, so a dual-wield master doesn't need pumped stats. The field `nitoryu dual wielding` and `[NPC] skill=<string id>:<value>` in the ini do the same. For an NPC that has to be a master whatever its stats, set its lowest Path too: the second value of the character in those squads, the field `nitoryu path`, or `[NPC] path=<string id>:<value>`.
5. **Races.** `nitoryu rig` on a RACE, if my mirroring looks wrong on your skeleton: 1 puts the second blade in a fixed left-hand grip, 2 turns Nitoryu off for that race.
6. **Training.** `nitoryu pair dummy` on a training BUILDING makes it train Dual Wielding up to its `output rate`.
7. **Your own plugin.** if you write a RE_Kenshi plugin, there's a C++ API: ask whether a character fights with two blades, its rank, Path and Dual Wielding, get events (the second blade hit, a parry, a special, a rank change), and add your own pair rule (any two weapons, heavy weapons too, with your own numbers). the manual is `API.md` next to this file (in Russian: `API_RU.md`), the header is `NitoryuApi.h`, a sample addon is in the source repo.

to see the fields with tooltips in FCS you need FCS Extended (it loads `fcs.def` from the mod folder, the same way Second Life of Kenshi's own fields show up). players don't need it. the manual is `FCS.md` next to this file (how to add a move, every field, my own records, how to set your NPCs' Dual Wielding in plain FCS; in Russian: `FCS_RU.md`), the settings table is `FCS-settings.md`, and every field with its description is in `fcs.def`; in the game data the skill is still called Ambidexterity and the Path "DP".

one thing isn't there yet: a track that animates both arms itself and plays as authored. for now a move is a right-hand track that I mirror or put on both blades.

## Settings

`Nitoryu.ini` in the mod folder, read when the game starts (the settings menu writes to it too, see "How to use it"). it's short: the switches on top, then the balance keys with their defaults. the balance keys are shipped commented out (with a `;`), because they also live in the game data and a mod can change them. remove the `;` and your value wins. the ones you'll most likely want:

- `[Main] only_my_strikes` (1): a dual fighter uses only my strikes; 0 mixes the game's swings and other mods' moves back in.
- `[General] language`: `auto`, `en`, `ru`, `zh`, `ja`, ... (the Chinese texts are by 7 from the Workshop discussions, thanks! Japanese is a machine translation, corrections welcome)
- `[General] diag`: how much goes into `RE_Kenshi_log.txt`: `off`, `normal` (default), `stage`, `verbose`.
- `[UI] toggle_button`, `any_weapon_button`, `button_tips`, `button_size`, `hotkey`.
- `[Stats] rank_colours`, `rank_up_message`, `rank_in_title` (the rank in the stats window's title, off by default), `panel_pair_bonus` (the character panel's Attack and Defense count the second blade while the pair is drawn), `group` (where the Dual Wielding line sits: 1 Weapons / 2 Combat).
- `[Marks] marks` (0 / 1 selected / 2 mine / 3 everyone), `place` (2 heads / 1 portraits / 3 both), `flow`, `mugen`, `pressure`, `tips`, `size`, `height`.
- `[Animations]`: each of my animations on or off, and `run_style` (which run, above).
- `[NPC] dual` (`all` / `authored` / `off`), `max_tier`, `opt_in` and `skill`.
- `[Weapons] categories`: what can pair.
- `[Guard] cooldown_ms` (one parry per game second), `arc_deg` (110 = front and flanks), `back_min_tier` (3: the rank that guards the back, 6 = never) and `back_share` (0.5 of the parry chance there).
- `[Moves] dash_cooldown_ms` (12000), `spin_cooldown_ms` (20000), `double_hit_cooldown_ms` (5000), `no_repeat`, `even_picks` (1: of the moves that come up, the second blade takes the one it used least), `cross_riposte_chance` (0.25: how often the X cut answers a block), `big_chop_cooldown_ms` (7000), `main_move_share` (0.6: of the attacks the second blade doesn't take, how many are right-blade strikes with my animations).
- `[Speed] dual_speed_bonus`, `dual_attack_cap`.
- `[Flow]`, `[Mugen]`, `[Pair]`, `[Scissor]` (the same body part), `[Split]` (two enemies), `[Training]`, `[LeftHand]`: each mechanic's switch and numbers.
- `[Balance]`: every curve by the Path.

## For plugin authors: what Nitoryu hooks

the C++ API for your plugin is in `API.md`. if your plugin hooks one of these too, both run (hooks chain), but tell me if something breaks. at start Nitoryu logs `shared_hook target=...` for every target someone hooked first, and after 60 s of play a `compat` line.

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
