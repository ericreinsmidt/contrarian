/* SPDX-License-Identifier: 0BSD */
#ifndef CTR_PATCH_H
#define CTR_PATCH_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ROMhacks are distributed as patches far more often than as whole ROMs, so
 * this is the primary way to add a version to Contrarian: drop the patch in
 * Contrarian/patches/ and it appears as its own channel.
 *
 * BPS carries a CRC-32 of its source and of its target in a 12-byte footer,
 * so a BPS-derived ROM is PROVABLY a hack of a specific base -- it verifies
 * itself, and no curation is needed to trust it.
 *
 * IPS carries no verification of any kind, so the base is named by the folder
 * the patch sits in (patches/USA/, patches/JAPAN/, patches/EUROPE/). */

typedef enum { PATCH_NONE = 0, PATCH_IPS, PATCH_BPS } patch_kind;

patch_kind patch_kind_of(const char *filename);

/* Apply patch to src, returning a newly malloc'd buffer. Caller frees.
 * For BPS the source CRC-32 is checked and the target CRC-32 verified;
 * a mismatch fails rather than producing a plausible-looking wrong ROM. */
uint8_t *patch_apply(patch_kind kind, const uint8_t *patch, size_t patch_len,
                     const uint8_t *src, size_t src_len, size_t *out_len);

/* Source CRC-32 a BPS expects, so the right base can be found without
 * applying anything. Returns false if this is not a usable BPS. */
bool bps_source_crc(const uint8_t *patch, size_t len, uint32_t *crc);

uint32_t crc32_buf(const uint8_t *p, size_t n);

#endif
