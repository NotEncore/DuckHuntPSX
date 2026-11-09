#ifndef HUNTPS1_AUDIO_H
#define HUNTPS1_AUDIO_H

enum {
	SFX_TITLE = 0,
	SFX_ROUND,
	SFX_BARK,
	SFX_FLAP,
	SFX_QUACK,
	SFX_SHOT,
	SFX_HIT,
	SFX_LAND,
	SFX_RETRIEVE,
	SFX_LAUGH,
	SFX_TALLY,
	SFX_PERFECT,
	SFX_FALL,
	SFX_CLEAR,
	SFX_GAME_OVER,
#ifdef HUNTPS1_GAME_C
	SFX_CLAY_GO,
	SFX_CLAY_LAUNCH,
	SFX_CLAY_BREAK,
#endif
#ifdef HUNTPS1_WOODS_SOUND
	SFX_WOODS_STEP_LEFT,
	SFX_WOODS_STEP_RIGHT,
#endif
	SFX_COUNT
};

void hunt_sfx_init(void);
void hunt_sfx_frame(void);
void hunt_sfx_reset(void);
void hunt_sfx_pause(int on);
void hunt_sfx_play(int id);
void hunt_sfx_play_buf(const uint8_t *adpcm, int bytes, int rate);
void hunt_sfx_stop_loops(void);
void hunt_sfx_stop_music(void);
void hunt_sfx_diag_lines(char *line0, char *line1, int n);
int hunt_sfx_intro_hold(void);
void hunt_sfx_begin_flight(void);
void hunt_sfx_flight(int empty);
void hunt_sfx_pair_flight(int empty);
int hunt_sfx_game_over_busy(void);

#endif
