/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Continue the Darkside introduction through ball dialogue, flight, bird, title and fire
 * scenes. darkside_later borrows earlier ball and caption resources and uses runtime until the
 * common cleanup boundary.
 */
#ifndef XEEN_DARKSIDE_LATER_H
#define XEEN_DARKSIDE_LATER_H
#include "runtime.h"
#include "subtitles.h"
/* Continue the Darkside introduction through the crystal-ball dialogue, flight and closing
 * sequence. Borrows the cinematic, archive, ball art, caption state, text pool, font and box;
 * takes the incoming ball position. Releases local assets and returns the cinematic result.
 */
int darkside_later(Cinematic *r, CC *cc, Blob ball[9], unsigned position, Subtitles *subtitle,
                   const Blob *text_pool, const Blob *font, const Blob *box);
/* Phase 308: the short intro's end (17F1B, 1A515): the configuration write and the fire segment. */
int darkside_later_fire_only(Cinematic *cinematic, CC *archive, Subtitles *subtitle,
                             const Blob *text_pool, const Blob *font, const Blob *box);
#endif
