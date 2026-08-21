/* SPDX-License-Identifier: 0BSD */
#ifndef CTR_SCAN_H
#define CTR_SCAN_H

#include "patch.h"
#include "verify.h"
#include <stdbool.h>
#include <sys/types.h>

#define SCAN_MAX_ITEMS 512
#define SCAN_PATH      1024
/* For buffers that COMPOSE a path from SCAN_PATH-sized parts. */
#define SCAN_PATHC     (SCAN_PATH + 512)
/* Must hold a full .nes member name: minarch names its per-game config
 * after it, so a truncated copy here would silently look up the wrong
 * file and the ROM would launch with default region and no shader. */
#define SCAN_NAME      256

typedef struct {
	char       path[SCAN_PATH];
	char       name[SCAN_NAME];   /* display name derived from the filename */
	char       member[SCAN_NAME]; /* .nes member inside a zip, "" for a bare file.
	                               * minarch names its per-game config after the
	                               * extracted member, so we must know it too. */
	ctr_result res;
	bool       has_face;          /* faces/<sha1>.png exists                */
	off_t      size;
	long       mtime;

	/* A patched version: path is the .ips/.bps, base_path is the ROM it
	 * applies to. The patched image is rebuilt into /tmp at launch, so the
	 * card never carries a derived ROM. */
	patch_kind pkind;
	char       base_path[SCAN_PATH];
} ctr_item;

typedef struct {
	ctr_item items[SCAN_MAX_ITEMS];
	int      count;
	int      n_verified, n_hack, n_reject;
	int      n_cached, n_read;    /* cache hits vs actual inflates          */
} ctr_list;

/* Walk root recursively, classify every .nes/.zip, and sort:
 * verified first, then accepted hacks, then rejects. */
bool ctr_scan(const char *root, const char *cache_path, const char *faces_dir,
              contra_db *db, const contra_bases *bases, ctr_list *out);

/* Build the patched image for a patch item. Caller frees. */
uint8_t *ctr_build_patched(const ctr_item *it, size_t *out_len);

#endif
