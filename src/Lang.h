// Player-visible texts in the player's language.
//
// Texts live in Kenshi\mods\Nitoryu\lang\<code>.ini (UTF-8, key=value; en.ini is
// the complete reference). The language is Nitoryu.ini [General] language, or
// with `auto` (the default) the `language=` line of Kenshi\settings.cfg
// (ru_RU -> ru). Every key missing from the chosen file falls back to en.ini,
// then to the built-in English, so a partial translation never shows blanks.
// Translators: lang\TRANSLATING.md and lang\lang_check.py.
#pragma once

#include <string>

namespace lang
{
    void init();                                   // before any text is used
    const std::string& text(const char* key);      // UTF-8; the key itself if unknown everywhere
    const std::string* find(const char* key);      // NULL if neither a file nor the built-in texts have the key
    const std::string& code();                    // the language in use ("en", "ru", ...)
}
