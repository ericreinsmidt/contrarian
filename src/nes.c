/* SPDX-License-Identifier: 0BSD */
#include "nes.h"
#include "sha1.h"
#include <string.h>
#include <stdlib.h>

bool nes_parse(const uint8_t *buf, size_t n, nes_rom *out)
{
	size_t prg_n, chr_n, off;

	if (!buf || n < 16) return false;
	if (memcmp(buf, "NES\x1a", 4) != 0) return false;

	memset(out, 0, sizeof(*out));
	out->nes2 = ((buf[7] & 0x0C) == 0x08);
	out->has_trainer = (buf[6] & 0x04) != 0;

	prg_n = buf[4];
	chr_n = buf[5];
	if (out->nes2) {
		/* NES 2.0 widens the counts by 4 bits each in byte 9. The
		 * exponent form (nibble 0xF) is not used by anything Contra
		 * shaped, so the plain widening is enough here. */
		if ((buf[9] & 0x0F) != 0x0F) prg_n |= (size_t)(buf[9] & 0x0F) << 8;
		if ((buf[9] >> 4)   != 0x0F) chr_n |= (size_t)(buf[9] >> 4) << 8;
		out->mapper = ((buf[8] & 0x0F) << 8) | (buf[7] & 0xF0) | (buf[6] >> 4);
	} else {
		out->mapper = (buf[7] & 0xF0) | (buf[6] >> 4);
	}

	off = 16 + (out->has_trainer ? 512u : 0u);
	if (off > n) return false;

	out->payload     = buf + off;
	out->payload_len = n - off;

	out->prg_len = prg_n * 16384u;
	out->chr_len = chr_n * 8192u;

	/* Trust the file over the header: a truncated or over-long dump still
	 * needs to fingerprint against whatever PRG it actually has. */
	if (out->prg_len > out->payload_len) out->prg_len = out->payload_len;
	out->prg = out->payload;

	if (out->chr_len > out->payload_len - out->prg_len)
		out->chr_len = out->payload_len - out->prg_len;
	out->chr = out->payload + out->prg_len;

	return out->prg_len > 0;
}

void nes_payload_sha1(const nes_rom *r, char hex[41])
{
	sha1_hex(r->payload, r->payload_len, hex);
}

static bool uniform(const uint8_t *p, size_t n)
{
	size_t i;
	for (i = 1; i < n; i++) if (p[i] != p[0]) return false;
	return true;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return x < y ? -1 : (x > y ? 1 : 0);
}

void nes_fingerprint(const nes_rom *r, nes_fp *out)
{
	size_t off;
	int i, w;

	memset(out, 0, sizeof(*out));

	for (off = 0; off + NES_BLOCK <= r->prg_len; off += NES_BLOCK) {
		const uint8_t *p = r->prg + off;
		uint8_t d[20];
		uint64_t v;
		sha1_ctx c;

		if (out->total >= NES_MAX_BLOCKS) break;
		out->total++;
		if (uniform(p, NES_BLOCK)) continue;   /* padding matches anything */

		sha1_init(&c);
		sha1_update(&c, p, NES_BLOCK);
		sha1_final(&c, d);
		v = 0;
		for (i = 0; i < 8; i++) v = (v << 8) | d[i];
		out->blocks[out->count++] = v;
	}

	qsort(out->blocks, (size_t)out->count, sizeof(uint64_t), cmp_u64);

	/* Dedup: a repeated block must not count twice against the total. */
	for (i = 0, w = 0; i < out->count; i++)
		if (w == 0 || out->blocks[i] != out->blocks[w-1])
			out->blocks[w++] = out->blocks[i];
	out->count = w;
}

static bool has_block(const nes_fp *f, uint64_t v)
{
	int lo = 0, hi = f->count - 1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		if (f->blocks[mid] == v) return true;
		if (f->blocks[mid] < v) lo = mid + 1; else hi = mid - 1;
	}
	return false;
}

float nes_similarity(const nes_fp *a, const nes_fp *b)
{
	int i, hit = 0;
	if (a->count == 0 || b->count == 0) return 0.0f;
	for (i = 0; i < a->count; i++) if (has_block(b, a->blocks[i])) hit++;
	return 100.0f * (float)hit / (float)a->count;
}
