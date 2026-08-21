/* SPDX-License-Identifier: 0BSD */
#ifndef CTR_FONT_H
#define CTR_FONT_H

#include <SDL.h>
#include <stdbool.h>

/* 5x7 bitmap text. Drawn from a one-row atlas built at startup, scaled by
 * whole numbers only -- a pixel font at a fractional scale is the one thing
 * that would look wrong on a screen otherwise full of exact 3x NES pixels. */

bool font_init(SDL_Renderer *r);
void font_quit(void);

/* Width in pixels of s at the given integer scale (1 px inter-glyph gap). */
int  font_text_w(const char *s, int scale);
#define FONT_LINE_H(scale) (7 * (scale))

void font_draw(SDL_Renderer *r, const char *s, int x, int y, int scale, SDL_Color c);
void font_draw_center(SDL_Renderer *r, const char *s, int cx, int y, int scale, SDL_Color c);

#endif
