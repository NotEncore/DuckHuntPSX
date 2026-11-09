#ifndef HUNT_CLAY_ROUND_H
#define HUNT_CLAY_ROUND_H

#include "clay_flight.h"

enum ClayPhase { CLAY_GO, CLAY_PAIR, CLAY_TALLY, CLAY_CLEAR, CLAY_BONUS, CLAY_OVER };
enum { CLAY_OVER_FRAMES = 122 };

typedef struct {
	HuntClayFlight target[2];
	int broken[2];
	int marks[10];
	unsigned rng, round;
	int phase, age, pair, ammo, hits, score;
	int launched, next_launch, last_seed;
	int tally, bonus;
	int popup[2];
} HuntClayRound;

void hunt_clay_round_start(HuntClayRound *game, unsigned seed);
void hunt_clay_round_tick(HuntClayRound *game);
/* Return slot 0/1 for hit, -1 for accepted miss, -2 for ignored edge. */
int hunt_clay_round_fire(HuntClayRound *game,
	const HuntClayFlight displayed[2], int have_light, int x, int y);

#endif
