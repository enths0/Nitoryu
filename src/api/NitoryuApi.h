/*
 * Nitoryu's API for other RE_Kenshi plugins. C only, no other include: copy this one file into your plugin.
 * The manual is docs/API.md.
 *
 * How your plugin gets the table
 *
 *   An addon exports one function and Nitoryu calls it, once, on the main loop, after every plugin is loaded:
 *
 *       extern "C" __declspec(dllexport) void __cdecl NitoryuAddon_Init(const NitoryuApi* api)
 *       {
 *           if (api->version < 1) return;
 *           g_nitoryu = api;                      // valid until the game closes
 *           api->subscribe("My addon", NITORYU_EV_ALL, &onEvent, NULL);
 *       }
 *
 *   The load order of the two mods does not matter, and neither does the name of Nitoryu's DLL. Without
 *   Nitoryu nobody calls the function, so your plugin must work with g_nitoryu == NULL.
 *
 *   A plugin that only wants to ask now and then can instead look for the export Nitoryu_GetApi in the loaded
 *   modules (define NITORYU_API_FIND before including this file for a helper that does it). It returns NULL
 *   until Nitoryu is ready, so ask again later instead of keeping the NULL.
 *
 * Rules of the table
 *
 *   - Members are only ever added at the end. Check `version` for what you need, and `size` before you call a
 *     member that a newer version added.
 *   - A struct you pass in starts with `size`: set it to sizeof(the struct) before the call.
 *   - Game objects are void* (Character*, Weapon*). They must be alive: Nitoryu cannot check a pointer.
 *   - Text comes back in your own buffer. Functions return NITORYU_OK or more on success, a negative
 *     NITORYU_E_* otherwise. Nothing throws.
 *   - Each function says which thread may call it. Event callbacks always run on the main loop.
 */
#ifndef NITORYU_API_H
#define NITORYU_API_H

#ifdef __cplusplus
extern "C" {
#endif

#define NITORYU_API_VERSION 1
#define NITORYU_CALL __cdecl

/* ---- results ---- */
#define NITORYU_OK        0
#define NITORYU_E_ARG    (-1)   /* a NULL pointer, an unknown key or event, a buffer too small */
#define NITORYU_E_OFF    (-2)   /* Nitoryu is off, or addons are switched off ([Compat] addons=0) */
#define NITORYU_E_THREAD (-3)   /* this function is for the main loop only */
#define NITORYU_E_BUSY   (-4)   /* called from inside a callback that must answer at once */
#define NITORYU_E_FULL   (-5)   /* no room for another registration */
#define NITORYU_E_FAULT  (-6)   /* an error inside Nitoryu; the call did nothing */

/* ---- why a character does not fight with two weapons right now ---- */
enum NitoryuReason
{
    NITORYU_REASON_DUAL = 0,            /* it does */
    NITORYU_REASON_OTHER = 1,           /* a reason this version of the header has no name for */
    NITORYU_REASON_NOT_DRAWN = 2,       /* nothing in the hands */
    NITORYU_REASON_LAYOUT = 3,          /* not two weapons on the back, or one on the back and one on the hip */
    NITORYU_REASON_HANDS_NOT_IN_PAIR = 4,   /* the weapon in the hands is not one of the two */
    NITORYU_REASON_WEAPON_REFUSED = 5,  /* a weapon's own record (or a list) keeps it out of pairs */
    NITORYU_REASON_TYPE_NOT_ALLOWED = 6,    /* a weapon category that does not pair */
    NITORYU_REASON_TWO_HANDED_ONLY = 7, /* a heavy weapon, a polearm or a bow */
    NITORYU_REASON_NO_BARE_MESH = 8,    /* the second weapon has no mesh for the left hand */
    NITORYU_REASON_ARM = 9,             /* an arm is lost or crippled */
    NITORYU_REASON_TOGGLED_OFF = 10,    /* the player switched the stance off for this character */
    NITORYU_REASON_RACE_REFUSED = 11,   /* the race's record says no */
    NITORYU_REASON_NPC = 12,            /* an NPC below the minimums, not opted in, or NPCs are off */
    NITORYU_REASON_GAME_VERSION = 13,   /* a game version Nitoryu cannot build the left hand on */
    NITORYU_REASON_PLUGIN_REFUSED = 14, /* a plugin's pair rule refused the pair */
    NITORYU_REASON_NOT_READY = 15       /* no stats or inventory yet, or the read failed */
};

/* ---- a character's state, by getInfo ---- */
typedef struct NitoryuInfo
{
    int size;                   /* in: sizeof(NitoryuInfo). out: the bytes Nitoryu filled */
    int dual;                   /* 1 = fights with two weapons right now */
    int reason;                 /* NitoryuReason */
    int rank;                   /* 0-5, after the caps. 0 when not dual */
    int pathRank;             /* the rank the Path alone gives */
    int offFromHip;             /* 1 = the second weapon is the hip one */
    int optedIn;                /* 1 = an NPC another mod opted in */
    float path;                 /* the Path, 0-100. 0 when not dual */
    float dualWielding;         /* the Dual Wielding skill as the stats window shows it */
    float dualWieldingInFight;  /* ... as it counts in a fight: after overload and armour */
    void* mainWeapon;           /* Weapon*: the one in the hands. NULL when not dual */
    void* offWeapon;            /* Weapon*: the second one. NULL when not dual */
} NitoryuInfo;

/* ---- events ---- */
enum NitoryuEventType
{
    NITORYU_EV_ALL = 0,                     /* subscribe: every event */
    NITORYU_EV_DUAL_ON = 1,                 /* the character now fights with two weapons. value = its rank */
    NITORYU_EV_DUAL_OFF = 2,                /* ... no longer does. value = NitoryuReason */
    NITORYU_EV_RANK_CHANGED = 3,            /* value = the new rank, amount = the old one */
    NITORYU_EV_DUAL_WIELDING_CHANGED = 4,   /* the skill passed a whole number. amount = the skill now */
    NITORYU_EV_OFF_HAND_HIT = 5,            /* the second weapon landed a hit. other = the one hit,
                                               weapon = the second weapon, value = NitoryuMoveKind,
                                               text = the move's animation track */
    NITORYU_EV_OFF_HAND_PARRY = 6,          /* the second weapon parried. other = the attacker */
    NITORYU_EV_SPECIAL = 7,                 /* a finisher or an opener starts. value = NitoryuMoveKind,
                                               text = the move's tag ("cross", "dash", "spin", or the kind) */
    NITORYU_EV_CHARACTER_GONE = 8           /* a character Nitoryu reported on is destroyed (a load, a
                                               death cleaned up). `character` is only a number now: do not
                                               read it, drop what you keep for it */
};

enum NitoryuMoveKind
{
    NITORYU_MOVE_OTHER = 0,
    NITORYU_MOVE_STRIKE = 1,
    NITORYU_MOVE_TWIN = 2,
    NITORYU_MOVE_FINISHER = 3,
    NITORYU_MOVE_OPENER = 4
};

typedef struct NitoryuEvent
{
    int size;                   /* the bytes Nitoryu filled */
    int type;                   /* NitoryuEventType */
    void* character;            /* Character*: whom the event is about */
    void* other;                /* Character*: the other side, when the event has one, else NULL */
    void* weapon;               /* Weapon*, when the event has one, else NULL */
    int value;
    float amount;
    const char* text;           /* never NULL; valid only inside the callback */
} NitoryuEvent;

/* Runs on the main loop, a frame or so after the moment itself. The game may have moved on: a character
   named by an older event in the same frame can already be gone (its NITORYU_EV_CHARACTER_GONE follows). */
typedef void (NITORYU_CALL *NitoryuEventFn)(const NitoryuEvent* ev, void* userData);

/* ---- the pair rule: your plugin's word on which two weapons pair ----
 *
 * Nitoryu asks every registered rule about a pair before its own answer stands. The order, first match decides:
 *   1. a weapon whose own record (or a list) refuses it never pairs, whatever a rule says;
 *   2. a rule that answers NITORYU_PAIR_REFUSE refuses the pair (the first one, in the order of registration);
 *   3. else the first rule that answers ALLOW or ALLOW_TWO_HANDED lets the pair in and its numbers count;
 *   4. else Nitoryu's own rule: the weapon's record, the category list, the player's any-weapon button.
 * A two-handed weapon (a heavy weapon, a polearm) pairs only by ALLOW_TWO_HANDED: no setting, no record and no
 * button does that. What still holds after an ALLOW: one weapon in the hands, the second weapon's left-hand
 * mesh, both arms, the race, the player's stance button, and for an NPC the usual minimums.
 *
 * The callback runs inline, on whatever thread asks (the main loop, the combat threads), several times a frame
 * for a character on screen. Keep it short and pure: read the query, fill the answer, return. Do not call the
 * game, do not wait on a lock another thread may hold, and do not call isDual, getInfo or invalidate from
 * inside it (they return NITORYU_E_BUSY).
 */
enum NitoryuPairVerdict
{
    NITORYU_PAIR_DEFAULT = 0,           /* no opinion */
    NITORYU_PAIR_ALLOW = 1,             /* a pair, whatever the categories (not a two-handed weapon) */
    NITORYU_PAIR_REFUSE = 2,            /* not a pair */
    NITORYU_PAIR_ALLOW_TWO_HANDED = 3   /* a pair, also with a two-handed weapon in it */
};

typedef struct NitoryuPairQuery
{
    int size;                   /* the bytes Nitoryu filled */
    int allowed;                /* Nitoryu's own answer with no plugin: 1 = a pair */
    int reason;                 /* ... and why not: NitoryuReason (WEAPON_REFUSED, TYPE_NOT_ALLOWED,
                                   TWO_HANDED_ONLY), DUAL when allowed */
    int categoryA, categoryB;   /* the game's weapon category: 0 katana, 1 sabre, 2 blunt, 3 heavy, 4 hacker,
                                   8 polearm */
    int twoHandedA, twoHandedB; /* 1 = the game treats it as two-handed only */
    int player;                 /* 1 = a player character */
    void* character;            /* Character* */
    void* weaponA;              /* Weapon*. A and B are the two carried weapons in no order: the rule is also
                                   asked while they are sheathed, so answer the same for (A, B) and (B, A) */
    void* weaponB;
} NitoryuPairQuery;

typedef struct NitoryuPairAnswer
{
    int size;                   /* the bytes you may write */
    int answer;                 /* NitoryuPairVerdict; comes in as NITORYU_PAIR_DEFAULT */
    /* Numbers for this pair, used when your answer lets it in. Each comes in as 1.0 (Nitoryu's own balance);
       a value outside 0.1 - 10 counts as 1.0. */
    float weight;               /* x the pair's weight in the strength and speed rule (lighter below 1) */
    float attackSpeed;          /* x the pair's attack speed factor */
    float blockSpeed;           /* x the pair's block speed factor */
    float offHandDamage;        /* x the second weapon's damage */
} NitoryuPairAnswer;

typedef void (NITORYU_CALL *NitoryuPairRuleFn)(const NitoryuPairQuery* query, NitoryuPairAnswer* answer, void* userData);

/* ---- the move filter: which of Nitoryu's moves a pair may use ----
 *
 * A dual fighter's moves are COMBAT_TECHNIQUE records with `nitoryu move` set (docs/FCS.md), Nitoryu's own and
 * other mods'. Each time one comes up for a fighter, the filters are asked. The order, first match decides:
 *   1. a filter that answers NITORYU_FILTER_REFUSE: the move is not used for this pair;
 *   2. a filter that answers NITORYU_FILTER_ALLOW: it is, whatever the record's own list says;
 *   3. the record's field `nitoryu move weapons` (`listed` below).
 * The callback runs inline like a pair rule, many times per attack: the same rules hold (short, pure, no game
 * calls, no questions back to Nitoryu).
 */
enum NitoryuFilterVerdict
{
    NITORYU_FILTER_DEFAULT = 0,         /* no opinion */
    NITORYU_FILTER_ALLOW = 1,
    NITORYU_FILTER_REFUSE = 2
};

typedef struct NitoryuMoveQuery
{
    int size;                   /* the bytes Nitoryu filled */
    int kind;                   /* NitoryuMoveKind */
    int mainHand;               /* 1 = a strike of the weapon in the hands alone */
    int categoryMain;           /* the game's weapon category of the weapon in the hands ... */
    int categoryOff;            /* ... and of the second one */
    int twoHandedPair;          /* 1 = the pair is in by a rule's NITORYU_PAIR_ALLOW_TWO_HANDED */
    int listed;                 /* 1 = the record's `nitoryu move weapons` lets this pair use the move (an empty
                                   list lets every pair), 0 = it does not */
    int reserved;
    void* character;            /* Character* */
    void* mainWeapon;           /* Weapon* */
    void* offWeapon;            /* Weapon* */
    void* record;               /* GameData*: the COMBAT_TECHNIQUE record */
    const char* recordId;       /* its string id ("9-Nitoryu.mod"); valid only inside the callback */
    const char* track;          /* its animation track; valid only inside the callback */
} NitoryuMoveQuery;

/* Returns a NitoryuFilterVerdict. */
typedef int (NITORYU_CALL *NitoryuMoveFilterFn)(const NitoryuMoveQuery* query, void* userData);

/* ---- the table ---- */
typedef struct NitoryuApi
{
    int version;                /* NITORYU_API_VERSION of the Nitoryu that is running */
    int size;                   /* sizeof(NitoryuApi) there */

    /* Nitoryu's version, "1.2.0". Any thread. Returns the length written. */
    int (NITORYU_CALL *getVersion)(char* out, unsigned outSize);

    /* 1 = this character fights with two weapons right now, 0 = not. Any thread. */
    int (NITORYU_CALL *isDual)(void* character);

    /* The character's state. Any thread. */
    int (NITORYU_CALL *getInfo)(void* character, NitoryuInfo* out);

    /* The name of a NitoryuReason as the log writes it ("two_handed_only"). Any thread. The string is a
       constant of Nitoryu and stays valid. */
    const char* (NITORYU_CALL *reasonName)(int reason);

    /* A setting's value as text, by its FCS key ("nitoryu weapon categories"): what is in force now, after
       the mods' FCS values and the player's ini. Main loop only. Returns the length written. */
    int (NITORYU_CALL *getSetting)(const char* fcsKey, char* out, unsigned outSize);

    /* Nitoryu keeps a character's state for a short while. Call this when something of yours changed what
       it should be, and the next question is answered fresh. Any thread. */
    int (NITORYU_CALL *invalidate)(void* character);

    /* Call `fn` for every event of this type (NITORYU_EV_ALL: all of them). `addonName` goes into the log
       next to anything that goes wrong in your callback. There is no way to unsubscribe. Any thread. */
    int (NITORYU_CALL *subscribe)(const char* addonName, int eventType, NitoryuEventFn fn, void* userData);

    /* Add a pair rule (above). `addonName` goes into the log next to every pair your rule decided. It cannot
       be taken back. Any thread. */
    int (NITORYU_CALL *registerPairRule)(const char* addonName, NitoryuPairRuleFn fn, void* userData);

    /* Add a move filter (above). It cannot be taken back. Any thread. */
    int (NITORYU_CALL *registerMoveFilter)(const char* addonName, NitoryuMoveFilterFn fn, void* userData);
} NitoryuApi;

/* What an addon exports. Nitoryu calls it once, on the main loop. */
typedef void (NITORYU_CALL *NitoryuAddon_InitFn)(const NitoryuApi* api);
#define NITORYU_ADDON_INIT_NAME "NitoryuAddon_Init"

/* What Nitoryu exports. NULL when Nitoryu is not ready, is off, or is older than `wantedVersion`. */
typedef const NitoryuApi* (NITORYU_CALL *Nitoryu_GetApiFn)(int wantedVersion);
#define NITORYU_GET_API_NAME "Nitoryu_GetApi"

#ifdef NITORYU_API_FIND
/* Looks for Nitoryu among the loaded modules by its export. Needs <windows.h> and <tlhelp32.h> included
   before this file. NULL: Nitoryu is not loaded or not ready yet. */
static const NitoryuApi* Nitoryu_FindApi(int wantedVersion)
{
    const NitoryuApi* api = NULL;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    MODULEENTRY32W me;
    if (snap == INVALID_HANDLE_VALUE)
        return NULL;
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me))
    {
        do
        {
            Nitoryu_GetApiFn get = (Nitoryu_GetApiFn)GetProcAddress(me.hModule, NITORYU_GET_API_NAME);
            if (get != NULL)
            {
                api = get(wantedVersion);
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return api;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
