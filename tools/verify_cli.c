/* SPDX-License-Identifier: 0BSD
 * Native harness: fingerprint ROMs and print the similarity matrix.
 * Used to validate the engine against known-good hashes on the host. */
#include "../src/nes.h"
#include "../src/romfile.h"
#include "../src/sha1.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	static rom_image img[16];
	static nes_rom   rom[16];
	static nes_fp    fp[16];
	static char      hex[16][41];
	const char      *label[16];
	int n = 0, i, j;

	if (argc < 2) { fprintf(stderr, "usage: verify_cli <rom>...\n"); return 2; }

	for (i = 1; i < argc && n < 16; i++) {
		const char *base = strrchr(argv[i], '/');
		base = base ? base + 1 : argv[i];
		if (!rom_load(argv[i], &img[n])) { printf("%-30s LOAD FAILED\n", base); continue; }
		if (!nes_parse(img[n].data, img[n].len, &rom[n])) {
			printf("%-30s NOT AN iNES IMAGE (%zu bytes)\n", base, img[n].len);
			rom_free(&img[n]);
			continue;
		}
		nes_payload_sha1(&rom[n], hex[n]);
		nes_fingerprint(&rom[n], &fp[n]);
		label[n] = base;
		printf("%-30s prg=%6zuK chr=%6zuK map=%3d nes2=%d  blocks=%3d/%3d  %s\n",
		       base, rom[n].prg_len/1024, rom[n].chr_len/1024, rom[n].mapper,
		       rom[n].nes2, fp[n].count, fp[n].total, hex[n]);
		if (img[n].member[0]) printf("%-30s   member: %s\n", "", img[n].member);
		n++;
	}

	printf("\nPRG similarity (%% of row's blocks present in column):\n%-30s", "");
	for (j = 0; j < n; j++) printf("%13.13s", label[j]);
	printf("\n");
	for (i = 0; i < n; i++) {
		printf("%-30.30s", label[i]);
		for (j = 0; j < n; j++) printf("%12.1f%%", nes_similarity(&fp[i], &fp[j]));
		printf("\n");
	}
	for (i = 0; i < n; i++) rom_free(&img[i]);
	return 0;
}
