// Nitoryu runtime helpers. VS2010 (v100) safe.
//
// Read a game record's FCS fields at runtime. GameData::bdata/fdata/idata/
// sdata are boost::unordered_maps: operator[] would INSERT a default into the
// game's own record, so only find() is used here.
#pragma once

#include <kenshi/GameData.h>
#include <string>

namespace kit
{
    inline bool fcsBool(const GameData* d, const std::string& key, bool fallback)
    {
        if (!d) return fallback;
        auto it = d->bdata.find(key);
        return it != d->bdata.end() ? it->second : fallback;
    }

    inline float fcsFloat(const GameData* d, const std::string& key, float fallback)
    {
        if (!d) return fallback;
        auto it = d->fdata.find(key);
        return it != d->fdata.end() ? it->second : fallback;
    }

    inline int fcsInt(const GameData* d, const std::string& key, int fallback)
    {
        if (!d) return fallback;
        auto it = d->idata.find(key);
        return it != d->idata.end() ? it->second : fallback;
    }

    inline std::string fcsString(const GameData* d, const std::string& key, const std::string& fallback)
    {
        if (!d) return fallback;
        auto it = d->sdata.find(key);
        return it != d->sdata.end() ? it->second : fallback;
    }

    // A `file` field (FCS filevalue, GameData::filesdata), e.g. WEAPON "bare sword"
    // or "sheath": the path exactly as the game stores it (what attachItem takes).
    inline std::string fcsFile(const GameData* d, const std::string& key, const std::string& fallback)
    {
        if (!d) return fallback;
        auto it = d->filesdata.find(key);
        return it != d->filesdata.end() ? it->second : fallback;
    }

    // The record's string id and display name, for log lines (never classify by them).
    inline std::string recordId(const GameData* d) { return d ? d->stringID : std::string("<null>"); }
    inline std::string recordName(const GameData* d) { return d ? d->name : std::string("<null>"); }
}
