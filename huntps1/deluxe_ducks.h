#ifndef HUNT_DELUXE_DUCKS_H
#define HUNT_DELUXE_DUCKS_H

#include "game_b.h"

enum { DELUXE_DUEL, DELUXE_SHOOTOUT, DELUXE_WOODS };
enum { DELUXE_MAX_DUCKS = 16, DELUXE_DUEL_DUCKS = 10, DELUXE_SHOOTOUT_GOAL = 10,
	DELUXE_WOODS_DUCKS = 20 };

typedef struct { int x, y, live; unsigned generation; } DeluxeTarget;
typedef struct { int fired, valid, x, y; } DeluxeShot;
typedef struct { int shots, hits, score; } DeluxePlayer;
typedef struct {
	HuntPair flight;
	unsigned generation, reflection;
	int delay;
} DeluxeDuck;
typedef struct {
	DeluxeDuck ducks[DELUXE_MAX_DUCKS];
	DeluxePlayer player[2];
	unsigned rng, generation;
	int mode, players, count, launched, escaped, elapsed, finished, priority;
} DeluxeMatch;

void deluxe_start(DeluxeMatch *match, int mode, int players, int population, unsigned seed);
void deluxe_tick(DeluxeMatch *match);
unsigned deluxe_fire(DeluxeMatch *match, const DeluxeTarget *displayed,
	const DeluxeShot shots[2], int half_width, int half_height);

#endif
