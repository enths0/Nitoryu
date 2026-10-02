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
}
