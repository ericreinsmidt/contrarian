/* SPDX-License-Identifier: 0BSD */
#include "verify.h"
#include <stdio.h>
#include <string.h>

void ctr_classify(const nes_rom *rom, contra_db *db, const contra_bases *bases,
                  ctr_result *out)
{
	nes_fp fp;
	int i;

	memset(out, 0, sizeof(*out));
	out->best_base = -1;
	out->mapper    = rom->mapper;

	nes_payload_sha1(rom, out->sha1);

	out->entry = db_find(db, out->sha1);
	if (out->entry) {
		out->verdict = VD_VERIFIED;
		out->score   = 100.0f;
		return;
	}

	nes_fingerprint(rom, &fp);
	for (i = 0; i < bases->count; i++) {
		float s = nes_similarity(&fp, &bases->b[i].fp);
		if (s > out->score) { out->score = s; out->best_base = i; }
	}

	out->verdict = (out->score >= CTR_MATCH_THRESHOLD) ? VD_HACK : VD_REJECT;
}

void ctr_describe(const ctr_result *r, const contra_bases *bases,
                  char *buf, size_t n)
{
	switch (r->verdict) {
	case VD_VERIFIED: {
		const db_entry *e = r->entry;
		const char *hz = (e->tv == REGION_PAL) ? "50Hz" : "60Hz";
		/* Deliberately no percentage: a verified ROM is verified, and
		 * "24.8% CONTRA" under Probotector would read as a bug. */
		snprintf(buf, n, "%s \xc2\xb7 %s \xc2\xb7 %s \xc2\xb7 VERIFIED",
		         e->region[0] ? e->region : db_tv_name(e->tv),
		         db_mapper_name(e->mapper), hz);
		break;
	}
	case VD_HACK:
		if (r->best_base >= 0 && r->best_base < bases->count)
			snprintf(buf, n, "HACK \xc2\xb7 %s \xc2\xb7 %.1f%% MATCH",
			         bases->b[r->best_base].name, r->score);
		else
			snprintf(buf, n, "HACK \xc2\xb7 %.1f%% MATCH", r->score);
		break;
	case VD_REJECT:
		snprintf(buf, n, "REJECTED \xc2\xb7 NOT CONTRA");
		break;
	default:
		snprintf(buf, n, "UNREADABLE \xc2\xb7 NOT A NES ROM");
		break;
	}
}
