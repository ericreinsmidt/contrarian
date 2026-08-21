/* SPDX-License-Identifier: 0BSD */
#ifndef CTR_NES_H
#define CTR_NES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* iNES / NES 2.0 parsing and the fingerprints Contrarian identifies ROMs by.
 *
 * Identity is the PAYLOAD hash: SHA-1 of the file with the 16-byte header and
 * any trainer stripped. Header bytes 7-15 get dirtied constantly by tools, so
 * two dumps of the same ROM routinely disagree on the file hash while agreeing
 * perfectly on the payload.
 *
 * Derivation is measured over PRG only, in 1K aligned blocks. See DESIGN.md for
 * the measured similarity matrix that sets the threshold. */

#define NES_BLOCK      1024   /* fingerprint block size over PRG */
#define NES_MAX_BLOCKS 1024   /* 1MB of PRG; far beyond anything Contra-shaped */

typedef struct {
	const uint8_t *payload;      /* header/trainer stripped */
	size_t         payload_len;
	const uint8_t *prg;
	size_t         prg_len;
	const uint8_t *chr;
	size_t         chr_len;
	int            mapper;
	bool           nes2;
	bool           has_trainer;
} nes_rom;

/* Fingerprint: sorted, deduped 64-bit block digests over PRG.
 * Uniform-byte blocks ($00/$FF padding) are excluded -- otherwise any two
 * padded ROMs match each other on their padding alone. */
typedef struct {
	uint64_t blocks[NES_MAX_BLOCKS];
	int      count;    /* usable (non-uniform, deduped) blocks */
	int      total;    /* blocks examined, including uniform ones */
} nes_fp;

/* Returns false if buf is not a valid iNES/NES 2.0 image. */
bool nes_parse(const uint8_t *buf, size_t n, nes_rom *out);

/* 40 hex chars + NUL. */
void nes_payload_sha1(const nes_rom *r, char hex[41]);

void nes_fingerprint(const nes_rom *r, nes_fp *out);

/* Percentage of a's usable blocks that also appear in b (0.0 - 100.0). */
float nes_similarity(const nes_fp *a, const nes_fp *b);

#endif
