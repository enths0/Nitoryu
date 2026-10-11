// Another plugin drives the body (Foreign.h).
//
// Such a plugin plays a combat animation of the character's AnimationClass outside the game's combat
// (combatState COMBAT_FINISHED, combat not active). The game's own lists say what plays:
// AnimationClassBase::layer -> AnimationLayer::addList -> SingleAnimation, with isACombatAnimation /
// isABlockAnimation and its weight.
// Threads:
//   No hook of its own: the main loop watches the other plugin's animations; combat threads ask blockHeld().
//   g_blocking.cs guards g_blocking.who.

#include "kit/input.h"   // Windows.h with NOMINMAX first

#include <kenshi/Character.h>
#include <kenshi/Enums.h>
#include <kenshi/combat/CombatClass.h>
#include <kenshi/Animation/AnimationClass.h>

#include <algorithm>
#include <set>
#include <string>

#include "Foreign.h"
#include "Settings.h"
#include "Diagnostics.h"
#include "kit/diag.h"

namespace
{
    enum GateMode { GATE_OFF, GATE_AUTO, GATE_ON };

    const float PLAYING_WEIGHT = 0.05f;     // below this an animation is fading in or out: not "playing"

    GateMode g_mode = GATE_AUTO;
    bool g_active = false;
    std::string g_plugin;                   // the loaded plugin auto found, "" when none
    double g_holdMs = 6000.0;
    double g_runMs = 300.0;
    // `foreign_action` lines have their own small budget; the counters go on.
    int g_linesLeft = 20;
    volatile LONG g_hitLinesLeft = 80;      // `incoming_hit` lines per session

    // The characters whose foreign block is up: written by the main loop, read by the combat threads.
    struct Blocking
    {
        CRITICAL_SECTION cs;
        std::set<Character*> who;       // compared, never dereferenced
        Blocking() { InitializeCriticalSection(&cs); }
    } g_blocking;

    void setBlockUp(Character* c, foreign::Watch& w, bool up)
    {
        if (w.blockUp == up)
            return;
        w.blockUp = up;
        EnterCriticalSection(&g_blocking.cs);
        if (up)
            g_blocking.who.insert(c);
        else
            g_blocking.who.erase(c);
        LeaveCriticalSection(&g_blocking.cs);
    }

    std::string trimmed(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t");
        size_t b = s.find_last_not_of(" \t");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    }

    // The combat or block animation playing on any layer, heaviest first. NULL: none.
    AnimationClassBase::SingleAnimation* combatAnimation(AnimationClassBase* a)
    {
        AnimationClassBase::SingleAnimation* best = NULL;
        for (uint32_t i = 0; i < a->layer.size(); ++i)
        {
            AnimationClassBase::AnimationLayer* l = a->layer[i];
            if (l == NULL)
                continue;
            for (uint32_t k = 0; k < l->addList.size(); ++k)
            {
                AnimationClassBase::SingleAnimation* s = l->addList[k];
                if (s == NULL || s->weight < PLAYING_WEIGHT)
                    continue;
                if (!s->isACombatAnimation() && !s->isABlockAnimation())
                    continue;
                if (best == NULL || s->weight > best->weight)
                    best = s;
            }
        }
        return best;
    }
}

namespace foreign
{
    void applySettings()
    {
        const std::string& m = cfg::text(cfg::FOREIGN_MODE);
        GateMode mode = m == "off" || m == "0" ? GATE_OFF : (m == "on" || m == "1" ? GATE_ON : GATE_AUTO);
        std::string found;
        if (mode == GATE_AUTO)
        {
            const std::string& list = cfg::text(cfg::FOREIGN_PLUGINS);
            size_t at = 0;
            while (at <= list.size() && found.empty())
            {
                size_t comma = list.find(',', at);
                std::string name = trimmed(list.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
                if (!name.empty() && GetModuleHandleA(name.c_str()) != NULL)
                    found = name;
                if (comma == std::string::npos)
                    break;
                at = comma + 1;
            }
        }
        bool active = mode == GATE_ON || (mode == GATE_AUTO && !found.empty());
        double holdMs = (double)std::max(0, cfg::i(cfg::FOREIGN_HOLD_MS));
        double runMs = (double)std::max(0, cfg::i(cfg::FOREIGN_RUN_MS));
        bool changed = mode != g_mode || active != g_active || found != g_plugin || holdMs != g_holdMs || runMs != g_runMs;
        static bool logged = false;
        g_mode = mode;
        g_active = active;
        g_plugin = found;
        g_holdMs = holdMs;
        g_runMs = runMs;
        if (changed || !logged)
        {
            logged = true;
            kit::Diag("foreign_config").kv("mode", mode == GATE_OFF ? "off" : (mode == GATE_ON ? "on" : "auto"))
                .kv("active", active).kv("plugin", found.empty() ? "-" : found).kv("hold_ms", holdMs).kv("run_ms", runMs).emit();
        }
    }

    bool active() { return g_active; }

    bool actionNow(Character* c, CombatClass* cc, Watch& w, double nowMs)
    {
        if (!g_active || cc == NULL || cc->animation == NULL)
        {
            w.anim = NULL;
            setBlockUp(c, w, false);
            return false;
        }
        AnimationClassBase::SingleAnimation* s = combatAnimation(cc->animation);
        if (s == NULL)
        {
            w.anim = NULL;
            setBlockUp(c, w, false);
            return false;
        }
        // a new animation: another object, or the same address played again from its start
        bool fresh = s != w.anim || s->currentFrameTime01 + 0.2f < w.time01;
        if (fresh)
        {
            w.startedForeign = cc->combatState == COMBAT_FINISHED && !cc->_isInCombatMode();
            diag::count(w.startedForeign ? "foreign_action_start" : "foreign_native_start");
            if (w.startedForeign && g_linesLeft > 0 && diag::takeDetail())
            {
                --g_linesLeft;
                kit::Diag("foreign_action").kv("who", c->displayName).kv("anim", s->animName)
                    .kv("block", s->isABlockAnimation()).kv("weight", s->weight).kv("plugin", g_plugin.empty() ? "-" : g_plugin).emit();
            }
        }
        w.anim = s;
        w.time01 = s->currentFrameTime01;
        setBlockUp(c, w, w.startedForeign && s->isABlockAnimation());
        if (!w.startedForeign)
            return false;
        w.until = nowMs + g_holdMs;
        diag::count("foreign_action_frame");
        return true;
    }

    bool blockHeld(Character* c)
    {
        EnterCriticalSection(&g_blocking.cs);
        bool up = g_blocking.who.find(c) != g_blocking.who.end();
        LeaveCriticalSection(&g_blocking.cs);
        return up;
    }

    void noteIncoming(Character* victim, const std::string& attacker, const std::string& tech, int combo,
                      int result, bool landed, float cutIn, float bluntIn, float cutLeft, float bluntLeft)
    {
        bool held = blockHeld(victim);
        diag::count(held ? (landed ? "foreign_block_up_hit_landed" : (result == (int)HIT_SWORD ? "foreign_block_up_hit_blocked"
                                                                         : "foreign_block_up_hit_missed"))
                         : (landed ? "foreign_block_down_hit_landed" : (result == (int)HIT_SWORD ? "foreign_block_down_hit_blocked"
                                                                         : "foreign_block_down_hit_missed")));
        if (InterlockedDecrement(&g_hitLinesLeft) < 0)
            return;
        kit::Diag("incoming_hit").kv("victim", victim->displayName).kv("attacker", attacker).kv("tech", tech)
            .kv("combo", combo).kv("result", result).kv("landed", landed).kv("block_up", held)
            .kv("cut_in", cutIn).kv("blunt_in", bluntIn).kv("cut_left", cutLeft).kv("blunt_left", bluntLeft).emit();
    }

    bool playing(const Watch& w)
    {
        return g_active && w.anim != NULL && w.startedForeign;
    }

    bool driven(const Watch& w, double nowMs)
    {
        return g_active && w.until > 0.0 && nowMs < w.until;
    }

    bool running(bool running, Watch& w, double nowMs)
    {
        if (!running)
        {
            w.runSince = 0.0;
            return false;
        }
        if (w.runSince <= 0.0)
            w.runSince = nowMs;
        if (!driven(w, nowMs) || nowMs - w.runSince >= g_runMs)
            return true;
        diag::count("foreign_run_hold_waits");
        return false;
    }
}
