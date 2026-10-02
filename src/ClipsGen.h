#pragma once

#define NITORYU_L3_CLIPS(X) \
    X(CLIP_BLOCK_LEFT, "nitoryu block left", "high_block", "high_block", "l3_block", "l3_block", true, NULL) \
    X(CLIP_SCISSOR, "nitoryu scissor block", "scissor_block", "scissor_block", "l3_scissor", "l3_scissor", false, NULL) \
    X(CLIP_DRAW, "nitoryu dual draw", "dual_draw", "dual_draw", "l3_draw", "l3_draw", false, NULL) \
    X(CLIP_CROSS, "nitoryu cross finisher", "cross_finisher", "cross_finisher", "l3_cross", "l3_cross", true, "downward combo") \
    X(CLIP_DASH, "nitoryu dash slash", "dash_slash", "dash_slash", "l3_dash", "l3_dash", true, "flyknee") \
    X(CLIP_DRAW_HIP, "nitoryu dual draw hip", "hip_draw", "dual_draw", "l3_draw", "l3_draw_hip", false, NULL) \
    X(CLIP_RUN_TRAIL, "nitoryu run trail", "run_trail", "run_hold", "l3_run", "l3_run_trail", false, NULL) \
    X(CLIP_RUN_LEAD, "nitoryu run lead", "run_lead", "run_hold", "l3_run", "l3_run_lead", false, NULL) \
    X(CLIP_SPIN, "nitoryu spin", "spin", "spin", "l3_spin", "l3_spin", true, "flyknee") \
    X(CLIP_OFF_RISING, "nitoryu off rising cut", "off_rising_cut", "off_rising_cut", "l3_off_rising", "l3_off_rising", true, "chop left") \
    X(CLIP_TWIN_SWEEP, "nitoryu twin sweep", "twin_sweep", "twin_sweep", "l3_twin_sweep", "l3_twin_sweep", true, "ma chudan") \
    X(CLIP_DUAL_IMPACT_L, "nitoryu dual block left impact", "dual_impact_left", "dual_block", "l3_dual_block", "l3_dual_impact_l", true, NULL) \
    X(CLIP_DUAL_IMPACT_R, "nitoryu dual block right impact", "dual_impact_right", "dual_block", "l3_dual_block", "l3_dual_impact_r", true, NULL) \
    X(CLIP_MAIN_BLOCK, "nitoryu main block", "main_block", "main_block_single", "l3_main_block", "l3_main_block", false, NULL) \
    X(CLIP_MAIN_IMPACT, "nitoryu main block impact", "main_impact", "main_block_single", "l3_main_block", "l3_main_impact", true, NULL) \
    X(CLIP_OFF_BLOCK, "nitoryu offhand block", "offhand_block", "offhand_block", "l3_offhand_block", "l3_offhand_block", true, NULL) \
    X(CLIP_OFF_BLOCK_FWD, "nitoryu offhand block forward", "offhand_block_forward", "offhand_block", "l3_offhand_block_fwd", "l3_off_block_fwd", true, NULL) \
    X(CLIP_STANCE_HILO, "nitoryu stance high low", "stance_hilo", "idle_stances", "l3_stance_hilo", "l3_stance_hilo", true, NULL) \
    X(CLIP_STANCE_REV, "nitoryu stance reverse grip", "stance_rev", "idle_stances", "l3_stance_rev", "l3_stance_rev", true, NULL) \
    X(CLIP_STANCE_HILO_WALK, "nitoryu stance high low walk", "stance_hilo_walk", "idle_stances", "l3_stance_hilo_walk", "l3_stance_hilo_walk", false, NULL) \
    X(CLIP_ANGLED_L, "nitoryu angled block left", "angled_block_left", "angled_block", "l3_angled_left", "l3_angled_l", false, NULL) \
    X(CLIP_ANGLED_R, "nitoryu angled block right", "angled_block_right", "angled_block", "l3_angled_right", "l3_angled_r", false, NULL) \
    X(CLIP_ANGLED_L_IMPACT, "nitoryu angled block left impact", "angled_left_impact", "angled_block", "l3_angled_left_impact", "l3_angled_l_impact", true, NULL) \
    X(CLIP_ANGLED_R_IMPACT, "nitoryu angled block right impact", "angled_right_impact", "angled_block", "l3_angled_right_impact", "l3_angled_r_impact", true, NULL) \
    X(CLIP_BROKEN_ANGLED_L, "nitoryu guard broken angled left", "broken_angled_left", "guard_broken", "l3_broken_angled_left", "l3_broken_angled_left", true, NULL) \
    X(CLIP_BROKEN_ANGLED_R, "nitoryu guard broken angled right", "broken_angled_right", "guard_broken", "l3_broken_angled_right", "l3_broken_angled_right", true, NULL) \
    X(CLIP_BROKEN_SCISSOR, "nitoryu guard broken scissor", "broken_scissor", "guard_broken", "l3_broken_scissor", "l3_broken_scissor", true, NULL) \
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
    X(CLIP_STANCE_KENSEI, "nitoryu stance kensei", "stance_kensei", "idle_stances", "l3_stance_kensei", "l3_stance_kensei", true, NULL)
