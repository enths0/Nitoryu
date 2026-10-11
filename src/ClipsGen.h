// GENERATED from the move registry. Do not edit by hand:
// a new clip is a registry entry. One row per L3 clip, in arms::Clip order:
// Approved moves only, numbered 0..n-1 here (registry clip.index keeps every move's slot).
// X(enum, track name, name (load line), [Animations] key, [Test] legacy key, counter prefix,
//   full body, base track the clip was authored on (NULL: none))
#pragma once

#define NITORYU_L3_CLIPS(X) \
    X(CLIP_BLOCK_LEFT, "nitoryu block left", "high_block", "high_block", "l3_block", "l3_block", true, NULL) \
    X(CLIP_SCISSOR, "nitoryu scissor block", "scissor_block", "scissor_block", "l3_scissor", "l3_scissor", false, NULL) \
    X(CLIP_DRAW, "nitoryu dual draw", "dual_draw", "dual_draw", "l3_draw", "l3_draw", false, NULL) \
    X(CLIP_X_CUT, "nitoryu x cut", "x_cut", "x_cut", "l3_cross", "l3_cross", true, "chop down static") \
    X(CLIP_DASH, "nitoryu dash slash", "dash_slash", "dash_slash", "l3_dash", "l3_dash", true, "flyknee") \
    X(CLIP_DRAW_HIP, "nitoryu dual draw hip", "hip_draw", "dual_draw", "l3_draw", "l3_draw_hip", false, NULL) \
    X(CLIP_RUN_TRAIL, "nitoryu run trail", "run_trail", "run_hold", "l3_run", "l3_run_trail", false, NULL) \
    X(CLIP_SPIN, "nitoryu spin", "spin", "spin", "l3_spin", "l3_spin", true, "flyknee") \
    X(CLIP_TWIN_SWEEP, "nitoryu twin sweep", "twin_sweep", "twin_sweep", "l3_twin_sweep", "l3_twin_sweep", true, "chop down static") \
    X(CLIP_DUAL_IMPACT_L, "nitoryu dual block left impact", "dual_impact_left", "dual_block", "l3_dual_block", "l3_dual_impact_l", true, NULL) \
    X(CLIP_DUAL_IMPACT_R, "nitoryu dual block right impact", "dual_impact_right", "dual_block", "l3_dual_block", "l3_dual_impact_r", true, NULL) \
    X(CLIP_MAIN_BLOCK, "nitoryu main block", "main_block", "main_block_single", "l3_main_block", "l3_main_block", false, NULL) \
    X(CLIP_MAIN_IMPACT, "nitoryu main block impact", "main_impact", "main_block_single", "l3_main_block", "l3_main_impact", true, NULL) \
    X(CLIP_OFF_BLOCK, "nitoryu offhand block", "offhand_block", "offhand_block", "l3_offhand_block", "l3_offhand_block", true, NULL) \
    X(CLIP_TWIN_BODY_CUT, "nitoryu twin body cut", "twin_body_cut", "twin_body_cut", "l3_twin_body_cut", "l3_twin_body_cut", true, "chop left") \
    X(CLIP_ANGLED_L, "nitoryu angled block left", "angled_block_left", "angled_block", "l3_angled_left", "l3_angled_l", false, NULL) \
    X(CLIP_ANGLED_R, "nitoryu angled block right", "angled_block_right", "angled_block", "l3_angled_right", "l3_angled_r", false, NULL) \
    X(CLIP_ANGLED_L_IMPACT, "nitoryu angled block left impact", "angled_left_impact", "angled_block", "l3_angled_left_impact", "l3_angled_l_impact", true, NULL) \
    X(CLIP_ANGLED_R_IMPACT, "nitoryu angled block right impact", "angled_right_impact", "angled_block", "l3_angled_right_impact", "l3_angled_r_impact", true, NULL) \
    X(CLIP_MAIN_OVERHEAD, "nitoryu main overhead block", "main_overhead_block", "main_block", "l3_main_overhead", "l3_main_overhead", false, NULL) \
    X(CLIP_MAIN_OVERHEAD_IMPACT, "nitoryu main overhead block impact", "main_overhead_impact", "main_block", "l3_main_overhead_impact", "l3_main_overhead_impact", true, NULL) \
    X(CLIP_HIT_TAKEN, "nitoryu hit taken", "hit_taken", "hit_taken", "l3_hit_taken", "l3_hit_taken", true, NULL) \
    X(CLIP_SHEATHE, "nitoryu dual sheathe", "dual_sheathe", "dual_sheathe", "l3_sheathe", "l3_sheathe", false, NULL) \
    X(CLIP_SHEATHE_HIP, "nitoryu dual sheathe hip", "hip_sheathe", "dual_sheathe", "l3_sheathe", "l3_sheathe_hip", false, NULL) \
    X(CLIP_CHIBURI, "nitoryu chiburi", "dual_chiburi", "dual_chiburi", "l3_chiburi", "l3_chiburi", false, NULL) \
    X(CLIP_CHIBURI_HIP, "nitoryu chiburi hip", "hip_chiburi", "dual_chiburi", "l3_chiburi", "l3_chiburi_hip", false, NULL) \
    X(CLIP_STANCE_DESHI, "nitoryu stance deshi", "stance_deshi", "idle_stances", "l3_stance_deshi", "l3_stance_deshi", true, NULL) \
    X(CLIP_STANCE_BUSHI, "nitoryu stance bushi", "stance_bushi", "idle_stances", "l3_stance_bushi", "l3_stance_bushi", true, NULL) \
    X(CLIP_STANCE_RONIN, "nitoryu stance ronin", "stance_ronin", "idle_stances", "l3_stance_ronin", "l3_stance_ronin", true, NULL) \
    X(CLIP_STANCE_KENSHI, "nitoryu stance kenshi", "stance_kenshi", "idle_stances", "l3_stance_kenshi", "l3_stance_kenshi", true, NULL) \
    X(CLIP_STANCE_KENSEI, "nitoryu stance kensei", "stance_kensei", "idle_stances", "l3_stance_kensei", "l3_stance_kensei", true, NULL) \
    X(CLIP_ONE_TWO, "nitoryu one two", "one_two", "one_two", "l3_one_two", "l3_one_two", true, "chop down static") \
    X(CLIP_OFF_BACKHAND, "nitoryu off backhand", "off_backhand", "off_backhand", "l3_off_backhand", "l3_off_backhand", true, "chop down static") \
    X(CLIP_OFF_LEG_CUT, "nitoryu off leg cut", "off_leg_cut", "off_leg_cut", "l3_off_leg_cut", "l3_off_leg_cut", true, "chop down static") \
    X(CLIP_DOUBLE_CHOP, "nitoryu double chop", "double_chop", "double_chop", "l3_double_chop", "l3_double_chop", true, "chop down static") \
    X(CLIP_TWIN_SHEAR, "nitoryu twin shear", "twin_shear", "twin_shear", "l3_twin_shear", "l3_twin_shear", true, "chop down static") \
    X(CLIP_HEAD_WRAP, "nitoryu head wrap", "head_wrap", "head_wrap", "l3_head_wrap", "l3_head_wrap", true, "ma 2strike") \
    X(CLIP_DUAL_CUT, "nitoryu dual cut", "dual_cut", "dual_cut", "l3_dual_cut", "l3_dual_cut", true, "ma chudan") \
    X(CLIP_OFF_HAND_CUT, "nitoryu off hand cut", "off_hand_cut", "off_hand_cut", "l3_off_hand_cut", "l3_off_hand_cut", true, "chop left") \
    X(CLIP_MAIN_HAND_CUT, "nitoryu main hand cut", "main_hand_cut", "main_hand_cut", "l3_main_hand_cut", "l3_main_hand_cut", true, "chop left") \
    X(CLIP_FIGURE_EIGHT, "nitoryu figure eight", "figure_eight", "figure_eight", "l3_figure_eight", "l3_figure_eight", true, "ma 2strike") \
    X(CLIP_THREE_CUTS, "nitoryu three cuts", "three_cuts", "three_cuts", "l3_three_cuts", "l3_three_cuts", true, "chop down static") \
    X(CLIP_BACK_LOW, "nitoryu back guard low", "back_guard_low", "back_guard", "l3_back_guard_low", "l3_back_guard_low", true, NULL) \
    X(CLIP_BACK_LOW_MAIN, "nitoryu back guard low main", "back_guard_low_main", "back_guard", "l3_back_guard_low_main", "l3_back_guard_low_main", true, NULL) \
    X(CLIP_BACK_HIGH, "nitoryu back guard high", "back_guard_high", "back_guard", "l3_back_guard_high", "l3_back_guard_high", true, NULL) \
    X(CLIP_BACK_HIGH_MAIN, "nitoryu back guard high main", "back_guard_high_main", "back_guard", "l3_back_guard_high_main", "l3_back_guard_high_main", true, NULL) \
    X(CLIP_OFF_BELLY_CUT, "nitoryu off belly cut", "off_belly_cut", "off_belly_cut", "l3_off_belly_cut", "l3_off_belly_cut", true, "chop down static") \
    X(CLIP_MAIN_CHOP, "nitoryu main chop", "main_chop", "main_chop", "l3_main_chop", "l3_main_chop", true, "chop down static") \
    X(CLIP_OFF_SHOULDER_CUT, "nitoryu off shoulder cut", "off_shoulder_cut", "off_shoulder_cut", "l3_off_shoulder_cut", "l3_off_shoulder_cut", true, "chop down static") \
    X(CLIP_MAIN_LEVEL_CUT, "nitoryu main level cut", "main_level_cut", "main_level_cut", "l3_main_level_cut", "l3_main_level_cut", true, "chop down static") \
    X(CLIP_OFF_HIGH_CUT, "nitoryu off high cut", "off_high_cut", "off_high_cut", "l3_off_high_cut", "l3_off_high_cut", true, "chop down static") \
    X(CLIP_MAIN_SHORT_CUT, "nitoryu main short cut", "main_short_cut", "main_short_cut", "l3_main_short_cut", "l3_main_short_cut", true, "chop down static") \
    X(CLIP_RUN_STANCE_A, "nitoryu run stance a", "run_stance_a", "run_hold", "l3_run", "l3_run_stance_a", true, NULL) \
    X(CLIP_RUN_STANCE_B, "nitoryu run stance b", "run_stance_b", "run_hold", "l3_run", "l3_run_stance_b", true, NULL) \
    X(CLIP_RUN_STANCE_C, "nitoryu run stance c", "run_stance_c", "run_hold", "l3_run", "l3_run_stance_c", true, NULL) \
    X(CLIP_BIG_CHOP, "nitoryu big chop", "big_chop", "big_chop", "l3_big_chop", "l3_big_chop", true, "bigchopv2") \
    X(CLIP_TWO_STEP, "nitoryu two step", "two_step", "two_step", "l3_two_step", "l3_two_step", true, "downward combo")
