# Nitoryu for mod authors: what is in FCS

*Русская версия: `FCS_RU.md`.*

most of Nitoryu is data, so a mod loaded after it can change the balance, add moves, let a weapon into a pair or keep it out, and make its own NPCs fight with two blades. no C++ for any of that. this is the long version of the README's "For mod authors" part.

in the game data things keep their first names: the Path is "dual proficiency" (DP), Dual Wielding is "Ambidexterity" and a rank is a "tier". below I write DP and rank.

next to this file: `fcs.def` (every field with its description) and `FCS-settings.md` (the table of all the settings).

## Seeing Nitoryu's fields in FCS

plain FCS doesn't read a mod's own `fcs.def`, so it won't show the `nitoryu ...` fields. **FCS Extended** does: a patched FCS launcher (BFrizzleFoShizzle, Nexus mod 1825) that loads an `fcs.def` from every mod folder. unzip it into the Kenshi folder and start `FCS_extended.exe`, the fields then show with their sections and tooltips, the same way Second Life of Kenshi's own fields do. players need none of this: the game stores whatever a `.mod` holds, and the plugin reads it. two things work in plain FCS too: an NPC's Dual Wielding and the NPC opt-in go through list records in `Nitoryu.mod` ("An NPC's Dual Wielding" below).

## Settings: the balance

122 settings are fields of the CONSTANTS record **GLOBAL CONSTANTS** (FCS: Game settings), all named `nitoryu ...`, in sixteen sections ("Nitoryu 01 dual proficiency" to "Nitoryu 16 stats window"). the full table with types and defaults is `FCS-settings.md`.

- **To change one for your mod or modpack:** in a mod loaded after Nitoryu, open GLOBAL CONSTANTS and edit the field. Only the fields you change are stored in your mod, the rest keep following Nitoryu.
- **Order:** the default, then the FCS value (the last mod in the load order wins, field by field), then a key that is present in a player's `Nitoryu.ini`. The shipped ini has every such key commented out, so your values reach players unless they chose their own.
- **When it is read:** after the game data loads, and again after each load of a save. No restart is needed beyond the one FCS changes always need.
- **Curves** are text: `DP:value,DP:value,...` in rising DP, straight lines between the points, flat outside (`20:0.03,40:0.10,...`). **Lists** are text: `a,b,c`. A malformed curve keeps the default and the load line says `(bad)`.
- **Check it:** `RE_Kenshi_log.txt` has `setting key='...' value='...' source='fcs'` for every value that is not the default, and one `settings total=122 record_found=1 from_fcs=N from_ini=N` line.
- **Direct-control mods (section 14):** a plugin that swings, blocks and switches weapons itself (ShoulderCombat) plays outside the game's own combat, and my animations used to stay on top of it. With `nitoryu foreign control` on `auto` they give way when one of the DLLs in `nitoryu foreign control plugins` is loaded (`on` = always, `off` = never). If you make such a plugin, add your DLL's file name to that field in your mod. The log has `foreign_config mode='auto' active=1 plugin='...'` and a `foreign_action` line per swing it caught.
- **Marks (section 15):** flow, Mugen and pressure show as small marks above the head, no text: a wave with a bar that runs out (flow), a loop (a Mugen chain is waiting), diamonds over the pressured enemy (one per stack, red at full pressure). `nitoryu marks`: 0 = none, 1 = the characters the player has selected and the enemies they press (the default), 2 = all of the player's characters, 3 = every fighter (for a mod whose NPCs reach rank 4-5). `nitoryu marks place`: 2 = above the heads, 1 = on the squad portraits, 3 = both. `nitoryu marks flow`, `mugen`, `pressure` switch each one, `nitoryu marks tips` a text under the mouse (off), `nitoryu marks size` and `height` place them; `nitoryu marks on name` (on) puts them right on top of the character's name label when the game shows it, `height` is used when it does not. What the marks mean is in the Dual Wielding description in the stats window (`stats.mark_*` in `lang/`), with the Path's formula built from the five `nitoryu dp weight ...` fields. The pictures are `Nitoryu_mark_*.png` in the mod folder; a mod loaded after Nitoryu can ship its own files with the same names. The player's side is `[Marks]` in the ini.
- **Stats window (section 16):** `nitoryu stats line group` says where the Dual Wielding line sits: 1 = at the end of the Weapons group (the default), 2 = in the Combat group, after its last skill (the Ranged group moves down a row). The player's side is `[Stats] group` in the ini.
- **Dual Wielding XP (section 13):** the skill takes half of the weapon XP of every exchange, and what the fighter did changes it. `nitoryu dual wielding xp parry` (2.0): a parry with the second blade, times a plain block. `xp twin` (1.5): a hit that lands from a move of both blades. `xp riposte` (1.5): the impacts of a riposte (with a twin riposte the higher of the two counts). `xp outnumbered` (1.5): everything, while a second enemy's attack reached the fighter within `xp outnumbered ms` (5000). `xp hit taken` (0.25): a hit taken trains the skill with this share of the Melee Defence XP the game gives for it, so a stronger attacker teaches more. 1 (or 0 for the last one) turns each off. The player's side is `[Training]` in the ini; the log's `ambi` lines have `deed` and `deed_mult`.

what the numbers do is in the README. the Cross, Dash and spin numbers are not settings, they sit on the moves' own records (below).

## Add your own dual-wield move

a move for dual wielders is one COMBAT_TECHNIQUE record. my own twelve are made the same way, so the easiest start is to copy one of them in FCS into your mod and change it.

**1. Put it in the private pool.** Turn every weapon flag off (`katanas`, `sabre`, `blunt`, `hackers`, `heavy weapons`, `polearm`, `unarmed`) and set `animal = ATTACK_NULL`. The game then never picks it by itself, Nitoryu draws from that pool for a dual fighter. A record that sets `nitoryu move` but keeps a weapon flag is ignored (the log's `techniques` line counts it as `ignored`).

**2. Say what it is: `nitoryu move`.**

| Value | Kind | When it is used |
|---|---|---|
| 0 | Not a Nitoryu move | Never |
| 1 | Strike | The second blade's turn (the off-hand share of attacks, by DP) |
| 2 | Twin strike | The same turn, when it rolls a twin (from rank 3) |
| 3 | Finisher | A special: the enemy staggers, or is under the fighter's full pressure, or the fighter blocked or parried within the last 1.5 s |
| 4 | Opener | A special: the enemy is at a distance (`nitoryu min gap` to `nitoryu max gap` beyond reach) |

Specials need rank 3 and Ambidexterity 50 as it counts in the fight (the setting `nitoryu special min ambidexterity`). My spin also needs rank 4.

**3. Fields the game already has, and what they mean here.**

- `min skill`: the **DP** the fighter needs (it is not a weapon skill here). `max skill`: leave 999.
- `chance`: the weight in the pool. Among finishers, and among openers, the record with the **higher `chance` goes first**. While it is ready, the ones with a lower `chance` wait (equal: each is tried in turn).
- `anim name`: the track. Any one-handed right-hand sword track the fighter's skeleton has: vanilla's, or one your mod adds the usual way (an ANIMATION_FILE linked to the races). Nitoryu mirrors it. A rig without the track skips the move.
- `limb 1` / `limb 2` with `num techniques`: which blade each impact is. **0 = left arm = the second blade** (its damage comes from the second weapon, scaled by DP), **1 = right arm = the main blade**. A twin is one right-arm and one left-arm impact a moment apart (`anim blocked frame 1` / `2`).
- `attack distance`, `anim speed mult`, `power 1` / `2`, the frames: as in any technique.

**4. Nitoryu's own fields** (section "Nitoryu" of the record, shown with tooltips by FCS Extended):

| Field | Type | Default | What it does |
|---|---|---|---|
| `nitoryu move` | int | 0 | The kind, above |
| `nitoryu arms` | int | 0 | How the track is played. 0 = by the kind: a strike is mirrored to the left hand (the whole body mirrors, the right blade keeps its guard), a twin, finisher or opener plays on both blades (the left arm mirrors the right). 1 = mirrored to the left hand. 2 = both blades. 3 = main hand only (the left arm keeps its guard) |
| `nitoryu clip` | text | empty | One of my own animations laid over the track: `cross_finisher`, `dash_slash`, `spin`, `off_rising_cut`, `twin_sweep`, `one_two`, `off_backhand`, `beat_cut`, `double_chop`. Each was made on one base track (downward combo, flyknee, flyknee, chop left, ma chudan, downward combo, chop left, ma chudan, ma chudan) and only fits that track |
| `nitoryu needs clip` | bool | False | True = the move is not used while that clip is switched off in a player's `Nitoryu.ini [Animations]` |
| `nitoryu echo` | bool | False | True = every impact is followed by a hit of the second blade (two impacts = four hits, like the spin). Leave it off when the record already has a left-arm impact |
| `nitoryu cooldown ms` | int | 0 | Finisher / opener: once per this many milliseconds of game time, per fighter |
| `nitoryu roll` | float | 1.0 | Finisher / opener: the chance 0-1 that it is used when everything else fits |
| `nitoryu min gap` | float | 0 | Opener: the enemy is at least this far beyond the fighter's reach |
| `nitoryu max gap` | float | 0 | Opener: and at most this far. While the move is ready the fighter engages from this distance |
| `nitoryu timing from` | text | empty | The string id of the record it was cloned from: its speed and hit timing follow that record as loaded, so an animation pack that retimes the original carries over |

**5. Changing my moves.** In a mod loaded after Nitoryu, open one of the twelve records and edit: the Dash's cooldown (`nitoryu cooldown ms`), the spin's skill (`min skill`), which opener goes first (`chance`), or `nitoryu move = 0` / `disabled` to take a move out. A player's `Nitoryu.ini [Moves]` can still override the cooldown of the Dash and the spin and the spin's skill for their own game.

**6. Check it.** The log's load line: `techniques total=N ours=12 third_party=K from_fields=12+K specials=... ignored=0`, and one `move_added record='...' kind='...' arms='...' ...` line per move of another mod. In a fight, `offhand_pick` and `special_pick` lines name the track.

**An example**, one record and nothing else in the mod. "Sample twin heavy cut": a twin strike on the vanilla track `heavy downcut`, which Nitoryu itself doesn't use. Clone vanilla's "heavy downcut" and set:

- Every weapon flag off, `animal = ATTACK_NULL`, `1 handed` on, `use L arm` and `use R arm` on
- `num techniques = 2`, `limb 1 = 1` (the main blade), `limb 2 = 0` (the second blade)
- `anim blocked frame 1 = 0.673`, `anim blocked frame 2 = 0.693` (the second blade 0.02 later), `power 1` and `power 2` = 80
- `min skill = 60` (rank 3, where twin strikes start), `max skill = 999`, `chance = 2.0`
- `nitoryu move = 2`

the load line then says `third_party=1`, and a dual fighter at DP 60+ uses the heavy overhead cut with both blades now and then, two hits in one swing.

**Not possible yet:** a track that animates both arms itself, played as authored (no mirroring), and clips of your own. Both need work in the plugin's pose engine, it is on my list.

## Weapons, characters, factions, races, buildings

fields on records of your own, or on vanilla ones overridden in your mod. all are read when they are needed, so a change shows after the next game start.

| Record | Field | Type | Default | What it does |
|---|---|---|---|---|
| WEAPON | `nitoryu dual wield` | int | 0 | This weapon in a two-weapon pair. 0 = by its category (the setting `nitoryu weapon categories`: katanas and sabres as shipped). 1 = allowed whatever its category. 2 = refused: a pair with it fights one-handed (log reason `weapon_refused`). Two-handed-only weapons are always refused, and the second weapon still needs a `bare sword` mesh. Left-hand fighting follows the same field |
| WEAPON | `nitoryu off hand damage` | float | 1.0 | A multiplier on this weapon's damage when it strikes as the second blade, on top of the off-hand damage curve (counter `offhand_weapon_factor`) |
| WEAPON | `nitoryu dual wielding xp` | float | 1.0 | Multiplier on the Dual Wielding XP a fighter gains while this weapon is in the pair; both weapons' values multiply. Above 1 makes a training weapon (wooden blades at 2.0 and almost no damage, for example), below 1 a weapon that teaches little (the log's `ambi` line has `weapon_mult`) |
| WEAPON | `nitoryu pair length` | float | 0.0 | The blade length the pair rule reads instead of the weapon's own `length` (0 = its own). The shorter blade at most 0.80 of the longer = long + short (better parries). At least 0.90 = equal blades (more twin strikes). Set it when a weapon's `length` doesn't say what it is, e.g. 16 on a short blade with a long reach |
| BUILDING | `nitoryu pair dummy` | bool | False | True = this training building (its functionality is BF_TRAINING) trains Dual Wielding instead of the functionality's `stat used`, up to the building's `output rate`, for a character carrying an allowed pair (counters `dummy_trained`, `dummy_no_pair`, `dummy_at_cap`). The `stat used` still picks the race's XP multiplier |
| CHARACTER, FACTION | `nitoryu dual wielder` | bool | False | True = this character, or every member of this faction, fights dual with an allowed pair: no stat minimums, no rank cap, and also when players set NPC dual wielding to `authored` (counter `npc_opt_in_by_field`). The same effect as the opt-in squad below, use whichever is easier |
| CHARACTER, FACTION | `nitoryu dual wielding` | float | -1.0 | This character's, or every member of this faction's, Dual Wielding skill (the number in the stats window). -1 = not set: it starts at a quarter of the best weapon skill. The character starts at the value, one already in a save is raised to it, and it grows from there like anyone's. The CHARACTER's own value wins over its FACTION's (counter `ambi_authored_field`, log lines `ambi_authored`) |
| RACE | `nitoryu rig` | int | 0 | How Nitoryu treats this race's skeleton. 0 = automatic: the second blade mirrors the first when the rig allows it. 1 = no mirroring: the second blade sits in a fixed left-hand grip (as for a rig the mirror can't build), and there is no left-hand fighting. 2 = no Nitoryu for this race (reason `race_refused`) |
| CHARACTER, FACTION | `nitoryu path` | float | -1.0 | This character's, or every member of this faction's, lowest Path (0-100, the number in the Dual Wielding hover). -1 = not set: the Path comes from the stats. With a value the Path is never below it, and the character holds the rank of that Path (20 / 40 / 60 / 75 / 90 as shipped) whatever its Dual Wielding and `[NPC] max_tier` say. Stats above the value still count. The CHARACTER's own value wins over its FACTION's (counter `path_authored_field`, log lines `path_authored`) |
| STATS | `NIT_ambidexterity` | float | -1.0 | A character's starting Dual Wielding, 0-100. -1 = not set: it starts at a quarter of the best weapon skill |

### An NPC's Dual Wielding and Path

an NPC starts with Dual Wielding at a quarter of its best weapon skill, and the ranks need it (40 for rank 3, 60 for rank 4, 80 for rank 5), so a faction leader with 90 in katanas still fights below his level. you can give your NPCs more **in plain FCS, no FCS Extended needed**.

`Nitoryu.mod` has five SQUAD_TEMPLATE records that nothing references, so they never spawn (search the squads for "Nitoryu"): **Nitoryu dual wielders (opt-in list)** and **Nitoryu dual wielders (Dual Wielding 40)**, **60**, **80**, **100**. in your mod, loaded after Nitoryu:

- **A whole faction at 40, 60, 80 or 100:** open the list with that number and add your FACTION to its `faction` list. Everyone in the faction gets that Dual Wielding.
- **A character at 40, 60, 80 or 100:** the same, add your CHARACTER to `squad`.
- **A character at an exact number:** add the CHARACTER to `squad` of any of the five lists and type the number into its first value. FCS puts 1 there, and 1 means "no number of its own"; 2 or more is the character's Dual Wielding.
- **A character's Path:** the same entry in `squad` (or `squad2`), the **second value**. FCS puts 0 there; 2 or more is the lowest Path the character fights at (below).

everyone in these lists is also opted in: with an allowed pair they fight dual with no stat minimums and no rank cap (below).

#### Where to click in plain FCS

the window of a CHARACTER or a FACTION in plain FCS has no Nitoryu fields, don't look for them there. the numbers go into my five squads:

1. Open **your own mod** in FCS as the active one, with Nitoryu ticked in the list of mods to load. Don't edit `Nitoryu.mod` itself, the next update of the mod overwrites it.
2. In the tree on the left go to **Faction > Squads**. The five lists are at the top (or type "Nitoryu" into the search).
3. Double-click the one you need, e.g. **Nitoryu dual wielders (Dual Wielding 80)**.
4. In its window use the dropdown at the top right, next to the **Add** button: pick **`squad`** for a character or **`faction`** for a faction, press **Add** and choose your record.
5. The record shows up on the right side of the window. A character in `squad` has two numbers next to its name, FCS puts `1` and `0` there. Click a number to change it:

| The two numbers | What the character gets |
|---|---|
| `1` `0` (as FCS adds it) | The list's Dual Wielding (80 in this list), the Path from its own stats |
| `95` `0` | Dual Wielding 95 |
| `1` `85` | The list's Dual Wielding, and a Path never below 85 (rank 4) |
| `95` `90` | Dual Wielding 95 and a Path never below 90 (rank 5) |

6. Save your mod and put it after Nitoryu in the load order. The lists are read when a game loads, so load a save to see it.

a FACTION in `faction` has no numbers next to it: everyone in it gets the list's Dual Wielding (40, 60, 80 or 100). a Path for a whole faction needs the field or the ini (below). in **Nitoryu dual wielders (opt-in list)** `1` `0` only opts the character in, the numbers work the same. `squad2` works like `squad`; `leader` and `choosefrom list` show one number, the Dual Wielding.

an example, the Temple Inquisitor as a master of two blades: open **Nitoryu dual wielders (Dual Wielding 100)**, pick `squad`, Add, Temple Inquisitor, change `1` `0` to `1` `90`. he still needs two katanas or sabres in his weapon slots to fight with two.

#### With FCS Extended

FCS Extended reads `fcs.def` from the mod folder, so the CHARACTER and FACTION windows get a section **Nitoryu** with three fields: `nitoryu dual wielder` (opt in), `nitoryu dual wielding` and `nitoryu path` (-1 = not set). set them right on your record.

#### Without FCS, in the ini

`Nitoryu.ini` in the mod folder, section `[NPC]`. the id is the grey **String ID** line at the top of the record's window in FCS, e.g. the Temple Inquisitor is `1219-gamedata.base` and the faction Bounty Hunters is `14508-gamedata.base`:

```
[NPC]
opt_in=1219-gamedata.base,14508-gamedata.base
skill=1219-gamedata.base:95,14508-gamedata.base:80
path=1219-gamedata.base:90
```

the other ways in short:

- **The field `nitoryu dual wielding`** on a CHARACTER or FACTION record (the table above). It shows in FCS only with FCS Extended.
- **The ini,** for players: `[NPC] skill=<string id>:<value>,<string id>:<value>` in `Nitoryu.ini`, ids of CHARACTER or FACTION records.

**the Path** is what the ranks and the moves read, and it is counted from the weapon skill, Dexterity, attack, defence and Dual Wielding together. so Dual Wielding 100 alone doesn't make a master out of a weak fighter. when your NPC has to be a master whatever its stats are, give it a lowest Path: the second value in the lists (above), the field `nitoryu path` on a CHARACTER or FACTION (FCS Extended), or `[NPC] path=<string id>:<value>` in the ini. the same order applies (own record, then the faction; ini, field, lists). the NPC's Path is then never below that number, and it holds the rank of that number (20 rank 1, 40 rank 2, 60 rank 3, 75 rank 4, 90 rank 5) even with a low Dual Wielding and above `[NPC] max_tier`. its damage still comes from its real weapon skill, so set both numbers and decent stats for a boss. a Path set by the field or the ini doesn't opt the NPC in: tick `nitoryu dual wielder` too, or use the lists, which do. the log has one `path_authored` line per record (who, from where, the value, what it was) and the counters `path_authored_list`, `path_authored_field`, `path_authored_ini`; `npc_opt_in` counts the records with a Path (`with_path`).

when several of them name the same NPC: its own record wins over its faction's, and for one record the ini wins, then the field, then the lists (a record in two lists takes the higher number).

the value is where the character starts, not a lock: it grows past it in fights, and a character already in a save is raised to it on the next load. the Path still counts the other stats, so Dual Wielding 100 on a weak fighter doesn't make a master. the log has an `ambi_authored` line per character it raised (who, from where, the value) and the counters `ambi_authored_list`, `ambi_authored_field`, `ambi_authored_ini`; the `npc_opt_in` line counts the records with a number (`with_skill`). STATS `NIT_ambidexterity` stays for one hand-made stats record.

for tests there is `[Test] npc_ambidexterity` in the ini (not in the shipped file, add the section yourself): `skill` gives every NPC a Dual Wielding equal to its best weapon skill, a number gives everyone that number. it is never saved and the load line in the log says it is on. `[Test] ambidexterity` does the same for the player's characters.

the vanilla races (humans, Shek, Hive, Skeletons) share one skeleton per sex and all work with the mirror. `nitoryu rig` is for a race with a skeleton of its own.

## The pair dummies

`Nitoryu.mod` holds three BUILDING records, `Pair Training Dummy mkI / mkII / mkIII`: clones of the vanilla Training Dummies (mesh, use nodes, functionality, build cost) with `nitoryu pair dummy` ticked, `output rate` 10 / 20 / 30, their own `building group` and their own `upgrades to` chain. three RESEARCH records open them (`Pair Training Dummies`, `Better Pair Training Dummies`, `Advanced Pair Training Dummies`, category Training): clones of the vanilla dummy researches, each enabling its dummy and requiring the one before.

to make your own, tick the field on any training building and set its `output rate`. to change the caps, override `output rate` on mine in a mod loaded after Nitoryu.

## In `Nitoryu.mod`: 12 COMBAT_TECHNIQUE records and the NPC lists

every record has all weapon flags off and `animal = ATTACK_NULL` (21), a category no weapon uses, so the game never picks them alone and FCS filters by weapon type hide them (search the combat technique list for "Nitoryu"). the plugin draws from them by setting the fighter's weapon type to 21 for one pick, with DP standing in for the skill, so `min skill` is the DP gate. each is a clone of a vanilla record playing a **vanilla track** with my values (arms, impacts, directions, gates) and its `nitoryu ...` fields.

| String id | Name | What it is (track) | min skill (DP) | Cloned from |
|---|---|---|---|---|
| `1-Nitoryu.mod` | Nitoryu off-hand cut | Rank 1 off-hand cut to the right (chop left, mirrored) | 20 | 40-gamedata.quack |
| `4-Nitoryu.mod` | Nitoryu off-hand cut static | The same against a static target (chop left, mirrored) | 20 | 196-gamedata.base |
| `13-Nitoryu.mod` | Nitoryu off-hand rising cut | Rank 1 rising cut with the left blade (chop left, mirrored, with my full-body clip, speed 1.1) | 20 | 40-gamedata.quack |
| `2-Nitoryu.mod` | Nitoryu off-hand downward cut | Rank 2 off-hand downward cut (chop down, mirrored) | 40 | 38-gamedata.quack |
| `5-Nitoryu.mod` | Nitoryu off-hand downward cut static | The same against a static target (chop down static, mirrored) | 40 | 128-gamedata.quack |
| `3-Nitoryu.mod` | Nitoryu off-hand combo | Rank 3, two cuts, right then down (downward combo, mirrored) | 60 | 45-gamedata.quack |
| `6-Nitoryu.mod` | Nitoryu twin overhead | Rank 3, both blades cut down at once (chop down, twin) | 60 | 38-gamedata.quack |
| `7-Nitoryu.mod` | Nitoryu cross slash | Rank 3, the right blade cuts left, the left blade cuts right (chop left, twin) | 60 | 40-gamedata.quack |
| `16-Nitoryu.mod` | Nitoryu twin sweep | Rank 3, both blades in one diagonal cut (ma chudan, twin, with my full-body clip, speed 1.2) | 60 | 38-gamedata.quack |
| `9-Nitoryu.mod` | Nitoryu cross finisher | Finisher: an X with both blades (downward combo at speed 1.5, with my clip). Roll 0.6, once per 3 s | 60 | 45-gamedata.quack |
| `10-Nitoryu.mod` | Nitoryu dash slash | Opener: a long dash, both blades on arrival (flyknee, with my clip). 6 to 35 beyond reach, roll 0.85, once per 12 s | 60 | 46052-rebirth.mod |
| `12-Nitoryu.mod` | Nitoryu spin | Opener: a rush, the sweep, a turn, the cross-split (flyknee, with my clip). Two impacts of the right blade, the left blade's is added to each (4 hits). 6 to 25 beyond reach, once per 20 s, rank 4 | 75 | 46052-rebirth.mod |

### Which opener goes first: the `chance` field

the spin and the Dash slash both open from a distance. the plugin reads the field **`chance`** of the two records as their order:

- The record with the **higher `chance` goes first** (shipped: spin 4, Dash 3). While that move is ready (its switch on, the fighter's skill, the left blade drawn, off its cooldown), the other one is not played, and the fighter engages from the first move's distance (spin: 25 beyond reach, Dash: 35). The other move plays while the first is on cooldown.
- **Equal** values: both are tried on every pick, the one with the smaller `nitoryu max gap` first (the spin).
- To put the Dash first, give `Nitoryu dash slash` the higher `chance` in your own mod loaded after Nitoryu.

the same rule holds for any number of openers and of finishers, another mod's included. `chance` is still the game's own weight for the record inside the private pool, a change of 1-2 there doesn't show. log counters: `special_dash_waits_for_spin`, `special_spin_waits_for_dash`.

### The NPC opt-in list (`11-Nitoryu.mod`)

a SQUAD_TEMPLATE named **Nitoryu dual wielders (opt-in list)** that no town or faction references, so it never spawns. it is the older way to make your own NPCs fight with two blades (the field `nitoryu dual wielder` does the same):

1. In your mod (loaded after Nitoryu), open that squad and add your CHARACTER records to **`squad`** (or `squad2`, `leader`, `choosefrom list`) and your FACTION records to **`faction`**. A character's first value of 2 or more is its Dual Wielding ("An NPC's Dual Wielding and Path" above), its second value of 2 or more its Path; leave the 1 FCS puts there to only opt it in.
2. Give those characters an allowed pair (two katanas or sabres on the back, or back + hip) with a `bare sword` mesh.

the four lists `23-Nitoryu.mod` to `26-Nitoryu.mod` (**Dual Wielding 40 / 60 / 80 / 100**) work the same way and also give that number. the plugin reads the lists after every game load. an NPC whose own record, or whose faction's record, is listed fights dual under `[NPC] dual=all` and `authored`, with no stat minimums and no `[NPC] max_tier` cap. players can do the same without FCS: `[NPC] opt_in=<string id>,<string id>` in `Nitoryu.ini`. the log's `npc_opt_in` line says how many ids were read, and `npc_record` lines show the record id a spawned NPC carries.

## What the plugin does with them (not FCS)

- **Mirrored:** the whole body is mirrored at run time, so a right-hand vanilla swing becomes a left-hand one.
- **Twin:** the right arm plays the vanilla track and the left arm mirrors it in the same frame, both blades.
- **My clip:** the specials, the rising cut and the twin sweep also get my own full-body animation, laid over the vanilla track and following its progress (the track keeps the ground travel and the timing).
- My own tracks (39: the blocks, the hits taken, the draws and sheathes, the run holds, a stance per rank, the nine attack clips) are **not FCS records**. They live in `character\meshes\male_skeleton\Nitoryu_L3_male.skeleton` and `character\meshes\female_skeleton\Nitoryu_L3_female.skeleton` in the mod folder, and the mod has a normal ANIMATION_FILE record for the two files (**Nitoryu animations**, `28-Nitoryu.mod`, `preprocess` on), so the game loads them at start-up like any animation mod's. No race lists that record and no ANIMATION or COMBAT_TECHNIQUE names the tracks: the plugin takes the file from Ogre's skeleton manager by its name and decides what plays and when (it also loads the file itself if the game hasn't). They play on any race whose skeleton has the vanilla human bone layout. Other rigs keep the game's pose (logged `l3_track_bad_bones`), and a rig the mirror can't build gets a fixed left-hand grip.

## FCS fields the plugin reads at run time

the game's own: WEAPON `bare sword` (the left-hand blade, a pair without it fights one-handed), `sheath`, `length`, `armour penetration`, `human damage mult`, `animal damage mult`, `robot damage mult`. COMBAT_TECHNIQUE `min skill`, `chance`, `1 handed`. CONSTANTS `GLOBAL CONSTANTS`: `base block chance`, `cut damage 1` / `99`, `blunt damage 1` / `99`, `damage multiplier`. SQUAD_TEMPLATE `11-Nitoryu.mod` and `23-` to `26-Nitoryu.mod`: their reference lists (the NPC opt-in and Dual Wielding, above).

Nitoryu's own, declared in `fcs.def`: CONSTANTS `nitoryu ...` (122 settings). COMBAT_TECHNIQUE `nitoryu move`, `arms`, `clip`, `needs clip`, `echo`, `cooldown ms`, `roll`, `min gap`, `max gap`, `timing from`. WEAPON `nitoryu dual wield`, `nitoryu off hand damage`, `nitoryu pair length`, `nitoryu dual wielding xp`. BUILDING `nitoryu pair dummy`. CHARACTER and FACTION `nitoryu dual wielder`, `nitoryu dual wielding`, `nitoryu path`. RACE `nitoryu rig`. STATS `NIT_ambidexterity`.
