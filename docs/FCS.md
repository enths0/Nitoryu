# What Nitoryu adds to FCS, and what lives elsewhere

For anyone working on Nitoryu's data side (the FCS collaborator). Source of truth:
`data/spec.json` (string ids stay stable in `data/spec.ids.json`).
Dependencies: `gamedata.base`, `rebirth.mod`.

## Seeing Nitoryu's own fields in FCS

Nitoryu declares its own fields in `mods\Nitoryu\fcs.def` (the game's own `fcs.def` grammar). Plain FCS does not read
that file. **FCS Extended** does: a patched FCS launcher (BFrizzleFoShizzle, Nexus mod 1825) that loads an `fcs.def`
from every mod folder. Unzip it into the Kenshi folder and start `FCS_extended.exe`; the Nitoryu fields then show with
their sections and tooltips, next to the ones Second Life of Kenshi adds the same way. Players need none of this: the
game stores, and the plugin reads, whatever a `.mod` holds.

## Settings: the balance in FCS (0.32)

69 settings are fields of the CONSTANTS record **GLOBAL CONSTANTS** (FCS: Game settings), all named `nitoryu ...`, in
ten sections ("Nitoryu 01 dual proficiency" ... "Nitoryu 10 left hand"). The full table with types and defaults is
generated: **`FCS-settings.md`**.

- **To change one for your mod or modpack:** in a mod loaded after Nitoryu, open GLOBAL CONSTANTS and edit the field.
  Only the fields you change are stored in your mod; the rest keep following Nitoryu.
- **Order:** the default < the FCS value (the last mod in the load order wins, field by field) < a key present in a
  player's `Nitoryu.ini`. The shipped ini has every such key commented out, so your values reach players unless they
  chose their own.
- **When it is read:** after the game data loads (and again after each load). No restart is needed beyond the one FCS
  changes always need.
- **Curves** are text: `DP:value,DP:value,...` in rising DP, straight lines between the points, flat outside
  (`20:0.03,40:0.10,...`). **Lists** are text: `a,b,c`. A malformed curve keeps the default and the load line says
  `(bad)`.
- **Check it:** the log has `setting key='...' value='...' source='fcs'` for every value that is not the default, and
  one `settings total=69 record_found=1 from_fcs=N from_ini=N` line.

What the numbers mean and why they are what they are: `BALANCE.md`. The Cross, Dash and spin numbers are not settings:
they are on the moves' own records (below).

## Add your own dual-wield move (0.33)

A move for dual wielders is one COMBAT_TECHNIQUE record. Nitoryu's own twelve are made the same way, so the easiest
start is to copy one of them in FCS (below) into your mod and change it.

**1. Put it in the private pool.** Turn every weapon flag off (`katanas`, `sabre`, `blunt`, `hackers`,
`heavy weapons`, `polearm`, `unarmed`) and set `animal = ATTACK_NULL`. The game then never picks it by itself;
Nitoryu draws from that pool for a dual fighter. A record that sets `nitoryu move` but keeps a weapon flag is ignored
(the log's `techniques` line counts it as `ignored`).

**2. Say what it is: `nitoryu move`.**

| Value | Kind | When it is used |
|---|---|---|
| 0 | not a Nitoryu move | never |
| 1 | strike | the second blade's turn (the off-hand share of attacks, by dual proficiency) |
| 2 | twin strike | the same turn, when it rolls a twin (from tier 3) |
| 3 | finisher | a special: the enemy staggers, or is under the fighter's full pressure |
| 4 | opener | a special: the enemy is at a distance (`nitoryu min gap` .. `nitoryu max gap` beyond reach) |

Specials need tier 3 and effective Ambidexterity 50 (the spin: tier 4, 0.36) (the setting `nitoryu special min ambidexterity`).

**3. Fields the game already has, and what they mean here.**

- `min skill`: the **dual proficiency** the fighter needs (not a weapon skill). `max skill`: leave 999.
- `chance`: the weight in the pool. Among finishers, and among openers, the record with the **higher `chance` goes
  first**; while it is ready, the ones with a lower `chance` wait (equal: each is tried in turn).
- `anim name`: the track. Any one-handed right-hand sword track the fighter's skeleton has: vanilla's, or one your
  mod adds the usual way (an ANIMATION_FILE linked to the races). Nitoryu mirrors it; a rig without the track skips
  the move.
- `limb 1` / `limb 2` with `num techniques`: which blade each impact is. **0 = left arm = the second blade** (its
  damage comes from the second weapon, scaled by dual proficiency), **1 = right arm = the main blade**. A twin is
  one right-arm and one left-arm impact a moment apart (`anim blocked frame 1` / `2`).
- `attack distance`, `anim speed mult`, `power 1` / `2`, the frames: as in any technique.

**4. Nitoryu's own fields** (section "Nitoryu" of the record; shown with tooltips by FCS Extended):

| Field | Type | Default | What it does |
|---|---|---|---|
| `nitoryu move` | int | 0 | the kind, above |
| `nitoryu arms` | int | 0 | how the track is played. 0 = by the kind: a strike is mirrored to the left hand (the whole body mirrors, the right blade keeps its guard); a twin, finisher or opener plays on both blades (the left arm mirrors the right). 1 = mirrored to the left hand. 2 = both blades. 3 = main hand only (the left arm keeps its guard) |
| `nitoryu clip` | text | empty | one of Nitoryu's own animations laid over the track: `cross_finisher`, `dash_slash`, `spin`, `off_rising_cut`, `twin_sweep`. Each was authored on one base track (downward combo, flyknee, flyknee, chop left, ma chudan) and only fits that track |
| `nitoryu needs clip` | bool | False | True = the move is not used while that clip is switched off in a player's `Nitoryu.ini [Animations]` |
| `nitoryu echo` | bool | False | True = every impact is followed by a hit of the second blade (two impacts = four hits, like the spin). Leave it off when the record already has a left-arm impact |
| `nitoryu cooldown ms` | int | 0 | finisher / opener: once per this many milliseconds of game time, per fighter |
| `nitoryu roll` | float | 1.0 | finisher / opener: the chance 0-1 that it is used when everything else fits |
| `nitoryu min gap` | float | 0 | opener: the enemy is at least this far beyond the fighter's reach |
| `nitoryu max gap` | float | 0 | opener: ... and at most this far; while the move is ready the fighter engages from this distance |
| `nitoryu timing from` | text | empty | the string id of the record it was cloned from: its speed and hit timing follow that record as loaded, so an animation pack that retimes the original carries over |

**5. Changing Nitoryu's own moves.** In a mod loaded after Nitoryu, open one of the twelve records and edit: the
Dash's cooldown (`nitoryu cooldown ms`), the spin's skill (`min skill`), which opener goes first (`chance`), or
`nitoryu move = 0` / `disabled` to take a move out. A player's `Nitoryu.ini [Moves]` can still override the cooldown
of the Dash and the spin and the spin's skill for their own game.

**6. Check it.** The log's load line: `techniques total=N ours=12 third_party=K from_fields=12+K specials=... ignored=0`,
and one `move_added record='...' kind='...' arms='...' ...` line per move of another mod. In a fight, `offhand_pick`
and `special_pick` lines name the track.

**Not possible yet:** a track that animates both arms itself, played as authored (no mirroring), and clips of your
own. Both need work in the plugin's pose engine (see the tracker).

## Weapons, characters, factions, races (0.34)

Four fields on records of your own (or on vanilla ones, overridden in your mod). All are read when they are needed,
so a change shows after the next game start.

| Record | Field | Type | Default | What it does |
|---|---|---|---|---|
| WEAPON | `nitoryu dual wield` | int | 0 | this weapon in a two-weapon pair. 0 = by its category (the setting `nitoryu weapon categories`: katanas and sabres as shipped). 1 = allowed whatever its category. 2 = refused: a pair with it fights one-handed (log reason `weapon_refused`). Two-handed-only weapons are always refused, and the second weapon still needs a `bare sword` mesh. Left-hand fighting follows the same field |
| WEAPON | `nitoryu off hand damage` | float | 1.0 | a multiplier on this weapon's damage when it strikes as the second blade, on top of the off-hand damage curve (counter `offhand_weapon_factor`) |
| WEAPON | `nitoryu pair length` | float | 0.0 | 0.42: the blade length the pair rule reads instead of the weapon's own `length` (0 = its own). The shorter blade at most 0.80 of the longer = long + short (better parries); at least 0.90 = equal blades (more twin strikes). Set it when a weapon's `length` does not say what it is, e.g. 16 on a short blade with a long reach |
| BUILDING | `nitoryu pair dummy` | bool | False | 0.42: True = this training building (its functionality is BF_TRAINING) trains Nitoryu's Dual Wielding instead of the functionality's `stat used`, up to the building's `output rate`, for a character carrying an allowed pair (counters `dummy_trained`, `dummy_no_pair`, `dummy_at_cap`). The `stat used` still picks the race's XP multiplier |
| CHARACTER, FACTION | `nitoryu dual wielder` | bool | False | True = this character, or every member of this faction, fights dual with an allowed pair: no stat minimums, no tier cap, and also when players set NPC dual wielding to `authored` (counter `npc_opt_in_by_field`). The same effect as the opt-in squad below; use whichever is easier |
| RACE | `nitoryu rig` | int | 0 | how Nitoryu treats this race's skeleton. 0 = automatic: the second blade mirrors the first when the rig allows it. 1 = no mirroring: the second blade sits in a fixed left-hand grip (as for a rig the mirror cannot build), and there is no left-hand fighting. 2 = no Nitoryu for this race (reason `race_refused`) |

`NIT_ambidexterity` on a STATS record (a character's starting Ambidexterity, shown in game as Dual Wielding since 0.36, -1 = seeded from the best weapon skill)
is declared in the same `fcs.def`, so it no longer needs a patch of the game's own `fcs.def`
.

## The pair dummies (0.42)

`Nitoryu.mod` holds three BUILDING records, `Pair Training Dummy mkI / mkII / mkIII`: clones of the vanilla Training
Dummies (mesh, use nodes, functionality, build cost) with `nitoryu pair dummy` ticked, `output rate` 10 / 20 / 30, their
own `building group` and their own `upgrades to` chain. The vanilla researches Simple / Better / Advanced Training
Dummies each get one more `enable buildings` reference, to ours. To make your own: tick the field on any training
building and set its `output rate`; to change the caps, override `output rate` on ours in a mod loaded after Nitoryu.

## In `Nitoryu.mod`: 12 COMBAT_TECHNIQUE records and one opt-in list

Every record has all weapon flags off (`katanas`, `sabre`, `blunt`, `hackers`, `heavy weapons`, `polearm`, `unarmed`)
and `animal = ATTACK_NULL` (21), a category no weapon uses: **the game never picks them alone**, and FCS filters by
weapon type hide them (search the combat technique list for "Nitoryu"). The plugin draws from them by setting the
fighter's weapon type to 21 for one pick, with dual proficiency (DP) standing in for the skill, so `min skill` is the
DP gate. Each is a clone of a vanilla record (column "from") playing a **vanilla track** with our values (arms,
impacts, directions, gates; the Dash also distance 35, speed, hit timing). Since 0.33 each also carries its
`nitoryu ...` fields (the section above): `nitoryu move` 1 for the six strikes, 2 for the three twins, 3 for the
Cross, 4 for the Dash and the spin, with their clips, cooldowns, rolls and gaps.

| String id | Name | What it is (track) | min skill (DP) | Cloned from |
|---|---|---|---|---|
| `1-Nitoryu.mod` | Nitoryu off-hand cut | T1 off-hand cut to the right (track chop left, mirrored) | 20 | 40-gamedata.quack |
| `4-Nitoryu.mod` | Nitoryu off-hand cut static | the same vs a static target (chop left, mirrored) | 20 | 196-gamedata.base |
| `2-Nitoryu.mod` | Nitoryu off-hand downward cut | T2 off-hand downward cut (chop down, mirrored) | 40 | 38-gamedata.quack |
| `5-Nitoryu.mod` | Nitoryu off-hand downward cut static | the same vs a static target (chop down static, mirrored) | 40 | 128-gamedata.quack |
| `3-Nitoryu.mod` | Nitoryu off-hand combo | T3 two cuts, right then down (downward combo, mirrored) | 60 | 45-gamedata.quack |
| `6-Nitoryu.mod` | Nitoryu twin overhead | T3 both blades cut down at once (chop down, TWIN) | 60 | 38-gamedata.quack |
| `7-Nitoryu.mod` | Nitoryu cross slash | T3 right blade cuts left, left blade cuts right (chop left, TWIN) | 60 | 40-gamedata.quack |
| `9-Nitoryu.mod` | Nitoryu cross finisher | special: an X on a staggered or fully pressured target (downward combo + our clip) | 60 | 45-gamedata.quack |
| `13-Nitoryu.mod` | Nitoryu off-hand rising cut | T1 rising cut with the left blade (chop left, mirrored, + our full-body clip; speed 1.1) | 20 | 40-gamedata.quack |
| `10-Nitoryu.mod` | Nitoryu dash slash | special: a long dash, both blades on arrival (flyknee + our clip) | 60 | 46052-rebirth.mod |
| `16-Nitoryu.mod` | Nitoryu twin sweep | T3 both blades in one diagonal cut (ma chudan, TWIN, + our full-body clip) | 60 | 38-gamedata.quack |
| `12-Nitoryu.mod` | Nitoryu spin | special (0.31): a rush, the sweep, a turn, the cross-split (flyknee + our clip); two impacts of the right blade, the plugin adds the left's to each (4 hits); tier 4 (0.36) | 75 | 46052-rebirth.mod |

`8-Nitoryu.mod` is an old id (0.10.0-test's animation link), no longer in the mod. `14` and `15` are reserved in
`spec.ids.json` for animation-wave moves.

### Which distance special goes first: the `chance` field (0.31)

The spin (`12-Nitoryu.mod`) and the Dash slash (`10-Nitoryu.mod`) both open from a distance. The
plugin reads the FCS field **`chance`** of these two records as their order:

- the record with the **higher `chance` goes first** (shipped: spin 4, Dash 3). While that move is ready (its switch on,
  the fighter's skill, the left blade drawn, off its cooldown), the other one is not played, and the fighter engages
  from the first move's distance (spin: 25 beyond reach, Dash: 35). The other move plays while the first is on cooldown;
- **equal** values: both are tried on every pick, the one with the smaller `nitoryu max gap` first (the spin);
- to put the Dash first, give `Nitoryu dash slash` the higher `chance` (in your own mod loaded after Nitoryu.mod, or here).

0.33: the same rule holds for any number of openers (and of finishers), another mod's included. `chance` is still the
game's own weight for the record inside Nitoryu's private pool; a change of 1-2 there does not show. The per-pick
rolls (spin 1.0, Dash 0.85), the cooldowns and the distance windows are fields of the two records since 0.33
(`nitoryu roll`, `nitoryu cooldown ms`, `nitoryu min gap` / `max gap`); `Nitoryu.ini [Moves]` can override the
cooldowns and the spin's skill for a player. Log counters: `special_dash_waits_for_spin`, `special_spin_waits_for_dash`.

### The NPC opt-in list (0.20, `11-Nitoryu.mod`)

A SQUAD_TEMPLATE named **Nitoryu dual wielders (opt-in list)** that no town or faction references, so it never
spawns (on purpose). It is how another mod makes its own NPCs fight with two blades:

1. In your mod (loaded after Nitoryu.mod), open that squad and add your CHARACTER records to **`squad`** (or
   `squad2`, `leader`, `choosefrom list`) and your FACTION records to **`faction`**. The values do not matter.
2. Give those characters an allowed pair (two katanas or sabres on the back, or back + hip) with a `bare sword` mesh.

The plugin reads the lists after every game load. An NPC whose own record, or whose faction's record, is listed
fights dual under `[NPC] dual=all` and `authored`, with no stat minimums and no `[NPC] max_tier` cap. Players can do
the same without FCS: `[NPC] opt_in=<string id>,<string id>` in `Nitoryu.ini`. The log's `npc_opt_in` line says how
many ids were read; `npc_record` lines show the record id a spawned NPC carries (checked in game before relying on it).

## What the plugin does with them (not FCS)

- **mirrored**: the whole body is mirrored at run time, so a right-hand vanilla swing becomes a left-hand one (SWAP).
- **TWIN**: the right arm plays the vanilla track and the left arm mirrors it in the same frame: both blades.
- **our clip**: the Cross and the Dash also get Nitoryu's own full-body animation, laid over the vanilla track and
  following its progress (the track keeps the ground travel and timing).
- Our own tracks (`nitoryu cross finisher`, `nitoryu dash slash`, `nitoryu block left`, `nitoryu scissor block`,
  `nitoryu dual draw`, `nitoryu dual draw hip`, `nitoryu run trail`, `nitoryu run lead`) are **not FCS records**:
  they live in `mods\Nitoryu\anim\Nitoryu_L3_male.skeleton` / `_female.skeleton`, which the plugin loads itself.
  They play on any race whose skeleton has the vanilla human bone layout; other rigs keep the game's pose (logged
  `l3_track_bad_bones`), and a rig the mirror cannot build gets a fixed left-hand grip
  `knowledge/where-nitoryu-moves-live.md`).

## FCS fields the plugin reads at run time

`plugin.json` `data_contract`: WEAPON `bare sword` (left-hand blade; a pair without it fights one-handed),
`sheath`, `armour penetration`; CONSTANTS `GLOBAL CONSTANTS`: `base block chance`, `cut damage 1/99`,
`blunt damage 1/99`, `damage multiplier` (0.19.1; since 0.20 the only source, no fixed addresses); SQUAD_TEMPLATE
`11-Nitoryu.mod` reference lists (the NPC opt-in, above).

Nitoryu's own fields (declared in `mod/fcs.def`): CONSTANTS `nitoryu ...` (69 settings, 0.32); COMBAT_TECHNIQUE
`nitoryu move`, `arms`, `clip`, `needs clip`, `echo`, `cooldown ms`, `roll`, `min gap`, `max gap`, `timing from`, with
the game's `min skill` and `chance` (0.33); WEAPON `nitoryu dual wield`, `nitoryu off hand damage`; CHARACTER and
FACTION `nitoryu dual wielder`; RACE `nitoryu rig`; STATS `NIT_ambidexterity` (0.34).
