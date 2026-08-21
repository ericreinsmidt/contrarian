#ifndef CTR_PLATFORM_H
#define CTR_PLATFORM_H

#include <SDL.h>
#include <stdbool.h>

#define CTR_SCREEN_W 1024
#define CTR_SCREEN_H 768

#define CTR_POWEROFF_FLAG "/tmp/contrarian_poweroff"

/* Filesystem roots. Device defaults; each is overridable via the
 * same-named environment variable (used by the native dev build). */
extern const char *P_ROOT;     /* CTR_ROOT:     /mnt/SDCARD/Contrarian */
extern const char *P_CARD;     /* CTR_CARD:     /mnt/SDCARD (scan root) */
extern const char *P_USERDATA; /* CTR_USERDATA: /mnt/SDCARD/.userdata/tg5040 */
extern const char *P_SHARED;   /* CTR_SHARED:   /mnt/SDCARD/.userdata/shared */
extern const char *P_FONT;     /* CTR_FONT:     /usr/trimui/res/regular.ttf */
void paths_init(void);

/* Logical buttons Contrarian cares about. */
typedef enum {
	IN_NONE = 0,
	IN_LEFT, IN_RIGHT, IN_UP, IN_DOWN,
	IN_ACCEPT,   /* A */
	IN_BACK,     /* B */
	IN_X,        /* X */
	IN_Y,        /* Y */
	IN_L1, IN_R1,
	IN_START, IN_SELECT, IN_MENU,
	IN_VOLUP, IN_VOLDN,
	IN_BRIGHTUP, IN_BRIGHTDN,   /* front function buttons */
	IN_POWER,
	IN_COUNT
} in_button;

typedef struct {
	bool down[IN_COUNT];      /* current held state */
	bool pressed[IN_COUNT];   /* went down this frame */
	Uint32 down_since[IN_COUNT];
	Uint32 last_repeat[IN_COUNT];
	bool quit_requested;      /* SDL_QUIT or MENU+START dev combo */
} in_state;

bool plat_video_init(void);
void plat_video_quit(void);
SDL_Renderer *plat_renderer(void);

bool plat_input_init(void);
void plat_input_quit(void);
/* Pump SDL + raw evdev (power/volume keys); updates st. */
void plat_input_poll(in_state *st);
/* Held-direction auto-repeat: true when b should fire this frame. */
bool in_repeat(in_state *st, in_button b);

/* Fork/exec argv with extra environment (NULL-terminated "K=V" strings),
 * chdir to workdir if non-NULL, wait for exit. Caller must have torn down
 * video first. Returns child exit status or -1. */
int plat_run(char *const argv[], const char *const envkv[], const char *workdir);
/* True when the last plat_run() was cut short by the power button. */
bool plat_run_power_pressed(void);

/* Resident minarch: holds the GL context and the core between games, so a
 * launch costs ~200ms instead of ~1100ms. plat_resident_run returns false if
 * it is not answering, and the caller falls back to running minarch per game. */
bool plat_resident_ready(void);
bool plat_resident_send(const char *req_line);
void plat_spawn_resident(const char *elf, const char *core,
                         const char *const envkv[]);
bool plat_resident_wait(void);

/* Drop input that queued while a game owned the screen. */
void plat_input_flush(void);

void plat_request_poweroff(void);

/* Turn every LED off and stop the animation engine (saves power). */
void plat_leds_off(void);

/* Volume + brightness via the device's libmsettings (shared with minarch,
 * correct hardware paths). No-ops if the library can't be loaded. */
void plat_settings_init(void);
void plat_volume_nudge(int delta);       /* delta in 0..20 units */
void plat_brightness_nudge(int delta);   /* delta in 0..10 units */
void plat_volume_set_pct(int pct);       /* 0..100 */
void plat_brightness_set(int level);     /* 0..10 */
int  plat_volume_get(void);              /* current 0..20, or -1 if unavailable */
int  plat_brightness_get(void);          /* current 0..10, or -1 if unavailable */

/* Show the top OSD line at an explicit level (kind: 1=brightness, 2=volume).
 * plat_*_nudge call this automatically; the Muse client calls it directly from
 * the daemon-reported level (it doesn't own the settings device). */
void plat_osd_show(int kind, int val, int max);

/* Draw the volume/brightness OSD line at the top, if one is active. Call once
 * per frame, just before SDL_RenderPresent. */
void plat_draw_osd(SDL_Renderer *r);

/* Battery level 0..100 and charging flag; false if no reading is available. */
bool plat_battery(int *pct, bool *charging);

unsigned plat_now_ms(void);

#endif
