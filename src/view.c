/* SPDX-License-Identifier: 0BSD */
#include "view.h"
#include <string.h>
#include <strings.h>

/* Index 0 is the default when contrarian.cfg names nothing. */
static const ctr_view *VIEWS[] = {
	&VIEW_ROOM,
};

int view_count(void) { return (int)(sizeof VIEWS / sizeof *VIEWS); }

const ctr_view *view_get(int i)
{
	if (i < 0 || i >= view_count()) i = 0;
	return VIEWS[i];
}

int view_find(const char *name)
{
	int i;
	if (!name || !*name) return -1;
	for (i = 0; i < view_count(); i++)
		if (!strcasecmp(VIEWS[i]->name, name)) return i;
	return -1;
}
