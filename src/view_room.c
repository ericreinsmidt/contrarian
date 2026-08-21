/* SPDX-License-Identifier: 0BSD */
#include "room.h"
#include "font.h"
#include "view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A photographed RCA ColorTrak console filling the panel, its tube keyed out
 * to a hole. Versions are channels; the d-pad retunes. Nothing scrolls,
 * because a television does not scroll -- it retunes, and for a moment while
 * it retunes you get static.
 *
 * Draw order is the whole trick: tube contents first, then the silver trim
 * ring, then the cabinet over everything. The cabinet's alpha hole is the
 * only mask needed, so nothing else has to be clipped. */

typedef struct {
	SDL_Texture *set, *bezel, *glass, *testcard;
	SDL_Texture *stat[STATIC_TILES];
	SDL_Texture *hum;
	SDL_Texture *face[SCAN_MAX_ITEMS];
	bool         face_tried[SCAN_MAX_ITEMS];
} room;

static room *R(app *a) { return (room *)a->vdata; }

static bool room_load(app *a)
{
	room *m = (room *)calloc(1, sizeof(room));
	int i;
	if (!m) return false;
	a->vdata = m;

	m->set      = app_load_tex(a, "room", "set.png");
	m->bezel    = app_load_tex(a, "room", "bezel.png");
	m->glass    = app_load_tex(a, "room", "glass.png");
	m->testcard = app_load_tex(a, "room", "testcard.png");
	for (i = 0; i < STATIC_TILES; i++) {
		char n[32]; snprintf(n, sizeof n, "static%d.png", i);
		m->stat[i] = app_load_tex(a, "room", n);
	}
	m->hum = app_load_tex(a, "room", "hum.png");
	return m->set != NULL;
}

static void room_unload(app *a)
{
	room *m = R(a);
	int i;
	if (!m) return;
	#define D(t) do { if (t) { SDL_DestroyTexture(t); (t) = NULL; } } while (0)
	D(m->set); D(m->bezel); D(m->glass); D(m->testcard);
	for (i = 0; i < STATIC_TILES; i++) D(m->stat[i]);
	D(m->hum);
	for (i = 0; i < SCAN_MAX_ITEMS; i++) { D(m->face[i]); m->face_tried[i] = false; }
	#undef D
	free(m);
	a->vdata = NULL;
}

/* A channel's picture is that version's own title screen, captured on device
 * the first time it was played. Loaded lazily and cached. */
static SDL_Texture *face_for(app *a, int i)
{
	room *m = R(a);
	const ctr_item *it;

	if (i < 0 || i >= a->list.count) return NULL;
	if (m->face[i]) return m->face[i];
	if (m->face_tried[i]) return NULL;
	m->face_tried[i] = true;

	it = &a->list.items[i];
	if (!it->has_face || !it->res.sha1[0]) return NULL;
	m->face[i] = app_load_face(a, it->res.sha1);
	return m->face[i];
}

/* ---- tube contents ------------------------------------------------------ */

static const SDL_Rect TUBE = { TUBE_X, TUBE_Y, TUBE_W, TUBE_H };

static void draw_static(app *a, unsigned t, Uint8 alpha)
{
	room *m = R(a);
	SDL_Texture *tex = m->stat[(t / 55u) % STATIC_TILES];
	if (!tex) return;
	SDL_SetTextureAlphaMod(tex, alpha);
	SDL_RenderCopy(a->r, tex, NULL, &TUBE);
}

static float tune_progress(app *a, unsigned now)
{
	float k = (float)(now - a->cursor_ms) / (float)TUNE_MS;
	return k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k);
}

/* The retune: static with the picture rolling through it, settling as the
 * channel locks -- which is what a set actually does between channels. */
static void draw_channel(app *a, unsigned t, float k)
{
	room *m = R(a);
	const ctr_item *it = app_current(a);
	SDL_Texture *pic;
	int roll;

	if (!it) return;

	if (it->res.verdict == VD_REJECT || it->res.verdict == VD_UNREADABLE) {
		draw_static(a, t, 235);
		return;
	}

	pic = face_for(a, a->cursor);
	if (!pic) pic = m->testcard;      /* never played: the channel shows a test card */

	/* Vertical hold: the picture slips upward and wraps, settling as the
	 * channel locks. ROLLS is how many full passes it makes, and the offset
	 * MUST wrap within the tube -- an unwrapped value is larger than the tube
	 * for the first part of the sweep, which gives a negative-height source
	 * rect and a visible tear rather than a roll. */
	roll = (int)((1.0f - k) * (1.0f - k) * TUBE_H * ROLLS) % TUBE_H;

	if (pic) {
		if (roll > 0) {
			/* Split in the TEXTURE's own coordinates. A captured face is a
			 * screen-sized grab, not tube-sized, so using tube dimensions as
			 * source coordinates sampled a crop of it -- which is why only
			 * channels that had been played tore, and the test card did not. */
			int pw = 0, ph = 0, split;
			SDL_QueryTexture(pic, NULL, NULL, &pw, &ph);
			if (pw <= 0 || ph <= 0) { pw = TUBE_W; ph = TUBE_H; }
			split = ph - (int)((float)roll / (float)TUBE_H * (float)ph);
			{
				SDL_Rect s1 = { 0, 0, pw, split };
				SDL_Rect s2 = { 0, split, pw, ph - split };
				SDL_Rect d1 = { TUBE_X, TUBE_Y + roll, TUBE_W, TUBE_H - roll };
				SDL_Rect d2 = { TUBE_X, TUBE_Y, TUBE_W, roll };
				if (s1.h > 0) SDL_RenderCopy(a->r, pic, &s1, &d1);
				if (s2.h > 0) SDL_RenderCopy(a->r, pic, &s2, &d2);
			}
		} else {
			SDL_RenderCopy(a->r, pic, NULL, &TUBE);
		}
	}
	if (k < 1.0f) draw_static(a, t, (Uint8)((1.0f - k) * 230));
}

static void draw_reject_mark(app *a, float k)
{
	const ctr_item *it = app_current(a);
	int cx = TUBE_X + TUBE_W / 2, cy = TUBE_Y + TUBE_H / 2 - 14;
	char pct[64];
	Uint8 al;

	if (!it) return;
	if (it->res.verdict != VD_REJECT && it->res.verdict != VD_UNREADABLE) return;
	if (k < 0.75f) return;
	al = (Uint8)((k - 0.75f) / 0.25f * 255.0f);

	app_draw_no_symbol(a->r, cx, cy, 82, 18,
	                   (SDL_Color){ C_BAD.r, C_BAD.g, C_BAD.b, al });

	if (it->res.verdict == VD_REJECT)
		snprintf(pct, sizeof pct, "%.1f%% CONTRA", (double)it->res.score);
	else
		snprintf(pct, sizeof pct, "NOT A NES ROM");
	font_draw_center(a->r, pct, cx, cy + 104, 3, (SDL_Color){ 255, 255, 255, al });
}

/* The channel ident: a banner inside the tube that fades, like a real set's
 * on-screen display. */
static void draw_banner(app *a, unsigned now)
{
	const ctr_item *it = app_current(a);
	char ch[16], meta[160];
	const char *title;
	unsigned age = now - a->cursor_ms;
	int y, alpha;
	SDL_Rect bar;

	if (!it || age > 2600u) return;
	alpha = (age < 2100u) ? 255 : (int)(255 - (age - 2100u) * 255 / 500u);
	if (alpha < 0) alpha = 0;

	title = (it->res.entry && it->res.entry->title[0]) ? it->res.entry->title : it->name;
	ctr_describe(&it->res, &a->bases, meta, sizeof meta);
	snprintf(ch, sizeof ch, "CH %02d", a->cursor + 1);

	bar = (SDL_Rect){ TUBE_X + 16, TUBE_Y + TUBE_H - 100, TUBE_W - 32, 82 };
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, (Uint8)(alpha * 150 / 255));
	SDL_RenderFillRect(a->r, &bar);
	SDL_SetRenderDrawColor(a->r, C_META.r, C_META.g, C_META.b, (Uint8)alpha);
	SDL_RenderFillRect(a->r, &(SDL_Rect){ bar.x, bar.y, bar.w, 2 });

	y = bar.y + 13;
	font_draw(a->r, ch, bar.x + 13, y, 3,
	          (SDL_Color){ C_META.r, C_META.g, C_META.b, (Uint8)alpha });
	font_draw(a->r, title, bar.x + 13 + font_text_w("CH 00", 3) + 16, y, 3,
	          (SDL_Color){ C_TITLE.r, C_TITLE.g, C_TITLE.b, (Uint8)alpha });
	font_draw(a->r, meta, bar.x + 13, y + FONT_LINE_H(3) + 9, 2,
	          (it->res.verdict == VD_VERIFIED || it->res.verdict == VD_HACK)
	          ? (SDL_Color){ C_META.r, C_META.g, C_META.b, (Uint8)alpha }
	          : (SDL_Color){ C_BAD.r, C_BAD.g, C_BAD.b, (Uint8)alpha });
}

static void room_draw(app *a)
{
	room *m = R(a);
	unsigned now = plat_now_ms(), t = now - a->t0;
	float k = tune_progress(a, now);
	const ctr_item *it = app_current(a);

	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 255);
	SDL_RenderClear(a->r);

	/* Everything inside the tube first; the cabinet covers any overflow. */
	SDL_SetRenderDrawColor(a->r, 5, 6, 6, 255);
	SDL_RenderFillRect(a->r, &TUBE);

	if (a->farewell) {
		/* Powering down: the set shows GAME OVER and nothing else -- no ident,
		 * no hum bar, no refusal mark. It is not tuned to anything any more. */
		SDL_RenderCopy(a->r, a->farewell, NULL, &TUBE);
		if (m->glass) SDL_RenderCopy(a->r, m->glass, NULL, &TUBE);
		if (m->bezel) SDL_RenderCopy(a->r, m->bezel, NULL, &TUBE);
		if (m->set)   SDL_RenderCopy(a->r, m->set, NULL, NULL);
		return;
	}

	draw_channel(a, t, k);
	draw_reject_mark(a, k);

	if (it && m->hum &&
	    (it->res.verdict == VD_VERIFIED || it->res.verdict == VD_HACK)) {
		/* Continuous drift: position is a function of elapsed time, so it moves
		 * every frame rather than jumping between fixed offsets. Travels from
		 * just above the tube to just below, and the cabinet covers the spill. */
		float p = (float)(t % HUM_PERIOD_MS) / (float)HUM_PERIOD_MS;
		int span = TUBE_H + HUM_H * 2;
		SDL_Rect dst = { TUBE_X, TUBE_Y - HUM_H + (int)(p * span), TUBE_W, HUM_H };
		SDL_SetTextureBlendMode(m->hum, SDL_BLENDMODE_ADD);
		SDL_RenderCopy(a->r, m->hum, NULL, &dst);
	}
	draw_banner(a, now);
	if (m->glass) SDL_RenderCopy(a->r, m->glass, NULL, &TUBE);
	if (m->bezel) SDL_RenderCopy(a->r, m->bezel, NULL, &TUBE);
	if (m->set)   SDL_RenderCopy(a->r, m->set, NULL, NULL);

	/* No button hints: a television does not caption its own remote. What is
	 * on the channel, and the ident when you retune, is the whole interface. */
	if (a->list.count == 0)
		font_draw_center(a->r, "NO SIGNAL", TUBE_X + TUBE_W / 2,
		                 TUBE_Y + TUBE_H / 2 - 30, 5, C_TITLE);
}

const ctr_view VIEW_ROOM = {
	.name = "room", .label = "SET",
	.load = room_load, .unload = room_unload,
	.draw = room_draw, .tuned = NULL,
};
