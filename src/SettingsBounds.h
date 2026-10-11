// Nitoryu's settings: the range of each number.
//
// A value from FCS or the ini outside its range is clamped to it and logged once (`setting_clamped`); a value
// that is not a number at all (nan, inf) falls back to the default. The ranges keep a setting meaningful (a
// chance in 0-1, a time not below 0, a tier 0-6 where 6 = never); they are never a cap on a skill or a stat,
// and they stay wide where a modder may want an odd value (a tier requirement above 100 = never reached).
// For a curve (C) the range is for its values, not its DP points; for a list (L) for each number.
// Every F, I, C and L row of SettingsDef.h has one line here. docs/FCS-settings.md shows the ranges.
//
// X(id, lowest, highest)
#pragma once

#define NITORYU_ANY 1.0e9

#define NITORYU_SETTING_BOUNDS(X) \
    X(W_SKILL, 0, 1) \
    X(W_DEX, 0, 1) \
    X(W_ATTACK, 0, 1) \
    X(W_DEFENCE, 0, 1) \
    X(W_AMBI, 0, 1) \
    X(TIER_DP, 0, NITORYU_ANY) \
    X(TIER_MIN_AMBI, 0, NITORYU_ANY) \
    X(AMB_SEED, 0, NITORYU_ANY) \
    X(AMB_XP_SHARE, 0, NITORYU_ANY) \
    X(WEIGHT_MULT, 0, NITORYU_ANY) \
    X(OFF_HAND_SHARE, 0, 1) \
    X(TWIN_SHARE, 0, 1) \
    X(OFF_HAND_DAMAGE, 0, NITORYU_ANY) \
    X(OFF_HAND_GUARD, 0, 1) \
    X(COUNTER_CHANCE, 0, 1) \
    X(PRESSURE_POINTS, 0, NITORYU_ANY) \
    X(TEMPO_POINTS, 0, NITORYU_ANY) \
    X(DIVIDED_GUARD, -NITORYU_ANY, NITORYU_ANY) \
    X(DEX_BONUS, 0, NITORYU_ANY) \
    X(CLUMSY0, 0, NITORYU_ANY) \
    X(CLUMSY_SLOPE, 0, NITORYU_ANY) \
    X(OVERLOAD_FLOOR, 0, 1) \
    X(ARMOUR_HEAVY_CUT, 0, 1) \
    X(ARMOUR_MEDIUM_CUT, 0, 1) \
    X(ARMOUR_FLOOR, 0, 1) \
    X(OFF_WEAPON_BONUS_SHARE, 0, NITORYU_ANY) \
    X(SPEED0, 0.05, NITORYU_ANY) \
    X(SPEED_FULL_DP, 0, NITORYU_ANY) \
    X(SPEED_BONUS, 0, NITORYU_ANY) \
    X(SPEED_FROM, 0, NITORYU_ANY) \
    X(DUAL_ATTACK_CAP, 0.05, NITORYU_ANY) \
    X(DUAL_BLOCK_CAP, 0.05, NITORYU_ANY) \
    X(GUARD_MIN_TIER, 0, 6) \
    X(GUARD_COOLDOWN_MS, 0, NITORYU_ANY) \
    X(GUARD_ARC_DEG, 0, 360) \
    X(COMBO_GUARD_SHARE, 0, NITORYU_ANY) \
    X(MAIN_GUARD_SHARE, 0, NITORYU_ANY) \
    X(MAIN_GUARD_MIN_TIER, 0, 6) \
    X(BACK_GUARD_MIN_TIER, 0, 6) \
    X(BACK_GUARD_SHARE, 0, NITORYU_ANY) \
    X(COUNTER_MIN_TIER, 0, 6) \
    X(COUNTER_WINDOW_MS, 0, NITORYU_ANY) \
    X(COUNTER_CHANCE_FIXED, -1, 1) \
    X(PRESSURE_MIN_TIER, 0, 6) \
    X(PRESSURE_MAX_STACKS, 1, 20) \
    X(PRESSURE_DECAY_MS, 0, NITORYU_ANY) \
    X(PRESSURE_POINTS_FIXED, -1, NITORYU_ANY) \
    X(PRESSURE_SHOUT_STACKS, 0, NITORYU_ANY) \
    X(SPECIAL_MIN_AMBI, 0, NITORYU_ANY) \
    X(DOUBLE_HIT_COOLDOWN_MS, 0, NITORYU_ANY) \
    X(MAIN_MOVE_SHARE, 0, 1) \
    X(CROSS_RIPOSTE_CHANCE, 0, 1) \
    X(OFF_CARRY_MS, 0, NITORYU_ANY) \
    X(FAR_PICK_GAP, 0, NITORYU_ANY) \
    X(CROSS_RIPOSTE_WINDOW_MS, 0, NITORYU_ANY) \
    X(CROSS_ARM_MS, 0, NITORYU_ANY) \
    X(LONG_DASH_MIN, 0, NITORYU_ANY) \
    X(FOREIGN_REPICK_OVER, 0, NITORYU_ANY) \
    X(NPC_MAX_TIER, 0, 5) \
    X(NPC_MIN_DEX, 0, NITORYU_ANY) \
    X(NPC_MIN_SKILL, 0, NITORYU_ANY) \
    X(NPC_STR_SHARE, 0, NITORYU_ANY) \
    X(WEAPON_CATEGORIES, 0, NITORYU_ANY) \
    X(LEFTY_MIN_AMBI, 0, NITORYU_ANY) \
    X(LEFTY_DAMAGE_AT_MIN, 0, NITORYU_ANY) \
    X(LEFTY_SPEED_AT_MIN, 0.05, NITORYU_ANY) \
    X(LEFTY_BLOCK_AT_MIN, 0, NITORYU_ANY) \
    X(FLOW_MIN_TIER, 0, 6) \
    X(FLOW_ATTACKS, 1, 8) \
    X(FLOW_WINDOW_MS, 0, NITORYU_ANY) \
    X(FLOW_DURATION_MS, 0, NITORYU_ANY) \
    X(FLOW_COOLDOWN_MS, 0, NITORYU_ANY) \
    X(FLOW_DEX_BONUS, 0, NITORYU_ANY) \
    X(FLOW_SPEED, 0, NITORYU_ANY) \
    X(FLOW_CAP_BONUS, 0, NITORYU_ANY) \
    X(MUGEN_MIN_TIER, 0, 6) \
    X(MUGEN_COOLDOWN_MS, 0, NITORYU_ANY) \
    X(MUGEN_CHAIN, 0, 1) \
    X(MUGEN_CHAIN_WINDOW_MS, 0, NITORYU_ANY) \
    X(PAIR_SHORT_RATIO, 0, 1) \
    X(PAIR_EQUAL_RATIO, 0, 1) \
    X(PAIR_GUARD_MULT, 0, NITORYU_ANY) \
    X(PAIR_TWIN_MULT, 0, NITORYU_ANY) \
    X(PAIR_MIXED_DEFENCE, 0, NITORYU_ANY) \
    X(SCISSOR_CHANCE, 0, 1) \
    X(SCISSOR_WINDOW_MS, 0, NITORYU_ANY) \
    X(SPLIT_MIN_TIER, 0, 6) \
    X(SPLIT_CHANCE, 0, 1) \
    X(SPLIT_ARC_DEG, 0, 360) \
    X(SPLIT_REACH, 0, NITORYU_ANY) \
    X(SPLIT_ATTACKER_MS, 0, NITORYU_ANY) \
    X(AMB_XP_PARRY, 0, NITORYU_ANY) \
    X(AMB_XP_OWN_RATIO, 0, 1) \
    X(AMB_XP_TWIN, 0, NITORYU_ANY) \
    X(AMB_XP_RIPOSTE, 0, NITORYU_ANY) \
    X(AMB_XP_OUTNUMBERED, 0, NITORYU_ANY) \
    X(AMB_XP_OUTNUMBERED_MS, 0, NITORYU_ANY) \
    X(AMB_XP_HIT_TAKEN, 0, NITORYU_ANY) \
    X(FOREIGN_HOLD_MS, 0, NITORYU_ANY) \
    X(FOREIGN_RUN_MS, 0, NITORYU_ANY) \
    X(RUN_STYLE, 0, 4) \
    X(HUD_MARKS, 0, 3) \
    X(HUD_PLACE, 1, 3) \
    X(HUD_SIZE, 8, 64) \
    X(HUD_HEIGHT, -NITORYU_ANY, NITORYU_ANY) \
    X(STATS_GROUP, 1, 2) \
    X(RUN_HOLD_FREE_WEIGHT, 0, 1)
