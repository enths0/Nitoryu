#include "kit/input.h"

#include "kit/diag.h"

#include <stdint.h>
#include <string>
#include <vector>

#include "Diagnostics.h"
#include "Hooks.h"
#include "kit/log.h"

namespace
{
    struct Row
    {
        const char* symbol;
        const char* group;
        bool ok;
        bool shared;
        const unsigned char* target;
        const unsigned char* detour;
        bool chainOk;
    };
    std::vector<Row> g_rows;
    int g_shared = 0;
    bool g_outerChecked = false;

    bool sharedTarget(const void* target)
    {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(target);
        if (p == NULL || IsBadReadPtr(p, 2))
            return false;
        return p[0] == 0xE9 || (p[0] == 0xFF && p[1] == 0x25);
    }

    const unsigned char* hopOnce(const unsigned char* p)
    {
        __try
        {
            if (p[0] == 0xE9)
                return p + 5 + *reinterpret_cast<const int32_t*>(p + 1);
            if (p[0] == 0xFF && p[1] == 0x25)
                return *reinterpret_cast<const unsigned char* const*>(p + 6 + *reinterpret_cast<const int32_t*>(p + 2));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return NULL;
    }

    const unsigned char* chainEnd(const unsigned char* target, const unsigned char* detour)
    {
        const unsigned char* p = target;
        for (int i = 0; i < 8 && p != NULL; ++i)
        {
            if (p == detour)
                return detour;
            const unsigned char* next = hopOnce(p);
            if (next == NULL)
                return p;
            p = next;
        }
        return p;
    }

    std::string moduleOf(const void* address)
    {
        HMODULE m = NULL;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCSTR>(address), &m) || m == NULL)
            return "";
        char path[MAX_PATH] = { 0 };
        if (GetModuleFileNameA(m, path, MAX_PATH) == 0)
            return "";
        std::string p(path);
        size_t slash = p.find_last_of("\\/");
        return slash == std::string::npos ? p : p.substr(slash + 1);
    }

    bool tellPlayer(const char* symbol)
    {
        std::string s(symbol);
        return s == "CharStats::xpMelee" || s == "CombatClass::_iHitYouAreYouHit";
    }
}

namespace hooks
{
    KenshiLib::HookStatus addRaw(const char* symbol, const char* group, void* target, void* detour, void** original)
    {
        Row r = { symbol, group, false, false, reinterpret_cast<const unsigned char*>(target),
                  reinterpret_cast<const unsigned char*>(detour), false };
        if (sharedTarget(target))
        {
            r.shared = true;
            ++g_shared;
            const unsigned char* p = reinterpret_cast<const unsigned char*>(target);
            kit::Diag("shared_hook").kv("target", symbol).kv("first_byte", (int)p[0]).emit();
        }
        KenshiLib::HookStatus s = target != NULL ? KenshiLib::AddHook(target, detour, original) : KenshiLib::FAIL;

        r.ok = s == KenshiLib::SUCCESS && original != NULL && *original != NULL;
        if (r.ok)
        {
            r.chainOk = chainEnd(r.target, r.detour) == r.detour;
            if (!r.chainOk)
                kit::Diag("hook_chain_unknown").kv("target", symbol).emit();
        }
        g_rows.push_back(r);
        return r.ok ? KenshiLib::SUCCESS : KenshiLib::FAIL;
    }

    int sharedCount() { return g_shared; }

    void logSummary()
    {
        int ok = 0;
        std::string failed;
        for (size_t i = 0; i < g_rows.size(); ++i)
        {
            if (g_rows[i].ok)
            {
                ++ok;
                continue;
            }
            if (!failed.empty())
                failed += ",";
            failed += std::string(g_rows[i].symbol) + "(" + g_rows[i].group + ")";
        }
        kit::Diag("hooks_installed").kv("rows", (int)g_rows.size()).kv("ok", ok)
            .kv("failed", failed.empty() ? std::string("-") : failed).kv("shared", g_shared).emit();
    }

    void checkOuter()
    {
        if (g_outerChecked)
            return;
        g_outerChecked = true;
        int checked = 0, outer = 0;
        for (size_t i = 0; i < g_rows.size(); ++i)
        {
            const Row& r = g_rows[i];
            if (!r.ok || !r.chainOk)
                continue;
            ++checked;
            const unsigned char* end = chainEnd(r.target, r.detour);
            if (end == r.detour)
                continue;
            ++outer;
            std::string by = moduleOf(end);
            diag::count("hook_outer");
            kit::Diag("hook_outer").kv("target", r.symbol).kv("group", r.group)
                .kv("by", by.empty() ? std::string("-") : by).emit();
            if (tellPlayer(r.symbol))
                kit::log::error(std::string("Nitoryu: ") + (by.empty() ? std::string("another plugin") : by) + " hooks "
                                + r.symbol + " on top of Nitoryu; if Dual Wielding stops growing or the off-hand guard"
                                " stops, put Nitoryu below that mod in the mod list.");
        }
        kit::Diag("hooks_outer_check").kv("checked", checked).kv("outer", outer).emit();
    }
}
