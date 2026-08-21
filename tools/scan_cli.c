/* SPDX-License-Identifier: 0BSD
 * Native harness for the scanner: walk a tree and print every verdict. */
#include "../src/scan.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *VD[] = { "VERIFIED", "HACK", "REJECT", "UNREADABLE" };

int main(int argc, char **argv)
{
	static contra_db    db;
	static contra_bases bases;
	static ctr_list     list;
	const char *root  = (argc > 1) ? argv[1] : ".";
	const char *dbp   = (argc > 2) ? argv[2] : "config/contra.db";
	const char *fpp   = (argc > 3) ? argv[3] : "res/contra.fp";
	const char *cache = (argc > 4) ? argv[4] : NULL;
	struct timespec t0, t1;
	int i;

	if (!db_load(dbp, &db))          fprintf(stderr, "warn: no db at %s\n", dbp);
	if (!db_load_bases(fpp, &bases)) fprintf(stderr, "warn: no fingerprints at %s\n", fpp);
	printf("db: %d entries   bases: %d\n\n", db.count, bases.count);

	clock_gettime(CLOCK_MONOTONIC, &t0);
	ctr_scan(root, cache, "res/faces", &db, &bases, &list);
	clock_gettime(CLOCK_MONOTONIC, &t1);

	for (i = 0; i < list.count; i++) {
		const ctr_item *it = &list.items[i];
		char desc[128];
		ctr_describe(&it->res, &bases, desc, sizeof(desc));
		printf("%-11s %-34.34s %s\n", VD[it->res.verdict],
		       it->res.entry ? it->res.entry->title : it->name, desc);
	}

	printf("\n%d items: %d verified, %d hacks, %d rejected  "
	       "(%d cached, %d read)  %.1f ms\n",
	       list.count, list.n_verified, list.n_hack, list.n_reject,
	       list.n_cached, list.n_read,
	       (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6);
	return 0;
}
