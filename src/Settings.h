// Nitoryu's settings at run time (docs/FCS.md).
//
// One value per row of SettingsDef.h, from three sources:
//   the default (the table) < FCS (a field of the CONSTANTS record "GLOBAL CONSTANTS", so a mod or a modpack
//   ships its own values) < Nitoryu.ini (a key that is present: the player's own override).
// The ini is read at start; the record after the game data loads and again on the main loop's slow tick
// (a load may bring another record). Numbers and list items are plain values the combat threads may read
// while the main loop writes them; text is for the main loop only.
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

    const int LIST_MAX = 8;                 // the longest list (divided guard: 6)

    void init(const std::string& iniPath);  // the defaults, then the ini; before every module's init
    // Main loop: reads GLOBAL CONSTANTS and lays the ini over it. True when a value changed (the modules then
    // re-apply their settings) or the record was found for the first time.
    bool refresh();
    bool recordFound();

    float f(Id id);                         // F, I, B as a number
    double d(Id id);
    int i(Id id);
    bool b(Id id);
    float item(Id id, int index);           // L: its item, 0 outside
    int items(Id id);                       // L: how many
    const std::string& text(Id id);         // C, L, S (main loop only)
    const char* defaultText(Id id);         // the table's default, as written there
    std::string valueText(Id id);           // the value in force as text, any type (main loop only)
    Source source(Id id);
    const char* fcsKey(Id id);

    // The in-game settings page (src/Mcm.cpp), main loop only.
    const std::string& iniPath();
    Id byIni(const char* section, const char* key);     // COUNT: the table has no row with this ini key
    std::string baseText(Id id);            // the value without the ini: FCS if the record has the key, else the default

    void log();                             // `settings` and one `setting` line per value that is not the default

    // An ini list `<string id>:<value>,<string id>:<value>` ([NPC] skill, [NPC] path): the entries with a
    // value of 0 or more go into `out`. Returns how many entries were refused.
    int readIdValues(const std::string& iniPath, const char* section, const char* key, std::map<std::string, float>& out);
}
