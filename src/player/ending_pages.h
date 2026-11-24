/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Draw ending score and modal pages and service their fresh-key waits. The ending page entry
 * points share runtime, text, ending_payload and the World ending's borrowed animation assets.
 */
#ifndef XEEN_ENDING_PAGES_H
#define XEEN_ENDING_PAGES_H
#include "runtime.h"
#include "ending_payload.h"
/* Draw a two-pass ending caption into the caller's framebuffer using a borrowed font and text.
 * The world flag selects the caption colors. Returns 1 on success or 0 on null text,
 * formatting overflow or text failure; partial pixels can remain.
 */
int ending_caption_text(const Blob *font, const char *text, int world, uint8_t *pixels);
/* Draw a modal page and key prompt with initially left-aligned text. Borrows font and body and
 * writes caller pixels; returns 1 on success or 0 on formatting or text failure, with possible
 * partial output.
 */
int ending_modal_text(const Blob *font, const char *body, uint8_t *pixels);
/* Present borrowed message pages with initially left-aligned text and animated dragon art.
 * Borrows the cinematic, archive and diagnostic prefix; returns 1 on completion or 0 on a
 * failed operation or input stop. Temporary assets are released by the aligned implementation.
 */
int ending_modal_play(Cinematic *r, CC *intro, const char *const *pages, unsigned count,
                      const char *prefix);
/* Present borrowed message pages with the dragon and claw animation, preserving the supplied
 * initial alignment across pages. Borrows the cinematic, archive and diagnostic prefix;
 * releases temporary art and font. Returns 1 on completion or 0 on resource, text, present or
 * input failure.
 */
int ending_modal_play_aligned(Cinematic *r, CC *intro, const char *const *pages, unsigned count,
                              const char *prefix, int initially_centered);
/* Draw a modal page and key prompt in two passes, preserving alignment through the caller's
 * center state. Borrows font and body and writes caller pixels. Returns 1 on success or 0 on
 * invalid input, formatting overflow or text failure; partial output can remain.
 */
int ending_modal_text_state(const Blob *font, const char *body, uint8_t *pixels, int *center);
/* Format the final-score message from a borrowed payload into the caller's bounded buffer.
 * Returns 1 when the complete terminated message fits or 0 for invalid inputs, formatting
 * failure or truncation; a partial string can remain.
 */
int ending_score_text(const EndingPayload *payload, char *out, size_t capacity);
/* Return the immutable Castleview continuation message. The static string is borrowed for the
 * lifetime of the process and must not be freed.
 */
const char *ending_castleview_text(void);
/* Return the immutable unification ceremony message. The static string is borrowed for the
 * lifetime of the process and must not be freed.
 */
const char *ending_world_opening_text(void);
/* Return the immutable bonus-badge phrase, or null for no badge or an invalid selection. The
 * returned static string is borrowed and must not be freed.
 */
const char *ending_badge_message(unsigned badge);
/* Format the selected bonus-badge message into the caller's bounded buffer. Returns 1 when the
 * complete message fits or 0 for no badge, invalid input, formatting failure or truncation; a
 * partial string can remain.
 */
int ending_world_badge_text(unsigned badge, char *out, size_t capacity);
#endif
