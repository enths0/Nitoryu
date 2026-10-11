# Nitoryu balance: how every number works

How Nitoryu's balance works: what each stat does, which moves open when, how often they happen, and what punishes a dual fighter.

**The numbers are settings.** The defaults are one table, `src/SettingsDef.h`, listed with their ranges in `docs/FCS-settings.md`. Each is also a field of the game record GLOBAL CONSTANTS (`nitoryu ...`), so a mod or a modpack can ship its own balance, and most have a key in `Nitoryu.ini`. Order: the default < the FCS value < an ini key that is present. Where a number below disagrees with the table, the table wins.

**Names.** Players see Dual Wielding, the Path and ranks. The settings, the log and the code say Ambidexterity (A), DP and tier. They are the same three things.

**The aim:** at effective stats around 80 a dual fighter is clearly better than an equal one-hander in a 1v1, and loses to two of them. Balance is on **effective** stats (after gear, weapon and injury modifiers): 75 is the ordinary strong fighter, 80 is reachable, beyond 80 is hard. So what changes how a dual fighter fights arrives by 75-80, not only at 90-100.

---

## 1. The short version

- **One number, the Path (DP, 0-100)**, measures how good a character is with two blades. With every input at X, the Path is X.
- **The Path decides everything:** the rank (which moves exist) and how often each move happens. Every chance grows in a straight line with it; nothing jumps at a rank line except that a move becomes available.
- **Dual Wielding (A)** is 60% of the Path, and it is the stat that punishes: overload and armour cut it. It is Nitoryu's own skill, limited like any skill by the game's level curve (mods that raise the stat limit raise it too). A character starts at 0.25 x its best allowed weapon skill, then it grows only in the dual stance. It is saved with the character as `NIT_ambidexterity`.
- **You see it in game:** the stats window shows Dual Wielding with its penalty in red; hovering it shows the value that counts in a fight, why it is lower, the Path with its rank, the speed factor, the pair's bonus and the five ranks. The attack speed on Dexterity's hover already includes the dual factor.
- **Weapons:** katanas and sabres by default (`[Weapons] categories`); a character's any-weapon switch adds blunt weapons and hackers.

## 2. What trains Dual Wielding

The base is half the weapon XP of the exchange (`ambidexterity xp share` 0.5), applied through the game's own level curve at Dual Wielding's level, so each level is slower than the last. The game's own factors are 1 for a hit, 0.4 for a blocked attack, 0.1 for a miss and 0.1 for a block. With `xp own level` on (the default) the amount follows Dual Wielding's own level against the opponent, as the game does for every skill, so it keeps growing after the weapon skill has outgrown the opponents.

| Deed | Factor | Setting (`[Training]`) |
|---|---|---|
| a parry with the second blade | x2 of a block | `xp_parry` |
| a hit that lands from a move of both blades (twin, Cross, Dash, spin) | x1.5 | `xp_twin` |
| the impacts of a riposte | x1.5 (a twin riposte: the higher of the two, not both) | `xp_riposte` |
| anything, while a second enemy's attack reached the fighter within 5 s | x1.5 more | `xp_outnumbered`, `xp_outnumbered_ms` |
| a hit taken | 0.25 of the Melee Defence XP of that hit | `xp_hit_taken` |

- A WEAPON's `nitoryu dual wielding xp` multiplies all of it for a pair that holds it (a training weapon).
- Weapon XP that another plugin gives outside the game's own melee XP call trains Dual Wielding by the same share.
- **The pair dummy:** Pair Training Dummy mkI / mkII / mkIII train Dual Wielding to 10 / 20 / 30 (the BUILDING's `output rate`), with the game's own training XP and level curve, for a character with an allowed pair in the weapon slots (`[Training] pair_dummy`). Ranks 3-5 are still earned in fights.

---

## 3. The Path

```
Path = 0.60 x effective Dual Wielding
     + 0.10 x lower weapon skill
     + 0.10 x Dexterity
     + 0.10 x melee attack
     + 0.10 x melee defence
```

- **All inputs are effective values,** the same ones the game's own combat formulas use: weapon skill and Dexterity with the injury and age modifiers; melee attack and defence with the gear and weapon bonuses and the indoor bonus when indoors; Dual Wielding after overload and armour (section 7).
- Each input counts up to 100. A katana + sabre pair uses the lower of the two skills.
- **Example:** weapon skill 70, Dexterity 60, attack 75, defence 80, Dual Wielding 70 gives 42 + 7 + 6 + 7.5 + 8 = **70.5**.
- The weights are settings (`nitoryu dp weight ...`); the hover text follows them.

### Ranks (the only steps; they open moves)

| Rank | Path from | Effective Dual Wielding from | Opens |
|---|---|---|---|
| 0 | 0 | - | nothing: only the learning penalties (section 6) |
| 1 | 20 | - | off-hand strikes |
| 2 | 40 | - | more strikes; the second blade's parry (combo hits, a second attacker) |
| 3 | 60 | 40 | twin strikes, counters, the parry in a 1v1, the back guard, pressure, the scissor; with effective Dual Wielding 50+ the special moves |
| 4 | 75 | 60 | flow, the spin, the second enemy |
| 5 | 90 | 80 | Mugen |

A fighter whose Path reaches a rank without the Dual Wielding it needs fights at the highest rank it has: the moves follow the capped rank, the chances (off-hand share, twin share, counter, parry, damage) still follow the Path. `nitoryu tier dp` and `nitoryu tier min ambidexterity` hold the two lists.

---

## 4. How often things happen (every effective stat = X, so the Path = X)

| Path | Rank | Off-hand share of attacks | of which twin | Off-hand damage | Parry base | Counter | Speed x |
|---|---|---|---|---|---|---|---|
| 20 | 1 | 20% | - | x1.04 | - | - | 0.95 |
| 30 | 1 | 22% | - | x1.09 | - | - | 1.00 |
| 40 | 2 | 23% | - | x1.14 | 20% | - | 1.01 |
| 50 | 2 | 25% | - | x1.19 | 25% | - | 1.02 |
| 60 | 3 | 26% | 25% | x1.25 | 30% | 30% | 1.06 |
| 70 | 3 | 29% | 30% | x1.30 | 37% | 38% | 1.10 |
| 75 | 4 | 30% | 33% | x1.30 | 40% | 41% | 1.12 |
| 80 | 4 | 35% | 35% | x1.30 | 43% | 45% | 1.15 |
| 85 | 4 | 40% | 38% | x1.30 | 47% | 49% | 1.19 |
| 90 | 5 | 50% | 40% | x1.30 | 50% | 53% | 1.22 |
| 100 | 5 | 55% | 45% | x1.30 | 55% | 60% | 1.30 |

Everything is linear between the rows (the curves' points are in `[Balance]`, section 11).

- **Off-hand share:** the chance that an attack uses the second blade (an off-hand strike or a twin strike). Every attack rolls it. Of the picks it does not take, `main move share` (0.6) is a right-blade strike with Nitoryu's own animation, the least played first; the rest are the game's swings, or, with only Nitoryu strikes on, another Nitoryu move.
- **Of which twin:** from rank 3, this part of the off-hand attacks hits with both blades in the same swing.
- **Off-hand damage:** how hard the second blade hits, against the same strike made with that weapon in the main hand (section 5).
- **Parry base:** from rank 2, when the main blade fails to stop a hit, the second blade parries with this chance x (the fighter's block chance against that attacker / the game's base block chance). A later cut of one attacker's combo gets half; so does the first hit of the enemy the main blade faces (from rank 3) and an attacker behind (the back guard, from rank 3). At most one off-hand parry per second of game time.
- **Counter:** from rank 3, after a successful block, this chance that the next attack within 1.5 s is a riposte.

---

## 5. Which weapon's numbers each strike uses

The game knows one weapon per character, so each hit is made with the main weapon's numbers and Nitoryu rewrites the hits of the second blade.

| Strike | Damage (cut, blunt) | Bleeding | Armour penetration | Bonus vs humans / animals / robots / races | Attack / defence / indoor bonus |
|---|---|---|---|---|---|
| **Main-hand strike** (right blade) | the main weapon, at its skill | the main weapon's | the main weapon's | the main weapon's | the main weapon's, full |
| **Off-hand strike, and the left blade of a twin** | the **second** weapon, at **its** skill, x the off-hand damage of section 4, x the weapon's own `nitoryu off hand damage` | the second weapon's | the second weapon's | the second weapon's | - |
| **Always, while dual** (not per strike) | - | - | - | - | **+ half** of the second weapon's (+4 attack becomes +2; -4 indoors becomes -2) |

- A dual fighter's Dexterity counts 12% more (`[Balance] dex_bonus`) in the game's two Dexterity formulas, the cut damage and the attack and block speed. Not in the Path or the stats window.
- A weapon's damage bonus against humans, animals, robots and its per-race `race damage` entries follow the blade that hits.
- Two weapon types in the pair (katana + sabre): the second weapon's defence bonus counts in full instead of half (section 9).

---

## 6. Learning: the Path from 0 to 40

Below 40 a dual fighter is worse with two blades than with one; the penalties fade in straight lines.

| Path | Clumsiness (melee attack and defence) | Divided guard (defence) | Speed x | Off-hand strikes |
|---|---|---|---|---|
| 0 | -10 | -4 | 0.85 | none (rank 0) |
| 10 | -7.5 | -4 | 0.90 | none |
| 20 | -5 | -4 | 0.95 | 20% (rank 1) |
| 30 | -2.5 | -4 | 1.00 | 22% |
| 40 | 0 | -2 | 1.01 | 23%, parries start (rank 2) |
| 60+ | 0 | 0 | section 4 | |

- **Clumsiness** = 10 - 0.25 x Path, gone at 40.
- **Divided guard** is by rank: 4 at ranks 0 and 1, 2 at rank 2, none from rank 3 (`[Balance] divided_guard`, six numbers).
- **Speed** below 30: 0.85 + 0.005 x Path.
- **Strength rule:** the pair weighs 1.25 x (both weights) in the game's weight formula, so a pair too heavy for the fighter's Strength is slow at any Path.

---

## 7. What punishes a dual fighter

The fight uses **effective Dual Wielding** = Dual Wielding x the overload factor x the armour factor. It feeds 60% of the Path, the rank gates, the special moves (50+) and left-hand fighting (50+). The saved value never changes: drop the weight and it is back.

### Overload

Overload = carried weight over capacity, the game's encumbrance.

| Overloaded by | Factor | 100 becomes | 70 becomes |
|---|---|---|---|
| 0% | 1.00 | 100 | 70 |
| 2% | 0.89 | 89 | 62 |
| 5% | 0.72 | 72 | 51 |
| 10% | 0.70 | 70 | 49 |
| 20% | 0.65 | 65 | 46 |
| 50% | 0.50 | 50 | 35 |
| 100% | 0.25 | 25 | 18 |
| 110%+ | 0.20 (floor) | 20 | 14 |

Linear in two pieces: up to 5% it drops fast (1 - 5.5 x overload, so any overload hurts at once), then slowly (0.75 - 0.5 x overload).

### Armour, per worn piece

Each worn piece cuts Dual Wielding by the share of its class, read from the ARMOUR record's `class`: **heavy 7%, medium 4%**, light and clothing 0. The cuts add up: the factor is 1 - the sum, never below 0.30.

| Worn | Factor | 80 becomes |
|---|---|---|
| nothing heavy or medium | 1.00 | 80 |
| 1 heavy | 0.93 | 74.4 |
| 2 heavy + 1 medium | 0.82 | 65.6 |
| 3 heavy + 1 medium | 0.75 | 60 |
| 4 heavy | 0.72 | 57.6 |

- The pieces are the armour items of the equipped inventory sections (any slot, belt included; not a backpack's contents).
- **Levers:** `nitoryu armour heavy cut` / `medium cut`, and per piece the ARMOUR field `nitoryu dual wielding cut` (-1 = by class, 0 = never cuts, else its own share).
- Both factors stack: 10% overloaded with 3 heavy + 1 medium gives 0.70 x 0.75 = 0.525.

---

## 8. Speed

- **The game:** `clamp((0.6 x Dexterity + 0.4 x weapon skill) / 100, 0.8, 1.2) x weight factor`. The weight factor is about 0.9 when Strength covers the weight, and includes the armour's combat speed.
- **Dual, from Path 30:** x (1 + 0.3 x dexN x dpN), with dexN = (Dexterity - 30) / 70 and dpN = (Path - 30) / 70, each 0-1. The 0.3 is `[Speed] dual_speed_bonus`; the cap is 1.5 (`dual_attack_cap`). A bonus never pushes past the cap; a result the game (or another mod) already made higher is kept.
- **Blocks:** the same factor multiplies the block speed, capped at `dual_block_cap` 4.0, the game's own. A master raises the guard faster with two blades; a beginner blocks slower.
- Only high Dexterity **and** a high Path give the big bonus: both 100 gives x1.30, both 60 x1.06, Dexterity 100 with Path 60 x1.13.
- The attack speed on Dexterity's hover is the game's own stored attack speed, which Nitoryu sets.

---

## 9. Moves

A move is a COMBAT_TECHNIQUE record with `nitoryu ...` fields, Nitoryu's or another mod's (`docs/FCS.md`). Its rank gate is the record's `min skill` (a Path), and every custom animation has its own switch in `[Animations]`.

### How a pick is made

| Rule | What it does |
|---|---|
| Only Nitoryu strikes (`[Main] only_my_strikes`, on) | every attack of a dual fighter is a Nitoryu move whose clip is switched on; vanilla swings and other mods' moves stay out. With nothing fitting out of reach, the fighter walks in; with nothing fitting in reach, the game's pick stays. Off: the game's own swings too, mirrored onto the left arm |
| Even picks (`[Moves] even_picks`, on) | of the moves of the wanted kind, the one played least wins, counted per rank as plays / the record's `nitoryu weight` (`nitoryu weight high` from rank 3). Weight 0.5 plays half as often, 0 never |
| Cooldowns | `nitoryu cooldown ms` on a record holds for any move, per fighter, in game time |
| Double-hit cooldown (`double_hit_cooldown_ms`, 5 s) | after any double hit (a twin, the Cross, the Dash, the spin, another mod's mirrored dash) the next one waits; meanwhile a twin roll is a single |
| No repeat (`no_repeat`, on) | the second blade avoids its last move |
| Carry-over (`offhand_carry`, on) | an off-hand roll that found nothing in reach waits up to 3 s for the next pick |
| Far picks (`far_pick_gap`, 35) | a pick made farther beyond reach than this is the game's alone; nothing is rolled on it |

### What opens when

| Move | Needs | How it is chosen |
|---|---|---|
| Off-hand cuts; the main hand's own strikes (chop, level cut, backhand) | rank 1 | off-hand share, single; the main hand's by `main_move_share` |
| Big chop | rank 1 | a right-blade strike for an enemy out of reach: three steps in; at most once per 7 s (`big_chop_cooldown_ms`) |
| Off-hand downward cut, double chop, leg cut, chest cuts; the twin body cut | rank 2 | off-hand share |
| Off-hand parry (the second blade) | rank 2 | section 4; only within `[Guard] arc_deg` (110) of the facing, from rank 3 also behind at half the chance; never while its fighter swings, staggers, is out of combat, down, ragdolled or carried |
| Off-hand combo; the twins (overhead, cross-slash, twin sweep, one-two, twin shear, figure eight, two step) | rank 3 | off-hand share x twin share; none while the double-hit cooldown runs |
| Head wrap, three cuts | rank 4 | as the twins |
| **Cross finisher** | rank 3, effective Dual Wielding 50+ | below |
| **Dash slash** | rank 3, effective Dual Wielding 50+ | below |
| **Spin** | rank 4, Path 75+ | below |
| Another mod's long move, both blades | rank 3, effective Dual Wielding 50+ | a non-Nitoryu technique with attack distance 20+ that the game picks is mirrored onto the left arm, and each impact is followed by the second blade's (`[Moves] mirror_long_dash`). It shares the Dash's cooldown. Below that level a long two-handed move of another mod is picked again |
| Pressure | rank 3 | below |
| Flow | rank 4 | below |
| Mugen | rank 5 | below |

### The special moves

**Cross finisher.** Two cuts, right blade then left, forming an X; each is its own hit, so it shows two damage numbers.
- Triggers: the target is staggering, or is under this fighter's full pressure, or the fighter blocked or parried within 1.5 s (a riposte; then only at `cross_riposte_chance` 0.25, otherwise an ordinary riposte). A trigger at a pick made out of reach waits 2 s for a pick in reach.
- Chance 60% when triggered, at most once per 3 s of game time per fighter.

**Dash slash.** A forward dash and a cut with both blades at once.
- The AI keeps a distance of its longest attack's reach and attacks from within it. A fighter who has the Dash gets that distance raised to 35 (never lowered), and an attack chosen 6-35 beyond reach is the Dash at 85%, at most once per 12 s.
- `[Moves] dash_slash=0` turns it off, and the far engage with it. It is rare in long fights by design: fighters stand close, and the Dash is for an enemy who is far away.

**Spin.** A rush, a sweep with both blades, a turn and a cross-split: two impacts of the right blade, each followed by the left blade's, so a clean spin lands four hits.
- For an enemy 6-25 beyond reach, before the Dash; at most once per 20 s and not within the double-hit wait. `[Moves] spin=0` turns it off.

The order of the Dash, the spin and any other opener is the `chance` field of their records: the higher goes first, and while it is ready the others wait.

### Pressure (rank 3)

- **How Kenshi decides who swings:** when two fighters are in range, the game runs a contest between their melee attack values; the loser blocks.
- **Stacks:** every hit of the fighter that an enemy blocks puts one stack on that enemy, up to 5. One stack fades per 3 s of game time without a new one; a hit the enemy lands on the fighter breaks all of it.
- **Effect:** in the contest between the two, each stack counts as +1 attack level for the fighter. The enemy blocks more and swings less; nobody else is affected.
- **Shown:** one diamond per stack, red at full pressure.
- Settings `[Pressure]`: `pressure`, `max_stacks`, `decay_ms`, `points`. Tempo, a standing initiative for a master, is off by default (`tempo`).

### Flow (rank 4)

- **Trigger:** every impact an enemy aims at the fighter counts, blocked, parried or landed. The 3rd within 5 s starts flow.
- **Effect, for 4 s of game time:** Dexterity counts 12% more in the cut damage (on top of section 5's), the attack speed is x1.04, and its cap is 0.3 higher. Blocks are unchanged.
- **Then:** 5 s during which attacks do not count (nor do they during flow). A knockdown ends flow at once.
- **Shown:** a wave, and a bar under it that runs out with the flow.
- Settings `[Flow]`: `flow`, `attacks`, `window_ms`, `duration_ms`, `cooldown_ms`, `dex_bonus`, `speed`, `cap_bonus`; `nitoryu flow min tier`.

### Mugen (rank 5)

- **The double-hit cooldown** shrinks by the Path: 3.5 s at 90 to 2 s at 100.
- **Chains:** when the left blade of a twin strike lands, a roll (25% at 90 to 50% at 100) makes the fighter's next attack, within 2.5 s, a twin again, whatever the cooldown. A chained twin that lands rolls again. Only twin strikes chain, and a blocked twin does not.
- **Shown:** a loop while a chain waits for its twin.
- Settings `[Mugen]`: `mugen`, `cooldown_ms`, `chain`; `nitoryu mugen min tier`, `nitoryu mugen chain window ms`.

### Two blades: the pair, the scissor, the second enemy

- **The pair.** Read from the two weapons' own `length` (a WEAPON's `nitoryu pair length` replaces it), whichever hand holds which:

  | Pair | Rule | Gives |
  |---|---|---|
  | long + short | the shorter is at most 0.80 of the longer | the second blade's parry chance x1.30 |
  | equal blades | the shorter is at least 0.90 of the longer | the twin share x1.30 |
  | two weapon types | the categories differ (katana + sabre) | the second weapon's defence bonus counts in full instead of half; a penalty keeps half |

  The first two exclude each other; the third adds to either. Pairs between the lines get nothing. The stats hover names the pair and its factor.
- **The scissor (rank 3).** The game picks a hit's body part by a weighted roll. When both blades of a twin strike land on one enemy, the later blade takes the earlier one's part: 80% at Path 60, always from 75. The damage per hit is unchanged; it lands in one place, so a limb goes sooner.
- **The second enemy (rank 4).** A swing hits the enemies inside a narrow box in front of the fighter. When a twin strike (or the Cross) is thrown and an enemy who attacked the fighter within 8 s stands in the front 180 degrees, within reach of the second blade and outside that box, the left blade lands on him instead; the main target takes the right blade only. One redirect per swing.
- Settings: `[Pair]` `pair`, `short_ratio`, `equal_ratio`, `long_short_parry`, `equal_twin_share`, `mixed_defence_share`; `[Scissor]` `scissor`, `chance`; `[Split]` `split`, `chance`, `arc_deg`, `reach`, `attacker_ms`; `nitoryu split min tier`, `nitoryu scissor window ms`.

### Left-hand fighting

A character whose right arm is crippled or missing fights with the left hand when its effective Dual Wielding is 50 or more (`[LeftHand] min_ambidexterity`) and it carries an allowed one-handed weapon. At 50 the damage is x0.7, the speed x0.85 and the block loses 10 points; all three are gone at 100.

---

## 10. Two-handed swings and the left blade

This is only about looks. Kenshi decides hits from the technique's impact points and the distance to the target, not from where the blade mesh passes, so a blade passing through a body never deals damage.

With only Nitoryu strikes off, some vanilla katana and sabre swings hold the hilt with both hands. The right arm plays that swing; the left arm, which holds the second blade, keeps its own guard pose instead of joining the grip, and for a moment the left blade can overlap the right arm or the chest.

---

## 11. `[Balance]`: the curves

`Nitoryu.ini [Balance]` (or the matching `nitoryu curve ...` field in FCS) replaces a curve. A curve is `Path:value,Path:value,...` in rising order: straight lines between the points, flat outside. A malformed value keeps the default and the load line says `(bad)`. The ini keys are shipped commented out; the log names every value that is not the default.

| Key | What |
|---|---|
| `off_hand_share` | share of picks that use the off hand |
| `twin_share` | of those, the share of twin strikes |
| `off_hand_damage` | off-hand strike damage x |
| `off_hand_guard` | the second blade's parry base chance |
| `counter_chance` | share of blocks answered by a riposte (`[Guard] counter_chance` wins when set) |
| `pressure_points` | attack levels per pressure stack (`[Pressure] points` wins when set) |
| `tempo` | a master's standing initiative, attack levels (off by default) |
| `divided_guard` | six numbers, ranks 0-5: attack levels an enemy gains against the dual fighter's block |
| `dex_bonus` | a dual fighter's Dexterity counts x(1 + this) in the cut damage and the speed formula |

## 12. NPCs

- `[NPC] dual = all` (the default): any NPC with an allowed pair that passes the gate (Dexterity 30+, both skills 30+, Strength at least half the pair's weight) fights dual; `authored`: only opted-in NPCs; `off`: none.
- `[NPC] max_tier = 3` (the default): NPCs get twins, counters, the parry, pressure and (with effective Dual Wielding 50+) the specials; flow, the spin, the second enemy and Mugen are for the player's characters and for NPCs a mod opts in.
- **An NPC's Dual Wielding:** the seed (a quarter of the best weapon skill) unless a value is set: `[NPC] skill=<string id>:<value>` (a CHARACTER or FACTION id), the field `nitoryu dual wielding`, then the lists of `Nitoryu.mod` (the opt-in squad and the four `Dual Wielding 40 / 60 / 80 / 100` squads). The character's own record goes before its faction's. The value is a floor on the stored stat: saved, raised on older saves, growing past it, no cap.
- **An NPC's Path:** computed from the stats unless a lowest Path is set: `[NPC] path=<string id>:<value>`, the field `nitoryu path`, then the second value of a `squad` / `squad2` reference in the same lists. It is a floor on the Path, never saved, and its rank stands above the Dual Wielding gate and `max_tier`. It does not opt the NPC in.
- **Opt-in** (for other mods' NPCs): string ids of CHARACTER or FACTION records in `[NPC] opt_in`, the field `nitoryu dual wielder`, or in plain FCS the SQUAD_TEMPLATE `Nitoryu dual wielders (opt-in list)`, which never spawns: a mod loaded after Nitoryu adds characters to its `squad` list and factions to its `faction` list. Opted-in NPCs skip the gate and the rank cap.
