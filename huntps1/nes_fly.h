#ifndef HUNT_NES_FLY_H
#define HUNT_NES_FLY_H

/* Host-verified CAD1/CAD7/$E661 stepper. $48 from $CAFB/$CB45 using $C588.
 * Edge turns apply the measured $C080 leftover (246 $C588) first.
 * Not cycle-identical to Mesen NMI remainder. */
typedef struct {
	int y, x, pat, idx, s5e, seen, pose, flip, ae;
	unsigned char ec, ed, ee, ef;
	int steer_prev;
} HuntNesFly;

/* Game A: Y=$AC, $48=0. spawn_x is grass X (live 32..208). */
void hunt_nes_fly_spawn(HuntNesFly *f, int round_n, int spawn_x);
/* $CA29-$CA3B / $CB77: ammo hit 0 → pattern 0 (straight up). Does not
 * touch leftover 246, spawn Y, or grass. Caller sets $AE. */
void hunt_nes_fly_start_escape(HuntNesFly *f);
/* 0 = still flying. 1 = $CF2A escape ($31=8): Y>=$F0, or $AE and
 * (Y<8 or X<$0C or X>=$F4). */
int hunt_nes_fly_step(HuntNesFly *f);
/* NES $16 D-pad mask: right=1, left=2, down=4, up=8. */
int hunt_nes_fly_step_control(HuntNesFly *f, int steer, unsigned int tick);
int hunt_nes_fly_inbox(const HuntNesFly *f);

#endif
