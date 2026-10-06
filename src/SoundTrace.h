#pragma once

#include <string>

class Character;

namespace soundtrace
{
    void init(const std::string& iniFile);
    bool on();
    bool installHooks();

    void impact(Character* victim, Character* attacker, int result, int comboID, const std::string& move, bool ours);
}
