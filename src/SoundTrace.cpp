#include "kit/input.h"

#include "kit/log.h"
#include <core/Functions.h>

#include <kenshi/Character.h>

#include <cstring>
#include <string>

#include "SoundTrace.h"
#include "Diagnostics.h"
#include "kit/diag.h"
#include "Hooks.h"

namespace
{
    bool g_on = false;
    volatile LONG g_soundLines = 0, g_impactLines = 0;
    const LONG SOUND_LINES = 1500, IMPACT_LINES = 600;

    std::string nameOf(Character* c) { return c != NULL ? c->displayName : std::string("<null>"); }

    bool (*audioEvent_orig)(Character*, const char*, SoundRange) = NULL;
    bool audioEvent_hook(Character* self, const char* name, SoundRange range)
    {
        bool ok = audioEvent_orig(self, name, range);
        diag::count("sound_calls");

        if (name != NULL && (strcmp(name, "Footsteps") == 0 || strncmp(name, "VO_Creature_Passive", 19) == 0))
        {
            diag::count("sound_steps_and_idle");
            return ok;
        }
        if (InterlockedIncrement(&g_soundLines) <= SOUND_LINES)
            kit::Diag("sound").kv("t", (int)GetTickCount()).kv("who", nameOf(self)).kv("name", name != NULL ? name : "<null>")
                .kv("range", (int)range).kv("ok", ok).emit();
        return ok;
    }
}

namespace soundtrace
{
    void init(const std::string& iniFile)
    {
        g_on = GetPrivateProfileIntA("Test", "sound_trace", 0, iniFile.c_str()) != 0;
    }

    bool on() { return g_on; }

    bool installHooks()
    {
        if (!g_on)
            return true;
        if (hooks::add("Character::audioEvent", "test", KenshiLib::GetRealAddress(&Character::audioEvent), audioEvent_hook,
                       &audioEvent_orig) != KenshiLib::SUCCESS)
        {
            kit::log::error("Nitoryu: could not hook Character::audioEvent (no sound trace)");
            g_on = false;
            return false;
        }
        kit::Diag("sound_trace").kv("on", true).emit();
        return true;
    }

    void impact(Character* victim, Character* attacker, int result, int comboID, const std::string& move, bool ours)
    {
        if (!g_on)
            return;
        diag::count("sound_impacts");
        if (InterlockedIncrement(&g_impactLines) <= IMPACT_LINES)
            kit::Diag("impact").kv("t", (int)GetTickCount()).kv("attacker", nameOf(attacker)).kv("victim", nameOf(victim))
                .kv("result", result).kv("combo", comboID).kv("move", move).kv("ours", ours).emit();
    }
}
