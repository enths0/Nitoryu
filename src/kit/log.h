#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Debug.h>
#include <string>

namespace kit
{
    namespace log
    {
        namespace detail
        {
            enum { SLOTS = 256 };

            struct State
            {
                CRITICAL_SECTION lock;
                volatile LONG ready;
                DWORD mainThread;
                std::string* ring[SLOTS];
                unsigned head;
                volatile LONG used;
                LONG queued;
                LONG dropped;
            };

            inline State& state()
            {
                static State s;
                return s;
            }

            inline void write(const std::string& tagged)
            {
                if (tagged[0] == 'E')
                    ErrorLog(tagged.substr(1));
                else
                    DebugLog(tagged.substr(1));
            }

            inline void drain(State& s)
            {
                std::string* out[SLOTS];
                unsigned n = 0;
                EnterCriticalSection(&s.lock);
                while (s.used > 0)
                {
                    out[n++] = s.ring[s.head];
                    s.ring[s.head] = NULL;
                    s.head = (s.head + 1) % SLOTS;
                    --s.used;
                }
                LeaveCriticalSection(&s.lock);
                for (unsigned i = 0; i < n; ++i)
                {
                    write(*out[i]);
                    delete out[i];
                }
            }

            inline void put(char kind, const std::string& message)
            {
                State& s = state();
                std::string tagged(1, kind);
                tagged += message;
                if (s.ready == 0)
                {
                    write(tagged);
                    return;
                }
                if (GetCurrentThreadId() == s.mainThread)
                {
                    if (s.used > 0)
                        drain(s);
                    write(tagged);
                    return;
                }
                std::string* p = new std::string();
                p->swap(tagged);
                EnterCriticalSection(&s.lock);
                if (s.used >= SLOTS)
                {
                    ++s.dropped;
                    LeaveCriticalSection(&s.lock);
                    delete p;
                    return;
                }
                s.ring[(s.head + s.used) % SLOTS] = p;
                ++s.used;
                ++s.queued;
                LeaveCriticalSection(&s.lock);
            }
        }

        inline void tick()
        {
            detail::State& s = detail::state();
            if (s.ready == 0)
            {
                InitializeCriticalSection(&s.lock);
                s.mainThread = GetCurrentThreadId();
                InterlockedExchange(&s.ready, 1);
                return;
            }
            if (s.used > 0)
                detail::drain(s);
        }

        inline void line(const std::string& message) { detail::put('D', message); }
        inline void line(const char* message) { detail::put('D', message ? message : ""); }
        inline void error(const std::string& message) { detail::put('E', message); }
        inline void error(const char* message) { detail::put('E', message ? message : ""); }

        inline void totals(int& queued, int& dropped)
        {
            detail::State& s = detail::state();
            if (s.ready == 0)
            {
                queued = dropped = 0;
                return;
            }
            EnterCriticalSection(&s.lock);
            queued = (int)s.queued;
            dropped = (int)s.dropped;
            LeaveCriticalSection(&s.lock);
        }
    }
}
