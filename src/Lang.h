#pragma once

#include <string>

namespace lang
{
    void init();
    const std::string& text(const char* key);
    const std::string& code();
}
