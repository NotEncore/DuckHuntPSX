#ifndef HUNT_CLAY_MOTION_H
#define HUNT_CLAY_MOTION_H

#include <stdint.h>

typedef struct {
	uint8_t axis[3][5];
	uint16_t lift;
} HuntClayMotion;

/* Native unsigned coordinates before the draw-stage vertical offset. */
void hunt_clay_project(const HuntClayMotion *motion, int *x, int *y);
void hunt_clay_motion_step(HuntClayMotion *motion);

#endif
