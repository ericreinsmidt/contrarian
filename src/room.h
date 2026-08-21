/* SPDX-License-Identifier: 0BSD */
#ifndef CTR_ROOM_H
#define CTR_ROOM_H

/* One television -- a photographed RCA ColorTrak console -- filling the panel,
 * with the tube keyed out to a hole. Versions are channels: the d-pad retunes,
 * and what the tube shows is what that channel is carrying. A rejected ROM is
 * not "a card with static on it", it is a channel that does not come in, which
 * is exactly what a real set would show.
 *
 * The same hole is where the GAME goes: minarch draws the cabinet as an overlay
 * and Contrarian hands it CONTRARIAN_VIEWPORT, so you play on the television
 * rather than in front of it. */

/* Measured from res/room/geometry.json, which tools/genart.py writes when it
 * keys the cabinet photo. The cabinet fills the panel; the tube is the hole. */
#define SET_W   1024
#define SET_H   768
#define TUBE_X  191
#define TUBE_Y  93
#define TUBE_W  598
#define TUBE_H  448

/* The picture inside the tube.
 *
 * "fill" uses the whole 598x448 tube -- 1.87x the NES's 240 lines.
 * "2x" insets to 512x480, an exact 2x vertical so the scanline shader stays
 * pixel-exact; but 480 does not FIT a 448-tall tube, so with the whole
 * cabinet on screen this overflows and is clamped back to fill. Showing the
 * entire set and getting an integer scale are mutually exclusive here. */
#define GAME_2X_W 512
#define GAME_2X_H 480

#define STATIC_TILES 6

/* The hum bar is one texture moved continuously, not a set of fixed
 * positions cycled -- the latter reads as a slow jerk, however many you make. */
#define HUM_H        150
#define HUM_PERIOD_MS 5200u

/* One retune. Long enough to read as a mechanism, short enough that holding
 * the d-pad still sweeps the band. */
#define TUNE_MS      330

/* Full vertical passes the picture makes while the channel locks. Whole
 * numbers land the wrap on the settle instead of part-way through it. */
#define ROLLS        2.0f

#endif
