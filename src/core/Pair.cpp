// The pair decision (Pair.h). No game header here, ever: this file also builds on the host for the unit tests.
#include "Pair.h"

namespace
{
    bool letIn(const core::WeaponFacts& w, bool button)
    {
        if (w.record == core::RECORD_ALLOWED || w.categoryAllowed)
            return true;
        return button && (w.category == core::CAT_BLUNT || w.category == core::CAT_HACKERS);
    }

    core::PairReason decide(const core::PairFacts& f, bool button)
    {
        if (f.a.record == core::RECORD_REFUSED || f.b.record == core::RECORD_REFUSED)
            return core::PAIR_WEAPON_REFUSED;
        if (f.plugin == core::ANSWER_REFUSE)
            return core::PAIR_PLUGIN_REFUSED;
        const bool byPlugin = f.plugin == core::ANSWER_ALLOW || f.plugin == core::ANSWER_ALLOW_TWO_HANDED;
        if (!byPlugin && (!letIn(f.a, button) || !letIn(f.b, button)))
            return core::PAIR_TYPE_NOT_ALLOWED;
        if (f.plugin != core::ANSWER_ALLOW_TWO_HANDED && (f.a.twoHandedOnly || f.b.twoHandedOnly))
            return core::PAIR_TWO_HANDED_ONLY;
        return core::PAIR_OK;
    }
}

namespace core
{
    PairVerdict pairVerdict(const PairFacts& f)
    {
        PairVerdict v;
        v.reason = decide(f, f.button);
        v.allowed = v.reason == PAIR_OK;
        v.needsButton = decide(f, false) != PAIR_OK && decide(f, true) == PAIR_OK;
        return v;
    }

    bool oneHandedAllowed(const WeaponFacts& w)
    {
        return w.record != RECORD_REFUSED && letIn(w, false) && !w.twoHandedOnly;
    }

    const char* pairReasonName(PairReason r)
    {
        switch (r)
        {
        case PAIR_WEAPON_REFUSED:   return "weapon_refused";
        case PAIR_PLUGIN_REFUSED:   return "plugin_refused";
        case PAIR_TYPE_NOT_ALLOWED: return "type_not_allowed";
        case PAIR_TWO_HANDED_ONLY:  return "two_handed_only";
        default:                    return "dual";
        }
    }
}
