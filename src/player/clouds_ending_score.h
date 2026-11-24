/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the Clouds score animation, input pages and final royal closing. The score and page
 * entry points use runtime, ending_payload, text and clouds_closing while retaining the
 * scrolling sky state.
 */
#ifndef XEEN_CLOUDS_ENDING_SCORE_H
#define XEEN_CLOUDS_ENDING_SCORE_H
#include "runtime.h"
#include "ending_payload.h"
/* Score display, text pages and closing animation for the Clouds ending. */
/* Animate rolling score digits over the moving sky and mirror. Borrows the cinematic and
 * archive and updates the caller's sky offset. Frees temporary art and returns 1 on completion
 * or 0 on failure or input stop.
 */
int clouds_ending_score_roll(Cinematic *r, CC *cc, unsigned *sky_offset);
/* Settle the rolling digits on the payload's final score. Borrows the cinematic, archive and
 * payload and updates the caller's sky offset. Frees temporary art; returns 1 on completion or
 * 0 on failure or input stop.
 */
int clouds_ending_score_fix(Cinematic *r, CC *cc, const EndingPayload *payload,
                            unsigned *sky_offset);
/* Draw the selected Clouds ending message into the caller's indexed framebuffer using a
 * borrowed font. Returns 1 on success or 0 for invalid inputs or page selection. No memory
 * ownership is transferred.
 */
int clouds_ending_page_text(const Blob *font, unsigned page, uint8_t *pixels);
/* Present the Clouds score and message pages over the moving ending background. Borrows the
 * cinematic, archive and payload and updates the caller's sky offset. Frees temporary assets;
 * returns 1 on completion or 0 on a failed operation or input stop.
 */
int clouds_ending_score_pages(Cinematic *r, CC *cc, const EndingPayload *payload,
                              unsigned *sky_offset);
/* Play the final royal farewell and close the Clouds ending. Borrows the cinematic and archive
 * and frees temporary assets; returns 1 on completion or 0 on failure or input stop.
 */
int clouds_ending_final_royal(Cinematic *r, CC *cc);
#endif
