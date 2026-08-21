/* SPDX-License-Identifier: 0BSD */
#ifndef CTR_SHA1_H
#define CTR_SHA1_H

#include <stddef.h>
#include <stdint.h>

/* Minimal SHA-1. Bundled rather than linked against libcrypto: the device's
 * libcrypto is a vendored 1.1 that we would otherwise have no reason to carry,
 * and ROM payloads are at most a few hundred KB. */

typedef struct {
	uint32_t h[5];
	uint64_t len;
	uint8_t  buf[64];
	size_t   buflen;
} sha1_ctx;

void sha1_init(sha1_ctx *c);
void sha1_update(sha1_ctx *c, const void *data, size_t n);
void sha1_final(sha1_ctx *c, uint8_t out[20]);
/* One-shot; writes 40 lowercase hex chars + NUL into hex[41]. */
void sha1_hex(const void *data, size_t n, char hex[41]);
void sha1_tohex(const uint8_t d[20], char hex[41]);

#endif
