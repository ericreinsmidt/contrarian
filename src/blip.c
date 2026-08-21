/* SPDX-License-Identifier: 0BSD */
#include "blip.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>

#define RATE 22050
#define MAXV 6

/* A voice is one square-wave tone: frequency, duration, and a linear decay.
 * The NES's pulse channels had four duty cycles; one is plenty here. */
typedef struct {
	float    freq;
	int      remain;    /* frames left */
	int      total;
	float    phase;
	float    amp;
	int      slide;     /* Hz added per frame, for the rising start chirp */
} voice;

static SDL_AudioDeviceID dev;
static voice voices[MAXV];
static SDL_AudioSpec have;

/* The retune sound is a real recording of a dial being turned -- a synthesised
 * approximation of a mechanical detent never sounds like one. Everything else
 * stays synthesised; this is the one thing worth carrying a file for. */
static Sint16 *tune_pcm;
static Uint32  tune_len;      /* in samples */
static Uint32  tune_pos;      /* == tune_len means idle */

static Sint16 *off_pcm;
static Uint32  off_len;
static Uint32  off_pos;

static void mix(void *ud, Uint8 *stream, int len)
{
	Sint16 *out = (Sint16 *)stream;
	int n = len / 2, i, v;

	(void)ud;
	memset(stream, 0, (size_t)len);
	for (i = 0; i < n; i++) {
		float acc = 0.0f;
		if (tune_pcm && tune_pos < tune_len)
			acc += (float)tune_pcm[tune_pos++] / 32768.0f;
		if (off_pcm && off_pos < off_len)
			acc += (float)off_pcm[off_pos++] / 32768.0f;
		for (v = 0; v < MAXV; v++) {
			voice *o = &voices[v];
			float env;
			if (o->remain <= 0) continue;
			env = (float)o->remain / (float)o->total;
			o->phase += (o->freq + (float)o->slide * (float)(o->total - o->remain))
			            / (float)RATE;
			if (o->phase >= 1.0f) o->phase -= 1.0f;
			acc += (o->phase < 0.5f ? 1.0f : -1.0f) * o->amp * env * env;
			o->remain--;
		}
		if (acc >  1.0f) acc =  1.0f;
		if (acc < -1.0f) acc = -1.0f;
		out[i] = (Sint16)(acc * 9000.0f);
	}
}

static void push(float freq, int ms, float amp, int slide)
{
	int v;
	for (v = 0; v < MAXV; v++) {
		if (voices[v].remain > 0) continue;
		voices[v].freq   = freq;
		voices[v].total  = voices[v].remain = RATE * ms / 1000;
		voices[v].phase  = 0.0f;
		voices[v].amp    = amp;
		voices[v].slide  = slide;
		return;
	}
}

/* Loads res/audio/tune.wav if it is there. Absent, the dial falls back to the
 * synthesised click, so a missing file costs a sound rather than silence. */
static bool load_wav(const char *res_dir, const char *name,
                     Sint16 **pcm, Uint32 *len, Uint32 *pos)
{
	char path[1024];
	SDL_AudioSpec spec;
	Uint8 *buf = NULL;
	Uint32 blen = 0;

	if (*pcm || !res_dir || !*res_dir) return false;
	snprintf(path, sizeof path, "%s/audio/%s", res_dir, name);
	if (!SDL_LoadWAV(path, &spec, &buf, &blen)) return false;

	/* Only the format we ship. Anything else is a build mistake, and silently
	 * playing it back at the wrong rate would be worse than not playing it. */
	if (spec.format == AUDIO_S16SYS && spec.channels == 1 && spec.freq == RATE) {
		*pcm = (Sint16 *)buf;
		*len = blen / sizeof(Sint16);
		*pos = *len;
		return true;
	}
	fprintf(stderr, "%s is %d Hz/%d ch, expected %d Hz mono\n",
	        name, spec.freq, spec.channels, RATE);
	SDL_FreeWAV(buf);
	return false;
}

static void load_tune(const char *res_dir)
{
	load_wav(res_dir, "tune.wav",     &tune_pcm, &tune_len, &tune_pos);
	load_wav(res_dir, "poweroff.wav", &off_pcm,  &off_len,  &off_pos);
}

unsigned blip_remaining_ms(void)
{
	Uint32 left = 0;
	if (!dev) return 0;
	SDL_LockAudioDevice(dev);
	if (tune_pcm && tune_pos < tune_len) left = tune_len - tune_pos;
	if (off_pcm && off_pos < off_len && off_len - off_pos > left)
		left = off_len - off_pos;
	SDL_UnlockAudioDevice(dev);
	return (unsigned)(left * 1000u / RATE);
}

bool blip_init_res(const char *res_dir)
{
	SDL_AudioSpec want;
	if (dev) { load_tune(res_dir); return true; }
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) return false;
	SDL_zero(want);
	want.freq     = RATE;
	want.format   = AUDIO_S16SYS;
	want.channels = 1;
	want.samples  = 512;
	want.callback = mix;
	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (!dev) return false;
	SDL_PauseAudioDevice(dev, 0);
	load_tune(res_dir);
	return true;
}

bool blip_init(void) { return blip_init_res(NULL); }

void blip_quit(void)
{
	if (!dev) return;
	SDL_CloseAudioDevice(dev);
	dev = 0;
	memset(voices, 0, sizeof voices);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void blip_play(blip_id id)
{
	if (!dev) return;
	SDL_LockAudioDevice(dev);
	switch (id) {
	case BLIP_TUNE:
		if (tune_pcm) tune_pos = 0;       /* retrigger the recorded detent */
		else          push(880.0f, 26, 0.5f, 0);
		break;
	case BLIP_START:                      /* rising two-note confirm */
		push(523.0f, 70, 0.6f, 0);
		push(1046.0f, 130, 0.5f, 2);
		break;
	case BLIP_DENY:                       /* low buzz: this is not Contra */
		push(120.0f, 180, 0.7f, -1);
		break;
	case BLIP_TOGGLE:
		push(1320.0f, 40, 0.45f, 0);
		break;
	case BLIP_POWEROFF:
		if (off_pcm) off_pos = 0;
		break;
	}
	SDL_UnlockAudioDevice(dev);
}
