#ifndef HUNT_GAME_B_H
#define HUNT_GAME_B_H

enum HuntPairPhase { PAIR_WAIT, PAIR_FLY, PAIR_HIT, PAIR_FALL, PAIR_DONE };

typedef struct {
	int x, y, phase, frame, facing, color;
	int popup_left, popup_tile, popup_x, popup_y;
	int script, duration, motion, loop, speed;
	int hit_left, fall_tick, cel, cel_tick, escaping;
} HuntPairDuck;

typedef struct {
	HuntPairDuck duck[2];
	int second_delay, last_script, hits, last_hit_x, result_age;
	int round, palette;
	unsigned int rng;
} HuntPair;

void hunt_pair_start(HuntPair *pair, int round, unsigned int seed);
void hunt_pair_start_single(HuntPair *pair, int round, unsigned int seed, int color);
int hunt_pair_hit(HuntPair *pair, int duck);
void hunt_pair_step(HuntPair *pair, int shots, int hit_duck);

void hunt_pair_step_duel(HuntPair *pair, unsigned *reflection);

#endif
