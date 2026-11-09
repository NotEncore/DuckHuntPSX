/*
 * HuntPS1 v15: NES $E661 flight behind HUNT_NES_ENGINE. GunCon/VBlank/cal/HUD frozen.
 * Pause still has no FntSort.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <psxpad.h>

#include "gfx.h"
#include "audio.h"
#include "cal_save.h"
#ifdef HUNTPS1_GAME_C
#include "clay_render.h"
static HuntClayRound g_clay;
static HuntClayFlight clay_flash_target[2];
static int clay_flash_index, clay_flash_count, clay_scan[4];
static volatile struct {
	int edges, accepted, hits, no_light, blocked, last_result;
	int raw_x, raw_y, aim_x, aim_y, target_x, target_y, target_size, target_live;
} g_clay_shot_diag;
static int clay_launch_audio[2];
#define ST_CLAY 15
#endif
#ifdef HUNTPS1_DIAG_SKIPCAL
#include "generated/hunt_sfx.h"
#include "generated/hunt_sfx_shot.h"
#include "generated/hunt_sfx_shot_game.h"
#include "generated/hunt_sfx_shot_full.h"
#include "spu_noise_shot.h"
#endif
#include "nes_fly.h"
#include "../guncal/guncal.h"

static HuntNesFly g_nf;
#ifdef HUNTPS1_GAME_B
#include "game_b.h"
typedef struct { int x, y, live; } PairTarget;
static HuntPair g_pair;
#endif
/* Public diagnostics for firmware-1.1 readback; calibration payload stays local. */
int g_cal_card_status;
int g_cal_loaded_at_boot;
int g_cal_card_busy;

#define OT_LENGTH   8
#define BUFFER_LEN  98304
#define FEEDBACK    16
#define WAIT_FRAMES 80

#define ST_TITLE    0
#define ST_WELCOME  1
#define ST_CAL      2
#define ST_WAIT     3
#define ST_DONE     4
#define ST_NEEDCAL  5
#define ST_INTRO    6
#define ST_PLAY     7
#define ST_ROUND    8
#define ST_OVER     9
#define ST_NYI      10
#define ST_PAUSE    11
#define ST_TALLY    12
#ifdef HUNTPS1_DIAG_SKIPCAL
#define ST_SOUND    13
#endif
#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
#define ST_OPTIONS  14
static volatile unsigned int g_zapper_flash;
#endif
#ifdef HUNTPS1_DELUXE_MENU
#include "deluxe_ducks.h"
#ifdef HUNTPS1_WOODS_INTEGRATED
int woods3d_run_deluxe(uint8_t input[2][34], const GunCal *saved_calibration,
 int selected_players, int primary_port);
#else
#include "deluxe_woods.h"
#endif
#define ST_DELUXE 16
#define ST_DELUXE_PLAYERS 17
#define ST_DELUXE_PLAY 18
#define ST_DELUXE_RESULT 19
static DeluxeMatch g_deluxe_match;
static DeluxeTarget deluxe_submitted[2][DELUXE_MAX_DUCKS];
static DeluxeTarget deluxe_crt[DELUXE_MAX_DUCKS], deluxe_latched[DELUXE_MAX_DUCKS];
static volatile unsigned deluxe_perf[4]; /* frames, late frames, max VBlank gap, peak flying */
#ifndef HUNTPS1_WOODS_INTEGRATED
static unsigned woods_submitted[2], woods_crt, woods_latched;
#endif
#endif

/* Native Game A pose runs: 569..571, 572..576, 577..579, etc.
 * ROM duration bytes count down inclusively: three/three/five frames. */
static const int k_fly_dur[3] = { 3, 3, 5 };
static const int k_fly_a[3] = { 0x00, 0x01, 0x02 };
static const int k_fly_b[3] = { 0x03, 0x04, 0x05 };
static const int k_fly_c[3] = { 0x06, 0x07, 0x08 };
static int fly_frame(int script, int step);

typedef struct {
	DISPENV  disp_env;
	DRAWENV  draw_env;
	uint32_t ot[OT_LENGTH];
	uint8_t  buffer[BUFFER_LEN];
	int      latch_x, latch_y, latch_hit;
#ifdef HUNTPS1_GAME_C
	HuntClayFlight clay_target[2];
#endif
#ifdef HUNTPS1_GAME_B
	PairTarget pair_target[2];
#endif
#ifdef HUNTPS1_EXPERIMENTAL_ZAPPER_FLASH
	int      flash_frame;
#endif
} RenderBuffer;

typedef struct {
	RenderBuffer buffers[2];
	uint8_t      *next_packet;
	int          active_buffer;
	int          crt_x, crt_y, crt_hit;
	int          latched_x, latched_y, latched_hit;
#ifdef HUNTPS1_GAME_C
	HuntClayFlight crt_clay[2], latched_clay[2];
#endif
#ifdef HUNTPS1_GAME_B
	PairTarget crt_pair[2], latched_pair[2];
#endif
#ifdef HUNTPS1_EXPERIMENTAL_ZAPPER_FLASH
	int          crt_flash, latched_flash;
#endif
} RenderContext;

#ifdef HUNTPS1_EXPERIMENTAL_ZAPPER_FLASH
static volatile unsigned int g_flash_suppressed;
#endif
static uint8_t pad_buff[2][34];
static GunCal g_cal;
static int g_box_pad = BOX_FORGIVE_PAD;
static unsigned int rng = 2463534242u;
/* Separate stream: the color draw must not perturb existing flight RNG. */
static unsigned int color_rng = 0x3ca55a12u;

typedef struct {
	int valid;
	int raw_x, raw_y;
	int orig_x, orig_y;
	int cal_x, cal_y;
} GunSample;

static int box_hw(void)
{
	return gfx_nw(HUNT_DUCK_HW + g_box_pad);
}

static int box_hh(void)
{
	return HUNT_DUCK_HH + g_box_pad;
}

static int cal_point_full(const GunCal *c, int point, int need)
{
	if (point < 0 || point >= GUNCAL_POINTS)
		return 1;
	return c->pt[point].n >= need;
}

static void gun_fill_sample(GunSample *s, int have_gun, const PADTYPE *gun)
{
	s->valid = 0;
	s->raw_x = s->raw_y = 0;
	s->orig_x = s->orig_y = 0;
	s->cal_x = s->cal_y = 0;
	if (!have_gun)
		return;
	s->raw_x = gun->gun_x;
	s->raw_y = gun->gun_y;
	if (!guncon_has_light((uint16_t)s->raw_x, (uint16_t)s->raw_y))
		return;
	s->valid = 1;
	map_gun_to_screen((uint16_t)s->raw_x, (uint16_t)s->raw_y, &s->orig_x, &s->orig_y);
	if (g_cal.has_cal)
		guncal_apply(&g_cal, s->orig_x, s->orig_y, &s->cal_x, &s->cal_y);
	else {
		s->cal_x = s->orig_x;
		s->cal_y = s->orig_y;
	}
}

static unsigned int rnd(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}

static uint8_t g_tile_discard[32];

static void *new_primitive(RenderContext *ctx, int z, size_t size)
{
	RenderBuffer *buffer = &ctx->buffers[ctx->active_buffer];
	uint8_t      *prim   = ctx->next_packet;
	uint8_t      *end    = buffer->buffer + BUFFER_LEN;

	if (prim + size > end)
		return g_tile_discard;
	addPrim(&buffer->ot[z], prim);
	ctx->next_packet += size;
	return prim;
}

static void setup_context(RenderContext *ctx)
{
	SetDefDrawEnv(&ctx->buffers[0].draw_env, 0, 0, SCREEN_XRES, SCREEN_YRES);
	SetDefDispEnv(&ctx->buffers[0].disp_env, 0, 0, SCREEN_XRES, SCREEN_YRES);
	SetDefDrawEnv(&ctx->buffers[1].draw_env, 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
	SetDefDispEnv(&ctx->buffers[1].disp_env, 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
	setRGB0(&ctx->buffers[0].draw_env, 0, 0, 0);
	setRGB0(&ctx->buffers[1].draw_env, 0, 0, 0);
	ctx->buffers[0].draw_env.isbg = 1;
	ctx->buffers[1].draw_env.isbg = 1;
	ctx->active_buffer = 0;
	ctx->next_packet = ctx->buffers[0].buffer;
	gfx_set_prim_limit(ctx->buffers[0].buffer + BUFFER_LEN);
	ctx->crt_x = ctx->crt_y = ctx->crt_hit = 0;
	ctx->latched_x = ctx->latched_y = ctx->latched_hit = 0;
#ifdef HUNTPS1_GAME_C
	memset(ctx->crt_clay, 0, sizeof(ctx->crt_clay));
	memset(ctx->latched_clay, 0, sizeof(ctx->latched_clay));
	memset(ctx->buffers[0].clay_target, 0, sizeof(ctx->buffers[0].clay_target));
	memset(ctx->buffers[1].clay_target, 0, sizeof(ctx->buffers[1].clay_target));
#endif
#ifdef HUNTPS1_EXPERIMENTAL_ZAPPER_FLASH
	ctx->buffers[0].flash_frame = ctx->buffers[1].flash_frame = 0;
	ctx->crt_flash = ctx->latched_flash = 0;
#endif
	ClearOTagR(ctx->buffers[0].ot, OT_LENGTH);
	SetDispMask(1);
}

static void flip_buffers(RenderContext *ctx)
{
	DrawSync(0);
	VSync(0);

	ctx->latched_x = ctx->crt_x;
	ctx->latched_y = ctx->crt_y;
	ctx->latched_hit = ctx->crt_hit;
#ifdef HUNTPS1_DELUXE_MENU
	memcpy(deluxe_latched, deluxe_crt, sizeof(deluxe_latched));
	memcpy(deluxe_crt, deluxe_submitted[ctx->active_buffer ^ 1], sizeof(deluxe_crt));
#ifndef HUNTPS1_WOODS_INTEGRATED
	woods_latched = woods_crt;
	woods_crt = woods_submitted[ctx->active_buffer ^ 1];
#endif
#endif
#ifdef HUNTPS1_GAME_C
	memcpy(ctx->latched_clay, ctx->crt_clay, sizeof(ctx->latched_clay));
#endif
#ifdef HUNTPS1_GAME_B
	memcpy(ctx->latched_pair, ctx->crt_pair, sizeof(ctx->latched_pair));
#endif
#ifdef HUNTPS1_EXPERIMENTAL_ZAPPER_FLASH
	ctx->latched_flash = ctx->crt_flash;
#endif

	{
		RenderBuffer *draw_buffer = &ctx->buffers[ctx->active_buffer];
		RenderBuffer *disp_buffer = &ctx->buffers[ctx->active_buffer ^ 1];

		PutDispEnv(&disp_buffer->disp_env);
		ctx->crt_x = disp_buffer->latch_x;
		ctx->crt_y = disp_buffer->latch_y;
		ctx->crt_hit = disp_buffer->latch_hit;
#ifdef HUNTPS1_GAME_C
		memcpy(ctx->crt_clay, disp_buffer->clay_target, sizeof(ctx->crt_clay));
#endif
#ifdef HUNTPS1_GAME_B
		memcpy(ctx->crt_pair, disp_buffer->pair_target, sizeof(ctx->crt_pair));
#endif
#ifdef HUNTPS1_EXPERIMENTAL_ZAPPER_FLASH
		ctx->crt_flash = disp_buffer->flash_frame;
#endif
		DrawOTagEnv(&draw_buffer->ot[OT_LENGTH - 1], &draw_buffer->draw_env);
		ctx->active_buffer ^= 1;
		ctx->next_packet = disp_buffer->buffer;
		gfx_set_prim_limit(disp_buffer->buffer + BUFFER_LEN);
		ClearOTagR(disp_buffer->ot, OT_LENGTH);
	}
}

static void draw_tile(RenderContext *ctx, int x, int y, int w, int h,
	int r, int g, int b, int z)
{
	TILE *tile;

	if (w <= 0 || h <= 0)
		return;
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > SCREEN_XRES)
		w = SCREEN_XRES - x;
	if (y + h > SCREEN_YRES)
		h = SCREEN_YRES - y;
	if (w <= 0 || h <= 0)
		return;
	tile = new_primitive(ctx, z, sizeof(TILE));
	setTile(tile);
	setXY0(tile, x, y);
	setWH(tile, w, h);
	setRGB0(tile, r, g, b);
}

static void draw_text(RenderContext *ctx, int x, int y, const char *text)
{
	RenderBuffer *buffer = &ctx->buffers[ctx->active_buffer];

	if (y < 32)
		y = 32;
	ctx->next_packet = (uint8_t *)
		FntSort(&buffer->ot[0], ctx->next_packet, x, y, text);
}

static void draw_text_c(RenderContext *ctx, int y, const char *text)
{
	int x = (SCREEN_XRES - (int)strlen(text) * 8) / 2;

	if (x < 16)
		x = 16;
	draw_text(ctx, x, y, text);
}

static void draw_filled_circle(RenderContext *ctx, int cx, int cy, int radius,
	int r, int g, int b, int z)
{
	int dy;

	for (dy = -radius; dy <= radius; dy++) {
		int dx = 0;
		int limit = radius * radius - dy * dy;

		if (limit < 0)
			continue;
		while (dx * dx <= limit)
			dx++;
		dx--;
		draw_tile(ctx, cx - dx, cy + dy, dx * 2 + 1, 1, r, g, b, z);
	}
}

static void draw_cal_label(RenderContext *ctx, uint32_t *ot, uint8_t **packet, int y, const char *label)
{
#if defined(HUNTPS1_DELUXE_MENU) && defined(HUNTPS1_EXPERIMENTAL_OPTIONS)
	(void)ctx;
	gfx_draw_deluxe_result_text(ot, packet, label, y, 0);
#else
	(void)ot;
	(void)packet;
	draw_text_c(ctx, y, label);
#endif
}

static void draw_cal_panel(RenderContext *ctx)
{
	draw_tile(ctx, 42, 36, 236, 124, 0, 0, 0, 2);
	draw_tile(ctx, 40, 34, 240, 128, 88, 117, 23, 2);
}

static void draw_cal_target(RenderContext *ctx, int cx, int cy)
{
	/* Same radius math for every point. Add small discs first so the
	 * outer TILE is OT-head (behind) and the rings stay visible. */
	draw_filled_circle(ctx, cx, cy, HUNT_CAL_R - 10, 255, 160, 32, 2);
	draw_filled_circle(ctx, cx, cy, HUNT_CAL_R - 5, 0, 0, 0, 2);
	draw_filled_circle(ctx, cx, cy, HUNT_CAL_R, 80, 176, 255, 2);
}

static int hits_bird(int ax, int ay, int bx, int by)
{
	int hw = box_hw();
	int hh = box_hh();

	return ax >= (bx - hw) && ax < (bx + hw) &&
		ay >= (by - hh) && ay < (by + hh);
}

static void begin_cal(int *pt, int *tx, int *ty)
{
	guncal_reset(&g_cal);
	*pt = 0;
	*tx = hunt_cal_tx(0);
	*ty = hunt_cal_ty(0);
}

static void pick_heading(int *sx, int *sy, int *facing, int *script)
{
	int dir = (int)(rnd() % 8);

	switch (dir) {
	case 0:
		*sx = 1; *sy = 0; break;
	case 1:
		*sx = 1; *sy = -1; break;
	case 2:
		*sx = 1; *sy = -1; break;
	case 3:
		*sx = -1; *sy = -1; break;
	case 4:
		*sx = -1; *sy = 0; break;
	case 5:
		*sx = -1; *sy = 1; break;
	case 6:
		*sx = 1; *sy = 1; break;
	default:
		*sx = -1; *sy = -1; break;
	}
	*facing = (*sx < 0);
	(void)script;
}

static void spawn_bird(int round_n, int *bx, int *by, int *sx, int *sy,
	int *facing, int *falling, int *shots, int *frame, int *anim_div,
	int *anim_step, int *script, int *dir_timer, int *hitstun,
	int *fly_timer, int *escaping, int *spd_i, int *dtype, int *nes_pat)
{
	hunt_sfx_begin_flight();
	int side;

	*shots = SHOTS_BIRD;
	*falling = 0;
	*hitstun = 0;
	*escaping = 0;
	*anim_div = 0;
	*anim_step = 0;
	*spd_i = 0;
	*fly_timer = FLYAWAY_FRAMES;
	color_rng = hunt_color_rng_step(color_rng);
	*dtype = hunt_duck_type(round_n, color_rng);
	*dir_timer = 70 + (int)(rnd() % 80);
#if HUNT_NES_ENGINE
	/* Live Game A grass X 32..208. Never pick BACK ($48=1) at spawn. */
	side = 32 + (int)(rnd() % 177);
	hunt_nes_fly_spawn(&g_nf, round_n, side);
	*bx = g_nf.x;
	*by = g_nf.y;
	*nes_pat = g_nf.pat;
	*script = g_nf.pose;
	*facing = g_nf.flip;
	*sx = 1;
	*sy = -1;
#else
	pick_heading(sx, sy, facing, script);
	*by = 52 + (int)(rnd() % 60);
	side = (int)(rnd() % 3);
	if (side == 0) {
		*bx = 12;
		if (*sx < 0)
			*sx = 1;
		*nes_pat = 0x00;
	} else if (side == 1) {
		*bx = NES_SRC_W - 12;
		if (*sx > 0)
			*sx = -1;
		*nes_pat = 0x08;
	} else {
		*bx = 48 + (int)(rnd() % 140);
		*by = GRASS_Y - 30;
		*nes_pat = 0x00;
		if (*sy > 0)
			*sy = -1;
	}
	*script = hunt_nes_pose_script(*nes_pat);
	*facing = hunt_nes_facing(*nes_pat);
#endif
	*frame = fly_frame(*script, 0);
}

static int fly_frame(int script, int step)
{
	if (script == 1)
		return k_fly_b[step];
	if (script == 2)
		return k_fly_c[step];
	return k_fly_a[step];
}

static void fill_hunt_targets(int *tx, int *ty)
{
	int i;

	for (i = 0; i < GUNCAL_POINTS; i++) {
		tx[i] = hunt_cal_tx(i);
		ty[i] = hunt_cal_ty(i);
	}
}

static void draw_game_hud(RenderContext *ctx, uint32_t *ot, uint8_t **packet,
	int shots, int score, int round_n, const int *mark, int cur, int flash)
{
	int i, n, x, tens;
	int digits[6];
	int s = score;

	(void)ctx;

	if (s < 0)
		s = 0;
	if (s > 999999)
		s = 999999;
	for (i = 5; i >= 0; i--) {
		digits[i] = s % 10;
		s /= 10;
	}

	for (i = 0; i < 3; i++) {
		x = HUD_AMMO_COL * 8 + i * 8;
		gfx_draw_game_hud_tile(ot, packet, 0, hunt_ammo_tile(shots, i), x,
			HUD_AMMO_ROW * 8);
	}

	n = hunt_hits_needed(round_n);
	for (i = 0; i < 10; i++) {
		x = HUD_HIT_COL * 8 + i * 8;
		gfx_draw_game_hud_tile(ot, packet, 0,
			hunt_hit_top(mark[i], i == cur, flash), x, HUD_HIT_ROW * 8);
		gfx_draw_game_hud_tile(ot, packet, 0,
			hunt_hit_bot(mark[i], i, n), x, HUD_HIT_BOT_ROW * 8);
	}

	for (i = 0; i < 6; i++) {
		x = HUD_SCORE_COL * 8 + i * 8;
		gfx_draw_game_hud_tile(ot, packet, 0, digits[i], x, HUD_SCORE_ROW * 8);
	}

	n = round_n;
	if (n < 1)
		n = 1;
	tens = hunt_round_tens(n);
	if (tens >= 0)
		gfx_draw_game_hud_tile(ot, packet, 0, tens,
			HUD_ROUND_TENS_COL * 8,
			HUD_ROUND_ROW * 8);
	gfx_draw_game_hud_tile(ot, packet, 0, hunt_round_ones(n),
		HUD_ROUND_COL * 8,
		HUD_ROUND_ROW * 8);

}

#ifdef HUNTPS1_DELUXE_MENU
static void draw_deluxe_hits(RenderContext *ctx, uint32_t *ot, uint8_t **packet,
	const DeluxeMatch *match)
{
	static const unsigned infinity_rows[] = {
		0x18c, 0x252, 0x222, 0x252, 0x18c
	};
	int ammo_x = HUD_AMMO_COL * 8;
	int ammo_y = HUD_AMMO_ROW * 8;
	for (int y = 0; y < 5; y++) {
		for (int x = 0; x < 11; x++) {
			if (infinity_rows[y] & (1u << x)) {
				int px = ammo_x + 6 + x;
				draw_tile(ctx, gfx_nx(px), ammo_y + 1 + y,
					gfx_nx(px + 1) - gfx_nx(px), 1, 255, 255, 255, 0);
			}
		}
	}
	draw_tile(ctx, gfx_nx(ammo_x), ammo_y,
		gfx_nx(ammo_x + 24) - gfx_nx(ammo_x), 8, 0, 0, 0, 0);

	for (int player = 0; player < match->players; player++) {
		int offset = match->players == 2 ? (player ? 2 : -2) : 0;
		int y = (HUD_HIT_ROW + player) * 8 + offset;
		int score = match->player[player].score;
		int shootout = match->mode == DELUXE_SHOOTOUT;
		for (int i = 5; i >= 0; i--) {
			gfx_draw_game_hud_tile(ot, packet, 0, score % 10,
				(HUD_SCORE_COL + i) * 8, (HUD_SCORE_ROW + player) * 8 + offset);
			score /= 10;
		}
		for (int i = 0; i < DELUXE_DUEL_DUCKS; i++) {
			int x = (HUD_HIT_COL + i) * 8;
			int mark = i < match->player[player].hits ? 2 : 0;
			gfx_draw_game_hud_tile(ot, packet, 0, hunt_hit_top(mark, 0, 0), x, y);
			if (match->players == 1)
				gfx_draw_game_hud_tile(ot, packet, 0, hunt_hit_bot(mark, i, DELUXE_DUEL_DUCKS),
					x, HUD_HIT_BOT_ROW * 8);
		}
		if (match->players == 2 || shootout) {
			int label_x = (HUD_HIT_COL - 4) * 8;
			if (shootout)
				gfx_draw_shootout_player(ot, packet, player, label_x, y);
			else
				gfx_draw_hud_text(ot, packet, 0, player ? "P2" : "P1", label_x, y);
			if (match->players == 1) {
				for (int i = 0; i < 4; i++)
					gfx_draw_game_hud_tile(ot, packet, 0, LOGIC_HUD_BLANK, label_x + i * 8, y);
			}
		}
	}
	if (match->players == 2) {
		/* Clear the stock single-player legends behind the split rows. */
		int hit_x = gfx_nx((HUD_HIT_COL - 4) * 8);
		int score_x = gfx_nx(HUD_SCORE_COL * 8);
		draw_tile(ctx, hit_x, HUD_HIT_ROW * 8 - 2,
			gfx_nx((HUD_HIT_COL + DELUXE_DUEL_DUCKS) * 8) - hit_x, 20, 0, 0, 0, 0);
		draw_tile(ctx, score_x, HUD_SCORE_ROW * 8 - 2,
			gfx_nx((HUD_SCORE_COL + 6) * 8) - score_x, 20, 0, 0, 0, 0);
		if (match->mode == DELUXE_DUEL) {
			/* Same-depth tiles draw back to front. Frame the split rows. */
			draw_tile(ctx, hit_x - 2, HUD_HIT_ROW * 8 - 4,
				gfx_nx((HUD_HIT_COL + DELUXE_DUEL_DUCKS) * 8) - hit_x + 4,
				24, 88, 117, 23, 0);
			draw_tile(ctx, score_x - 2, HUD_SCORE_ROW * 8 - 4,
				gfx_nx((HUD_SCORE_COL + 6) * 8) - score_x + 4,
				24, 88, 117, 23, 0);
		}
	}
}

#endif

static void draw_score_digits(uint32_t *ot, uint8_t **packet, int score,
	int nes_x, int nes_y, int width)
{
	int i, s = score;
	int digits[6];

	if (width < 1)
		width = 1;
	if (width > 6)
		width = 6;
	if (s < 0)
		s = 0;
	if (s > 999999)
		s = 999999;
	for (i = 5; i >= 0; i--) {
		digits[i] = s % 10;
		s /= 10;
	}
	for (i = 6 - width; i < 6; i++)
		gfx_draw_title_digit(ot, packet, 0, digits[i],
			nes_x + (i - (6 - width)) * 8, nes_y);
}

static void draw_round_banner(uint32_t *ot, uint8_t **packet, int round_n)
{
	int tens = hunt_round_tens(round_n);

	/* Number overlays are submitted first (same-depth OT is LIFO). */
	if (tens >= 0)
		gfx_draw_game_hud_tile(ot, packet, 0, tens, 124, 72);
	gfx_draw_game_hud_tile(ot, packet, 0, hunt_round_ones(round_n),
		tens >= 0 ? 132 : 128, 72);
	gfx_draw_game_banner(ot, packet, 0, 0);
}

static void draw_pause_menu(RenderContext *ctx, uint32_t *ot, uint8_t **packet,
	int sel)
{
	int x = 88;
	int y0 = 84;
	int i;

	/* PS1-only menu, NES HUD tiles + title cursor. No FntSort: BIOS
	 * font after a full 8x8 field OT overflowed BUFFER_LEN on hardware. */
	for (i = 0; i < 3; i++) {
		if (sel == i)
			gfx_draw_cursor(ot, packet, 0, x - 16, y0 + 16 + i * 16);
	}
	gfx_draw_hud_text(ot, packet, 0, "PAUSE", 112, y0);
	gfx_draw_hud_text(ot, packet, 0, "RESUME", x, y0 + 16);
	gfx_draw_hud_text(ot, packet, 0, "RESTART", x, y0 + 32);
	gfx_draw_hud_text(ot, packet, 0, "MAIN MENU", x, y0 + 48);
	draw_tile(ctx, 72, 76, 176, 80, 0, 0, 0, 0);
}

static void start_intro(int *dog_x, int *dog_y, int *dog_frame, int *dog_phase,
	int *dog_timer, int *dog_show, int *dog_face)
{
	*dog_x = DOG_WALK_X0;
	*dog_y = 172;
	*dog_frame = FRAME_DOG_WALK0;
	*dog_phase = DOGPH_WALK1;
	*dog_timer = 0;
	*dog_show = 0;
	*dog_face = 0;
}

/* $CC55-$CC64 clamps the dog's world origin, not the atlas top-left.
 * Genuine hit trace: duck X118 -> dog origin118, OAM left102. */
static int retrieve_dog_x(int duck_x)
{
	const int left = 72;
	const int right = 160;
	int x = duck_x;
	if (x < left) x = left;
	if (x > right) x = right;
	return x;
}

static void start_retrieve(int *dog_x, int *dog_y, int *dog_frame, int *dog_phase,
	int *dog_timer, int *dog_show, int laugh, int duck_x)
{
	*dog_show = 1;
	*dog_timer = 0;
	*dog_x = laugh ? DOG_RETRIEVE_X : retrieve_dog_x(duck_x);
	*dog_y = laugh ? DOG_LAUGH_Y0 : DOG_RETRIEVE_Y0;
	if (laugh) {
		*dog_phase = DOGPH_LAUGH;
		*dog_frame = FRAME_DOG_LAUGH;
	} else {
		*dog_phase = DOGPH_RISE;
		*dog_frame = FRAME_DOG_HOLD;
		hunt_retrieve_y(0, dog_y);
	}
}

int main(void)
{
	static RenderContext ctx;
	int state = ST_TITLE;
	int pt = 0;
	int tx, ty;
	int wait = 0, fb = 0, fb_ok = 0;
	int prev_trig = 0, prev_a = 0, prev_b = 0, prev_sel = 0;
	unsigned int steer_clock = 0;
	int prev_up = 0, prev_dn = 0;
	int prev_start = 0;
	int bx = 160, by = 80, sx = 1, sy = 0, facing = 0, falling = 0;
	int shots = 3, frame = 3, anim_div = 0, anim_step = 0, script = 1, nes_pat = 0;
#ifdef HUNTPS1_DIAG_SKIPCAL
	int diag_arm = 0;
#endif
	int dir_timer = 80, fall_tick = 0, fall_origin_x = 0, hitstun = 0;
	int fly_timer = FLYAWAY_FRAMES, escaping = 0, spd_i = 0, dtype = 0;
	int score = 0, respawn = 0, round_n = 1, birds_done = 0, hits_round = 0;
	int popup_left = 0, popup_tile = 0, popup_x = 0, popup_y = 0;
#ifdef HUNTPS1_GAME_B
	int game_b = 0, pair_started = 0, shot_flash_second = 0;
	int shot_flash_x2 = 0, shot_flash_y2 = 0;
#endif
#ifdef HUNTPS1_EXPERIMENTAL_ZAPPER_FLASH
	int shot_flash_frame = 0, shot_flash_length = 0;
	int shot_flash_x = 0, shot_flash_y = 0;
#endif
	int won = 0, menu_sel = 0, nyi_which = 0;
#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
	int options_sel = 0, cal_from_options = 0;
#ifdef HUNTPS1_DELUXE_MENU
	int deluxe_sel = 0, options_return = ST_TITLE;
	int player_sel = 0, deluxe_ports[2] = {0, 1};
	int deluxe_held[2] = {1, 1}, deluxe_missing = 0;
	int deluxe_population = 8, deluxe_prev_horizontal = 0;
	unsigned deluxe_last_vblank = 0;
#endif
#endif
	int dog_x = 80, dog_y = GRASS_Y, dog_frame = FRAME_DOG_WALK0;
	int intro_grass_over = 0;
	int dog_phase = 0, dog_timer = 0, dog_show = 0, dog_face = 0;
	int hit_mark[10];
	int flash = 0;
	int ab_hold = 0, mode_hint = 0, top_score = NES_TITLE_TOP;
	int pause_sel = 0, pause_ignore = 0, pause_from = ST_PLAY;
	int hud_clk = 0, tally_t = 0, tally_hold = 0;
	int cal_shots = HUNT_CAL_SHOTS;
	int title_played = 0;
#ifdef HUNTPS1_DIAG_SKIPCAL
	int sound_cue = SFX_TITLE;
	int sound_wait = 0;
	int sound_played = 0;
	int prev_l1 = 0, prev_r1 = 0, prev_cir = 0;
	int shot_rep = 0;
	int shot_rep_t = 0;
	int sq_pend = 0;
	const char *shot_lab = "SHOT LAB";
	char noise_lab[24];
#endif
#ifdef HUNTPS1_EMU_TEST
	int emu_f = 0;
#endif
	int hunt_tx[GUNCAL_POINTS], hunt_ty[GUNCAL_POINTS];
	int i;
	char line[52];

	tx = hunt_cal_tx(0);
	ty = hunt_cal_ty(0);
	fill_hunt_targets(hunt_tx, hunt_ty);

	for (i = 0; i < 10; i++)
		hit_mark[i] = 0;

	ResetGraph(0);
	FntLoad(960, 0);
	setup_context(&ctx);
	gfx_upload();
#ifdef HUNTPS1_GAME_C
	hunt_clay_upload();
#endif
#if defined(HUNTPS1_DELUXE_MENU) && !defined(HUNTPS1_WOODS_INTEGRATED)
	deluxe_woods_upload();
#endif
	hunt_sfx_init();
	guncal_reset(&g_cal);
#if defined(HUNTPS1_EMU_TEST) || defined(HUNTPS1_DIAG_SKIPCAL)
	/* Diagnostic only: identity cal. Does not change GunCon sample/map. */
	g_cal.has_cal = 1;
	g_cal.use_scale = 0;
	g_cal.sx = GUNCAL_FP_ONE;
	g_cal.sy = GUNCAL_FP_ONE;
	g_cal.ox = 0;
	g_cal.oy = 0;
#endif

	InitPAD(pad_buff[0], sizeof(pad_buff[0]), pad_buff[1], sizeof(pad_buff[1]));
	pad_buff[0][0] = 0xff;
	pad_buff[1][0] = 0xff;
	hunt_cal_card_init();
	StartPAD();
	StartCARD();
	ChangeClearPAD(0);
	_bu_init();
#if !defined(HUNTPS1_EMU_TEST) && !defined(HUNTPS1_DIAG_SKIPCAL)
	g_cal_card_status = hunt_cal_load(&g_cal);
#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
	g_zapper_flash = hunt_cal_flash_enabled();
	int flash_save_status = -1;
#endif
	g_cal_loaded_at_boot = g_cal_card_status == CAL_SAVE_LOADED;
#endif

	for (;;) {
		PADTYPE *pad1 = (PADTYPE *)pad_buff[0];
		PADTYPE *pad2 = (PADTYPE *)pad_buff[1];
		PADTYPE snap;
		int have_gun = 0, have_light = 0, have_pad = 0;
		int trig = 0, btn_a = 0, btn_b = 0, btn_sel = 0;
		int btn_up = 0, btn_dn = 0;
		int btn_start = 0;
		int can_pause = state == ST_INTRO || state == ST_PLAY ||
			state == ST_ROUND || state == ST_TALLY
#ifdef HUNTPS1_GAME_C
			|| state == ST_CLAY
#endif
#ifdef HUNTPS1_DELUXE_MENU
			|| state == ST_DELUXE_PLAY
#endif
			;
#ifdef HUNTPS1_DELUXE_MENU
		int title_side = 0;
#endif
		int duck_steer = 0;
#ifdef HUNTPS1_DIAG_SKIPCAL
		int btn_l1 = 0, btn_r1 = 0, btn_cir = 0;
#endif
		int orig_x = 0, orig_y = 0, aim_x = 0, aim_y = 0;
		int hit_x = ctx.latched_x, hit_y = ctx.latched_y;
		int hittable = ctx.latched_hit;
		uint16_t raw_x = 0, raw_y = 0;
		GunSample gs;
		PADTYPE *gunp = 0;
		RenderBuffer *ab;
		int spd;
		int grass_over;
#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
		int opened_options = 0;
#endif

		memset(&snap, 0, sizeof(snap));
		steer_clock++;
		if (pad1->stat == 0 && pad1->type == PAD_ID_GUNCON) {
			snap = *pad1;
			gunp = pad1;
			have_gun = 1;
		} else if (pad2->stat == 0 && pad2->type == PAD_ID_GUNCON) {
			snap = *pad2;
			gunp = pad2;
			have_gun = 1;
		}
		if (pad1->stat == 0 &&
			(pad1->type == PAD_ID_DIGITAL || pad1->type == PAD_ID_ANALOG))
			have_pad = 1;
		else if (pad2->stat == 0 &&
			(pad2->type == PAD_ID_DIGITAL || pad2->type == PAD_ID_ANALOG))
			have_pad = 1;

		if (have_gun) {
			trig = !(snap.btn & GCON_TRIGGER);
			btn_a = !(snap.btn & GCON_A);
			btn_b = !(snap.btn & GCON_B);
		}
		/* Menu controls are shared. Keep aiming and calibration on the
		 * existing selected gun, and let either gun pause active play. */
		int shared_menu = state == ST_TITLE || state == ST_PAUSE
#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
			|| state == ST_OPTIONS
#endif
#ifdef HUNTPS1_DELUXE_MENU
			|| state == ST_DELUXE || state == ST_DELUXE_PLAYERS || state == ST_DELUXE_RESULT
#endif
			;
		if (pad2->stat == 0 && pad2->type == PAD_ID_GUNCON && gunp != pad2) {
			if (can_pause || shared_menu)
				btn_b |= !(pad2->btn & GCON_B);
			if (shared_menu) {
				btn_a |= !(pad2->btn & GCON_A);
				trig |= !(pad2->btn & GCON_TRIGGER);
			}
		}
		for (int port = 0; have_pad && port < 2; port++) {
			PADTYPE *p = port ? pad2 : pad1;
			if (p->stat != 0 || (p->type != PAD_ID_DIGITAL && p->type != PAD_ID_ANALOG))
				continue;

			if (port && !shared_menu && pad1->stat == 0 &&
					(pad1->type == PAD_ID_DIGITAL || pad1->type == PAD_ID_ANALOG)) {
				btn_b |= !(p->btn & PAD_SQUARE);
				btn_start |= !(p->btn & PAD_START);
				break;
			}

			if (!(p->btn & PAD_TRIANGLE))
				btn_a = 1;
			if (!(p->btn & PAD_SQUARE))
				btn_b = 1;
			if (!(p->btn & PAD_SELECT))
				btn_sel = 1;
			if (!(p->btn & PAD_UP))
				btn_up = 1;
			if (!(p->btn & PAD_DOWN))
				btn_dn = 1;
#ifdef HUNTPS1_DELUXE_MENU
			if (!title_side)
				title_side = !(p->btn & PAD_LEFT) ? -1 : !(p->btn & PAD_RIGHT) ? 1 : 0;
#endif
			btn_start |= !(p->btn & PAD_START);
			if (!can_pause &&
					(!(p->btn & PAD_CROSS) || !(p->btn & PAD_START)))
				trig = 1;
			if (have_gun && state == ST_PLAY) {
				duck_steer = (!(p->btn & PAD_RIGHT) ? 1 : 0) |
					(!(p->btn & PAD_LEFT) ? 2 : 0) |
					(btn_dn ? 4 : 0) | (btn_up ? 8 : 0);
			}
#ifdef HUNTPS1_DIAG_SKIPCAL
			if (!(p->btn & PAD_L1))
				btn_l1 = 1;
			if (!(p->btn & PAD_R1))
				btn_r1 = 1;
			if (!(p->btn & PAD_CIRCLE))
				btn_cir = 1;
#endif
			if (!shared_menu && !can_pause)
				break;
		}

		gun_fill_sample(&gs, have_gun, gunp ? gunp : &snap);
		have_light = gs.valid;
		raw_x = (uint16_t)gs.raw_x;
		raw_y = (uint16_t)gs.raw_y;
		orig_x = gs.orig_x;
		orig_y = gs.orig_y;
		aim_x = gs.cal_x;
		aim_y = gs.cal_y;

		flash = 0;
		if (state == ST_PLAY && dog_show == 0 && respawn == 0 &&
			!falling && !escaping) {
			hud_clk++;
			flash = hunt_flash_blank(hud_clk);
		}
		if (mode_hint > 0)
			mode_hint--;
		if (pause_ignore > 0)
			pause_ignore--;
		hunt_sfx_frame();
#ifdef HUNTPS1_DIAG_SKIPCAL
		hunt_spu_noise_shot_frame();
		/*
		 * Isolated cue: L1 plays only SFX_TITLE, R1 only SFX_ROUND.
		 * 45 silent frames, then one shot. No other voice is keyed.
		 * SQUARE+UP is the SPU hardware-noise gunshot (voice 8).
		 * SQUARE alone still returns to title; UP while SQUARE is
		 * held is waited 12 frames so the chord is reachable.
		 */
		if ((btn_l1 && !prev_l1) || (btn_r1 && !prev_r1)) {
			if (state == ST_TITLE || state == ST_SOUND) {
				sound_cue = (btn_r1 && !prev_r1) ? SFX_ROUND : SFX_TITLE;
				sound_wait = 45;
				sound_played = 0;
				shot_rep = 0;
				sq_pend = 0;
				hunt_spu_noise_shot_stop();
				hunt_sfx_reset();
				state = ST_SOUND;
			}
		} else if (state == ST_SOUND) {
			int noise_chord = 0;
			int go_back = 0;

			if (sound_wait > 0)
				sound_wait--;
			else if (!sound_played) {
				hunt_sfx_play(sound_cue);
				sound_played = 1;
			}
			if (btn_b && btn_up && ((!prev_b) || (!prev_up)))
				noise_chord = 1;
			if (btn_b && !prev_b && !btn_up)
				sq_pend = 12;
			if (sq_pend > 0) {
				sq_pend--;
				if (btn_up) {
					noise_chord = 1;
					sq_pend = 0;
				} else if (sq_pend == 0 && !btn_up)
					go_back = 1;
			}
			if (noise_chord) {
				shot_rep = 0;
				shot_lab = "SPU NOISE";
				sound_played = 1;
				hunt_spu_noise_shot_start();
			} else if (btn_a && !prev_a) {
				shot_rep = 0;
				shot_lab = "V14 SHOT";
				hunt_spu_noise_shot_stop();
				hunt_sfx_reset();
				hunt_sfx_play_buf(sfx_v14_shot, SFX_V14_SHOT_BYTES, 22050);
			} else if (btn_cir && !prev_cir) {
				shot_rep = 0;
				shot_lab = "OLD CAPTURE";
				hunt_spu_noise_shot_stop();
				hunt_sfx_reset();
				hunt_sfx_play_buf(native_shot, NATIVE_SHOT_BYTES, 22050);
			} else if (btn_up && !prev_up) {
				shot_rep = 0;
				shot_lab = "ROM ATTACK";
				hunt_spu_noise_shot_stop();
				hunt_sfx_reset();
				hunt_sfx_play_buf(native_shot_game, NATIVE_SHOT_GAME_BYTES, 22050);
			}
			if (btn_dn && !prev_dn) {
				shot_rep = 0;
				shot_lab = "ROM FULL";
				hunt_spu_noise_shot_stop();
				hunt_sfx_reset();
				hunt_sfx_play_buf(native_shot_full, NATIVE_SHOT_FULL_BYTES, 22050);
			}
			if (btn_sel && !prev_sel) {
				shot_rep = !shot_rep;
				shot_rep_t = 0;
				shot_lab = shot_rep ? "GAME REPEAT" : "REPEAT OFF";
				if (shot_rep) {
					hunt_spu_noise_shot_stop();
					hunt_sfx_reset();
					hunt_sfx_play_buf(native_shot_game, NATIVE_SHOT_GAME_BYTES, 22050);
				}
			}
			if (shot_rep) {
				shot_rep_t++;
				if (shot_rep_t >= 40) {
					shot_rep_t = 0;
					hunt_spu_noise_shot_stop();
					hunt_sfx_reset();
					hunt_sfx_play_buf(native_shot_game, NATIVE_SHOT_GAME_BYTES, 22050);
				}
			}
			if ((trig && !prev_trig) || go_back) {
				shot_rep = 0;
				sq_pend = 0;
				hunt_spu_noise_shot_stop();
				hunt_sfx_reset();
				state = ST_TITLE;
			}
		} else if (sq_pend) {
			sq_pend = 0;
			hunt_spu_noise_shot_stop();
		}
#endif
		color_rng = hunt_color_rng_step(color_rng);
		if (state != ST_PLAY && state != ST_PAUSE)
			popup_left = 0;
		if (state == ST_TITLE
#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
				|| state == ST_OPTIONS
#endif
				) {
			if (!title_played) {
				hunt_sfx_play(SFX_TITLE);
				title_played = 1;
			}
		} else {
			title_played = 0;
		}

#ifdef HUNTPS1_EMU_TEST
		emu_f++;
		if (state == ST_TITLE && emu_f == 90)
			trig = 1;
		if (state == ST_PLAY) {
			if (emu_f == 420 || emu_f == 480)
				trig = 1;
			if (emu_f == 540) {
				trig = 1;
				btn_sel = 1;
			}
			if (emu_f == 620) {
				start_retrieve(&dog_x, &dog_y, &dog_frame, &dog_phase,
					&dog_timer, &dog_show, 1, bx);
				hunt_sfx_play(SFX_LAUGH);
			}
			if (emu_f == 700)
				btn_b = 1;
			if (emu_f == 900) {
				for (i = 0; i < 10; i++)
					hit_mark[i] = (i < 7) ? 2 : 1;
				hits_round = 7;
				birds_done = 10;
				score = 3500;
				tally_t = HUD_TALLY_SLOT_FR;
				tally_hold = 0;
				dog_show = 0;
				respawn = 0;
				state = ST_TALLY;
			}
		} else if (state == ST_PAUSE) {
			if (emu_f == 760)
				btn_b = 1;
		}
#endif

		if (can_pause && pause_ignore == 0 &&
				((btn_b && !prev_b) || (btn_start && !prev_start))) {
			pause_from = state;
			pause_sel = 0;
			pause_ignore = 24;
			hunt_sfx_pause(1);
			state = ST_PAUSE;
		} else if (state == ST_TITLE) {
#ifdef HUNTPS1_DELUXE_MENU
			const int last_menu_item = 4;
#else
			const int last_menu_item = 3;
#endif
			if (btn_a && btn_b) {
				ab_hold++;
				if (ab_hold == AB_HOLD_FRAMES) {
					gfx_set_wide(!gfx_wide());
					mode_hint = 90;
				}
			} else {
				ab_hold = 0;
				if (btn_up && !prev_up) {
					menu_sel--;
					if (menu_sel < 0)
						menu_sel = last_menu_item;
				}
				if ((btn_a && !prev_a) || (btn_b && !prev_b) || (btn_dn && !prev_dn)) {
					menu_sel++;
					if (menu_sel > last_menu_item)
						menu_sel = 0;
				}
			}
#ifdef HUNTPS1_DELUXE_MENU
			if (menu_sel >= 3 && title_side)
				menu_sel = title_side < 0 ? 3 : 4;
#endif
			if (btn_sel && !prev_sel) {
				gfx_set_wide(!gfx_wide());
				mode_hint = 90;
			}
			if (trig && !prev_trig) {
#ifdef HUNTPS1_DELUXE_MENU
				if (menu_sel == 3) {
					deluxe_sel = 0;
					state = ST_DELUXE;
					prev_trig = 1;
				} else
#endif
				if (menu_sel == last_menu_item) {
#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
					options_sel = 0;
#ifdef HUNTPS1_DELUXE_MENU
					options_return = ST_TITLE;
#endif
					opened_options = 1;
					state = ST_OPTIONS;
#else
					hunt_sfx_stop_music();
					begin_cal(&pt, &tx, &ty);
					state = ST_WELCOME;
#endif
#ifdef HUNTPS1_GAME_C
				} else if (menu_sel == 2 && g_cal.has_cal) {
					hunt_sfx_stop_music();
restart_clay:
					hunt_clay_round_start(&g_clay, color_rng);
					clay_flash_index = clay_flash_count = 0;
					clay_launch_audio[0] = clay_launch_audio[1] = 0;
					hunt_sfx_play(SFX_CLAY_GO);
					state = ST_CLAY;
#else
				} else if (
#ifndef HUNTPS1_GAME_B
						menu_sel == 1 ||
#endif
						menu_sel == 2) {
					hunt_sfx_stop_music();
					nyi_which = menu_sel;
					state = ST_NYI;
					wait = 90;
#endif
				} else if (!g_cal.has_cal) {
					hunt_sfx_stop_music();
					state = ST_NEEDCAL;
				} else {
#ifdef HUNTPS1_GAME_B
					game_b = menu_sel == 1;
					pair_started = 0;
#endif
					round_n = 1;
					score = 0;
					birds_done = 0;
					hits_round = 0;
					for (i = 0; i < 10; i++)
						hit_mark[i] = 0;
					state = ST_INTRO;
					start_intro(&dog_x, &dog_y, &dog_frame, &dog_phase,
						&dog_timer, &dog_show, &dog_face);
					hunt_sfx_play(SFX_ROUND);
				}
			}
		} else {
			ab_hold = 0;
		}

#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
		if (state == ST_OPTIONS) {
			if (!opened_options) {
				int options_count = 3, return_state = ST_TITLE;
#ifdef HUNTPS1_DELUXE_MENU
				return_state = options_return;
#endif
				if (btn_up && !prev_up)
					options_sel = (options_sel + options_count - 1) % options_count;
				else if ((btn_a && !prev_a) || (btn_dn && !prev_dn))
					options_sel = (options_sel + 1) % options_count;
				if (btn_b && !prev_b)
					state = return_state;
				else if (trig && !prev_trig) {
					if (options_sel == 0) {
						cal_from_options = 1;
						hunt_sfx_stop_music();
						state = ST_WELCOME;
					} else if (options_sel == 1) {
						g_zapper_flash = !g_zapper_flash;
						flash_save_status = hunt_cal_save_flash(g_zapper_flash);
					} else {
						state = return_state;
					}
				}
			}
		} else
#endif
#ifdef HUNTPS1_DELUXE_MENU
		if (state == ST_DELUXE) {
			if ((btn_up && !prev_up) || (btn_b && !prev_b)) {
				deluxe_sel = (deluxe_sel + 4) % 5;
			} else if ((btn_a && !prev_a) || (btn_dn && !prev_dn)) {
				deluxe_sel = (deluxe_sel + 1) % 5;
			}
			if (deluxe_sel >= 3 && title_side)
				deluxe_sel = title_side < 0 ? 3 : 4;
			if (trig && !prev_trig) {
				if (deluxe_sel == 4) {
					options_sel = 0;
					options_return = ST_DELUXE;
					state = ST_OPTIONS;
				} else if (deluxe_sel == 3) {
					state = ST_TITLE;
				} else {
					player_sel = 0;
					state = ST_DELUXE_PLAYERS;
				}
			}
		} else if (state == ST_DELUXE_PLAYERS) {
			int horizontal = title_side;
			if (deluxe_sel == DELUXE_SHOOTOUT && horizontal && horizontal != deluxe_prev_horizontal) {
				int next = deluxe_population + 2 * horizontal;
				if (next >= 4 && next <= DELUXE_MAX_DUCKS)
					deluxe_population = next;
			}
			deluxe_prev_horizontal = horizontal;
			if (btn_up && !prev_up)
				player_sel = (player_sel + 2) % 3;
			else if ((btn_a && !prev_a) || (btn_dn && !prev_dn))
				player_sel = (player_sel + 1) % 3;
			if ((btn_b && !prev_b) || (trig && !prev_trig && player_sel == 2))
				state = ST_DELUXE;
			else if (trig && !prev_trig) {
				deluxe_ports[0] = player_sel == 0 && gunp == pad2 ? 1 : 0;
				deluxe_ports[1] = 1;
				deluxe_held[0] = deluxe_held[1] = 1;
				deluxe_missing = 0;
#ifdef HUNTPS1_WOODS_INTEGRATED
				if (deluxe_sel == DELUXE_WOODS) {
					hunt_sfx_stop_music();
					hunt_sfx_stop_loops();
					draw_cal_label(&ctx, ctx.buffers[ctx.active_buffer].ot,
						&ctx.next_packet, 108, "ENTERING THE WOODS");
					flip_buffers(&ctx);
					flip_buffers(&ctx);
					int completed = woods3d_run_deluxe(pad_buff, &g_cal,
						player_sel + 1, deluxe_ports[0]);
					hunt_sfx_stop_loops();
					setup_context(&ctx);
					gfx_upload();
					hunt_clay_upload();
					state = completed ? ST_DELUXE_PLAYERS : ST_DELUXE;
					prev_trig = prev_a = prev_b = prev_sel = prev_up = prev_dn = 1;
					continue;
				}
#endif
restart_deluxe:
				memset(deluxe_crt, 0, sizeof(deluxe_crt));
				memset(deluxe_latched, 0, sizeof(deluxe_latched));
				memset(deluxe_submitted, 0, sizeof(deluxe_submitted));
				deluxe_start(&g_deluxe_match, deluxe_sel, player_sel + 1, deluxe_population, color_rng);
				for (i = 0; i < 4; i++)
					deluxe_perf[i] = 0;
				deluxe_last_vblank = 0;
				hunt_sfx_stop_music();
				hunt_sfx_begin_flight();
				state = ST_DELUXE_PLAY;
			}
		} else if (state == ST_DELUXE_PLAY) {
			DeluxeShot input[2] = {{0}};
			int connected = 0;
			/* Solo accepts either port, including a gun connected after selection. */
			if (g_deluxe_match.players == 1 && gunp && deluxe_ports[0] != (gunp == pad2)) {
				deluxe_ports[0] = gunp == pad2;
				deluxe_held[0] = 1;
			}
			for (i = 0; i < g_deluxe_match.players; i++) {
				const PADTYPE pad = *(deluxe_ports[i] ? pad2 : pad1);
				int present = pad.stat == 0 && pad.type == PAD_ID_GUNCON;
				int held = present && !(pad.btn & GCON_TRIGGER);
				connected += present;
				input[i].fired = held && !deluxe_held[i];
				deluxe_held[i] = !present || held;
				if (!input[i].fired || !guncon_has_light(pad.gun_x, pad.gun_y))
					continue;
				input[i].valid = 1;
				map_gun_to_screen(pad.gun_x, pad.gun_y, &input[i].x, &input[i].y);
				/* The original save contains one profile. P2 starts with raw mapping. */
				if (i == 0 && g_cal.has_cal)
					guncal_apply(&g_cal, input[i].x, input[i].y, &input[i].x, &input[i].y);
#ifndef HUNTPS1_WOODS_INTEGRATED
				if (deluxe_sel == DELUXE_WOODS &&
						deluxe_woods_occluded(woods_latched, input[i].x, input[i].y))
					input[i].valid = 0;
#endif
			}
			deluxe_missing = connected < g_deluxe_match.players;
			if (!deluxe_missing) {
				unsigned hit = deluxe_fire(&g_deluxe_match, deluxe_latched, input, box_hw(), box_hh());
				if (input[0].fired || input[1].fired)
					hunt_sfx_play(SFX_SHOT);
				if (hit)
					hunt_sfx_play(SFX_HIT);
				deluxe_tick(&g_deluxe_match);
				unsigned flying = 0;
				for (i = 0; i < g_deluxe_match.count; i++)
					flying += g_deluxe_match.ducks[i].flight.duck[0].phase == PAIR_FLY;
				if (flying > deluxe_perf[3])
					deluxe_perf[3] = flying;
				if (flying)
					hunt_sfx_pair_flight(0);
				if (g_deluxe_match.finished) {
					hunt_sfx_play(deluxe_sel == DELUXE_WOODS &&
						g_deluxe_match.player[0].hits + g_deluxe_match.player[1].hits ==
						DELUXE_WOODS_DUCKS ? SFX_PERFECT : SFX_CLEAR);
					state = ST_DELUXE_RESULT;
				}
			}
		} else if (state == ST_DELUXE_RESULT) {
			if ((trig && !prev_trig) || (btn_b && !prev_b))
				state = ST_DELUXE;
		} else
#endif
#ifdef HUNTPS1_GAME_C
		if (state == ST_CLAY) {
			if (trig && !prev_trig) {
				g_clay_shot_diag.edges++;
				if (clay_flash_count || ctx.latched_flash)
					g_clay_shot_diag.blocked++;
			}
			if (g_clay.phase == CLAY_OVER && g_clay.age >= CLAY_OVER_FRAMES) {
				if (g_clay.score > top_score)
					top_score = g_clay.score;
				hunt_sfx_reset();
				state = ST_TITLE;
			} else if (!clay_flash_count && !ctx.latched_flash) {
				int before = g_clay.phase;
				int tally = g_clay.tally;
				int launched = g_clay.launched;
				if (trig && !prev_trig) {
					int result = hunt_clay_round_fire(&g_clay, ctx.latched_clay,
						have_light, (aim_x * 4 + 3) / 5, aim_y);
					int target = ctx.latched_clay[0].live ? 0 : 1;
					g_clay_shot_diag.last_result = result;
					g_clay_shot_diag.raw_x = raw_x;
					g_clay_shot_diag.raw_y = raw_y;
					g_clay_shot_diag.aim_x = (aim_x * 4 + 3) / 5;
					g_clay_shot_diag.aim_y = aim_y;
					g_clay_shot_diag.target_x = ctx.latched_clay[target].x;
					g_clay_shot_diag.target_y = ctx.latched_clay[target].y;
					g_clay_shot_diag.target_size = ctx.latched_clay[target].hit_size;
					g_clay_shot_diag.target_live = ctx.latched_clay[target].live;
					g_clay_shot_diag.accepted += result != -2;
					g_clay_shot_diag.hits += result >= 0;
					g_clay_shot_diag.no_light += result != -2 && !have_light;
					if (result != -2) {
						hunt_sfx_play(SFX_SHOT);
						if (g_zapper_flash) {
							int first = ctx.latched_clay[0].live ? 0 : 1;
							memcpy(clay_flash_target, ctx.latched_clay,
								sizeof(clay_flash_target));
							clay_scan[0] = -1;
							clay_scan[1] = first;
							clay_scan[2] = first == 0 && result != 0 &&
								ctx.latched_clay[1].live ? 1 : -1;
							clay_scan[3] = clay_scan[2];
							clay_flash_index = 0;
							clay_flash_count = clay_scan[2] >= 0 ?
								(result < 0 ? 4 : 3) : (result < 0 ? 3 : 2);
						}
					}
				}
				hunt_clay_round_tick(&g_clay);
				for (i = 0; i < 2; i++) {
					if (clay_launch_audio[i] && !--clay_launch_audio[i])
						hunt_sfx_play(SFX_CLAY_LAUNCH);
					if (g_clay.broken[i] == 57)
						hunt_sfx_play(SFX_CLAY_BREAK);
				}
				if (g_clay.launched > launched)
					clay_launch_audio[launched] = 4;
				if (g_clay.tally > tally)
					hunt_sfx_play(SFX_TALLY);
				if (g_clay.phase != before) {
					if (g_clay.phase == CLAY_GO)
						hunt_sfx_play(SFX_CLAY_GO);
					if (g_clay.phase == CLAY_CLEAR)
						hunt_sfx_play(SFX_CLEAR);
					if (g_clay.phase == CLAY_BONUS && g_clay.bonus)
						hunt_sfx_play(SFX_PERFECT);
					if (g_clay.phase == CLAY_OVER)
						hunt_sfx_play(SFX_GAME_OVER);
				}
			}
		} else
#endif
		if (state == ST_NYI) {
			if (wait > 0)
				wait--;
			if (wait == 0 || (trig && !prev_trig) || (btn_b && !prev_b))
				state = ST_TITLE;
		} else if (state == ST_NEEDCAL) {
			if (trig && !prev_trig) {
#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
				cal_from_options = 0;
#endif
				begin_cal(&pt, &tx, &ty);
				state = ST_WELCOME;
			}
			if (btn_b && !prev_b)
				state = ST_TITLE;
		} else if (state == ST_WELCOME) {
#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
			if (btn_b && !prev_b) {
				state = cal_from_options ? ST_OPTIONS : ST_TITLE;
				cal_from_options = 0;
			} else if (trig && !prev_trig) {
				if (cal_from_options)
					begin_cal(&pt, &tx, &ty);
				state = ST_CAL;
			}
#else
			if (trig && !prev_trig)
				state = ST_CAL;
			if (btn_b && !prev_b)
				state = ST_TITLE;
#endif
		} else if (state == ST_CAL && trig && !prev_trig) {
			if (!have_light) {
				fb = FEEDBACK + 20;
				fb_ok = 0;
			} else if (guncal_add_shot(&g_cal, pt, orig_x, orig_y, raw_x, raw_y)) {
				fb = FEEDBACK;
				fb_ok = 1;
				if (cal_point_full(&g_cal, pt, cal_shots)) {
					wait = WAIT_FRAMES;
					state = ST_WAIT;
				}
			}
		} else if (state == ST_WAIT) {
			if (wait > 0)
				wait--;
			if (wait == 0) {
				if (pt < GUNCAL_POINTS - 1) {
					pt++;
					tx = hunt_cal_tx(pt);
					ty = hunt_cal_ty(pt);
					state = ST_CAL;
				} else {
					guncal_compute_to_min(&g_cal, hunt_tx, hunt_ty, cal_shots);
					g_cal_card_busy = 1;
					g_cal_card_status = hunt_cal_save(&g_cal);
					g_cal_card_busy = 0;
					state = ST_DONE;
				}
			}
		} else if (state == ST_DONE) {
			if ((trig && !prev_trig) || (btn_b && !prev_b))
				state = ST_TITLE;
		} else if (state == ST_PAUSE) {
#ifdef HUNTPS1_DELUXE_MENU
			deluxe_held[0] = deluxe_held[1] = 1;
#endif
			if (btn_up && !prev_up) {
				pause_sel--;
				if (pause_sel < 0)
					pause_sel = 2;
			}
			if ((btn_a && !prev_a) || (btn_dn && !prev_dn)) {
				pause_sel++;
				if (pause_sel > 2)
					pause_sel = 0;
			}
			if (pause_ignore == 0 && btn_b && !prev_b) {
				pause_ignore = 24;
				hunt_sfx_pause(0);
				state = pause_from;
			}
			if (pause_ignore == 0 && trig && !prev_trig) {
				pause_ignore = 24;
				if (pause_sel == 0) {
					hunt_sfx_pause(0);
					state = pause_from;
				} else if (pause_sel == 1) {
					hunt_sfx_reset();
#ifdef HUNTPS1_GAME_C
					if (pause_from == ST_CLAY)
						goto restart_clay;
#endif
#ifdef HUNTPS1_DELUXE_MENU
					if (pause_from == ST_DELUXE_PLAY) {
						deluxe_held[0] = deluxe_held[1] = 1;
						goto restart_deluxe;
					}
#endif
#ifdef HUNTPS1_GAME_B
					pair_started = 0;
#endif
					round_n = 1;
					score = 0;
					birds_done = 0;
					hits_round = 0;
					won = 0;
					falling = 0;
					escaping = 0;
					respawn = 0;
					shots = 3;
					for (i = 0; i < 10; i++)
						hit_mark[i] = 0;
					start_intro(&dog_x, &dog_y, &dog_frame, &dog_phase,
						&dog_timer, &dog_show, &dog_face);
					hunt_sfx_play(SFX_ROUND);
					state = ST_INTRO;
				} else {
					hunt_sfx_reset();
					state = ST_TITLE;
#ifdef HUNTPS1_DELUXE_MENU
					if (pause_from == ST_DELUXE_PLAY)
						state = ST_DELUXE;
#endif
				}
			}
		} else if (state == ST_TALLY) {
			if (tally_t > 0)
				tally_t--;
			if (tally_t == 0) {
				if (hunt_tally_step(hit_mark, 10)) {
					hunt_sfx_play(SFX_TALLY);
					tally_t = HUD_TALLY_SLOT_FR;
				} else if (hits_round >= hunt_hits_needed(round_n)) {
					if (tally_hold == 0) {
						tally_hold = HUD_TALLY_HOLD_FR;
						hunt_sfx_play(SFX_CLEAR);
					}
					else {
						tally_hold--;
						if (tally_hold == 0) {
							if (hits_round == BIRDS_ROUND) {
								hunt_sfx_play(SFX_PERFECT);
								score += PERFECT_BONUS;
								if (score > top_score)
									top_score = score;
							}
							round_n++;
#ifdef HUNTPS1_GAME_B
							pair_started = 0;
#endif
							birds_done = 0;
							hits_round = 0;
							for (i = 0; i < 10; i++)
								hit_mark[i] = 0;
							state = ST_ROUND;
							wait = ROUND_ANNOUNCE_FR;
							spawn_bird(round_n, &bx, &by, &sx, &sy, &facing,
								&falling, &shots, &frame, &anim_div,
								&anim_step, &script, &dir_timer, &hitstun,
								&fly_timer, &escaping, &spd_i, &dtype, &nes_pat);
						}
					}
				} else {
					won = 0;
					wait = HUD_TALLY_FAIL_FR;
					hunt_sfx_play(SFX_GAME_OVER);
					state = ST_OVER;
				}
			}
		} else if (state == ST_ROUND) {
			if (wait > 0)
				wait--;
			/* Duck is drawn only in ST_PLAY. M3 oneshot is 330 VBlanks;
			 * ST_INTRO+ROUND_ANNOUNCE is 279. Hold here, keep the banner. */
			if (wait == 0 && !hunt_sfx_intro_hold())
				state = ST_PLAY;
		} else if (state == ST_OVER) {
			/* NES fail: tally then title (Mesen late_round @8000). */
			if (wait > 0)
				wait--;
			if ((wait == 0 && !hunt_sfx_game_over_busy()) || (trig && !prev_trig)) {
				if (score > top_score)
					top_score = score;
				hunt_sfx_reset();
				state = ST_TITLE;
			}
#ifdef HUNTPS1_GAME_B
		} else if (state == ST_PLAY && game_b) {
			int hit = -1;
			if (!pair_started) {
				hunt_pair_start(&g_pair, round_n, color_rng);
				color_rng = g_pair.rng;
				pair_started = 1;
				shots = SHOTS_BIRD;
				dog_show = 0;
				hunt_sfx_begin_flight();
			}
			if (trig && !prev_trig && shots > 0 &&
					(ctx.latched_pair[0].live || ctx.latched_pair[1].live)) {
				if (shot_flash_length || ctx.crt_flash || ctx.latched_flash) {
					g_flash_suppressed++;
				} else {
					int first = ctx.latched_pair[0].live ? 0 : 1;
					/* $D1A7/$D274 tests duck 0 before duck 1. */
					for (i = first; i < 2; i++)
						if (ctx.latched_pair[i].live &&
								g_pair.duck[i].phase == PAIR_FLY &&
								have_light && hits_bird(aim_x, aim_y,
									ctx.latched_pair[i].x, ctx.latched_pair[i].y)) {
							hit = i;
							break;
						}
					shots--;
					hunt_sfx_play(SFX_SHOT);
					if (hit >= 0) {
						score += hunt_pair_hit(&g_pair, hit);
						if (score > top_score)
							top_score = score;
						hit_mark[birds_done + hit] = 2;
						hits_round++;
						hunt_sfx_play(SFX_HIT);
					}
					if (g_zapper_flash) {
						shot_flash_second = first == 0 && hit != 0 &&
							ctx.latched_pair[1].live;
						shot_flash_frame = 0;
						shot_flash_length = 2 + shot_flash_second + (hit < 0);
						shot_flash_x = ctx.latched_pair[first].x - gfx_nw(16);
						shot_flash_y = ctx.latched_pair[first].y - 15;
						shot_flash_x2 = ctx.latched_pair[1].x - gfx_nw(16);
						shot_flash_y2 = ctx.latched_pair[1].y - 15;
					}
				}
			}
			for (i = 0; i < 2; i++) {
				HuntPairDuck *duck = &g_pair.duck[i];
				if (duck->phase == PAIR_HIT && duck->hit_left == 1)
					hunt_sfx_play(SFX_FALL);
				if (duck->phase == PAIR_FALL && duck->y >= 176)
					hunt_sfx_play(SFX_LAND);
			}
			hunt_pair_step(&g_pair, shots, hit);
			if (g_pair.duck[0].phase == PAIR_FLY || g_pair.duck[1].phase == PAIR_FLY)
				hunt_sfx_pair_flight(shots == 0);
			if (g_pair.result_age >= 0) {
				int age = g_pair.result_age - DUCK_RETRIEVE_DELAY;
				int laugh = g_pair.hits == 0;
				int shown = laugh ? 100 : 63;
				if (age == 0) {
					start_retrieve(&dog_x, &dog_y, &dog_frame, &dog_phase,
						&dog_timer, &dog_show, laugh, g_pair.last_hit_x);
					if (laugh)
						dog_y = 173; /* $CF39 Y=$AC, plus OAM scanline. */
					hunt_sfx_play(laugh ? SFX_LAUGH : SFX_RETRIEVE);
				}
				if (age >= 0 && age < shown) {
					dog_timer = age;
					if (laugh) {
						dog_frame = hunt_laugh_frame(age);
						if (age > 0 && age <= DOG_LAUGH_RISE)
							dog_y--;
						else if (age > DOG_LAUGH_RISE + DOG_LAUGH_PEAK)
							dog_y += 2; /* $E846 laugh sink commands. */
					} else {
						dog_frame = FRAME_DOG_HOLD + (g_pair.hits == 2);
						hunt_retrieve_y(age, &dog_y);
					}
				} else if (age >= shown) {
					dog_show = 0;
					if (age >= (laugh ? 171 : 110)) {
						birds_done += 2;
						pair_started = 0;
						if (birds_done >= BIRDS_ROUND) {
							tally_t = HUD_TALLY_SLOT_FR;
							tally_hold = 0;
							state = ST_TALLY;
						}
					}
				}
			}
#endif
		} else if (state == ST_PLAY) {
			if (popup_left > 0)
				popup_left--;
#ifdef HUNTPS1_DIAG_SKIPCAL
			if (btn_sel && !prev_sel && respawn == 0 && dog_show == 0 &&
				!falling && !escaping)
				diag_arm = 1;
#endif
#ifdef HUNTPS1_EXPERIMENTAL_ZAPPER_FLASH
			int fire = trig && !prev_trig && respawn == 0 && shots > 0 &&
				!falling && hitstun == 0 && dog_show == 0 && !escaping;
			/* Consume an edge during the queued/displayed flash without
			 * replaying it later or stopping the flight update. */
			if (fire && (shot_flash_length || ctx.crt_flash || ctx.latched_flash)) {
				g_flash_suppressed++;
				fire = 0;
			}
			if (fire) {
#else
			if (trig && !prev_trig && respawn == 0 && shots > 0 &&
				!falling && hitstun == 0 && dog_show == 0 && !escaping) {
#endif
				int hit = have_light && hittable &&
					hits_bird(aim_x, aim_y, hit_x, hit_y);
#ifdef HUNTPS1_EMU_TEST
				if (btn_sel)
					hit = 1;
#endif
#ifdef HUNTPS1_DIAG_SKIPCAL
				/* Instrumented only: Select arms the next shot as a hit.
				 * Not retail, and not a GunCon timing change. */
				if (diag_arm) {
					hit = 1;
					diag_arm = 0;
				}
#endif

				shots--;
#ifdef HUNTPS1_EXPERIMENTAL_ZAPPER_FLASH
				shot_flash_frame = 0;
#ifdef HUNTPS1_GAME_B
				shot_flash_second = 0;
#endif
				shot_flash_length =
#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
					g_zapper_flash ? (hit ? 2 : 3) : 0;
#else
					hit ? 2 : 3;
#endif
				shot_flash_x = hit_x - gfx_nw(16);
				shot_flash_y = hit_y - 15; /* NES OAM origin plus visible scanline. */
#endif
				hunt_sfx_play(SFX_SHOT);
				if (hit) {
					falling = 1;
					hitstun = DUCK_HIT_POSE_FRAMES;
					fall_tick = 0;
					fall_origin_x = bx;
					frame = FRAME_HIT;
					hunt_sfx_play(SFX_HIT);
					popup_tile = hunt_duck_score_tile(round_n, dtype);
					popup_x = bx - 8;
					popup_y = by - 3; /* $CFF9 Y-4, plus PPU OAM scanline. */
					score += hunt_duck_score_points(popup_tile);
					if (score > top_score)
						top_score = score;
					hits_round++;
					hit_mark[birds_done] = 2;
				} else if (shots == 0) {
#if HUNT_NES_ENGINE
					/* $CA29-$CA3B: first empty ammo → $CB77 pattern 0
					 * (FF 00 AA, straight up). Later frames set $AE. */
					hunt_nes_fly_start_escape(&g_nf);
					g_nf.ae = 1;
					nes_pat = g_nf.pat;
					script = g_nf.pose;
					facing = g_nf.flip;
#else
					escaping = 1;
					sy = -1;
#endif
					hit_mark[birds_done] = 1;
				}
			}

			if (dog_show) {
				dog_timer++;
				if (dog_phase == DOGPH_RISE || dog_phase == DOGPH_HOLD ||
					dog_phase == DOGPH_SINK) {
					dog_frame = FRAME_DOG_HOLD;
					if (!hunt_retrieve_y(dog_timer, &dog_y)) {
						dog_show = 0;
						dog_phase = 0;
						respawn = DUCK_NEXT_DELAY;
					}
				} else if (dog_phase == DOGPH_LAUGH) {
					dog_frame = hunt_laugh_frame(dog_timer);
					if (dog_timer <= DOG_LAUGH_RISE)
						dog_y -= 1;
					else if (dog_timer <= DOG_LAUGH_RISE + DOG_LAUGH_PEAK)
						;
					else
						dog_y += 1;
					if (dog_timer >= DOG_LAUGH_RISE + DOG_LAUGH_PEAK +
						DOG_LAUGH_SINK) {
						dog_show = 0;
						dog_phase = 0;
						respawn = DOG_NEXT_DELAY;
					}
				}
			} else if (respawn > 0) {
				respawn--;
				if (respawn == 0) {
					if (dog_phase == 99) {
						start_retrieve(&dog_x, &dog_y, &dog_frame, &dog_phase,
							&dog_timer, &dog_show, 0, fall_origin_x);
						hunt_sfx_play(SFX_RETRIEVE);
					} else if (dog_phase == 98) {
						start_retrieve(&dog_x, &dog_y, &dog_frame, &dog_phase,
							&dog_timer, &dog_show, 1, bx);
						hunt_sfx_play(SFX_LAUGH);
					} else {
						birds_done++;
						if (birds_done >= BIRDS_ROUND) {
							/* NES $D2DD tally: $2B delay then $AA vs $AB. */
							tally_t = HUD_TALLY_SLOT_FR;
							tally_hold = 0;
							state = ST_TALLY;
						} else {
							spawn_bird(round_n, &bx, &by, &sx, &sy, &facing,
								&falling, &shots, &frame, &anim_div, &anim_step,
								&script, &dir_timer, &hitstun, &fly_timer,
								&escaping, &spd_i, &dtype, &nes_pat);
						}
					}
				}
			} else if (hitstun > 0) {
				hitstun--;
				frame = FRAME_HIT;
			} else if (falling) {
				if (fall_tick == 0)
					popup_left = 47; /* $30 countdown, decrement before OAM. */
				/* $E64B pattern16: +2Y, alternating +1/-1X; five
				 * displayed frames per cel. Check the previous Y, as
				 * $CC7C does, retaining the final fall pose one update. */
				if (by >= 176) {
					falling = 0;
					respawn = DUCK_RETRIEVE_DELAY;
					dog_phase = 99;
					hunt_sfx_play(SFX_LAND);
				} else {
					if (fall_tick == 0)
						hunt_sfx_play(SFX_FALL);
					frame = FRAME_FALL0 + (fall_tick / DUCK_FALL_CEL_FRAMES) % 2;
					bx = fall_origin_x + ((fall_tick & 1) ? 0 : 1);
					by += 2;
					fall_tick++;
				}
			} else if (escaping) {
				if (++anim_div >= k_fly_dur[anim_step]) {
					anim_div = 0;
					anim_step++;
					if (anim_step > 2)
						anim_step = 0;
				}
				frame = fly_frame(script, anim_step);
				by -= 2;
				bx += sx;
				if (by < 8) {
					escaping = 0;
					respawn = DOG_LAND_DELAY;
					dog_phase = 98;
				}
			} else {
				if (fly_timer > 0)
					fly_timer--;
				if (++anim_div >= k_fly_dur[anim_step]) {
					anim_div = 0;
					anim_step++;
					if (anim_step > 2)
						anim_step = 0;
				}
				frame = fly_frame(script, anim_step);
#if HUNT_NES_ENGINE
				/* $4C expiry is the NES "time's up" ($CA6C). The
				 * host still uses FLYAWAY_FRAMES; it now sets $AE
				 * instead of abandoning the script immediately. */
				if (fly_timer == 0 || shots == 0)
					g_nf.ae = 1;
				if (hunt_nes_fly_step_control(&g_nf, duck_steer, steer_clock)) {
					/* $CF2A: $31=8. Laugh after the duck left the
					 * outer box. Do not add the host by-=2 climb. */
					bx = g_nf.x;
					by = g_nf.y;
					nes_pat = 0;
					script = hunt_nes_pose_script(0);
					facing = hunt_nes_facing(0);
					escaping = 0;
					respawn = DOG_LAND_DELAY;
					dog_phase = 98;
					if (hit_mark[birds_done] == 0)
						hit_mark[birds_done] = 1;
				} else {
					bx = g_nf.x;
					by = g_nf.y;
					nes_pat = g_nf.pat;
					script = g_nf.pose;
					facing = g_nf.flip;
					/* Select the sprite after the NES step updates its pattern.
					 * Previously a turn drew the preceding direction for one frame. */
					frame = fly_frame(script, anim_step);
				}
				(void)spd;
				(void)spd_i;
				(void)dir_timer;
#else
				spd = hunt_speed(round_n, spd_i);
				spd_i++;
				bx += sx * spd;
				by += sy * spd;
				if (--dir_timer <= 0) {
					dir_timer = 50 + (int)(rnd() % 90);
					pick_heading(&sx, &sy, &facing, &script);
					if (rnd() & 1)
						nes_pat = (nes_pat + 1) & 15;
					else
						nes_pat = (nes_pat + 15) & 15;
					script = hunt_nes_pose_script(nes_pat);
					facing = hunt_nes_facing(nes_pat);
				}
				if (bx < 16) {
					bx = 16;
					if (sx < 0)
						sx = 1;
					facing = 0;
				}
				if (bx > NES_SRC_W - 16) {
					bx = NES_SRC_W - 16;
					if (sx > 0)
						sx = -1;
					facing = 1;
				}
				if (by < 36) {
					by = 36;
					if (sy < 0)
						sy = 1;
				}
				if (by > GRASS_Y - 20) {
					by = GRASS_Y - 20;
					if (sy > 0)
						sy = -1;
				}
				script = hunt_nes_pose_script(nes_pat);
				facing = hunt_nes_facing(nes_pat);
#endif
				if (respawn == 0)
					hunt_sfx_flight(shots == 0);
#if !HUNT_NES_ENGINE
				if (shots == 0 || fly_timer == 0) {
					escaping = 1;
					sy = -1;
					nes_pat = 0;
					script = hunt_nes_pose_script(nes_pat);
					if (hit_mark[birds_done] == 0)
						hit_mark[birds_done] = 1;
				}
#endif
			}
		}

		/* Intro clock advances only while running; entering from title or a
		 * pause-menu restart uses the same frame-zero update/render boundary. */
		if (state == ST_INTRO) {
			if (dog_timer < DOG_INTRO_END_FRAME) {
				HuntIntroSample intro = hunt_intro_sample(dog_timer);
				dog_x = intro.x;
				dog_y = intro.y;
				dog_frame = intro.pose;
				dog_show = intro.visible;
				dog_phase = intro.phase;
				intro_grass_over = intro.grass_over;
			}
#ifdef HUNTPS1_DIAG_SKIPCAL
			if (btn_r1 && !prev_r1)
				hunt_sfx_play(SFX_BARK);
#endif
			if (dog_timer == DOG_INTRO_BARK_FRAME)
				hunt_sfx_play(SFX_BARK);
			if (dog_timer == DOG_INTRO_END_FRAME) {
				dog_show = 0;
				state = ST_ROUND;
				wait = DOG_INTRO_POST_WAIT;
				spawn_bird(round_n, &bx, &by, &sx, &sy, &facing,
					&falling, &shots, &frame, &anim_div, &anim_step,
					&script, &dir_timer, &hitstun, &fly_timer,
					&escaping, &spd_i, &dtype, &nes_pat);
			}
			dog_timer++;
		}
		prev_trig = trig;
		prev_a = btn_a;
		prev_b = btn_b;
		prev_start = btn_start;
		prev_sel = btn_sel;
		prev_up = btn_up;
		prev_dn = btn_dn;
#ifdef HUNTPS1_DIAG_SKIPCAL
		prev_l1 = btn_l1;
		prev_r1 = btn_r1;
		prev_cir = btn_cir;
#endif

		if (fb > 0)
			fb--;

		ab = &ctx.buffers[ctx.active_buffer];
#ifdef HUNTPS1_DELUXE_MENU
#ifndef HUNTPS1_WOODS_INTEGRATED
		woods_submitted[ctx.active_buffer] = (unsigned)g_deluxe_match.elapsed;
#endif
		for (i = 0; i < DELUXE_MAX_DUCKS; i++) {
			const DeluxeDuck *slot = &g_deluxe_match.ducks[i];
			const HuntPairDuck *duck = &slot->flight.duck[0];
			deluxe_submitted[ctx.active_buffer][i] = (DeluxeTarget){
				gfx_nx(duck->x), duck->y,
				state == ST_DELUXE_PLAY && i < g_deluxe_match.count && duck->phase == PAIR_FLY,
				slot->generation
			};
		}
#endif
#ifdef HUNTPS1_GAME_C
		for (i = 0; i < 2; i++) {
			ab->clay_target[i] = g_clay.target[i];
			if (state != ST_CLAY)
				ab->clay_target[i].live = 0;
		}
#endif
#ifdef HUNTPS1_GAME_B
		for (i = 0; i < 2; i++) {
			const HuntPairDuck *duck = &g_pair.duck[i];
			ab->pair_target[i].x = gfx_nx(duck->x);
			ab->pair_target[i].y = duck->y;
			ab->pair_target[i].live = game_b && pair_started && state == ST_PLAY &&
				duck->phase == PAIR_FLY && duck->y < 240;
		}
#endif
		if (state == ST_CAL || state == ST_WAIT) {
			ab->latch_x = tx;
			ab->latch_y = ty;
			ab->latch_hit = 1;
		} else if (state == ST_PLAY && dog_show == 0 && respawn == 0 &&
			!escaping) {
			int dh = (!falling || hitstun > 0) &&
				bx > 8 && bx < NES_SRC_W - 8 &&
				by > 20 && by < SCREEN_YRES;

			ab->latch_x = gfx_nx(bx);
			ab->latch_y = by;
			ab->latch_hit = dh && hitstun == 0 && !falling;
		} else {
			ab->latch_x = tx;
			ab->latch_y = ty;
			ab->latch_hit = 0;
		}

		/* Same-depth packets are LIFO, so queue the menu before the field. */
		if (state == ST_PAUSE)
			draw_pause_menu(&ctx, ab->ot, &ctx.next_packet, pause_sel);

#ifdef HUNTPS1_EXPERIMENTAL_ZAPPER_FLASH
		if (state != ST_PLAY)
			shot_flash_length = 0;
		ab->flash_frame = shot_flash_length != 0;
		if (ab->flash_frame) {
			/* Same-depth insertion is LIFO: black executes before white. */
			if (shot_flash_frame == 1)
				draw_tile(&ctx, shot_flash_x, shot_flash_y, gfx_nw(32), 32,
					255, 255, 255, 0);
#ifdef HUNTPS1_GAME_B
			/* NES Game B leaves the second target up for the final miss frame. */
			else if (shot_flash_second && shot_flash_frame >= 2)
				draw_tile(&ctx, shot_flash_x2, shot_flash_y2, gfx_nw(32), 32,
					255, 255, 255, 0);
#endif
			draw_tile(&ctx, 0, 0, SCREEN_XRES, SCREEN_YRES, 0, 0, 0, 0);
			if (++shot_flash_frame == shot_flash_length)
				shot_flash_length = 0;
		} else
#endif
#ifdef HUNTPS1_GAME_C
		if (state == ST_CLAY || (state == ST_PAUSE && pause_from == ST_CLAY)) {
			const HuntClayFlight *white = 0;
			ab->flash_frame = state == ST_CLAY && clay_flash_count != 0;
			if (ab->flash_frame) {
				int target = clay_scan[clay_flash_index++];
				if (target >= 0)
					white = &clay_flash_target[target];
				clay_flash_count--;
			}
			hunt_clay_draw(ab->ot, &ctx.next_packet, ab->buffer + BUFFER_LEN,
				&g_clay, ab->flash_frame, white);
		} else
#endif
#ifdef HUNTPS1_DELUXE_MENU
		if (state == ST_DELUXE_PLAY || (state == ST_PAUSE && pause_from == ST_DELUXE_PLAY)) {
#ifndef HUNTPS1_WOODS_INTEGRATED
			if (deluxe_sel == DELUXE_WOODS)
				deluxe_woods_draw(ab->ot, &ctx.next_packet, ab->buffer + BUFFER_LEN,
					(unsigned)g_deluxe_match.elapsed, 0);
			else
#endif
				gfx_draw_field_back(ab->ot, &ctx.next_packet, 3);
			for (i = g_deluxe_match.count - 1; i >= 0; i--) {
				const HuntPairDuck *duck = &g_deluxe_match.ducks[i].flight.duck[0];
				if (duck->phase >= PAIR_FLY && duck->phase <= PAIR_FALL)
					gfx_draw_duck(ab->ot, &ctx.next_packet, 2, duck->x,
						duck->y, duck->frame, duck->facing ^ (g_deluxe_match.ducks[i].reflection & 1), duck->color, 1);
			}
			if (deluxe_sel != DELUXE_WOODS) {
				gfx_draw_field_front(ab->ot, &ctx.next_packet, 1);
				draw_deluxe_hits(&ctx, ab->ot, &ctx.next_packet, &g_deluxe_match);
			}
			for (i = 0; deluxe_sel == DELUXE_WOODS && i < g_deluxe_match.players; i++) {
				const DeluxePlayer *p = &g_deluxe_match.player[i];
				int y = 200 + i * 16;
				sprintf(line, "P%d HITS %d   TEAM %d/%d", i + 1, p->hits,
					g_deluxe_match.player[0].hits + g_deluxe_match.player[1].hits,
					DELUXE_WOODS_DUCKS);
				draw_text(&ctx, 48, y, line);
				draw_tile(&ctx, 32, y, 8, 8, i ? 32 : 224, 48, i ? 240 : 48, 0);
				draw_tile(&ctx, 24, y - 2, 272, 12, 0, 0, 0, 0);
			}
			if (deluxe_missing)
				draw_text_c(&ctx, 48, g_deluxe_match.players == 2 ?
					"CONNECT BOTH GUNCONS" : "CONNECT A GUNCON");
#ifndef HUNTPS1_WOODS_INTEGRATED
		} else if (state == ST_DELUXE_RESULT && deluxe_sel == DELUXE_WOODS) {
			deluxe_woods_draw(ab->ot, &ctx.next_packet, ab->buffer + BUFFER_LEN,
				(unsigned)g_deluxe_match.elapsed, 1);
			draw_tile(&ctx, 40, 28, 240, 180, 0, 0, 0, 1);
			gfx_draw_dog(ab->ot, &ctx.next_packet, 1, 10, 195, FRAME_DOG_HOLD, 0);
#endif
		} else
#endif
		if (state == ST_TITLE || state == ST_NYI) {
			gfx_draw_title(ab->ot, &ctx.next_packet, 3);
#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
			draw_tile(&ctx, 96, 184, 88, 8, 0, 0, 0, 2);
#ifdef HUNTPS1_DELUXE_MENU
			gfx_draw_title_deluxe(ab->ot, &ctx.next_packet);
#endif
			gfx_draw_title_options(ab->ot, &ctx.next_packet);
#endif
			{
				int rows[] = { 17, 19, 21, 23, 23 };
				int cx = (menu_sel == 4 ? 16 : 6) * 8;
				int cy = rows[menu_sel] * 8;

				gfx_draw_title_cursor(ab->ot, &ctx.next_packet, menu_sel == 4 ? 1 : 2, cx, cy);
			}
			if (state == ST_NYI) {
				draw_tile(&ctx, 48, 96, 224, 40, 0, 0, 0, 1);
				draw_text(&ctx, 64, 108,
					nyi_which == 1 ? "GAME B NOT IMPLEMENTED" : "GAME C NOT IMPLEMENTED");
			}
			if (state == ST_TITLE) {
				int tw = TOP_SCORE_DIGITS;
				int tx0 = TOP_SCORE_COL;

				if (top_score > 99999) {
					tw = 6;
					tx0 = TOP_SCORE_COL - 1;
				}
				draw_score_digits(ab->ot, &ctx.next_packet, top_score,
					tx0 * 8, TOP_SCORE_ROW * 8, tw);
#ifdef HUNTPS1_DEBUG_OVERLAYS
				if (mode_hint > 0) {
					draw_tile(&ctx, 80, 88, 160, 24, 0, 0, 0, 1);
					draw_text_c(&ctx, 92, gfx_wide() ? "WIDE MODE" : "256 MODE");
				}
#endif
			}
		} else if (state == ST_INTRO || state == ST_PLAY || state == ST_ROUND ||
			state == ST_OVER || state == ST_PAUSE || state == ST_TALLY) {
			int show_duck = (state == ST_PLAY && dog_show == 0 && dog_phase != 98
				&& (respawn == 0 || (dog_phase == 99 && respawn == DUCK_RETRIEVE_DELAY)));

			grass_over = (state == ST_INTRO || (state == ST_PAUSE && pause_from == ST_INTRO))
				? intro_grass_over
				: hunt_dog_grass_over(0, dog_phase, dog_timer);
			gfx_draw_field_back(ab->ot, &ctx.next_packet, 3);
#ifdef HUNTPS1_GAME_B
			if (game_b && pair_started && state == ST_PLAY) {
				/* Lower OAM index is in front. The OT is LIFO. */
				for (i = 0; i < 2; i++) {
					const HuntPairDuck *duck = &g_pair.duck[i];
					if (duck->phase >= PAIR_FLY && duck->phase <= PAIR_FALL)
						gfx_draw_duck(ab->ot, &ctx.next_packet, 2, duck->x,
							duck->y + (duck->phase != PAIR_FLY), duck->frame,
							duck->facing, duck->color, 1);
				}
				for (i = 0; i < 2; i++) {
					const HuntPairDuck *duck = &g_pair.duck[i];
					if (duck->popup_left)
						gfx_draw_duck_score(ab->ot, &ctx.next_packet,
							duck->popup_tile, duck->popup_x, duck->popup_y);
				}
			}
			if (game_b)
				show_duck = 0;
#endif
			if (show_duck)
				/* Post-hit cels use the captured NES OAM top +1 PPU
				 * scanline. Flight registration and its hit latch stay intact. */
				gfx_draw_duck(ab->ot, &ctx.next_packet, 2, bx,
					by + (frame >= FRAME_HIT && frame <= FRAME_FALL0 + 1), frame,
					facing, dtype, 1);
			if (popup_left > 0 && (state == ST_PLAY ||
					(state == ST_PAUSE && pause_from == ST_PLAY)))
				gfx_draw_duck_score(ab->ot, &ctx.next_packet,
					popup_tile, popup_x, popup_y);
			if (dog_show && grass_over)
				gfx_draw_dog(ab->ot, &ctx.next_packet, 2, dog_x, dog_y,
					dog_frame, dog_face);

			gfx_draw_field_front(ab->ot, &ctx.next_packet, 1);
			if (state == ST_PLAY || state == ST_ROUND || state == ST_INTRO ||
				state == ST_PAUSE || state == ST_TALLY || state == ST_OVER)
				draw_game_hud(&ctx, ab->ot, &ctx.next_packet, shots, score,
					round_n, hit_mark, birds_done, flash);
			/* NES shows ROUND n in the sky during early intro (frame 137)
			 * and between rounds. Gone by the grass walk (frame 501). */
			if (state == ST_ROUND ||
				(state == ST_INTRO && dog_phase <= DOGPH_SNIFF))
				draw_round_banner(ab->ot, &ctx.next_packet, round_n);
			if (state == ST_OVER)
				gfx_draw_game_banner(ab->ot, &ctx.next_packet, 0, 1);
			if (dog_show && !grass_over) {
				if ((state == ST_INTRO || (state == ST_PAUSE && pause_from == ST_INTRO)) &&
					(dog_phase == DOGPH_WALK1 || dog_phase == DOGPH_WALK2) &&
					dog_frame >= 0x0C && dog_frame <= 0x0F) {
					/* Same indexed pixels as the atlas, registered to its
					 * recorded anchor; retain the established walk tpage. */
					gfx_draw_dog_walk(ab->ot, &ctx.next_packet, 0, dog_x + 4,
						dog_y + 28 - hunt_dog_hot_y[dog_frame - 0x0C],
						dog_frame - 0x0C, dog_face);
				} else if (state != ST_INTRO && !(state == ST_PAUSE && pause_from == ST_INTRO) &&
					(dog_phase == DOGPH_WALK1 || dog_phase == DOGPH_WALK2))
					gfx_draw_dog_walk(ab->ot, &ctx.next_packet, 0, dog_x,
						dog_y, dog_frame, dog_face);
				else
					gfx_draw_dog(ab->ot, &ctx.next_packet, 0, dog_x, dog_y,
						dog_frame, dog_face);
			}
#ifdef HUNTPS1_DEBUG_OVERLAYS
			/* Keep bark diagnostic visible after intro, so a missed
			 * jump event can be distinguished from silent SPU playback. */
			{
				char a0[40], a1[40];
				hunt_sfx_diag_lines(a0, a1, (int)sizeof(a0));
				draw_text(&ctx, 24, 32, a1);
			}
#endif
		} else if (state == ST_CAL || state == ST_WAIT) {
			draw_tile(&ctx, 0, 0, SCREEN_XRES, SCREEN_YRES, 0, 0, 0, 3);
			if (state != ST_WAIT)
				draw_cal_target(&ctx, tx, ty);
		} else {
			draw_tile(&ctx, 0, 0, SCREEN_XRES, SCREEN_YRES, 0, 0, 0, 3);
		}

#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
		if (state == ST_OPTIONS) {
			draw_text_c(&ctx, 48, "OPTIONS");
			gfx_draw_title_cursor(ab->ot, &ctx.next_packet, 0,
				24, 88 + options_sel * 24);
			draw_text(&ctx, 72, 88, "GUNCON CALIBRATION");
			draw_text(&ctx, 72, 112, g_zapper_flash ? "ZAPPER FLASH: ON" : "ZAPPER FLASH: OFF");
			draw_text(&ctx, 72, 136, "BACK");
			if (flash_save_status >= 0)
				draw_text_c(&ctx, 168, flash_save_status == CAL_SAVE_WRITTEN ?
					"FLASH SAVED" : "FLASH RAM ONLY");
#ifdef HUNTPS1_DELUXE_MENU
		} else if (state == ST_DELUXE) {
			gfx_draw_deluxe_menu(ab->ot, &ctx.next_packet, deluxe_sel);
		} else if (state == ST_DELUXE_PLAYERS) {
			gfx_draw_deluxe_players(ab->ot, &ctx.next_packet, deluxe_sel,
				player_sel, deluxe_population);
		} else if (state == ST_DELUXE_RESULT) {
			const DeluxePlayer *p = g_deluxe_match.player;
			if (deluxe_sel != DELUXE_WOODS) {
				int shootout = deluxe_sel == DELUXE_SHOOTOUT;
				int p1_result = p[0].hits;
				int p2_result = p[1].hits;
				const char *outcome = g_deluxe_match.players == 2 ?
					(p1_result == p2_result ? "TIE" :
					 p1_result > p2_result ? "PLAYER 1 WINS" : "PLAYER 2 WINS") :
					(shootout ? "CHALLENGE COMPLETE" : p[0].hits == 10 ? "PERFECT" :
					 p[0].hits >= 6 ? "SHARPSHOOTER" : "MATCH COMPLETE");
				gfx_draw_field_back(ab->ot, &ctx.next_packet, 2);
				gfx_draw_field_front(ab->ot, &ctx.next_packet, 2);
				draw_deluxe_hits(&ctx, ab->ot, &ctx.next_packet, &g_deluxe_match);
				gfx_draw_deluxe_result_text(ab->ot, &ctx.next_packet, shootout ? "SHOOTOUT" : "DUCK DUEL", 57, 0);
				gfx_draw_deluxe_result_text(ab->ot, &ctx.next_packet, outcome, 73, 0);
				for (i = 0; i < g_deluxe_match.players; i++) {
					sprintf(line, "P%d  %d HITS  %d SHOTS", i + 1, p[i].hits, p[i].shots);
					gfx_draw_deluxe_result_text(ab->ot, &ctx.next_packet, line, 94 + i * 12, shootout ? i + 1 : 0);
				}
				if (shootout)
					sprintf(line, "TIME %d SECONDS", g_deluxe_match.elapsed / 60);
				else
					sprintf(line, "ESCAPED %d", g_deluxe_match.escaped);
				gfx_draw_deluxe_result_text(ab->ot, &ctx.next_packet, line, 122, 0);
				gfx_draw_deluxe_result_text(ab->ot, &ctx.next_packet, "TRIGGER  BACK", 140, 0);
				draw_tile(&ctx, 54, 49, 212, 106, 0, 0, 0, 1);
				draw_tile(&ctx, 52, 47, 216, 110, 88, 117, 23, 1);
			} else {
				draw_text_c(&ctx, 40, "RESULTS");
			if (deluxe_sel == DELUXE_WOODS) {
				int hits = p[0].hits + p[1].hits;
				sprintf(line, "TEAM %d/%d  %s", hits, DELUXE_WOODS_DUCKS,
					hits == DELUXE_WOODS_DUCKS ? "PERFECT" : "COMPLETE");
				draw_text_c(&ctx, 64, line);
			} else if (g_deluxe_match.players == 2)
				draw_text_c(&ctx, 64, p[0].score == p[1].score ? "TIE" :
					p[0].score > p[1].score ? "PLAYER 1 WINS" : "PLAYER 2 WINS");
			else
				draw_text_c(&ctx, 64, deluxe_sel == DELUXE_SHOOTOUT ? "CHALLENGE COMPLETE" :
					p[0].hits == 10 ? "PERFECT" : p[0].hits >= 6 ? "SHARPSHOOTER" : "MATCH COMPLETE");
			for (i = 0; i < g_deluxe_match.players; i++) {
				sprintf(line, "P%d SCORE %05d  HITS %d", i + 1, p[i].score, p[i].hits);
				draw_text_c(&ctx, 88 + i * 32, line);
				sprintf(line, "ACCURACY %d%%", p[i].shots ? p[i].hits * 100 / p[i].shots : 0);
				draw_text_c(&ctx, 100 + i * 32, line);
			}
			if (deluxe_sel != DELUXE_SHOOTOUT) {
				sprintf(line, "ESCAPED %d", g_deluxe_match.escaped);
				draw_text_c(&ctx, 160, line);
			} else {
				sprintf(line, "TIME %d SECONDS", g_deluxe_match.elapsed / 60);
				draw_text_c(&ctx, 160, line);
			}
			draw_text_c(&ctx, 192, "TRIGGER  BACK");
			}
#endif
		} else
#endif
		if (state == ST_NEEDCAL) {
			draw_text_c(&ctx, 72, "SET UP YOUR GUN");
			draw_text_c(&ctx, 96, "CALIBRATE BEFORE PLAY");
			draw_text_c(&ctx, 128, "TRIGGER  CALIBRATE");
			draw_text_c(&ctx, 148, "B  BACK");
		} else if (state == ST_WELCOME) {
			draw_cal_panel(&ctx);
			draw_cal_label(&ctx, ab->ot, &ctx.next_packet, 48, "GUN CALIBRATION");
			draw_cal_label(&ctx, ab->ot, &ctx.next_packet, 72, "AIM AT THE ORANGE CENTER");
			draw_cal_label(&ctx, ab->ot, &ctx.next_packet, 92, "FIRE THREE TIMES");
			draw_cal_label(&ctx, ab->ot, &ctx.next_packet, 120, "TRIGGER  BEGIN");
			draw_text_c(&ctx, 140, "B  BACK");
		} else if (state == ST_CAL) {
			sprintf(line, "TARGET %d OF %d  SHOTS %d OF %d",
				pt + 1, GUNCAL_POINTS, g_cal.pt[pt].n, cal_shots);
			draw_cal_label(&ctx, ab->ot, &ctx.next_packet, hunt_cal_status_y(), line);
			if (fb > 0 && !fb_ok)
				draw_text_c(&ctx, 216, "SHOT NOT DETECTED");
			else if (fb > 0 && fb_ok)
				draw_text_c(&ctx, 216, "SHOT SAVED");
		} else if (state == ST_WAIT) {
			draw_text_c(&ctx, 48, "TARGET SAVED");
			draw_text_c(&ctx, 72, "GET READY");
		} else if (state == ST_DONE) {
			draw_cal_panel(&ctx);
			draw_cal_label(&ctx, ab->ot, &ctx.next_packet, 64, "CALIBRATION COMPLETE");
			draw_cal_label(&ctx, ab->ot, &ctx.next_packet, 88, "YOUR GUN IS READY");
			draw_text_c(&ctx, 104, hunt_cal_save_status(g_cal_card_status));
			draw_cal_label(&ctx, ab->ot, &ctx.next_packet, 136, "TRIGGER  TITLE");
#ifdef HUNTPS1_DIAG_SKIPCAL
		} else if (state == ST_SOUND) {
			draw_text_c(&ctx, 24, "AUDIO COMPARE");
			draw_text_c(&ctx, 40, shot_lab);
			if (hunt_spu_noise_shot_busy()) {
				sprintf(noise_lab, "NOISE S%d/48 F%d",
					hunt_spu_noise_shot_step() + 1,
					hunt_spu_noise_shot_frames());
				draw_text_c(&ctx, 56, noise_lab);
			} else {
				draw_text_c(&ctx, 56,
					sound_played ? "CUE PLAYED" : "CUE WAIT");
			}
			draw_text_c(&ctx, 76, "UP        ROM ATTACK");
			draw_text_c(&ctx, 92, "DOWN      ROM FULL");
			draw_text_c(&ctx, 108, "SQUARE+UP SPU NOISE");
			draw_text_c(&ctx, 124, "CIRCLE    OLD CAPTURE");
			draw_text_c(&ctx, 140, "TRIANGLE  V14 SHOT");
			draw_text_c(&ctx, 156, "SELECT    REPEAT ATTACK");
			draw_text_c(&ctx, 172, "L1 TITLE  R1 ROUND");
			draw_text_c(&ctx, 188, "SQUARE BACK");
#endif
		}

#ifdef HUNTPS1_EMU_TEST
		/*
		 * Visual telemetry fallback: DuckStation's redirected stdout/stderr
		 * do not currently carry PS1 BIOS TTY. Render a small diagnostic
		 * overlay in the emulator-only build so the timed screenshots still
		 * expose actual game-state measurements. This is NOT a continuous
		 * frame trace; screenshots are only sparse checkpoints.
		 */
		{
			char trace_line[80];
			sprintf(trace_line, "F%04d ST%d R%d B%d", emu_f, state,
				round_n, birds_done);
			draw_text_c(&ctx, 200, trace_line);
			sprintf(trace_line, "X%d Y%d P%d S%d", bx, by, nes_pat, shots);
			draw_text_c(&ctx, 216, trace_line);
		}
#ifdef HUNTPS1_TTY_TRACE
		/*
		 * Optional high-volume BIOS TTY diagnostics. DuckStation's current
		 * CI process streams do not expose this output; leave disabled by
		 * default to avoid printf overhead changing flight timing.
		 */
		/*
		 * Emulator-only, frame-indexed state trace. No game logic, input,
		 * calibration or displayed-frame GunCon latch is modified.
		 * Capture the PS1 TTY stream when supported by the CI emulator.
		 * Frame zero is the first main-loop iteration after initialization.
		 */
		{
			static int trace_previous_state = -1;
			static int trace_previous_bird = -1;
			static int trace_previous_shots = -1;
			int trace_bird = birds_done;
			if (state != trace_previous_state) {
				printf("HUNT_FRAME event=state frame=%d from=%d to=%d round=%d bird=%d\n",
					emu_f, trace_previous_state, state, round_n, trace_bird);
				trace_previous_state = state;
			}
			if (state == ST_PLAY) {
				if (trace_previous_bird != trace_bird) {
					printf("HUNT_FRAME event=bird frame=%d round=%d bird=%d x=%d y=%d pattern=%d\n",
						emu_f, round_n, trace_bird, bx, by, nes_pat);
					trace_previous_bird = trace_bird;
				}
				printf("HUNT_FRAME event=flight frame=%d round=%d bird=%d x=%d y=%d sx=%d sy=%d facing=%d anim=%d pattern=%d shots=%d falling=%d escaping=%d\n",
					emu_f, round_n, trace_bird, bx, by, sx, sy, facing,
					frame, nes_pat, shots, falling, escaping);
				if (trace_previous_shots >= 0 && shots < trace_previous_shots)
					printf("HUNT_FRAME event=shot frame=%d bird=%d shots_before=%d shots_after=%d falling=%d score=%d\n",
						emu_f, trace_bird, trace_previous_shots, shots, falling, score);
				trace_previous_shots = shots;
			} else {
				trace_previous_bird = -1;
				trace_previous_shots = -1;
			}
		}
#endif /* HUNTPS1_TTY_TRACE */
#endif
		flip_buffers(&ctx);
#ifdef HUNTPS1_DELUXE_MENU
		if (state == ST_DELUXE_PLAY && !deluxe_missing) {
			unsigned now = (unsigned)VSync(-1);
			if (deluxe_last_vblank) {
				unsigned gap = now - deluxe_last_vblank;
				deluxe_perf[0]++;
				if (gap > 1)
					deluxe_perf[1]++;
				if (gap > deluxe_perf[2])
					deluxe_perf[2] = gap;
			}
			deluxe_last_vblank = now;
		} else {
			deluxe_last_vblank = 0;
		}
#endif
	}

	return 0;
}
