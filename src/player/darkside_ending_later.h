/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the remaining Darkside ending, including transport, Corak's arrival, battle, collapse
 * and score page. darkside_ending_later borrows the caller's archives and ending clock and
 * uses ending_pages for the final input screen.
 */
#ifndef XEEN_DARKSIDE_ENDING_LATER_H
#define XEEN_DARKSIDE_ENDING_LATER_H
#include "runtime.h"
#include "ending_payload.h"
/* Select an inclusive index from zero through upper using mutable cinematic selector and time
 * state. The busy-call flag preserves the alternate accumulator path. Returns zero on clock
 * failure as well as a legitimate zero selection; failure is recorded in the cinematic. The
 * reason this caller uses that alternate accumulator is unknown.
 */
unsigned darkside_ending_select(Cinematic *r, unsigned upper, int yes_busy_call);
/* Play the dialogue, battle, collapse and score portion of the Darkside ending. Borrows the
 * cinematic, both archives and payload; owns and releases local scene assets. Returns 1 on
 * completion or 0 on load, draw, present, clock or input failure.
 */
int darkside_ending_later(Cinematic *r, CC *intro, CC *dark, const EndingPayload *payload);
#endif
