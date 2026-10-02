#pragma once

namespace tuning
{

    struct Anchor { float dp; float v; };

    const float CROSS_CHANCE = 0.6f;

    const double CROSS_COOLDOWN_MS = 3000.0;

    const float DASH_DISTANCE = 35.0f;
    const float DASH_CHANCE = 0.85f;

    const double DASH_COOLDOWN_MS = 12000.0;
    const float DASH_MIN_GAP = 6.0f;

    const float SPIN_MIN_SKILL = 75.0f;
    const int SPIN_MIN_TIER = 4;
    const float SPIN_CHANCE = 1.0f;
    const double SPIN_COOLDOWN_MS = 20000.0;
    const float SPIN_MIN_GAP = 6.0f;
    const float SPIN_MAX_GAP = 25.0f;

    const float SPIN_PRIORITY = 4.0f;
    const float DASH_PRIORITY = 3.0f;

    const int PICK_BUDGET = 32;
    const unsigned STANCE_CACHE_MS = 500;
    const int TIERS = 5;
}
