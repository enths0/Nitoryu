// Nitoryu runtime helpers. VS2010 (v100) safe.
//
// Find a virtual function that KenshiLib does not export, by class name, at
// run time: MSVC RTTI names every polymorphic class (".?AVTask_MeleeAttack@@"),
// so the vtable is found the same way on Steam, GOG and any rebuild that keeps
// the class. Chain: the name string (inside the TypeDescriptor, at +16) ->
// the Complete Object Locator that points at it (signature 1, offset 0, its own
// RVA at +20) -> the vtable (the qword after a pointer to that locator).
#pragma once

#include <windows.h>
#include <string.h>

namespace kit
{
    namespace rtti_detail
    {
        inline bool section(HMODULE m, const char* name, unsigned char*& lo, unsigned char*& hi)
        {
            unsigned char* base = reinterpret_cast<unsigned char*>(m);
            IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            IMAGE_NT_HEADERS64* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
            for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s)
            {
                if (strncmp(reinterpret_cast<const char*>(s->Name), name, 8) == 0)
                {
                    lo = base + s->VirtualAddress;
                    hi = lo + s->Misc.VirtualSize;
                    return true;
                }
            }
            return false;
        }

        inline unsigned char* findBytes(unsigned char* lo, unsigned char* hi, const void* needle, size_t n, size_t step)
        {
            for (unsigned char* p = lo; p + n <= hi; p += step)
                if (memcmp(p, needle, n) == 0)
                    return p;
            return NULL;
        }
    }

    // The vtable of MSVC class `mangled` (e.g. ".?AVTask_MeleeAttack@@") in the
    // game executable, or NULL. Scans .data and .rdata once per call (~20 ms).
    inline void** findVtable(const char* mangled)
    {
        using namespace rtti_detail;
        HMODULE m = GetModuleHandleA(NULL);
        unsigned char* base = reinterpret_cast<unsigned char*>(m);
        unsigned char *dlo, *dhi, *rlo, *rhi;
        if (!section(m, ".data", dlo, dhi) || !section(m, ".rdata", rlo, rhi))
            return NULL;
        size_t len = strlen(mangled) + 1;               // with the NUL
        unsigned char* name = findBytes(dlo, dhi, mangled, len, 1);
        if (name == NULL)
            name = findBytes(rlo, rhi, mangled, len, 1);
        if (name == NULL)
            return NULL;
        DWORD tdRva = (DWORD)((name - 16) - base);
        // Complete Object Locator: {sig=1, offset=0, cdOffset, pTypeDescriptor, pClassDescriptor, pSelf} (RVAs).
        for (unsigned char* p = rlo; p + 24 <= rhi; p += 4)
        {
            const DWORD* col = reinterpret_cast<const DWORD*>(p);
            if (col[0] != 1 || col[1] != 0 || col[3] != tdRva || col[5] != (DWORD)(p - base))
                continue;
            unsigned long long colVa = (unsigned long long)p;
            unsigned char* ref = findBytes(rlo, rhi, &colVa, 8, 8);
            if (ref != NULL)
                return reinterpret_cast<void**>(ref + 8);
        }
        return NULL;
    }

    // Follows one incremental-link thunk (E9 rel32) to the function body.
    inline void* followThunk(void* f)
    {
        unsigned char* p = static_cast<unsigned char*>(f);
        if (p != NULL && p[0] == 0xE9)
            return p + 5 + *reinterpret_cast<int*>(p + 1);
        return f;
    }

    // True if the first `n` bytes of `f` contain `needle` (a cheap identity check
    // before hooking something found by name).
    inline bool codeContains(void* f, size_t n, const void* needle, size_t m)
    {
        unsigned char* p = static_cast<unsigned char*>(f);
        for (size_t i = 0; p != NULL && i + m <= n; ++i)
            if (memcmp(p + i, needle, m) == 0)
                return true;
        return false;
    }
}
