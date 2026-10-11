// A sample addon for Nitoryu: what its plugin API gives another RE_Kenshi plugin, in one file.
//
// It takes Nitoryu's function table, writes a line into RE_Kenshi_log.txt for every event, reads the state of
// the character an event is about, and registers one pair rule (a heavy weapon pairs with a sabre or a katana) and one move filter (that pair does no twin strikes).
// Start your own addon from it: keep NitoryuAddon_Init and startPlugin, replace onEvent, pairRule and moveFilter.
//
// The manual is docs/API.md, the header src/api/NitoryuApi.h (copy that one file into your project).
#include <Windows.h>

#include <Debug.h>      // KenshiLib: DebugLog

#include <cstdio>
#include <string>

#include "../../src/api/NitoryuApi.h"

namespace
{
    // NULL until Nitoryu calls NitoryuAddon_Init. It stays NULL when Nitoryu is not installed, is off, or the
    // player switched addons off, so every use checks it.
    const NitoryuApi* g_nitoryu = NULL;

    const char* eventName(int type)
    {
        switch (type)
        {
        case NITORYU_EV_DUAL_ON:                return "dual_on";
        case NITORYU_EV_DUAL_OFF:               return "dual_off";
        case NITORYU_EV_RANK_CHANGED:           return "rank_changed";
        case NITORYU_EV_DUAL_WIELDING_CHANGED:  return "dual_wielding_changed";
        case NITORYU_EV_OFF_HAND_HIT:           return "off_hand_hit";
        case NITORYU_EV_OFF_HAND_PARRY:         return "off_hand_parry";
        case NITORYU_EV_SPECIAL:                return "special";
        case NITORYU_EV_CHARACTER_GONE:         return "character_gone";
        default:                                return "unknown";       // an event newer than this header
        }
    }

    // The game's weapon categories, as a pair query names them.
    const int CATEGORY_KATANA = 0, CATEGORY_SABRE = 1, CATEGORY_HEAVY = 3;

    // The pair rule. It runs inline, on any thread, several times a frame: read the query, fill the answer,
    // return. No logging, no game calls, no questions back to Nitoryu from here.
    //
    // A heavy weapon with a sabre or a katana is a pair. A heavy weapon is two-handed for the game, so the
    // answer has to be ALLOW_TWO_HANDED; nothing else in Nitoryu lets one in. The pair is heavy and slow, and
    // the numbers say so: Nitoryu uses them in its own formulas for this pair only.
    void NITORYU_CALL pairRule(const NitoryuPairQuery* query, NitoryuPairAnswer* answer, void* /*userData*/)
    {
        bool heavyA = query->categoryA == CATEGORY_HEAVY, heavyB = query->categoryB == CATEGORY_HEAVY;
        int other = heavyA ? query->categoryB : query->categoryA;
        if (heavyA == heavyB || (other != CATEGORY_SABRE && other != CATEGORY_KATANA))
            return;             // two heavy weapons, or none: no opinion, Nitoryu's own rule decides
        answer->answer = NITORYU_PAIR_ALLOW_TWO_HANDED;
        answer->weight = 0.8f;          // the pair counts as a fifth lighter than its two weapons weigh
        answer->attackSpeed = 0.9f;     // ... but swings a tenth slower
        answer->offHandDamage = 1.0f;   // the second weapon hits as Nitoryu's balance says
    }

    // The move filter. Inline like the pair rule, many times per attack. A pair with the heavy weapon does not
    // use twin strikes (both weapons in one swing); every other move stays as Nitoryu has it.
    int NITORYU_CALL moveFilter(const NitoryuMoveQuery* query, void* /*userData*/)
    {
        if (query->twoHandedPair && query->kind == NITORYU_MOVE_TWIN)
            return NITORYU_FILTER_REFUSE;
        return NITORYU_FILTER_DEFAULT;
    }

    const char* okText(int result) { return result == NITORYU_OK ? "ok" : "failed"; }

    // Runs on the main loop, so it may log and may ask Nitoryu about the character.
    void NITORYU_CALL onEvent(const NitoryuEvent* ev, void* /*userData*/)
    {
        char line[320];
        sprintf_s(line, "NitoryuAddonSample: event=%s character=%p other=%p value=%d amount=%.1f text='%s'",
                  eventName(ev->type), ev->character, ev->other, ev->value, ev->amount, ev->text);
        std::string text(line);
        // The character of a "gone" event is only a number by now: never hand it back to the game or to Nitoryu.
        if (ev->type != NITORYU_EV_CHARACTER_GONE && g_nitoryu != NULL)
        {
            NitoryuInfo info;
            info.size = sizeof(info);
            if (g_nitoryu->getInfo(ev->character, &info) == NITORYU_OK)
            {
                sprintf_s(line, " | now: dual=%d reason=%s rank=%d path=%.1f dual_wielding=%.1f (%.1f in a fight) off_from_hip=%d",
                          info.dual, g_nitoryu->reasonName(info.reason), info.rank, info.path, info.dualWielding,
                          info.dualWieldingInFight, info.offFromHip);
                text += line;
            }
        }
        DebugLog(text);
    }
}

// Nitoryu looks for this export in every loaded plugin and calls it once, on the main loop, when all plugins
// are loaded. The load order of the two mods does not matter.
extern "C" __declspec(dllexport) void __cdecl NitoryuAddon_Init(const NitoryuApi* api)
{
    if (api == NULL || api->version < 1)
        return;
    g_nitoryu = api;
    char version[32] = "?";
    api->getVersion(version, sizeof(version));
    char categories[64] = "?";
    api->getSetting("nitoryu weapon categories", categories, sizeof(categories));
    int subscribed = api->subscribe("NitoryuAddonSample", NITORYU_EV_ALL, &onEvent, NULL);
    int rule = api->registerPairRule("NitoryuAddonSample", &pairRule, NULL);
    int filter = api->registerMoveFilter("NitoryuAddonSample", &moveFilter, NULL);
    char line[256];
    sprintf_s(line, "NitoryuAddonSample: Nitoryu %s, API %d (table of %d bytes); weapon categories '%s'; events %s, pair rule %s, move filter %s",
              version, api->version, api->size, categories, okText(subscribed), okText(rule), okText(filter));
    DebugLog(line);
}

// RE_Kenshi calls this when it loads the plugin. Nitoryu may not be loaded yet, so nothing is asked here.
__declspec(dllexport) void startPlugin()
{
    DebugLog("NitoryuAddonSample: loaded; waiting for Nitoryu.");
}
