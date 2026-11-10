/*
 * HuntPS1 SPU — M13 TITLE+INTRO+BARK. Not a CRT-passed production build.
 *
 * M10 gate between each dest. Bark is a third oneshot after M3, played
 * on voice 4 so it can overlap the intro. Do not restore bank upload.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <psxgpu.h>
#include <psxetc.h>
#include <psxspu.h>
#include <hwregs_c.h>
#include "audio.h"
#include "generated/hunt_sfx_m2_title.h"
#include "generated/hunt_sfx_m3_intro.h"
#ifndef HUNTPS1_ROM_BARK
#include "generated/hunt_sfx_m13_bark.h"
#else
extern const uint8_t rom_bark_spu[];
extern const unsigned int rom_bark_spu_size;
#endif

#ifdef HUNTPS1_GAME_A_AUDIO
#include "generated/game_a_sfx.h"
#include "generated/game_a_tally.h"
#ifdef HUNTPS1_WOODS_SOUND
#include "woods_footsteps.h"
#endif
#ifdef HUNTPS1_GAME_C
#include "generated/clay_sfx.h"
#endif

typedef struct {
	int id, voice, volume;
	const uint8_t *data;
	unsigned int bytes, adpcm, address;
} GameEffect;

#ifdef HUNTPS1_WOODS_SOUND
enum { WOODS_STEP_VOICE = 11, WOODS_STEP_VOLUME = 0x3fff };
#endif
#define EFFECT(id, voice, volume, name, stem) \
	{ id, voice, volume, game_a_##name, sizeof(game_a_##name), GAME_A_##stem##_ADPCM, 0 }
static GameEffect game_effects[] = {
	EFFECT(SFX_SHOT,      1, 0x2800, shot, SHOT),
	EFFECT(SFX_FLAP,      1, 0x1537, flap, FLAP), /* Test 4: ADPCM excitation scaled 4x. */
	EFFECT(SFX_QUACK,     2, 0x2800, call, CALL),
	EFFECT(SFX_FALL,      3, 0x3000, fall, FALL),
	EFFECT(SFX_LAND,      3, 0x1c00, land, LAND),
	EFFECT(SFX_RETRIEVE,  3, 0x1600, retrieve, RETRIEVE),
	EFFECT(SFX_LAUGH,     5, 0x1e00, laugh, LAUGH),
	EFFECT(SFX_CLEAR,     6, 0x1600, clear, CLEAR),
	EFFECT(SFX_PERFECT,   6, 0x2000, bonus, BONUS),
	EFFECT(SFX_GAME_OVER, 6, 0x1600, over, OVER),
	EFFECT(SFX_TALLY,     6, 0x1600, tally, TALLY),
#ifdef HUNTPS1_WOODS_SOUND
	{ SFX_WOODS_STEP_LEFT, WOODS_STEP_VOICE, WOODS_STEP_VOLUME,
		woods_step_0, sizeof(woods_step_0), WOODS_STEP_0_ADPCM, 0 },
	{ SFX_WOODS_STEP_RIGHT, WOODS_STEP_VOICE, WOODS_STEP_VOLUME,
		woods_step_1, sizeof(woods_step_1), WOODS_STEP_1_ADPCM, 0 },
#endif
#ifdef HUNTPS1_GAME_C
	{ SFX_CLAY_GO, 8, 0x2800, clay_go, sizeof(clay_go), CLAY_GO_BYTES, 0 },
	{ SFX_CLAY_LAUNCH, 9, 0x2800, clay_launch, sizeof(clay_launch), CLAY_LAUNCH_BYTES, 0 },
	{ SFX_CLAY_BREAK, 10, 0x2800, clay_break, sizeof(clay_break), CLAY_BREAK_BYTES, 0 },
#endif
};
#undef EFFECT
static int game_audio_ready, flight_age, call_left, hit_call_left, over_left;
/* Read-only hardware diagnostics: starts, last natural length, replacements,
 * and peak envelope. These never decide whether a cue plays. */
static volatile unsigned int game_effect_diag[SFX_COUNT][4];
enum { GAME_B_FLAP_VOICE = 7,
#if defined(HUNTPS1_WOODS_SOUND)
	GAME_EFFECT_VOICES = WOODS_STEP_VOICE + 1
#elif defined(HUNTPS1_GAME_C)
	GAME_EFFECT_VOICES = 11
#else
	GAME_EFFECT_VOICES = 8
#endif
};
static int game_voice_cue[GAME_EFFECT_VOICES], game_voice_age[GAME_EFFECT_VOICES];
static int game_shot_this_frame;
static int woods_mix;
#endif

#define TITLE_DEST   0x1010
#define INTRO_DEST   0xCB10
#define BARK_DEST    0x1D990
#define PLAY_RATE    22050
#define VOICE_MUSIC  0
#define VOICE_DOG    4
#define BARK_VOLUME  0x1e00 /* Test 4: +22 dB over the v1.0.0 voice gain. */
#define STOP_WAIT    8
#define TITLE_ADPCM  47840
#define TITLE_XFER   47872
#define INTRO_BYTES  69248
#ifdef HUNTPS1_ROM_BARK
#define BARK_ADPCM   10080
#define BARK_XFER    ((BARK_ADPCM + 63) & ~63)
#define BARK_DATA    rom_bark_spu
#define BARK_SIZE    rom_bark_spu_size
#define BARK_END_FLAG 0x07
#else
#define BARK_ADPCM   M13_BARK_ADPCM
#define BARK_XFER    M13_BARK_BYTES
#define BARK_DATA    m13_bark
#define BARK_SIZE    m13_bark_bytes
#define BARK_END_FLAG 0x05
#endif
/* 4328 blocks * 28 samples / 22050 Hz * 60 VBlank = 329.78. */
#define INTRO_AUDIO_FRAMES 330

#if M2_TITLE_BYTES != TITLE_XFER
#error M11 needs M2 title transfer 47872
#endif

#if M2_TITLE_ADPCM != TITLE_ADPCM
#error M11 needs M2 title ADPCM 47840
#endif

#if M3_INTRO_BYTES != INTRO_BYTES
#error M11 needs isolated M3 intro 69248
#endif

#if M3_INTRO_ADPCM != INTRO_BYTES
#error M11 M3 intro is already 64-aligned 69248
#endif

#if (TITLE_DEST + TITLE_XFER) != INTRO_DEST
#error M11 TITLE pad must end at M3 dest 0xCB10
#endif

#if (INTRO_DEST + INTRO_BYTES) != BARK_DEST
#error M13 bark dest must sit after M3 intro
#endif

#ifndef HUNTPS1_ROM_BARK
#if M13_BARK_BYTES != BARK_XFER
#error M13 bark transfer size
#endif

#if M13_BARK_ADPCM != BARK_ADPCM
#error M13 bark ADPCM size
#endif

#if ((M13_BARK_ADPCM + 63) & ~63) != M13_BARK_BYTES
#error M13 bark xfer must be 64-aligned pad of ADPCM
#endif
#endif

static uint8_t g_title_bounce[TITLE_XFER] __attribute__((aligned(64)));
static uint8_t g_intro_bounce[INTRO_BYTES] __attribute__((aligned(64)));
static uint8_t g_bark_bounce[BARK_XFER] __attribute__((aligned(64)));
static int g_ready;
static int g_paused;
static int g_title_left;
static int g_intro_left;
static unsigned int g_clk;
static int g_last[SFX_COUNT];
static int g_missed;
static int g_last_ok;
static int g_last_miss;
static int g_check_ok;
static int g_bark_attempts;
static int g_bark_accepted;
#ifdef HUNTPS1_ROM_BARK
/* Passive observation only: latch the peak envelope for 16 frames after key-on. */
static unsigned int g_bark_env_peak;
static int g_bark_probe_left;
#endif
static int g_fail_stage; /* 1 payload, 2 title DMA, 3 intro DMA, 4 bark DMA */
static uint32_t g_title_set;
static uint32_t g_intro_set;
static size_t g_title_wr;
static size_t g_intro_wr;
static size_t g_bark_wr;
static int g_title_comp;
static int g_intro_comp;
static int g_bark_comp;
static uint32_t g_chcr_before;
static uint32_t g_bark_set;

static void wait_voice_stopped(void)
{
	int i;

	SpuSetKey(0, 1 << VOICE_MUSIC);
	for (i = 0; i < STOP_WAIT; i++)
		VSync(0);
	SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
}

static int count_one_end(const uint8_t *adpcm, int bytes)
{
	int i, n, sh, fi, fl, ends;

	if ((bytes % 16) != 0)
		return 0;
	n = bytes / 16;
	ends = 0;
	for (i = 0; i < n; i++) {
		sh = adpcm[i * 16] & 0x0f;
		fi = adpcm[i * 16] >> 4;
		fl = adpcm[i * 16 + 1];
		if (sh > 12 || fi > 4)
			return 0;
		if (fl & 1)
			ends++;
	}
	return ends == 1;
}

#ifdef HUNTPS1_GAME_A_AUDIO
static int wait_game_dma(void)
{
	int frames;
	/* SPUSTAT can read idle between DMA requests. Wait for the DMA
	 * channel itself before changing SPU transfer address or direction. */
	for (frames = 0; DMA_CHCR(DMA_SPU) & (1u << 24); frames++) {
		if (frames == 8)
			return 0;
		VSync(0);
	}
	return SpuIsTransferCompleted(SPU_TRANSFER_WAIT) > 0;
}

static void upload_game_effects(void)
{
	unsigned int i, address = (BARK_DEST + BARK_XFER + 63) & ~63u;
	game_audio_ready = 0;
	if (!wait_game_dma())
		return;
	for (i = 0; i < sizeof(game_effects) / sizeof(game_effects[0]); i++) {
		GameEffect *e = &game_effects[i];
		if (e->bytes % 64 || address + e->bytes > 0x80000 ||
			e->adpcm > e->bytes || !count_one_end(e->data, e->adpcm) ||
			e->data[e->adpcm - 15] != 1)
			return;
		e->address = address;
		if (SpuSetTransferStartAddr(address) != address ||
			SpuWrite((const uint32_t *)e->data, e->bytes) != e->bytes ||
			!wait_game_dma())
			return;
		address += e->bytes;
	}
	game_audio_ready = 1;
}

static void play_game_effect_on(int id, int voice_override)
{
	unsigned int i;
	if (!game_audio_ready)
		return;
	if (id == SFX_HIT) {
		/* The NES starts the flight DMC sample nine frames into hit stun. */
		hit_call_left = 9;
		return;
	}
	if (id == SFX_FLAP && game_shot_this_frame)
		return;
	for (i = 0; i < sizeof(game_effects) / sizeof(game_effects[0]); i++) {
		const GameEffect *e = &game_effects[i];
		int voice = voice_override >= 0 ? voice_override : e->voice;
		if (e->id != id)
			continue;
		if (game_voice_age[voice])
			game_effect_diag[game_voice_cue[voice]][2]++;
		game_voice_cue[voice] = id;
		game_voice_age[voice] = 1;
		game_effect_diag[id][0]++;
		SpuSetKey(0, 1u << voice);
		SPU_CH_FREQ(voice) = getSPUSampleRate(PLAY_RATE);
		SPU_CH_ADDR(voice) = getSPUAddr(e->address);
		int volume = e->volume;
		if (woods_mix) {
			switch (id) {
			case SFX_QUACK: volume = 0x3fff; break;
			case SFX_LAND:  volume = 0x3000; break;
			default: break;
			}
		}
		SPU_CH_VOL_L(voice) = volume;
		SPU_CH_VOL_R(voice) = volume;
		SPU_CH_ADSR1(voice) = 0x00ff;
		SPU_CH_ADSR2(voice) = 0;
		SpuSetKey(1, 1u << voice);
		if (id == SFX_SHOT)
			game_shot_this_frame = 1;
		if (id == SFX_GAME_OVER) {
			unsigned int samples = e->adpcm / 16 * 28;
			over_left = (samples * 60 + PLAY_RATE - 1) / PLAY_RATE;
		}
		return;
	}
}

static void play_game_effect(int id)
{
	play_game_effect_on(id, -1);
}
#endif

static int prepare_title_bounce(void)
{
	int i;

	memset(g_title_bounce, 0, sizeof(g_title_bounce));
	memcpy(g_title_bounce, m2_title, (size_t)TITLE_XFER);
	if (g_title_bounce[TITLE_ADPCM - 15] != 0x05)
		return 0;
	for (i = TITLE_ADPCM; i < TITLE_XFER; i++) {
		if (g_title_bounce[i] != 0)
			return 0;
	}
	return count_one_end(g_title_bounce, TITLE_ADPCM);
}

static int prepare_intro_bounce(void)
{
	memset(g_intro_bounce, 0, sizeof(g_intro_bounce));
	memcpy(g_intro_bounce, m3_intro, (size_t)INTRO_BYTES);
	if (g_intro_bounce[INTRO_BYTES - 15] != 0x05)
		return 0;
	return count_one_end(g_intro_bounce, INTRO_BYTES);
}

static int prepare_bark_bounce(void)
{
	int i;

	memset(g_bark_bounce, 0, sizeof(g_bark_bounce));
	/* ROM source has no DMA padding; copy only its validated byte count. */
	memcpy(g_bark_bounce, BARK_DATA, (size_t)BARK_SIZE);
	if (g_bark_bounce[BARK_ADPCM - 15] != BARK_END_FLAG)
		return 0;
	for (i = BARK_ADPCM; i < BARK_XFER; i++) {
		if (g_bark_bounce[i] != 0)
			return 0;
	}
#ifdef HUNTPS1_ROM_BARK
	/* A one-shot bark uses normal data blocks followed by a silent loop. */
	for (i = 1; i < BARK_ADPCM - 15; i += 16) {
		if (g_bark_bounce[i] != 0)
			return 0;
	}
	for (i = BARK_ADPCM - 16; i < BARK_ADPCM; i++) {
		if (i != BARK_ADPCM - 15 && g_bark_bounce[i] != 0)
			return 0;
	}
#endif
	return count_one_end(g_bark_bounce, BARK_ADPCM);
}

static void upload_title(void)
{
	SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
	g_title_set = SpuSetTransferStartAddr((uint32_t)TITLE_DEST);
	g_title_wr = SpuWrite((const uint32_t *)g_title_bounce,
		(size_t)TITLE_XFER);
	g_title_comp = SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
}

static void upload_intro(void)
{
	g_chcr_before = DMA_CHCR(DMA_SPU);
	SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
	g_intro_set = SpuSetTransferStartAddr((uint32_t)INTRO_DEST);
	g_intro_wr = SpuWrite((const uint32_t *)g_intro_bounce,
		(size_t)INTRO_BYTES);
	g_intro_comp = SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
}

static void upload_bark(void)
{
	SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
	g_bark_set = SpuSetTransferStartAddr((uint32_t)BARK_DEST);
	g_bark_wr = SpuWrite((const uint32_t *)g_bark_bounce,
		(size_t)BARK_XFER);
	g_bark_comp = SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
}

static int payloads_ok(void)
{
	if (m2_title_bytes != TITLE_XFER)
		return 0;
	if (m3_intro_bytes != INTRO_BYTES)
		return 0;
	if (getSPUSampleRate(PLAY_RATE) != 0x0800)
		return 0;
	if (m2_title[TITLE_ADPCM - 15] != 0x05)
		return 0;
	if (m3_intro[INTRO_BYTES - 15] != 0x05)
		return 0;
#ifdef HUNTPS1_ROM_BARK
	if (BARK_SIZE != BARK_ADPCM)
#else
	if (BARK_SIZE != BARK_XFER)
#endif
		return 0;
	if (BARK_DATA[BARK_ADPCM - 15] != BARK_END_FLAG)
		return 0;
	return prepare_title_bounce() && prepare_intro_bounce()
		&& prepare_bark_bounce();
}

static void note_miss(int id)
{
	g_missed++;
	g_last_miss = id;
}

static const char *sfx_name(int id)
{
	if (id == SFX_TITLE)
		return "TITLE";
	if (id == SFX_ROUND)
		return "INTRO";
	if (id == SFX_BARK)
		return "BARK";
	if (id == SFX_FLAP)
		return "FLAP";
	if (id == SFX_QUACK)
		return "QUACK";
	if (id == SFX_SHOT)
		return "SHOT";
	if (id == SFX_HIT)
		return "HIT";
	if (id == SFX_LAND)
		return "LAND";
	if (id == SFX_RETRIEVE)
		return "RETR";
	if (id == SFX_LAUGH)
		return "LAUGH";
	if (id == SFX_TALLY)
		return "TALLY";
	if (id == SFX_PERFECT)
		return "PERF";
	if (id == -2)
		return "BUF";
	return "?";
}

void hunt_sfx_init(void)
{
	int i;

	SpuInit();
	SpuSetCommonMasterVolume(0x3fff, 0x3fff);
	g_paused = 0;
	g_bark_attempts = 0;
	g_bark_accepted = 0;
#ifdef HUNTPS1_ROM_BARK
	g_bark_env_peak = 0;
	g_bark_probe_left = 0;
#endif
	g_clk = 0;
	g_ready = 0;
	g_title_left = 0;
	g_intro_left = 0;
	g_missed = 0;
	g_last_ok = -1;
	g_last_miss = -1;
	g_check_ok = 0;
	g_fail_stage = 0;
	g_title_wr = g_intro_wr = g_bark_wr = 0;
	g_title_comp = g_intro_comp = g_bark_comp = 0;
	g_chcr_before = 0;
	for (i = 0; i < SFX_COUNT; i++)
		g_last[i] = -1000;

	if (!payloads_ok()) {
		g_fail_stage = 1;
		return;
	}

	upload_title();
	wait_voice_stopped();
	upload_intro();
	wait_voice_stopped();
	upload_bark();
	/* Do not report audio ready if any DMA upload was short or the
	 * completion wait did not report success. Previously these results
	 * were collected but never checked, masking SPU transfer failures. */
	if (g_title_wr != TITLE_XFER || g_title_comp <= 0) g_fail_stage = 2;
	else if (g_intro_wr != INTRO_BYTES || g_intro_comp <= 0) g_fail_stage = 3;
	else if (g_bark_wr != BARK_XFER || g_bark_comp <= 0) g_fail_stage = 4;
	if (g_fail_stage) return;
#ifdef HUNTPS1_GAME_A_AUDIO
	upload_game_effects();
#endif
	g_check_ok = 1;
	g_ready = 1;
}

void hunt_sfx_frame(void)
{
#ifdef HUNTPS1_GAME_A_AUDIO
	int voice;
	game_shot_this_frame = 0;
	for (voice = 1; voice < GAME_EFFECT_VOICES; voice++) {
		unsigned int env;
		int cue;
		if (!game_voice_age[voice])
			continue;
		cue = game_voice_cue[voice];
		env = SPU_CH_ADSR_VOL(voice);
		if (env > game_effect_diag[cue][3])
			game_effect_diag[cue][3] = env;
		if (!env && game_voice_age[voice] > 2) {
			game_effect_diag[cue][1] = game_voice_age[voice] - 1;
			game_voice_age[voice] = 0;
		} else {
			game_voice_age[voice]++;
		}
	}
	if (!g_paused && hit_call_left > 0 && --hit_call_left == 0)
		play_game_effect(SFX_QUACK);
	if (over_left > 0)
		over_left--;
#endif
#ifdef HUNTPS1_ROM_BARK
	if (g_bark_probe_left > 0) {
		unsigned int env = SPU_CH_ADSR_VOL(VOICE_DOG);
		if (env > g_bark_env_peak) g_bark_env_peak = env;
		g_bark_probe_left--;
	}
#endif
	g_clk++;
	if (g_intro_left > 0)
		g_intro_left--;
	if (g_title_left > 0) {
		g_title_left--;
		if (g_title_left == 0)
			SpuSetKey(0, 1 << VOICE_MUSIC);
	}
}

int hunt_sfx_intro_hold(void)
{
	return g_intro_left > 0;
}

void hunt_sfx_reset(void)
{
#ifdef HUNTPS1_GAME_A_AUDIO
	hit_call_left = over_left = 0;
	memset(game_voice_age, 0, sizeof(game_voice_age));
#endif
	SpuSetKey(0, 0xFFFFFF);
	g_paused = 0;
	g_title_left = 0;
	g_intro_left = 0;
}

void hunt_sfx_stop_loops(void)
{
	SpuSetKey(0, (1 << 2) | (1 << 3));
}

void hunt_sfx_stop_music(void)
{
	g_title_left = 0;
	g_intro_left = 0;
	SpuSetKey(0, 1 << VOICE_MUSIC);
}

void hunt_sfx_pause(int on)
{
	g_paused = on ? 1 : 0;
	if (on) {
#ifdef HUNTPS1_GAME_A_AUDIO
		hit_call_left = over_left = 0;
		memset(game_voice_age, 0, sizeof(game_voice_age));
#endif
		g_title_left = 0;
		g_intro_left = 0;
		SpuSetKey(0, 0xFFFFFF);
	}
}

void hunt_sfx_play(int id)
{
	int dest;

	if (id == SFX_BARK) g_bark_attempts++;

	if (!g_ready || g_paused || id < 0 || id >= SFX_COUNT) {
		if (id >= 0 && id < SFX_COUNT)
			note_miss(id);
		return;
	}
	if (id != SFX_TITLE && id != SFX_ROUND && id != SFX_BARK) {
#ifdef HUNTPS1_GAME_A_AUDIO
		play_game_effect(id);
#else
		note_miss(id);
#endif
		return;
	}
	if ((int)g_clk - g_last[id] < 4)
		return;
	g_last[id] = (int)g_clk;
	g_last_ok = id;

	if (id == SFX_BARK) {
		g_bark_accepted++;
		SpuSetKey(0, 1 << VOICE_DOG);
		SPU_CH_FREQ(VOICE_DOG) = getSPUSampleRate(PLAY_RATE);
		SPU_CH_ADDR(VOICE_DOG) = getSPUAddr(BARK_DEST);
		/* Last 0x05 oneshot. Do not write LOOP. Do not touch voice 0. */
		SPU_CH_VOL_L(VOICE_DOG) = BARK_VOLUME;
		SPU_CH_VOL_R(VOICE_DOG) = BARK_VOLUME;
		SPU_CH_ADSR1(VOICE_DOG) = 0x00ff;
		SPU_CH_ADSR2(VOICE_DOG) = 0x0000;
		SpuSetKey(1, 1 << VOICE_DOG);
#ifdef HUNTPS1_ROM_BARK
		g_bark_env_peak = 0;
		g_bark_probe_left = 16;
#endif
		return;
	}

	dest = (id == SFX_TITLE) ? TITLE_DEST : INTRO_DEST;
	SpuSetKey(0, 1 << VOICE_MUSIC);
	SPU_CH_FREQ(VOICE_MUSIC) = getSPUSampleRate(PLAY_RATE);
	SPU_CH_ADDR(VOICE_MUSIC) = getSPUAddr(dest);
	/* M2 title and isolated M3 intro are last 0x05. Do not write LOOP. */
	SPU_CH_VOL_L(VOICE_MUSIC) = 0x3fff;
	SPU_CH_VOL_R(VOICE_MUSIC) = 0x3fff;
	SPU_CH_ADSR1(VOICE_MUSIC) = 0x00ff;
	SPU_CH_ADSR2(VOICE_MUSIC) = 0x0000;
	SpuSetKey(1, 1 << VOICE_MUSIC);
	if (id == SFX_TITLE)
		g_title_left = 240;
	else {
		g_title_left = 0;
		g_intro_left = INTRO_AUDIO_FRAMES;
	}
}

void hunt_sfx_set_woods_mix(int active)
{
#ifdef HUNTPS1_GAME_A_AUDIO
	if (woods_mix && !active)
		hit_call_left = 0;
	woods_mix = active;
#else
	(void)active;
#endif
}

void hunt_sfx_begin_flight(void)
{
#ifdef HUNTPS1_GAME_A_AUDIO
	flight_age = 0;
	call_left = 17;
#endif
}

static void flight_effects(int empty, int pair)
{
#ifdef HUNTPS1_GAME_A_AUDIO
	if (++flight_age % 8 == 0) {
		if (pair && g_ready && !g_paused)
			play_game_effect_on(SFX_FLAP, GAME_B_FLAP_VOICE);
		else
			hunt_sfx_play(SFX_FLAP);
	}
	if (empty && call_left > 21)
		call_left = 21;
	if (--call_left <= 0) {
		hunt_sfx_play(SFX_QUACK);
		call_left = empty ? 21 : 65;
	}
#else
	(void)empty;
	(void)pair;
#endif
}

void hunt_sfx_flight(int empty)
{
	flight_effects(empty, 0);
}

void hunt_sfx_pair_flight(int empty)
{
	flight_effects(empty, 1);
}

int hunt_sfx_game_over_busy(void)
{
#ifdef HUNTPS1_GAME_A_AUDIO
	return over_left > 0;
#else
	return 0;
#endif
}

void hunt_sfx_play_buf(const uint8_t *adpcm, int bytes, int rate)
{
	(void)adpcm;
	(void)bytes;
	(void)rate;
	note_miss(-2);
}

void hunt_sfx_diag_lines(char *line0, char *line1, int n)
{
	if (!line0 || !line1 || n < 8)
		return;
	(void)n;
	if (!g_check_ok) {
		sprintf(line0, "AUDIO FAIL STAGE %d", g_fail_stage);
		sprintf(line1, "W %d/%d/%d C%d%d%d", (int)g_title_wr, (int)g_intro_wr, (int)g_bark_wr, g_title_comp > 0, g_intro_comp > 0, g_bark_comp > 0);
		return;
	}
	sprintf(line0, "M13P T%d I%d B%d H%d",
		(int)g_title_wr, (int)g_intro_wr, (int)g_bark_wr, g_intro_left);
	sprintf(line1, "OK%d BARK %d/%d MISS %d",
		g_ready, g_bark_accepted, g_bark_attempts, g_missed);
#ifdef HUNTPS1_ROM_BARK
	/* RB5 identifies the bark-only 0x01B0 channel-volume test. */
	snprintf(line1, n, "RB5 BARK %d/%d E%04X",
		g_bark_accepted, g_bark_attempts, g_bark_env_peak);
#endif
	(void)g_title_set;
	(void)g_intro_set;
	(void)g_bark_set;
	(void)g_title_comp;
	(void)g_intro_comp;
	(void)g_bark_comp;
}
