/* SPDX-License-Identifier: 0BSD */
#ifndef CTR_DB_H
#define CTR_DB_H

#include "nes.h"
#include <stdbool.h>

#define DB_STR      96
#define DB_MAX      512
#define DB_MAX_BASE 8

typedef enum { REGION_NTSC = 0, REGION_PAL, REGION_DENDY } ctr_region;

/* One known ROM. Mirrors a contra.db line:
 *   payload-sha1 | title | region | tv | mapper | crt | crop | note
 *
 * region and tv are deliberately separate. "USA" and "JAPAN" are both NTSC, so
 * folding them together makes the two Contras indistinguishable in the HUD;
 * and it is tv, not region, that drives fceumm's timing. */
typedef struct {
	char       sha1[41];
	char       title[DB_STR];
	char       region[24];   /* display only: USA / JAPAN / EUROPE      */
	ctr_region tv;           /* drives fceumm_region; never global      */
	int        mapper;
	bool       crt;     /* per-ROM shader choice, set on first launch */
	bool       crop;    /* per-ROM overscan crop */
	char       note[DB_STR];
} db_entry;

typedef struct {
	db_entry e[DB_MAX];
	int      count;
	bool     dirty;     /* a first-launch answer changed something */
} contra_db;

/* Base fingerprints, shipped as contra.fp so fuzzy matching works even when the
 * user has only a hack and none of the original ROMs on the card. A fingerprint
 * is ~128 truncated one-way block digests: about 1KB, and no ROM content. */
typedef struct {
	char   name[DB_STR];
	char   sha1[41];
	nes_fp fp;
} db_base;

typedef struct {
	db_base b[DB_MAX_BASE];
	int     count;
} contra_bases;

bool db_load(const char *path, contra_db *out);
bool db_save(const char *path, const contra_db *db);
db_entry *db_find(contra_db *db, const char *sha1hex);

bool db_load_bases(const char *path, contra_bases *out);
bool db_save_bases(const char *path, const contra_bases *b);

const char *db_tv_name(ctr_region r);
/* Human mapper name for the HUD: 2 -> UNROM, 23 -> VRC2. */
const char *db_mapper_name(int mapper);

#endif
