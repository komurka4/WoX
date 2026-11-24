/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the World ending ceremony, joining of Xeen and final score and badge pages.
 * world_ending_play shares runtime and ending_pages; its headless test entry follows the same
 * choreography.
 */
#ifndef XEEN_WORLD_ENDING_H
#define XEEN_WORLD_ENDING_H
#include "runtime.h"
#include "ending_payload.h"
/* Play the World unification ending, final score and optional badge page. Borrows the
 * cinematic, intro archive and payload, takes the badge selection, and releases local assets
 * and the secondary archive. Returns the cinematic result: completed or failed.
 */
int world_ending_play(Cinematic *r, CC *intro, const EndingPayload *payload, unsigned badge);
/* Play the World ending through the headless-only compatibility entry point. Borrows the
 * cinematic, archive and payload and takes the badge selection. Returns failure for live
 * playback; otherwise returns the ending result after releasing local resources.
 */
int world_ending_play_headless(Cinematic *r, CC *intro, const EndingPayload *payload,
                               unsigned badge);
#endif
