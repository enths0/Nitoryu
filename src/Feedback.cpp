// Threads:
//   No hook of its own: combat threads queue speech lines, the main loop says them (flush).
//   g_lock guards g_queue and g_lastSaid; g_noticeLock (ready before any init) guards g_offSeen.

#include "kit/input.h"   // Windows.h with NOMINMAX first

#include <kenshi/Character.h>
#include <kenshi/util/hand.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "Feedback.h"
#include "Stance.h"
#include "Diagnostics.h"
#include "kit/log.h"

namespace
{
    bool g_stage = false;   // [General] env=stage: test builds only

    struct Line
    {
        hand who;
        std::string text;
    };

    CRITICAL_SECTION g_lock;
    std::vector<Line> g_queue;
    std::map<Character*, DWORD> g_lastSaid;

    struct Lock
    {
        Lock() { EnterCriticalSection(&g_lock); }
        ~Lock() { LeaveCriticalSection(&g_lock); }
    };

    // Initialised with the DLL's globals, so a feature can be reported off before init() ran.
    struct NoticeLock
    {
        CRITICAL_SECTION cs;
        NoticeLock() { InitializeCriticalSection(&cs); }
    } g_noticeLock;

    struct NLock
    {
        NLock() { EnterCriticalSection(&g_noticeLock.cs); }
        ~NLock() { LeaveCriticalSection(&g_noticeLock.cs); }
    };

    std::set<std::string> g_offSeen;        // every feature reported, so each is named once
}

namespace feedback
{
    void init()
    {
        InitializeCriticalSection(&g_lock);
        char env[16] = { 0 };
        GetPrivateProfileStringA("General", "env", "prod", env, sizeof(env), stance::iniFile().c_str());
        g_stage = _stricmp(env, "stage") == 0;
    }

    bool stage() { return g_stage; }

    void forget(Character* c)
    {
        Lock l;
        g_lastSaid.erase(c);
    }

    void say(Character* c, const char* text, unsigned minGapMs)
    {
        if (!g_stage || c == NULL || text == NULL)
            return;
        DWORD now = GetTickCount();
        Lock l;
        std::map<Character*, DWORD>::iterator it = g_lastSaid.find(c);
        if (it != g_lastSaid.end() && now - it->second < minGapMs)
            return;
        g_lastSaid[c] = now;
        Line line;
        line.who = c->getHandle();
        line.text = text;
        g_queue.push_back(line);
    }

    void announce(Character* c, const char* text)
    {
        if (c == NULL || text == NULL)
            return;
        Lock l;
        Line line;
        line.who = c->getHandle();
        line.text = text;
        g_queue.push_back(line);
    }

    void flush()
    {
        std::vector<Line> lines;
        {
            Lock l;
            lines.swap(g_queue);
            if (g_lastSaid.size() > 256)
                g_lastSaid.clear();
        }
        for (size_t i = 0; i < lines.size(); ++i)
        {
            Character* c = lines[i].who.getCharacter();
            if (c != NULL)
                c->say(lines[i].text);
        }
    }

    void featureOff(const char* feature, const char* why)
    {
        if (feature == NULL)
            return;
        {
            NLock l;
            if (!g_offSeen.insert(feature).second)
                return;
        }
        kit::log::error(std::string("Nitoryu: ") + feature + " turned off: " + (why != NULL ? why : "?"));
        diag::count("feature_off_reported");
    }
}
