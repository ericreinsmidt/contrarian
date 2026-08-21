/* SPDX-License-Identifier: 0BSD */
#ifndef CTR_VERIFY_H
#define CTR_VERIFY_H

#include "db.h"
#include "nes.h"
#include <stdbool.h>

/* Accept threshold. Unrelated ROMs score a true 0.0%; the lowest legitimate
 * relationship measured in the corpus is 24.8% (Contra USA <-> Probotector, two
 * official builds sharing engine code). A hack scores far higher than either,
 * since it modifies a base rather than rebuilding it. See DESIGN.md. */
#define CTR_MATCH_THRESHOLD 8.0f

typedef enum {
	VD_VERIFIED = 0,  /* payload hash is in contra.db          */
	VD_HACK,          /* unknown, but derived from a base      */
	VD_REJECT,        /* a NES ROM, but not Contra             */
	VD_UNREADABLE     /* not a NES ROM at all                  */
} ctr_verdict;

typedef struct {
	ctr_verdict verdict;
	float       score;        /* 0..100; meaningful for HACK and REJECT */
	int         best_base;    /* index into contra_bases, -1 if none    */
	char        sha1[41];
	int         mapper;
	db_entry   *entry;        /* non-NULL when VD_VERIFIED              */
} ctr_result;

/* Classify an already-parsed ROM. Never shows a score for a verified entry:
 * Probotector reading "24.8% CONTRA" would look like a bug. */
void ctr_classify(const nes_rom *rom, contra_db *db, const contra_bases *bases,
                  ctr_result *out);

/* One-line label for the HUD metadata row. */
void ctr_describe(const ctr_result *r, const contra_bases *bases,
                  char *buf, size_t n);

#endif
