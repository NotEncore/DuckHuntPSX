/*
 * Known-good GunCon calibration (GunConCalibrationMenu on SCPH-5501).
 * Do not change these constants or the compute/apply math.
 *
 * Transform: after original map_gun_to_screen,
 *   if use_scale: x = (ox * sx + 32768) >> 16; same for y (sx/sy 16.16)
 *   then x += ox; y += oy; clamp to screen.
 * Offset-only unless |scale-1| > 5% on X or Y from 5-point regression.
 * Points: (80,104) (240,104) (160,148) (80,188) (240,188)
 * 8 mapped samples/point; drop |d| > 24 from per-point median.
 */

#ifndef GUNCAL_H
#define GUNCAL_H

#include <stdint.h>

#define GUNCAL_SCREEN_X    320
#define GUNCAL_SCREEN_Y    240

#define GUN_ERR_X          0x0001
#define GUN_ERR_Y_A        0x0005
#define GUN_ERR_Y_B        0x000A
#define GUN_X_MIN          0x004D
#define GUN_X_MAX          0x01CD
#define GUN_Y_MIN_NTSC     0x0019
#define GUN_Y_MAX_NTSC     0x00F8

#define GUNCAL_POINTS      5
#define GUNCAL_SHOTS       8
#define GUNCAL_OUTLIER_PX  24
#define GUNCAL_FP_ONE      65536
#define GUNCAL_SCALE_PCT   5
#define GUNCAL_RADIUS      22

typedef struct {
	int mx[GUNCAL_SHOTS];
	int my[GUNCAL_SHOTS];
	int rx[GUNCAL_SHOTS];
	int ry[GUNCAL_SHOTS];
	int n;
	int mean_mx, mean_my;
	int mean_rx, mean_ry;
	int kept;
	int dropped;
} GunCalPoint;

typedef struct {
	int has_cal;
	int use_scale;
	int sx, sy;
	int ox, oy;
	int dropped_all;
	GunCalPoint pt[GUNCAL_POINTS];
} GunCal;

extern const int guncal_tx[GUNCAL_POINTS];
extern const int guncal_ty[GUNCAL_POINTS];

int guncon_has_light(uint16_t gun_x, uint16_t gun_y);
void map_gun_to_screen(uint16_t gun_x, uint16_t gun_y, int *sx, int *sy);
void guncal_reset(GunCal *c);
int guncal_add_shot(GunCal *c, int point, int orig_x, int orig_y,
	int raw_x, int raw_y);
int guncal_point_full(const GunCal *c, int point);
void guncal_compute(GunCal *c);
void guncal_compute_to(GunCal *c, const int *tx, const int *ty);
/* min_keep=5 matches BirdHunt. HuntPS1 uses 3 accepted shots/point.
 * BirdHunt still calls guncal_compute / guncal_compute_to. */
void guncal_compute_to_min(GunCal *c, const int *tx, const int *ty, int min_keep);
void guncal_apply(const GunCal *c, int orig_x, int orig_y, int *sx, int *sy);
void guncal_fmt_scale(char *out, int fp);

#endif
