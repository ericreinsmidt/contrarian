/* SPDX-License-Identifier: 0BSD */
#include "romfile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

#define ZIP_EOCD_SIG 0x06054b50u
#define ZIP_CEN_SIG  0x02014b50u
#define ZIP_LOC_SIG  0x04034b50u

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1]<<8)); }

static bool ends_with_ci(const char *s, const char *suf)
{
	size_t ls = strlen(s), lf = strlen(suf);
	return ls >= lf && strcasecmp(s + ls - lf, suf) == 0;
}

bool rom_path_interesting(const char *filename)
{
	if (!filename || !*filename) return false;
	/* Leading dot covers .Trashes, .userdata, and -- the one that actually
	 * bites -- macOS AppleDouble sidecars like "._Contra (USA).zip", which end
	 * in .zip, sail through a naive glob, and then parse as garbage. */
	if (filename[0] == '.') return false;
	return ends_with_ci(filename, ".nes") || ends_with_ci(filename, ".zip");
}

static uint8_t *slurp(const char *path, size_t *out_len)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf;
	long n;

	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
	n = ftell(f);
	if (n <= 0 || n > 64L*1024*1024) { fclose(f); return NULL; }
	rewind(f);
	buf = (uint8_t *)malloc((size_t)n);
	if (!buf) { fclose(f); return NULL; }
	if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
	fclose(f);
	*out_len = (size_t)n;
	return buf;
}

/* Locate the .nes member in the central directory and inflate just that one. */
static bool unzip_nes(const uint8_t *zip, size_t zn, rom_image *out)
{
	const uint8_t *eocd = NULL;
	size_t i, cd_off, cd_size, p;
	uint16_t nent;
	bool found = false;
	uint16_t method = 0;
	uint32_t csize = 0, usize = 0, loff = 0;
	char name[256] = {0};

	if (zn < 22) return false;
	for (i = zn - 22; ; i--) {                    /* EOCD, scanning back */
		if (rd32(zip + i) == ZIP_EOCD_SIG) { eocd = zip + i; break; }
		if (i == 0 || zn - i > 66000) break;
	}
	if (!eocd) return false;

	nent    = rd16(eocd + 10);
	cd_size = rd32(eocd + 12);
	cd_off  = rd32(eocd + 16);
	if (cd_off + cd_size > zn) return false;

	p = cd_off;
	for (i = 0; i < nent && p + 46 <= zn; i++) {
		uint16_t nlen, elen, clen;
		if (rd32(zip + p) != ZIP_CEN_SIG) break;
		nlen = rd16(zip + p + 28);
		elen = rd16(zip + p + 30);
		clen = rd16(zip + p + 32);
		if (p + 46 + nlen > zn) break;
		if (!found && nlen < sizeof(name)) {
			memcpy(name, zip + p + 46, nlen);
			name[nlen] = 0;
			if (ends_with_ci(name, ".nes") && name[0] != '.') {
				method = rd16(zip + p + 10);
				csize  = rd32(zip + p + 20);
				usize  = rd32(zip + p + 24);
				loff   = rd32(zip + p + 42);
				found  = true;
			} else {
				name[0] = 0;
			}
		}
		p += 46u + nlen + elen + clen;
	}
	if (!found) return false;

	/* Local header repeats the name/extra lengths; the payload follows it. */
	if ((size_t)loff + 30 > zn || rd32(zip + loff) != ZIP_LOC_SIG) return false;
	{
		size_t data = (size_t)loff + 30u + rd16(zip + loff + 26) + rd16(zip + loff + 28);
		uint8_t *dst;

		if (data + csize > zn) return false;
		if (usize == 0 || usize > 16u*1024*1024) return false;

		dst = (uint8_t *)malloc(usize);
		if (!dst) return false;

		if (method == 0) {
			if (csize != usize) { free(dst); return false; }
			memcpy(dst, zip + data, usize);
		} else if (method == 8) {
			z_stream zs;
			int rc;
			memset(&zs, 0, sizeof(zs));
			if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) { free(dst); return false; }
			zs.next_in   = (Bytef *)(zip + data);
			zs.avail_in  = csize;
			zs.next_out  = dst;
			zs.avail_out = usize;
			rc = inflate(&zs, Z_FINISH);
			inflateEnd(&zs);
			if (rc != Z_STREAM_END) { free(dst); return false; }
		} else {
			free(dst);
			return false;
		}

		out->data = dst;
		out->len  = usize;
		snprintf(out->member, sizeof(out->member), "%s", name);
		return true;
	}
}

bool rom_load(const char *path, rom_image *out)
{
	size_t n = 0;
	uint8_t *buf;

	memset(out, 0, sizeof(*out));
	buf = slurp(path, &n);
	if (!buf) return false;

	if (ends_with_ci(path, ".zip")) {
		bool ok = unzip_nes(buf, n, out);
		free(buf);
		return ok;
	}

	out->data = buf;
	out->len  = n;
	out->member[0] = 0;
	return true;
}

void rom_free(rom_image *img)
{
	if (img && img->data) { free(img->data); img->data = NULL; img->len = 0; }
}
