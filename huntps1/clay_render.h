#ifndef HUNT_CLAY_RENDER_H
#define HUNT_CLAY_RENDER_H

#include "clay_round.h"

void hunt_clay_upload(void);
void hunt_clay_draw(uint32_t *ot, uint8_t **packet, uint8_t *end,
 const HuntClayRound *game, int flash, const HuntClayFlight *white);

#endif
