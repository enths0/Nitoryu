// Nitoryu runtime helpers. VS2010 (v100) safe.
//
// Log lines from any thread without racing KenshiLib's log streams.
//
// KenshiLib's DebugLog / ErrorLog write one global ofstream and one global
// stringstream with no lock, and combat hooks, AI and physics run off the main
// loop's thread. So: on the main thread a line is written at once; on any other
// thread it is queued in a fixed ring and written by the next main-loop frame.
// A full ring drops the new line and counts it.
//
// Use: call kit::log::tick() first thing in the main-loop hook, every frame
// (the first call records the main thread). Log with kit::log::line() /
// kit::log::error() instead of DebugLog / ErrorLog. Before the first tick every
// line is written at once, as DebugLog does (start-up runs on one thread).
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

            // POD on purpose: a function-local static of a POD type is zero-filled before
            // any code runs, so there is no unguarded initialiser (VS2010 has no thread-safe
            // statics). A slot holds a heap string; its first char is 'E' (error) or 'D'.
            struct State
            {
                CRITICAL_SECTION lock;
                volatile LONG ready;        // 1 once the lock exists and the main thread is known
                DWORD mainThread;
                std::string* ring[SLOTS];
                unsigned head;              // next slot to write out
                volatile LONG used;         // lines in the ring
                LONG queued;                // lines ever queued
                LONG dropped;               // lines lost to a full ring
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

            // Takes the queued lines out under the lock and writes them after it, so a hook
            // thread never waits on the file. Main thread only (the one writer keeps the order).
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
                    if (s.used > 0)             // keep the order: older queued lines first
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

        // Every main-loop frame, on the main loop's thread.
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

        // Totals since start, for a counters line: lines queued from other threads, lines dropped.
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
