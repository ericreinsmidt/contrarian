/* SPDX-License-Identifier: 0BSD */
#include "app.h"
#include <math.h>
#include <stdio.h>

const SDL_Color C_TITLE = { 242, 239, 230, 255 };
const SDL_Color C_META  = { 214, 150,  64, 255 };
const SDL_Color C_BAD   = { 216,  69,  58, 255 };
const SDL_Color C_DIM   = { 132, 118, 100, 255 };

SDL_Surface *app_load_surf(app *a, const char *sub, const char *name)
{
	char p[SCAN_PATHC];
	SDL_Surface *s;
	snprintf(p, sizeof p, "%s/%s/%s", a->res_dir, sub, name);
	s = IMG_Load(p);
	if (!s) fprintf(stderr, "missing asset %s\n", p);
	return s;
}

SDL_Texture *app_load_tex(app *a, const char *sub, const char *name)
{
	SDL_Surface *s = app_load_surf(a, sub, name);
	SDL_Texture *t;
	if (!s) return NULL;
	t = SDL_CreateTextureFromSurface(a->r, s);
	SDL_FreeSurface(s);
	if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	return t;
}

SDL_Texture *app_load_face(app *a, const char *sha1)
{
	char p[SCAN_PATHC];
	SDL_Surface *s;
	SDL_Texture *t;
	snprintf(p, sizeof p, "%s/%s.png", a->faces_dir, sha1);
	s = IMG_Load(p);
	if (!s) return NULL;
	t = SDL_CreateTextureFromSurface(a->r, s);
	SDL_FreeSurface(s);
	if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	return t;
}

/* Ring plus bar. Two things matter and both were wrong before:
 *
 * 1. The bar is DIAGONAL, so sweeping it from -rad to +rad in x puts its
 *    endpoints at rad*sqrt(2) -- outside the ring. It has to stop at the
 *    ring's inner edge, which on a 45-degree line means sweeping along the
 *    bar's own direction, not along an axis.
 * 2. Thickening a diagonal bar in x alone gives it a perpendicular weight of
 *    thick/sqrt(2), so it reads thinner than the ring it sits in. The offset
 *    has to run perpendicular to the bar.
 *
 * The ring is filled by testing the annulus in squared distance: even weight
 * all the way round, no gaps, and no sqrt in the loop. */
void app_draw_no_symbol(SDL_Renderer *r, int cx, int cy, int rad, int thick,
                        SDL_Color c)
{
	const float INV_SQRT2 = 0.70710678f;
	int half  = thick / 2;
	int inner = rad - half;          /* the bar stops here */
	int lo    = rad - half, hi = rad + half;
	int lo2   = lo * lo, hi2 = hi * hi;
	int x, y, s;

	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);

	for (y = -hi; y <= hi; y++) {
		for (x = -hi; x <= hi; x++) {
			int d2 = x * x + y * y;
			if (d2 >= lo2 && d2 <= hi2)
				SDL_RenderDrawPoint(r, cx + x, cy + y);
		}
	}

	/* Half-pixel steps: a diagonal sweep at whole steps leaves a ragged edge. */
	for (s = -inner * 2; s <= inner * 2; s++) {
		float t = (float)s * 0.5f;
		int   w;
		for (w = -half * 2; w <= half * 2; w++) {
			float o  = (float)w * 0.5f;
			/* along the bar          + perpendicular to it */
			float px =  t * INV_SQRT2 + o * INV_SQRT2;
			float py = -t * INV_SQRT2 + o * INV_SQRT2;
			SDL_RenderDrawPoint(r, cx + (int)lroundf(px), cy + (int)lroundf(py));
		}
	}
}

const ctr_item *app_current(const app *a)
{
	if (a->list.count <= 0) return NULL;
	if (a->cursor < 0 || a->cursor >= a->list.count) return NULL;
	return &a->list.items[a->cursor];
}
