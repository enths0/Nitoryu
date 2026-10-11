# Nitoryu's settings in FCS

Generated from the plugin's settings table (`src/SettingsDef.h`), not edited by
hand. How to use these is in `FCS.md`.

All of them are fields of the CONSTANTS record **GLOBAL CONSTANTS** (`110-gamedata.quack`). Nitoryu.mod holds each at its
default; a mod loaded after Nitoryu overrides the ones it sets. The plugin reads them after every game load.
Order: the default below < the value in FCS < a key present in `Nitoryu.ini` (the player's own override).

Types: float, int, bool; **curve** = text `DP:value,DP:value,...` in rising DP (straight lines between the
points, flat outside); **list** = text `a,b,c`; **text**.

Range: a value outside it is clamped to it, and the log says so once (`setting_clamped`); for a curve it is
the range of the values, for a list of each number.

## Nitoryu 00 main

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu only my strikes` | bool | `1` |  | `[Main] only_my_strikes` | A fighter with two blades attacks only with Nitoryu's own strikes (each with its animation switch on): no vanilla swing, no other mod's move. Out of reach the fighter walks in; in reach with none of them fitting, the game's pick stays. Off = the game's swings and other mods' moves are mixed in, mirrored onto the left arm. |

## Nitoryu 01 dual proficiency

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu dp weight skill` | float | `0.10` | 0-1 |  | Dual proficiency (DP, 0-100) is a weighted average of five inputs, each capped at 100. This is the weight of the lower of the two weapon skills. The five weights should sum to 1. |
| `nitoryu dp weight dexterity` | float | `0.10` | 0-1 |  | Weight of Dexterity in dual proficiency. |
| `nitoryu dp weight attack` | float | `0.10` | 0-1 |  | Weight of Melee Attack in dual proficiency. |
| `nitoryu dp weight defence` | float | `0.10` | 0-1 |  | Weight of Melee Defence in dual proficiency. |
| `nitoryu dp weight ambidexterity` | float | `0.60` | 0-1 |  | Weight of effective Ambidexterity (after overload and armour) in dual proficiency. |
| `nitoryu tier dp` | list | `20,40,60,75,90` | 0 or more |  | Five numbers: the DP a fighter needs for tiers T1..T5. Moves and mechanics unlock per tier. |
| `nitoryu tier min ambidexterity` | list | `0,0,40,60,80` | 0 or more |  | Five numbers: the effective Ambidexterity a fighter also needs for tiers T1..T5. |
| `nitoryu ambidexterity seed` | float | `0.25` | 0 or more |  | A character with no saved Ambidexterity starts at this share of its best weapon skill. |
| `nitoryu ambidexterity xp share` | float | `0.5` | 0 or more |  | Share of the weapon XP of a dual or left-handed hit that also trains Ambidexterity. |
| `nitoryu pair weight mult` | float | `1.25` | 0 or more |  | Strength rule: a pair weighs this x (main weapon + second weapon) in the weight and speed formula. |

## Nitoryu 02 curves by DP

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu curve off hand share` | curve | `20:0.20,40:0.23,60:0.26,75:0.30,85:0.40,90:0.50,100:0.55` | 0-1 | `[Balance] off_hand_share` | Curve DP:value. Share of attacks that use the second blade (an off-hand strike or a twin strike). |
| `nitoryu curve twin share` | curve | `59.99:0,60:0.25,100:0.45` | 0-1 | `[Balance] twin_share` | Curve DP:value. Of the second blade's attacks, the share that are twin strikes (both blades). |
| `nitoryu curve off hand damage` | curve | `20:1.04,40:1.14,70:1.30` | 0 or more | `[Balance] off_hand_damage` | Curve DP:value. Damage of a second-blade strike, x the second weapon's own damage. |
| `nitoryu curve off hand guard` | curve | `39.99:0,40:0.20,60:0.30,75:0.40,90:0.50,100:0.55` | 0-1 | `[Balance] off_hand_guard` | Curve DP:value. Base chance that the second blade parries a hit the main blade missed. |
| `nitoryu curve counter chance` | curve | `59.99:0,60:0.30,100:0.60` | 0-1 | `[Balance] counter_chance` | Curve DP:value. Share of successful blocks answered by a riposte. |
| `nitoryu curve pressure points` | curve | `60:1,100:1` | 0 or more | `[Balance] pressure_points` | Curve DP:value. Attack levels each pressure stack counts for in the who-attacks-first contest. |
| `nitoryu curve tempo` | curve | `59.99:0,60:1.0,80:3.0,100:5.0` | 0 or more | `[Balance] tempo` | Curve DP:value. Attack levels of initiative a dual master always has in the who-attacks-first contest. |
| `nitoryu divided guard` | list | `4,4,2,0,0,0` | any | `[Balance] divided_guard` | Six numbers, tiers T0..T5: extra attack levels an enemy counts against a dual fighter's block. |
| `nitoryu dexterity bonus` | float | `0.12` | 0 or more | `[Balance] dex_bonus` | A dual fighter's Dexterity counts this much more in the cut damage and the attack and block speed (0.12 = +12 percent, 0 = off). |

## Nitoryu 03 penalties

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu clumsiness base` | float | `10` | 0 or more |  | Clumsiness: Melee Attack and Defence points lost at DP 0 (base - slope x DP, never below 0). |
| `nitoryu clumsiness slope` | float | `0.25` | 0 or more |  | Clumsiness points regained per point of DP. |
| `nitoryu overload floor` | float | `0.2` | 0-1 |  | Carrying over capacity lowers effective Ambidexterity; it never falls below this factor. |
| `nitoryu armour heavy cut` | float | `0.07` | 0-1 |  | Each worn armour piece of class heavy lowers effective Ambidexterity by this share (0.07 = 7 percent); the pieces add up. The field nitoryu dual wielding cut on an ARMOUR record overrides it for that piece. |
| `nitoryu armour medium cut` | float | `0.04` | 0-1 |  | Each worn armour piece of class medium lowers effective Ambidexterity by this share. Light armour and clothing cut nothing unless their record sets nitoryu dual wielding cut. |
| `nitoryu armour floor` | float | `0.3` | 0-1 |  | The armour factor on Ambidexterity (1 minus the worn pieces' cuts) never falls below this. |
| `nitoryu off weapon bonus share` | float | `0.5` | 0 or more |  | The second weapon adds this share of its own attack, defence and indoor bonuses. |

## Nitoryu 04 speed

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu speed at dp 0` | float | `0.85` | 0.05 or more |  | Attack and block speed factor of a beginner at DP 0; it rises to 1 at the full-speed DP. |
| `nitoryu speed full dp` | float | `30` | 0 or more |  | DP at which the beginner's speed penalty is gone. |
| `nitoryu speed bonus` | float | `0.3` | 0 or more | `[Speed] dual_speed_bonus` | Speed bonus of a master: x(1 + this) of the game's speed at Dexterity 100 and DP 100. |
| `nitoryu speed bonus from` | float | `30` | 0 or more |  | Dexterity and DP at which the speed bonus starts to grow. |
| `nitoryu attack speed cap` | float | `1.5` | 0.05 or more | `[Speed] dual_attack_cap` | A dual fighter's attack speed stops here (the game's own stops at 1.2). |
| `nitoryu block speed cap` | float | `4.0` | 0.05 or more | `[Speed] dual_block_cap` | A dual fighter's block speed stops here (the game's own cap is 4.0). |

## Nitoryu 05 guard and counter

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu guard` | bool | `1` |  | `[Guard] guard` | The second blade may parry a hit the main blade missed. |
| `nitoryu guard min tier` | int | `2` | 0-6 |  | Tier from which the second blade parries. |
| `nitoryu guard cooldown ms` | int | `1000` | 0 or more | `[Guard] cooldown_ms` | At most one parry by the second blade per this many milliseconds of game time. |
| `nitoryu guard arc deg` | int | `110` | 0-360 | `[Guard] arc_deg` | The second blade parries only attackers within this angle of the fighter's facing, either side. |
| `nitoryu guard share combo` | float | `0.5` | 0 or more |  | Share of the guard chance against a later hit of an attacker's combo. |
| `nitoryu guard share main` | float | `0.5` | 0 or more |  | Share of the guard chance against the first hit of the enemy the main blade faces. |
| `nitoryu guard main min tier` | int | `3` | 0-6 |  | Tier from which the second blade also parries the first hit of the enemy the main blade faces. |
| `nitoryu back guard min tier` | int | `3` | 0-6 | `[Guard] back_min_tier` | Tier from which a blade guards the back: the second blade may parry an attacker outside the guard arc, and a hit from behind that the main blade blocked shows the back guard. 6 = never. |
| `nitoryu back guard share` | float | `0.5` | 0 or more | `[Guard] back_share` | Share of the guard chance against an attacker behind the fighter (outside the guard arc). |
| `nitoryu counter` | bool | `1` |  | `[Guard] counter` | The first attack after a successful block may be an off-hand riposte. |
| `nitoryu counter min tier` | int | `3` | 0-6 |  | Tier from which ripostes happen. |
| `nitoryu counter window ms` | int | `1500` | 0 or more | `[Guard] counter_window_ms` | How long after the block the riposte may start, in milliseconds of game time. |
| `nitoryu counter chance fixed` | float | `-1` | -1-1 | `[Guard] counter_chance` | 0-1: one riposte chance for every DP instead of the curve. -1 = use the curve. |

## Nitoryu 06 pressure and tempo

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu pressure` | bool | `1` |  | `[Pressure] pressure` | Every hit of a master that the enemy blocks puts a stack of pressure on that enemy. |
| `nitoryu pressure min tier` | int | `3` | 0-6 |  | Tier from which a fighter builds pressure. |
| `nitoryu pressure max stacks` | int | `5` | 1-20 | `[Pressure] max_stacks` | Most stacks one target can carry (1-20). |
| `nitoryu pressure decay ms` | int | `3000` | 0 or more | `[Pressure] decay_ms` | One stack fades per this many milliseconds of game time without a new one. |
| `nitoryu pressure points fixed` | float | `-1` | -1 or more | `[Pressure] points` | Attack levels per stack for every DP instead of the curve. -1 = use the curve. |
| `nitoryu pressure shout stacks` | int | `3` | 0 or more |  | Stacks from which the target says it cannot find an opening (test builds only). |
| `nitoryu tempo` | bool | `0` |  | `[Pressure] tempo` | Off since 0.41.1 (flow replaced it). 1 = a dual master keeps some initiative of its own (the tempo curve). |

## Nitoryu 07 moves

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu special min ambidexterity` | float | `50` | 0 or more |  | Effective Ambidexterity needed for the special moves (with tier 3). |
| `nitoryu double hit cooldown ms` | int | `5000` | 0 or more | `[Moves] double_hit_cooldown_ms` | After a move that hits with both blades the next one waits this many milliseconds of game time. 0 = no cooldown. |
| `nitoryu no repeat` | bool | `1` |  | `[Moves] no_repeat` | The second blade does not use the same move twice in a row when another one fits. |
| `nitoryu even picks` | bool | `1` |  | `[Moves] even_picks` | Of the second blade's moves that come up for a pick, the fighter takes the one it has used least, so every move is seen about as often. Off: the first that comes up, by the game's own weights. |
| `nitoryu main move share` | float | `0.6` | 0-1 | `[Moves] main_move_share` | Of the picks the second blade does not take, this share 0-1 is a strike of the right blade with its own animation (a record whose use R arm is on and use L arm off), the least played first; the rest are the game swings. 0 = only the game swings. |
| `nitoryu cross riposte chance` | float | `0.25` | 0-1 | `[Moves] cross_riposte_chance` | Chance that the answer to a block or a parry is the X cut (the riposte finisher); otherwise it is an ordinary riposte. |
| `nitoryu off hand carry` | bool | `1` |  | `[Moves] offhand_carry` | A second-blade turn that found no move in reach waits for the next attack instead of being lost. |
| `nitoryu off hand carry ms` | int | `3000` | 0 or more |  | How long such a turn waits, in milliseconds of game time. |
| `nitoryu far pick gap` | float | `35` | 0 or more | `[Moves] far_pick_gap` | An attack picked from farther than this beyond the fighter's reach is left to the game. |
| `nitoryu mirror long dash` | bool | `1` |  | `[Moves] mirror_long_dash` | Another mod's long move is mirrored onto the left arm and the second blade hits too (fighters with the special moves). |
| `nitoryu cross riposte` | bool | `1` |  | `[Moves] cross_riposte` | The Cross also answers a block or a second-blade parry (the riposte finisher), besides a stagger and full pressure. |
| `nitoryu cross riposte window ms` | int | `1500` | 0 or more | `[Moves] cross_riposte_window_ms` | How long after a block or parry the riposte finisher may come, in milliseconds of game time. |
| `nitoryu cross arm ms` | int | `2000` | 0 or more | `[Moves] cross_arm_ms` | A Cross trigger that comes at a pick made too far away waits this long for the next pick in reach. 0 = no wait. |
| `nitoryu long dash shared cooldown` | bool | `1` |  | `[Moves] long_dash_shared_cooldown` | Every long dash of a dual fighter (the Dash slash or another mod's long move) shares the Dash slash's cooldown; the spin keeps its own. |
| `nitoryu long move min distance` | float | `20` | 0 or more |  | Attack distance from which another mod's move counts as a long move. |
| `nitoryu two handed repick over` | float | `25` | 0 or more |  | A fighter without the special moves picks again when another mod's two-handed move is longer than this. |

## Nitoryu 08 NPCs

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu npc dual` | text | `all` |  | `[NPC] dual` | Which NPCs fight with two weapons: all (anyone with an allowed pair and the stats), authored (only NPCs a mod opts in), off. |
| `nitoryu npc max tier` | int | `3` | 0-5 | `[NPC] max_tier` | The highest tier an NPC fights at (0-5). Opted-in NPCs are not capped. |
| `nitoryu npc min dexterity` | float | `30` | 0 or more |  | Dexterity an NPC needs to fight dual (opted-in NPCs skip it). |
| `nitoryu npc min skill` | float | `30` | 0 or more |  | Skill with both weapons an NPC needs to fight dual (opted-in NPCs skip it). |
| `nitoryu npc strength share` | float | `0.5` | 0 or more |  | An NPC needs Strength of at least this x the pair's weight (opted-in NPCs skip it). |

## Nitoryu 09 weapons

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu weapon categories` | list | `0,1` | 0 or more | `[Weapons] categories` | Weapon categories allowed in a pair: 0 katanas, 1 sabres, 2 blunt, 4 hackers. Heavy weapons, bows and polearms are always refused. |

## Nitoryu 10 left hand

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu left hand` | bool | `1` |  | `[LeftHand] enabled` | A character whose right arm is crippled keeps fighting with a one-handed weapon in the left hand. |
| `nitoryu left hand min ambidexterity` | float | `50` | 0 or more | `[LeftHand] min_ambidexterity` | Ambidexterity needed to fight left-handed. |
| `nitoryu left hand damage at min` | float | `0.7` | 0 or more | `[LeftHand] damage_at_min` | Damage factor at the minimum Ambidexterity; it fades to 1 at Ambidexterity 100. |
| `nitoryu left hand speed at min` | float | `0.85` | 0.05 or more | `[LeftHand] speed_at_min` | Attack and block speed factor at the minimum Ambidexterity; it fades to 1 at Ambidexterity 100. |
| `nitoryu left hand block penalty at min` | float | `10` | 0 or more | `[LeftHand] block_penalty_at_min` | Defence points lost when blocking at the minimum Ambidexterity; none at 100. |

## Nitoryu 11 flow and mugen

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu flow` | bool | `1` |  | `[Flow] flow` | Flow: a dual fighter who has been attacked several times in a row concentrates and fights faster and harder for a few seconds. |
| `nitoryu flow min tier` | int | `4` | 0-6 |  | Tier from which a fighter enters flow. |
| `nitoryu flow attacks` | int | `3` | 1-8 | `[Flow] attacks` | Enemy attacks aimed at the fighter (blocked, parried or landed) that start flow (1-8). |
| `nitoryu flow window ms` | int | `5000` | 0 or more | `[Flow] window_ms` | Those attacks must come within this many milliseconds of game time. |
| `nitoryu flow duration ms` | int | `4000` | 0 or more | `[Flow] duration_ms` | How long flow lasts, in milliseconds of game time. A knockdown ends it at once. |
| `nitoryu flow cooldown ms` | int | `5000` | 0 or more | `[Flow] cooldown_ms` | After flow ends, attacks are not counted for this many milliseconds of game time (none are counted during flow either). |
| `nitoryu flow dexterity bonus` | float | `0.12` | 0 or more | `[Flow] dex_bonus` | In flow the fighter's Dexterity counts this much more in the cut damage, on top of the dexterity bonus (0.30 = +30 percent). |
| `nitoryu flow speed` | float | `0.04` | 0 or more | `[Flow] speed` | In flow the attack speed is x(1 + this). |
| `nitoryu flow attack cap bonus` | float | `0.3` | 0 or more | `[Flow] cap_bonus` | In flow the attack speed cap is this much higher. |
| `nitoryu mugen` | bool | `1` |  | `[Mugen] mugen` | Mugen: a master's moves that hit with both blades come back sooner, and a twin strike that lands may chain another one. |
| `nitoryu mugen min tier` | int | `5` | 0-6 |  | Tier from which a fighter has Mugen. |
| `nitoryu curve mugen cooldown ms` | curve | `90:3500,100:2000` | 0 or more | `[Mugen] cooldown_ms` | Curve DP:value. The double hit cooldown of a fighter with Mugen, in milliseconds of game time (never longer than the plain one). |
| `nitoryu curve mugen chain` | curve | `90:0.25,100:0.5` | 0-1 | `[Mugen] chain` | Curve DP:value. Chance that a twin strike that lands makes the next attack a twin too, whatever the cooldown. |
| `nitoryu mugen chain window ms` | int | `2500` | 0 or more |  | How long a chained twin waits for the next attack, in milliseconds of game time. |

## Nitoryu 12 two blades

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu pair bonuses` | bool | `1` |  | `[Pair] pair` | What you pair matters: a long and a short blade parry better, two equal blades twin more, a pair of two weapon types takes the second weapon's defence bonus in full. |
| `nitoryu pair short ratio` | float | `0.80` | 0-1 | `[Pair] short_ratio` | Long + short: the shorter blade's length is at most this share of the longer one's (katana 21 + wakizashi 16 = 0.76). |
| `nitoryu pair equal ratio` | float | `0.90` | 0-1 | `[Pair] equal_ratio` | Equal blades: the shorter blade's length is at least this share of the longer one's. |
| `nitoryu pair long short parry` | float | `1.30` | 0 or more | `[Pair] long_short_parry` | Long + short: the second blade's parry chance x this. |
| `nitoryu pair equal twin share` | float | `1.30` | 0 or more | `[Pair] equal_twin_share` | Equal blades: the twin share x this. |
| `nitoryu pair mixed defence share` | float | `1.0` | 0 or more | `[Pair] mixed_defence_share` | Two weapon types in the pair (katana + sabre): the second weapon's defence bonus counts this share instead of the off weapon bonus share. A penalty keeps the usual share. |
| `nitoryu scissor` | bool | `1` |  | `[Scissor] scissor` | Scissor: the second blade of a twin strike may land on the same body part as the first. |
| `nitoryu curve scissor chance` | curve | `59.99:0,60:0.8,75:1.0` | 0-1 | `[Scissor] chance` | Curve DP:value. Chance that the second blade of a twin goes to the body part the first blade hit. |
| `nitoryu scissor window ms` | int | `1000` | 0 or more |  | The two blades of a twin count as one scissor within this many milliseconds of game time. |
| `nitoryu split` | bool | `1` |  | `[Split] split` | Two enemies, two blades: the left blade of a twin strike lands on a second enemy who is attacking the fighter from the flank, instead of the main target. |
| `nitoryu split min tier` | int | `4` | 0-6 |  | Tier from which the left blade of a twin may go to a second enemy. |
| `nitoryu curve split chance` | curve | `75:1.0,100:1.0` | 0-1 | `[Split] chance` | Curve DP:value. Chance that the left blade of a twin goes to a flanker in reach. |
| `nitoryu split arc deg` | int | `180` | 0-360 | `[Split] arc_deg` | The flanker must stand within this angle in front of the fighter (180 = the front half). |
| `nitoryu split reach` | float | `2.5` | 0 or more | `[Split] reach` | The flanker must be within this x the second blade's reach (half its length, as the game counts it) plus both bodies. |
| `nitoryu split attacker ms` | int | `8000` | 0 or more | `[Split] attacker_ms` | A flanker is an enemy who attacked the fighter within this many milliseconds of game time. |

## Nitoryu 13 training

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu pair dummy training` | bool | `1` |  | `[Training] pair_dummy` | A training building whose record is ticked nitoryu pair dummy trains Dual Wielding up to its output rate, for a character carrying an allowed pair. |
| `nitoryu dual wielding xp parry` | float | `2.0` | 0 or more | `[Training] xp_parry` | Dual Wielding XP by deed: a parry with the second blade trains Dual Wielding this many times more than a plain block (the game gives a block a tenth of a hit's weapon XP). 1 = as a block. |
| `nitoryu dual wielding xp own level` | bool | `1` | 0-1 | `[Training] xp_own_level` | 1 = a fight trains Dual Wielding by how its own level compares with the opponent (the game does that for every skill), so it keeps growing while it is below the weapon skill, also against opponents the weapon skill has outgrown. 0 = by the weapon skill's level (before 1.1: Dual Wielding stalled once the weapon skill outgrew the opponents). |
| `nitoryu dual wielding xp twin` | float | `1.5` | 0 or more | `[Training] xp_twin` | Dual Wielding XP by deed: a hit that lands from a move of both blades (a twin strike, a finisher, an opener) trains Dual Wielding this many times more. 1 = as any hit. |
| `nitoryu dual wielding xp riposte` | float | `1.5` | 0 or more | `[Training] xp_riposte` | Dual Wielding XP by deed: the impacts of a riposte (the attack that answers a block) train Dual Wielding this many times more. With a twin riposte the higher of the two factors counts. 1 = as any attack. |
| `nitoryu dual wielding xp outnumbered` | float | `1.5` | 0 or more | `[Training] xp_outnumbered` | Dual Wielding XP by deed: while two or more enemies attack the fighter, everything trains Dual Wielding this many times more (on top of the factors above). 1 = off. |
| `nitoryu dual wielding xp outnumbered ms` | int | `5000` | 0 or more | `[Training] xp_outnumbered_ms` | The fighter is outnumbered while a second enemy's attack reached it (blocked or landed) within this many milliseconds of game time. |
| `nitoryu dual wielding xp hit taken` | float | `0.25` | 0 or more | `[Training] xp_hit_taken` | A hit taken in the dual stance trains Dual Wielding: this share of the Melee Defence XP the game gives for that hit, which is higher the stronger the attacker is. 0 = off. |

## Nitoryu 14 other plugins

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu foreign control` | text | `auto` |  | `[Compat] foreign_control` | Another plugin that plays swings, blocks and weapon switches itself, outside the game's own combat (a direct-control mod): off = Nitoryu ignores it; auto = Nitoryu's animations give way only when one of the plugins below is loaded; on = always. |
| `nitoryu foreign control plugins` | text | `ShoulderCombat.dll` |  | `[Compat] plugins` | DLL file names, comma separated. With foreign control on auto, one of them loaded turns the giving way on. |
| `nitoryu foreign control hold ms` | int | `6000` | 0 or more | `[Compat] hold_ms` | After the other plugin's last swing or block, for this many milliseconds of game time the run animation waits for a steady run (run ms). A draw or a sheathe under its swing gets no animation, the blades follow at once. |
| `nitoryu foreign control run ms` | int | `300` | 0 or more | `[Compat] run_ms` | In that time the run animation starts only after this many milliseconds of running without a stop. |

## Nitoryu 15 marks

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu marks` | int | `1` | 0-3 | `[Marks] marks` | Marks that show the rank skills at work, without text. 0 = none, 1 = the characters the player has selected (and the enemies they press), 2 = all of the player's characters, 3 = every fighter. |
| `nitoryu marks place` | int | `2` | 1-3 | `[Marks] place` | Where the marks show: 2 = above the heads, 1 = on the squad portraits (all of the player's characters, whatever nitoryu marks says), 3 = both. |
| `nitoryu marks flow` | bool | `1` |  | `[Marks] flow` | The flow mark: a wave for a fighter in flow, with a bar under it that runs out with the flow. |
| `nitoryu marks mugen` | bool | `1` |  | `[Marks] mugen` | The Mugen mark: a loop for a fighter whose chained twin strike is waiting. |
| `nitoryu marks pressure` | bool | `1` |  | `[Marks] pressure` | The pressure mark: one pip per stack, on the portrait of the fighter who presses (the most stacks on one enemy) or above the pressured fighter; the pips change colour at full pressure. |
| `nitoryu marks size` | int | `22` | 8-64 | `[Marks] size` | Above the heads: side of a mark in pixels (8-64). A pressure pip is half of it. On a portrait the marks follow the portrait's size. |
| `nitoryu marks height` | float | `24` | any | `[Marks] height` | Above the heads: how far above a character's feet the marks sit, in game units, when the character's name is not shown (or on name is 0). |
| `nitoryu marks on name` | bool | `1` |  | `[Marks] on_name` | Above the heads: the marks sit right on top of the game's name label over the character when it is shown; 0 = always at height. |
| `nitoryu marks tips` | bool | `0` |  | `[Marks] tips` | The mouse over a mark shows the skill's name and what it does. |

## Nitoryu 16 stats window

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu stats line group` | int | `1` | 1-2 | `[Stats] group` | Where the Dual Wielding line sits in the character's stats window: 1 = at the end of the Weapons group; 2 = in the Combat group, after its last skill (the Ranged group moves down a row). |

## Nitoryu 17 animations

| FCS field | Type | Default | Range | Nitoryu.ini | What it does |
|---|---|---|---|---|---|
| `nitoryu run hold free weight` | float | `0.5` | 0-1 | `[Animations] run_hold_free_weight` | The two-blade run draws the arms and the torso over the legs of the run that is playing. When it cannot keep in step with that run (another mod's run animation), the torso takes only this share of the pose, 0-1: 0 = the torso is left to the other run, 1 = fully ours. The arms always take it in full. |
| `nitoryu run style` | int | `3` | 0-4 | `[Animations] run_style` | Which run a dual fighter shows (while [Animations] run_hold is on): 0 = both blades trailing over the game legs, 1 = stance 1 (the left arm thrown back, its blade trailing low), 2 = stance 2 (the right blade dragging behind, the left at the shoulder), 3 = stance 3 (the right blade reversed along the back, the left out to the right), 4 = stance 1 or 3, drawn once per fighter. Stances 1-3 move the whole body, legs included, in step with the game run; over another mod run they give way to 0. |
| `nitoryu yield to reactions` | bool | `1` |  | `[Animations] yield_to_reactions` | While the game plays a stagger, a knockdown, a knockout or a get-up on a dual fighter, the dual pose steps aside and that animation shows, the game's own or another mod's. Off: the dual pose stays on the upper body through it. |
