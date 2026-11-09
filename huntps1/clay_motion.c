#include "clay_motion.h"

static unsigned word(const uint8_t *bytes)
{
	return bytes[0] | ((unsigned)bytes[1] << 8);
}

/* $D8DB shifts a two-byte partial result through the accumulator. */
static unsigned multiply(unsigned value, unsigned factor)
{
	unsigned partial = 0, accumulator = 0;

	for (int bit = 0; bit < 8; bit++, factor >>= 1) {
		if (factor & 1) {
			partial = (partial + value) & 0xffff;
			accumulator = partial >> 8;
		}
		partial = (partial >> 1) | ((accumulator & 0x80) << 8);
		accumulator = (accumulator << 1) & 0xff;
	}
	return partial;
}

/* $D907 uses the final subtract carry separately from the quotient. */
static unsigned divide(unsigned numerator, unsigned divisor, int *carry)
{
	unsigned quotient = 0, remainder = numerator;
	int trial = (int)(numerator + divisor);
	int accepted = trial > 0xffff;

	for (int bit = 0; bit < 8; bit++) {
		if (accepted)
			remainder = (unsigned)trial & 0xffff;
		remainder = ((remainder << 1) | (quotient >> 7)) & 0xffff;
		quotient = ((quotient << 1) | (unsigned)accepted) & 0xff;
		trial = (int)remainder - (int)divisor;
		accepted = trial >= 0;
	}
	*carry = accepted;
	return quotient;
}

void hunt_clay_project(const HuntClayMotion *motion, int *x, int *y)
{
	unsigned height = (word(&motion->axis[2][3]) - 0x1000) & 0xffff;
	unsigned depth = (word(&motion->axis[1][3]) - 0x1800) & 0xffff;
	unsigned difference = (multiply(depth, 127) + 0x234f -
		multiply(height, 59)) & 0xffff;
	unsigned divisor = (difference >> 1) | (difference >= 0x5000 ? 0x8000 : 0);
	unsigned vertical = (multiply(motion->lift, 108) +
		multiply(height, 231)) & 0xffff;
	unsigned horizontal = (word(&motion->axis[0][3]) - 0x0900) & 0xffff;
	int carry;
	unsigned quotient = divide(vertical, divisor, &carry);

	*y = (int)(((quotient - (unsigned)!carry) & 0xff) ^ 0x7f);
	*x = (int)(divide(horizontal, divisor, &carry) ^ 0x80);
}

void hunt_clay_motion_step(HuntClayMotion *motion)
{
	for (int i = 0; i < 3; i++) {
		uint8_t *axis = motion->axis[i];
		/* $D8AF integrates acceleration, then adds rounded velocity. */
		int acceleration = axis[0] < 128 ? axis[0] : (int)axis[0] - 256;
		unsigned velocity = (word(&axis[1]) + (unsigned)acceleration) & 0xffff;
		int whole = (int)(velocity >> 8);
		unsigned position;

		if (whole >= 128)
			whole -= 256;
		position = (word(&axis[3]) + (unsigned)(whole +
			((velocity & 0x80) != 0))) & 0xffff;
		axis[1] = (uint8_t)velocity;
		axis[2] = (uint8_t)(velocity >> 8);
		axis[3] = (uint8_t)position;
		axis[4] = (uint8_t)(position >> 8);
	}
}
