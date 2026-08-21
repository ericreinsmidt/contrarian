/* SPDX-License-Identifier: GPL-3.0-only
 *
 * Copied from NextUI (GPL-3.0) and modified for Contrarian.
 * Modification: capture one frame a few seconds into a game's first run and
 * save it as that ROM's card face. Contrarian's Cover Flow shows each version's
 * own title screen, and this is where that image comes from -- captured on
 * device, from the user's own ROM, so no artwork is ever shipped.
 * Enabled only when CONTRARIAN_FACE names a path that does not exist yet.
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <msettings.h>

#include <SDL2/SDL_image.h>

#include "notification.h"
#include "ra_integration.h"

#include "ma_internal.h"
#include "ma_cheats.h"
#include "ma_audio.h"
#include "ma_input.h"
#include "ma_options.h"
#include "ma_frontend_opts.h"
#include "ma_saves.h"
#include "ma_video.h"
#include "ma_core.h"
#include "ma_game.h"
#include "ma_environment.h"
#include "ma_config.h"
#include "ma_runframe.h"

///////////////////////////////////////

SDL_Surface* screen;
int quit = 0;
int newScreenshot = 0;
int show_menu = 0;
int simple_mode = 0;
enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;

// default frontend options
int screen_scaling = SCALE_ASPECT;
int resampling_quality = 2;
int ambient_mode = 0;
int screen_sharpness = SHARPNESS_SOFT;
int screen_effect = EFFECT_NONE;
int cfg_screenx = 64;
int cfg_screeny = 64;
int overlay = 0; 
int use_core_fps = 0;
int sync_ref = 0;
int show_debug = 0;
int max_ff_speed = 3; // 4x
int ff_audio = 0;
int fast_forward = 0;
int rewind_pressed = 0;
int rewind_toggle = 0;
int last_rewind_pressed = 0;
int ff_toggled = 0;
int ff_hold_active = 0;
int ff_paused_by_rewind_hold = 0;
int rewinding = 0;
int rewind_cfg_enable = MINARCH_DEFAULT_REWIND_ENABLE;
int rewind_cfg_buffer_mb = MINARCH_DEFAULT_REWIND_BUFFER_MB;
int rewind_cfg_granularity = MINARCH_DEFAULT_REWIND_GRANULARITY;
int rewind_cfg_audio = MINARCH_DEFAULT_REWIND_AUDIO;
int rewind_cfg_compress = 1;
int rewind_cfg_lz4_acceleration = MINARCH_DEFAULT_REWIND_LZ4_ACCELERATION;
int rewind_init_ready = 0; // gate Rewind_init from syncFrontend until startup is past Core_load
int overclock = 0; // auto
int has_custom_controllers = 0;
int gamepad_type = 0; // index in gamepad_labels/gamepad_values

// these are no longer constants as of the RG CubeXX (even though they look like it)
int DEVICE_WIDTH = 0;
int DEVICE_HEIGHT = 0;
int DEVICE_PITCH = 0;
int shader_reset_suppressed = 0;

GFX_Renderer renderer;

///////////////////////////////////////

struct Core core;



///////////////////////////////
static struct Special {
	int palette_updated;
} special;
void Special_updatedDMGPalette(int frames) {
	// LOG_info("Special_updatedDMGPalette(%i)\n", frames);
	special.palette_updated = frames; // must wait a few frames
}
static void Special_refreshDMGPalette(void) {
	special.palette_updated -= 1;
	if (special.palette_updated>0) return;
	
	int rgb = getInt("/tmp/dmg_grid_color");
	GFX_setEffectColor(rgb);
}
static void Special_init(void) {
	if (special.palette_updated>1) special.palette_updated = 1;
	// else if (exactMatch((char*)core.tag, "GBC"))  {
	// 	putInt("/tmp/dmg_grid_color",0xF79E);
	// 	special.palette_updated = 1;
	// }
}
void Special_render(void) {
	if (special.palette_updated) Special_refreshDMGPalette();
}
static void Special_quit(void) {
	system("rm -f /tmp/dmg_grid_color");
}
///////////////////////////////

///////////////////////////////

void hdmimon(void) {
	// handle HDMI change
	static int had_hdmi = -1;
	int has_hdmi = GetHDMI();
	if (had_hdmi==-1) had_hdmi = has_hdmi;
	if (has_hdmi!=had_hdmi) {
		had_hdmi = has_hdmi;

		LOG_info("restarting after HDMI change...\n");
		Menu_beforeSleep();
		sleep(4);
		show_menu = 0;
		quit = 1;
	}
}

#define PWR_UPDATE_FREQ 5
#define PWR_UPDATE_FREQ_INGAME 20

/* ---- Contrarian: card-face capture ------------------------------------- */
/* Grab the framebuffer once, a few seconds in, which for Contra and every hack
 * of it lands on the title screen -- the one screen that is always different
 * between versions, and so the most informative possible card face. */
#define CTR_FACE_FRAME 300

/* ---- Contrarian: volume and brightness in game ------------------------- */
/* NextUI does not change either of these itself. PWR_update() only decides
 * which OSD to show and leaves the actual adjustment to keymon.elf, the
 * background daemon that owns the keys system-wide. Contrarian does not run
 * keymon -- the launcher adjusts them in process instead -- so the moment
 * minarch took the screen, volume and brightness went dead.
 *
 * The buttons are all ones NextUI's own brick mapping already knows about, so
 * this needs no raw evdev: the volume rocker is BTN_PLUS/BTN_MINUS, and the
 * front F1/F2 keys arrive on the gamepad node as BTN_THUMBL/BTN_THUMBR, which
 * that mapping calls BTN_L3/BTN_R3 (joystick buttons 9 and 10 on brick).
 *
 * No OSD call is needed: the poll further down this file already watches
 * GetVolume()/GetBrightness() and raises the system indicator when they move.
 * Ranges are msettings' own -- volume 0-20, brightness 0-10. */
static void ctr_system_keys(void) {
	int v;
	if (PAD_justRepeated(BTN_PLUS))  { v = GetVolume();     if (v < 20) SetVolume(v + 1); }
	if (PAD_justRepeated(BTN_MINUS)) { v = GetVolume();     if (v > 0)  SetVolume(v - 1); }
	if (PAD_justRepeated(BTN_R3))    { v = GetBrightness(); if (v < 10) SetBrightness(v + 1); }
	if (PAD_justRepeated(BTN_L3))    { v = GetBrightness(); if (v > 0)  SetBrightness(v - 1); }
}

static void ctr_capture_face(void) {
	const char* path = getenv("CONTRARIAN_FACE");
	int cw = 0, ch = 0;
	unsigned char* pixels;
	SDL_Surface* raw;

	if (!path || !*path) return;
	if (access(path, F_OK) == 0) return;   /* already developed */

	pixels = GFX_GL_screenCapture(&cw, &ch);
	if (!pixels) return;
	if (cw > 0 && ch > 0) {
		raw = SDL_CreateRGBSurfaceWithFormatFrom(pixels, cw, ch, 32, cw * 4,
		                                         SDL_PIXELFORMAT_ABGR8888);
		if (raw) {
			/* Crop to the viewport. The grab is the whole framebuffer, which
			 * now includes the television cabinet drawn around the game --
			 * saving that would put a picture of the set inside the set. */
			const char* vp = getenv("CONTRARIAN_VIEWPORT");
			int vx, vy, vw, vh;
			SDL_Surface* out = raw;
			SDL_Surface* crop = NULL;
			if (vp && sscanf(vp, "%d,%d,%d,%d", &vx, &vy, &vw, &vh) == 4 &&
			    vw > 0 && vh > 0 && vx + vw <= cw && vy + vh <= ch) {
				crop = SDL_CreateRGBSurfaceWithFormat(0, vw, vh, 32,
				                                      SDL_PIXELFORMAT_ABGR8888);
				if (crop) {
					SDL_Rect r = { vx, vy, vw, vh };
					SDL_BlitSurface(raw, &r, crop, NULL);
					out = crop;
				}
			}
			if (IMG_SavePNG(out, path) == 0)
				LOG_info("contrarian: card face -> %s (%ix%i)\n",
				         path, out->w, out->h);
			if (crop) SDL_FreeSurface(crop);
			SDL_FreeSurface(raw);
		}
	}
	free(pixels);
}

/* ---- Contrarian: residency -----------------------------------------------
 * Launching a game costs ~1100ms, and almost none of it is the game: the
 * EGL/GL context is ~620ms, the core dlopen ~170ms, audio and settings ~140ms,
 * while opening the ROM itself is ~36ms. All of that except the ROM is the
 * cost of STARTING A PROCESS, so it is paid once, at boot, behind the boot
 * animation -- and every launch after that is just the 36ms.
 *
 * The process sits blocked on a FIFO between games, holding the GL context and
 * the core but nothing exclusive: the audio device is released so the
 * launcher's own sounds keep working (verified on hardware -- SND_quit frees
 * the device while this process stays alive, and SND_init takes it back), and
 * being blocked means it reads no input and draws no frames.
 *
 * The launcher falls back to running this program the old way, one game per
 * process, if the resident one is not answering. That fallback is why the
 * classic path below is still here. */
/* End the running game without ending the process. The launcher's power
 * button watcher used to SIGTERM minarch, which is still right when minarch is
 * one game per process -- but would take residency down with it. SIGUSR1 ends
 * the game only; the process falls back to waiting on the fifo. */
static void ctr_end_game(int sig) { (void)sig; quit = 1; }

static uint32_t ctr_req_ms;      /* when the current request arrived */
/* The real screen surface, as GFX_init returned it. The quit animation at the
 * end of a game reassigns the global `screen` to a temporary surface and then
 * frees it, leaving it dangling -- which no one noticed because the process
 * always exited immediately afterwards. A resident process runs Menu_init()
 * again for the next game, which reads screen->format, so it has to be put
 * back first. */
static SDL_Surface *ctr_screen;

/* Opening the ALSA device costs ~137ms, and it was being paid on every launch
 * because the device was closed between games. It never needed to be: the
 * default playback path here runs through dmix (see /etc/asound.conf), so the
 * launcher can play its own sounds while a game holds the device -- verified
 * by playing through the default device with a game running. The comment this
 * replaces claimed the opposite and was never tested.
 *
 * So the device stays open, paused, between games, and is reopened only when
 * the timing actually changes: SND_init sizes its buffers from the core's
 * sample rate and frame rate, and an NTSC game and a PAL one disagree on the
 * latter. Relaunching the same version reuses the open device. */
extern void ctr_snd_close_device(void);

static bool   ctr_snd_open;
static double ctr_snd_rate, ctr_snd_fps;
static char ctr_core_path[MAX_PATH];
static char ctr_tag[MAX_PATH];

static void resident_init(void)
{
	screen = GFX_init(MODE_MENU);

	/* Startup is dominated by this one call -- SDL video plus the EGL/GL
	 * context, ~620ms of the ~1100ms it takes to reach a running game.
	 * Logged because it is the number that decides whether launching is
	 * ever going to be quick. */
	LOG_info("ma: gfx_init %ims\n", SDL_GetTicks());
	// initialize default shaders
	GFX_initShaders();
	PLAT_initNotificationTexture();

	PAD_init();
	DEVICE_WIDTH = screen->w;
	DEVICE_HEIGHT = screen->h;
	DEVICE_PITCH = screen->pitch;
	// LOG_info("DEVICE_SIZE: %ix%i (%i)\n", DEVICE_WIDTH,DEVICE_HEIGHT,DEVICE_PITCH);
	
	/* The lights are handled in the api.c override, not here: GFX_init()
	 * calls LEDS_initLeds() from inside api.c, so dropping this call would
	 * change nothing. Left in place so this file stays close to NextUI's. */
	LEDS_initLeds();
	VIB_init();
	PWR_init();
	/* The power button belongs to the launcher. It is watching this process
	 * and will terminate it on a press, then show GAME OVER and halt --  but
	 * only if minarch does not get there first. PWR_update() calls
	 * PWR_powerOff() directly on a press, which paints "Powering off" and
	 * cuts power immediately; disabling it makes PWR_powerOff a no-op and
	 * leaves the press for the launcher. Sleep goes with it: BTN_SLEEP is
	 * BTN_POWER on this device, so leaving sleep armed would swallow the
	 * same press. */
	PWR_disablePowerOff();
	PWR_disableSleep();
	MSG_init();
	IMG_Init(IMG_INIT_PNG);
	Core_open(ctr_core_path, ctr_tag);

	LOG_info("ma: core_dlopen %ims\n", SDL_GetTicks());

	/* Once, before any audio: re-running InitSettings per game re-initialises
	 * the codec and pops the speaker on every launch. */
	InitSettings();
	ctr_screen = screen;
}

/* Everything a single game needs, set up and torn back down. Returns 0 when
 * the game ran, -1 if the ROM would not open. */
static int run_one_game(char *rom_path)
{
	/* Restore the screen the previous game's quit animation left dangling. */
	if (ctr_screen) screen = ctr_screen;

	/* Per-game globals. A resident process runs this more than once, so
	 * anything the frame loop accumulates has to start clean. */
	quit = 0; show_menu = 0; newScreenshot = 0;
	fast_forward = 0; ff_toggled = 0; ff_hold_active = 0;
	rewind_pressed = 0; rewind_toggle = 0; last_rewind_pressed = 0;
	rewinding = 0; ff_paused_by_rewind_hold = 0;
	rewind_init_ready = 0; shader_reset_suppressed = 0;

	/* Were statics in NextUI, where the process only ever ran one game. As
	 * locals they reset per game: the face grab fires for each new ROM, and
	 * the -1 sentinel stops a launch from flashing the settings line. */
	int ctr_frames = 0;
	int last_volume = -1, last_brightness = -1, last_colortemp = -1;

	Game_open(rom_path); // nes tries to load gamegenie setting before this returns ffs
	if (!game.is_open) { LOG_warn("could not open %s\n", rom_path); return -1; }
	
	simple_mode = exists(SIMPLE_MODE_PATH);
	
	// restore options
	Config_load(); // before init?
	Config_init();
	Config_readOptions(); // cores with boot logo option (eg. gb) need to load options early
	
	Core_init();

	// Initialize RetroAchievements after core.init() but before Core_load()
	// Set up memory accessors for achievement memory reading
	RA_setMemoryAccessors(core.get_memory_data, core.get_memory_size);
	RA_init();

	// TODO: find a better place to do this
	// mixing static and loaded data is messy
	// why not move to Core_init()?
	Menu_setCoreVersionDesc(core.version);
	Core_load();
	
	Input_init(NULL);
	Config_readOptions(); // but others load and report options later (eg. nes)
	Config_readControls(); // restore controls (after the core has reported its defaults)

	// Mute audio during startup to avoid pops (InitSettings would be logical, but too late)
	SND_overrideMute(1);
	if (ctr_snd_open && core.sample_rate == ctr_snd_rate &&
	    core.fps == ctr_snd_fps) {
		SND_pauseAudio(false);          /* same timing: keep the open device */
	} else {
		if (ctr_snd_open) ctr_snd_close_device();
		SND_init(core.sample_rate, core.fps);
		SND_registerDeviceWatcher(Audio_onSinkChanged);
		ctr_snd_open = true;
		ctr_snd_rate = core.sample_rate;
		ctr_snd_fps  = core.fps;
	}
	LOG_info("ma: snd+settings %ims\n", SDL_GetTicks());
	Menu_init();
	Notification_init();
	
	// Load game for RetroAchievements tracking (must be after Notification_init)
	// Pass ROM data if available, otherwise just path (for cores that load from file)
	{
		char* rom_path_for_ra = game.tmp_path[0] ? game.tmp_path : game.path;
		RA_loadGame(rom_path_for_ra, game.data, game.size, core.tag);
	}
	
	State_resume();
	Menu_initState(); // make ready for state shortcuts

	PWR_disableAutosleep();
	// we dont need five second updates while ingame, and wifi status isnt displayed either
	PWR_updateFrequency(PWR_UPDATE_FREQ, 0); 

	// force a vsync immediately before loop
	// for better frame pacing?
	GFX_clearAll();
	GFX_clearLayers(0);
	GFX_clear(screen);

	// need to draw real black background first otherwise u get weird pixels sometimes

	GFX_flip(screen);

	Special_init(); // after config

	chooseSyncRef();
	
	int has_pending_opt_change = 0;

	// then initialize custom  shaders from settings
	/* Contrarian: syncing the shader options loads and links a program for
	 * every slot regardless of how many are actually in use -- and the slot
	 * default is index 0, which is a real file, not "none". On a game with the
	 * CRT off that was ~89ms of a ~370ms launch spent compiling shaders
	 * nothing would sample. Set the count and skip the sync; applyShaderSettings
	 * below already loops only over active shaders, so it costs nothing here. */
	if (config.shaders.options[SH_NROFSHADERS].value == 0)
		GFX_setShaders(0);
	else
		initShaders();
	Config_readOptions();
	applyShaderSettings();
	int rewind_initialized = Rewind_init(core.serialize_size ? core.serialize_size() : 0);
	rewind_init_ready = 1;  // Mark setup as attempted, even if rewind init failed, so option changes can retry it later.
	if (rewind_initialized && core.serialize_size) Rewind_on_state_change();
	// release config when all is loaded
	Config_free();

	if (ctr_req_ms)
		LOG_info("resident: game up in %ums\n", SDL_GetTicks() - ctr_req_ms);
	else
		LOG_info("total startup time %ims\n\n",SDL_GetTicks());
	
	// we started in performance mode, now reset to the desired mode
	// if the config didn't specify the desired cpu speed, the default is 0 = auto
	setOverclock(overclock);

	while (!quit) {
		GFX_startFrame();

		run_frame();

		/* Contrarian: one-shot card-face capture. Counted in frames rather
		 * than milliseconds so it lands at the same point in the game on both
		 * NTSC and PAL builds. */
		{
			if (++ctr_frames == CTR_FACE_FRAME) ctr_capture_face();
		}

		/* Volume and brightness. run_frame() has just polled the pad, so the
		 * button state read here is this frame's. */
		ctr_system_keys();
		
		// Process RetroAchievements for this frame
		RA_doFrame();
		
		// Update and render notifications overlay
		Notification_update(SDL_GetTicks());
		
		// Poll for volume/brightness/colortemp changes and show system indicators
		{
			
			int cur_volume = GetVolume();
			int cur_brightness = GetBrightness();
			int cur_colortemp = GetColortemp();
			
			if (last_volume == -1) {
				// First frame - just initialize cached values, don't show indicator
				last_volume = cur_volume;
				last_brightness = cur_brightness;
				last_colortemp = cur_colortemp;
			} else {
				/* Contrarian: these raise the launcher's thin top line, not
				 * NextUI's corner pill -- render_system_indicator is overridden
				 * in common/notification.c. Colortemp is tracked but never
				 * shown; Contrarian does not change it. */
				if (cur_volume != last_volume) {
					last_volume = cur_volume;
					Notification_showSystemIndicator(SYSTEM_INDICATOR_VOLUME);
				}
				if (cur_brightness != last_brightness) {
					last_brightness = cur_brightness;
					Notification_showSystemIndicator(SYSTEM_INDICATOR_BRIGHTNESS);
				}
				last_colortemp = cur_colortemp;
			}
		}
		
		Notification_renderToLayer(5);  // Always call - handles cleanup when inactive

		if (has_pending_opt_change) {
			has_pending_opt_change = 0;
			if (Core_updateAVInfo()) {
				LOG_info("AV info changed, reset sound system");
				SND_resetAudio(core.sample_rate, core.fps);
			}
			chooseSyncRef();
		}

		if (show_menu) {
			PWR_updateFrequency(PWR_UPDATE_FREQ,1);
			Menu_loop();
			// Process RA async operations while menu is shown
			RA_idle();
			PWR_updateFrequency(PWR_UPDATE_FREQ_INGAME,0);
			has_pending_opt_change = config.core.changed;
			chooseSyncRef();
		}

		Audio_checkAndResetIfNeeded();

		hdmimon();
	}
	int cw, ch;
	unsigned char* pixels = GFX_GL_screenCapture(&cw, &ch);
	
	renderer.dst = pixels;
	SDL_Surface* rawSurface = SDL_CreateRGBSurfaceWithFormatFrom(
		pixels, cw, ch, 32, cw * 4, SDL_PIXELFORMAT_ABGR8888
	);
	SDL_Surface* converted = SDL_ConvertSurfaceFormat(rawSurface, screen->format->format, 0);
	screen = converted;
	SDL_FreeSurface(rawSurface);
	free(pixels); 
	GFX_animateSurfaceOpacity(converted, 0, 0, cw, ch, 255, 0, CFG_getMenuTransitions() ? 200 : 20, 1);
	SDL_FreeSurface(converted); 
	
	Video_cleanup();

	PLAT_clearTurbo();

	Menu_quit();
	/* QuitSettings() used to sit here. It belongs to the process, not to one
	 * game: settings are initialised once at residency init (re-running
	 * InitSettings per game re-inits the codec and pops the speaker), so
	 * tearing them down after every game would leave the next one without
	 * any. Moved to resident_quit(). */


	/* Per-game teardown. The GL context, the pad, the loaded core object and
	 * the settings all stay -- that is the entire point. Menu_quit is called
	 * here although NextUI never calls it: one game per process does not need
	 * to, and this does. */
	RA_unloadGame();
	RA_quit();
	Notification_quit();
	Game_close();
	Rewind_free();
	Core_unload();
	Core_quit();
	Config_quit();
	Special_quit();
	/* Paused, not closed: the launcher's sounds mix alongside it, and the next
	 * launch of the same version skips the ~137ms device open. */
	SND_pauseAudio(true);
	return 0;
}

static void resident_quit(void)
{
	if (ctr_snd_open) { SND_quit(); ctr_snd_open = false; }
	QuitSettings();
	Core_close();
	MSG_quit();
	PWR_quit();
	VIB_quit();
	SND_removeDeviceWatcher();
	PAD_quit();
	GFX_quit();
	Menu_waitScreenshot();
}

/* The request the launcher sends per game. Everything that used to arrive as
 * an environment variable has to come down the pipe instead -- a running
 * process cannot have its environment changed from outside -- so it is set
 * with setenv() here and every existing getenv() call site keeps working,
 * including the viewport lookup over in common/generic_video.c. */
#define CTR_PID "/tmp/contrarian_res.pid"
#define CTR_REQ "/tmp/contrarian_req"
#define CTR_REP "/tmp/contrarian_rep"

static void ctr_apply_request(char *line, char *rom, size_t rom_n)
{
	char *face, *view;

	rom[0] = 0;
	face = strchr(line, '\t');
	if (face) *face++ = 0;
	view = face ? strchr(face, '\t') : NULL;
	if (view) *view++ = 0;

	snprintf(rom, rom_n, "%s", line);
	if (face && *face) setenv("CONTRARIAN_FACE", face, 1);
	else               unsetenv("CONTRARIAN_FACE");
	if (view && *view) setenv("CONTRARIAN_VIEWPORT", view, 1);
}

/* A fifo opened for reading alone blocks until a writer appears, and returns
 * EOF the moment the last writer leaves -- which turns a simple request/reply
 * into a race that deadlocks whichever side opens first. Holding a write
 * descriptor of our own removes both behaviours: the read just blocks until a
 * line arrives, for as long as this process lives. Both ends do the same. */
static int ctr_fifo_open(const char *path)
{
	unlink(path);
	if (mkfifo(path, 0666) != 0) return -1;
	return open(path, O_RDWR);
}

static bool ctr_read_line(int fd, char *out, size_t n)
{
	size_t i = 0;
	char c;
	while (i + 1 < n) {
		ssize_t r = read(fd, &c, 1);
		if (r <= 0) return false;
		if (c == '\n') break;
		out[i++] = c;
	}
	out[i] = 0;
	return true;
}

static int resident_loop(void)
{
	char line[MAX_PATH * 3], rom[MAX_PATH];
	int fd_req, fd_rep;

	signal(SIGUSR1, ctr_end_game);

	{	/* Only one of these may run: two of them race to create the fifos and
		 * unlink each other's pidfile, which leaves the launcher unable to
		 * reach the game that is actually playing. launch.sh guards with
		 * pgrep, but that races against a dying process, so refuse here too. */
		FILE *pf = fopen(CTR_PID, "r");
		if (pf) {
			long other = 0;
			if (fscanf(pf, "%ld", &other) != 1) other = 0;
			fclose(pf);
			if (other > 0 && other != (long)getpid() &&
			    kill((pid_t)other, 0) == 0) {
				LOG_info("resident: %ld already running, exiting\n", other);
				return EXIT_SUCCESS;
			}
		}
		pf = fopen(CTR_PID, "w");
		if (pf) { fprintf(pf, "%ld\n", (long)getpid()); fclose(pf); }
	}

	fd_req = ctr_fifo_open(CTR_REQ);
	fd_rep = ctr_fifo_open(CTR_REP);
	if (fd_req < 0 || fd_rep < 0) {
		LOG_error("resident: cannot create fifos\n");
		return EXIT_FAILURE;
	}

	resident_init();
	LOG_info("resident: ready in %ims, waiting for a game\n", SDL_GetTicks());

	for (;;) {
		/* Blocks here between games: no frames, no input, no audio device. */
		if (!ctr_read_line(fd_req, line, sizeof line)) break;
		if (!strcmp(line, "exit")) break;
		if (!line[0]) continue;

		ctr_req_ms = SDL_GetTicks();
		ctr_apply_request(line, rom, sizeof rom);
		if (!rom[0]) continue;

		LOG_info("resident: loading %s\n", rom);
		run_one_game(rom);
		ctr_req_ms = 0;

		/* Leave the screen black rather than on the game's last frame: the
		 * launcher plays its own set-switching-off animation next. */
		GFX_clearAll();
		GFX_clear(screen);
		GFX_flip(screen);

		if (write(fd_rep, "done\n", 5) != 5)
			LOG_error("resident: reply failed\n");
		LOG_info("resident: idle again, waiting\n");
	}

	resident_quit();
	close(fd_req); close(fd_rep);
	{	/* Leave another process's files alone. */
		FILE *pf = fopen(CTR_PID, "r");
		long owner = 0;
		if (pf) { if (fscanf(pf, "%ld", &owner) != 1) owner = 0; fclose(pf); }
		if (owner == (long)getpid()) {
			unlink(CTR_REQ); unlink(CTR_REP); unlink(CTR_PID);
		}
	}
	return EXIT_SUCCESS;
}

int main(int argc , char* argv[]) {
	if (argc < 2)
		return EXIT_FAILURE;

	PWR_setCPUSpeed(CPU_SPEED_PERFORMANCE); // start in performance mode for fast loading
	PWR_pinToCores(CPU_CORE_PERFORMANCE); // thread affinity

	/* Resident: minarch.elf --resident <core>. Stays up, one game after
	 * another, so only the first pays for the GL context and the core. */
	if (!strcmp(argv[1], "--resident")) {
		if (argc < 3) return EXIT_FAILURE;
		snprintf(ctr_core_path, sizeof ctr_core_path, "%s", argv[2]);
		{
			/* Core_open honours CONTRARIAN_TAG and ignores this, but there is
			 * no filename to infer a tag from in resident mode, so give it a
			 * sane one rather than an empty string. */
			const char *t = getenv("CONTRARIAN_TAG");
			snprintf(ctr_tag, sizeof ctr_tag, "%s", (t && *t) ? t : "Contra");
		}
		return resident_loop();
	}

	/* Classic: minarch.elf <core> <rom>, one game per process. Kept because
	 * it is the launcher's fallback when residency is unavailable. */
	if (argc < 3) return EXIT_FAILURE;
	snprintf(ctr_core_path, sizeof ctr_core_path, "%s", argv[1]);
	getEmuName(argv[2], ctr_tag);
	LOG_info("rom_path: %s\n", argv[2]);

	resident_init();
	{
		char rom[MAX_PATH];
		snprintf(rom, sizeof rom, "%s", argv[2]);
		run_one_game(rom);
	}
	resident_quit();
	return EXIT_SUCCESS;
}
