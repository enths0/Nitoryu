#pragma once

#include <string>

namespace diag
{
    enum Level { OFF = 0, NORMAL = 1, STAGE = 2, VERBOSE = 3 };

    void init();
    void configure(const std::string& iniFile);
    Level level();
    const char* levelName();
    bool stage();
    int budget(int base);

    void count(const char* reason);
    void countThread();
    void emitCounts();
    bool takeDetail();
    unsigned mainThread();
    void setMainThread(unsigned id);

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

    void emitHooks();
}
