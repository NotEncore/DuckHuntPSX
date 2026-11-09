#include "generated/rom_hit_score_tables.h"
#include "clay_flight.h"
#include "generated/clay_tables.h"

void hunt_clay_launch(HuntClayFlight *clay, unsigned seed)
{
	const unsigned char *record = &clay_seeds[(seed & 15) * 6];

	*clay = (HuntClayFlight){0};
	/* $D669 clears the object and $D6A2 copies the six seed bytes. */
	clay->motion.axis[0][4] = record[0];
	clay->motion.axis[0][3] = record[1];
	clay->motion.axis[2][0] = record[2];
	clay->motion.axis[0][2] = record[3];
	clay->motion.axis[1][2] = record[4];
	clay->motion.axis[2][2] = record[5];
	clay->motion.axis[2][4] = 2;
	clay->depth_band = -1;
	clay->live = 1;
}

void hunt_clay_fly(HuntClayFlight *clay, unsigned round)
{
	unsigned depth, band, speed_offset, size_offset, steps;

	if (!clay->live)
		return;
	depth = clay->motion.axis[1][4];
	band = depth >> 2;
	/* $D6E2 bypasses the shift and sends negative velocity to the clamp. */
	if ((clay->motion.axis[2][2] & 0x80) || band > 7)
		band = 7;
	if ((int)band != clay->depth_band) {
		clay->depth_band = (int)band;
		speed_offset = round >= 10 ? 48 : clay_round_offsets[round ? round - 1 : 0];
		clay->speed_index = clay_depth_speeds[speed_offset + band];
	}
	steps = clay_update_counts[clay->speed_index];
	clay->speed_index = (clay->speed_index & ~3) | ((clay->speed_index + 1) & 3);
	while (steps--)
		hunt_clay_motion_step(&clay->motion);
	depth = clay->motion.axis[1][4];
	if ((clay->motion.axis[2][4] & 0x80) || depth >= 48) {
		clay->live = 0;
		return;
	}
	hunt_clay_project(&clay->motion, &clay->x, &clay->y);
	clay->y = (clay->y - 64) & 0xff;
	clay->cel = clay_cels[depth >> 1];
	size_offset = round >= 23 ? 6 : round >= 12 ? 3 : 0;
	clay->hit_size = clay_hit_sizes[(depth >> 1) + size_offset];
}

int hunt_clay_hit(const HuntClayFlight *displayed, int x, int y, int margin)
{
	/* $EB30 points to up to nine 8x8 white tiles for each apparent size. */
	static const signed char tile_offsets[6][18] = ROM_CLAY_HIT_OFFSETS;
	static const unsigned char tile_counts[6] = ROM_CLAY_HIT_COUNTS;
	int size = displayed->hit_size;

	if (!displayed->live || size < 0 || size >= 6)
		return 0;
	for (int tile = 0; tile < tile_counts[size]; tile++) {
		int top = (displayed->y + tile_offsets[size][tile * 2]) & 255;
		int left = (displayed->x + tile_offsets[size][tile * 2 + 1]) & 255;

		if (top >= 168)
			continue;
		if (x >= left - margin && x < left + 8 + margin &&
				y > top - margin && y <= top + 8 + margin)
			return 1;
	}
	return 0;
}
