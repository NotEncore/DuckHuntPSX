#include "nes_fly.h"
#include "nes_fly_rom.h"
#include "logic.h"

static unsigned char rng_step(HuntNesFly *f)
{
	int t = (f->ec & 2) ^ (f->ed & 2);
	int c = t != 0;
	unsigned char *p[4];
	int i;

	p[0] = &f->ec;
	p[1] = &f->ed;
	p[2] = &f->ee;
	p[3] = &f->ef;
	for (i = 0; i < 4; i++) {
		int v = *p[i];
		int n = (v >> 1) | (c ? 0x80 : 0);
		c = v & 1;
		*p[i] = (unsigned char)n;
	}
	return f->ec;
}

static int dest_cb(int e, int yf)
{
	if (e == 0) {
		if (yf == 1) return 75;
		if (yf == 2) return 45;
		return 50;
	}
	if (e == 4) {
		if (yf == 0) return 52;
		if (yf == 2) return 75;
		return 45;
	}
	if (e == 8) {
		if (yf == 0) return 45;
		if (yf == 1) return 50;
		return 75;
	}
	if (e == 12) {
		if (yf == 0) return 75;
		if (yf == 1) return 45;
		return 50;
	}
	return 52;
}

static int choose48(HuntNesFly *f, int kind)
{
	int xbit = (kind == 0 || kind == 1) ? (f->pat & 8) : (kind == 2 ? 0 : 8);
	int c = ((f->pat & 7) ^ 7) | xbit;
	int r = rng_step(f) & 3;
	int e, yf, dest;

	if (r == 3)
		r = 2;
	e = c + r;
	yf = kind == 0 ? 0 : kind == 1 ? 2 : kind == 2 ? 4 : 2;
	dest = dest_cb(e, yf);
	if (dest == 45) {
		unsigned char a = rng_step(f);
		e += (a & 0x80) ? 1 : -1;
	} else if (dest == 75)
		e--;
	else if (dest == 50)
		e++;
	return e & 0x0F;
}

static int read_delta(HuntNesFly *f, int loop)
{
	int g;

	for (g = 0; g < 8; g++) {
		unsigned char v;

		if (f->idx < 0 || f->idx >= (int)sizeof(k_e661))
			f->idx = loop;
		v = k_e661[f->idx++];
		if (v != 0xAA)
			return (int)(signed char)v;
		f->idx = loop;
	}
	return 0;
}

static int pairs_from_5e(int s)
{
	int n = k_ea00[s & 31];

	return n < 1 ? 1 : n;
}

void hunt_nes_fly_spawn(HuntNesFly *f, int round_n, int spawn_x)
{
	int w;

	if (!f)
		return;
	w = 16 + ((round_n - 1) & 7) * 2;
	if (w > 28)
		w = 28;
	w &= ~3;
	f->s5e = w;
	f->seen = 0;
	/* Seed is host-local. NES $C080 burns RNG until NMI; do not fake those. */
	f->ec = (unsigned char)(round_n * 17 + 1);
	f->ed = 0x5A;
	f->ee = 0xA5;
	f->ef = 0x3C;
	f->ae = 0;
	/* $CF3F LDA #$AC STA $32. Live Game A always $48=0 $34=0 at grass. */
	f->y = 0xAC;
	f->pat = 0;
	f->steer_prev = 0;
	/* Live Game A grass X was 32..208. $C976 is script X, not spawn. */
	if (spawn_x < 32)
		spawn_x = 32;
	if (spawn_x > 208)
		spawn_x = 208;
	f->x = spawn_x;
	f->idx = k_e64b[0];
	f->pose = hunt_nes_pose_script(0);
	f->flip = hunt_nes_facing(0);
}

void hunt_nes_fly_start_escape(HuntNesFly *f)
{
	if (!f)
		return;
	/* $CB77 with A=0: $48=0, $49/$4A=$E64B[0]. $E661 is FF 00 AA. */
	f->pat = 0;
	f->idx = k_e64b[0];
	f->pose = hunt_nes_pose_script(0);
	f->flip = hunt_nes_facing(0);
}

int hunt_nes_fly_inbox(const HuntNesFly *f)
{
	if (!f)
		return 0;
	return f->y >= 0x20 && f->y < 0x90 && f->x >= 0x10 && f->x < 0xF0;
}

int hunt_nes_fly_step_control(HuntNesFly *f, int steer, unsigned int tick)
{
	int edge = 0, kind = -1, pairs, i, loop;
	int ny, nx;

	if (!f)
		return 0;
	steer &= 15;
	/* $C588 only at $CAFB/$CB45 (edge). $C080 title/idle burns are
	 * NMI-timed on NES and are not a fixed per-VBlank count on PS1.
	 * $16/$17 are joypads ($C284/$C2B1). $CA76 skips $CA8D only while
	 * A/B/Select/Start are held; GunCon play does not take that branch.
	 * $CBC7: Y >= $F0 jumps to $CF2A ($31=8) before any pair. */
	if (f->y >= 0xF0)
		return 1;
	/* $CA55/$CA59: $BA==0 or $4C expired stores $AE=1 and uses $CF16.
	 * $CF16 escapes below Y=8 or outside X $0C..$F4. Y>=$90 still
	 * floor-turns at $CAD7. Callers set ae; this does not invent $4C. */
	if (f->ae) {
		if (f->y < 0x08 || f->x < 0x0C || f->x >= 0xF4)
			return 1;
		if (f->y >= 0x90) {
			f->y = (f->y - 1) & 255;
			edge = 1;
			kind = 1;
		}
	} else if (steer) {
		/* $CAA8: a changed direction mask selects a pattern; a held mask
		 * continues the existing motion. Priority is R, L, D, U. */
		if (steer != f->steer_prev) {
			int packed = steer & 1 ? 0x35 : steer & 2 ? 0xdb :
				steer & 4 ? 0x79 : 0x1f;
			f->pat = (tick & 1) ? packed & 15 : packed >> 4;
			f->idx = k_e64b[f->pat];
			f->pose = hunt_nes_pose_script(f->pat);
			f->flip = hunt_nes_facing(f->pat);
		}
	} else if (f->y < 0x20) {
		f->y = (f->y + 1) & 255;
		edge = 1;
		kind = 0;
	} else if (f->seen && f->y >= 0x90) {
		f->y = (f->y - 1) & 255;
		edge = 1;
		kind = 1;
	} else if (f->x < 0x10) {
		f->x = (f->x + 1) & 255;
		edge = 1;
		kind = 2;
	} else if (f->x >= 0xF0) {
		f->x = (f->x - 1) & 255;
		edge = 1;
		kind = 3;
	}
	f->steer_prev = f->ae ? 0 : steer;
	if (edge) {
		/* $C080 leftover until NMI, then $CAFB/$CB45. Measured $C588
		 * count while $31=3: rng_calls_a med=246 (n=1355), rng_calls_b
		 * med=246 (n=1357). Not the old 230 fudge for frame 715.
		 * The 32-bit ROR is linear; this is that map applied 246 times. */
		for (i = 0; i < 246; i++)
			rng_step(f);
		f->pat = choose48(f, kind);
		if (f->pat < 0 || f->pat > 21)
			f->pat = 0;
		f->idx = k_e64b[f->pat];
		f->pose = hunt_nes_pose_script(f->pat);
		f->flip = hunt_nes_facing(f->pat);
	}
	loop = k_e64b[f->pat <= 21 ? f->pat : 0];
	pairs = pairs_from_5e(f->s5e);
	/* Spawn/climb: NES $AE skips CA8D clamp; host 151→149 is two $E661 pairs. */
	for (i = 0; i < pairs; i++) {
		int dy = read_delta(f, loop);
		int dx = read_delta(f, loop);
		/* $CBF1/$CC0C: $AE or $4B skips the pair clamp, so flyaway
		 * can cross $20/$10 and reach the $CF16 outer edge. */
		int skip = f->ae || !f->seen ||
			(!steer && (f->y < 0x20 || f->y >= 0x90));

		ny = f->y + dy;
		if (skip || (ny >= 0x1E && ny < 0x92))
			f->y = ny & 255;
		nx = f->x + dx;
		if (skip || (nx >= 0x0E && nx < 0xF2))
			f->x = nx & 255;
	}
	/* $D81B window walk */
	f->s5e = (f->s5e & ~3) | ((f->s5e + 1) & 3);
	if (hunt_nes_fly_inbox(f))
		f->seen = 1;
	return 0;
}

int hunt_nes_fly_step(HuntNesFly *f)
{
	return hunt_nes_fly_step_control(f, 0, 0);
}
