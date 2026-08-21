/* SPDX-License-Identifier: 0BSD */
#include "patch.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

uint32_t crc32_buf(const uint8_t *p, size_t n)
{
	static uint32_t tbl[256];
	static int init = 0;
	uint32_t c = 0xFFFFFFFFu;
	size_t i;

	if (!init) {
		uint32_t k;
		int j;
		for (k = 0; k < 256; k++) {
			uint32_t v = k;
			for (j = 0; j < 8; j++) v = (v & 1) ? (0xEDB88320u ^ (v >> 1)) : (v >> 1);
			tbl[k] = v;
		}
		init = 1;
	}
	for (i = 0; i < n; i++) c = tbl[(c ^ p[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

patch_kind patch_kind_of(const char *filename)
{
	size_t n = filename ? strlen(filename) : 0;
	if (n > 4 && !strcasecmp(filename + n - 4, ".ips")) return PATCH_IPS;
	if (n > 4 && !strcasecmp(filename + n - 4, ".bps")) return PATCH_BPS;
	return PATCH_NONE;
}

/* ---- IPS ---------------------------------------------------------------- */
/* "PATCH", then records of a 3-byte big-endian offset and a 2-byte length;
 * length 0 marks an RLE run (2-byte count, one byte value). Ends at "EOF". */
static uint8_t *ips_apply(const uint8_t *p, size_t pn,
                          const uint8_t *src, size_t sn, size_t *out_len)
{
	uint8_t *out;
	size_t i = 5, cap = sn;

	if (pn < 8 || memcmp(p, "PATCH", 5) != 0) return NULL;

	/* A patch may extend the ROM, so size the buffer to the furthest write. */
	while (i + 3 <= pn) {
		size_t off, len;
		if (!memcmp(p + i, "EOF", 3)) break;
		if (i + 5 > pn) return NULL;
		off = ((size_t)p[i] << 16) | ((size_t)p[i+1] << 8) | p[i+2];
		len = ((size_t)p[i+3] << 8) | p[i+4];
		i += 5;
		if (len == 0) {
			if (i + 3 > pn) return NULL;
			len = ((size_t)p[i] << 8) | p[i+1];
			i += 3;
		} else {
			if (i + len > pn) return NULL;
			i += len;
		}
		if (off + len > cap) cap = off + len;
	}

	out = (uint8_t *)malloc(cap ? cap : 1);
	if (!out) return NULL;
	memset(out, 0, cap);
	memcpy(out, src, sn);

	i = 5;
	while (i + 3 <= pn) {
		size_t off, len;
		if (!memcmp(p + i, "EOF", 3)) break;
		off = ((size_t)p[i] << 16) | ((size_t)p[i+1] << 8) | p[i+2];
		len = ((size_t)p[i+3] << 8) | p[i+4];
		i += 5;
		if (len == 0) {
			size_t rle = ((size_t)p[i] << 8) | p[i+1];
			uint8_t v = p[i+2];
			i += 3;
			if (off + rle > cap) { free(out); return NULL; }
			memset(out + off, v, rle);
		} else {
			if (off + len > cap) { free(out); return NULL; }
			memcpy(out + off, p + i, len);
			i += len;
		}
	}
	*out_len = cap;
	return out;
}

/* ---- BPS ---------------------------------------------------------------- */

static uint64_t bps_varint(const uint8_t *p, size_t n, size_t *i)
{
	uint64_t data = 0, shift = 1;
	while (*i < n) {
		uint8_t x = p[(*i)++];
		data += (uint64_t)(x & 0x7F) * shift;
		if (x & 0x80) break;
		shift <<= 7;
		data += shift;
	}
	return data;
}

static uint32_t rd32le(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool bps_source_crc(const uint8_t *p, size_t n, uint32_t *crc)
{
	if (n < 4 + 12 || memcmp(p, "BPS1", 4) != 0) return false;
	*crc = rd32le(p + n - 12);
	return true;
}

static uint8_t *bps_apply(const uint8_t *p, size_t pn,
                          const uint8_t *src, size_t sn, size_t *out_len)
{
	size_t i = 4, out_off = 0;
	uint64_t src_size, tgt_size, meta_size;
	int64_t src_rel = 0, tgt_rel = 0;
	uint8_t *out;
	uint32_t want_src, want_tgt;

	if (pn < 4 + 12 || memcmp(p, "BPS1", 4) != 0) return NULL;

	src_size  = bps_varint(p, pn, &i);
	tgt_size  = bps_varint(p, pn, &i);
	meta_size = bps_varint(p, pn, &i);
	i += (size_t)meta_size;
	if (i > pn || src_size != sn) return NULL;

	want_src = rd32le(p + pn - 12);
	want_tgt = rd32le(p + pn - 8);
	if (crc32_buf(src, sn) != want_src) return NULL;   /* wrong base */

	if (tgt_size == 0 || tgt_size > 64u * 1024 * 1024) return NULL;
	out = (uint8_t *)malloc((size_t)tgt_size);
	if (!out) return NULL;

	while (i < pn - 12 && out_off < tgt_size) {
		uint64_t d      = bps_varint(p, pn - 12, &i);
		uint64_t action = d & 3;
		uint64_t len    = (d >> 2) + 1;
		uint64_t k;

		if (out_off + len > tgt_size) { free(out); return NULL; }
		switch (action) {
		case 0:  /* SourceRead */
			for (k = 0; k < len; k++) {
				if (out_off + k >= sn) { free(out); return NULL; }
				out[out_off + k] = src[out_off + k];
			}
			break;
		case 1:  /* TargetRead */
			if (i + len > pn - 12) { free(out); return NULL; }
			memcpy(out + out_off, p + i, (size_t)len);
			i += (size_t)len;
			break;
		case 2: { /* SourceCopy */
			uint64_t v = bps_varint(p, pn - 12, &i);
			src_rel += (v & 1) ? -(int64_t)(v >> 1) : (int64_t)(v >> 1);
			for (k = 0; k < len; k++) {
				if (src_rel < 0 || (size_t)src_rel >= sn) { free(out); return NULL; }
				out[out_off + k] = src[src_rel++];
			}
			break;
		}
		default: { /* TargetCopy */
			uint64_t v = bps_varint(p, pn - 12, &i);
			tgt_rel += (v & 1) ? -(int64_t)(v >> 1) : (int64_t)(v >> 1);
			for (k = 0; k < len; k++) {
				if (tgt_rel < 0 || (size_t)tgt_rel >= tgt_size) { free(out); return NULL; }
				out[out_off + k] = out[tgt_rel++];
			}
			break;
		}
		}
		out_off += len;
	}

	if (out_off != tgt_size || crc32_buf(out, (size_t)tgt_size) != want_tgt) {
		free(out);          /* the patch applied but the result is not what it
		                     * promised -- refuse rather than ship a wrong ROM */
		return NULL;
	}
	*out_len = (size_t)tgt_size;
	return out;
}

uint8_t *patch_apply(patch_kind kind, const uint8_t *patch, size_t patch_len,
                     const uint8_t *src, size_t src_len, size_t *out_len)
{
	if (kind == PATCH_IPS) return ips_apply(patch, patch_len, src, src_len, out_len);
	if (kind == PATCH_BPS) return bps_apply(patch, patch_len, src, src_len, out_len);
	return NULL;
}
