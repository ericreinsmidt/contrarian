/* SPDX-License-Identifier: 0BSD */
#include "launchcfg.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* These mirror minarch's compile-time roots (all/common/defines.h,
 * tg5040/platform/platform.h). They are not configurable there, so they are
 * not configurable here either -- but they are named, so the coupling is
 * visible rather than buried in a format string. */
#define MA_SDCARD   "/mnt/SDCARD"
#define MA_USERDATA MA_SDCARD "/.userdata/tg5040"
/* core.name is the .so basename up to its last '_' (Core_getName). */

void lc_config_dir(char *out, size_t n)
{
	snprintf(out, n, "%s/%s-%s", MA_USERDATA, CTR_TAG, CTR_CORE);
}

void lc_alt_name(const char *launch_path, char *out, size_t n)
{
	const char *base = strrchr(launch_path, '/');
	snprintf(out, n, "%s", base ? base + 1 : launch_path);
}

/* fceumm's region values are NOT bare "PAL": the core matches the full label.
 * Writing "PAL" silently fails to match and leaves the option at Auto, which
 * is precisely the bug this whole per-ROM mechanism exists to prevent. */
static const char *region_value(ctr_region tv)
{
	switch (tv) {
	case REGION_PAL:   return "System: PAL";
	case REGION_DENDY: return "System: Dendy";
	default:           return "System: NTSC";
	}
}

bool lc_write(const char *launch_path, const db_entry *e)
{
	char dir[SCAN_PATH], alt[SCAN_NAME], path[SCAN_PATHC];
	bool crt  = e ? e->crt  : false;   /* off by default; Y turns it on */
	bool crop = e ? e->crop : false;
	ctr_region tv = e ? e->tv : REGION_NTSC;
	FILE *f;

	lc_config_dir(dir, sizeof dir);
	mkdir(MA_USERDATA, 0755);
	mkdir(dir, 0755);

	lc_alt_name(launch_path, alt, sizeof alt);
	/* minarch: "<config_dir>/<alt_name><device_tag>.cfg", device_tag being
	 * "-brick" here. Getting this name wrong is silent -- minarch simply finds
	 * no config and uses defaults, which is exactly how the CRT shader, the
	 * overlay and the region setting all appeared to be ignored. */
	if (snprintf(path, sizeof path, "%s/%s-%s.cfg", dir, alt, CTR_DEVICE)
	    >= (int)sizeof path)
		return false;

	f = fopen(path, "w");
	if (!f) return false;

	/* Timing. This is the value retro_get_region() reports back, which is what
	 * flips minarch's sync_ref onto core fps -- the entire 50Hz chain hangs off
	 * this one line. */
	fprintf(f, "fceumm_region = %s\n", region_value(tv));

	/* Overscan. Off for every known build; a per-hack flag for the ones that
	 * draw garbage in the hidden rows. */
	fprintf(f, "fceumm_overscan_v = %s\n", crop ? "enabled" : "disabled");
	fprintf(f, "fceumm_overscan_h = disabled\n");

	/* Sprite limit stays ON: Contra flickers on real hardware, and removing
	 * the limit makes it look wrong to anyone who knows the game. */
	fprintf(f, "fceumm_nospritelimit = disabled\n");

	/* Autofire. fceumm exposes Turbo A and Turbo B as libretro X and Y, and
	 * minarch's default binds are identity (X->JOYPAD_X, Y->JOYPAD_Y), so this
	 * one option lands them on the physical X and Y with no remapping:
	 * X = auto-A, Y = auto-B. Contra without rapid fire is a thumb injury. */
	fprintf(f, "fceumm_turbo_enable = Player 1\n");
	fprintf(f, "fceumm_turbo_delay = 3\n");

	/* The cabinet, drawn over the running game by minarch's own overlay pass.
	 * Together with CONTRARIAN_VIEWPORT (which puts the picture in the tube)
	 * this is what makes the game appear ON the television rather than behind
	 * it. minarch lists Overlays/<tag>/ and matches by filename. */
	fprintf(f, "minarch_overlay = cabinet.png\n");

	/* CRT. "off" is simply zero passes -- the per-ROM flag selects a config,
	 * not a code path, so there is nothing to toggle at runtime. */
	if (crt) {
		fprintf(f, "minarch_nrofshaders = 2\n");
		fprintf(f, "minarch_shader1 = contrarian-glow.glsl\n");
		fprintf(f, "minarch_shader1_filter = LINEAR\n");
		fprintf(f, "minarch_shader1_srctype = source\n");
		fprintf(f, "minarch_shader1_scaletype = source\n");
		fprintf(f, "minarch_shader1_upscale = screen\n");
		fprintf(f, "minarch_shader2 = contrarian-scanline.glsl\n");
		fprintf(f, "minarch_shader2_filter = NEAREST\n");
		fprintf(f, "minarch_shader2_srctype = source\n");
		fprintf(f, "minarch_shader2_scaletype = source\n");
		fprintf(f, "minarch_shader2_upscale = screen\n");
	} else {
		fprintf(f, "minarch_nrofshaders = 0\n");
	}

	fclose(f);
	sync();
	return true;
}

int lc_read_shader_count(const char *launch_path)
{
	char dir[SCAN_PATH], alt[SCAN_NAME], path[SCAN_PATHC], line[256];
	FILE *f;
	int n = -1;

	lc_config_dir(dir, sizeof dir);
	lc_alt_name(launch_path, alt, sizeof alt);
	snprintf(path, sizeof path, "%s/%s-%s.cfg", dir, alt, CTR_DEVICE);

	f = fopen(path, "r");
	if (!f) return -1;
	while (fgets(line, sizeof line, f)) {
		int v;
		if (sscanf(line, "minarch_nrofshaders = %d", &v) == 1) { n = v; break; }
	}
	fclose(f);
	return n;
}
