#ifndef HUNTPS1_GFX_H
#define HUNTPS1_GFX_H

#include <stdint.h>
#include <psxgpu.h>
#include "logic.h"
#include "generated/hunt_ps1_assets.h"

#define SCREEN_XRES 320
#define SCREEN_YRES 240
#define HUD_HEIGHT  32

#define HUNT_BG_TPAGE_X     320
#define HUNT_BG_TPAGE_Y     0
#define TITLE_BG_TPAGE_X    384
#define TITLE_BG_TPAGE_Y    0
#define HUNT_BG_CLUT_X      0
#define HUNT_BG_CLUT_Y      480
#define TITLE_BG_CLUT_X     16
#define TITLE_BG_CLUT_Y     480
#define HUNT_SPR_CLUT_X     32
#define HUNT_SPR_CLUT_Y     480
#define HUNT_SPR_BLUE_X     48
#define HUNT_SPR_RED_X      64
#define HUNT_DUCK_TPAGE_X   704
#define HUNT_DUCK_TPAGE_Y   256
#define HUNT_DOG_TPAGE_X    384
#define HUNT_DOG_TPAGE_Y    256
#define HUNT_CURSOR_TPAGE_X 448
#define HUNT_CURSOR_TPAGE_Y 256
#define HUNT_HUD_TPAGE_X    512
#define HUNT_HUD_TPAGE_Y    256
#define HUNT_WALK_TPAGE_X   640
#define HUNT_WALK_TPAGE_Y   0

#define GFX_STRIP_W   32
#define GFX_STRIP_H   8
#define GFX_STRIP_FINE 8

#define NES_FIELD_W  HUNT_BG_W
#define NES_FIELD_H  HUNT_BG_H

#define DUCK_BLACK 0
#define DUCK_BLUE  1
#define DUCK_RED   2

void gfx_upload(void);
void gfx_set_prim_limit(uint8_t *end);
void gfx_set_wide(int on);
int  gfx_wide(void);
int  gfx_nx(int nes_x);
int  gfx_nw(int nes_w);

void gfx_draw_title(uint32_t *ot, uint8_t **packet, int z);
#ifdef HUNTPS1_EXPERIMENTAL_OPTIONS
void gfx_draw_title_options(uint32_t *ot, uint8_t **packet);
#ifdef HUNTPS1_DELUXE_MENU
void gfx_draw_deluxe_result_text(uint32_t *ot, uint8_t **packet, const char *text, int y, int player);
void gfx_draw_shootout_player(uint32_t *ot, uint8_t **packet, int player, int x, int y);
void gfx_draw_title_deluxe(uint32_t *ot, uint8_t **packet);
void gfx_draw_deluxe_menu(uint32_t *ot, uint8_t **packet, int selection);
void gfx_draw_deluxe_players(uint32_t *ot, uint8_t **packet,
	int mode, int selection, int population);
#endif
#endif
void gfx_draw_title_cursor(uint32_t *ot, uint8_t **packet, int z, int x, int y);
void gfx_draw_title_digit(uint32_t *ot, uint8_t **packet, int z, int digit, int x, int y);
void gfx_draw_field_back(uint32_t *ot, uint8_t **packet, int z);
void gfx_draw_field_front(uint32_t *ot, uint8_t **packet, int z);
void gfx_draw_game_hud_tile(uint32_t *ot, uint8_t **packet, int z,
	int tile, int nes_x, int nes_y);
void gfx_draw_game_banner(uint32_t *ot, uint8_t **packet, int z, int over);
void gfx_draw_duck_score(uint32_t *ot, uint8_t **packet, int tile, int x, int y);
void gfx_draw_duck(uint32_t *ot, uint8_t **packet, int z,
	int cx, int cy, int frame, int facing_left, int color, int scale);
void gfx_draw_dog(uint32_t *ot, uint8_t **packet, int z,
	int cx, int cy, int frame, int facing_left);
void gfx_draw_dog_walk(uint32_t *ot, uint8_t **packet, int z,
	int cx, int cy, int cel, int facing_left);
void gfx_draw_cursor(uint32_t *ot, uint8_t **packet, int z, int x, int y);
void gfx_draw_hud_tile(uint32_t *ot, uint8_t **packet, int z,
	int tile, int nes_x, int nes_y);
void gfx_draw_hud_text(uint32_t *ot, uint8_t **packet, int z,
	const char *text, int nes_x, int nes_y);
/* Copy a NES-pixel rectangle of hunt BG to a display position (layout layer). */
void gfx_copy_hunt_bg(uint32_t *ot, uint8_t **packet, int z,
	int src_x, int src_y, int w, int h, int dst_x, int dst_y);
int gfx_hud_dx(int panel);

#endif
