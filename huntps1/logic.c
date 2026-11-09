#include "generated/rom_hit_score_tables.h"
#include "logic.h"
#include "generated/hunt_round_thresholds.h"
int hunt_retrieve_y(int tick, int *y)
{
	if (tick < 0 || tick >= 63)
		return 0;
	/* Rise two pixels per frame, hold, then sink to the grass line. */
	if (tick < 20)
		*y = 173 - 2 * tick;
	else if (tick < 39)
		*y = 133;
	else if (tick < 58)
		*y = 135 + 2 * (tick - 39);
	else
		*y = 173;
	return 1;
}

/* PS1 approximation only. NES uses $48 → $E64B → signed stream $E661. */
static const int k_speed[12][4] = {
	ROM_LEGACY_SPEED_ROWS
};

int hunt_cal_edge_x(void)
{
	return HUNT_CAL_INSET_X;
}

int hunt_cal_edge_y(void)
{
	return HUNT_CAL_INSET_Y;
}

int hunt_cal_tx(int i)
{
	int xL = HUNT_CAL_VIS_X0 + HUNT_CAL_INSET_X_L + HUNT_CAL_R;
	int xR = HUNT_CAL_VIS_X1 - HUNT_CAL_INSET_X_R - HUNT_CAL_R;
	int xC = (HUNT_CAL_VIS_X0 + HUNT_CAL_VIS_X1) / 2;
	int xs[HUNT_CAL_POINTS];

	xs[0] = xL;
	xs[1] = xR;
	xs[2] = xC;
	xs[3] = xL;
	xs[4] = xR;
	if (i < 0 || i >= HUNT_CAL_POINTS)
		return xs[0];
	return xs[i];
}

int hunt_cal_ty(int i)
{
	int yU = HUNT_CAL_VIS_Y0 + HUNT_CAL_INSET_Y + HUNT_CAL_R;
	int yD = HUNT_CAL_VIS_Y1 - HUNT_CAL_INSET_Y - HUNT_CAL_R;
	int yC = (HUNT_CAL_VIS_Y0 + HUNT_CAL_VIS_Y1) / 2;
	int ys[HUNT_CAL_POINTS];

	ys[0] = yU;
	ys[1] = yU;
	ys[2] = yC;
	ys[3] = yD;
	ys[4] = yD;
	if (i < 0 || i >= HUNT_CAL_POINTS)
		return ys[0];
	return ys[i];
}

int hunt_cal_status_y(void)
{
	return hunt_cal_ty(0) + HUNT_CAL_R + 8;
}

int hunt_cal_circle_ok(int i)
{
	int x = hunt_cal_tx(i);
	int y = hunt_cal_ty(i);
	int r = HUNT_CAL_R;

	return x - r >= HUNT_CAL_VIS_X0 && x + r <= HUNT_CAL_VIS_X1 &&
		y - r >= HUNT_CAL_VIS_Y0 && y + r <= HUNT_CAL_VIS_Y1;
}

int hunt_hits_needed(int round_n)
{
	int i = round_n - 1;

	if (i < 0)
		i = 0;
	if (i >= 20)
		return 10;
	return hunt_round_thresholds[i];
}

int hunt_speed(int round_n, int idx)
{
	int g = round_n - 1;

	if (g < 0)
		g = 0;
	if (g > 11)
		g = 11;
	return k_speed[g][idx & 3];
}

unsigned int hunt_color_rng_step(unsigned int seed)
{
	/* $C588 rotates $05EC..$05EF in order. In this byte ordering the
	 * carry entering each byte is the old low bit of the preceding byte. */
	unsigned int feedback = ((seed >> 1) ^ (seed >> 9)) & 1u;
	return ((seed >> 1) & 0x7f7f7f7fu) |
		((seed & 0x00010101u) << 15) | (feedback << 7);
}

int hunt_duck_type(int round_n, unsigned int color_random)
{
	/* Game A takes the high nibble at $C992. The pair table's low
	 * nibble is for the second bird, absent in Game A. */
	static const unsigned char first_duck[8] = ROM_FIRST_DUCK_COLORS;
	return first_duck[color_random & (round_n <= 1 ? 3u : 7u)];
}

int hunt_duck_score_tile(int round_n, int type)
{
	/* $EBF1, indexed by the three BCD round bands in $CF68. */
	static const unsigned char score_tiles[3][3] = ROM_SCORE_TILES;
	int band = round_n >= 11 ? 2 : round_n >= 6 ? 1 : 0;
	return score_tiles[band][type];
}

int hunt_duck_score_points(int tile)
{
	/* $EBFA, represented as points instead of packed BCD hundreds. */
	static const int points[8] = { 500, 800, 1000, 1500, 1600, 2000, 2400, 3000 };
	return points[tile];
}

int hunt_ammo_tile(int shots, int slot)
{
	if (slot < 0 || slot > 2)
		return LOGIC_HUD_BLANK;
	return (slot < shots) ? LOGIC_HUD_AMMO : LOGIC_HUD_BLANK;
}

int hunt_hit_tile(int mark, int is_cur, int flash)
{
	return hunt_hit_top(mark, is_cur, flash);
}

int hunt_flash_blank(int frame)
{
	/* $9B bit 4: 16 frames $B7, 16 frames $D6 while a duck is flying. */
	return (frame & HUD_FLASH_BIT) == 0;
}

int hunt_hud_dx(int panel)
{
	if (panel == HUD_PANEL_HIT)
		return HUD_HIT_DX;
	if (panel == HUD_PANEL_SCORE)
		return HUD_SCORE_DX;
	return HUD_SHOT_DX;
}

int hunt_hud_dy(void)
{
	return HUD_SHIFT_UP;
}

int hunt_nes_fly_script(int sx, int sy)
{
	(void)sx;
	(void)sy;
	return 1;
}

int hunt_nes_pose_script(int pat)
{
	/* $CBAC: $48 AND #$07. Pose is the pattern nibble, not sx/sy. */
	int v = pat & 7;

	if (v == 0)
		return 1;
	if (v >= 3 && v <= 5)
		return 2;
	return 0;
}

int hunt_nes_facing(int pat)
{
	/* $CB87 $48>=$11 or $CBA2 $48>=$08 sets $34 (H-flip). */
	return pat >= 0x08;
}

int hunt_tally_step(int *mark, int n)
{
	int x, moved = 0;

	if (!mark || n < 2)
		return 0;
	/* $D2DD: X=1..9, if $03F0,X and $03EF,X==0 then INC $03EF / DEC $03F0.
	 * One full left-shift of every loose hit, then $2B=$10. */
	for (x = 1; x < n; x++) {
		if (mark[x] == 2 && mark[x - 1] != 2) {
			mark[x - 1] = 2;
			mark[x] = 0;
			moved = 1;
		}
	}
	return moved;
}

int hunt_hit_top(int mark, int is_cur, int flash_blank)
{
	if (is_cur && mark == 0 && flash_blank)
		return LOGIC_HUD_BLANK;
	if (mark == 2)
		return LOGIC_HUD_RDUCK;
	return LOGIC_HUD_WDUCK;
}

int hunt_hit_bot(int mark, int slot, int need)
{
	/* Game A hit only DEYs the top ($D6→$D5). Bottoms stay $D7/$B7 by need. */
	(void)mark;
	if (slot < need)
		return LOGIC_HUD_WDUCK + 1;
	return LOGIC_HUD_BLANK;
}

int hunt_round_ones(int n)
{
	if (n < 1)
		n = 1;
	if (n > 99)
		n = 99;
	return n % 10;
}

int hunt_round_tens(int n)
{
	if (n < 10)
		return -1;
	if (n > 99)
		n = 99;
	return n / 10;
}

int hunt_dog_grass_over(int state_intro, int phase, int timer)
{
	if (state_intro) {
		if (phase == DOGPH_JUMP && timer > DOG_JUMP_UP_FRAMES)
			return 1;
		if (phase > DOGPH_JUMP && phase != DOGPH_SNIFF2)
			return 1;
		return 0;
	}
	return phase == DOGPH_LAUGH || phase == DOGPH_HOLD ||
		phase == DOGPH_RISE || phase == DOGPH_SINK;
}

int hunt_walk_frame(int timer)
{
	/* Four CHR cels at $DE34. Live OAM steps them in order.
	 * Baked $0C/$10 are the same first cel (61 pixels apart). */
	return (timer / DOG_WALK_DT) & 3;
}

int hunt_sniff_frame(int timer)
{
	static const int sniff[2] = { 0x0C, 0x10 };

	return sniff[(timer / DOG_SNIFF_DT) & 1];
}

HuntIntroSample hunt_intro_sample(int tick)
{
	/* Measured vertical anchors for the two jump cels. */
	static const unsigned char jump_y[] = {
		156,153,150,147,144,142,140,138,136,135,
		133,132,131,130,129,128,127,123,123,122,
		122,121,121,121,121,120,120,120,120,120,
		121,121,122,122,123,124,125,127,129,131,
		134,137,141,145,150,155,160,165,170,175
	};
	if (tick < 3 || tick >= DOG_INTRO_END_FRAME)
		return (HuntIntroSample){0};
	if (tick < 118 || (tick >= 175 && tick < 290)) {
		int second = tick >= 175;
		int walk = tick - (second ? 175 : 3);
		return (HuntIntroSample){
			(second ? 60 : 26) + 2 * (walk / DOG_WALK_DT), 172,
			0x0c + hunt_walk_frame(walk), 1, 0, second ? DOGPH_WALK2 : DOGPH_WALK1
		};
	}
	if (tick < 347) {
		int second = tick >= 290;
		int sniff = tick - (second ? 292 : 120);
		int phase = second ? DOGPH_SNIFF2 : DOGPH_SNIFF;
		if (sniff >= 53) {
			phase = DOGPH_WALK2;
			sniff = 53;
		}
		return (HuntIntroSample){second ? 92 : 58, 172,
			hunt_sniff_frame(sniff < 0 ? 0 : sniff), 1, 0, phase};
	}
	if (tick < 354)
		return (HuntIntroSample){94, 172, 0x0c, 1, 0, DOGPH_WALK2};
	if (tick < 373)
		return (HuntIntroSample){96, 172, tick == 354 ? 0x0d : 0x11, 1, 0, DOGPH_CROUCH};

	int x = 99 + tick - 373;
	return (HuntIntroSample){x > 140 ? 140 : x, jump_y[tick - 373],
		tick < 390 ? 0x12 : 0x13, 1, tick >= 399, DOGPH_JUMP};
}

int hunt_laugh_frame(int timer)
{
	return FRAME_DOG_LAUGH + ((timer / 4) & 1);
}
