/* SPDX-License-Identifier: 0BSD */
#ifndef CTR_VIEW_H
#define CTR_VIEW_H

#include "app.h"

/* A view is a complete way of presenting the same one game's versions.
 *
 * One ships: "room", a single stationary television whose tube is the whole
 * interface. Each version is a channel and the d-pad retunes, so a rejected
 * ROM is simply a channel that does not come in. The interface exists so a
 * second reading of the same idea can be dropped in beside it. */
typedef struct {
	const char *name;        /* as written in contrarian.cfg */
	const char *label;       /* shown when switching views */
	bool (*load)(app *a);    /* acquire textures; false on failure */
	void (*unload)(app *a);  /* release them (called before every game launch) */
	void (*draw)(app *a);    /* one frame, background included */
	/* Optional: told when the cursor moves, so a view can start a transition. */
	void (*tuned)(app *a, int delta);
} ctr_view;

extern const ctr_view VIEW_ROOM;

/* Registry. view_find returns an index, or -1. */
const ctr_view *view_get(int i);
int             view_count(void);
int             view_find(const char *name);

#endif
