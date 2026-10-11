# Nitoryu for plugin authors: the C++ API

*Русская версия: `API_RU.md`.*

if your mod is data only, you don't need this file, `FCS.md` covers it. this one is for people who write their own RE_Kenshi plugin and want it to work with Nitoryu: ask whether a character fights with two weapons, what its rank is, and get told when the second blade hits or parries.

this is API version 1: reading, events, the pair rule, which lets an addon open its own pairs (a heavy weapon with a sabre, two polearms) with its own numbers, and the move filter, which says what moves a pair uses. your own animations for a pair are not in it yet, so a two-handed pair plays Nitoryu's animations for two blades for now.

next to this file: `NitoryuApi.h`, the whole API in one C header. in the source repository it is `src/api/NitoryuApi.h`, and `examples/NitoryuAddonSample` is a small addon that uses everything below.

## What you can do, and what not yet

| You want to | Data (`FCS.md`) | The API |
|---|---|---|
| Let a weapon into a pair, or keep it out | Yes: the WEAPON field, the two weapon lists | Not needed |
| Make your NPCs fight with two blades, set their skill | Yes | Not needed |
| Add a dual-wield move | Yes | Not needed |
| Know in your plugin that a character fights dual, its rank, Path, Dual Wielding | No | Yes: `isDual`, `getInfo` |
| React when the second blade hits, parries, a special starts, a rank changes | No | Yes: `subscribe` |
| Read a Nitoryu setting as it is in force | No | Yes: `getSetting` |
| Let a heavy weapon or a polearm into a pair | No, never from data | Yes: `registerPairRule` |
| Refuse a pair, or let in one of any categories, with your own weight, speed and damage numbers | Partly (the record, the lists) | Yes: `registerPairRule` |
| Say which moves a pair may use | Yes: the field `nitoryu move weapons` on the move | Yes: `registerMoveFilter` |
| Your own animations for a pair | No | Later |

## How your plugin gets in

your addon is its own mod: its own folder, its own `RE_Kenshi.json`, Nitoryu as a required item on the Workshop. the load order of the two mods doesn't matter.

you export one function, and Nitoryu calls it:

```cpp
#include "NitoryuApi.h"

static const NitoryuApi* g_nitoryu = NULL;

extern "C" __declspec(dllexport) void __cdecl NitoryuAddon_Init(const NitoryuApi* api)
{
    if (api == NULL || api->version < 1)
        return;
    g_nitoryu = api;        // stays valid until the game closes
    api->subscribe("My addon", NITORYU_EV_ALL, &onEvent, NULL);
}
```

Nitoryu looks for `NitoryuAddon_Init` in every loaded plugin, once, on the main loop, after all plugins are loaded and its settings are read. so you never look for Nitoryu's DLL yourself, and it doesn't matter what that DLL is called in a modpack.

when Nitoryu is not installed, is off, or the player turned addons off, nobody calls your function. keep `g_nitoryu` NULL then and let your plugin work without it.

**a plugin that only wants to ask now and then** (no events) can skip the export: define `NITORYU_API_FIND` before including the header (after `<windows.h>` and `<tlhelp32.h>`) and call `Nitoryu_FindApi(1)`. it finds Nitoryu by its export `Nitoryu_GetApi`. it returns NULL until Nitoryu is ready (the game's first frames), so ask again later, don't keep the NULL.

## Rules

- **C only.** No classes, no `std::string` across the border. Everything is `__cdecl`.
- **Game objects are `void*`**: a `Character*`, a `Weapon*`. They have to be alive, Nitoryu can't check a pointer you hand it.
- **Text comes back in your buffer**: `getVersion(buf, sizeof(buf))`. A function returns `NITORYU_OK` (0) or more when it worked and a negative `NITORYU_E_*` when not. Nothing throws.
- **A struct you pass in starts with `size`.** Set `info.size = sizeof(info)` before the call. That is how an addon built against an older header keeps working with a newer Nitoryu.
- **The table only grows.** New functions go to the end, nothing moves or goes away. Check `api->version` for what you need.
- **Threads.** Each function below says where it may be called. Your event callback always runs on the main loop.
- **If your callback crashes**, Nitoryu catches it, stops calling that callback for the session and writes `api_callback_fault addon='...'` into the log. The game goes on.

the return codes: `NITORYU_E_ARG` (a NULL, an unknown key, a buffer too small), `NITORYU_E_OFF` (Nitoryu is off or addons are switched off), `NITORYU_E_THREAD` (main loop only), `NITORYU_E_BUSY` (you asked from inside your pair rule or move filter), `NITORYU_E_FULL` (no room: 64 subscriptions, 16 pair rules, 16 move filters), `NITORYU_E_FAULT` (an error inside Nitoryu, the call did nothing).

## The functions

all of them are members of `NitoryuApi`, since API version 1 (Nitoryu 1.2.0).

| Function | Thread | What it does |
|---|---|---|
| `int getVersion(char* out, unsigned outSize)` | any | Nitoryu's version, like `1.2.0`. Returns the length |
| `int isDual(void* character)` | any | 1 = the character fights with two weapons right now, 0 = not |
| `int getInfo(void* character, NitoryuInfo* out)` | any | The character's state, below |
| `const char* reasonName(int reason)` | any | A short name for a `NitoryuReason` (`two_handed_only`), for your log. The string is a constant, keep it as long as you like |
| `int getSetting(const char* fcsKey, char* out, unsigned outSize)` | main loop | A setting's value as text by its FCS key (`nitoryu weapon categories`), as it is in force: the default, a mod's FCS value, or the player's ini. The keys are in `FCS-settings.md`. Returns the length |
| `int invalidate(void* character)` | any | Nitoryu keeps a character's state for a moment. Call this when something of yours changed what it should be, the next question is answered fresh |
| `int subscribe(const char* addonName, int eventType, NitoryuEventFn fn, void* userData)` | any | Your `fn` is called for every event of that type, `NITORYU_EV_ALL` for all. `addonName` shows in the log when something goes wrong in your callback. There is no unsubscribe |
| `int registerPairRule(const char* addonName, NitoryuPairRuleFn fn, void* userData)` | any | Your `fn` is asked about every pair, "The pair rule" below. It can't be taken back |
| `int registerMoveFilter(const char* addonName, NitoryuMoveFilterFn fn, void* userData)` | any | Your `fn` is asked about every move that comes up for a pair, "The move filter" below. It can't be taken back |

"right now" in `isDual` means the stance is on: two weapons that pair, one of them in the hands, both arms working, and for a player character the stance button not switched off. a fighter with sheathed blades is not dual (`not_drawn`).

### NitoryuInfo

```cpp
NitoryuInfo info;
info.size = sizeof(info);
if (g_nitoryu->getInfo(character, &info) == NITORYU_OK && info.dual)
    ...
```

| Field | What |
|---|---|
| `dual` | 1 = fights with two weapons right now |
| `reason` | Why not, a `NitoryuReason`: `NOT_DRAWN`, `LAYOUT` (not two on the back or back + hip), `HANDS_NOT_IN_PAIR`, `WEAPON_REFUSED`, `TYPE_NOT_ALLOWED`, `TWO_HANDED_ONLY`, `NO_BARE_MESH`, `ARM`, `TOGGLED_OFF`, `RACE_REFUSED`, `NPC` (an NPC below the minimums or not opted in), `GAME_VERSION`, `NOT_READY`, `OTHER`. `DUAL` when it is dual |
| `rank` | 0-5, after the caps. 0 when not dual |
| `pathRank` | The rank the Path alone gives |
| `path` | The Path, 0-100. 0 when not dual |
| `dualWielding` | The Dual Wielding skill as the stats window shows it. Filled when not dual too |
| `dualWieldingInFight` | The same after overload and armour, the number the fight uses |
| `offFromHip` | 1 = the second weapon is the hip one |
| `optedIn` | 1 = an NPC that a mod opted in |
| `mainWeapon`, `offWeapon` | `Weapon*`: the one in the hands and the second one. NULL when not dual |

## Events

```cpp
void __cdecl onEvent(const NitoryuEvent* ev, void* userData)
{
    if (ev->type == NITORYU_EV_OFF_HAND_HIT)
        ...     // ev->character hit ev->other with ev->weapon
}
```

the callback runs on the main loop, a frame or so after the moment itself, so you can log, touch your UI and call the API from it. the price: by then the game has moved on. the events of one frame come in the order they happened.

| Event | `character` | The rest |
|---|---|---|
| `NITORYU_EV_DUAL_ON` | who now fights with two weapons | `value` = its rank |
| `NITORYU_EV_DUAL_OFF` | who no longer does | `value` = the `NitoryuReason`, `text` = its name. Sheathing the blades is a `DUAL_OFF` too. It comes about 0.6 s after the fact: in a fight the stance drops out for a moment now and then, and that is not told |
| `NITORYU_EV_RANK_CHANGED` | the fighter | `value` = the new rank, `amount` = the old one |
| `NITORYU_EV_DUAL_WIELDING_CHANGED` | the fighter | `amount` = the skill now. Sent when it passes a whole number, while the fighter is dual |
| `NITORYU_EV_OFF_HAND_HIT` | the attacker | `other` = who was hit, `weapon` = the second weapon, `value` = the move's kind (1 strike, 2 twin, 3 finisher, 4 opener, 0 another mod's move), `text` = its animation track. Only hits that landed |
| `NITORYU_EV_OFF_HAND_PARRY` | the defender | `other` = the attacker |
| `NITORYU_EV_SPECIAL` | the fighter | A finisher or an opener was picked. `value` = the kind, `text` = the move's tag (`cross`, `dash`, `spin`, or the kind's name for another mod's move) |
| `NITORYU_EV_CHARACTER_GONE` | a number, no longer a character | A character you got events about was destroyed (a load, a body cleaned up). Don't read the pointer, just drop what you keep for it |

`text` is never NULL and lives only inside the callback, copy it if you need it later.

Nitoryu starts watching a character when it first fights dual, so a `DUAL_ON` is always the first thing you hear about anyone. when your addon subscribes in the middle of a game you get a `DUAL_ON` for every fighter who is dual then, as soon as Nitoryu looks at them again.

a queue holds 256 events between two frames. past that the new ones are dropped and counted (`api_events_dropped` in the log's `counts` line).

## The pair rule

which two weapons fight as a pair is Nitoryu's own rule: the weapon's record, the category list (katanas and sabres as shipped), the player's any-weapon button (blunt and hackers). a pair rule is your plugin's word in that:

```cpp
void __cdecl pairRule(const NitoryuPairQuery* q, NitoryuPairAnswer* a, void* userData)
{
    bool heavyA = q->categoryA == 3, heavyB = q->categoryB == 3;
    int other = heavyA ? q->categoryB : q->categoryA;
    if (heavyA == heavyB || (other != 0 && other != 1))
        return;                             // no opinion
    a->answer = NITORYU_PAIR_ALLOW_TWO_HANDED;  // a heavy weapon with a katana or a sabre
    a->attackSpeed = 0.9f;
}
```

| Answer | What it does |
|---|---|
| `NITORYU_PAIR_DEFAULT` | No opinion. The answer comes in like this, so a rule that doesn't care just returns |
| `NITORYU_PAIR_ALLOW` | A pair, whatever the two categories. Not with a two-handed weapon in it |
| `NITORYU_PAIR_REFUSE` | Not a pair (reason `plugin_refused`) |
| `NITORYU_PAIR_ALLOW_TWO_HANDED` | A pair, also with a heavy weapon or a polearm in it |

the order, first match decides:

1. A weapon whose own record or a list refuses it (`nitoryu dual wield` = 2) never pairs, whatever a rule says. The weapon's author has the last word.
2. A rule that answers REFUSE refuses the pair. With several addons: the first one, in the order they registered.
3. Else the first rule that answers ALLOW or ALLOW_TWO_HANDED lets the pair in, and its numbers count.
4. Else Nitoryu's own rule.

**two-handed weapons** (heavy weapons, polearms) pair only through ALLOW_TWO_HANDED. no setting, no record value, no list and not the any-weapon button does that: that button is blunt weapons and hackers only, and it stays hidden for a pair your rule let in. bows never pair.

what still holds after an ALLOW: one of the two is in the hands, the second weapon has a `bare sword` mesh, both arms work, the race is not refused, the player didn't switch the stance off, and an NPC still passes its minimums or is opted in.

**the numbers.** when your answer lets the pair in, four multipliers go into Nitoryu's own formulas for that pair, each 1.0 as it comes in:

| Field | What it scales |
|---|---|
| `weight` | The pair's weight in the strength and speed rule (the two weapons' weights x `nitoryu pair weight mult`). Below 1 = the pair counts as lighter |
| `attackSpeed` | The pair's attack speed factor |
| `blockSpeed` | The pair's block speed factor |
| `offHandDamage` | The second weapon's damage |

a value outside 0.1-10 counts as 1.0. you can answer ALLOW with numbers for a pair Nitoryu lets in anyway, e.g. to make a katana with a short blade faster. this is the way to balance your pair: hooking the same game functions as Nitoryu depends on which of the two plugins loaded first.

the query: `allowed` and `reason` are Nitoryu's own answer without any plugin, `categoryA` / `categoryB` the game's weapon categories (0 katana, 1 sabre, 2 blunt, 3 heavy, 4 hacker, 8 polearm), `twoHandedA` / `twoHandedB`, `player`, and the pointers `character`, `weaponA`, `weaponB`. A and B are the two carried weapons in no order, the rule is also asked while they are sheathed (the stance button, the stats window), so answer the same for (A, B) and (B, A).

**the callback runs inline**, on whatever thread asks (the main loop, the combat threads), several times a frame for a character on screen. so:

- keep it short: read the query, fill the answer, return
- no game calls that can wait, no lock another thread of yours may hold, no logging on every call
- don't ask Nitoryu back from inside it: `isDual`, `getInfo` and `invalidate` return `NITORYU_E_BUSY` there
- when your rule depends on something of your own that changed (a toggle in your UI), call `invalidate(character)` after the change

how a two-handed pair looks: the game plays its own animations for the weapon in the right hand, Nitoryu's stance, blocks, draws and sword moves are the ones made for two blades. there are no animations for a heavy pair in Nitoryu. that part of the API (your own animation set for a pair) comes later.

## The move filter

a dual fighter's moves are COMBAT_TECHNIQUE records with `nitoryu move` set (`FCS.md`, "Add your own dual-wield move"), mine and other mods'. there are two ways to say which pair uses which move.

**data, no plugin:** the field `nitoryu move weapons` on the move, a list of weapon categories (`1,3` = sabres and heavy weapons). the move is used only by a pair whose two weapons are both in the list. empty = every pair. so an addon's own moves for a heavy + sabre pair carry `1,3` and nobody else gets them.

**the filter**, for what data can't say (my own moves have an empty list, so every pair gets them):

```cpp
int __cdecl moveFilter(const NitoryuMoveQuery* q, void* userData)
{
    if (q->twoHandedPair && q->kind == NITORYU_MOVE_TWIN)
        return NITORYU_FILTER_REFUSE;       // no twin strikes with a heavy weapon
    return NITORYU_FILTER_DEFAULT;
}
```

the order, first match decides: a filter that answers `NITORYU_FILTER_REFUSE` (the move is not used for this pair), a filter that answers `NITORYU_FILTER_ALLOW` (it is, whatever the record's list says), then the record's list.

the query: `kind` (1 strike, 2 twin, 3 finisher, 4 opener), `mainHand` (a strike of the weapon in the hands alone), `categoryMain` / `categoryOff`, `twoHandedPair` (the pair is in by a rule's ALLOW_TWO_HANDED), `listed` (what the record's own list says for this pair), the pointers `character`, `mainWeapon`, `offWeapon`, `record` (the move's `GameData*`), and two strings that live only inside the callback: `recordId` (`9-Nitoryu.mod`) and `track`.

it runs inline like the pair rule, a dozen times per attack, and the same rules hold: short, no game calls, no questions back to Nitoryu. if your filter refuses every move, the fighter attacks with the main weapon only.

the game's own moves for the weapon in the hands are not asked about: with "only Nitoryu's strikes" on (the default) a dual fighter doesn't use them, and with it off they are the game's pick. one thing changes for a two-handed pair: another mod's long two-handed move, which Nitoryu takes away from a normal pair (the left hand can't join the grip), stays, since it is that weapon's own move (counter `two_handed_kept_pair`).

## What shows in the log

`RE_Kenshi_log.txt`, right after the game reaches its first frames:

- `api_config addons=1 api_version=1 found=N subscriptions=M pair_rules=R move_filters=F`: the API opened, N addons were found. `addons=0`: the player (or a modpack) turned addons off.
- `api_addon module='YourAddon.dll' subscriptions=K pair_rules=R move_filters=F init='ok'`: your `NitoryuAddon_Init` ran. `init='fault'`: it crashed.
- `api_pair_rule addon='...' answer=3 category_a=3 category_b=1 own_allowed=0 weight=... attack_speed=...`: a rule decided a pair. one line for each different decision (rule, the two categories, the answer). answer: 1 allow, 2 refuse, 3 allow two-handed.
- `api_callback_fault addon='...' kind='event' event=N` (or `kind='pair_rule'`, `kind='move_filter'`): your callback crashed and is off for the session.
- counters: `pick_skip_plugin_filter` (a filter refused a move), `pick_skip_move_weapons` (the record's list did), `api_pair_allowed`, `api_pair_two_handed`, `api_pair_refused` (per evaluation of a fighter's stance), `api_pair_no_weapon_skill` (a pair was let in with a category the game has no weapon skill for: ignored), `api_events_dropped`, `api_callback_fault`.

a player can turn all addons off with `[Compat] addons=0` in `Nitoryu.ini` (a modpack: the setting `nitoryu addons` on GLOBAL CONSTANTS). it is for bug reports, to see whose bug it is. it is read once, at the start.

## The sample addon

`examples/NitoryuAddonSample` in the source repository: one `.cpp`, a project for the Visual C++ 2010 toolset (the same setup as Nitoryu itself, `BUILDING.md`), a `RE_Kenshi.json`.

1. `startPlugin` only writes a line: Nitoryu may not be loaded yet, so nothing is asked there.
2. `NitoryuAddon_Init` keeps the table, reads Nitoryu's version and one setting, subscribes to every event and registers a pair rule and a move filter.
3. `onEvent` writes a line per event and asks `getInfo` about the character, except for `CHARACTER_GONE`.
4. `pairRule` lets a heavy weapon pair with a sabre or a katana, a bit lighter and a bit slower.
5. `moveFilter` takes the twin strikes away from that pair.

to try it: build it, make a mod folder `NitoryuAddonSample` in `Kenshi\mods` with the DLL, the `RE_Kenshi.json` and an empty `NitoryuAddonSample.mod` (make it in FCS, the launcher needs it to list the mod), tick it above or below Nitoryu, draw two katanas and fight. the log fills with `NitoryuAddonSample:` lines. then a heavy weapon and a sabre on the back: the stance button shows and they fight as a pair.

## Versions and what I promise

- names, fields and the order of the table stay as they are. new things are added at the end, with a higher `NITORYU_API_VERSION`.
- an addon built for API 1 works with every later Nitoryu. the other way round you check: `api->version` for the function you need, `ev->size` and `info.size` for a field that a later version added.
- an event type you don't know is not an error, ignore it.

## Credit and license

list Nitoryu as a required item on your Workshop page, that is all I ask. Nitoryu's source is GPL-3.0, and so are KenshiLib and RE_Kenshi that every plugin is built on, so a plugin for this game is open source anyway. if you copy code out of Nitoryu, keep it under the same license and say where it came from.
