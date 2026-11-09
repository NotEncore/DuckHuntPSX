/*
 * HuntPS1 textures come from romconv --ps1pack (local ROM).
 *
 * Diagonal sky seam: a single 256x240 POLY_FT4 is two affine triangles.
 * The PS1 GPU does not do perspective-correct UVs, so a large quad shows a
 * diagonal discontinuity. Backgrounds are 32x8 quads (NES tile rows).
 * Wide 16px strips still warped title letter tops; 32x8 keeps affine span small.
 *
 * Horizontal 5/4 scale is GPU XY only; NES UVs and source art stay 256-wide.
 */

#include "gfx.h"
#ifdef HUNTPS1_DELUXE_MENU
#include "deluxe_ducks.h"
#endif
#include <string.h>
#include "generated/game_a_banners.h"
#include "generated/hunt_score_sprites.h"

#define HUNT_SCORE_TPAGE_X 576
#define HUNT_SCORE_TPAGE_Y 256

static int g_wide = 1;
static int g_scale_n = 5;
static int g_scale_d = 4;
static uint8_t *g_prim_limit;
static uint8_t g_prim_discard[64];
#define TITLE_UI_CLUT_X 80
#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
#define TITLE_OPTIONS_CLUT_X 144
static const uint16_t title_options_clut[16] __attribute__((aligned(4))) = {
	[4] = 0x923a, [5] = 0x8000
};
#ifdef HUNTPS1_DELUXE_MENU
#define SHOOTOUT_P1_CLUT_X 160
#define SHOOTOUT_P2_CLUT_X 176
static const uint16_t shootout_p1_clut[16] __attribute__((aligned(4))) = { [4] = 0x8c9f, [5] = 0x8000 };
static const uint16_t shootout_p2_clut[16] __attribute__((aligned(4))) = { [4] = 0xff0c, [5] = 0x8000 };
static const char title_extra_keys[] = "CFGHKLWXBY";
static const uint32_t title_extra_letters[8][10] = {
	{0x00444440,0x04444444,0x00444440,0x04400044,0x04400044,0x00000440,0x04400044,0x04400440,0x00444444,0x04400044},
	{0x04400044,0x00000044,0x04400044,0x04400044,0x00440044,0x00000440,0x04400044,0x00444400,0x04400044,0x04400044},
	{0x00000044,0x00000044,0x00000044,0x04400044,0x00044044,0x00000440,0x04400044,0x00044000,0x04400044,0x00444440},
	{0x00000044,0x00444444,0x04444044,0x04444444,0x00004444,0x00000440,0x04404044,0x00044000,0x00444444,0x00044000},
	{0x00000044,0x00000044,0x04400044,0x04400044,0x00044044,0x00000440,0x04444444,0x00444400,0x04400044,0x00044000},
	{0x04400044,0x00000044,0x04400044,0x04400044,0x00440044,0x04444440,0x04440444,0x04400440,0x04400044,0x00044000},
	{0x00444440,0x00000044,0x00444440,0x04400044,0x04400044,0x04444440,0x04400044,0x04400440,0x00444444,0x00044000},
	{0},
};
#endif
#endif
#define GAME_HUD_RED_CLUT_X 96
#define GAME_HUD_ROUND_CLUT_X 112
#define GAME_HUD_BANNER_CLUT_X 128
#define GAME_HUD_Y 192
#define GAME_HUD_HEIGHT (NES_SRC_H - GAME_HUD_Y)
#define GAME_HUD_HALF_W 160
#define GAME_HUD_ATLAS_W 192
#define GAME_HUD_ATLAS_V 8
#define GAME_HUD_GLYPH_V (GAME_HUD_ATLAS_V + 2 * GAME_HUD_HEIGHT)
#define GAME_HUD_GLYPHS_PER_ROW 16
#define GAME_HUD_BANNER_V (GAME_HUD_GLYPH_V + 16)
#define GAME_HUD_NATIVE_BAR_U 150
static uint16_t game_hud_wide[(GAME_HUD_ATLAS_W / 4) *
	(2 * GAME_HUD_HEIGHT + 16)] __attribute__((aligned(4)));
static uint16_t game_hud_red_clut[16] __attribute__((aligned(4)));
static uint16_t game_hud_round_clut[16] __attribute__((aligned(4)));
static uint16_t game_hud_banner_clut[16] __attribute__((aligned(4)));
/* Cursor texel 3 uses the live NES menu's white palette. HUD digit
 * texel 4 uses the title score's green, with opaque black at 5. */
static const uint16_t title_ui_clut[16] __attribute__((aligned(4))) = {
	[3] = 0xf7bd, [4] = 0x9349, [5] = 0x8000
};

void gfx_set_prim_limit(uint8_t *end)
{
	g_prim_limit = end;
}

static void *next_prim(uint8_t **packet, uint32_t *ot, int z, size_t size)
{
	uint8_t *prim = *packet;

	if (g_prim_limit && prim + size > g_prim_limit)
		return g_prim_discard;
	addPrim(&ot[z], prim);
	*packet += size;
	return prim;
}

void gfx_set_wide(int on)
{
	g_wide = on ? 1 : 0;
}

int gfx_wide(void)
{
	return g_wide;
}

int gfx_nx(int nes_x)
{
	if (g_wide)
		return (nes_x * g_scale_n) / g_scale_d;
	return 32 + nes_x;
}

int gfx_nw(int nes_w)
{
	if (g_wide)
		return (nes_w * g_scale_n) / g_scale_d;
	return nes_w;
}

static void draw_strip_ft4(uint32_t *ot, uint8_t **packet, int z,
	int tpage_x, int tpage_y, int clut_x, int clut_y,
	int u0, int v0, int u1, int v1)
{
	POLY_FT4 *p = next_prim(packet, ot, z, sizeof(POLY_FT4));
	int x0 = gfx_nx(u0);
	int x1 = gfx_nx(u1 + 1);
	int y0 = v0;
	int y1 = v1 + 1;

	setPolyFT4(p);
	setRGB0(p, 128, 128, 128);
	setShadeTex(p, 1);
	setXY4(p, x0, y0, x1, y0, x0, y1, x1, y1);
	setUV4(p, (uint8_t)u0, (uint8_t)v0, (uint8_t)u1, (uint8_t)v0,
		(uint8_t)u0, (uint8_t)v1, (uint8_t)u1, (uint8_t)v1);
	setClut(p, clut_x, clut_y);
	p->tpage = getTPage(0, 0, tpage_x, tpage_y);
}

static void draw_bg_range(uint32_t *ot, uint8_t **packet, int z,
	int tpage_x, int tpage_y, int clut_x, int clut_y, int y0, int y1,
	int strip_w)
{
	DR_TPAGE *tp;
	int y, x;

	if (strip_w < 8)
		strip_w = 8;
	for (y = y0; y < y1; y += GFX_STRIP_H) {
		int v1 = y + GFX_STRIP_H - 1;

		if (v1 >= y1)
			v1 = y1 - 1;
		for (x = 0; x < NES_SRC_W; x += strip_w) {
			int u1 = x + strip_w - 1;

			if (u1 >= NES_SRC_W)
				u1 = NES_SRC_W - 1;
			draw_strip_ft4(ot, packet, z, tpage_x, tpage_y, clut_x, clut_y,
				x, y, u1, v1);
		}
	}
	/* One DR_TPAGE after the quads so the GPU sees it first (addPrim is LIFO). */
	tp = next_prim(packet, ot, z, sizeof(DR_TPAGE));
	setDrawTPage(tp, 1, 0, getTPage(0, 0, tpage_x, tpage_y));
}

static unsigned int texel4(const uint16_t *tex, int stride, int x, int y)
{
	return (tex[y * stride + x / 4] >> (4 * (x & 3))) & 15;
}

static void put_game_hud_texel(int x, int y, unsigned int index)
{
	int shift = 4 * (x & 3);
	uint16_t *word = &game_hud_wide[y * (GAME_HUD_ATLAS_W / 4) + x / 4];
	*word = (*word & ~(15 << shift)) | (index << shift);
}

static unsigned int game_hud_texel(int tile, int x, int y)
{
	/* The reference's $D7 is four cyan 1x5 bars. The frozen atlas
	 * copied a blank requirement slot, so restore its measured mask. */
	if (tile == HUNT_HUD_WDUCK_B)
		return !(x & 1) && y >= 2 && y <= 6 ? 0 : 5;
	return texel4(hunt_hud_tex, HUNT_HUD_W / 4, tile * 8 + x, y);
}

static unsigned int game_hud_wide_texel(int tile, int x, int y)
{
	/* Keep the NES duck silhouette at native width inside its 10-dot
	 * slot. Opaque gutters keep adjacent bright ducks distinct on CRT.
	 * The red marker uses this same silhouette with a different CLUT. */
	if (tile == HUNT_HUD_WDUCK_T)
		return x == 0 || x == 9 ? 5 : game_hud_texel(tile, x - 1, y);
	return game_hud_texel(tile, x * 4 / 5, y);
}

static void upload_game_hud(void)
{
	RECT rect;
	int half, x, y, tile, i;

	/* Keep the normal 5:4 coordinate transform, but expand the HUD
	 * indices before upload so the GPU samples each destination pixel
	 * exactly once. Two halves keep the texture inside one 4bpp page. */
	for (half = 0; half < 2; half++)
		for (y = 0; y < GAME_HUD_HEIGHT; y++)
			for (x = 0; x < GAME_HUD_HALF_W; x++)
				put_game_hud_texel(x, y + half * GAME_HUD_HEIGHT,
					texel4(hunt_bg_tex, HUNT_BG_W / 4,
						half * (NES_SRC_W / 2) + x * 4 / 5,
						GAME_HUD_Y + y));
	for (tile = 0; tile < HUNT_HUD_W / 8; tile++)
		for (y = 0; y < 8; y++)
			for (x = 0; x < 10; x++)
				put_game_hud_texel((tile % GAME_HUD_GLYPHS_PER_ROW) * 10 + x,
					2 * GAME_HUD_HEIGHT + (tile / GAME_HUD_GLYPHS_PER_ROW) * 8 + y,
					game_hud_wide_texel(tile, x, y));
	/* Slot 31 is unused; store the native $D7 variant there. */
	for (y = 0; y < 8; y++)
		for (x = 0; x < 8; x++)
			put_game_hud_texel(GAME_HUD_NATIVE_BAR_U + x, 2 * GAME_HUD_HEIGHT + 8 + y,
				game_hud_texel(HUNT_HUD_WDUCK_B, x, y));
	setRECT(&rect, HUNT_HUD_TPAGE_X, HUNT_HUD_TPAGE_Y + GAME_HUD_ATLAS_V,
		GAME_HUD_ATLAS_W / 4, 2 * GAME_HUD_HEIGHT + 16);
	LoadImage(&rect, (const uint32_t *)game_hud_wide);
	for (i = 0; i < 16; i++) {
		game_hud_red_clut[i] = hunt_bg_clut[i];
		game_hud_round_clut[i] = hunt_bg_clut[i];
	}
	/* Live NES $D5 and $D6 have identical masks, using palette colors
	 * $16 and $30 respectively. The round digits use $29, not $30. */
	game_hud_red_clut[4] = 0x90d6;
	game_hud_round_clut[4] = 0x8351;
	setRECT(&rect, GAME_HUD_RED_CLUT_X, HUNT_BG_CLUT_Y, 16, 1);
	LoadImage(&rect, (const uint32_t *)game_hud_red_clut);
	setRECT(&rect, GAME_HUD_ROUND_CLUT_X, HUNT_BG_CLUT_Y, 16, 1);
	LoadImage(&rect, (const uint32_t *)game_hud_round_clut);
	game_hud_banner_clut[4] = hunt_bg_clut[4];
	game_hud_banner_clut[5] = hunt_bg_clut[5];
	setRECT(&rect, GAME_HUD_BANNER_CLUT_X, HUNT_BG_CLUT_Y, 16, 1);
	LoadImage(&rect, (const uint32_t *)game_hud_banner_clut);
	setRECT(&rect, HUNT_HUD_TPAGE_X, HUNT_HUD_TPAGE_Y + GAME_HUD_BANNER_V,
		32, 132);
	LoadImage(&rect, (const uint32_t *)game_banner_tex);
}

void gfx_upload(void)
{
	RECT rect;
	/* Duck atlas indices 2, 3 and 4 are body, primary head and
	 * secondary detail, respectively. Dog CLUT stays separate. */
	static const uint16_t duck_black[16] __attribute__((aligned(4))) = {
		0, 0xf7bd, 0x8000, 0x8100, 0xb1bd
	};
	static const uint16_t duck_red[16] __attribute__((aligned(4))) = {
		0, 0xf7bd, 0x980b, 0x8000, 0xb1bd
	};

	setRECT(&rect, HUNT_BG_CLUT_X, HUNT_BG_CLUT_Y, 16, 1);
	LoadImage(&rect, (uint32_t *)hunt_bg_clut);
	setRECT(&rect, TITLE_BG_CLUT_X, TITLE_BG_CLUT_Y, 16, 1);
	LoadImage(&rect, (uint32_t *)hunt_title_clut);
	setRECT(&rect, TITLE_UI_CLUT_X, TITLE_BG_CLUT_Y, 16, 1);
	LoadImage(&rect, (const uint32_t *)title_ui_clut);
#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
	setRECT(&rect, TITLE_OPTIONS_CLUT_X, TITLE_BG_CLUT_Y, 16, 1);
	LoadImage(&rect, (const uint32_t *)title_options_clut);
#ifdef HUNTPS1_DELUXE_MENU
	setRECT(&rect, SHOOTOUT_P1_CLUT_X, TITLE_BG_CLUT_Y, 16, 1);
	LoadImage(&rect, (const uint32_t *)shootout_p1_clut);
	setRECT(&rect, SHOOTOUT_P2_CLUT_X, TITLE_BG_CLUT_Y, 16, 1);
	LoadImage(&rect, (const uint32_t *)shootout_p2_clut);
	setRECT(&rect, HUNT_CURSOR_TPAGE_X, HUNT_CURSOR_TPAGE_Y + 8, 20, 8);
	LoadImage(&rect, &title_extra_letters[0][0]);
#endif
#endif
	setRECT(&rect, HUNT_SPR_CLUT_X, HUNT_SPR_CLUT_Y, 16, 1);
	LoadImage(&rect, (uint32_t *)hunt_spr_clut);
	setRECT(&rect, HUNT_SPR_BLUE_X, HUNT_SPR_CLUT_Y, 16, 1);
	LoadImage(&rect, (const uint32_t *)duck_black);
	setRECT(&rect, HUNT_SPR_RED_X, HUNT_SPR_CLUT_Y, 16, 1);
	LoadImage(&rect, (const uint32_t *)duck_red);
	setRECT(&rect, HUNT_SCORE_TPAGE_X, HUNT_SCORE_TPAGE_Y, 64, 16);
	LoadImage(&rect, (const uint32_t *)hunt_score_tex);
	setRECT(&rect, HUNT_BG_TPAGE_X, HUNT_BG_TPAGE_Y, HUNT_BG_W / 4, HUNT_BG_H);
	LoadImage(&rect, (uint32_t *)hunt_bg_tex);
	setRECT(&rect, TITLE_BG_TPAGE_X, TITLE_BG_TPAGE_Y, HUNT_BG_W / 4, HUNT_BG_H);
	LoadImage(&rect, (uint32_t *)hunt_title_tex);
	setRECT(&rect, HUNT_DUCK_TPAGE_X, HUNT_DUCK_TPAGE_Y,
		HUNT_DUCK_ATLAS_W / 4, HUNT_DUCK_ATLAS_H);
	LoadImage(&rect, (uint32_t *)hunt_duck_tex);
	setRECT(&rect, HUNT_DOG_TPAGE_X, HUNT_DOG_TPAGE_Y,
		HUNT_DOG_ATLAS_W / 4, HUNT_DOG_ATLAS_H);
	LoadImage(&rect, (uint32_t *)hunt_dog_tex);
	setRECT(&rect, HUNT_CURSOR_TPAGE_X, HUNT_CURSOR_TPAGE_Y, 8 / 4, 8);
	LoadImage(&rect, (uint32_t *)hunt_cursor_tex);
	/* RECT.w is 16-bit VRAM units (4bpp pixels/4). Must stay a multiple of 16
	 * on real GPU DMA; v6 used 168px → w=42 and never left gfx_upload. */
	setRECT(&rect, HUNT_HUD_TPAGE_X, HUNT_HUD_TPAGE_Y, HUNT_HUD_W / 4, HUNT_HUD_H);
	LoadImage(&rect, (uint32_t *)hunt_hud_tex);
	upload_game_hud();
	setRECT(&rect, HUNT_WALK_TPAGE_X, HUNT_WALK_TPAGE_Y, 256 / 4, 48);
	/* The first 48 atlas rows are the four walk cells, byte for byte. */
	LoadImage(&rect, (const uint32_t *)hunt_dog_tex);
	DrawSync(0);
}

static void draw_tex_sprite(uint32_t *ot, uint8_t **packet, int z,
	int tpage_x, int tpage_y, int clut_x, int clut_y,
	int x, int y, int u, int v, int w, int h)
{
	SPRT *p = next_prim(packet, ot, z, sizeof(SPRT));
	DR_TPAGE *tp = next_prim(packet, ot, z, sizeof(DR_TPAGE));
	setDrawTPage(tp, 1, 0, getTPage(0, 0, tpage_x, tpage_y));
	setSprt(p);
	setShadeTex(p, 1);
	setXY0(p, x, y);
	setUV0(p, u, v);
	setWH(p, w, h);
	setClut(p, clut_x, clut_y);
}

static void draw_title_sprite(uint32_t *ot, uint8_t **packet, int z,
	int tpage_x, int tpage_y, int clut_x, int clut_y,
	int x, int y, int u, int v, int w, int h)
{
	draw_tex_sprite(ot, packet, z, tpage_x, tpage_y, clut_x, clut_y,
		(SCREEN_XRES - NES_SRC_W) / 2 + x, y, u, v, w, h);
}

void gfx_draw_duck_score(uint32_t *ot, uint8_t **packet, int tile, int x, int y)
{
	int w = gfx_nw(16);
	int v = gfx_wide() ? 8 : 0;
	int top = GRASS_SPLIT_Y - y;

	if (top < 0)
		top = 0;
	if (top > 8)
		top = 8;
	/* OAM 60/61 is behind the duck but has foreground priority over
	 * scenery. Keep that ordering across the port's grass split. */
	if (top)
		draw_tex_sprite(ot, packet, 2, HUNT_SCORE_TPAGE_X, HUNT_SCORE_TPAGE_Y,
			HUNT_SPR_CLUT_X, HUNT_SPR_CLUT_Y, gfx_nx(x), y, tile * w, v, w, top);
	if (top < 8)
		draw_tex_sprite(ot, packet, 0, HUNT_SCORE_TPAGE_X, HUNT_SCORE_TPAGE_Y,
			HUNT_SPR_CLUT_X, HUNT_SPR_CLUT_Y, gfx_nx(x), y + top,
			tile * w, v + top, w, 8 - top);
}

void gfx_draw_title(uint32_t *ot, uint8_t **packet, int z)
{
	/* Native 1:1 texel steps keep the tile edges and glyph gutters intact. */
	draw_title_sprite(ot, packet, z, TITLE_BG_TPAGE_X, TITLE_BG_TPAGE_Y,
		TITLE_BG_CLUT_X, TITLE_BG_CLUT_Y, 0, 0, 0, 0, NES_SRC_W, NES_SRC_H);
}

void gfx_draw_title_cursor(uint32_t *ot, uint8_t **packet, int z, int x, int y)
{
	draw_title_sprite(ot, packet, z, HUNT_CURSOR_TPAGE_X, HUNT_CURSOR_TPAGE_Y,
		TITLE_UI_CLUT_X, TITLE_BG_CLUT_Y, x, y, 0, 0, 8, 8);
}

void gfx_draw_title_digit(uint32_t *ot, uint8_t **packet, int z, int digit, int x, int y)
{
	draw_title_sprite(ot, packet, z, HUNT_HUD_TPAGE_X, HUNT_HUD_TPAGE_Y,
		TITLE_UI_CLUT_X, TITLE_BG_CLUT_Y, x, y, digit * 8, 0, 8, 8);
}

void gfx_draw_field_back(uint32_t *ot, uint8_t **packet, int z)
{
	draw_bg_range(ot, packet, z, HUNT_BG_TPAGE_X, HUNT_BG_TPAGE_Y,
		HUNT_BG_CLUT_X, HUNT_BG_CLUT_Y, 0, GRASS_SPLIT_Y, GFX_STRIP_FINE);
}

void gfx_draw_field_front(uint32_t *ot, uint8_t **packet, int z)
{
	draw_bg_range(ot, packet, z, HUNT_BG_TPAGE_X, HUNT_BG_TPAGE_Y,
		HUNT_BG_CLUT_X, HUNT_BG_CLUT_Y, GRASS_SPLIT_Y, GAME_HUD_Y, GFX_STRIP_FINE);
	if (gfx_wide()) {
		int half;
		for (half = 0; half < 2; half++)
			draw_tex_sprite(ot, packet, z, HUNT_HUD_TPAGE_X, HUNT_HUD_TPAGE_Y,
				HUNT_BG_CLUT_X, HUNT_BG_CLUT_Y, half * GAME_HUD_HALF_W,
				GAME_HUD_Y, 0, GAME_HUD_ATLAS_V + half * GAME_HUD_HEIGHT,
				GAME_HUD_HALF_W, GAME_HUD_HEIGHT);
	} else {
		draw_tex_sprite(ot, packet, z, HUNT_BG_TPAGE_X, HUNT_BG_TPAGE_Y,
			HUNT_BG_CLUT_X, HUNT_BG_CLUT_Y, gfx_nx(0), GAME_HUD_Y,
			0, GAME_HUD_Y, NES_SRC_W, GAME_HUD_HEIGHT);
	}
}

void gfx_draw_game_hud_tile(uint32_t *ot, uint8_t **packet, int z,
	int tile, int nes_x, int nes_y)
{
	int clut_x = HUNT_BG_CLUT_X;
	if (tile == HUNT_HUD_RDUCK_T) {
		tile = HUNT_HUD_WDUCK_T;
		clut_x = GAME_HUD_RED_CLUT_X;
	} else if (nes_y == HUD_ROUND_ROW * 8) {
		clut_x = GAME_HUD_ROUND_CLUT_X;
	}
	int u = tile * 8, v = 0;
	if (gfx_wide()) {
		u = (tile % GAME_HUD_GLYPHS_PER_ROW) * 10;
		v = GAME_HUD_GLYPH_V + (tile / GAME_HUD_GLYPHS_PER_ROW) * 8;
	} else if (tile == HUNT_HUD_WDUCK_B) {
		u = GAME_HUD_NATIVE_BAR_U;
		v = GAME_HUD_GLYPH_V + 8;
	}
	draw_tex_sprite(ot, packet, z, HUNT_HUD_TPAGE_X, HUNT_HUD_TPAGE_Y,
		clut_x, HUNT_BG_CLUT_Y, gfx_nx(nes_x), nes_y,
		u, v, gfx_nw(8), 8);
}

void gfx_draw_game_banner(uint32_t *ot, uint8_t **packet, int z, int over)
{
	int x = over ? 91 : 107, y = over ? 43 : 51;
	int w = over ? 81 : 49;
	int v = GAME_HUD_BANNER_V + (over ? 33 : 0) + (gfx_wide() ? 66 : 0);
	draw_tex_sprite(ot, packet, z, HUNT_HUD_TPAGE_X, HUNT_HUD_TPAGE_Y,
		GAME_HUD_BANNER_CLUT_X, HUNT_BG_CLUT_Y, gfx_nx(x), y,
		0, v, gfx_nx(x + w) - gfx_nx(x), 33);
}

int gfx_hud_dx(int panel)
{
	return hunt_hud_dx(panel);
}

void gfx_copy_hunt_bg(uint32_t *ot, uint8_t **packet, int z,
	int src_x, int src_y, int w, int h, int dst_x, int dst_y)
{
	DR_TPAGE *tp;
	int x, y, sw, sh, sx, sy, dx, dy;

	if (w < 1 || h < 1)
		return;
	/* 8px rows keep $E2/$E8 chrome from affine-thinning (32px quads
	 * warped top/bottom borders on CRT). Width 32 keeps OT budget. */
	for (y = 0; y < h; y += GFX_STRIP_H) {
		sh = h - y;
		if (sh > GFX_STRIP_H)
			sh = GFX_STRIP_H;
		sy = src_y + y;
		dy = dst_y + y;
		for (x = 0; x < w; x += GFX_STRIP_W) {
			POLY_FT4 *p;
			int x0, x1, y0, y1;
			uint8_t u0, u1, v0, v1;

			sw = w - x;
			if (sw > GFX_STRIP_W)
				sw = GFX_STRIP_W;
			sx = src_x + x;
			dx = dst_x + x;
			x0 = gfx_nx(dx);
			x1 = gfx_nx(dx + sw);
			y0 = dy;
			y1 = dy + sh;
			u0 = (uint8_t)sx;
			u1 = (uint8_t)(sx + sw - 1);
			v0 = (uint8_t)sy;
			v1 = (uint8_t)(sy + sh - 1);
			p = next_prim(packet, ot, z, sizeof(POLY_FT4));
			setPolyFT4(p);
			setRGB0(p, 128, 128, 128);
			setShadeTex(p, 1);
			setXY4(p, x0, y0, x1, y0, x0, y1, x1, y1);
			setUV4(p, u0, v0, u1, v0, u0, v1, u1, v1);
			setClut(p, HUNT_BG_CLUT_X, HUNT_BG_CLUT_Y);
			p->tpage = getTPage(0, 0, HUNT_BG_TPAGE_X, HUNT_BG_TPAGE_Y);
		}
	}
	tp = next_prim(packet, ot, z, sizeof(DR_TPAGE));
	setDrawTPage(tp, 1, 0, getTPage(0, 0, HUNT_BG_TPAGE_X, HUNT_BG_TPAGE_Y));
}

void gfx_draw_duck(uint32_t *ot, uint8_t **packet, int z,
	int cx, int cy, int frame, int facing_left, int color, int scale)
{
	POLY_FT4 *p = next_prim(packet, ot, z, sizeof(POLY_FT4));
	DR_TPAGE *tp = next_prim(packet, ot, z, sizeof(DR_TPAGE));
	int col, row, x0, y0, x1, y1, hw, hh, clut_x;
	uint8_t u0, u1, v0, v1;

	if (frame < 0)
		frame = 0;
	if (frame > 0x0B)
		frame = 0x0B;
	if (scale < 1)
		scale = 1;
	col = frame % HUNT_DUCK_COLS;
	row = frame / HUNT_DUCK_COLS;
	hw = (HUNT_DUCK_CELL_W * scale) / 2;
	hh = (HUNT_DUCK_CELL_H * scale) / 2;
	x0 = gfx_nx(cx - hw);
	y0 = cy - hh;
	x1 = gfx_nx(cx + hw);
	y1 = cy + hh;
	u0 = (uint8_t)(col * HUNT_DUCK_CELL_W);
	v0 = (uint8_t)(row * HUNT_DUCK_CELL_H);
	u1 = (uint8_t)(u0 + HUNT_DUCK_CELL_W - 1);
	v1 = (uint8_t)(v0 + HUNT_DUCK_CELL_H - 1);
	if (facing_left) {
		uint8_t t = u0;

		u0 = u1;
		u1 = t;
	}
	clut_x = HUNT_SPR_CLUT_X;
	if (color == DUCK_BLACK)
		clut_x = HUNT_SPR_BLUE_X;
	else if (color == DUCK_RED)
		clut_x = HUNT_SPR_RED_X;

	setDrawTPage(tp, 1, 0, getTPage(0, 0, HUNT_DUCK_TPAGE_X, HUNT_DUCK_TPAGE_Y));
	setPolyFT4(p);
	setRGB0(p, 128, 128, 128);
	setShadeTex(p, 1);
	setXY4(p, x0, y0, x1, y0, x0, y1, x1, y1);
	setUV4(p, u0, v0, u1, v0, u0, v1, u1, v1);
	setClut(p, clut_x, HUNT_SPR_CLUT_Y);
	p->tpage = getTPage(0, 0, HUNT_DUCK_TPAGE_X, HUNT_DUCK_TPAGE_Y);
}

void gfx_draw_dog(uint32_t *ot, uint8_t **packet, int z,
	int cx, int cy, int frame, int facing_left)
{
	POLY_FT4 *p = next_prim(packet, ot, z, sizeof(POLY_FT4));
	DR_TPAGE *tp = next_prim(packet, ot, z, sizeof(DR_TPAGE));
	int i, col, row, x0, y0, x1, y1;
	uint8_t u0, u1, v0, v1;

	if (frame < 0x0C)
		frame = 0x0C;
	if (frame > 0x17)
		frame = 0x17;
	i = frame - 0x0C;
	col = i % HUNT_DOG_COLS;
	row = i / HUNT_DOG_COLS;
	{
		int hx = hunt_dog_hot_x[i];
		int hy = hunt_dog_hot_y[i];

		if (facing_left)
			hx = HUNT_DOG_CELL_W - hx;
		x0 = gfx_nx(cx - hx);
		y0 = cy - hy;
		x1 = gfx_nx(cx - hx + HUNT_DOG_CELL_W);
		y1 = y0 + HUNT_DOG_CELL_H;
	}
	u0 = (uint8_t)(col * HUNT_DOG_CELL_W);
	v0 = (uint8_t)(row * HUNT_DOG_CELL_H);
	u1 = (uint8_t)(u0 + HUNT_DOG_CELL_W - 1);
	v1 = (uint8_t)(v0 + HUNT_DOG_CELL_H - 1);
	if (facing_left) {
		uint8_t t = u0;

		u0 = u1;
		u1 = t;
	}

	setDrawTPage(tp, 1, 0, getTPage(0, 0, HUNT_DOG_TPAGE_X, HUNT_DOG_TPAGE_Y));
	setPolyFT4(p);
	setRGB0(p, 128, 128, 128);
	setShadeTex(p, 1);
	setXY4(p, x0, y0, x1, y0, x0, y1, x1, y1);
	setUV4(p, u0, v0, u1, v0, u0, v1, u1, v1);
	setClut(p, HUNT_SPR_CLUT_X, HUNT_SPR_CLUT_Y);
	p->tpage = getTPage(0, 0, HUNT_DOG_TPAGE_X, HUNT_DOG_TPAGE_Y);
}

void gfx_draw_dog_walk(uint32_t *ot, uint8_t **packet, int z,
	int cx, int cy, int cel, int facing_left)
{
	POLY_FT4 *p = next_prim(packet, ot, z, sizeof(POLY_FT4));
	DR_TPAGE *tp = next_prim(packet, ot, z, sizeof(DR_TPAGE));
	int hx = 32;
	int hy = 28;
	int x0, y0, x1, y1;
	uint8_t u0, u1;

	if (cel < 0)
		cel = 0;
	if (cel > 3)
		cel = 3;
	if (facing_left)
		hx = 64 - hx;
	x0 = gfx_nx(cx - hx);
	y0 = cy - hy;
	x1 = gfx_nx(cx - hx + 64);
	y1 = y0 + 48;
	u0 = (uint8_t)(cel * 64);
	u1 = (uint8_t)(u0 + 63);
	if (facing_left) {
		uint8_t t = u0;

		u0 = u1;
		u1 = t;
	}
	setDrawTPage(tp, 1, 0, getTPage(0, 0, HUNT_WALK_TPAGE_X, HUNT_WALK_TPAGE_Y));
	setPolyFT4(p);
	setRGB0(p, 128, 128, 128);
	setShadeTex(p, 1);
	setXY4(p, x0, y0, x1, y0, x0, y1, x1, y1);
	setUV4(p, u0, 0, u1, 0, u0, 47, u1, 47);
	setClut(p, HUNT_SPR_CLUT_X, HUNT_SPR_CLUT_Y);
	p->tpage = getTPage(0, 0, HUNT_WALK_TPAGE_X, HUNT_WALK_TPAGE_Y);
}

void gfx_draw_cursor(uint32_t *ot, uint8_t **packet, int z, int x, int y)
{
	POLY_FT4 *p = next_prim(packet, ot, z, sizeof(POLY_FT4));
	DR_TPAGE *tp = next_prim(packet, ot, z, sizeof(DR_TPAGE));
	int x0 = gfx_nx(x);
	int x1 = gfx_nx(x + 8);

	setDrawTPage(tp, 1, 0, getTPage(0, 0, HUNT_CURSOR_TPAGE_X, HUNT_CURSOR_TPAGE_Y));
	setPolyFT4(p);
	setRGB0(p, 128, 128, 128);
	setShadeTex(p, 1);
	setXY4(p, x0, y, x1, y, x0, y + 8, x1, y + 8);
	setUV4(p, 0, 0, 7, 0, 0, 7, 7, 7);
	setClut(p, TITLE_BG_CLUT_X, TITLE_BG_CLUT_Y);
	p->tpage = getTPage(0, 0, HUNT_CURSOR_TPAGE_X, HUNT_CURSOR_TPAGE_Y);
}

static void draw_hud_tile_clut(uint32_t *ot, uint8_t **packet, int z,
	int tile, int nes_x, int nes_y, int clut_x, int clut_y)
{
	POLY_FT4 *p = next_prim(packet, ot, z, sizeof(POLY_FT4));
	DR_TPAGE *tp = next_prim(packet, ot, z, sizeof(DR_TPAGE));
	int x0 = gfx_nx(nes_x);
	int x1 = gfx_nx(nes_x + 8);
	uint8_t u0 = (uint8_t)(tile * 8);
	uint8_t u1 = (uint8_t)(u0 + 7);

	setDrawTPage(tp, 1, 0, getTPage(0, 0, HUNT_HUD_TPAGE_X, HUNT_HUD_TPAGE_Y));
	setPolyFT4(p);
	setRGB0(p, 128, 128, 128);
	setShadeTex(p, 1);
	setXY4(p, x0, nes_y, x1, nes_y, x0, nes_y + 8, x1, nes_y + 8);
	setUV4(p, u0, 0, u1, 0, u0, 7, u1, 7);
	setClut(p, clut_x, clut_y);
	p->tpage = getTPage(0, 0, HUNT_HUD_TPAGE_X, HUNT_HUD_TPAGE_Y);
}

void gfx_draw_hud_tile(uint32_t *ot, uint8_t **packet, int z,
	int tile, int nes_x, int nes_y)
{
	draw_hud_tile_clut(ot, packet, z, tile, nes_x, nes_y, HUNT_BG_CLUT_X, HUNT_BG_CLUT_Y);
}

static int hud_font_tile(int ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch == 'A')
		return HUNT_HUD_A;
	if (ch == 'D')
		return HUNT_HUD_D;
	if (ch == 'E')
		return HUNT_HUD_E;
	if (ch == 'I')
		return HUNT_HUD_I;
	if (ch == 'M')
		return HUNT_HUD_M;
	if (ch == 'N')
		return HUNT_HUD_N;
	if (ch == 'O')
		return HUNT_HUD_O;
	if (ch == 'P')
		return HUNT_HUD_P;
	if (ch == 'R')
		return HUNT_HUD_R;
	if (ch == 'S')
		return HUNT_HUD_S;
	if (ch == 'T')
		return HUNT_HUD_T;
	if (ch == 'U')
		return HUNT_HUD_U;
	return -1;
}

#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
void gfx_draw_title_options(uint32_t *ot, uint8_t **packet)
{
	static const char label[] = "OPTIONS";
	int i;

	for (i = 0; label[i]; i++)
		draw_title_sprite(ot, packet, 1, HUNT_HUD_TPAGE_X, HUNT_HUD_TPAGE_Y,
			TITLE_OPTIONS_CLUT_X, TITLE_BG_CLUT_Y,
#ifdef HUNTPS1_DELUXE_MENU
			144 + i * 8, 184, hud_font_tile(label[i]) * 8, 0, 8, 8);
#else
			64 + i * 8, 184, hud_font_tile(label[i]) * 8, 0, 8, 8);
#endif
}
#ifdef HUNTPS1_DELUXE_MENU
static void draw_title_label_clut(uint32_t *ot, uint8_t **packet, const char *label, int x, int y, int clut_x)
{
	for (int i = 0; label[i]; i++) {
		int tile = hud_font_tile(label[i]);
		const char *extra = tile < 0 ? strchr(title_extra_keys,label[i]) : 0;
		if (tile < 0 && !extra)
			continue;
		draw_title_sprite(ot, packet, 1,
			extra ? HUNT_CURSOR_TPAGE_X : HUNT_HUD_TPAGE_X,
			extra ? HUNT_CURSOR_TPAGE_Y : HUNT_HUD_TPAGE_Y,
			clut_x, TITLE_BG_CLUT_Y, x + i * 8, y,
			extra ? (extra-title_extra_keys)*8 : tile*8, extra ? 8 : 0, 8, 8);
	}
}

static void draw_title_label(uint32_t *ot, uint8_t **packet, const char *label, int x, int y)
{
	draw_title_label_clut(ot, packet, label, x, y, TITLE_OPTIONS_CLUT_X);
}

void gfx_draw_deluxe_result_text(uint32_t *ot, uint8_t **packet, const char *text, int y, int player)
{
	int clut_x = player == 1 ? SHOOTOUT_P1_CLUT_X : player == 2 ? SHOOTOUT_P2_CLUT_X : TITLE_OPTIONS_CLUT_X;
	draw_title_label_clut(ot, packet, text, (256 - (int)strlen(text) * 8) / 2, y, clut_x);
}

void gfx_draw_shootout_player(uint32_t *ot, uint8_t **packet, int player, int x, int y)
{
	int clut_x = player ? SHOOTOUT_P2_CLUT_X : SHOOTOUT_P1_CLUT_X;
	draw_hud_tile_clut(ot, packet, 0, HUNT_HUD_P, x, y, clut_x, TITLE_BG_CLUT_Y);
	draw_hud_tile_clut(ot, packet, 0, player + 1, x + 8, y, clut_x, TITLE_BG_CLUT_Y);
}

void gfx_draw_title_deluxe(uint32_t *ot, uint8_t **packet)
{
	draw_title_label(ot,packet,"DELUXE",64,184);
}

void gfx_draw_deluxe_menu(uint32_t *ot, uint8_t **packet,int selection)
{
	static const char *const games[]={"GAME D","GAME E","GAME F"};
	static const char *const names[]={"DUCK DUEL","SHOOTOUT","INTO THE WOODS"};
	draw_title_sprite(ot,packet,2,TITLE_BG_TPAGE_X,TITLE_BG_TPAGE_Y,
		TITLE_BG_CLUT_X,TITLE_BG_CLUT_Y,0,0,0,0,NES_SRC_W,128);
	draw_title_label(ot,packet,"DELUXE",176,124);
	for (int i=0;i<3;i++) {
		draw_title_label(ot,packet,games[i],64,136+i*16);
		draw_title_label(ot,packet,names[i],128,136+i*16);
	}
	draw_title_label(ot,packet,"ORIGINAL",64,184);
	gfx_draw_title_options(ot,packet);
	gfx_draw_title_cursor(ot,packet,1,selection==4 ? 128 : 48,
		selection<3 ? 136+selection*16 : 184);
}

static void draw_deluxe_logo_line(uint32_t *ot, uint8_t **packet,
	const char *label, int x, int y, int advance)
{
	static const char original[] = "DUCKHNT";
	static const uint8_t source_x[] = {32,72,112,152,72,152,192};
	static const char additions[] = "EILSW";
	/* New letters use the original logo's heavy left stems and block counters. */
	static const uint8_t rows[][8] = {
		{255,255,240,254,254,240,255,255},
		{255,255,60,60,60,60,255,255},
		{240,240,240,240,240,248,255,255},
		{127,255,240,254,127,3,255,254},
		{195,195,195,195,219,219,255,102},
	};
	int width = advance == 40 ? 32 : advance - 5;
	uint16_t blue = hunt_title_clut[1];
	for (int i = 0; label[i]; i++, x += advance) {
		if (label[i] == 'O') {
			/* Close the native U's rounded bowl with its vertically mirrored half. */
			for (int half = 0; half < 2; half++) {
				POLY_FT4 *p = next_prim(packet, ot, 1, sizeof(*p));
				int top = y + half * 20;
				int v0 = half ? 40 : 63, v1 = half ? 64 : 39;
				setPolyFT4(p);
				setShadeTex(p, 1);
				setXY4(p, x,top, x+width,top, x,top+20, x+width,top+20);
				setUV4(p, 72,v0, 104,v0, 72,v1, 104,v1);
				setClut(p, TITLE_BG_CLUT_X, TITLE_BG_CLUT_Y);
				p->tpage = getTPage(0,0,TITLE_BG_TPAGE_X,TITLE_BG_TPAGE_Y);
			}
			continue;
		}
		const char *glyph = strchr(original, label[i]);
		if (glyph) {
			int index = glyph - original;
			int u = source_x[index], v = index < 4 ? 24 : 80;
			POLY_FT4 *p = next_prim(packet, ot, 1, sizeof(*p));
			setPolyFT4(p);
			setShadeTex(p, 1);
			setXY4(p, x,y, x+width,y, x,y+40, x+width,y+40);
			setUV4(p, u,v, u+32,v, u,v+40, u+32,v+40);
			setClut(p, TITLE_BG_CLUT_X, TITLE_BG_CLUT_Y);
			p->tpage = getTPage(0,0,TITLE_BG_TPAGE_X,TITLE_BG_TPAGE_Y);
			continue;
		}
		glyph = strchr(additions, label[i]);
		if (!glyph)
			continue;
		int glyph_width = width + (label[i] == 'W' ? 10 : 0);
		for (int row = 0; row < 8; row++) {
			int mask = rows[glyph - additions][row];
			for (int col = 0; col < 8;) {
				int start = col++;
				if (!(mask & (128 >> start)))
					continue;
				while (col < 8 && (mask & (128 >> col)))
					col++;
				TILE *p = next_prim(packet, ot, 1, sizeof(*p));
				setTile(p);
				setRGB0(p, (blue & 31)*8, ((blue >> 5) & 31)*8, ((blue >> 10) & 31)*8);
				setXY0(p, x + start*glyph_width/8, y + row*5);
				setWH(p, col*glyph_width/8 - start*glyph_width/8, 5);
			}
		}
		x += glyph_width - width;
	}
}

void gfx_draw_deluxe_players(uint32_t *ot, uint8_t **packet,
	int mode, int selection, int population)
{
	static const char *const entries[] = {"1 PLAYER", "2 PLAYERS", "BACK"};
	if (mode == DELUXE_DUEL) {
		draw_title_sprite(ot,packet,1,TITLE_BG_TPAGE_X,TITLE_BG_TPAGE_Y,
			TITLE_BG_CLUT_X,TITLE_BG_CLUT_Y,32,24,32,24,152,40);
		draw_deluxe_logo_line(ot, packet, "DUEL", 104, 80, 40);
	} else if (mode == DELUXE_SHOOTOUT) {
		draw_deluxe_logo_line(ot, packet, "SHOOTOUT", 66, 24, 24);
	} else {
		draw_deluxe_logo_line(ot, packet, "INTO THE", 66, 24, 24);
		draw_deluxe_logo_line(ot, packet, "WOODS", 111, 80, 29);
	}
	draw_title_sprite(ot,packet,1,TITLE_BG_TPAGE_X,TITLE_BG_TPAGE_Y,
		TITLE_BG_CLUT_X,TITLE_BG_CLUT_Y,32,72,32,72,192,2);
	for (int i = 0; i < 3; i++)
		draw_title_label(ot, packet, entries[i], 64, 136 + i * 16);
	gfx_draw_title_cursor(ot, packet, 1, 48, 136 + selection * 16);
	if (mode == DELUXE_SHOOTOUT) {
		char hint[] = "DUCKS 00  LEFT RIGHT";
		hint[6] = '0' + population / 10;
		hint[7] = '0' + population % 10;
		draw_title_label(ot, packet, hint, 56, 192);
	}
}
#endif
#endif

void gfx_draw_hud_text(uint32_t *ot, uint8_t **packet, int z,
	const char *text, int nes_x, int nes_y)
{
	int i;

	for (i = 0; text[i]; i++) {
		int tile = hud_font_tile(text[i]);

		if (tile >= 0)
			gfx_draw_hud_tile(ot, packet, z, tile, nes_x + i * 8, nes_y);
	}
}
