/* SPDX-License-Identifier: 0BSD
 *
 * Contrarian -- a CFW that plays exactly one game.
 *
 * Boot, scan the card, present every version of NES Contra it can find.
 * Everything else is shown and refused. No system list, no folder browser and
 * no settings screen, because with one game there is nothing to choose
 * between. main.c owns state and the loop; how any of it LOOKS belongs to a
 * view (see view.h).
 */
#include "app.h"
#include "blip.h"
#include "font.h"
#include "launchcfg.h"
#include "romfile.h"
#include "room.h"
#include "view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static const char *shot_path;
static int shot_ms = 0, shot_cursor = 0, shot_view = -1;

/* Startup stopwatch. Boot time is the one thing a novelty firmware cannot be
 * careless about -- five seconds of animation is a choice, everything after it
 * is a cost -- so the phases are timed and logged rather than guessed at. */
static unsigned t_boot0;
static void t_mark(const char *what)
{
	fprintf(stderr, "boot: %-14s %5u ms\n", what, plat_now_ms() - t_boot0);
}

/* The boot animation plays in a background process while everything above
 * runs -- scan, GL init, asset decode, the lot. Both it and this process draw
 * to /dev/fb0, so presenting now would fight it: last writer wins, at 60fps
 * against its 30. Wait for it to clear the marker, then draw.
 *
 * Bounded, because a boot that hangs behind a stuck decoder would be a far
 * worse bug than a seam in an animation. Absent marker means no animation is
 * playing -- every launcher restart after a game -- and this returns at once. */
static void wait_for_boot_anim(void)
{
	const char *flag = getenv("CTR_ANIM_FLAG");
	unsigned start;

	if (!flag || !*flag || access(flag, F_OK) != 0) return;
	start = plat_now_ms();
	while (access(flag, F_OK) == 0) {
		if (plat_now_ms() - start > 8000u) {
			fprintf(stderr, "boot: animation flag stuck, drawing anyway\n");
			break;
		}
		SDL_Delay(16);
	}
	t_mark("anim wait");
}

/* ---------- views -------------------------------------------------------- */

static void view_switch(app *a, int idx)
{
	const ctr_view *old = view_get(a->view);
	const ctr_view *nw;

	if (idx == a->view) return;
	if (old->unload) old->unload(a);
	a->view = ((idx % view_count()) + view_count()) % view_count();
	nw = view_get(a->view);
	if (nw->load && !nw->load(a))
		fprintf(stderr, "view '%s' failed to load\n", nw->name);
}

/* ---------- launching ---------------------------------------------------- */

static const char *minarch_env[20];
static char env_buf[10][SCAN_PATH];

static void build_child_env(void)
{
	static const char *fixed[] = {
		"PLATFORM=tg5040", ("DEVICE=" CTR_DEVICE),
		"SDCARD_PATH=/mnt/SDCARD",
		"BIOS_PATH=/mnt/SDCARD/Bios",
		"CHEATS_PATH=/mnt/SDCARD/Cheats",
		"SAVES_PATH=/mnt/SDCARD/Saves",
	};
	size_t i; int n = 0;
	for (i = 0; i < sizeof fixed / sizeof *fixed; i++) minarch_env[n++] = fixed[i];
	snprintf(env_buf[0], sizeof env_buf[0], "ROMS_PATH=%s", P_CARD);
	snprintf(env_buf[1], sizeof env_buf[1], "SYSTEM_PATH=%s", P_ROOT);
	snprintf(env_buf[2], sizeof env_buf[2], "CORES_PATH=%s/cores", P_ROOT);
	snprintf(env_buf[3], sizeof env_buf[3], "USERDATA_PATH=%s", P_USERDATA);
	snprintf(env_buf[4], sizeof env_buf[4], "SHARED_USERDATA_PATH=%s", P_SHARED);
	snprintf(env_buf[5], sizeof env_buf[5], "LOGS_PATH=%s/logs", P_USERDATA);
	snprintf(env_buf[6], sizeof env_buf[6], "HOME=%s", P_USERDATA);
	snprintf(env_buf[7], sizeof env_buf[7], "LD_LIBRARY_PATH=%s/lib:/usr/trimui/lib", P_ROOT);
	for (i = 0; i <= 7; i++) minarch_env[n++] = env_buf[i];
	minarch_env[n] = NULL;
}

/* Contra (Japan) is mapper 23 (VRC2) and carries a real NES 2.0 header with
 * submapper 3. The bundled fceumm SEGFAULTS on that combination -- verified by
 * clearing the NES 2.0 marker on an otherwise byte-identical ROM, which then
 * runs fine. So for VRC-family ROMs that declare NES 2.0, hand the core a copy
 * whose header has been stepped back to plain iNES 1.0.
 *
 * The mapper number is unchanged; all that is dropped is the submapper nibble
 * and the 2.0 marker, which is exactly the information that trips it. The
 * user's file is never touched -- the copy lives in /tmp.
 *
 * Remove this when the core is rebuilt from a fceumm that handles it. */
static bool needs_ines1(const nes_rom *rom)
{
	return rom->nes2 && (rom->mapper == 23 || rom->mapper == 21 ||
	                     rom->mapper == 22 || rom->mapper == 25);
}

static bool write_ines1_copy(const ctr_item *it, char *out, size_t n)
{
	rom_image img;
	nes_rom   rom;
	FILE *f;
	bool ok = false;

	if (!rom_load(it->path, &img)) return false;
	if (nes_parse(img.data, img.len, &rom) && needs_ines1(&rom)) {
		mkdir("/tmp/Contra", 0755);
		snprintf(out, n, "/tmp/Contra/%s.nes", it->name);
		f = fopen(out, "wb");
		if (f) {
			uint8_t hdr[16];
			memcpy(hdr, img.data, 16);
			hdr[7] &= (uint8_t)~0x0C;   /* clear the NES 2.0 marker */
			hdr[8]  = 0;                /* submapper / mapper-high  */
			ok = fwrite(hdr, 1, 16, f) == 16 &&
			     fwrite(img.data + 16, 1, img.len - 16, f) == img.len - 16;
			fclose(f);
			if (ok) fprintf(stderr, "ines1 shim: mapper %d -> %s\n",
			                rom.mapper, out);
		}
	}
	rom_free(&img);
	return ok;
}

/* The path to hand minarch. Normally just the ROM, exactly where it sits.
 *
 * The exceptions are a patched version, which exists only as base + patch and
 * has to be written somewhere real before a core can open it, and a VRC ROM
 * with a NES 2.0 header, which the core cannot survive. /tmp is tmpfs, so
 * nothing derived from the user's ROMs ever lands on the card. */
static void launch_path(const ctr_item *it, char *out, size_t n)
{
	if (it->pkind != PATCH_NONE) {
		size_t len = 0;
		uint8_t *img = ctr_build_patched(it, &len);
		FILE *f;
		mkdir("/tmp/Contra", 0755);
		snprintf(out, n, "/tmp/Contra/%s.nes", it->name);
		if (img) {
			f = fopen(out, "wb");
			if (f) { fwrite(img, 1, len, f); fclose(f); }
			free(img);
			return;
		}
	}
	if (write_ines1_copy(it, out, n)) return;
	snprintf(out, n, "%s", it->path);
}

/* The card face. minarch grabs the title screen itself on a ROM's first run
 * (see minarch/overrides). If that did not happen -- quit too early, or an
 * unpatched minarch -- fall back to the auto-state preview it always writes.
 * That shows where you left off rather than the title screen: a lesser thing,
 * but still a real face. */
static void capture_face(app *a, const ctr_item *it)
{
	char face[SCAN_PATHC], bmp[SCAN_PATHC];
	const char *base;
	struct stat st;
	SDL_Surface *s;

	if (!it->res.sha1[0]) return;
	snprintf(face, sizeof face, "%s/%s.png", a->faces_dir, it->res.sha1);
	if (stat(face, &st) == 0 && st.st_size > 0) return;

	base = strrchr(it->path, '/');
	base = base ? base + 1 : it->path;
	snprintf(bmp, sizeof bmp, "%s/.minui/Contra/%s.9.bmp", P_SHARED, base);
	if (stat(bmp, &st) != 0) return;

	s = IMG_Load(bmp);
	if (!s) return;
	IMG_SavePNG(s, face);
	SDL_FreeSurface(s);
	fprintf(stderr, "face captured: %s\n", face);
}

/* With one game, resume is the overwhelmingly common path, so the launcher
 * comes back up tuned to whatever was played last. */
static void remember_last_played(app *a, const ctr_item *it)
{
	char p[SCAN_PATH];
	FILE *f;
	if (!it->res.sha1[0]) return;
	snprintf(p, sizeof p, "%s/.last", P_ROOT);
	f = fopen(p, "w");
	if (!f) return;
	fputs(it->res.sha1, f);
	fclose(f);
}

static int find_last_played(app *a)
{
	char p[SCAN_PATH], sha[64] = {0};
	FILE *f;
	int i;
	snprintf(p, sizeof p, "%s/.last", P_ROOT);
	f = fopen(p, "r");
	if (!f) return 0;
	if (!fgets(sha, sizeof sha, f)) { fclose(f); return 0; }
	fclose(f);
	sha[strcspn(sha, "\r\n")] = 0;
	for (i = 0; i < a->list.count; i++)
		if (!strcmp(a->list.items[i].res.sha1, sha)) return i;
	return 0;
}

/* A television turning off: the picture collapses to a bright line, then a
 * dot, then nothing. Played on the way BACK from a game, before the launcher
 * redraws — the game's set switching off. */
static void tv_off(app *a)
{
	int steps = 22, i;
	for (i = 0; i <= steps; i++) {
		float t = (float)i / steps;
		int h = (int)((1.0f - t) * CTR_SCREEN_H * 0.55f) + 2;
		int w = (int)((1.0f - t * t * t) * CTR_SCREEN_W * 0.72f) + 2;
		SDL_Rect band = { (CTR_SCREEN_W - w)/2, (CTR_SCREEN_H - h)/2, w, h };
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 255);
		SDL_RenderClear(a->r);
		SDL_SetRenderDrawColor(a->r, 235, 240, 245, (Uint8)(255 - t * 40));
		SDL_RenderFillRect(a->r, &band);
		SDL_RenderPresent(a->r);
		SDL_Delay(10);
	}
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 255);
	SDL_RenderClear(a->r);
	SDL_RenderPresent(a->r);
}

/* A television turning on: a bright line snaps out from the centre and opens
 * into a picture. Played when LAUNCHING, covering the SDL teardown before
 * minarch takes the display. */
static void tv_on(app *a)
{
	int steps = 18, i;
	for (i = 0; i <= steps; i++) {
		float t = (float)i / steps;
		int w = (int)((0.25f + 0.75f * (t < 0.5f ? t * 2.0f : 1.0f)) * CTR_SCREEN_W * 0.92f);
		int h = (int)(t * t * CTR_SCREEN_H * 0.55f) + 2;
		SDL_Rect band = { (CTR_SCREEN_W - w)/2, (CTR_SCREEN_H - h)/2, w, h };
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 255);
		SDL_RenderClear(a->r);
		SDL_SetRenderDrawColor(a->r, 235, 240, 245, (Uint8)(235 - t * 200));
		SDL_RenderFillRect(a->r, &band);
		SDL_RenderPresent(a->r);
		SDL_Delay(9);
	}
}

static void power_off(app *a);

static void launch(app *a)
{
	int gw = TUBE_W, gh = TUBE_H, gx = TUBE_X, gy = TUBE_Y;
	bool resident = false;
	ctr_item *it;
	char core[SCAN_PATH], elf[SCAN_PATH], rom[SCAN_PATH], face_env[SCAN_PATHC];
	char viewport_env[96];
	char panel_env[SCAN_PATHC];
	static const char *env[24];
	char *argv[4];
	FILE *f;
	int n = 0, rc;

	if (a->list.count == 0) return;
	it = &a->list.items[a->cursor];
	if (it->res.verdict != VD_VERIFIED && it->res.verdict != VD_HACK) {
		blip_play(BLIP_DENY);
		return;
	}
	blip_play(BLIP_START);

	/* Resolve the path FIRST: a patched or header-shimmed version is launched
	 * from /tmp, and minarch names its config after whatever it is handed. */
	launch_path(it, rom, sizeof rom);
	snprintf(core, sizeof core, "%s/cores/fceumm_libretro.so", P_ROOT);
	snprintf(elf,  sizeof elf,  "%s/minarch.elf", P_ROOT);

	/* With no settings screen, this is where every per-ROM decision becomes
	 * real: the CRT pair, the region timing that makes Probotector run at
	 * 50Hz, and the overscan crop. */
	lc_write(rom, it->res.entry);

	f = fopen("/tmp/resume_slot.txt", "w");
	if (f) { fputs("9", f); fclose(f); }

	while (minarch_env[n]) { env[n] = minarch_env[n]; n++; }
	/* One game, so one tag -- stated outright instead of hoping minarch infers
	 * it from punctuation in the filename. Saves, states and the per-game
	 * config all hang off this. */
	env[n++] = "CONTRARIAN_TAG=" CTR_TAG;

	/* Put the picture in the tube. The cabinet is drawn over it by minarch's
	 * own overlay pass, so the game plays ON the television. */
	{
		if (a->game_2x) {
			gw = GAME_2X_W; gh = GAME_2X_H;
			gx = TUBE_X + (TUBE_W - gw) / 2;
			gy = TUBE_Y + (TUBE_H - gh) / 2;
		}
		snprintf(viewport_env, sizeof viewport_env,
		         "CONTRARIAN_VIEWPORT=%d,%d,%d,%d", gx, gy, gw, gh);
		env[n++] = viewport_env;
	}

	/* The in-game menu is the console's own front panel; minarch loads the art
	 * from here. Absent, it falls back to its own menu. */
	snprintf(panel_env, sizeof panel_env, "CONTRARIAN_PANEL=%s/nes", a->res_dir);
	env[n++] = panel_env;
	if (it->res.sha1[0]) {
		snprintf(face_env, sizeof face_env, "CONTRARIAN_FACE=%s/%s.png",
		         a->faces_dir, it->res.sha1);
		env[n++] = face_env;
	}
	env[n] = NULL;

	argv[0] = elf; argv[1] = core; argv[2] = rom; argv[3] = NULL;

	/* Starting a game is the set coming ON: a bright line snaps out and opens,
	 * and the game is behind it. Quitting is the set going off. Reading it from
	 * the player's side rather than the launcher's is the intuitive way round. */
	/* The audio device is exclusive, and minarch wants it either way. Released
	 * before the request so the game's own audio init does not wait on it. */
	blip_quit();

	if (plat_resident_ready()) {
		/* Resident minarch: it already holds the GL context and the core, so
		 * the game is up in ~200ms instead of ~1100ms. Nothing here is torn
		 * down -- keeping this process's own context costs another ~440ms
		 * each way, and there is no need to give it up: only one of the two
		 * draws at a time, and this one is blocked until the game ends. */
		char req[SCAN_PATHC * 2 + 64];
		char face[SCAN_PATHC];

		face[0] = 0;
		if (it->res.sha1[0])
			snprintf(face, sizeof face, "%s/%s.png", a->faces_dir, it->res.sha1);

		snprintf(req, sizeof req, "%s\t%s\t%d,%d,%d,%d\n",
		         rom, face, gx, gy, gw, gh);

		if (plat_resident_send(req)) {
			/* The set comes on WHILE the game loads, rather than before it.
			 * The animation is ~220ms and the load ~430ms, so this is that
			 * much taken off what the player waits through -- the same
			 * reason the boot animation runs alongside startup. */
			tv_on(a);
			plat_resident_wait();
			resident = true;
			fprintf(stderr, "resident game finished\n");
		} else {
			fprintf(stderr, "resident minarch did not answer, falling back\n");
		}
	}

	/* Fallback path still animates first: it is about to tear the display
	 * down, so there is nothing to overlap with. */
	if (!resident) tv_on(a);

	if (!resident) {
		/* One game per process, the old way. Kept as the fallback for a
		 * resident minarch that is missing or has died. */
		if (view_get(a->view)->unload) view_get(a->view)->unload(a);
		font_quit();
		plat_input_quit();
		plat_video_quit();

		rc = plat_run(argv, env, P_ROOT);
		fprintf(stderr, "minarch exited %d\n", rc);
	}

	/* A CRT toggle made in the in-game menu is written into minarch's config;
	 * fold it back into contra.db so it survives the next launch. */
	if (it->res.entry) {
		int nsh = lc_read_shader_count(rom);
		if (nsh >= 0 && (nsh > 0) != it->res.entry->crt) {
			it->res.entry->crt = (nsh > 0);
			a->db.dirty = true;
		}
	}
	capture_face(a, it);
	if (!it->has_face && it->res.sha1[0]) {
		char p[SCAN_PATHC]; struct stat st;
		snprintf(p, sizeof p, "%s/%s.png", a->faces_dir, it->res.sha1);
		it->has_face = (stat(p, &st) == 0 && st.st_size > 0);
	}
	/* A launch that fell back means there is no resident; start one so the
	 * next launch is fast again. */
	if (!resident) plat_spawn_resident(elf, core, env);

	remember_last_played(a, it);
	if (a->db.dirty) { db_save(a->db_path, &a->db); a->db.dirty = false; }

	if (access(CTR_POWEROFF_FLAG, F_OK) == 0) { a->running = false; return; }
	if (!resident) {
		if (!plat_video_init() || !plat_input_init()) { a->running = false; return; }
		a->r = plat_renderer();
		font_init(a->r);
		if (view_get(a->view)->load) view_get(a->view)->load(a);
	}
	plat_leds_off();
	blip_init_res(a->res_dir);   /* take the audio device back */
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);

	/* Power was pressed during the game. minarch no longer handles that key
	 * itself, so the press came here: the launcher has just been rebuilt far
	 * enough to draw, and GAME OVER is what it draws. No tv_off first -- the
	 * set does not tune back to a channel on its way out. */
	if (plat_run_power_pressed()) { power_off(a); return; }

	tv_off(a);
}

/* ---------- input -------------------------------------------------------- */

/* Powering down gets a send-off, played out in full. The set collapses to a
 * dot while it runs, then the flag is written and the loop ends -- launch.sh
 * sees the flag and calls poweroff. Cutting the audio off at the moment you
 * press the button would be worse than having none. */
static void power_off(app *a)
{
	unsigned start, wait;

	a->farewell = app_load_tex(a, "room", "gameover.png");
	blip_play(BLIP_POWEROFF);

	/* Hold GAME OVER on the set for as long as the music runs, redrawing so
	 * the panel is not a frozen buffer. Capped so a bad audio file cannot
	 * stall the shutdown. */
	wait = blip_remaining_ms();
	if (wait == 0u)    wait = 1200u;     /* no audio: still show it briefly */
	if (wait > 9000u)  wait = 9000u;
	start = plat_now_ms();
	while (plat_now_ms() - start < wait) {
		view_get(a->view)->draw(a);
		SDL_RenderPresent(a->r);
		SDL_Delay(16);
	}

	/* No collapse-to-a-dot here: GAME OVER is the last thing on the panel, and
	 * it holds until the display actually dies. The texture is deliberately
	 * NOT freed and the frame is deliberately NOT cleared -- whatever is on
	 * screen when power cuts is what you see. */
	plat_request_poweroff();
	a->running = false;
}

static void move_cursor(app *a, int delta)
{
	const ctr_view *v;
	if (a->list.count <= 0) return;
	a->prev_cursor = a->cursor;
	a->cursor = (a->cursor + delta) % a->list.count;
	if (a->cursor < 0) a->cursor += a->list.count;
	a->cursor_ms = plat_now_ms();
	blip_play(BLIP_TUNE);
	v = view_get(a->view);
	if (v->tuned) v->tuned(a, delta);
}

/* The one thing a player might genuinely want to change, so it gets one button
 * rather than a settings screen. Stored per-ROM in contra.db, applied by
 * lc_write on the next launch. */
static void toggle_crt(app *a)
{
	ctr_item *it;
	if (a->list.count == 0) return;
	it = &a->list.items[a->cursor];
	if (!it->res.entry) return;
	it->res.entry->crt = !it->res.entry->crt;
	blip_play(BLIP_TOGGLE);
	db_save(a->db_path, &a->db);
}

/* ---------- config ------------------------------------------------------- */

static void read_cfg(app *a, int *view_idx)
{
	char p[SCAN_PATH], line[256];
	FILE *f;
	snprintf(p, sizeof p, "%s/contrarian.cfg", P_ROOT);
	f = fopen(p, "r");
	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		char val[64];
		if (sscanf(line, "view=%63s", val) == 1) {
			int i = view_find(val);
			if (i >= 0) *view_idx = i;
		} else if (sscanf(line, "game_size=%63s", val) == 1) {
			a->game_2x = (strcmp(val, "2x") == 0);
		}
	}
	fclose(f);
}

static void write_view_pref(app *a)
{
	char p[SCAN_PATH], tmp[SCAN_PATH * 4] = {0};
	char line[256];
	FILE *f;
	size_t used = 0;
	bool wrote = false;

	snprintf(p, sizeof p, "%s/contrarian.cfg", P_ROOT);
	f = fopen(p, "r");
	if (f) {
		while (fgets(line, sizeof line, f)) {
			if (!strncmp(line, "view=", 5)) {
				used += (size_t)snprintf(tmp + used, sizeof tmp - used,
				                         "view=%s\n", view_get(a->view)->name);
				wrote = true;
			} else if (used + strlen(line) < sizeof tmp) {
				used += (size_t)snprintf(tmp + used, sizeof tmp - used, "%s", line);
			}
		}
		fclose(f);
	}
	if (!wrote)
		snprintf(tmp + used, sizeof tmp - used, "view=%s\n", view_get(a->view)->name);
	f = fopen(p, "w");
	if (!f) return;
	fputs(tmp, f);
	fclose(f);
}

/* ---------- main --------------------------------------------------------- */

int main(int argc, char **argv)
{
	static app a;
	char cache[SCAN_PATH], fp[SCAN_PATH];
	int argi, view_idx = 0;

	for (argi = 1; argi < argc; argi++) {
		if (!strcmp(argv[argi], "--shot") && argi + 3 < argc) {
			shot_path   = argv[++argi];
			shot_ms     = atoi(argv[++argi]);
			shot_cursor = atoi(argv[++argi]);
		} else if (!strcmp(argv[argi], "--view") && argi + 1 < argc) {
			shot_view = view_find(argv[++argi]);
		}
	}

	t_boot0 = plat_now_ms();
	paths_init();
	build_child_env();

	snprintf(a.root,      sizeof a.root,      "%s", P_ROOT);
	snprintf(a.res_dir,   sizeof a.res_dir,   "%s/res", P_ROOT);
	snprintf(a.db_path,   sizeof a.db_path,   "%s/contra.db", P_ROOT);
	snprintf(fp,          sizeof fp,          "%s/contra.fp", P_ROOT);
	snprintf(cache,       sizeof cache,       "%s/.cache",    P_ROOT);
	snprintf(a.faces_dir, sizeof a.faces_dir, "%s/faces",     P_ROOT);
	mkdir(a.faces_dir, 0755);

	if (!db_load(a.db_path, &a.db))   fprintf(stderr, "no contra.db at %s\n", a.db_path);
	if (!db_load_bases(fp, &a.bases)) fprintf(stderr, "no contra.fp at %s\n", fp);
	read_cfg(&a, &view_idx);
	if (shot_view >= 0) view_idx = shot_view;

	ctr_scan(P_CARD, cache, a.faces_dir, &a.db, &a.bases, &a.list);
	fprintf(stderr, "scan: %d items (%d verified, %d hack, %d reject; %d cached)\n",
	        a.list.count, a.list.n_verified, a.list.n_hack, a.list.n_reject,
	        a.list.n_cached);

	/* --scan: warm the verdict cache and stop, touching neither the display
	 * nor the input devices. launch.sh runs this behind the boot animation so
	 * the cold scan -- inflating every zip on the card to hash it -- happens
	 * during five seconds the boot is already spending, instead of after them.
	 * The real launcher then finds every verdict cached and starts at once. */
	t_mark("scan");

	if (!plat_video_init()) { fprintf(stderr, "video init failed\n"); return 1; }
	IMG_Init(IMG_INIT_PNG);
	a.r = plat_renderer();
	plat_input_init();
	plat_settings_init();
	plat_leds_off();
	t_mark("video+input");
	if (!font_init(a.r)) fprintf(stderr, "font init failed\n");
	blip_init_res(a.res_dir);
	t_mark("font+audio");

	a.t0 = plat_now_ms();
	a.cursor = shot_path ? ((shot_cursor < a.list.count) ? shot_cursor : 0)
	                     : find_last_played(&a);
	a.cursor_ms = a.t0;
	a.running = true;
	a.view = view_idx;
	if (view_get(a.view)->load && !view_get(a.view)->load(&a))
		fprintf(stderr, "view '%s' failed to load\n", view_get(a.view)->name);
	t_mark("view assets");

	if (shot_path) {
		SDL_Surface *out;
		a.t0 = plat_now_ms() - (unsigned)shot_ms;
		a.cursor_ms = a.t0;
		view_get(a.view)->draw(&a);
		out = SDL_CreateRGBSurfaceWithFormat(0, CTR_SCREEN_W, CTR_SCREEN_H, 32,
		                                     SDL_PIXELFORMAT_RGBA32);
		if (out) {
			/* read BEFORE present: the backbuffer is invalidated by it */
			SDL_RenderReadPixels(a.r, NULL, SDL_PIXELFORMAT_RGBA32,
			                     out->pixels, out->pitch);
			SDL_RenderPresent(a.r);
			IMG_SavePNG(out, shot_path);
			SDL_FreeSurface(out);
			fprintf(stderr, "wrote %s\n", shot_path);
		}
		if (view_get(a.view)->unload) view_get(a.view)->unload(&a);
		font_quit();
		plat_video_quit();
		return 0;
	}

	wait_for_boot_anim();

	while (a.running) {
		plat_input_poll(&a.in);
		if (a.in.quit_requested) break;

		/* Channel up/down only, the way a remote does it: UP raises the
		 * number. Left and right are deliberately dead -- this is a tuner,
		 * not a list. */
		if (in_repeat(&a.in, IN_UP))    move_cursor(&a, +1);
		if (in_repeat(&a.in, IN_DOWN))  move_cursor(&a, -1);
		if (a.in.pressed[IN_ACCEPT])    launch(&a);
		if (a.in.pressed[IN_Y])         toggle_crt(&a);
		if (a.in.pressed[IN_X])       { view_switch(&a, a.view + 1); write_view_pref(&a);
		                                blip_play(BLIP_TOGGLE); }
		/* Nothing may draw after this: power_off leaves the set dark and the
		 * loop would otherwise fall through to draw() and paint the launcher
		 * back over it for one frame. */
		if (a.in.pressed[IN_POWER])   { power_off(&a); break; }

		/* Volume and brightness. The platform layer reads these keys and knows
		 * how to apply them, but nothing was calling it -- so they did nothing
		 * on every screen of the launcher. */
		if (in_repeat(&a.in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a.in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a.in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a.in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		if (!a.running) break;
		view_get(a.view)->draw(&a);
		plat_draw_osd(a.r);
		SDL_RenderPresent(a.r);
		SDL_Delay(8);
	}

	if (view_get(a.view)->unload) view_get(a.view)->unload(&a);
	blip_quit();
	font_quit();
	IMG_Quit();
	plat_input_quit();
	plat_video_quit();
	return 0;
}
