#ifndef HUNT_CLAY_FLIGHT_H
#define HUNT_CLAY_FLIGHT_H

#include "clay_motion.h"

typedef struct {
	HuntClayMotion motion;
	int x, y, cel, hit_size;
	int depth_band, speed_index;
	int live;
} HuntClayFlight;

/* Seed index is the native four-bit launch draw. Round starts at one. */
void hunt_clay_launch(HuntClayFlight *clay, unsigned seed);
void hunt_clay_fly(HuntClayFlight *clay, unsigned round);
/* Test a point in native NES coordinates against the optical tile mask. */
int hunt_clay_hit(const HuntClayFlight *displayed, int x, int y, int margin);

#endif
