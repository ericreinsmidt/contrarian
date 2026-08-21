/* SPDX-License-Identifier: 0BSD
 * Build res/contra.fp from the three base ROMs.
 *   mkfp <out.fp> <name=path> ...
 * Emits only truncated one-way block digests -- no ROM content ships. */
#include "../src/db.h"
#include "../src/nes.h"
#include "../src/romfile.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	static contra_bases bases;
	int i;

	if (argc < 3) { fprintf(stderr, "usage: mkfp <out.fp> <name=path>...\n"); return 2; }

	for (i = 2; i < argc && bases.count < DB_MAX_BASE; i++) {
		char *eq = strchr(argv[i], '=');
		rom_image img;
		nes_rom   rom;
		db_base  *b;

		if (!eq) { fprintf(stderr, "bad arg %s\n", argv[i]); return 2; }
		*eq = 0;
		if (!rom_load(eq + 1, &img))              { fprintf(stderr, "load %s\n", eq+1); return 1; }
		if (!nes_parse(img.data, img.len, &rom))  { fprintf(stderr, "parse %s\n", eq+1); return 1; }

		b = &bases.b[bases.count++];
		snprintf(b->name, sizeof(b->name), "%s", argv[i]);
		nes_payload_sha1(&rom, b->sha1);
		nes_fingerprint(&rom, &b->fp);
		printf("%-14s %s  %3d/%3d blocks\n", b->name, b->sha1, b->fp.count, b->fp.total);
		rom_free(&img);
	}

	if (!db_save_bases(argv[1], &bases)) { fprintf(stderr, "write %s\n", argv[1]); return 1; }
	printf("wrote %s (%d bases)\n", argv[1], bases.count);
	return 0;
}
