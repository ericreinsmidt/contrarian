/* SPDX-License-Identifier: 0BSD */
#ifndef CTR_LAUNCHCFG_H
#define CTR_LAUNCHCFG_H

#include "db.h"
#include "scan.h"

/* One game, one tag. Passed to minarch as CONTRARIAN_TAG (see the ma_core.c
 * override) and used here to find the directory minarch will read its
 * per-game config from. The two MUST agree, so they share this. */
#define CTR_TAG    "Contra"
#define CTR_CORE   "fceumm"
/* minarch appends "-<device_tag>" to the per-game config filename, from the
 * DEVICE env var. The launcher must pass the same string it builds the
 * filename from, so both come from here. */
#define CTR_DEVICE "brick"
#include <stdbool.h>

/* Contrarian has no settings screen, so nothing is ever configured by hand.
 * Instead the launcher writes minarch's per-game config immediately before
 * each launch, from the contra.db entry for that exact ROM. This is what makes
 * three design promises real rather than merely shipped:
 *
 *   - the CRT shader pair is actually enabled (or not),
 *   - Probotector actually runs at 50Hz,
 *   - a hack that needs overscan cropping actually gets it.
 *
 * minarch reads <config_dir>/<alt_name>.cfg as "key = value" lines, where
 * config_dir is USERDATA_PATH/<tag>-<core> and alt_name is the .nes filename
 * (the member name, for a zip). Both are compile-time-rooted in minarch. */

/* Directory minarch will look in for this ROM's config. */
void lc_config_dir(char *out, size_t n);

/* The name minarch builds this ROM's config filename from: the basename,
 * including extension, of the path it is HANDED -- which is not always the
 * ROM's own path. A patched version and a header-shimmed one are both launched
 * from /tmp, and minarch names their configs after those copies. */
void lc_alt_name(const char *launch_path, char *out, size_t n);

/* Write the per-game config. Returns false if it could not be written --
 * in which case minarch simply falls back to its own defaults. */
bool lc_write(const char *launch_path, const db_entry *e);

/* Read back minarch_nrofshaders after the game exits, so a CRT toggle made in
 * the in-game menu persists into contra.db. Returns -1 if unreadable. */
int lc_read_shader_count(const char *launch_path);

#endif
