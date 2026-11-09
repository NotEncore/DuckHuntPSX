#include "clay_round.h"
#include "logic.h"

enum { FIRST_LAUNCH = 128, GO_FRAMES = 164, TALLY_INTERVAL = 16,
	BREAK_CLEANUP = 59, CLEAR_FRAMES = 240, BONUS_FRAMES = 200, NEXT_ROUND_FRAMES = 40 };

static unsigned clay_random(HuntClayRound *game)
{
	game->rng = hunt_color_rng_step(game->rng);
	return game->rng & 255;
}

static void start_pair(HuntClayRound *game)
{
	game->phase = CLAY_PAIR;
	game->age = 0;
	game->ammo = 3;
	game->launched = 0;
	game->next_launch = FIRST_LAUNCH;
	game->popup[0] = game->popup[1] = 0;
}

static int round_bonus(unsigned round)
{
	/* $D350 uses BCD round boundaries $11/$16/$21. */
	return round < 11 ? 10000 : round < 16 ? 15000 : round < 21 ? 20000 : 30000;
}

void hunt_clay_round_start(HuntClayRound *game, unsigned seed)
{
	*game = (HuntClayRound){0};
	game->rng = seed ? seed : 1;
	game->round = 1;
	game->phase = CLAY_GO;
	game->ammo = 3;
	game->last_seed = -1;
}

int hunt_clay_round_fire(HuntClayRound *game,
	const HuntClayFlight displayed[2], int have_light, int x, int y)
{
	if (game->phase != CLAY_PAIR || !game->ammo ||
			(!displayed[0].live && !displayed[1].live))
		return -2;
	game->ammo--;
	/* Prefer the original mask if another clay is only within the margin. */
	enum { HIT_MARGIN = 6 };
	int hit = -1;
	for (int margin = 0; have_light && hit < 0 && margin <= HIT_MARGIN; margin += HIT_MARGIN) {
		for (int i = 0; i < 2; i++) {
			if (game->target[i].live && hunt_clay_hit(&displayed[i], x, y, margin)) {
				hit = i;
				break;
			}
		}
	}
	if (hit >= 0) {
		int i = hit;
		game->target[i] = displayed[i];
		game->target[i].live = 0;
		game->broken[i] = BREAK_CLEANUP;
		game->popup[i] = 1;
		game->marks[game->pair * 2 + i] = 1;
		game->hits++;
		/* $CF56 and the score digit table at $EBFA. */
		game->score += game->round < 6 ? 1000 : game->round < 11 ? 1500 : 2000;
		return i;
	}
	return -1;
}

void hunt_clay_round_tick(HuntClayRound *game)
{
	game->age++;
	switch (game->phase) {
	case CLAY_GO:
		if (game->age >= GO_FRAMES)
			start_pair(game);
		break;
	case CLAY_PAIR:
		if (game->launched < 2 && game->age >= game->next_launch) {
			/* $D680 draws the next interval before the launch seed. */
			int delay = 1 + (clay_random(game) & 63);
			int seed = clay_random(game) & 15;
			if (seed == game->last_seed)
				seed = (seed + 1) & 15;
			game->last_seed = seed;
			hunt_clay_launch(&game->target[game->launched++], seed);
			game->next_launch = game->age + delay;
		}
		for (int i = 0; i < 2; i++) {
			hunt_clay_fly(&game->target[i], game->round);
			if (game->broken[i])
				game->broken[i]--;
		}
		if (game->launched == 2 && !game->target[0].live && !game->target[1].live &&
				!game->broken[0] && !game->broken[1]) {
			if (++game->pair < 5)
				start_pair(game);
			else {
				game->phase = CLAY_TALLY;
				game->age = 0;
			}
		}
		break;
	case CLAY_TALLY:
		if (game->age >= TALLY_INTERVAL) {
			int moved = 0;
			game->age = 0;
			/* $D2DD sweeps all marks left one open position at a time. */
			for (int i = 1; i < 10; i++) {
				if (!game->marks[i] || game->marks[i - 1])
					continue;
				game->marks[i] = 0;
				game->marks[i - 1] = 1;
				moved = 1;
			}
			if (moved) {
				game->tally++;
				return;
			}
			game->phase = game->hits >= hunt_hits_needed((int)game->round) ?
				CLAY_CLEAR : CLAY_OVER;
		}
		break;
	case CLAY_CLEAR:
		if (game->age >= CLEAR_FRAMES) {
			game->age = 0;
			game->phase = CLAY_BONUS;
			if (game->hits == 10) {
				game->bonus = round_bonus(game->round);
				game->score += game->bonus;
			}
		}
		break;
	case CLAY_BONUS:
		if (game->age >= (game->bonus ? BONUS_FRAMES : NEXT_ROUND_FRAMES)) {
			game->round++;
			game->phase = CLAY_GO;
			game->age = game->pair = game->hits = game->tally = game->bonus = 0;
			game->ammo = 3;
			for (int i = 0; i < 10; i++)
				game->marks[i] = 0;
		}
		break;
	case CLAY_OVER:
		if (game->age > CLAY_OVER_FRAMES)
			game->age = CLAY_OVER_FRAMES;
		break;
	}
}
