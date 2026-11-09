#include "deluxe_ducks.h"
#include "logic.h"

void deluxe_start(DeluxeMatch *match, int mode, int players, int population, unsigned seed)
{
	*match = (DeluxeMatch){0};
	match->mode = mode;
	match->players = players == 2 ? 2 : 1;
	match->count = mode != DELUXE_SHOOTOUT ? 1 :
		population < 2 ? 2 : population > DELUXE_MAX_DUCKS ? DELUXE_MAX_DUCKS : population;
	match->rng = seed ? seed : 1;
	for (int i = 0; i < match->count; i++)
		match->ducks[i].delay = 30 + i * 5;
}

void deluxe_tick(DeluxeMatch *match)
{
	if (match->finished)
		return;
	match->elapsed++;
	for (int i = 0; i < match->count; i++) {
		DeluxeDuck *slot = &match->ducks[i];
		HuntPair *flight = &slot->flight;
		if (slot->delay) {
			if (--slot->delay)
				continue;
			int limit = match->mode == DELUXE_WOODS ? DELUXE_WOODS_DUCKS : 0;
			if (limit && match->launched == limit) {
				match->finished = 1;
				break;
			}
			match->rng = hunt_color_rng_step(match->rng ^ (0x9e3779b9u + match->generation));
			int color = match->mode == DELUXE_SHOOTOUT ? ((i & 1) ? 1 : 2) :
				(int)((match->rng >> 8) % 3);
			unsigned flight_seed = match->mode == DELUXE_WOODS ?
				(0x13u + (unsigned)match->launched * 5u) : match->rng;
			hunt_pair_start_single(flight, 1, flight_seed, color);
			slot->generation = ++match->generation;
			slot->reflection = 0;
			match->launched++;
		}
		if (match->mode == DELUXE_DUEL)
			hunt_pair_step_duel(flight, &slot->reflection);
		else
			hunt_pair_step(flight, 1, -1);
		if (flight->result_age == 0 && !flight->hits)
			match->escaped++;
		if (flight->result_age >= 24)
			slot->delay = match->mode == DELUXE_WOODS ? 61 : 1;
	}
}

unsigned deluxe_fire(DeluxeMatch *match, const DeluxeTarget *displayed,
	const DeluxeShot shots[2], int half_width, int half_height)
{
	unsigned hits = 0;
	int first = 0;
	if (match->finished)
		return 0;
	if (match->players == 2 && shots[0].fired && shots[1].fired) {
		first = match->priority;
		match->priority ^= 1;
	}
	for (int n = 0; n < match->players; n++) {
		int player = first ^ n;
		const DeluxeShot *shot = &shots[player];
		DeluxePlayer *score = &match->player[player];
		if (!shot->fired)
			continue;
		score->shots++;
		for (int i = 0; shot->valid && i < match->count; i++) {
			DeluxeDuck *slot = &match->ducks[i];
			const DeluxeTarget *target = &displayed[i];
			int dx = shot->x - target->x, dy = shot->y - target->y;
			if (!target->live || target->generation != slot->generation ||
					slot->flight.duck[0].phase != PAIR_FLY ||
					dx < -half_width || dx >= half_width || dy < -half_height || dy >= half_height)
				continue;
			int points = hunt_pair_hit(&slot->flight, 0);
			if (match->mode != DELUXE_SHOOTOUT ||
					slot->flight.duck[0].color == (player ? 1 : 2)) {
				score->hits++;
				score->score += points;
				if ((match->mode == DELUXE_DUEL && score->hits == DELUXE_DUEL_DUCKS) ||
						(match->mode == DELUXE_SHOOTOUT && score->hits == DELUXE_SHOOTOUT_GOAL))
					match->finished = 1;
			}
			hits |= 1u << player;
			break;
		}
		if (match->finished)
			break;
	}
	return hits;
}
