#include "platform.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#ifdef __linux__
#include <linux/input.h>
#endif

/* Front function buttons on the Brick report on the gamepad node (event3)
 * as BTN_THUMBL / BTN_THUMBR (verified by capture on hardware). SDL doesn't
 * map these to anything Contrarian uses, so we read them raw. */
#define CODE_FN_LEFT  317
#define CODE_FN_RIGHT 318
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* SDL joystick button indices on the Brick's "TRIMUI Player1" device
 * (BTN_SOUTH..BTN_THUMBR enumerate to 0..10; d-pad is hat 0). */
enum {
	JOY_B = 0, JOY_A = 1, JOY_Y = 2, JOY_X = 3,
	JOY_L1 = 4, JOY_R1 = 5, JOY_SELECT = 6, JOY_START = 7,
	JOY_MENU = 8, JOY_L3 = 9, JOY_R3 = 10,
	JOY_VOLDN = 13, JOY_VOLUP = 14,
};

#define REPEAT_DELAY_MS 380
#define REPEAT_RATE_MS  120

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Joystick *joy;
static int fd_power = -1; /* axp2202-pek: KEY_POWER */
static int fd_keys = -1;  /* sunxi-keyboard: volume keys */
static int fd_joy = -1;   /* TRIMUI Player1: raw, for the FN buttons */

/* Opt-in via CTR_INPUT_DEBUG=1: log raw evdev codes + SDL joystick button
 * indices so a single button press reveals exactly which source a control
 * arrives on. Off by default; costs nothing in normal runs. */
static int dbg_input;

const char *P_ROOT = "/mnt/SDCARD/Contrarian";
const char *P_CARD = "/mnt/SDCARD";
const char *P_USERDATA = "/mnt/SDCARD/.userdata/tg5040";
const char *P_SHARED = "/mnt/SDCARD/.userdata/shared";
const char *P_FONT = "/usr/trimui/res/regular.ttf";

void paths_init(void)
{
	const char *v;
	if ((v = getenv("CTR_ROOT"))) P_ROOT = v;
	if ((v = getenv("CTR_CARD"))) P_CARD = v;
	if ((v = getenv("CTR_USERDATA"))) P_USERDATA = v;
	if ((v = getenv("CTR_SHARED"))) P_SHARED = v;
	if ((v = getenv("CTR_FONT"))) P_FONT = v;
}

SDL_Renderer *plat_renderer(void) { return ren; }
unsigned plat_now_ms(void) { return SDL_GetTicks(); }

bool plat_video_init(void)
{
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
#ifdef __linux__
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
#endif
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "video init: %s\n", SDL_GetError());
		return false;
	}
	SDL_ShowCursor(SDL_DISABLE);
	win = SDL_CreateWindow("Contrarian", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
	                       CTR_SCREEN_W, CTR_SCREEN_H,
	                       SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
	if (!win) {
		fprintf(stderr, "window: %s\n", SDL_GetError());
		return false;
	}
	ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!ren) {
		fprintf(stderr, "renderer: %s\n", SDL_GetError());
		return false;
	}
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
	SDL_RendererInfo info;
	if (SDL_GetRendererInfo(ren, &info) == 0)
		fprintf(stderr, "renderer: %s, driver: %s\n", info.name, SDL_GetCurrentVideoDriver());
	return true;
}

void plat_video_quit(void)
{
	if (ren) { SDL_DestroyRenderer(ren); ren = NULL; }
	if (win) { SDL_DestroyWindow(win); win = NULL; }
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

static void open_joystick(void)
{
	for (int i = 0; i < SDL_NumJoysticks(); i++) {
		SDL_Joystick *j = SDL_JoystickOpen(i);
		if (!j) continue;
		fprintf(stderr, "joystick %d: %s (%d buttons, %d hats, %d axes)\n",
		        i, SDL_JoystickName(j), SDL_JoystickNumButtons(j),
		        SDL_JoystickNumHats(j), SDL_JoystickNumAxes(j));
		if (!joy) joy = j; /* first one is TRIMUI Player1 */
	}
}

bool plat_input_init(void)
{
	dbg_input = getenv("CTR_INPUT_DEBUG") != NULL ||
	            access("/mnt/SDCARD/Contrarian/.input_debug", F_OK) == 0;
	if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0) {
		fprintf(stderr, "joystick init: %s\n", SDL_GetError());
		return false;
	}
	SDL_JoystickEventState(SDL_ENABLE);
	open_joystick();
#ifdef __linux__
	if (fd_power < 0) fd_power = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
	if (fd_keys < 0) fd_keys = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
	if (fd_joy < 0) fd_joy = open("/dev/input/event3", O_RDONLY | O_NONBLOCK);
#endif
	return true;
}

/* Throw away everything that queued up while a game owned the screen. The
 * joystick stayed open through the game, so SDL has been collecting presses
 * meant for it, and the raw fds have too. */
void plat_input_flush(void)
{
	SDL_Event e;
	SDL_PumpEvents();
	while (SDL_PollEvent(&e)) { }
#ifdef __linux__
	{
		struct input_event ev;
		int fds[3], i;
		fds[0] = fd_power; fds[1] = fd_keys; fds[2] = fd_joy;
		for (i = 0; i < 3; i++)
			while (fds[i] >= 0 &&
			       read(fds[i], &ev, sizeof ev) == (ssize_t)sizeof ev) { }
	}
#endif
}

void plat_input_quit(void)
{
	if (joy) { SDL_JoystickClose(joy); joy = NULL; }
	SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
	/* keep raw fds; they are display-independent */
}

static void set_btn(in_state *st, in_button b, bool down)
{
	if (b == IN_NONE) return;
	if (down && !st->down[b]) {
		st->pressed[b] = true;
		st->down_since[b] = SDL_GetTicks();
		st->last_repeat[b] = 0;
	}
	st->down[b] = down;
}

static in_button map_joy_button(int jb)
{
	switch (jb) {
	case JOY_A: return IN_ACCEPT;
	case JOY_B: return IN_BACK;
	case JOY_X: return IN_X;
	case JOY_Y: return IN_Y;
	case JOY_L1: return IN_L1;
	case JOY_R1: return IN_R1;
	case JOY_START: return IN_START;
	case JOY_SELECT: return IN_SELECT;
	case JOY_MENU: return IN_MENU;
	case JOY_VOLUP: return IN_VOLUP;
	case JOY_VOLDN: return IN_VOLDN;
	default: return IN_NONE;
	}
}

static void poll_raw_fd(int fd, in_state *st)
{
#ifdef __linux__
	struct input_event ev;
	while (fd >= 0 && read(fd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
		if (ev.type != EV_KEY) continue;
		if (dbg_input)
			fprintf(stderr, "[in] raw fd=%d code=%d val=%d\n", fd, ev.code, ev.value);
		in_button b = IN_NONE;
		if (ev.code == KEY_POWER) b = IN_POWER;
		else if (ev.code == KEY_VOLUMEUP) b = IN_VOLUP;
		else if (ev.code == KEY_VOLUMEDOWN) b = IN_VOLDN;
		else if (ev.code == CODE_FN_RIGHT) b = IN_BRIGHTUP;
		else if (ev.code == CODE_FN_LEFT) b = IN_BRIGHTDN;
		if (b != IN_NONE && ev.value != 2)
			set_btn(st, b, ev.value == 1);
	}
#else
	(void)fd; (void)st;
#endif
}

/* keyboard fallback so the native dev build is drivable */
static in_button map_key(SDL_Keycode k)
{
	switch (k) {
	case SDLK_LEFT: return IN_LEFT;
	case SDLK_RIGHT: return IN_RIGHT;
	case SDLK_UP: return IN_UP;
	case SDLK_DOWN: return IN_DOWN;
	case SDLK_RETURN: return IN_ACCEPT;
	case SDLK_ESCAPE: return IN_BACK;
	case SDLK_BACKSPACE: return IN_BACK;
	default: return IN_NONE;
	}
}

void plat_input_poll(in_state *st)
{
	memset(st->pressed, 0, sizeof st->pressed);
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			st->quit_requested = true;
			break;
		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP:
			if (dbg_input && e.type == SDL_JOYBUTTONDOWN)
				fprintf(stderr, "[in] joy button=%d\n", e.jbutton.button);
			set_btn(st, map_joy_button(e.jbutton.button),
			        e.type == SDL_JOYBUTTONDOWN);
			break;
		case SDL_JOYHATMOTION: {
			Uint8 v = e.jhat.value;
			set_btn(st, IN_LEFT,  (v & SDL_HAT_LEFT)  != 0);
			set_btn(st, IN_RIGHT, (v & SDL_HAT_RIGHT) != 0);
			set_btn(st, IN_UP,    (v & SDL_HAT_UP)    != 0);
			set_btn(st, IN_DOWN,  (v & SDL_HAT_DOWN)  != 0);
			break;
		}
		case SDL_JOYDEVICEADDED:
			if (!joy) open_joystick();
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			if (e.key.repeat) break;
			set_btn(st, map_key(e.key.keysym.sym), e.type == SDL_KEYDOWN);
			if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_q)
				st->quit_requested = true;
			break;
		}
	}
	poll_raw_fd(fd_power, st);
	poll_raw_fd(fd_keys, st);
	poll_raw_fd(fd_joy, st);
	/* dev escape hatch while iterating over ssh */
	if (st->down[IN_MENU] && st->down[IN_START])
		st->quit_requested = true;
}

bool in_repeat(in_state *st, in_button b)
{
	if (st->pressed[b]) return true;
	if (!st->down[b]) return false;
	Uint32 now = SDL_GetTicks();
	if (now - st->down_since[b] < REPEAT_DELAY_MS) return false;
	if (now - st->last_repeat[b] < REPEAT_RATE_MS) return false;
	st->last_repeat[b] = now;
	return true;
}

/* Whether the last plat_run() ended because the player hit power rather than
 * because the child chose to exit. The launcher owns the shutdown sequence --
 * GAME OVER on the set, then halt -- so it has to be able to tell the two
 * apart when the child returns. */
static bool run_power_pressed;

bool plat_run_power_pressed(void) { return run_power_pressed; }

int plat_run(char *const argv[], const char *const envkv[], const char *workdir)
{
	pid_t pid;
	run_power_pressed = false;
	pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		if (workdir) {
			if (chdir(workdir) != 0) { /* still try to run */ }
		}
		for (int i = 0; envkv && envkv[i]; i++)
			putenv((char *)envkv[i]);
		execv(argv[0], argv);
		fprintf(stderr, "execv %s: failed\n", argv[0]);
		_exit(127);
	}
	int status = 0;
#ifdef __linux__
	/* Escape hatch: a core that dead-ends (e.g. a bad/mismatched romset)
	 * leaves the child showing an error screen that eats no input. Watch
	 * the power button while waiting and kill the child on a press. */
	struct input_event ev;
	while (fd_power >= 0 && read(fd_power, &ev, sizeof ev) == (ssize_t)sizeof ev)
		; /* drain stale events */
	int ms_since_term = -1;
	for (;;) {
		pid_t r = waitpid(pid, &status, WNOHANG);
		if (r == pid) break;
		if (r < 0) return -1;
		usleep(100 * 1000);
		while (fd_power >= 0 && read(fd_power, &ev, sizeof ev) == (ssize_t)sizeof ev) {
			if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value == 1 &&
			    ms_since_term < 0) {
				run_power_pressed = true;
				kill(pid, SIGTERM);
				ms_since_term = 0;
			}
		}
		if (ms_since_term >= 0) {
			ms_since_term += 100;
			if (ms_since_term > 3000) {
				kill(pid, SIGKILL);
				ms_since_term = -1;
			}
		}
	}
#else
	if (waitpid(pid, &status, 0) < 0) return -1;
#endif
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* ---- resident minarch ---------------------------------------------------
 * A resident minarch holds the GL context and the loaded core between games,
 * which takes a launch from ~1100ms to ~200ms. The launcher talks to it over
 * two fifos rather than forking it per game.
 *
 * Both are opened O_RDWR and held: a fifo opened for reading alone blocks
 * until a writer appears and returns EOF when the last one leaves, which
 * turns a request/reply into a race that deadlocks whichever side opens
 * first. Holding a write descriptor of our own removes both behaviours. */
#define CTR_PID "/tmp/contrarian_res.pid"
#define CTR_REQ "/tmp/contrarian_req"
#define CTR_REP "/tmp/contrarian_rep"

/* The resident process writes its pid here at startup; it is how the power
 * button reaches the running game. */
static pid_t resident_pid(void)
{
	FILE *f = fopen(CTR_PID, "r");
	long v = 0;
	if (!f) return -1;
	if (fscanf(f, "%ld", &v) != 1) v = 0;
	fclose(f);
	return v > 0 ? (pid_t)v : -1;
}

bool plat_resident_ready(void)
{
	struct stat st;
	return stat(CTR_REQ, &st) == 0 && S_ISFIFO(st.st_mode) &&
	       stat(CTR_REP, &st) == 0 && S_ISFIFO(st.st_mode);
}

/* Hand a game to the resident minarch and block until it says the game is
 * over, watching the power button throughout -- the same job plat_run does
 * for the one-shot binary, except the signal ends the GAME rather than the
 * process, so residency survives to serve the next launch.
 *
 * Returns true if it ran, false if the resident process did not answer (the
 * caller then falls back to running minarch the old way). */
static int res_fd_rep = -1;

/* Ask for a game. Returns immediately -- the caller can draw its launch
 * animation while the game loads, which is most of what the launch costs. */
bool plat_resident_send(const char *req_line)
{
	int fd_req;
	char c;

	run_power_pressed = false;
	if (res_fd_rep >= 0) { close(res_fd_rep); res_fd_rep = -1; }

	fd_req = open(CTR_REQ, O_WRONLY | O_NONBLOCK);
	if (fd_req < 0) return false;              /* nobody listening */
	res_fd_rep = open(CTR_REP, O_RDWR | O_NONBLOCK);
	if (res_fd_rep < 0) { close(fd_req); return false; }

	/* Drop anything left over from a previous game before asking for one. */
	while (read(res_fd_rep, &c, 1) == 1) { }

	if (write(fd_req, req_line, strlen(req_line)) < 0) {
		close(fd_req); close(res_fd_rep); res_fd_rep = -1;
		return false;
	}
	close(fd_req);
	return true;
}

/* Block until the game is over, watching the power button throughout -- the
 * same job plat_run does for the one-shot binary, except the signal ends the
 * GAME rather than the process, so residency survives the next launch. */
bool plat_resident_wait(void)
{
	int fd_rep = res_fd_rep;
	bool ok = false;
	char c;

	if (fd_rep < 0) return false;

#ifdef __linux__
	{
		struct input_event ev;
		int sent_stop = 0;
		pid_t res = resident_pid();

		while (fd_power >= 0 && read(fd_power, &ev, sizeof ev) == (ssize_t)sizeof ev)
			; /* drain stale presses */

		for (;;) {
			if (read(fd_rep, &c, 1) == 1) { ok = true; break; }
			usleep(20 * 1000);
			while (fd_power >= 0 &&
			       read(fd_power, &ev, sizeof ev) == (ssize_t)sizeof ev) {
				if (ev.type == EV_KEY && ev.code == KEY_POWER &&
				    ev.value == 1 && !sent_stop) {
					run_power_pressed = true;
					sent_stop = 1;
					/* End the game, not the process. */
					if (res > 0) kill(res, SIGUSR1);
				}
			}
			if (res > 0 && kill(res, 0) != 0) break;   /* it died */
		}
	}
#endif
	close(fd_rep);
	res_fd_rep = -1;
	return ok;
}

void plat_request_poweroff(void)
{
	FILE *f = fopen(CTR_POWEROFF_FLAG, "w");
	if (f) fclose(f);
}

#ifdef __linux__
static void write_str(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY);
	if (fd < 0) return;
	if (write(fd, val, strlen(val)) < 0) { /* best effort */ }
	close(fd);
}
#endif

void plat_leds_off(void)
{
#ifdef __linux__
	/* stop the animation engine, then scale all groups to zero */
	write_str("/sys/class/led_anim/effect_enable", "0");
	static const char *groups[] = { "l", "r", "lr", "m", "f1", "f2" };
	char path[96];
	for (size_t i = 0; i < sizeof groups / sizeof *groups; i++) {
		snprintf(path, sizeof path, "/sys/class/led_anim/effect_rgb_hex_%s", groups[i]);
		write_str(path, "000000 ");
	}
	write_str("/sys/class/led_anim/max_scale", "0");
	write_str("/sys/class/led_anim/max_scale_lr", "0");
	write_str("/sys/class/led_anim/max_scale_f1f2", "0");
	/* and zero every raw channel directly (23 leds x r/g/b) */
	for (int n = 0; n < 23; n++) {
		for (const char *c = "rgb"; *c; c++) {
			snprintf(path, sizeof path,
			         "/sys/class/leds/sunxi_led%d%c/brightness", n, *c);
			write_str(path, "0");
		}
	}
#endif
}

/* ---- volume + brightness via libmsettings (dlopen'd at runtime) ---- */

static void (*ms_init)(void);
static int (*ms_get_vol)(void);
static void (*ms_set_vol)(int);
static int (*ms_get_bright)(void);
static void (*ms_set_bright)(int);

void plat_settings_init(void)
{
	void *h = dlopen("libmsettings.so", RTLD_NOW | RTLD_GLOBAL);
	if (!h) {
		fprintf(stderr, "libmsettings: %s\n", dlerror());
		return;
	}
	ms_init = dlsym(h, "InitSettings");
	ms_get_vol = dlsym(h, "GetVolume");
	ms_set_vol = dlsym(h, "SetVolume");
	ms_get_bright = dlsym(h, "GetBrightness");
	ms_set_bright = dlsym(h, "SetBrightness");
	if (ms_init) ms_init();
}

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

/* OSD: a thin white line across the very top on any volume/brightness change,
 * drawn by plat_draw_osd() just before present. Same look as Muse's play bar
 * (6px, faint track + near-white fill) and the in-game minarch line, so
 * volume/brightness feedback is one consistent thing across the whole system.
 * No glyph, no text -- you already know which button you pressed. */
#define OSD_LINE_H     6    /* == Muse play bar */
#define OSD_PAD        3    /* transparent-black scrim above + below (50%) */
#define OSD_WINDOW_MS  900  /* visible this long after the last change */

static int    osd_kind = 0; /* 0 none, 1 brightness, 2 volume */
static int    osd_val = 0, osd_max = 1;
static Uint32 osd_shown_at = 0;

/* Show the OSD line at an explicit level. Passing the value in (rather than
 * reading libmsettings here) lets the Muse client drive the line from the
 * daemon-reported level without itself owning the settings device. */
void plat_osd_show(int kind, int val, int max)
{
	osd_kind = kind;
	osd_max = max > 0 ? max : 1;
	osd_val = val < 0 ? 0 : (val > osd_max ? osd_max : val);
	osd_shown_at = SDL_GetTicks();
}

int plat_volume_get(void)     { return ms_get_vol ? ms_get_vol() : -1; }
int plat_brightness_get(void) { return ms_get_bright ? ms_get_bright() : -1; }

void plat_draw_osd(SDL_Renderer *r)
{
	if (!osd_kind) return;
	if (SDL_GetTicks() - osd_shown_at > OSD_WINDOW_MS) { osd_kind = 0; return; }

	float pct = (float)osd_val / osd_max;
	if (pct < 0) pct = 0; else if (pct > 1) pct = 1;

	int W = CTR_SCREEN_W;
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(r, 0, 0, 0, 128);                 /* scrim */
	SDL_RenderFillRect(r, &(SDL_Rect){ 0, 0, W, OSD_LINE_H + OSD_PAD * 2 });
	SDL_SetRenderDrawColor(r, 60, 62, 72, 255);              /* faint track */
	SDL_RenderFillRect(r, &(SDL_Rect){ 0, OSD_PAD, W, OSD_LINE_H });
	SDL_SetRenderDrawColor(r, 235, 235, 240, 255);           /* near-white fill */
	SDL_RenderFillRect(r, &(SDL_Rect){ 0, OSD_PAD, (int)(W * pct), OSD_LINE_H });
}

void plat_volume_nudge(int delta)
{
	if (!ms_get_vol || !ms_set_vol) return;
	int v = clampi(ms_get_vol() + delta, 0, 20);
	ms_set_vol(v);
	plat_osd_show(2, v, 20);
	fprintf(stderr, "volume -> %d/20\n", v);
}

void plat_brightness_nudge(int delta)
{
	if (!ms_get_bright || !ms_set_bright) return;
	int v = clampi(ms_get_bright() + delta, 0, 10);
	ms_set_bright(v);
	plat_osd_show(1, v, 10);
	fprintf(stderr, "brightness -> %d/10\n", v);
}

void plat_volume_set_pct(int pct)
{
	if (!ms_set_vol) return;
	ms_set_vol(clampi((pct * 20 + 50) / 100, 0, 20));
}

void plat_brightness_set(int level)
{
	if (!ms_set_bright) return;
	ms_set_bright(clampi(level, 0, 10));
}

/* ---- battery ---- */

/* Fill *pct (0..100) and *charging from the AXP2202 fuel gauge. Returns false
 * when no reading is available (e.g. the native dev build). CTR_FAKE_BATT
 * forces a level for testing the low-battery indicator. */
bool plat_battery(int *pct, bool *charging)
{
	const char *fake = getenv("CTR_FAKE_BATT");
	if (fake && *fake) {
		if (pct) *pct = atoi(fake);
		if (charging) *charging = false;
		return true;
	}
#ifdef __linux__
	FILE *f = fopen("/sys/class/power_supply/axp2202-battery/capacity", "r");
	if (!f) return false;
	int v = -1;
	if (fscanf(f, "%d", &v) != 1) v = -1;
	fclose(f);
	if (v < 0) return false;
	if (pct) *pct = v;
	if (charging) {
		*charging = false;
		FILE *s = fopen("/sys/class/power_supply/axp2202-battery/status", "r");
		if (s) {
			char st[32] = { 0 };
			if (fgets(st, sizeof st, s) &&
			    (strncmp(st, "Charging", 8) == 0 || strncmp(st, "Full", 4) == 0))
				*charging = true;
			fclose(s);
		}
	}
	return true;
#else
	(void)pct;
	(void)charging;
	return false;
#endif
}
