#pragma once

#define NITORYU_SETTINGS(X) \
    X(W_SKILL, F, "0.30", "nitoryu dp weight skill", "Nitoryu 01 dual proficiency", "", "", \
      "Dual proficiency (DP, 0-100) is a weighted average of five inputs, each capped at 100. This is the weight of the lower of the two weapon skills. The five weights should sum to 1.") \
    X(W_DEX, F, "0.20", "nitoryu dp weight dexterity", "Nitoryu 01 dual proficiency", "", "", \
      "Weight of Dexterity in dual proficiency.") \
    X(W_ATTACK, F, "0.10", "nitoryu dp weight attack", "Nitoryu 01 dual proficiency", "", "", \
      "Weight of Melee Attack in dual proficiency.") \
    X(W_DEFENCE, F, "0.10", "nitoryu dp weight defence", "Nitoryu 01 dual proficiency", "", "", \
      "Weight of Melee Defence in dual proficiency.") \
    X(W_AMBI, F, "0.30", "nitoryu dp weight ambidexterity", "Nitoryu 01 dual proficiency", "", "", \
      "Weight of effective Ambidexterity (after overload and armour) in dual proficiency.") \
    X(TIER_DP, L, "20,40,60,75,90", "nitoryu tier dp", "Nitoryu 01 dual proficiency", "", "", \
      "Five numbers: the DP a fighter needs for tiers T1..T5. Moves and mechanics unlock per tier.") \
    X(TIER_MIN_AMBI, L, "0,0,40,60,80", "nitoryu tier min ambidexterity", "Nitoryu 01 dual proficiency", "", "", \
      "Five numbers: the effective Ambidexterity a fighter also needs for tiers T1..T5.") \
    X(AMB_SEED, F, "0.25", "nitoryu ambidexterity seed", "Nitoryu 01 dual proficiency", "", "", \
      "A character with no saved Ambidexterity starts at this share of its best weapon skill.") \
    X(AMB_XP_SHARE, F, "0.5", "nitoryu ambidexterity xp share", "Nitoryu 01 dual proficiency", "", "", \
      "Share of the weapon XP of a dual or left-handed hit that also trains Ambidexterity.") \
    X(WEIGHT_MULT, F, "1.25", "nitoryu pair weight mult", "Nitoryu 01 dual proficiency", "", "", \
      "Strength rule: a pair weighs this x (main weapon + second weapon) in the weight and speed formula.") \
    \
    X(OFF_HAND_SHARE, C, "20:0.03,40:0.10,60:0.18,75:0.30,85:0.40,90:0.50,100:0.55", "nitoryu curve off hand share", "Nitoryu 02 curves by DP", "Balance", "off_hand_share", \
      "Curve DP:value. Share of attacks that use the second blade (an off-hand strike or a twin strike).") \
    X(TWIN_SHARE, C, "59.99:0,60:0.25,100:0.45", "nitoryu curve twin share", "Nitoryu 02 curves by DP", "Balance", "twin_share", \
      "Curve DP:value. Of the second blade's attacks, the share that are twin strikes (both blades).") \
    X(OFF_HAND_DAMAGE, C, "20:1.04,40:1.14,70:1.30", "nitoryu curve off hand damage", "Nitoryu 02 curves by DP", "Balance", "off_hand_damage", \
      "Curve DP:value. Damage of a second-blade strike, x the second weapon's own damage.") \
    X(OFF_HAND_GUARD, C, "39.99:0,40:0.20,60:0.30,75:0.40,90:0.50,100:0.55", "nitoryu curve off hand guard", "Nitoryu 02 curves by DP", "Balance", "off_hand_guard", \
      "Curve DP:value. Base chance that the second blade parries a hit the main blade missed.") \
    X(COUNTER_CHANCE, C, "59.99:0,60:0.30,100:0.60", "nitoryu curve counter chance", "Nitoryu 02 curves by DP", "Balance", "counter_chance", \
      "Curve DP:value. Share of successful blocks answered by a riposte.") \
    X(PRESSURE_POINTS, C, "60:1,100:1", "nitoryu curve pressure points", "Nitoryu 02 curves by DP", "Balance", "pressure_points", \
      "Curve DP:value. Attack levels each pressure stack counts for in the who-attacks-first contest.") \
    X(TEMPO_POINTS, C, "59.99:0,60:1.0,80:3.0,100:5.0", "nitoryu curve tempo", "Nitoryu 02 curves by DP", "Balance", "tempo", \
      "Curve DP:value. Attack levels of initiative a dual master always has in the who-attacks-first contest.") \
    X(DIVIDED_GUARD, L, "4,4,2,0,0,0", "nitoryu divided guard", "Nitoryu 02 curves by DP", "Balance", "divided_guard", \
      "Six numbers, tiers T0..T5: extra attack levels an enemy counts against a dual fighter's block.") \
    X(DEX_BONUS, F, "0.12", "nitoryu dexterity bonus", "Nitoryu 02 curves by DP", "Balance", "dex_bonus", \
      "A dual fighter's Dexterity counts this much more in the cut damage and the attack and block speed (0.12 = +12 percent, 0 = off).") \
    \
    X(CLUMSY0, F, "10", "nitoryu clumsiness base", "Nitoryu 03 penalties", "", "", \
      "Clumsiness: Melee Attack and Defence points lost at DP 0 (base - slope x DP, never below 0).") \
    X(CLUMSY_SLOPE, F, "0.25", "nitoryu clumsiness slope", "Nitoryu 03 penalties", "", "", \
      "Clumsiness points regained per point of DP.") \
    X(OVERLOAD_FLOOR, F, "0.2", "nitoryu overload floor", "Nitoryu 03 penalties", "", "", \
      "Carrying over capacity lowers effective Ambidexterity; it never falls below this factor.") \
    X(ARMOUR_PENALTY, F, "2.0", "nitoryu armour penalty", "Nitoryu 03 penalties", "", "", \
      "Heavy armour: effective Ambidexterity loses this x the shortfall of the combat speed multiplier below 1.") \
    X(ARMOUR_FLOOR, F, "0.3", "nitoryu armour floor", "Nitoryu 03 penalties", "", "", \
      "The armour factor on Ambidexterity never falls below this.") \
    X(OFF_WEAPON_BONUS_SHARE, F, "0.5", "nitoryu off weapon bonus share", "Nitoryu 03 penalties", "", "", \
      "The second weapon adds this share of its own attack, defence and indoor bonuses.") \
    \
    X(SPEED0, F, "0.85", "nitoryu speed at dp 0", "Nitoryu 04 speed", "", "", \
      "Attack and block speed factor of a beginner at DP 0; it rises to 1 at the full-speed DP.") \
    X(SPEED_FULL_DP, F, "30", "nitoryu speed full dp", "Nitoryu 04 speed", "", "", \
      "DP at which the beginner's speed penalty is gone.") \
    X(SPEED_BONUS, F, "0.3", "nitoryu speed bonus", "Nitoryu 04 speed", "Speed", "dual_speed_bonus", \
      "Speed bonus of a master: x(1 + this) of the game's speed at Dexterity 100 and DP 100.") \
    X(SPEED_FROM, F, "30", "nitoryu speed bonus from", "Nitoryu 04 speed", "", "", \
      "Dexterity and DP at which the speed bonus starts to grow.") \
    X(DUAL_ATTACK_CAP, F, "1.5", "nitoryu attack speed cap", "Nitoryu 04 speed", "Speed", "dual_attack_cap", \
      "A dual fighter's attack speed stops here (the game's own stops at 1.2).") \
    X(DUAL_BLOCK_CAP, F, "4.0", "nitoryu block speed cap", "Nitoryu 04 speed", "Speed", "dual_block_cap", \
      "A dual fighter's block speed stops here (the game's own cap is 4.0).") \
    \
    X(GUARD_ON, B, "1", "nitoryu guard", "Nitoryu 05 guard and counter", "Guard", "guard", \
      "The second blade may parry a hit the main blade missed.") \
    X(GUARD_MIN_TIER, I, "2", "nitoryu guard min tier", "Nitoryu 05 guard and counter", "", "", \
      "Tier from which the second blade parries.") \
    X(GUARD_COOLDOWN_MS, I, "1000", "nitoryu guard cooldown ms", "Nitoryu 05 guard and counter", "Guard", "cooldown_ms", \
      "At most one parry by the second blade per this many milliseconds of game time.") \
    X(GUARD_ARC_DEG, I, "110", "nitoryu guard arc deg", "Nitoryu 05 guard and counter", "Guard", "arc_deg", \
      "The second blade parries only attackers within this angle of the fighter's facing, either side.") \
    X(COMBO_GUARD_SHARE, F, "0.5", "nitoryu guard share combo", "Nitoryu 05 guard and counter", "", "", \
      "Share of the guard chance against a later hit of an attacker's combo.") \
    X(MAIN_GUARD_SHARE, F, "0.5", "nitoryu guard share main", "Nitoryu 05 guard and counter", "", "", \
      "Share of the guard chance against the first hit of the enemy the main blade faces.") \
    X(MAIN_GUARD_MIN_TIER, I, "3", "nitoryu guard main min tier", "Nitoryu 05 guard and counter", "", "", \
      "Tier from which the second blade also parries the first hit of the enemy the main blade faces.") \
    X(COUNTER_ON, B, "1", "nitoryu counter", "Nitoryu 05 guard and counter", "Guard", "counter", \
      "The first attack after a successful block may be an off-hand riposte.") \
    X(COUNTER_MIN_TIER, I, "3", "nitoryu counter min tier", "Nitoryu 05 guard and counter", "", "", \
      "Tier from which ripostes happen.") \
    X(COUNTER_WINDOW_MS, I, "1500", "nitoryu counter window ms", "Nitoryu 05 guard and counter", "Guard", "counter_window_ms", \
      "How long after the block the riposte may start, in milliseconds of game time.") \
    X(COUNTER_CHANCE_FIXED, F, "-1", "nitoryu counter chance fixed", "Nitoryu 05 guard and counter", "Guard", "counter_chance", \
      "0-1: one riposte chance for every DP instead of the curve. -1 = use the curve.") \
    \
    X(PRESSURE_ON, B, "1", "nitoryu pressure", "Nitoryu 06 pressure and tempo", "Pressure", "pressure", \
      "Every hit of a master that the enemy blocks puts a stack of pressure on that enemy.") \
    X(PRESSURE_MIN_TIER, I, "3", "nitoryu pressure min tier", "Nitoryu 06 pressure and tempo", "", "", \
      "Tier from which a fighter builds pressure.") \
    X(PRESSURE_MAX_STACKS, I, "5", "nitoryu pressure max stacks", "Nitoryu 06 pressure and tempo", "Pressure", "max_stacks", \
      "Most stacks one target can carry (1-20).") \
    X(PRESSURE_DECAY_MS, I, "3000", "nitoryu pressure decay ms", "Nitoryu 06 pressure and tempo", "Pressure", "decay_ms", \
      "One stack fades per this many milliseconds of game time without a new one.") \
    X(PRESSURE_POINTS_FIXED, F, "-1", "nitoryu pressure points fixed", "Nitoryu 06 pressure and tempo", "Pressure", "points", \
      "Attack levels per stack for every DP instead of the curve. -1 = use the curve.") \
    X(PRESSURE_SHOUT_STACKS, I, "3", "nitoryu pressure shout stacks", "Nitoryu 06 pressure and tempo", "", "", \
      "Stacks from which the target says it cannot find an opening (test builds only).") \
    X(TEMPO_ON, B, "0", "nitoryu tempo", "Nitoryu 06 pressure and tempo", "Pressure", "tempo", \
      "Off since 0.41.1 (flow replaced it). 1 = a dual master keeps some initiative of its own (the tempo curve).") \
    \
    X(SPECIAL_MIN_AMBI, F, "50", "nitoryu special min ambidexterity", "Nitoryu 07 moves", "", "", \
      "Effective Ambidexterity needed for the special moves (with tier 3).") \
    X(DOUBLE_HIT_COOLDOWN_MS, I, "5000", "nitoryu double hit cooldown ms", "Nitoryu 07 moves", "Moves", "double_hit_cooldown_ms", \
      "After a move that hits with both blades the next one waits this many milliseconds of game time. 0 = no cooldown.") \
    X(NO_REPEAT, B, "1", "nitoryu no repeat", "Nitoryu 07 moves", "Moves", "no_repeat", \
      "The second blade does not use the same move twice in a row when another one fits.") \
    X(OFF_CARRY_ON, B, "1", "nitoryu off hand carry", "Nitoryu 07 moves", "Moves", "offhand_carry", \
      "A second-blade turn that found no move in reach waits for the next attack instead of being lost.") \
    X(OFF_CARRY_MS, I, "3000", "nitoryu off hand carry ms", "Nitoryu 07 moves", "", "", \
      "How long such a turn waits, in milliseconds of game time.") \
    X(FAR_PICK_GAP, F, "35", "nitoryu far pick gap", "Nitoryu 07 moves", "Moves", "far_pick_gap", \
      "An attack picked from farther than this beyond the fighter's reach is left to the game.") \
    X(MIRROR_LONG_DASH, B, "1", "nitoryu mirror long dash", "Nitoryu 07 moves", "Moves", "mirror_long_dash", \
      "Another mod's long move is mirrored onto the left arm and the second blade hits too (fighters with the special moves).") \
    X(CROSS_RIPOSTE, B, "1", "nitoryu cross riposte", "Nitoryu 07 moves", "Moves", "cross_riposte",       "The Cross also answers a block or a second-blade parry (the riposte finisher), besides a stagger and full pressure.")     X(CROSS_RIPOSTE_WINDOW_MS, I, "1500", "nitoryu cross riposte window ms", "Nitoryu 07 moves", "Moves", "cross_riposte_window_ms",       "How long after a block or parry the riposte finisher may come, in milliseconds of game time.")     X(CROSS_ARM_MS, I, "2000", "nitoryu cross arm ms", "Nitoryu 07 moves", "Moves", "cross_arm_ms",       "A Cross trigger that comes at a pick made too far away waits this long for the next pick in reach. 0 = no wait.")     X(LONG_DASH_SHARED, B, "1", "nitoryu long dash shared cooldown", "Nitoryu 07 moves", "Moves", "long_dash_shared_cooldown", \
      "Every long dash of a dual fighter (the Dash slash or another mod's long move) shares the Dash slash's cooldown; the spin keeps its own.") \
    X(LONG_DASH_MIN, F, "20", "nitoryu long move min distance", "Nitoryu 07 moves", "", "", \
      "Attack distance from which another mod's move counts as a long move.") \
    X(FOREIGN_REPICK_OVER, F, "25", "nitoryu two handed repick over", "Nitoryu 07 moves", "", "", \
      "A fighter without the special moves picks again when another mod's two-handed move is longer than this.") \
    \
    X(NPC_MODE, S, "all", "nitoryu npc dual", "Nitoryu 08 NPCs", "NPC", "dual", \
      "Which NPCs fight with two weapons: all (anyone with an allowed pair and the stats), authored (only NPCs a mod opts in), off.") \
    X(NPC_MAX_TIER, I, "3", "nitoryu npc max tier", "Nitoryu 08 NPCs", "NPC", "max_tier", \
      "The highest tier an NPC fights at (0-5). Opted-in NPCs are not capped.") \
    X(NPC_MIN_DEX, F, "30", "nitoryu npc min dexterity", "Nitoryu 08 NPCs", "", "", \
      "Dexterity an NPC needs to fight dual (opted-in NPCs skip it).") \
    X(NPC_MIN_SKILL, F, "30", "nitoryu npc min skill", "Nitoryu 08 NPCs", "", "", \
      "Skill with both weapons an NPC needs to fight dual (opted-in NPCs skip it).") \
    X(NPC_STR_SHARE, F, "0.5", "nitoryu npc strength share", "Nitoryu 08 NPCs", "", "", \
      "An NPC needs Strength of at least this x the pair's weight (opted-in NPCs skip it).") \
    \
    X(WEAPON_CATEGORIES, L, "0,1", "nitoryu weapon categories", "Nitoryu 09 weapons", "Weapons", "categories", \
      "Weapon categories allowed in a pair: 0 katanas, 1 sabres, 2 blunt, 4 hackers. Heavy weapons, bows and polearms are always refused.") \
    \
    X(LEFTY_ON, B, "1", "nitoryu left hand", "Nitoryu 10 left hand", "LeftHand", "enabled", \
      "A character whose right arm is crippled keeps fighting with a one-handed weapon in the left hand.") \
    X(LEFTY_MIN_AMBI, F, "50", "nitoryu left hand min ambidexterity", "Nitoryu 10 left hand", "LeftHand", "min_ambidexterity", \
      "Ambidexterity needed to fight left-handed.") \
    X(LEFTY_DAMAGE_AT_MIN, F, "0.7", "nitoryu left hand damage at min", "Nitoryu 10 left hand", "LeftHand", "damage_at_min", \
      "Damage factor at the minimum Ambidexterity; it fades to 1 at Ambidexterity 100.") \
    X(LEFTY_SPEED_AT_MIN, F, "0.85", "nitoryu left hand speed at min", "Nitoryu 10 left hand", "LeftHand", "speed_at_min", \
      "Attack and block speed factor at the minimum Ambidexterity; it fades to 1 at Ambidexterity 100.") \
    X(LEFTY_BLOCK_AT_MIN, F, "10", "nitoryu left hand block penalty at min", "Nitoryu 10 left hand", "LeftHand", "block_penalty_at_min", \
      "Defence points lost when blocking at the minimum Ambidexterity; none at 100.") \
    \
    X(FLOW_ON, B, "1", "nitoryu flow", "Nitoryu 11 flow and mugen", "Flow", "flow", \
      "Flow: a dual fighter who has been attacked several times in a row concentrates and fights faster and harder for a few seconds.") \
    X(FLOW_MIN_TIER, I, "4", "nitoryu flow min tier", "Nitoryu 11 flow and mugen", "", "", \
      "Tier from which a fighter enters flow.") \
    X(FLOW_ATTACKS, I, "3", "nitoryu flow attacks", "Nitoryu 11 flow and mugen", "Flow", "attacks", \
      "Enemy attacks aimed at the fighter (blocked, parried or landed) that start flow (1-8).") \
    X(FLOW_WINDOW_MS, I, "5000", "nitoryu flow window ms", "Nitoryu 11 flow and mugen", "Flow", "window_ms", \
      "Those attacks must come within this many milliseconds of game time.") \
    X(FLOW_DURATION_MS, I, "4000", "nitoryu flow duration ms", "Nitoryu 11 flow and mugen", "Flow", "duration_ms", \
      "How long flow lasts, in milliseconds of game time. A knockdown ends it at once.") \
    X(FLOW_COOLDOWN_MS, I, "5000", "nitoryu flow cooldown ms", "Nitoryu 11 flow and mugen", "Flow", "cooldown_ms", \
      "After flow ends, attacks are not counted for this many milliseconds of game time (none are counted during flow either).") \
    X(FLOW_DEX_BONUS, F, "0.12", "nitoryu flow dexterity bonus", "Nitoryu 11 flow and mugen", "Flow", "dex_bonus", \
      "In flow the fighter's Dexterity counts this much more in the cut damage, on top of the dexterity bonus (0.30 = +30 percent).") \
    X(FLOW_SPEED, F, "0.04", "nitoryu flow speed", "Nitoryu 11 flow and mugen", "Flow", "speed", \
      "In flow the attack speed is x(1 + this).") \
    X(FLOW_CAP_BONUS, F, "0.3", "nitoryu flow attack cap bonus", "Nitoryu 11 flow and mugen", "Flow", "cap_bonus", \
      "In flow the attack speed cap is this much higher.") \
    X(MUGEN_ON, B, "1", "nitoryu mugen", "Nitoryu 11 flow and mugen", "Mugen", "mugen", \
      "Mugen: a master's moves that hit with both blades come back sooner, and a twin strike that lands may chain another one.") \
    X(MUGEN_MIN_TIER, I, "5", "nitoryu mugen min tier", "Nitoryu 11 flow and mugen", "", "", \
      "Tier from which a fighter has Mugen.") \
    X(MUGEN_COOLDOWN_MS, C, "90:3500,100:2000", "nitoryu curve mugen cooldown ms", "Nitoryu 11 flow and mugen", "Mugen", "cooldown_ms", \
      "Curve DP:value. The double hit cooldown of a fighter with Mugen, in milliseconds of game time (never longer than the plain one).") \
    X(MUGEN_CHAIN, C, "90:0.25,100:0.5", "nitoryu curve mugen chain", "Nitoryu 11 flow and mugen", "Mugen", "chain", \
      "Curve DP:value. Chance that a twin strike that lands makes the next attack a twin too, whatever the cooldown.") \
    X(MUGEN_CHAIN_WINDOW_MS, I, "2500", "nitoryu mugen chain window ms", "Nitoryu 11 flow and mugen", "", "", \
      "How long a chained twin waits for the next attack, in milliseconds of game time.") \
    \
    X(PAIR_ON, B, "1", "nitoryu pair bonuses", "Nitoryu 12 two blades", "Pair", "pair", \
      "What you pair matters: a long and a short blade parry better, two equal blades twin more, a pair of two weapon types takes the second weapon's defence bonus in full.") \
    X(PAIR_SHORT_RATIO, F, "0.80", "nitoryu pair short ratio", "Nitoryu 12 two blades", "Pair", "short_ratio", \
      "Long + short: the shorter blade's length is at most this share of the longer one's (katana 21 + wakizashi 16 = 0.76).") \
    X(PAIR_EQUAL_RATIO, F, "0.90", "nitoryu pair equal ratio", "Nitoryu 12 two blades", "Pair", "equal_ratio", \
      "Equal blades: the shorter blade's length is at least this share of the longer one's.") \
    X(PAIR_GUARD_MULT, F, "1.30", "nitoryu pair long short parry", "Nitoryu 12 two blades", "Pair", "long_short_parry", \
      "Long + short: the second blade's parry chance x this.") \
    X(PAIR_TWIN_MULT, F, "1.30", "nitoryu pair equal twin share", "Nitoryu 12 two blades", "Pair", "equal_twin_share", \
      "Equal blades: the twin share x this.") \
    X(PAIR_MIXED_DEFENCE, F, "1.0", "nitoryu pair mixed defence share", "Nitoryu 12 two blades", "Pair", "mixed_defence_share", \
      "Two weapon types in the pair (katana + sabre): the second weapon's defence bonus counts this share instead of the off weapon bonus share. A penalty keeps the usual share.") \
    X(SCISSOR_ON, B, "1", "nitoryu scissor", "Nitoryu 12 two blades", "Scissor", "scissor", \
      "Scissor: the second blade of a twin strike may land on the same body part as the first.") \
    X(SCISSOR_CHANCE, C, "59.99:0,60:0.8,75:1.0", "nitoryu curve scissor chance", "Nitoryu 12 two blades", "Scissor", "chance", \
      "Curve DP:value. Chance that the second blade of a twin goes to the body part the first blade hit.") \
    X(SCISSOR_WINDOW_MS, I, "1000", "nitoryu scissor window ms", "Nitoryu 12 two blades", "", "", \
      "The two blades of a twin count as one scissor within this many milliseconds of game time.") \
    X(SPLIT_ON, B, "1", "nitoryu split", "Nitoryu 12 two blades", "Split", "split", \
      "Two enemies, two blades: the left blade of a twin strike lands on a second enemy who is attacking the fighter from the flank, instead of the main target.") \
    X(SPLIT_MIN_TIER, I, "4", "nitoryu split min tier", "Nitoryu 12 two blades", "", "", \
      "Tier from which the left blade of a twin may go to a second enemy.") \
    X(SPLIT_CHANCE, C, "75:1.0,100:1.0", "nitoryu curve split chance", "Nitoryu 12 two blades", "Split", "chance", \
      "Curve DP:value. Chance that the left blade of a twin goes to a flanker in reach.") \
    X(SPLIT_ARC_DEG, I, "180", "nitoryu split arc deg", "Nitoryu 12 two blades", "Split", "arc_deg", \
      "The flanker must stand within this angle in front of the fighter (180 = the front half).") \
    X(SPLIT_REACH, F, "2.5", "nitoryu split reach", "Nitoryu 12 two blades", "Split", "reach", \
      "The flanker must be within this x the second blade's reach (half its length, as the game counts it) plus both bodies.") \
    X(SPLIT_ATTACKER_MS, I, "8000", "nitoryu split attacker ms", "Nitoryu 12 two blades", "Split", "attacker_ms", \
      "A flanker is an enemy who attacked the fighter within this many milliseconds of game time.") \
    \
    X(DUMMY_ON, B, "1", "nitoryu pair dummy training", "Nitoryu 13 training", "Training", "pair_dummy", \
      "A training building whose record is ticked nitoryu pair dummy trains Dual Wielding up to its output rate, for a character carrying an allowed pair.") \
    X(AMB_XP_PARRY, F, "2.0", "nitoryu dual wielding xp parry", "Nitoryu 13 training", "Training", "xp_parry", \
      "Dual Wielding XP by deed: a parry with the second blade trains Dual Wielding this many times more than a plain block (the game gives a block a tenth of a hit's weapon XP). 1 = as a block.") \
    X(AMB_XP_TWIN, F, "1.5", "nitoryu dual wielding xp twin", "Nitoryu 13 training", "Training", "xp_twin", \
      "Dual Wielding XP by deed: a hit that lands from a move of both blades (a twin strike, a finisher, an opener) trains Dual Wielding this many times more. 1 = as any hit.") \
    X(AMB_XP_RIPOSTE, F, "1.5", "nitoryu dual wielding xp riposte", "Nitoryu 13 training", "Training", "xp_riposte", \
      "Dual Wielding XP by deed: the impacts of a riposte (the attack that answers a block) train Dual Wielding this many times more. With a twin riposte the higher of the two factors counts. 1 = as any attack.") \
    X(AMB_XP_OUTNUMBERED, F, "1.5", "nitoryu dual wielding xp outnumbered", "Nitoryu 13 training", "Training", "xp_outnumbered", \
      "Dual Wielding XP by deed: while two or more enemies attack the fighter, everything trains Dual Wielding this many times more (on top of the factors above). 1 = off.") \
    X(AMB_XP_OUTNUMBERED_MS, I, "5000", "nitoryu dual wielding xp outnumbered ms", "Nitoryu 13 training", "Training", "xp_outnumbered_ms", \
      "The fighter is outnumbered while a second enemy's attack reached it (blocked or landed) within this many milliseconds of game time.") \
    X(AMB_XP_HIT_TAKEN, F, "0.25", "nitoryu dual wielding xp hit taken", "Nitoryu 13 training", "Training", "xp_hit_taken", \
      "A hit taken in the dual stance trains Dual Wielding: this share of the Melee Defence XP the game gives for that hit, which is higher the stronger the attacker is. 0 = off.") \
    \
    X(FOREIGN_MODE, S, "auto", "nitoryu foreign control", "Nitoryu 14 other plugins", "Compat", "foreign_control", \
      "Another plugin that plays swings, blocks and weapon switches itself, outside the game's own combat (a direct-control mod): off = Nitoryu ignores it; auto = Nitoryu's animations give way only when one of the plugins below is loaded; on = always.") \
    X(FOREIGN_PLUGINS, S, "ShoulderCombat.dll", "nitoryu foreign control plugins", "Nitoryu 14 other plugins", "Compat", "plugins", \
      "DLL file names, comma separated. With foreign control on auto, one of them loaded turns the giving way on.") \
    X(FOREIGN_HOLD_MS, I, "6000", "nitoryu foreign control hold ms", "Nitoryu 14 other plugins", "Compat", "hold_ms", \
      "After the other plugin's last swing or block, for this many milliseconds of game time the run animation waits for a steady run (run ms). A draw or a sheathe under its swing gets no animation, the blades follow at once.") \
    X(FOREIGN_RUN_MS, I, "300", "nitoryu foreign control run ms", "Nitoryu 14 other plugins", "Compat", "run_ms", \
      "In that time the run animation starts only after this many milliseconds of running without a stop.") \
    \
    X(HUD_MARKS, I, "1", "nitoryu marks", "Nitoryu 15 marks", "Marks", "marks", \
      "Marks that show the rank skills at work, without text. 0 = none, 1 = the characters the player has selected (and the enemies they press), 2 = all of the player's characters, 3 = every fighter.") \
    X(HUD_PLACE, I, "2", "nitoryu marks place", "Nitoryu 15 marks", "Marks", "place", \
      "Where the marks show: 2 = above the heads, 1 = on the squad portraits (all of the player's characters, whatever nitoryu marks says), 3 = both.") \
    X(HUD_FLOW, B, "1", "nitoryu marks flow", "Nitoryu 15 marks", "Marks", "flow", \
      "The flow mark: a wave for a fighter in flow, with a bar under it that runs out with the flow.") \
    X(HUD_MUGEN, B, "1", "nitoryu marks mugen", "Nitoryu 15 marks", "Marks", "mugen", \
      "The Mugen mark: a loop for a fighter whose chained twin strike is waiting.") \
    X(HUD_PRESSURE, B, "1", "nitoryu marks pressure", "Nitoryu 15 marks", "Marks", "pressure", \
      "The pressure mark: one pip per stack, on the portrait of the fighter who presses (the most stacks on one enemy) or above the pressured fighter; the pips change colour at full pressure.") \
    X(HUD_SIZE, I, "22", "nitoryu marks size", "Nitoryu 15 marks", "Marks", "size", \
      "Above the heads: side of a mark in pixels (8-64). A pressure pip is half of it. On a portrait the marks follow the portrait's size.") \
    X(HUD_HEIGHT, F, "24", "nitoryu marks height", "Nitoryu 15 marks", "Marks", "height", \
      "Above the heads: how far above a character's feet the marks sit, in game units, when the character's name is not shown (or on name is 0).") \
    X(HUD_ON_NAME, B, "1", "nitoryu marks on name", "Nitoryu 15 marks", "Marks", "on_name", \
      "Above the heads: the marks sit right on top of the game's name label over the character when it is shown; 0 = always at height.") \
    X(HUD_TIPS, B, "0", "nitoryu marks tips", "Nitoryu 15 marks", "Marks", "tips", \
      "The mouse over a mark shows the skill's name and what it does.") \
    \
    X(STATS_GROUP, I, "1", "nitoryu stats line group", "Nitoryu 16 stats window", "Stats", "group", \
      "Where the Dual Wielding line sits in the character's stats window: 1 = at the end of the Weapons group; 2 = in the Combat group, after its last skill (the Ranged group moves down a row).")
