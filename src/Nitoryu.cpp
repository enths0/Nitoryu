// Nitoryu: Dual Wielding Combat System. Two weapons on the back (or back +
// hip) make a fighter dual: the main hand keeps its vanilla moveset, the off
// hand adds strikes, and the stance costs guard, precision and strength until
// it is mastered. Balance: docs/BALANCE.md.
// Threads:
//   mainLoop_hook - the main loop. It drains the queued log lines first (kit/log.h), then runs every module's
//   tick. The main loop is the only thread that writes widgets, logs directly and reads the settings' text.

#include "kit/input.h"   // Windows.h with NOMINMAX first

#include "kit/log.h"
#include <core/Functions.h>

#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/util/hand.h>
#include <kenshi/Kenshi.h>
#include <kenshi/CharStats.h>
#include <kenshi/combat/CombatClass.h>
#include <kenshi/combat/CombatTechniqueData.h>

#include <cctype>
#include <cstdio>
#include <string>

#include "Combat.h"
#include "Stance.h"
#include "Diagnostics.h"
#include "Feedback.h"
#include "Visual.h"
#include "Arms.h"
#include "Defense.h"
#include "Pressure.h"
#include "Mastery.h"
#include "Blades.h"
#include "Dummy.h"
#include "Ambidex.h"
#include "Lefty.h"
#include "Foreign.h"
#include "SoundTrace.h"
#include "Lang.h"
#include "Ui.h"
#include "Hud.h"
#include "Balance.h"
#include "Settings.h"
#include "kit/diag.h"
#include "Hooks.h"
#include "Api.h"

namespace
{
    const char* VERSION = "1.2.0";
    // The counts line: every 30 s in stage (tests), every 2 min in prod, so a short session still logs its counts.
    const DWORD COUNTS_STAGE_MS = 30000;
    const DWORD COUNTS_PROD_MS = 120000;
    const DWORD CONSTANTS_EVERY_MS = 30000;
    DWORD g_lastConstants = 0;
    const unsigned PRUNE_AFTER_MS = 60000;

    // The compatibility line: after 60 s of play, one line with what did not fit.
    DWORD g_startTick = 0;
    bool g_compatDone = false;

    void compatLine()
    {
        if (g_compatDone || g_startTick == 0 || GetTickCount() - g_startTick < 60000)
            return;
        g_compatDone = true;
        KenshiLib::BinaryVersion v = KenshiLib::GetKenshiVersion();
        kit::Diag("compat").kv("platform", v.GetPlatform() == KenshiLib::BinaryVersion::STEAM ? "steam" : "other")
            .kv("version", v.GetVersion()).kv("layout", stance::knownLayout())
            .kv("shared_hooks", hooks::sharedCount()).kv("rigs_unsupported", arms::unsupportedCount())
            .kv("base_tracks_missing", combat::missingTrackCount()).emit();
    }

    bool g_toggleWasHeld = false;
    // No hotkey by default (Alt+Shift switches the Windows keyboard layout; the inventory button
    // stays). [UI] hotkey=alt+n / shift+n / alt+shift+n / n turns one on.
    int g_hotKey = 0;                   // virtual key, 0 = off
    bool g_hotAlt = false, g_hotShift = false;

    void readHotkey()
    {
        char buf[32] = { 0 };
        GetPrivateProfileStringA("UI", "hotkey", "", buf, sizeof(buf), stance::iniFile().c_str());
        std::string s(buf);
        for (size_t i = 0; i < s.size(); ++i)
            s[i] = (char)tolower((unsigned char)s[i]);
        g_hotAlt = s.find("alt") != std::string::npos;
        g_hotShift = s.find("shift") != std::string::npos;
        size_t plus = s.find_last_of('+');
        std::string key = plus == std::string::npos ? s : s.substr(plus + 1);
        g_hotKey = key.size() == 1 && isalnum((unsigned char)key[0]) ? toupper((unsigned char)key[0]) : 0;
    }

    // The load line names what it runs on, so one line of a player's log is enough: the game build and platform
    // (KenshiLib's check of the exe) and the KenshiLib and RE_Kenshi versions, taken from the lines they print
    // first in the same log (KenshiLib has no version call). "?" when a line is missing.
    std::string versionAfter(const std::string& log, const char* name)
    {
        std::string key = std::string(": ") + name + " ";
        size_t at = log.find(key);
        if (at == std::string::npos)
            return "?";
        at += key.size();
        size_t end = at;
        while (end < log.size() && (isdigit((unsigned char)log[end]) || log[end] == '.'))
            ++end;
        return end > at ? log.substr(at, end - at) : "?";
    }

    std::string identityText()
    {
        KenshiLib::BinaryVersion v = KenshiLib::GetKenshiVersion();
        std::string log = GetDebugLog();
        return " game=" + v.GetVersion() + " platform=" + v.GetPlatformStr() + " kenshilib="
               + versionAfter(log, "KenshiLib") + " re_kenshi=" + versionAfter(log, "RE_Kenshi") + ".";
    }

    std::string hotkeyText()
    {
        if (g_hotKey == 0)
            return "no hotkey";
        std::string t = std::string(g_hotAlt ? "Alt+" : "") + (g_hotShift ? "Shift+" : "");
        t += (char)g_hotKey;
        return t;
    }
    DWORD g_lastCounts = 0;
    HANDLE g_instance = NULL;

    bool kenshiFocused()
    {
        HWND w = GetForegroundWindow();
        DWORD pid = 0;
        if (w != NULL)
            GetWindowThreadProcessId(w, &pid);
        return pid == GetCurrentProcessId();
    }

    // DualWieldProbe (DualWieldCore.dll), an older plugin, hooks the same combat functions. Its preload
    // plugin loads even when its mod is not ticked, so this only leaves a line in the log.
    bool probeLoaded() { return GetModuleHandleA("DualWieldCore.dll") != NULL; }

    // One Nitoryu per game. Two installed copies (a local and a Workshop one) would hook everything
    // twice (a doubled speed factor, doubled damage scaling).
    bool firstCopy()
    {
        char name[64];
        sprintf_s(name, "Local\\Nitoryu_plugin_%lu", (unsigned long)GetCurrentProcessId());
        HANDLE h = CreateMutexA(NULL, FALSE, name);
        if (h == NULL)
            return true;                // cannot tell: run
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(h);
            return false;
        }
        g_instance = h;                 // held for the life of the game
        return true;
    }

    // Every feature off: its hooks pass through, blades and poses are taken back.
    void allOff()
    {
        combat::setEnabled(false);
        defense::setEnabled(false);
        pressure::setEnabled(false);
        mastery::setEnabled(false);
        blades::setEnabled(false);
        dummy::setEnabled(false);
        ambidex::setEnabled(false);
        lefty::setEnabled(false);
        visual::setEnabled(false);
        ui::setEnabled(false);
        hud::setEnabled(false);
    }

    // The load line lists the feature groups that run and those turned off.
    void feature(std::string& on, std::string& off, const char* name, bool ok)
    {
        std::string& to = ok ? on : off;
        to += (to.empty() ? "" : ",") + std::string(name);
        // A group off because its hooks failed (not one the player switched off in the ini) is reported.
        if (!ok && std::string(name).find("(ini)") == std::string::npos)
            feedback::featureOff(name, "its game hooks could not be installed (see the hook lines above)");
    }

    void toggleSelected()
    {
        Character* c = ou->player != NULL ? ou->player->selectedCharacter.getCharacter() : NULL;
        if (c == NULL || !c->isPlayerCharacter())
        {
            diag::count("toggle_no_selection");
            return;
        }
        bool on = stance::togglePlayer(c);
        feedback::say(c, lang::text(on ? "speech.two_blades" : "speech.one_blade").c_str(), 0);   // env=stage only
        kit::Diag("toggle").kv("who", c->displayName).kv("dual_allowed", on).emit();
    }

    void (*mainLoop_orig)(GameWorld*, float) = NULL;
    // Nitoryu's own cost against the game's frame time, one `cost` line with the counts: the frame
    // interval, our main-loop work, the pose engine (arms::takeCost).
    LARGE_INTEGER g_costFreq = { 0 }, g_lastFrame = { 0 };
    LONGLONG g_frameTicks = 0, g_mainTicks = 0, g_mainMaxTicks = 0;
    unsigned g_frames = 0;

    // Two things run on a 30 s timer (the slow tick and, at stage, the periodic lines), so each piece of the
    // per-frame work keeps its own worst time of the window; the `cost` line names those above SPAN_SHOWN_MS
    // as max_<piece>_ms (stage).
    enum Span
    {
        SP_LOG_DRAIN, SP_FEEDBACK, SP_COMPAT, SP_DEFENSE, SP_VISUAL, SP_HUD, SP_UI, SP_CONSTANTS, SP_SETTINGS,
        SP_FOREIGN, SP_OPT_IN, SP_TECHNIQUES, SP_PRELOAD, SP_PRUNE, SP_COUNTS, SP_HOOKS, SP_FIGHT, SP_COST,
        SP_COUNT
    };
    const char* SPAN_NAMES[SP_COUNT] = {
        "log_drain", "feedback", "compat", "defense", "visual", "hud", "ui", "constants", "settings",
        "foreign", "opt_in", "techniques", "preload", "prune", "counts", "hooks", "fight", "cost" };
    const double SPAN_SHOWN_MS = 0.1;
    LONGLONG g_spanMax[SP_COUNT] = { 0 };

    struct Timed
    {
        explicit Timed(Span sp) : sp_(sp), on_(diag::stage())
        {
            if (on_)
                QueryPerformanceCounter(&t0_);
        }
        ~Timed()
        {
            if (!on_)
                return;
            LARGE_INTEGER t1;
            QueryPerformanceCounter(&t1);
            if (t1.QuadPart - t0_.QuadPart > g_spanMax[sp_])
                g_spanMax[sp_] = t1.QuadPart - t0_.QuadPart;
        }
        Span sp_;
        bool on_;
        LARGE_INTEGER t0_;
    };

    // The line's own time (SP_COST) and the frame that wrote it end after the reset here, so both show in the
    // NEXT window's line.
    void emitCost()
    {
        double totalMs = 0.0, maxMs = 0.0;
        unsigned calls = 0;
        int peak = 0;
        arms::takeCost(totalMs, maxMs, calls, peak);
        if (g_frames > 0 && g_costFreq.QuadPart > 0 && diag::level() != diag::OFF)
        {
            double f = 1000.0 / (double)g_costFreq.QuadPart;
            double frame = g_frameTicks * f / g_frames, main = g_mainTicks * f / g_frames, pose = totalMs / g_frames;
            kit::Diag d("cost");
            d.kv("frames", (int)g_frames).kv("frame_ms", (float)frame).kv("main_ms", (float)main)
                .kv("main_max_ms", (float)(g_mainMaxTicks * f)).kv("pose_ms", (float)pose).kv("pose_max_ms", (float)maxMs)
                .kv("pose_updates_per_frame", (float)calls / g_frames).kv("fighters_peak", peak)
                .kv("share_pct", frame > 0.0 ? (float)((main + pose) * 100.0 / frame) : 0.0f);
            for (int i = 0; i < SP_COUNT; ++i)
                if (g_spanMax[i] * f >= SPAN_SHOWN_MS)
                    d.kv((std::string("max_") + SPAN_NAMES[i] + "_ms").c_str(), (float)(g_spanMax[i] * f));
            d.emit();
        }
        for (int i = 0; i < SP_COUNT; ++i)
            g_spanMax[i] = 0;
        g_frameTicks = g_mainTicks = g_mainMaxTicks = 0;
        g_frames = 0;
    }

    void mainLoopWork(GameWorld* self);

    const int TICK_FAULTS_OFF = 10;
    int g_tickFaults = 0;

    void tickFault(const char* what)
    {
        diag::count("tick_exception");
        if (++g_tickFaults == 1)
            kit::log::error(std::string("Nitoryu: an error in the per-frame work: ") + (what != NULL ? what : "?"));
        if (g_tickFaults == TICK_FAULTS_OFF)
        {
            allOff();
            feedback::featureOff("all features", "repeated errors in the per-frame work; the mod is off for this session");
        }
    }

    void mainLoop_hook(GameWorld* self, float time)
    {
        {
            Timed t(SP_LOG_DRAIN);
            kit::log::tick();   // lines queued by hooks on other threads are written here
        }
        mainLoop_orig(self, time);
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        if (g_costFreq.QuadPart == 0)
            QueryPerformanceFrequency(&g_costFreq);
        if (g_lastFrame.QuadPart != 0)
        {
            g_frameTicks += t0.QuadPart - g_lastFrame.QuadPart;
            ++g_frames;
        }
        g_lastFrame = t0;
        // An exception out of our per-frame work would end the game inside its own loop. Each is counted and
        // the first logged; after TICK_FAULTS_OFF of them the whole mod turns off (its ticks keep running to
        // put things back).
        try
        {
            mainLoopWork(self);
        }
        catch (const std::exception& e)
        {
            tickFault(e.what());
        }
        catch (...)
        {
            tickFault("unknown exception");
        }
        LARGE_INTEGER t1;
        QueryPerformanceCounter(&t1);
        g_mainTicks += t1.QuadPart - t0.QuadPart;
        if (t1.QuadPart - t0.QuadPart > g_mainMaxTicks)
            g_mainMaxTicks = t1.QuadPart - t0.QuadPart;
    }

    void mainLoopWork(GameWorld* self)
    {
        diag::setMainThread(GetCurrentThreadId());
        if (ou == NULL)
            return;
        bool held = g_hotKey != 0 && kit::held(g_hotKey) && kit::altHeld() == g_hotAlt
                    && kit::shiftHeld() == g_hotShift && kenshiFocused();
        if (held && !g_toggleWasHeld && combat::enabled())
            toggleSelected();
        g_toggleWasHeld = held;
        // Once a game runs (every plugin is in by then, also one that hooks late), is any of our detours now
        // inside another plugin's? Once per session.
        if (ou->player != NULL && ou->player->selectedCharacter.getCharacter() != NULL)
            hooks::checkOuter();
        { Timed t(SP_FEEDBACK); feedback::flush(); }
        { Timed t(SP_COMPAT); compatLine(); }
        { Timed t(SP_DEFENSE); defense::tick(); }
        { Timed t(SP_VISUAL); visual::tick(); }
        { Timed t(SP_HUD); hud::tick(); }       // the marks above the heads
        { Timed t(SP_UI); ui::tick(); }         // the hover text of the inventory buttons
        DWORD now = GetTickCount();
        if (g_lastConstants == 0 || now - g_lastConstants > CONSTANTS_EVERY_MS)
        {
            { Timed t(SP_CONSTANTS); defense::readConstants(); }    // after a load the record may have changed
            // The settings from FCS (GLOBAL CONSTANTS), the ini laid over them. A change (the first read, or
            // a load that brought another record) goes to every module that keeps a copy.
            LARGE_INTEGER settings0, settings1;
            QueryPerformanceCounter(&settings0);
            if (cfg::refresh())
            {
                balance::applySettings();
                stance::applySettings();
                defense::applySettings();
                pressure::applySettings();
                mastery::applySettings();
                hud::applySettings();
                blades::applySettings();
                dummy::applySettings();
                combat::applySettings();
                lefty::applySettings();
            }
            cfg::log();                     // once per change: `setting` lines and the `settings` summary
            QueryPerformanceCounter(&settings1);
            if (settings1.QuadPart - settings0.QuadPart > g_spanMax[SP_SETTINGS])
                g_spanMax[SP_SETTINGS] = settings1.QuadPart - settings0.QuadPart;
            { Timed t(SP_FOREIGN); foreign::applySettings(); }      // every slow tick (the other plugin may load after us)
            { Timed t(SP_OPT_IN); stance::refreshOptIn(); }         // the NPC opt-in list (ini + data)
            { Timed t(SP_TECHNIQUES); combat::logTechniques(); }    // once
            if (combat::enabled())
            {
                Timed t(SP_PRELOAD);
                arms::preload();            // the clip files, before the first draw needs them
            }
            { Timed t(SP_PRUNE); stance::prune(PRUNE_AFTER_MS); }
            g_lastConstants = now == 0 ? 1 : now;
        }
        if (now - g_lastCounts > (diag::stage() ? COUNTS_STAGE_MS : COUNTS_PROD_MS))
        {
            { Timed t(SP_COUNTS); diag::emitCounts(); }
            { Timed t(SP_HOOKS); diag::emitHooks(); }       // every hook's calls / ours / passed, session totals
            { Timed t(SP_FIGHT); combat::emitFight(); }     // stage only (empty in prod)
            { Timed t(SP_COST); emitCost(); }
            g_lastCounts = now;
        }
        api::tick();    // after the settings were read: the addons' init once, then the queued events
    }
}

static void startPluginBody()
{
    if (!firstCopy())
    {
        kit::log::error("Nitoryu: another Nitoryu is loaded (a local and a Workshop copy?); this copy stays off.");
        return;
    }
    cfg::init(stance::iniFile());       // the settings table and the ini; FCS joins after the game data loads
    stance::init();
    api::init();        // the lock of the plugin API, before the first hook can queue an event
    diag::configure(stance::iniFile());   // [General] diag
    balance::applySettings();           // the curves ([Balance] overrides, FCS)
    readHotkey();
    lang::init();
    feedback::init();
    visual::init();     // also configures the pose engine (arms)
    defense::init();
    pressure::init();
    mastery::init();    // flow (rank 4) and Mugen (rank 5)
    blades::init();     // pair bonuses, the scissor, the second enemy
    ambidex::init();
    lefty::init();
    ui::init();
    soundtrace::init(stance::iniFile());       // [Test] sound_trace
    hud::init();
    if (probeLoaded())
        kit::log::line("Nitoryu: DualWieldProbe (DualWieldCore.dll) is loaded too; both hook the same combat functions. If fights misbehave, remove one of them.");
    // Feature groups, each on only if all its hooks installed. Core (the main loop, the stance's
    // combat hooks, the character destructor) decides for the whole mod.
    std::string on, off;
    g_startTick = GetTickCount();
    bool core = hooks::add("GameWorld::mainLoop_GPUSensitiveStuff", "core", KenshiLib::GetRealAddress(&GameWorld::_NV_mainLoop_GPUSensitiveStuff),
                                   mainLoop_hook, &mainLoop_orig) == KenshiLib::SUCCESS;
    if (!core)
        kit::log::error("Nitoryu: could not hook GameWorld::mainLoop_GPUSensitiveStuff");
    core = combat::installHooks() && core;
    core = ambidex::installLifetimeHook() && core;
    if (!core)
    {
        allOff();
        kit::log::error("Nitoryu: core hooks failed; Nitoryu stays off.");
        kit::log::line(std::string("Nitoryu ") + VERSION + ": loaded, OFF (core hooks failed).");
        return;
    }
    feature(on, off, "core", true);
    // Raw layouts (the left-hand slot, the sheath scan, the button) on 1.0.65 only.
    bool layout = stance::knownLayout();
    if (!layout)
    {
        visual::setEnabled(false);
        kit::log::error(std::string("Nitoryu: game version ") + stance::gameVersion()
                 + ": the dual stance needs the 1.0.65 memory layout and stays off (left-hand fighting still runs).");
    }
    feature(on, off, "layout", layout);
    // Without the Ogre hook the left blade takes the hand slot.
    bool pose = arms::installHook();
    if (!pose)
    {
        visual::setPoseEngine(false);
        kit::log::error("Nitoryu: pose engine off; the second blade hangs in the left hand with a fixed grip.");
    }
    feature(on, off, "pose", pose);
    bool guard = defense::installHooks();
    if (!guard)
    {
        defense::setEnabled(false);
        mastery::setEnabled(false);     // flow counts attacks through the guard's hook
    }
    feature(on, off, "guard", guard);
    // Pressure counts impacts through the guard's _iHitYouAreYouHit hook.
    bool press = guard && pressure::installHooks();
    if (!press)
        pressure::setEnabled(false);
    feature(on, off, "pressure", press);
    // The scissor reads the body part of a wound; without the hook only the scissor is off.
    feature(on, off, "scissor", blades::installHooks());
    bool amb = ambidex::installHooks();
    if (!amb)
        ambidex::setEnabled(false);
    feature(on, off, "ambidex", amb);
    // The pair dummy trains the stat the Dual Wielding group keeps.
    bool dum = amb && dummy::installHooks();
    if (!dum)
        dummy::setEnabled(false);
    feature(on, off, "dummy", dum);
    bool left = lefty::installHooks();
    if (!left)
        lefty::setEnabled(false);
    feature(on, off, lefty::iniOn() ? "lefty" : "lefty(ini)", left && lefty::iniOn());
    soundtrace::installHooks();                 // a test hook, only with [Test] sound_trace=1
    bool button = ui::installHooks();
    if (!button)
        ui::setEnabled(false);
    feature(on, off, ui::iniOn() ? "ui" : "ui(ini)", button && ui::iniOn());
    feature(on, off, "marks", hud::installHooks());      // the marks on the portraits
    hooks::logSummary();                // one line for every hook row
    api::start(VERSION);                // other plugins may ask from the first frame on
    // Logged under `RE_Kenshi:` because startPlugin runs inside the loader. A std::string, not a fixed
    // buffer: the line grows with the clip list, and sprintf_s ends the process on an overflow.
    std::string test;
    if (stance::forcedTier() > 0 || stance::testShareSet())
    {
        char forced[96];
        sprintf_s(forced, " TEST SETTINGS ON: force_tier=%d follow_up_chance=%.2f.", stance::forcedTier(),
                  stance::offHandShare(0.0f));
        test += forced;
    }
    if (!ambidex::testText().empty())
        test += " TEST ambidexterity=" + ambidex::testText() + " (player characters).";
    if (!ambidex::testNpcText().empty())
        test += " TEST npc_ambidexterity=" + ambidex::testNpcText() + " (every NPC).";
    if (!balance::overridesText().empty())
        test += " [Balance] overrides: " + balance::overridesText() + ".";
    if (arms::clipText()[0] != '\0')
        test += std::string(" L3 TEST ON: ") + arms::clipText() + ".";
    kit::log::line(std::string("Nitoryu ") + VERSION + ": loaded. features on=" + on + " off=" + (off.empty() ? "-" : off)
             + ". Weapon categories: " + stance::allowListText() + ". The inventory button toggles the stance (" + hotkeyText() + ")." + test
             + (feedback::stage() ? " ENV=stage (test speech bubbles on)." : " ENV=prod.") + " diag=" + diag::levelName() + "."
             + identityText());
}

// RE_Kenshi calls startPlugin with no handler of its own, so an exception from the settings, the texts or a
// hook's install would close the game while plugins load. Every module's init runs before the first hook
// goes in, and allOff only clears flags, so after a catch the hooks already in (if any) see every feature
// off and pass the game's calls through.
__declspec(dllexport) void startPlugin()
{
    diag::init();
    const char* what = NULL;
    std::string text;
    try
    {
        startPluginBody();
        return;
    }
    catch (const std::exception& e)
    {
        text = e.what();
        what = text.c_str();
    }
    catch (...)
    {
        what = "unknown exception";
    }
    allOff();
    feedback::featureOff("all features", (std::string("an error while loading: ") + what).c_str());
    kit::log::line(std::string("Nitoryu ") + VERSION + ": loaded, OFF (an error while loading).");
}
