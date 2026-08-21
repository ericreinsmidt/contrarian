/* SPDX-License-Identifier: 0BSD */
#ifndef CTR_APP_H
#define CTR_APP_H

#include "db.h"
#include "platform.h"
#include "scan.h"
#include "verify.h"
#include <SDL.h>
#include <SDL_image.h>
#include <stdbool.h>

/* Shared launcher state. Views read it; only main.c mutates it. */
typedef struct app {
	SDL_Renderer *r;
	contra_db     db;
	contra_bases  bases;
	ctr_list      list;

	int           cursor;
	int           prev_cursor;    /* what we tuned away from */
	unsigned      cursor_ms;      /* when the cursor last changed */
	in_state      in;
	bool          running;

	char          root[SCAN_PATH];
	char          db_path[SCAN_PATH];
	char          faces_dir[SCAN_PATH];
	char          res_dir[SCAN_PATH];
	unsigned      t0;

	int           view;           /* index into the view registry */
	/* When set, the view shows this in the tube instead of the channel and
	 * drops its overlays. Used for the power-off send-off. */
	SDL_Texture  *farewell;
	bool          game_2x;        /* inset the picture to an exact 2x 640x480 */
	void         *vdata;          /* active view's private state */
} app;

/* Convenience wrappers used by every view. */
/* sub is a folder under res/, e.g. "room" or "tv". */
SDL_Surface *app_load_surf(app *a, const char *sub, const char *name);
SDL_Texture *app_load_tex(app *a, const char *sub, const char *name);
/* A captured card face, by payload hash. NULL when it has not developed yet. */
SDL_Texture *app_load_face(app *a, const char *sha1);

/* The item under the cursor, or NULL when the card holds nothing. */
const ctr_item *app_current(const app *a);

/* The refusal mark: a ring with a bar through it, drawn over a rejected ROM.
 * Both views use it, so the geometry lives in one place. */
void app_draw_no_symbol(SDL_Renderer *r, int cx, int cy, int rad, int thick,
                        SDL_Color c);

/* Shared palette. */
extern const SDL_Color C_TITLE, C_META, C_BAD, C_DIM;

#endif
