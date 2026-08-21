/* SPDX-License-Identifier: 0BSD */
#ifndef CTR_BLIP_H
#define CTR_BLIP_H

#include <stdbool.h>

/* Menu audio, synthesised as square waves rather than played from samples:
 * no audio files to bundle, no licensing question, and it sounds like the
 * machine it is pretending to be. */

typedef enum {
	BLIP_TUNE = 0,   /* dial click, one channel over        */
	BLIP_START,      /* launching a version                 */
	BLIP_DENY,       /* pressing A on something not Contra  */
	BLIP_TOGGLE,     /* CRT on/off, view change             */
	BLIP_POWEROFF,   /* the send-off, played out in full    */
} blip_id;

/* Milliseconds of audio still to play, so a send-off can be waited out
 * instead of being cut off by the process exiting. */
unsigned blip_remaining_ms(void);

bool blip_init(void);
/* As blip_init, but also loads <res_dir>/audio/tune.wav for the dial. */
bool blip_init_res(const char *res_dir);
void blip_quit(void);
void blip_play(blip_id id);

#endif
