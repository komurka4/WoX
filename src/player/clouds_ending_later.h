/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the Clouds mirror reveal, cards, throne and room pan after the helmet dialogue.
 * clouds_ending_later uses runtime and returns the sampled pan increment needed by
 * clouds_ending_royal.
 */
#ifndef XEEN_CLOUDS_ENDING_LATER_H
#define XEEN_CLOUDS_ENDING_LATER_H
#include "runtime.h"
/* Play the later Clouds ending transition into the royal scenes. Borrows the cinematic and
 * archive, takes the final score, and writes the caller's panorama step. Frees temporary
 * assets; returns 1 on completion or 0 on a failed operation or input stop.
 */
int clouds_ending_later(Cinematic *r, CC *cc, uint32_t score, unsigned *pan_step);
#endif
