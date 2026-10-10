#pragma once

#define NITORYU_INI_KEYS(X) \
    X("Animations.angled_block") \
    X("Animations.angled_block_swap") \
    X("Animations.back_guard") \
    X("Animations.beat_cut") \
    X("Animations.cross_finisher") \
    X("Animations.dash_slash") \
    X("Animations.double_chop") \
    X("Animations.dual_block") \
    X("Animations.dual_chiburi") \
    X("Animations.dual_cut") \
    X("Animations.dual_draw") \
    X("Animations.dual_sheathe") \
    X("Animations.dual_sheathe_main_blade") \
    X("Animations.fan_sweep") \
    X("Animations.figure_eight") \
    X("Animations.head_wrap") \
    X("Animations.high_block") \
    X("Animations.hit_taken") \
    X("Animations.idle_stances") \
    X("Animations.limp_arm_swing") \
    X("Animations.main_block") \
    X("Animations.main_block_single") \
    X("Animations.main_hand_cut") \
    X("Animations.off_backhand") \
    X("Animations.off_hand_cut") \
    X("Animations.off_leg_cut") \
    X("Animations.off_rising_cut") \
    X("Animations.offhand_block") \
    X("Animations.offhand_block_style") \
    X("Animations.one_two") \
    X("Animations.run_hold") \
    X("Animations.run_hold_free_weight") \
    X("Animations.scissor_block") \
    X("Animations.sheathe_yields_to_jobs") \
    X("Animations.spin") \
    X("Animations.three_cuts") \
    X("Animations.twin_sweep") \
    X("Animations.x_cut") \
    X("Animations.yield_to_reactions") \
    X("Balance.counter_chance") \
    X("Balance.dex_bonus") \
    X("Balance.divided_guard") \
    X("Balance.off_hand_damage") \
    X("Balance.off_hand_guard") \
    X("Balance.off_hand_share") \
    X("Balance.pressure_points") \
    X("Balance.tempo") \
    X("Balance.twin_share") \
    X("Compat.foreign_control") \
    X("Compat.hold_ms") \
    X("Compat.plugins") \
    X("Compat.run_ms") \
    X("Flow.attacks") \
    X("Flow.cap_bonus") \
    X("Flow.cooldown_ms") \
    X("Flow.dex_bonus") \
    X("Flow.duration_ms") \
    X("Flow.flow") \
    X("Flow.speed") \
    X("Flow.window_ms") \
    X("General.auto") \
    X("General.diag") \
    X("General.env") \
    X("General.language") \
    X("General.prod") \
    X("Guard.arc_deg") \
    X("Guard.back_min_tier") \
    X("Guard.back_share") \
    X("Guard.cooldown_ms") \
    X("Guard.counter") \
    X("Guard.counter_chance") \
    X("Guard.counter_window_ms") \
    X("Guard.guard") \
    X("Guard.parry_ms") \
    X("Guard.parry_pose") \
    X("LeftHand.block_penalty_at_min") \
    X("LeftHand.damage_at_min") \
    X("LeftHand.enabled") \
    X("LeftHand.min_ambidexterity") \
    X("LeftHand.speed_at_min") \
    X("Marks.0") \
    X("Marks.1") \
    X("Marks.flow") \
    X("Marks.height") \
    X("Marks.marks") \
    X("Marks.mugen") \
    X("Marks.on_name") \
    X("Marks.place") \
    X("Marks.pressure") \
    X("Marks.size") \
    X("Marks.tips") \
    X("Moves.1") \
    X("Moves.cross_arm_ms") \
    X("Moves.cross_riposte") \
    X("Moves.cross_riposte_chance") \
    X("Moves.cross_riposte_window_ms") \
    X("Moves.dash_cooldown_ms") \
    X("Moves.dash_slash") \
    X("Moves.double_hit_cooldown_ms") \
    X("Moves.even_picks") \
    X("Moves.far_pick_gap") \
    X("Moves.long_dash_shared_cooldown") \
    X("Moves.mirror_long_dash") \
    X("Moves.no_repeat") \
    X("Moves.offhand_carry") \
    X("Moves.spin") \
    X("Moves.spin_cooldown_ms") \
    X("Moves.spin_min_skill") \
    X("Mugen.chain") \
    X("Mugen.cooldown_ms") \
    X("Mugen.mugen") \
    X("NPC.dual") \
    X("NPC.max_tier") \
    X("NPC.opt_in") \
    X("NPC.path") \
    X("NPC.skill") \
    X("OffHand.blend_ms") \
    X("OffHand.bone") \
    X("OffHand.mirror_pose") \
    X("OffHand.pos") \
    X("OffHand.rot") \
    X("OffHand.show_blade") \
    X("OffHand.square_torso") \
    X("OffHand.swap_strikes") \
    X("OffHand.uncross") \
    X("Pair.equal_ratio") \
    X("Pair.equal_twin_share") \
    X("Pair.long_short_parry") \
    X("Pair.mixed_defence_share") \
    X("Pair.pair") \
    X("Pair.short_ratio") \
    X("Pressure.decay_ms") \
    X("Pressure.max_stacks") \
    X("Pressure.points") \
    X("Pressure.pressure") \
    X("Pressure.shout") \
    X("Pressure.tempo") \
    X("Scissor.chance") \
    X("Scissor.scissor") \
    X("Speed.dual_attack_cap") \
    X("Speed.dual_block_cap") \
    X("Speed.dual_speed_bonus") \
    X("Split.arc_deg") \
    X("Split.attacker_ms") \
    X("Split.chance") \
    X("Split.reach") \
    X("Split.split") \
    X("Stats.affects") \
    X("Stats.affects_title") \
    X("Stats.description") \
    X("Stats.description_title") \
    X("Stats.dex_bonus") \
    X("Stats.group") \
    X("Stats.heavy_armour") \
    X("Stats.in_fight") \
    X("Stats.label") \
    X("Stats.mark_flow") \
    X("Stats.mark_mugen") \
    X("Stats.mark_pressure") \
    X("Stats.marks_title") \
    X("Stats.next_level") \
    X("Stats.overloaded") \
    X("Stats.pair") \
    X("Stats.pair_equal") \
    X("Stats.pair_long_short") \
    X("Stats.pair_mixed") \
    X("Stats.panel_pair_bonus") \
    X("Stats.path_formula") \
    X("Stats.path_note") \
    X("Stats.path_title") \
    X("Stats.proficiency") \
    X("Stats.rank1") \
    X("Stats.rank1_opens") \
    X("Stats.rank2") \
    X("Stats.rank2_opens") \
    X("Stats.rank3") \
    X("Stats.rank3_opens") \
    X("Stats.rank4") \
    X("Stats.rank4_opens") \
    X("Stats.rank5") \
    X("Stats.rank5_opens") \
    X("Stats.rank_colours") \
    X("Stats.rank_in_title") \
    X("Stats.rank_needs") \
    X("Stats.rank_needs_dp") \
    X("Stats.rank_up") \
    X("Stats.rank_up_message") \
    X("Stats.rank_up_opens") \
    X("Stats.ranks_title") \
    X("Stats.show") \
    X("Stats.speed") \
    X("Stats.tier") \
    X("Stats.tier_capped") \
    X("Stats.train") \
    X("Stats.train_title") \
    X("Test.ambidexterity") \
    X("Test.follow_up_chance") \
    X("Test.force_tier") \
    X("Test.npc_ambidexterity") \
    X("Test.sound_trace") \
    X("Training.pair_dummy") \
    X("Training.xp_hit_taken") \
    X("Training.xp_outnumbered") \
    X("Training.xp_outnumbered_ms") \
    X("Training.xp_own_level") \
    X("Training.xp_parry") \
    X("Training.xp_riposte") \
    X("Training.xp_twin") \
    X("UI.any_weapon_button") \
    X("UI.button_size") \
    X("UI.button_tips") \
    X("UI.hotkey") \
    X("UI.toggle_button") \
    X("Weapons.categories") \

#define NITORYU_INI_LEGACY(X) \
    X("Test.l3_angled_left", "Animations.angled_block") \
    X("Test.l3_angled_left_impact", "Animations.angled_block") \
    X("Test.l3_angled_right", "Animations.angled_block") \
    X("Test.l3_angled_right_impact", "Animations.angled_block") \
    X("Test.l3_back_guard_high", "Animations.back_guard") \
    X("Test.l3_back_guard_high_main", "Animations.back_guard") \
    X("Test.l3_back_guard_low", "Animations.back_guard") \
    X("Test.l3_back_guard_low_main", "Animations.back_guard") \
    X("Test.l3_beat_cut", "Animations.beat_cut") \
    X("Test.l3_block", "Animations.high_block") \
    X("Test.l3_chiburi", "Animations.dual_chiburi") \
    X("Test.l3_cross", "Animations.x_cut") \
    X("Test.l3_dash", "Animations.dash_slash") \
    X("Test.l3_double_chop", "Animations.double_chop") \
    X("Test.l3_draw", "Animations.dual_draw") \
    X("Test.l3_dual_block", "Animations.dual_block") \
    X("Test.l3_dual_cut", "Animations.dual_cut") \
    X("Test.l3_fan_sweep", "Animations.fan_sweep") \
    X("Test.l3_figure_eight", "Animations.figure_eight") \
    X("Test.l3_head_wrap", "Animations.head_wrap") \
    X("Test.l3_hit_taken", "Animations.hit_taken") \
    X("Test.l3_main_block", "Animations.main_block_single") \
    X("Test.l3_main_hand_cut", "Animations.main_hand_cut") \
    X("Test.l3_main_overhead", "Animations.main_block") \
    X("Test.l3_main_overhead_impact", "Animations.main_block") \
    X("Test.l3_off_backhand", "Animations.off_backhand") \
    X("Test.l3_off_hand_cut", "Animations.off_hand_cut") \
    X("Test.l3_off_leg_cut", "Animations.off_leg_cut") \
    X("Test.l3_off_rising", "Animations.off_rising_cut") \
    X("Test.l3_offhand_block", "Animations.offhand_block") \
    X("Test.l3_offhand_block_fwd", "Animations.offhand_block") \
    X("Test.l3_one_two", "Animations.one_two") \
    X("Test.l3_run", "Animations.run_hold") \
    X("Test.l3_scissor", "Animations.scissor_block") \
    X("Test.l3_sheathe", "Animations.dual_sheathe") \
    X("Test.l3_spin", "Animations.spin") \
    X("Test.l3_stance_bushi", "Animations.idle_stances") \
    X("Test.l3_stance_deshi", "Animations.idle_stances") \
    X("Test.l3_stance_kensei", "Animations.idle_stances") \
    X("Test.l3_stance_kenshi", "Animations.idle_stances") \
    X("Test.l3_stance_ronin", "Animations.idle_stances") \
    X("Test.l3_three_cuts", "Animations.three_cuts") \
    X("Test.l3_twin_sweep", "Animations.twin_sweep") \
