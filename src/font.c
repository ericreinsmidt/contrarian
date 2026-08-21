/* SPDX-License-Identifier: 0BSD */
#include "font.h"
#include "font_data.h"
#include <string.h>

#define GLYPH_COUNT ((FONT_LAST - FONT_FIRST + 1) + 2)
#define CELL_W (FONT_W + 1)

static SDL_Texture *atlas;

/* Map a byte (or a decoded codepoint) to an atlas slot. */
static int slot_of(unsigned c)
{
	if (c == FONT_DOT || c == FONT_BLOCK) return (FONT_LAST - FONT_FIRST + 1) + (int)c - 1;
	if (c >= 'a' && c <= 'z') c -= 32;                 /* the HUD is uppercase */
	if (c < FONT_FIRST || c > FONT_LAST) return 0;     /* space */
	return (int)c - FONT_FIRST;
}

/* Decodes just enough UTF-8 to turn U+00B7 MIDDLE DOT -- the separator the HUD
 * metadata line is built from -- into its private slot. Everything else
 * multi-byte collapses to a space rather than rendering mojibake. */
static int next_slot(const char **p)
{
	unsigned char c = (unsigned char)**p;

	if (c < 0x80) { (*p)++; return slot_of(c); }
	if ((c & 0xE0) == 0xC0 && ((unsigned char)(*p)[1] & 0xC0) == 0x80) {
		unsigned cp = ((unsigned)(c & 0x1F) << 6) | ((unsigned char)(*p)[1] & 0x3F);
		*p += 2;
		return slot_of(cp == 0x00B7 ? FONT_DOT : ' ');
	}
	while (**p && ((unsigned char)**p & 0xC0) == 0x80) (*p)++;
	if (**p) (*p)++;
	return slot_of(' ');
}

bool font_init(SDL_Renderer *r)
{
	SDL_Surface *s;
	int i, y, x;
	Uint32 *px;

	if (atlas) return true;
	s = SDL_CreateRGBSurfaceWithFormat(0, GLYPH_COUNT * CELL_W, FONT_H, 32,
	                                   SDL_PIXELFORMAT_RGBA32);
	if (!s) return false;
	SDL_FillRect(s, NULL, 0);
	px = (Uint32 *)s->pixels;

	for (i = 0; i < GLYPH_COUNT; i++)
		for (y = 0; y < FONT_H; y++)
			for (x = 0; x < FONT_W; x++)
				if (FONT_GLYPHS[i][y] & (1 << (FONT_W - 1 - x)))
					px[y * (s->pitch / 4) + i * CELL_W + x] = 0xFFFFFFFFu;

	atlas = SDL_CreateTextureFromSurface(r, s);
	SDL_FreeSurface(s);
	if (!atlas) return false;
	/* NEAREST: a pixel font must never be filtered. */
	SDL_SetTextureScaleMode(atlas, SDL_ScaleModeNearest);
	SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
	return true;
}

void font_quit(void)
{
	if (atlas) { SDL_DestroyTexture(atlas); atlas = NULL; }
}

int font_text_w(const char *s, int scale)
{
	int n = 0;
	if (!s) return 0;
	while (*s) { next_slot(&s); n++; }
	return n ? (n * CELL_W - 1) * scale : 0;
}

void font_draw(SDL_Renderer *r, const char *s, int x, int y, int scale, SDL_Color c)
{
	if (!atlas || !s) return;
	SDL_SetTextureColorMod(atlas, c.r, c.g, c.b);
	SDL_SetTextureAlphaMod(atlas, c.a);
	while (*s) {
		int g = next_slot(&s);
		SDL_Rect src = { g * CELL_W, 0, FONT_W, FONT_H };
		SDL_Rect dst = { x, y, FONT_W * scale, FONT_H * scale };
		SDL_RenderCopy(r, atlas, &src, &dst);
		x += CELL_W * scale;
	}
}

void font_draw_center(SDL_Renderer *r, const char *s, int cx, int y, int scale, SDL_Color c)
{
	font_draw(r, s, cx - font_text_w(s, scale) / 2, y, scale, c);
}
