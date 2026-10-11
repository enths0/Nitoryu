// Nitoryu runtime helpers. VS2010 (v100) safe.
//
// Structured diagnostics, one line per event, that a log reader can parse:
//
//     piece id='Wall 3' gate=0 len=255.0 verdict='removed'
//
// Rules this encodes:
//   - no plugin-name prefix: RE_Kenshi already logs the DLL name after startPlugin;
//   - one named counter per skip/failure reason, never one lumped counter;
//   - log the measured quantity per object so thresholds come from a histogram.
// Lines go out through kit/log.h: written at once on the main thread, queued
// from any other thread once the plugin calls kit::log::tick() every frame.
#pragma once

#include "log.h"
#include <cstdio>
#include <map>
#include <string>

namespace kit
{
    class Diag
    {
    public:
        explicit Diag(const char* event) : line_(event) {}

        Diag& kv(const char* key, int v) { char b[32]; sprintf_s(b, "%d", v); return raw(key, b); }
        Diag& kv(const char* key, unsigned v) { char b[32]; sprintf_s(b, "%u", v); return raw(key, b); }
        Diag& kv(const char* key, float v) { char b[48]; sprintf_s(b, "%.3f", v); return raw(key, b); }
        Diag& kv(const char* key, double v) { char b[48]; sprintf_s(b, "%.3f", v); return raw(key, b); }
        Diag& kv(const char* key, bool v) { return raw(key, v ? "1" : "0"); }
        Diag& kv(const char* key, const char* v) { return kv(key, std::string(v ? v : "")); }
        Diag& kv(const char* key, const std::string& v)
        {
            std::string q("'");
            for (size_t i = 0; i < v.size(); ++i)
            {
                if (v[i] == '\'' || v[i] == '\\') q += '\\';
                q += v[i];
            }
            q += "'";
            return raw(key, q.c_str());
        }

        void emit() const { log::line(line_); }

    private:
        Diag& raw(const char* key, const char* value)
        {
            line_ += ' ';
            line_ += key;
            line_ += '=';
            line_ += value;
            return *this;
        }
        std::string line_;
    };

    // Named counters, emitted as one `<event> reason=count ...` line.
    class Counters
    {
    public:
        void inc(const char* reason) { ++counts_[reason]; }
        void add(const char* reason, int n) { counts_[reason] += n; }
        int get(const char* reason) const
        {
            std::map<std::string, int>::const_iterator it = counts_.find(reason);
            return it == counts_.end() ? 0 : it->second;
        }
        void emit(const char* event) const
        {
            Diag d(event);
            for (std::map<std::string, int>::const_iterator it = counts_.begin(); it != counts_.end(); ++it)
                d.kv(it->first.c_str(), it->second);
            d.emit();
        }
        void clear() { counts_.clear(); }

    private:
        std::map<std::string, int> counts_;
    };
}
