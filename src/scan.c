/* SPDX-License-Identifier: 0BSD */
#include "scan.h"
#include "romfile.h"
#include "patch.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* Directories the scanner never descends into. DO_NOT_TOUCH is a user-labelled
 * hands-off tree; the rest are ours or the firmware's and hold no ROMs a player
 * put there. Anything starting with '.' is skipped separately, which also
 * covers macOS AppleDouble sidecars ("._Foo.zip" parses as garbage). */
static const char *SKIP_DIRS[] = {
	"DO_NOT_TOUCH", "Saves", "Bios", "System", "trimui", "Contrarian", NULL
};

static bool skip_dir(const char *name)
{
	int i;
	if (name[0] == '.') return true;
	for (i = 0; SKIP_DIRS[i]; i++)
		if (!strcasecmp(name, SKIP_DIRS[i])) return true;
	return false;
}

/* "Contra (USA).zip" -> "CONTRA (USA)"; the HUD font is uppercase-only. */
static void display_name(const char *filename, char *out, size_t n)
{
	const char *dot = strrchr(filename, '.');
	size_t len = dot ? (size_t)(dot - filename) : strlen(filename);
	size_t i;

	if (len >= n) len = n - 1;
	for (i = 0; i < len; i++) {
		char c = filename[i];
		out[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
	}
	out[len] = 0;
}

/* ---- verdict cache -------------------------------------------------------
 * The expensive part is not inflating once, it is inflating on every boot.
 * Keyed on path + size + mtime, so steady-state startup is a directory walk. */

typedef struct cache_row {
	char  path[SCAN_PATH];
	off_t size;
	long  mtime;
	int   verdict;
	float score;
	int   best_base;
	int   mapper;
	char  sha1[41];
	char  member[SCAN_NAME];
	struct cache_row *next;
} cache_row;

static cache_row *cache_load(const char *path)
{
	FILE *f = fopen(path, "r");
	cache_row *head = NULL;
	char line[SCAN_PATH + 256];

	if (!f) return NULL;
	while (fgets(line, sizeof(line), f)) {
		cache_row *r = (cache_row *)calloc(1, sizeof(*r));
		if (!r) break;
		/* path last: it may contain anything except a tab or newline */
		long long sz = 0;
		/* Read into a long long rather than punning &r->size: off_t width
		 * differs between the host build and the device toolchain. */
		if (sscanf(line, "%lld\t%ld\t%d\t%f\t%d\t%d\t%40s\t%159[^\t]\t%1023[^\n]",
		           &sz, &r->mtime, &r->verdict, &r->score,
		           &r->best_base, &r->mapper, r->sha1, r->member, r->path) == 9) {
			r->size = (off_t)sz;
			r->next = head; head = r;
		} else {
			free(r);
		}
	}
	fclose(f);
	return head;
}

static const cache_row *cache_get(const cache_row *head, const char *path,
                                  off_t size, long mtime)
{
	for (; head; head = head->next)
		if (head->size == size && head->mtime == mtime && !strcmp(head->path, path))
			return head;
	return NULL;
}

static void cache_free(cache_row *head)
{
	while (head) { cache_row *n = head->next; free(head); head = n; }
}

static void cache_save(const char *path, const ctr_list *l)
{
	FILE *f = fopen(path, "w");
	int i;
	if (!f) return;
	for (i = 0; i < l->count; i++) {
		const ctr_item *it = &l->items[i];
		fprintf(f, "%lld\t%ld\t%d\t%.4f\t%d\t%d\t%s\t%s\t%s\n",
		        (long long)it->size, it->mtime, (int)it->res.verdict,
		        it->res.score, it->res.best_base, it->res.mapper,
		        it->res.sha1[0] ? it->res.sha1 : "-",
		        it->member[0] ? it->member : "-", it->path);
	}
	fclose(f);
}

/* ---- walk ---------------------------------------------------------------- */

typedef struct {
	ctr_list        *out;
	contra_db       *db;
	const contra_bases *bases;
	const cache_row *cache;
	const char      *faces_dir;
} walk_ctx;

static void consider(walk_ctx *w, const char *path, const char *filename,
                     const struct stat *st)
{
	ctr_list *l = w->out;
	ctr_item *it;
	const cache_row *c;

	if (l->count >= SCAN_MAX_ITEMS) return;

	it = &l->items[l->count];
	memset(it, 0, sizeof(*it));
	snprintf(it->path, sizeof(it->path), "%s", path);
	display_name(filename, it->name, sizeof(it->name));
	it->size  = st->st_size;
	it->mtime = (long)st->st_mtime;

	c = cache_get(w->cache, it->path, it->size, it->mtime);
	if (c) {
		it->res.verdict   = (ctr_verdict)c->verdict;
		it->res.score     = c->score;
		it->res.best_base = c->best_base;
		it->res.mapper    = c->mapper;
		if (strcmp(c->sha1, "-")) snprintf(it->res.sha1, sizeof(it->res.sha1), "%s", c->sha1);
		if (strcmp(c->member, "-")) snprintf(it->member, sizeof(it->member), "%s", c->member);
		/* db may have gained an entry since the cache was written */
		it->res.entry = it->res.sha1[0] ? db_find(w->db, it->res.sha1) : NULL;
		if (it->res.entry) it->res.verdict = VD_VERIFIED;
		l->n_cached++;
	} else {
		rom_image img;
		nes_rom   rom;

		if (!rom_load(it->path, &img)) return;
		snprintf(it->member, sizeof(it->member), "%s", img.member);
		if (!nes_parse(img.data, img.len, &rom)) {
			it->res.verdict = VD_UNREADABLE;
		} else {
			ctr_classify(&rom, w->db, w->bases, &it->res);
		}
		rom_free(&img);
		l->n_read++;
	}

	if (it->res.sha1[0] && w->faces_dir && *w->faces_dir) {
		char face[SCAN_PATH];
		struct stat fs;
		snprintf(face, sizeof(face), "%s/%s.png", w->faces_dir, it->res.sha1);
		it->has_face = (stat(face, &fs) == 0 && fs.st_size > 0);
	}

	switch (it->res.verdict) {
	case VD_VERIFIED: l->n_verified++; break;
	case VD_HACK:     l->n_hack++;     break;
	default:          l->n_reject++;   break;
	}
	l->count++;
}

static void walk(walk_ctx *w, const char *dir, int depth)
{
	DIR *d;
	struct dirent *de;

	if (depth > 8) return;
	d = opendir(dir);
	if (!d) return;

	while ((de = readdir(d))) {
		char path[SCAN_PATH];
		struct stat st;

		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
		if (snprintf(path, sizeof(path), "%s/%s", dir, de->d_name) >= (int)sizeof(path))
			continue;
		if (stat(path, &st) != 0) continue;

		if (S_ISDIR(st.st_mode)) {
			if (!skip_dir(de->d_name)) walk(w, path, depth + 1);
		} else if (S_ISREG(st.st_mode) && rom_path_interesting(de->d_name)) {
			consider(w, path, de->d_name, &st);
		}
	}
	closedir(d);
}

/* ---- patches -------------------------------------------------------------
 * Contrarian/patches/ holds .ips/.bps. Each becomes its own channel, with the
 * patched image rebuilt in memory at scan time to identify it, and again into
 * /tmp at launch -- so a derived ROM never has to sit on the card.
 *
 * A BPS names its own base: the footer's source CRC-32 picks it out of the
 * ROMs already found, with no curation. An IPS carries no verification at all,
 * so its base is named by the folder it sits in. */

uint8_t *ctr_build_patched(const ctr_item *it, size_t *out_len)
{
	rom_image base, pat;
	uint8_t *out = NULL;

	if (it->pkind == PATCH_NONE || !it->base_path[0]) return NULL;
	if (!rom_load(it->base_path, &base)) return NULL;
	if (!rom_load(it->path, &pat)) { rom_free(&base); return NULL; }

	out = patch_apply(it->pkind, pat.data, pat.len, base.data, base.len, out_len);
	rom_free(&pat);
	rom_free(&base);
	return out;
}

static const char *IPS_BASE_DIRS[] = { "USA", "JAPAN", "EUROPE", NULL };

static void scan_patches(walk_ctx *w, const char *patches_dir)
{
	ctr_list *l = w->out;
	int base_first = l->count;   /* patch items are appended after real ROMs */
	int d;

	for (d = -1; IPS_BASE_DIRS[d + 1] || d < 0; d++) {
		char dir[SCAN_PATHC];
		DIR *dp;
		struct dirent *de;

		if (d < 0) snprintf(dir, sizeof dir, "%s", patches_dir);
		else       snprintf(dir, sizeof dir, "%s/%s", patches_dir, IPS_BASE_DIRS[d]);
		dp = opendir(dir);
		if (!dp) { if (d >= 0 && !IPS_BASE_DIRS[d + 1]) break; else continue; }

		while ((de = readdir(dp)) && l->count < SCAN_MAX_ITEMS) {
			patch_kind kind = patch_kind_of(de->d_name);
			rom_image pat;
			/* Compose straight into the slot this will occupy: no scratch
			 * buffer to truncate between, and the length check bounds the
			 * only field that matters. */
			ctr_item *it = &l->items[l->count];
			int i, chosen = -1;
			uint8_t *built = NULL;
			size_t built_len = 0;
			nes_rom rom;
			struct stat st;

			if (de->d_name[0] == '.' || kind == PATCH_NONE) continue;
			memset(it, 0, sizeof(*it));
			if (snprintf(it->path, sizeof it->path, "%s/%s", dir, de->d_name)
			    >= (int)sizeof it->path)
				continue;
			if (stat(it->path, &st) != 0) continue;
			if (!rom_load(it->path, &pat)) continue;

			if (kind == PATCH_BPS) {
				/* self-naming: match the footer's source CRC to a found ROM */
				uint32_t want = 0;
				if (bps_source_crc(pat.data, pat.len, &want)) {
					for (i = 0; i < base_first; i++) {
						rom_image b;
						if (l->items[i].res.verdict != VD_VERIFIED &&
						    l->items[i].res.verdict != VD_HACK) continue;
						if (!rom_load(l->items[i].path, &b)) continue;
						if (crc32_buf(b.data, b.len) == want) chosen = i;
						rom_free(&b);
						if (chosen >= 0) break;
					}
				}
			} else if (d >= 0) {
				/* IPS: the folder names the base */
				for (i = 0; i < base_first; i++) {
					const db_entry *e = l->items[i].res.entry;
					if (e && !strcasecmp(e->region, IPS_BASE_DIRS[d])) { chosen = i; break; }
				}
			}

			if (chosen >= 0) {
				rom_image b;
				if (rom_load(l->items[chosen].path, &b)) {
					built = patch_apply(kind, pat.data, pat.len, b.data, b.len, &built_len);
					rom_free(&b);
				}
			}
			rom_free(&pat);
			if (!built) continue;           /* wrong base, or a bad patch */

			snprintf(it->base_path, sizeof it->base_path, "%s", l->items[chosen].path);
			it->pkind = kind;
			display_name(de->d_name, it->name, sizeof it->name);
			it->size = st.st_size;
			it->mtime = (long)st.st_mtime;

			if (nes_parse(built, built_len, &rom))
				ctr_classify(&rom, w->db, w->bases, &it->res);
			else
				it->res.verdict = VD_UNREADABLE;
			free(built);

			if (w->faces_dir && it->res.sha1[0]) {
				char face[SCAN_PATHC];
				struct stat fs;
				snprintf(face, sizeof face, "%s/%s.png", w->faces_dir, it->res.sha1);
				it->has_face = (stat(face, &fs) == 0 && fs.st_size > 0);
			}
			switch (it->res.verdict) {
			case VD_VERIFIED: l->n_verified++; break;
			case VD_HACK:     l->n_hack++;     break;
			default:          l->n_reject++;   break;
			}
			l->count++;
		}
		closedir(dp);
		if (d >= 0 && !IPS_BASE_DIRS[d + 1]) break;
	}
}

/* Verified first, then hacks, then rejects; alphabetical within each group. */
static int cmp_item(const void *a, const void *b)
{
	const ctr_item *x = (const ctr_item *)a, *y = (const ctr_item *)b;
	if (x->res.verdict != y->res.verdict) return (int)x->res.verdict - (int)y->res.verdict;
	if (x->res.verdict == VD_HACK && x->res.score != y->res.score)
		return (x->res.score < y->res.score) ? 1 : -1;
	return strcasecmp(x->name, y->name);
}

bool ctr_scan(const char *root, const char *cache_path, const char *faces_dir,
              contra_db *db, const contra_bases *bases, ctr_list *out)
{
	char patches_dir[SCAN_PATH] = {0};
	walk_ctx w;
	cache_row *cache;

	memset(out, 0, sizeof(*out));
	if (faces_dir && *faces_dir) {
		/* patches/ sits beside faces/ under the Contrarian payload */
		const char *slash = strrchr(faces_dir, '/');
		if (slash) snprintf(patches_dir, sizeof patches_dir, "%.*s/patches",
		                    (int)(slash - faces_dir), faces_dir);
	}
	cache = cache_path ? cache_load(cache_path) : NULL;

	w.out = out; w.db = db; w.bases = bases; w.cache = cache; w.faces_dir = faces_dir;
	walk(&w, root, 0);
	if (patches_dir[0]) scan_patches(&w, patches_dir);
	cache_free(cache);

	qsort(out->items, (size_t)out->count, sizeof(ctr_item), cmp_item);
	if (cache_path) cache_save(cache_path, out);
	return true;
}
