#include "kit/input.h"

#include <kenshi/Character.h>
#include <kenshi/util/hand.h>

#include <map>
#include <string>
#include <vector>

#include "Feedback.h"
#include "Stance.h"

namespace
{
    bool g_stage = false;

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
}
