#pragma once

#include <map>
#include <string>

#include "SettingsDef.h"

namespace cfg
{
    enum Id
    {
#define NITORYU_SETTING_ID(id, type, def, fcs, section, iniSection, iniKey, tip) id,
        NITORYU_SETTINGS(NITORYU_SETTING_ID)
#undef NITORYU_SETTING_ID
        COUNT
    };

    enum Source { DEFAULT, FCS, INI };

    const int LIST_MAX = 8;

    void init(const std::string& iniPath);

    bool refresh();
    bool recordFound();

    float f(Id id);
    double d(Id id);
    int i(Id id);
    bool b(Id id);
    float item(Id id, int index);
    int items(Id id);
    const std::string& text(Id id);
    const char* defaultText(Id id);
    Source source(Id id);
    const char* fcsKey(Id id);

    void log();

    int readIdValues(const std::string& iniPath, const char* section, const char* key, std::map<std::string, float>& out);
}
