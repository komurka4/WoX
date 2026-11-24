/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the Clouds royal dialogue, room zoom and mirror transition. clouds_ending_royal
 * inherits the caller's caption baseline and pan increment, then passes sky state to the
 * creature parade.
 */
#ifndef XEEN_CLOUDS_ENDING_ROYAL_H
#define XEEN_CLOUDS_ENDING_ROYAL_H
#include "runtime.h"
/* Play the royal dialogue and sky transition before the creature parade. Borrows the cinematic
 * and archive, takes the score and incoming panorama step, and writes the outgoing sky offset.
 * Frees temporary assets; returns 1 on completion or 0 on failure or input stop.
 */
int clouds_ending_royal(Cinematic *r, CC *cc, uint32_t score, unsigned pan_step,
                        unsigned *sky_offset);
#endif
