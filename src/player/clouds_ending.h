/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the Clouds ending from the castle fall through mirror, parade, score and final royal
 * dialogue. clouds_ending_play coordinates the later ending modules with one shared cinematic
 * and caller-owned score payload.
 */
#ifndef XEEN_CLOUDS_ENDING_H
#define XEEN_CLOUDS_ENDING_H
#include "runtime.h"
#include "ending_payload.h"
#include "subtitles.h"
#include <string.h>

/* Poll for global abort while discarding ordinary keys during ending animation. */
static inline int clouds_ending_poll(Cinematic *r)
{
    if (r->dump || platform_pending_key() != 2) {
        return 1;
    }
    r->aborted = 1;
    r->result = XEEN_FAILED;
    return 0;
}

/* Advance the caption at most once per call after a signed secondary-clock
 * comparison. Late calls deliberately do not catch up several characters.
 */
static inline int clouds_ending_ticker(Cinematic *r, Subtitles *text)
{
    if (!r->approximate) {
        subtitles_update(text, r->now);
        return 1;
    }
    if ((int16_t)(uint16_t)cine_secondary_elapsed(r) > 1) {
        unsigned start;
        unsigned n;
        if (!text->length) {
            return 0;
        }
        text->counter = (text->counter + 1) % text->length;
        start = text->counter > 40 ? text->counter - 40 : 0;
        n = text->counter - start;
        memcpy(text->window, text->text + start, n);
        text->window[n] = 0;
        if (text->counter == SUBTITLE_COUNTER_ALIAS_CHARACTER) {
            text->counter &= 0xff00u;
        }
        if (!cine_secondary_reset(r)) {
            return 0;
        }
    }
    return 1;
}

/* Play the Clouds ending from the falling castle through the royal farewell and score pages.
 * Borrows the cinematic, archive and ending payload; releases temporary assets before
 * returning the cinematic result: completed, failed or skipped.
 */
int clouds_ending_play(Cinematic *r, CC *archive, const EndingPayload *payload);
#endif
