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

int main(int argc , char* argv[]) {
	//static char asoundpath[MAX_PATH];
	//sprintf(asoundpath, "%s/.asoundrc", getenv("HOME"));
	//LOG_info("minarch: need asoundrc at %s\n", asoundpath);
	//if(exists(asoundpath))
	//	LOG_info("asoundrc exists at %s\n", asoundpath);
	//else 
	//	LOG_info("asoundrc does not exist at %s\n", asoundpath);

	if(argc < 2)
		return EXIT_FAILURE;

	PWR_setCPUSpeed(CPU_SPEED_PERFORMANCE); // start in performance mode for fast loading
	PWR_pinToCores(CPU_CORE_PERFORMANCE); // thread affinity

	char core_path[MAX_PATH];
	char rom_path[MAX_PATH];
	char tag_name[MAX_PATH];

	strcpy(core_path, argv[1]);
	strcpy(rom_path, argv[2]);
	getEmuName(rom_path, tag_name);
	
	LOG_info("rom_path: %s\n", rom_path);
	
	screen = GFX_init(MODE_MENU);

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
	Core_open(core_path, tag_name);

	Game_open(rom_path); // nes tries to load gamegenie setting before this returns ffs
	if (!game.is_open) goto finish;
	
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
	SND_init(core.sample_rate, core.fps);
	SND_registerDeviceWatcher(Audio_onSinkChanged);
	InitSettings(); // after we initialize audio
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
	initShaders();
	Config_readOptions();
	applyShaderSettings();
	int rewind_initialized = Rewind_init(core.serialize_size ? core.serialize_size() : 0);
	rewind_init_ready = 1;  // Mark setup as attempted, even if rewind init failed, so option changes can retry it later.
	if (rewind_initialized && core.serialize_size) Rewind_on_state_change();
	// release config when all is loaded
	Config_free();

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
			static int ctr_frames = 0;
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
			static int last_volume = -1;
			static int last_brightness = -1;
			static int last_colortemp = -1;
			
			int cur_volume = GetVolume();
			int cur_brightness = GetBrightness();
			int cur_colortemp = GetColortemp();
			
			if (last_volume == -1) {
				// First frame - just initialize cached values, don't show indicator
				last_volume = cur_volume;
				last_brightness = cur_brightness;
				last_colortemp = cur_colortemp;
			} else {
				/* Contrarian: track the values, show nothing. NextUI's pill is
				 * the one piece of another firmware's chrome that was still
				 * appearing on the television, and it does not belong on it --
				 * same reason the button hints came out of the launcher. */
				last_volume = cur_volume;
				last_brightness = cur_brightness;
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
	QuitSettings();

finish:
    Perf_setCPUMonitorEnabled(0);

	// Unload game and shutdown RetroAchievements before Notification_quit —
	// RA background threads (sync, badge downloads) may call notification
	// APIs, so the notification mutex should outlive all RA threads.
	RA_unloadGame();
	RA_quit();
	Notification_quit();
	
	Game_close();
	Rewind_free();
	Core_unload();
	Core_quit();
	Core_close();
	Config_quit();
	Special_quit();
	MSG_quit();
	PWR_quit();
	VIB_quit();
	SND_removeDeviceWatcher();
	// Disabling this is a dumb hack for bluetooth, we should really be using 
	// bluealsa with --keep-alive=-1 - but SDL wont reconnect the stream on next start.
	// Reenable as soon as we have a more recent SDL available, if ever.
	//SND_quit();
	PAD_quit();
	GFX_quit();
	Menu_waitScreenshot();
	return EXIT_SUCCESS;
}
