#include "kit/input.h"

#include <Debug.h>
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
#include "Lang.h"
#include "Ui.h"
#include "Hud.h"
#include "Balance.h"
#include "Settings.h"
#include "kit/diag.h"

namespace
{
    const char* VERSION = "1.0.3";

    const DWORD COUNTS_STAGE_MS = 30000;
    const DWORD COUNTS_PROD_MS = 600000;
    const DWORD CONSTANTS_EVERY_MS = 30000;
    DWORD g_lastConstants = 0;
    const unsigned PRUNE_AFTER_MS = 60000;

    int g_sharedHooks = 0;

    void checkShared(intptr_t target, const char* name)
    {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(target);
        if (p == NULL || IsBadReadPtr(p, 2))
            return;
        if (p[0] == 0xE9 || (p[0] == 0xFF && p[1] == 0x25))
        {
            ++g_sharedHooks;
            kit::Diag("shared_hook").kv("target", name).kv("first_byte", (int)p[0]).emit();
        }
    }

    void checkSharedHooks()
    {
        checkShared(KenshiLib::GetRealAddress(&CharStats::setWeapon), "CharStats::setWeapon");
        checkShared(KenshiLib::GetRealAddress(&CharStats::_calculateWeaponWeightSpeedMultiplier), "CharStats::_calculateWeaponWeightSpeedMultiplier");
        checkShared(KenshiLib::GetRealAddress(&CharStats::calculateAttackOrBlockSpeed), "CharStats::calculateAttackOrBlockSpeed");
        checkShared(KenshiLib::GetRealAddress(&CharStats::getMeleeAttack), "CharStats::getMeleeAttack");
        checkShared(KenshiLib::GetRealAddress(&CharStats::_calculateBlockChance), "CharStats::_calculateBlockChance");
        checkShared(KenshiLib::GetRealAddress(&CharStats::_NV_getEquippedWeaponSkill), "CharStats::getEquippedWeaponSkill");
        checkShared(KenshiLib::GetRealAddress(&CharStats::chooseAttack), "CharStats::chooseAttack");
        checkShared(KenshiLib::GetRealAddress(&CharStats::serialise), "CharStats::serialise");
        checkShared(KenshiLib::GetRealAddress(&CharStats::updateStats), "CharStats::updateStats");
        checkShared(KenshiLib::GetRealAddress(&CharStats::xpMelee), "CharStats::xpMelee");
        checkShared(KenshiLib::GetRealAddress(&Character::_NV_hitByMeleeAttack), "Character::hitByMeleeAttack");
        checkShared(KenshiLib::GetRealAddress(&Character::_DESTRUCTOR), "Character::~Character");
        checkShared(KenshiLib::GetRealAddress(&CombatClass::_iHitYouAreYouHit), "CombatClass::_iHitYouAreYouHit");
        checkShared(KenshiLib::GetRealAddress(&CombatClass::initialiseBlock), "CombatClass::initialiseBlock");
        checkShared(KenshiLib::GetRealAddress(&CombatClassAI::_NV_whoAttacksYouOrMe), "CombatClassAI::whoAttacksYouOrMe");
        checkShared(KenshiLib::GetRealAddress(&CombatTechniqueData::_CONSTRUCTOR), "CombatTechniqueData::CombatTechniqueData");
        checkShared(KenshiLib::GetRealAddress(&GameWorld::_NV_mainLoop_GPUSensitiveStuff), "GameWorld::mainLoop_GPUSensitiveStuff");
    }

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
            .kv("shared_hooks", g_sharedHooks).kv("rigs_unsupported", arms::unsupportedCount())
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

    void mainLoop_hook(GameWorld* self, float time)
    {
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
        mainLoopWork(self);
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
            combat::emitFight();
            emitCost();
            g_lastCounts = now;
        }
    }
}

__declspec(dllexport) void startPlugin()
{
    diag::init();
    if (!firstCopy())
    {
        ErrorLog("Nitoryu: another Nitoryu is loaded (a local and a Workshop copy?); this copy stays off.");
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
    hud::init();
    if (probeLoaded())
        DebugLog("Nitoryu: DualWieldProbe (DualWieldCore.dll) is loaded too; both hook the same combat functions. If fights misbehave, remove one of them.");

    std::string on, off;
    checkSharedHooks();
    g_startTick = GetTickCount();
    bool core = KenshiLib::AddHook(KenshiLib::GetRealAddress(&GameWorld::_NV_mainLoop_GPUSensitiveStuff),
                                   mainLoop_hook, &mainLoop_orig) == KenshiLib::SUCCESS;
    if (!core)
        ErrorLog("Nitoryu: could not hook GameWorld::mainLoop_GPUSensitiveStuff");
    core = combat::installHooks() && core;
    core = ambidex::installLifetimeHook() && core;
    if (!core)
    {
        allOff();
        ErrorLog("Nitoryu: core hooks failed; Nitoryu stays off.");
        DebugLog(std::string("Nitoryu ") + VERSION + ": loaded, OFF (core hooks failed).");
        return;
    }
    feature(on, off, "core", true);

    bool layout = stance::knownLayout();
    if (!layout)
    {
        visual::setEnabled(false);
        ErrorLog(std::string("Nitoryu: game version ") + stance::gameVersion()
                 + ": the dual stance needs the 1.0.65 memory layout and stays off (left-hand fighting still runs).");
    }
    feature(on, off, "layout", layout);

    bool pose = arms::installHook();
    if (!pose)
    {
        visual::setPoseEngine(false);
        ErrorLog("Nitoryu: pose engine off; the second blade hangs in the left hand with a fixed grip.");
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

    bool button = ui::installHooks();
    if (!button)
        ui::setEnabled(false);
    feature(on, off, ui::iniOn() ? "ui" : "ui(ini)", button && ui::iniOn());
    feature(on, off, "marks", hud::installHooks());

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
    DebugLog(std::string("Nitoryu ") + VERSION + ": loaded. features on=" + on + " off=" + (off.empty() ? "-" : off)
             + ". Weapon categories: " + stance::allowListText() + ". The inventory button toggles the stance (" + hotkeyText() + ")." + test
             + (feedback::stage() ? " ENV=stage (test speech bubbles on)." : " ENV=prod.") + " diag=" + diag::levelName() + ".");
}
