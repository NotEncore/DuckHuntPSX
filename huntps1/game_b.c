#include "game_b.h"
#include "logic.h"
#include "nes_fly_rom.h"
#include "generated/game_b_flight.h"

#include <string.h>

static int random_byte(HuntPair *pair)
{
	pair->rng = hunt_color_rng_step(pair->rng);
	return pair->rng & 255;
}

static void start_duck(HuntPair *pair, int i)
{
	/* $C93A: draw a script and advance a repeated index by one. */
	HuntPairDuck *d = &pair->duck[i];
	int script = random_byte(pair) & 15;
	int band = pair->round < 10 ? pair->round - 1 : 9;

	if (script == pair->last_script)
		script = (script + 1) & 15;
	pair->last_script = script;
	d->script = game_b_script_offset[script];
	d->x = game_b_scripts[d->script++];
	d->y = 168;
	d->phase = PAIR_FLY;
	d->frame = 0;
	d->speed = game_b_speeds[band * 3 + d->color];
	if (i == 0)
		pair->second_delay = (random_byte(pair) & 63) + 1;
}

void hunt_pair_start(HuntPair *pair, int round, unsigned int seed)
{
	int last_script = pair->last_script;

	memset(pair, 0, sizeof(*pair));
	pair->last_script = last_script;
	pair->round = round;
	pair->rng = seed;
	pair->palette = game_b_colors[random_byte(pair) & (round == 1 ? 3 : 7)];
	pair->duck[0].color = pair->palette >> 4;
	pair->duck[1].color = pair->palette & 15;
	pair->result_age = -1;
	start_duck(pair, 0);
}

void hunt_pair_start_single(HuntPair *pair, int round, unsigned int seed, int color)
{
	int previous = pair->last_script;
	memset(pair, 0, sizeof(*pair));
	pair->last_script = previous;
	pair->round = round;
	pair->rng = seed;
	pair->result_age = -1;
	pair->duck[0].color = color;
	pair->duck[1].phase = PAIR_DONE;
	start_duck(pair, 0);
}

static void set_pattern(HuntPairDuck *d, int pattern)
{
	d->motion = d->loop = k_e64b[pattern];
	d->facing = hunt_nes_facing(pattern);
	d->frame = hunt_nes_pose_script(pattern) * 3 + d->cel;
}

static void fly(HuntPairDuck *d, int shots, unsigned *reflection)
{
	static const int cel_time[3] = { 3, 3, 5 };
	int count;

	if (shots == 0 && !d->escaping) {
		d->escaping = 1;
		d->duration = 240;
		set_pattern(d, 0);
	} else if (d->duration == 0) {
		int duration = game_b_scripts[d->script++];
		if (duration == 255) {
			if (!reflection) {
				d->phase = PAIR_DONE;
				return;
			}
			d->escaping = 1;
			d->duration = 240;
			*reflection = 0;
			set_pattern(d, 0);
		} else {
			d->duration = duration;
			set_pattern(d, game_b_scripts[d->script++]);
		}
	}
	if (d->y >= 240) {
		d->phase = PAIR_DONE;
		return;
	}
	d->duration--;
	count = k_ea00[d->speed & 31];
	if ((++d->speed & 3) == 0)
		d->speed -= 4;
	while (count--) {
		if (k_e661[d->motion] == 0xaa)
			d->motion = d->loop;
		int dy = (signed char)k_e661[d->motion++];
		int dx = (signed char)k_e661[d->motion++];
		if (reflection && !d->escaping) {
			const int left = 20, right = 235, top = 32, bottom = 168;
			if (d->x < left) d->x = left;
			if (d->x > right) d->x = right;
			d->x += (*reflection & 1) ? -2 * dx : 2 * dx;
			d->y += (*reflection & 2) ? -2 * dy : 2 * dy;
			if (d->x < left || d->x > right) {
				d->x = 2 * (d->x < left ? left : right) - d->x;
				*reflection ^= 1;
			}
			if (d->y < top || d->y > bottom) {
				d->y = 2 * (d->y < top ? top : bottom) - d->y;
				*reflection ^= 2;
			}
		} else {
			d->x = (d->x + dx) & 255;
			d->y = (d->y + dy) & 255;
		}
	}
	if (++d->cel_tick >= cel_time[d->cel]) {
		d->cel_tick = 0;
		d->frame -= d->cel;
		d->cel = (d->cel + 1) % 3;
		d->frame += d->cel;
	}
}

int hunt_pair_hit(HuntPair *pair, int i)
{
	HuntPairDuck *d = &pair->duck[i];
	d->phase = PAIR_HIT;
	d->hit_left = DUCK_HIT_POSE_FRAMES;
	d->frame = FRAME_HIT;
	d->popup_tile = hunt_duck_score_tile(pair->round, d->color);
	d->popup_x = d->x - 8;
	d->popup_y = d->y - 3;
	pair->hits++;
	return hunt_duck_score_points(d->popup_tile);
}

static void step(HuntPair *pair, int shots, int hit_duck, unsigned *reflection)
{
	int i;

	for (i = 0; i < 2; i++)
		if (pair->duck[i].popup_left)
			pair->duck[i].popup_left--;
	if (pair->result_age >= 0) {
		pair->result_age++;
		return;
	}
	if (pair->duck[1].phase == PAIR_WAIT && --pair->second_delay == 0)
		start_duck(pair, 1);
	for (i = 0; i < 2; i++) {
		HuntPairDuck *d = &pair->duck[i];
		if (i == hit_duck)
			continue;
		switch (d->phase) {
		case PAIR_FLY:
			fly(d, shots, reflection);
			break;
		case PAIR_HIT:
			if (--d->hit_left != 0)
				break;
			d->phase = PAIR_FALL;
			d->popup_left = 47;
			pair->last_hit_x = d->x;
			/* First visible fall update also advances the duck. */
			/* fall through */
		case PAIR_FALL:
			if (d->y >= 176) {
				d->phase = PAIR_DONE;
				break;
			}
			d->frame = FRAME_FALL0 + (d->fall_tick / DUCK_FALL_CEL_FRAMES) % 2;
			d->x += (d->fall_tick++ & 1) ? -1 : 1;
			d->y += 2;
			break;
		default:
			break;
		}
	}
	if (pair->duck[0].phase == PAIR_DONE && pair->duck[1].phase == PAIR_DONE)
		pair->result_age = 0;
}

void hunt_pair_step(HuntPair *pair, int shots, int hit_duck)
{
	step(pair, shots, hit_duck, 0);
}

void hunt_pair_step_duel(HuntPair *pair, unsigned *reflection)
{
	step(pair, 1, -1, reflection);
}
