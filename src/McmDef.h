// Nitoryu's page in a mod settings menu (MCM): the one table of what the player sees there.
//
// A settings menu that looks for the export MCM_Describe in every loaded plugin (src/Mcm.cpp) builds the page from
// this table. Nothing here depends on the menu being installed. A row shows the value in effect (an ini key that is
// present > FCS > the default) and writes the player's choice into Nitoryu.ini; a choice equal to what FCS or the
// default gives removes the key, so "reset" never masks a modpack's values.
//
// Every ini key and FCS setting is either a row here or a line in NITORYU_MCM_HIDDEN with the reason. Every
// label and tooltip is also in lang/en.ini, ru.ini, zh.ini and ja.ini.
//
// H(page, "id", "heading")
//     a heading inside the page; text key mcm.h.<id>
// X(page, "ini section", "ini key", kind, "default", lowest, highest, decimals, restart, "choices", "label", "tooltip")
//     page:    0 = "Nitoryu", 1 = "Nitoryu: extra" (what few players touch)
//     kind:    T toggle (1/0), I whole number, N number, C choice, S text
//     restart: 1 = the row is marked "after a restart" even though the settings table reloads its key
//              (a key that is not in SettingsDef.h is read once at start and is always marked)
//     choices: C only, "value=Label|value=Label"; text key mcm.<section>.<key>.<value>
//     label, tooltip: English; text keys mcm.<section>.<key> and mcm.<section>.<key>.tip
//     No double quotes inside a text. Only ASCII: the game's fonts lack dashes, guillemets and the ellipsis.
#pragma once

#define NITORYU_MCM_PAGES(P) \
    P(0, "Nitoryu", "Nitoryu", "Dual wielding: my animations one by one, the inventory buttons, NPCs, the speed caps and the look of the left arm") \
    P(1, "NitoryuExtra", "Nitoryu: extra", "The marks above the heads and the Dual Wielding line of the stats window")

#define NITORYU_MCM_ROWS(H, X)     H(0, "main", "Main")     X(0, "Main", "only_my_strikes", T, "1", 0, 1, 0, 0, "", "Only Nitoryu strikes",       "A fighter with two blades attacks only with Nitoryu's own strikes. Off = the game's swings and other mods' moves are mixed in, mirrored onto the left arm")     \
    H(0, "anim_run", "Animations: run") \
    X(0, "Animations", "run_style", C, "3", 0, 0, 0, 0, "0=Current|1=Stance 1|2=Stance 2|3=Stance 3|4=Stance 1 or 3", "Run style", \
      "Current = the blades trail over the game legs. Stances 1-3 lean deep and move the whole body. Stance 1 or 3 = one of the two per fighter") \
    \
    H(0, "anim_blocks", "Animations: blocks and hits") \
    X(0, "Animations", "angled_block", T, "1", 0, 1, 0, 0, "", "Angled block", \
      "A blow from the left or the right is met with both blades together, leaning to that side. Off = the game's mirrored block") \
    X(0, "Animations", "angled_block_swap", T, "0", 0, 1, 0, 0, "", "Angled block: swap sides", \
      "Turn it on if the angled block leans away from the blow instead of into it") \
    X(0, "Animations", "main_block", T, "1", 0, 1, 0, 0, "", "Overhead block", \
      "A blow from above is met with the right blade flat over the head") \
    X(0, "Animations", "scissor_block", T, "1", 0, 1, 0, 0, "", "Scissor block", \
      "A blow from the front is met with both blades crossed in an X") \
    X(0, "Animations", "main_block_single", T, "1", 0, 1, 0, 0, "", "One-blade block", \
      "A blow from the front is sometimes met with one blade: the right one up across the front, the left one back at the hip") \
    X(0, "Animations", "dual_block", T, "1", 0, 1, 0, 0, "", "Block recoil", \
      "When a blow lands on the crossed blades, the guard is pushed back once and comes back") \
    X(0, "Animations", "offhand_block", T, "1", 0, 1, 0, 0, "", "Second-blade parry", \
      "The left blade goes up across the front and the turn of the body throws the blow off. Off = the left blade is lifted over the head (the high block below)") \
    X(0, "Animations", "high_block", T, "1", 0, 1, 0, 0, "", "High block", \
      "The left blade lifted over the head: the parry used when the second-blade parry is off") \
    X(0, "Animations", "back_guard", T, "1", 0, 1, 0, 0, "", "Back guard", \
      "Ronin and up: a blow from behind is caught without turning round, the blade laid across the back") \
    X(0, "Animations", "hit_taken", T, "1", 0, 1, 0, 0, "", "Hit taken", \
      "A blow that lands while the fighter was not blocking rocks it back once; both blades stay in the hands") \
    X(0, "Animations", "yield_to_reactions", T, "1", 0, 1, 0, 0, "", "Game reactions first", \
      "Staggered, knocked down, knocked out, getting up: the game's own animation shows. Off = the dual pose stays on") \
    \
    H(0, "anim_draw", "Animations: draw, sheathe, stances") \
    X(0, "Animations", "dual_draw", T, "1", 0, 1, 0, 0, "", "Dual draw", \
      "The hands go up to the hilts behind the head, pull both blades and swing them out wide into the guard; the second one from the hip if it is worn there") \
    X(0, "Animations", "dual_draw_hold", T, "1", 0, 1, 0, 0, "", "Blades stay sheathed until gripped", \
      "In the dual draw both blades hang on the back until the hands take the hilts. Off = they are in the hands from the first frame") \
    X(0, "Animations", "dual_sheathe", T, "1", 0, 1, 0, 0, "", "Dual sheathe", \
      "A flick, the right blade home first, then the left. Only the look takes a moment: the game sheathes at once") \
    X(0, "Animations", "dual_chiburi", T, "1", 0, 1, 0, 0, "", "Blood flick", \
      "A fighter who sheathes right after a won fight first flicks both blades down and out, pauses, then sheathes") \
    X(0, "Animations", "sheathe_yields_to_jobs", T, "1", 0, 1, 0, 0, "", "No flourish before a job", \
      "The flick and the sheathe are skipped when the fighter goes straight on to first aid, a splint, looting or lifting someone") \
    X(0, "Animations", "idle_stances", T, "1", 0, 1, 0, 0, "", "A stance per rank", \
      "In a fight, when the fighter is not attacking, blocking or running, it holds the stance of its rank") \
    X(0, "Animations", "run_hold", T, "1", 0, 1, 0, 0, "", "Running with the blades", \
      "Both blades trail behind, the body leans into the run") \
    X(0, "Animations", "run_hold_free_weight", N, "0.5", 0, 1, 2, 0, "", "Run over another mod's run", \
      "Over another mod's run animation that mine cannot keep in step with, the torso takes this share of my run pose. 0 = none, 1 = all") \
    X(0, "Animations", "limp_arm_swing", T, "1", 0, 1, 0, 0, "", "Limp arm swings", \
      "Left-hand fighting: the crippled right arm swings as the body moves. Off = it hangs still") \
    \
    H(0, "anim_strikes", "Animations: strikes (off = the move is not used)") \
    X(0, "Animations", "off_hand_cut", T, "1", 0, 1, 0, 0, "", "Off-hand chest cut", \
      "A cut across the chest with the left blade") \
    X(0, "Animations", "main_hand_cut", T, "1", 0, 1, 0, 0, "", "Main-hand chest cut", \
      "A cut across the chest with the right blade") \
    X(0, "Animations", "off_belly_cut", T, "1", 0, 1, 0, 0, "", "Off-hand belly cut", \
      "A low cut across the belly with the left blade") \
    X(0, "Animations", "main_chop", T, "1", 0, 1, 0, 0, "", "Main-hand chop", \
      "The right blade comes down from over the head onto the head and collarbone") \
    X(0, "Animations", "off_shoulder_cut", T, "1", 0, 1, 0, 0, "", "Off-hand shoulder cut", \
      "The left blade falls from over the left shoulder through the neck") \
    X(0, "Animations", "main_level_cut", T, "1", 0, 1, 0, 0, "", "Main-hand level cut", \
      "The right blade loops up behind the shoulder and cuts across the chest without stopping") \
    X(0, "Animations", "off_high_cut", T, "1", 0, 1, 0, 0, "", "Off-hand high cut", \
      "The left blade loops down past the hip and rises into the neck without stopping") \
    X(0, "Animations", "main_short_cut", T, "1", 0, 1, 0, 0, "", "Main-hand backhand", \
      "The right blade from over the left shoulder, level at the head") \
    X(0, "Animations", "big_chop", T, "1", 0, 1, 0, 0, "", "Big chop", \
      "A strike from range: three steps in, the right blade comes down the diagonal from the neck to the hip") \
    X(0, "Animations", "off_leg_cut", T, "1", 0, 1, 0, 0, "", "Leg cut", \
      "The left blade goes for the legs") \
    X(0, "Animations", "off_backhand", T, "1", 0, 1, 0, 0, "", "Off-hand backhand", \
      "A backhand cut with the left blade") \
    X(0, "Animations", "one_two", T, "1", 0, 1, 0, 0, "", "One-two", \
      "The right blade, then the left, one after the other") \
    X(0, "Animations", "two_step", T, "1", 0, 1, 0, 0, "", "Two step", \
      "Ronin: a twin strike from range, the one-two walking in: the right blade down on the first step, the left across on the second") \
    X(0, "Animations", "double_chop", T, "1", 0, 1, 0, 0, "", "Double chop", \
      "Both blades chop down together") \
    X(0, "Animations", "dual_cut", T, "1", 0, 1, 0, 0, "", "Dual cut", \
      "Both blades cut in one swing") \
    X(0, "Animations", "twin_body_cut", T, "1", 0, 1, 0, 0, "", "Twin body cut", \
      "Bushi: one step in, both blades come in level from both sides, the left through the belly, the right through the chest") \
    X(0, "Animations", "twin_sweep", T, "1", 0, 1, 0, 0, "", "Twin sweep", \
      "A twin strike: both blades sweep across together") \
    X(0, "Animations", "twin_shear", T, "1", 0, 1, 0, 0, "", "Twin shear", \
      "Ronin: a twin strike, the blades cross going opposite ways, neck and thigh") \
    X(0, "Animations", "figure_eight", T, "1", 0, 1, 0, 0, "", "Figure eight", \
      "Ronin: a twin strike, each blade wheels round into its own diagonal without a stop") \
    X(0, "Animations", "head_wrap", T, "1", 0, 1, 0, 0, "", "Head wrap", \
      "Kenshi: a twin strike, the right blade swings round behind the head and it ends in a long lunge") \
    X(0, "Animations", "three_cuts", T, "1", 0, 1, 0, 0, "", "Three cuts", \
      "Kenshi: a twin strike, three small quick cuts walking in") \
    \
    H(0, "anim_specials", "Animations: the specials (off = the game's animation)") \
    X(0, "Animations", "x_cut", T, "1", 0, 1, 0, 0, "", "X cut", \
      "The right blade down, then the left: an X, with the whole body") \
    X(0, "Animations", "dash_slash", T, "1", 0, 1, 0, 0, "", "Dash slash", \
      "A sprint from a distance, the blades cross in an X and split out on the hit") \
    X(0, "Animations", "spin", T, "1", 0, 1, 0, 0, "", "Spin", \
      "A rush, a sweep with both blades, a side step, a turn rising into an X over the head and a cross-split") \
    \
    H(0, "buttons", "Inventory buttons") \
    X(0, "UI", "toggle_button", T, "1", 0, 1, 0, 0, "", "Two-blades button", \
      "The button on the back weapon slot of the inventory: turns fighting with two blades on and off for this character") \
    X(0, "UI", "any_weapon_button", T, "1", 0, 1, 0, 0, "", "Any-weapon button", \
      "A second button on a character who carries two blunt weapons or hackers: lets it fight with them as a pair. Saved with the character") \
    X(0, "UI", "button_tips", T, "1", 0, 1, 0, 0, "", "Button hints", \
      "The mouse over a button says what it does") \
    X(0, "UI", "button_size", I, "22", 12, 48, 0, 0, "", "Button size, px", \
      "The side of the inventory buttons in pixels") \
    X(0, "UI", "hotkey", S, "", 0, 0, 0, 0, "", "Key", \
      "A key that does the same as the two-blades button for the selected characters: alt+n, shift+n, n. Empty = no key") \
    \
    H(0, "npc", "NPCs") \
    X(0, "NPC", "dual", C, "all", 0, 0, 0, 0, "all=All|authored=Only those a mod names|off=None", "NPCs with two blades", \
      "Which NPCs fight with two blades when they carry an allowed pair") \
    X(0, "NPC", "max_tier", I, "3", 0, 5, 0, 0, "", "Highest NPC rank", \
      "The highest Dual Wielding rank an NPC fights at (0-5). NPCs a mod names are not capped") \
    \
    H(0, "speed", "Speed caps") \
    X(0, "Speed", "dual_attack_cap", N, "1.5", 0.5, 5, 2, 0, "", "Attack speed cap", \
      "A fighter with two blades cannot attack faster than this. The game's own cap is 1.2") \
    X(0, "Speed", "dual_block_cap", N, "4.0", 0.5, 10, 2, 0, "", "Block speed cap", \
      "A fighter with two blades cannot block faster than this. The game's own cap is 4.0") \
    \
    H(0, "offhand", "Left arm (leave it alone unless a race looks wrong)") \
    X(0, "OffHand", "show_blade", T, "1", 0, 1, 0, 0, "", "Show the second blade", \
      "The second blade is drawn in the left hand") \
    X(0, "OffHand", "mirror_pose", T, "1", 0, 1, 0, 0, "", "Left arm mirrors the right", \
      "The left arm takes the mirrored pose of the right one. Off = it hangs as the game leaves it") \
    X(0, "OffHand", "swap_strikes", T, "1", 0, 1, 0, 0, "", "Mirror off-hand strikes", \
      "The body is mirrored during a strike of the left blade, so the left arm does the cutting") \
    X(0, "OffHand", "square_torso", N, "0.8", 0, 1, 2, 0, "", "Square the chest", \
      "How far the chest turns to face the enemy in a fight. 0 = the game's side-on chest, 1 = fully square") \
    X(0, "OffHand", "uncross", N, "0.7", 0, 1.5, 2, 0, "", "Keep the hands apart", \
      "Each hand stays at least this share of the shoulder's offset on its own side, so the arms do not cross. 0 = off") \
    \
    H(1, "marks", "Marks above the heads") \
    X(1, "Marks", "marks", C, "1", 0, 0, 0, 0, "0=None|1=Selected characters|2=All my characters|3=Every fighter", "Show marks for", \
      "Small marks that show the rank skills at work: a wave = flow, a loop = a Mugen chain is waiting, pips over an enemy = pressure. What they mean is in the Dual Wielding description in the stats window") \
    X(1, "Marks", "place", C, "2", 0, 0, 0, 0, "2=Above the heads|1=On the squad portraits|3=Both", "Where", \
      "Where the marks are drawn") \
    X(1, "Marks", "flow", T, "1", 0, 1, 0, 0, "", "Flow mark", \
      "A wave for a fighter in flow, with a bar under it that runs out with the flow") \
    X(1, "Marks", "mugen", T, "1", 0, 1, 0, 0, "", "Mugen mark", \
      "A loop for a fighter whose chained twin strike is waiting") \
    X(1, "Marks", "pressure", T, "1", 0, 1, 0, 0, "", "Pressure mark", \
      "One pip per pressure stack; the pips change colour at full pressure") \
    X(1, "Marks", "tips", T, "0", 0, 1, 0, 0, "", "Mark hints", \
      "The mouse over a mark shows the skill's name and what it does") \
    X(1, "Marks", "size", I, "22", 8, 64, 0, 0, "", "Mark size, px", \
      "Above the heads: the side of a mark in pixels. On a portrait the marks follow the portrait's size") \
    X(1, "Marks", "on_name", T, "1", 0, 1, 0, 0, "", "On top of the name", \
      "Above the heads: the marks sit right on top of the character's name when it is shown. Off = always at the height below") \
    X(1, "Marks", "height", N, "24", 0, 100, 0, 0, "", "Height", \
      "Above the heads: how far above the feet the marks sit, in game units, when the name is not shown") \
    \
    H(1, "stats", "Stats window") \
    X(1, "Stats", "show", T, "1", 0, 1, 0, 0, "", "Dual Wielding line", \
      "The Dual Wielding line in the character's stats window") \
    X(1, "Stats", "group", C, "1", 0, 0, 0, 1, "1=End of the Weapons group|2=Combat group", "Line position", \
      "Where the Dual Wielding line sits in the stats window") \
    X(1, "Stats", "rank_colours", T, "1", 0, 1, 0, 0, "", "Rank colours", \
      "The rank is coloured like item grades") \
    X(1, "Stats", "rank_in_title", T, "0", 0, 1, 0, 0, "", "Rank in the window title", \
      "The rank is also written in the title of the stats window") \
    X(1, "Stats", "rank_up_message", T, "1", 0, 1, 0, 0, "", "Rank-up message", \
      "A message when a character reaches a new rank") \
    X(1, "Stats", "panel_pair_bonus", T, "1", 0, 1, 0, 0, "", "Pair in Attack and Defense", \
      "The character panel's Attack and Defense count the second blade while the pair is drawn")

// What the page leaves out, and why. "*" = the whole section.
// N("ini section", "ini key", "reason")
#define NITORYU_MCM_HIDDEN(N) \
    N("General", "*", "the language follows the game; env and diag are for test builds and bug reports") \
    N("Test", "*", "test overrides") \
    N("Compat", "*", "set by the mod that needs it, not by the player") \
    N("Weapons", "*", "a modder's lever: FCS") \
    N("Balance", "*", "balance: a modpack's levers (only the speed caps are for the player)") \
    N("Guard", "*", "balance and mechanic switches") \
    N("Pressure", "*", "balance and mechanic switches") \
    N("Flow", "*", "balance and mechanic switches") \
    N("Mugen", "*", "balance and mechanic switches") \
    N("Pair", "*", "balance and mechanic switches") \
    N("Scissor", "*", "balance and mechanic switches") \
    N("Split", "*", "balance and mechanic switches") \
    N("Training", "*", "balance") \
    N("LeftHand", "*", "balance and mechanic switches") \
    N("Moves", "*", "mechanic switches, cooldowns and the move picker's internals") \
    N("Speed", "dual_speed_bonus", "balance; the two caps are shown") \
    N("NPC", "opt_in", "a modder's lever: FCS") \
    N("NPC", "skill", "a modder's lever: FCS") \
    N("NPC", "path", "a modder's lever: FCS") \
    N("OffHand", "bone", "fits the blade to the skeleton") \
    N("OffHand", "pos", "fits the blade to the skeleton") \
    N("OffHand", "rot", "fits the blade to the skeleton") \
    N("OffHand", "blend_ms", "internal timing") \
    N("Animations", "cross_finisher", "old name of x_cut, still read") \
    N("Animations", "fan_sweep", "the fan sweep left the game: retired key, still known") \
    N("Animations", "beat_cut", "the beat cut left the game (the off shoulder cut replaced it): retired key, still known") \
    N("Animations", "off_rising_cut", "the off rising cut left the game (the off high cut replaced it): retired key, still known") \
    N("Animations", "offhand_block_style", "the second-blade parry has one look: retired key, still known") \
    N("Animations", "dual_sheathe_main_blade", "retired key, still known") \
    N("Stats", "*text*", "the texts of the stats window: translators set them in lang files")
