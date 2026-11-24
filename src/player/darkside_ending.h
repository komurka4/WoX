/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Begin the Darkside ending with its opening face and dialogue. darkside_ending_play owns the
 * scene archives and passes the same ending clock and payload to darkside_ending_later.
 */
#ifndef XEEN_DARKSIDE_ENDING_H
#define XEEN_DARKSIDE_ENDING_H
#include "runtime.h"
#include "ending_payload.h"
/* Play the Darkside ending and score sequence from the opening face and dialogue onward.
 * Borrows the cinematic, intro archive and payload; opens and closes the needed secondary
 * archive and temporary assets. Returns the cinematic result: completed, failed or skipped.
 */
int darkside_ending_play(Cinematic *r, CC *intro, const EndingPayload *payload);
#endif
