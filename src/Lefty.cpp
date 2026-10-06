#include "kit/input.h"

#include "kit/log.h"
#include <core/Functions.h>

#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/Inventory.h>
#include <kenshi/Gear.h>
#include <kenshi/util/hand.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "Lefty.h"
#include "Ambidex.h"
#include "Stance.h"
#include "Settings.h"
#include "Diagnostics.h"
#include "kit/diag.h"
#include "kit/rtti.h"
#include "Hooks.h"

namespace
{
    const unsigned CACHE_MS = 500;

    volatile bool g_enabled = true;

    bool g_on = true;
    float g_minA = 50.0f;
    float g_dmgMin = 0.7f;
    float g_speedMin = 0.85f;
    float g_blockMin = 10.0f;

    struct Cached
    {
        DWORD tick;
        bool ok;
        float a;
    };

    CRITICAL_SECTION g_lock;
    std::map<Character*, Cached> g_cache;
    std::vector<hand> g_noted;

    struct Lock
    {
        Lock() { EnterCriticalSection(&g_lock); }
        ~Lock() { LeaveCriticalSection(&g_lock); }
    };

    bool oneHandedAllowed(Weapon* w)
    {
        return w != NULL && stance::weaponAllowed(w) && !w->is2HandedOnly();
    }

    bool carriesOneHanded(Character* c)
    {
        Inventory* inv = c->getInventory();
        if (inv == NULL)
            return false;
        const char* sections[2] = { "back", "hip" };
        for (int s = 0; s < 2; ++s)
        {
            InventorySection* sec = inv->getSection(std::string(sections[s]));
            if (sec == NULL)
                continue;
            const Ogre::vector<InventorySection::SectionItem>::type& items = sec->getItems();
            for (size_t i = 0; i < items.size(); ++i)
                if (items[i].item != NULL && oneHandedAllowed(items[i].item->isWeapon()))
                    return true;
        }
        return false;
    }

    bool compute(Character* c, float& a)
    {
        a = 0.0f;
        if (!g_enabled || !g_on || c == NULL)
            return false;

        if (stance::rigOf(c) != stance::RIG_AUTO)
            return false;
        MedicalSystem* med = c->getMedical();
        if (med == NULL || med->isRightArmOk() || !med->isLeftArmOk())
            return false;
        if (c->isPlayerCharacter() && !stance::playerAllowed(c))
            return false;
        a = ambidex::effective(c).eff;
        if (a < g_minA)
        {
            diag::count("lefty_ambidexterity_low");
            return false;
        }
        return carriesOneHanded(c);
    }

    float progress(float a)
    {
        float t = g_minA < 100.0f ? (a - g_minA) / (100.0f - g_minA) : 1.0f;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    bool cachedA(Character* c, float& a)
    {
        DWORD now = GetTickCount();
        {
            Lock l;
            std::map<Character*, Cached>::iterator it = g_cache.find(c);
            if (it != g_cache.end() && now - it->second.tick < CACHE_MS)
            {
                a = it->second.a;
                return it->second.ok;
            }
        }
        bool ok = compute(c, a);
        Lock l;
        if (g_cache.size() > 2048)
            g_cache.clear();
        Cached& e = g_cache[c];
        e.tick = now;
        e.ok = ok;
        e.a = a;
        return ok;
    }

    struct ArmOverride
    {
        bool* flag;
        bool saved;
        explicit ArmOverride(bool* f) : flag(f), saved(*f) { *f = true; }
        ~ArmOverride() { *flag = saved; }
    };

    void (*choose_orig)(void*, Character*) = NULL;
    void choose_hook(void* task, Character* c)
    {
        diag::HookCall hc(diag::HK_MELEE_TASK);
        float a = 0.0f;
        if (c == NULL || !cachedA(c, a))
        {
            choose_orig(task, c);
            return;
        }
        hc.ours();
        {
            ArmOverride arm(&c->medical.rightArmOk);
            choose_orig(task, c);
        }
        diag::count("lefty_weapon_choice");
        Lock l;
        if (g_noted.size() < 256)
            g_noted.push_back(c->getHandle());
    }
}

namespace lefty
{
    void forget(Character* c)
    {
        Lock l;
        g_cache.erase(c);
    }

    void init()
    {
        InitializeCriticalSection(&g_lock);
        applySettings();
    }

    void applySettings()
    {
        g_on = cfg::b(cfg::LEFTY_ON);
        g_minA = cfg::f(cfg::LEFTY_MIN_AMBI);
        g_dmgMin = cfg::f(cfg::LEFTY_DAMAGE_AT_MIN);
        g_speedMin = cfg::f(cfg::LEFTY_SPEED_AT_MIN);
        g_blockMin = cfg::f(cfg::LEFTY_BLOCK_AT_MIN);
        Lock l;
        g_cache.clear();
    }

    bool installHooks()
    {

        if (!g_on)
            return true;
        void** vt = kit::findVtable(".?AVTask_MeleeAttack@@");
        void** vtFocused = kit::findVtable(".?AVTask_FocusedMeleeAttack@@");
        void* target = vt != NULL ? kit::followThunk(vt[5]) : NULL;
        void* focused = vtFocused != NULL ? kit::followThunk(vtFocused[5]) : NULL;

        const unsigned char armDisp[4] = { 0xBD, 0x05, 0x00, 0x00 };
        bool looksRight = target != NULL && kit::codeContains(target, 0x80, armDisp, 4);
        char where[160];
        sprintf_s(where, "melee task weapon choice: %p (RVA %#llx), focused task %s, arm check %s",
                  target, target != NULL ? (unsigned long long)((char*)target - (char*)GetModuleHandleA(NULL)) : 0ULL,
                  focused == target ? "same" : "DIFFERENT", looksRight ? "found" : "NOT FOUND");
        kit::Diag("lefty_config").kv("text", where).kv("min_ambidexterity", g_minA).emit();
        if (!looksRight)
        {
            kit::log::error("Nitoryu: left-hand fighting is off: the melee task's weapon choice was not recognised on this game build");
            return false;
        }

        if (hooks::add("Task_MeleeAttack (vtable slot 5)", "lefty", target, reinterpret_cast<void*>(choose_hook), reinterpret_cast<void**>(&choose_orig))
            != KenshiLib::SUCCESS)
        {
            kit::log::error("Nitoryu: could not hook the melee task's weapon choice; no left-hand fighting");
            return false;
        }
        return true;
    }

    void setEnabled(bool on) { g_enabled = on; }
    bool iniOn() { return g_on; }

    bool qualifies(Character* c)
    {
        float a = 0.0f;
        return c != NULL && cachedA(c, a);
    }

    bool active(Character* c)
    {
        return qualifies(c) && oneHandedAllowed(c->getCurrentWeapon());
    }

    float damageFactor(Character* c)
    {
        float a = 0.0f;
        if (c == NULL || !cachedA(c, a))
            return 1.0f;
        return g_dmgMin + (1.0f - g_dmgMin) * progress(a);
    }

    float speedFactor(Character* c)
    {
        float a = 0.0f;
        if (c == NULL || !cachedA(c, a))
            return 1.0f;
        return g_speedMin + (1.0f - g_speedMin) * progress(a);
    }

    float blockPenalty(Character* c)
    {
        float a = 0.0f;
        if (c == NULL || !cachedA(c, a))
            return 0.0f;
        return g_blockMin * (1.0f - progress(a));
    }

    void takeNoted(void (*fn)(Character*))
    {
        std::vector<hand> noted;
        {
            Lock l;
            noted.swap(g_noted);
        }
        for (size_t i = 0; i < noted.size(); ++i)
        {
            Character* c = noted[i].getCharacter();
            if (c != NULL)
                fn(c);
        }
    }
}
