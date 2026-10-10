#include "kit/input.h"

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

namespace
{
    const char* VERSION = "1.0.6";

    const DWORD COUNTS_STAGE_MS = 30000;
    const DWORD COUNTS_PROD_MS = 120000;
    const DWORD CONSTANTS_EVERY_MS = 30000;
    DWORD g_lastConstants = 0;
    const unsigned PRUNE_AFTER_MS = 60000;

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

    int g_hotKey = 0;
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

    bool probeLoaded() { return GetModuleHandleA("DualWieldCore.dll") != NULL; }

    bool firstCopy()
    {
        char name[64];
        sprintf_s(name, "Local\\Nitoryu_plugin_%lu", (unsigned long)GetCurrentProcessId());
        HANDLE h = CreateMutexA(NULL, FALSE, name);
        if (h == NULL)
            return true;
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(h);
            return false;
        }
        g_instance = h;
        return true;
    }

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

    void feature(std::string& on, std::string& off, const char* name, bool ok)
    {
        std::string& to = ok ? on : off;
        to += (to.empty() ? "" : ",") + std::string(name);

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
        feedback::say(c, lang::text(on ? "speech.two_blades" : "speech.one_blade").c_str(), 0);
        kit::Diag("toggle").kv("who", c->displayName).kv("dual_allowed", on).emit();
    }

    void (*mainLoop_orig)(GameWorld*, float) = NULL;

    LARGE_INTEGER g_costFreq = { 0 }, g_lastFrame = { 0 };
    LONGLONG g_frameTicks = 0, g_mainTicks = 0, g_mainMaxTicks = 0;
    unsigned g_frames = 0;

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
            kit::Diag("cost").kv("frames", (int)g_frames).kv("frame_ms", (float)frame).kv("main_ms", (float)main)
                .kv("main_max_ms", (float)(g_mainMaxTicks * f)).kv("pose_ms", (float)pose).kv("pose_max_ms", (float)maxMs)
                .kv("pose_updates_per_frame", (float)calls / g_frames).kv("fighters_peak", peak)
                .kv("share_pct", frame > 0.0 ? (float)((main + pose) * 100.0 / frame) : 0.0f).emit();
        }
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
        kit::log::tick();
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

        if (ou->player != NULL && ou->player->selectedCharacter.getCharacter() != NULL)
            hooks::checkOuter();
        feedback::flush();
        compatLine();
        defense::tick();
        visual::tick();
        hud::tick();
        ui::tick();
        DWORD now = GetTickCount();
        if (g_lastConstants == 0 || now - g_lastConstants > CONSTANTS_EVERY_MS)
        {
            defense::readConstants();

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
            cfg::log();
            foreign::applySettings();
            stance::refreshOptIn();
            combat::logTechniques();
            if (combat::enabled())
                arms::preload();
            stance::prune(PRUNE_AFTER_MS);
            g_lastConstants = now == 0 ? 1 : now;
        }
        if (now - g_lastCounts > (diag::stage() ? COUNTS_STAGE_MS : COUNTS_PROD_MS))
        {
            diag::emitCounts();
            diag::emitHooks();
            combat::emitFight();
            emitCost();
            g_lastCounts = now;
        }
    }
}

static void startPluginBody()
{
    if (!firstCopy())
    {
        kit::log::error("Nitoryu: another Nitoryu is loaded (a local and a Workshop copy?); this copy stays off.");
        return;
    }
    cfg::init(stance::iniFile());
    stance::init();
    diag::configure(stance::iniFile());
    balance::applySettings();
    readHotkey();
    lang::init();
    feedback::init();
    visual::init();
    defense::init();
    pressure::init();
    mastery::init();
    blades::init();
    ambidex::init();
    lefty::init();
    ui::init();
    soundtrace::init(stance::iniFile());
    hud::init();
    if (probeLoaded())
        kit::log::line("Nitoryu: DualWieldProbe (DualWieldCore.dll) is loaded too; both hook the same combat functions. If fights misbehave, remove one of them.");

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

    bool layout = stance::knownLayout();
    if (!layout)
    {
        visual::setEnabled(false);
        kit::log::error(std::string("Nitoryu: game version ") + stance::gameVersion()
                 + ": the dual stance needs the 1.0.65 memory layout and stays off (left-hand fighting still runs).");
    }
    feature(on, off, "layout", layout);

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
        mastery::setEnabled(false);
    }
    feature(on, off, "guard", guard);

    bool press = guard && pressure::installHooks();
    if (!press)
        pressure::setEnabled(false);
    feature(on, off, "pressure", press);

    feature(on, off, "scissor", blades::installHooks());
    bool amb = ambidex::installHooks();
    if (!amb)
        ambidex::setEnabled(false);
    feature(on, off, "ambidex", amb);

    bool dum = amb && dummy::installHooks();
    if (!dum)
        dummy::setEnabled(false);
    feature(on, off, "dummy", dum);
    bool left = lefty::installHooks();
    if (!left)
        lefty::setEnabled(false);
    feature(on, off, lefty::iniOn() ? "lefty" : "lefty(ini)", left && lefty::iniOn());

    soundtrace::installHooks();
    bool button = ui::installHooks();
    if (!button)
        ui::setEnabled(false);
    feature(on, off, ui::iniOn() ? "ui" : "ui(ini)", button && ui::iniOn());
    feature(on, off, "marks", hud::installHooks());
    hooks::logSummary();

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
