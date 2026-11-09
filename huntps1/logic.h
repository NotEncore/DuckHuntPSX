/*
 * Host-testable HuntPS1 Game A tables and HUD/dog helpers.
 * GunCon map/apply math lives in guncal and is not duplicated here.
 */
#ifndef HUNTPS1_LOGIC_H
#define HUNTPS1_LOGIC_H

#define HUNT_CAL_SHOTS       3
#define HUNT_CAL_R         20
#define HUNT_CAL_POINTS    5
#define HUNT_SCREEN_W      320
#define HUNT_SCREEN_H      240
/* Visible framebuffer rectangle and inset to the CIRCLE EDGE (not the center). */
#define HUNT_CAL_VIS_X0    0
#define HUNT_CAL_VIS_Y0    0
#define HUNT_CAL_VIS_X1    320
#define HUNT_CAL_VIS_Y1    240
#define HUNT_CAL_INSET_X_L 12
#define HUNT_CAL_INSET_X_R 36
#define HUNT_CAL_INSET_X   HUNT_CAL_INSET_X_L
#define HUNT_CAL_INSET_Y   20

#define NES_SRC_W          256
#define NES_SRC_H          240
#define GRASS_Y            176
#define GRASS_SPLIT_Y      160
#define BIRDS_ROUND        10
#define SHOTS_BIRD         3
#define FLYAWAY_FRAMES     300
/* 1 = $E661 + edge $48 (v14 k_speed/dir_timer remains in #else). */
#define HUNT_NES_ENGINE    1
#define PERFECT_BONUS      10000
#define BOX_FORGIVE_PAD    10
#define DOG_RETRIEVE_X     128
#define ROUND_ANNOUNCE_FR  96
#define ROUND_BANNER_X     96
#define ROUND_BANNER_Y     72
#define TOP_SCORE_COL      19
#define TOP_SCORE_ROW      25
#define TOP_SCORE_DIGITS   5
#define NES_TITLE_TOP      0

#define FRAME_HIT          0x09
#define FRAME_FALL0        0x0A
#define FRAME_DOG_WALK0    0x0C
#define FRAME_DOG_WALK1    0x10
#define FRAME_DOG_CROUCH   0x11
#define FRAME_DOG_JUMP     0x12
#define FRAME_DOG_STAND    0x13
#define FRAME_DOG_HOLD     0x14
#define FRAME_DOG_LAUGH    0x16

#define DOGPH_WALK1   0
#define DOGPH_SNIFF   1
#define DOGPH_WALK2   2
#define DOGPH_CROUCH  3
#define DOGPH_JUMP    4
#define DOGPH_SNIFF2  5
#define DOGPH_LAUGH   10
#define DOGPH_HOLD    11
#define DOGPH_RISE    12
#define DOGPH_SINK    13

#define DOG_WALK_DT        7
#define DOG_SNIFF_DT       9
#define DOG_SNIFF_LOOPS    2
#define DOG_WALK_X0        24
#define DOG_SNIFF_X        88
#define DOG_JUMP_X         128
#define DOG_CROUCH_FRAMES  8
#define DOG_JUMP_UP_FRAMES 12
#define DOG_JUMP_UP_VY     2

#define DOG_RISE_FRAMES    36
#define DOG_HOLD_FRAMES    16
#define DOG_SINK_FRAMES    40
#define DOG_RISE_VY        2
/* Intro jump OAM min-Y 80 (captures/tally/dog_00648). Retrieve/laugh
 * peaks were not captured (Zapper). v12 CRT: retrieve +3 lower, laugh
 * +3 higher vs v11. Independent origins. */
#define DOG_RETRIEVE_Y0    (GRASS_Y + 37)
#define DOG_LAUGH_Y0       (GRASS_Y + 14)
#define DOG_LAUGH_RISE     40
#define DOG_LAUGH_PEAK     32
#define DOG_LAUGH_SINK     32
#define DOG_LAND_DELAY     16
#define DOG_NEXT_DELAY     40
/* Genuine Game A hit trace, captures/milestone1-nes/native-complete.
 * Durations refer to displayed poses, not the Zapper flash/CPU hit callback. */
#define DUCK_HIT_POSE_FRAMES 23
#define DUCK_FALL_CEL_FRAMES 5
#define DUCK_RETRIEVE_DELAY 67
#define DUCK_NEXT_DELAY     48
int hunt_retrieve_y(int tick, int *y);
#define AB_HOLD_FRAMES     120

#define HUD_AMMO_COL       3
#define HUD_AMMO_ROW       26
#define HUD_HIT_COL        12
#define HUD_HIT_ROW        26
#define HUD_HIT_BOT_ROW    27
#define HUD_SCORE_COL      24
#define HUD_SCORE_ROW      26
#define HUD_ROUND_COL      5
#define HUD_ROUND_ROW      24
#define HUD_ROUND_TENS_COL 4
/*
 * One grouped HUD composition (NES source tiles unchanged).
 * v8 independently shifted HIT/SCORE and filled leftover bands with a solid
 * TILE (72,48,8). Those bands sat left of SHOT, right of HIT, and right of
 * SCORE and vanished once the game HUD was no longer drawn (after tally).
 *
 * v9 covers the HUD strip with NES dirt ($50 at col 31) then places SHOT,
 * HIT, and SCORE with one group origin plus a single HIT–SCORE gap.
 *
 * NES compose: SHOT 16–56, HIT 56–184, SCORE 184–248.
 * v12 dest: SHOT 4–44, HIT 48–176, SCORE 180–244.
 * v13 CRT: whole HUD up 4 NES px; SHOT +2 NES x; SCORE −2 NES x.
 */
#define HUD_SAFE_L         4
#define HUD_SAFE_R         12
#define HUD_OVERSCAN_L     0
#define HUD_SHIFT_UP       (-4)
#define HUD_GAP_SHOT_HIT   2
#define HUD_GAP_HIT_SCORE  2
#define HUD_SHIFT_LEFT     (-4)
#define HUD_GROUP_DX       ((-8) + HUD_SHIFT_LEFT)
#define HUD_SHOT_DX        (-10)
#define HUD_HIT_DX         (-8)
#define HUD_SCORE_DX       (-6)
#define HUD_SHOT_SRC_X     16
#define HUD_SHOT_SRC_W     40
/* NES r26 col 7 = $E5 left wall, col 22 = $E7 right wall. v9 started at
 * col 8 w=112 and cropped both vertical borders. */
#define HUD_HIT_SRC_X      56
#define HUD_HIT_SRC_W      128
#define HUD_SCORE_SRC_X    184
#define HUD_SCORE_SRC_W    64
#define HUD_DIRT_SRC_X     248
#define HUD_DIRT_SRC_W     8
#define HUD_STRIP_Y        192
#define HUD_STRIP_H        40
#define HUD_DIRT_H         (HUD_STRIP_H - HUD_SHIFT_UP)
#define HUD_PANEL_Y        200
#define HUD_PANEL_H        32
#define HUD_PANEL_SHOT     0
#define HUD_PANEL_HIT      1
#define HUD_PANEL_SCORE    2
#define HUD_FLASH_BIT      0x10
/* NES $D2D6: $2B=$10 then compact. After a move, $A9=0 so another 16
 * wait. After no-move, $D309 $AA vs $AB. Pass: $BE 0..$0F × $2B=$10
 * (15×16=240). Fail: $D32B then $C62E (title), no GAME OVER tiles. */
#define HUD_TALLY_SLOT_FR  16
#define HUD_TALLY_HOLD_FR  240
#define HUD_TALLY_FAIL_FR  32

enum {
	LOGIC_HUD_AMMO = 10,
	LOGIC_HUD_BLANK = 11,
	LOGIC_HUD_WDUCK = 12,
	LOGIC_HUD_RDUCK = 14,
	LOGIC_HUD_R = 16,
	LOGIC_HUD_O = 17,
	LOGIC_HUD_U = 18,
	LOGIC_HUD_N = 19,
	LOGIC_HUD_D = 20
};

int hunt_cal_tx(int i);
int hunt_cal_ty(int i);
int hunt_cal_status_y(void);
int hunt_cal_circle_ok(int i);
int hunt_cal_edge_x(void);
int hunt_cal_edge_y(void);
int hunt_hits_needed(int round_n);
int hunt_speed(int round_n, int idx);
unsigned int hunt_color_rng_step(unsigned int seed);
int hunt_duck_type(int round_n, unsigned int color_random);
int hunt_duck_score_tile(int round_n, int type);
int hunt_duck_score_points(int tile);
int hunt_ammo_tile(int shots, int slot);
int hunt_hit_tile(int mark, int is_cur, int flash);
int hunt_hit_top(int mark, int is_cur, int flash_blank);
int hunt_hit_bot(int mark, int slot, int need);
int hunt_flash_blank(int frame);
int hunt_tally_step(int *mark, int n);
int hunt_hud_dx(int panel);
int hunt_hud_dy(void);
int hunt_nes_fly_script(int sx, int sy);
int hunt_nes_pose_script(int pat);
int hunt_nes_facing(int pat);
int hunt_round_ones(int n);
int hunt_round_tens(int n);
int hunt_dog_grass_over(int state_intro, int phase, int timer);
int hunt_walk_frame(int timer);
int hunt_sniff_frame(int timer);
/* Opening frame zero is Game A initialization. */
#define DOG_INTRO_BARK_FRAME 376
#define DOG_INTRO_END_FRAME  423
#define DOG_INTRO_POST_WAIT   46 /* First ST_PLAY remains tick 469 (zero-based). */
typedef struct {
 short x, y;
 unsigned char pose, visible, grass_over, phase;
} HuntIntroSample;
HuntIntroSample hunt_intro_sample(int tick);
int hunt_laugh_frame(int timer);

#endif
