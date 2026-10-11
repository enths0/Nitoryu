// Thread-safe per-reason counters: combat hooks count on the combat thread, the main loop emits them.
//
// How much the log gets is one ini switch, `[General] diag`, read once at load:
//   off     - no counts, no detail lines (the load, compat and error lines stay)
//   normal  - counts and the `hooks` line every 2 min, detail lines within their budgets (the release)
//   stage   - normal + per-frame counters, the `fight` ledger, counts every 30 s, 20 times the detail budgets
//   verbose - stage with 100 times the detail budgets
// Unset: stage when `env=stage`, else normal. `env` itself only decides the speech bubbles (Feedback.h).
#pragma once

#include <string>

namespace diag
{
    enum Level { OFF = 0, NORMAL = 1, STAGE = 2, VERBOSE = 3 };

    void init();
    void configure(const std::string& iniFile);   // after stance::init, before the modules' init
    Level level();
    const char* levelName();
    bool stage();                       // level() >= STAGE
    int budget(int base);               // a module's detail budget at this level

    void count(const char* reason);     // nothing when off
    void countThread();                 // "thread_<id>" once per call site thread
    void emitCounts();                  // one `counts ...` line, then clear
    bool takeDetail();                  // budget for detailed lines
    unsigned mainThread();
    void setMainThread(unsigned id);

    // Every hook counts its calls and how each one ended, with no lock (interlocked totals, cheap enough for
    // the per-frame hooks):
    //   ours    - the call went past the hook's cheap checks into Nitoryu's own logic;
    //   passed  - it went straight to the game's original;
    //   thrown  - an exception left the hook (the game's own or ours), caught somewhere above it.
    // calls = ours + passed + thrown + open, where open is a call still inside a hook at that moment (another
    // thread, or the hooks nested: chooseAttack inside setWeapon). An `open` that grows from one `hooks` line to
    // the next names a hook whose calls never return. Totals are kept for the whole session.
    enum Hook
    {
        HK_SET_WEAPON, HK_WEIGHT_SPEED, HK_ATTACK_SPEED, HK_MELEE_ATTACK, HK_BLOCK_CHANCE,
        HK_EQUIPPED_SKILL, HK_CHOOSE_ATTACK, HK_HIT_BY, HK_I_HIT, HK_INIT_BLOCK, HK_SERIALISE, HK_UPDATE_STATS,
        HK_CHAR_DTOR, HK_INCREASE_STAT, HK_XP_MELEE, HK_XP_TRAINING, HK_OPERATE, HK_STATS_WINDOW, HK_LINE_RESIZE,
        HK_REFRESH_SECTION, HK_CELL_UPDATE, HK_WHO_ATTACKS, HK_TECH_CTOR, HK_ADD_WOUND, HK_UPDATE_TRANSFORMS,
        HK_MELEE_TASK,
        HK_COUNT
    };

    class HookCall
    {
    public:
        explicit HookCall(Hook h);
        ~HookCall();
        void ours() { ours_ = true; }
    private:
        Hook h_;
        bool ours_;
    };

    // One `hooks name=calls/ours/passed[/thrown]@thread[ name_open=n] ...` line; @main / @other / @both:<calls off
    // the main loop's thread> says where the hook runs.
    void emitHooks();
}
