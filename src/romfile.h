/* SPDX-License-Identifier: 0BSD */
#ifndef CTR_ROMFILE_H
#define CTR_ROMFILE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Loads a ROM image into memory from a bare .nes or from a .zip.
 *
 * Zips are inflated in memory, never extracted to disk -- SD writes are the
 * slowest thing on the device and an extracted copy would litter the card.
 * The .nes member is selected explicitly: archives in the wild carry junk
 * siblings (readme/nfo/txt from whatever site they came from), and streaming
 * a whole archive produces a hash for the concatenation rather than the ROM. */

typedef struct {
	uint8_t *data;
	size_t   len;
	char     member[256];   /* the .nes member's name, or "" for a bare file */
} rom_image;

bool rom_load(const char *path, rom_image *out);
void rom_free(rom_image *img);

/* True for paths the scanner should consider at all. Rejects macOS AppleDouble
 * sidecars (._Foo.zip), which end in .zip and otherwise parse as garbage. */
bool rom_path_interesting(const char *filename);

#endif
