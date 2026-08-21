/* SPDX-License-Identifier: 0BSD */
#include "sha1.h"
#include <string.h>

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void sha1_block(sha1_ctx *c, const uint8_t *p)
{
	uint32_t w[80], a, b, d, e, f, k, t;
	uint32_t cc;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
		       ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
	for (i = 16; i < 80; i++)
		w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

	a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4];

	for (i = 0; i < 80; i++) {
		if (i < 20)      { f = (b & cc) | (~b & d);           k = 0x5A827999; }
		else if (i < 40) { f = b ^ cc ^ d;                    k = 0x6ED9EBA1; }
		else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
		else             { f = b ^ cc ^ d;                    k = 0xCA62C1D6; }
		t = rol(a, 5) + f + e + k + w[i];
		e = d; d = cc; cc = rol(b, 30); b = a; a = t;
	}

	c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

void sha1_init(sha1_ctx *c)
{
	c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE;
	c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
	c->len = 0; c->buflen = 0;
}

void sha1_update(sha1_ctx *c, const void *data, size_t n)
{
	const uint8_t *p = (const uint8_t *)data;

	c->len += (uint64_t)n * 8;
	if (c->buflen) {
		size_t take = 64 - c->buflen;
		if (take > n) take = n;
		memcpy(c->buf + c->buflen, p, take);
		c->buflen += take; p += take; n -= take;
		if (c->buflen == 64) { sha1_block(c, c->buf); c->buflen = 0; }
	}
	while (n >= 64) { sha1_block(c, p); p += 64; n -= 64; }
	if (n) { memcpy(c->buf, p, n); c->buflen = n; }
}

void sha1_final(sha1_ctx *c, uint8_t out[20])
{
	uint64_t bits = c->len;
	uint8_t pad = 0x80;
	uint8_t lenbe[8];
	int i;

	sha1_update(c, &pad, 1);
	c->len = bits; /* padding must not count toward the length */
	while (c->buflen != 56) {
		uint8_t z = 0;
		sha1_update(c, &z, 1);
		c->len = bits;
	}
	for (i = 0; i < 8; i++) lenbe[i] = (uint8_t)(bits >> (56 - i * 8));
	sha1_update(c, lenbe, 8);

	for (i = 0; i < 5; i++) {
		out[i*4]   = (uint8_t)(c->h[i] >> 24);
		out[i*4+1] = (uint8_t)(c->h[i] >> 16);
		out[i*4+2] = (uint8_t)(c->h[i] >> 8);
		out[i*4+3] = (uint8_t)(c->h[i]);
	}
}

void sha1_tohex(const uint8_t d[20], char hex[41])
{
	static const char *H = "0123456789abcdef";
	int i;
	for (i = 0; i < 20; i++) { hex[i*2] = H[d[i] >> 4]; hex[i*2+1] = H[d[i] & 15]; }
	hex[40] = 0;
}

void sha1_hex(const void *data, size_t n, char hex[41])
{
	sha1_ctx c; uint8_t d[20];
	sha1_init(&c); sha1_update(&c, data, n); sha1_final(&c, d);
	sha1_tohex(d, hex);
}
