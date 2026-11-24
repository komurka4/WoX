/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Open or close the Clouds hand and marble frame around a saved composition. clouds_hands and
 * clouds_closing use runtime drawing and input; Darkside callers can bypass imagery while
 * retaining an optional fade.
 */
#ifndef XEEN_CLOUDS_CLOSING_H
#define XEEN_CLOUDS_CLOSING_H
#include "runtime.h"
/* Play the closing hands and their music cleanup using the requested edition behavior. Borrows
 * the cinematic and archive; returns 1 on completion or 0 on failure or input stop.
 */
int clouds_closing(Cinematic *r, CC *archive, int darkside_flag);
/* Animate the opening or closing hands using the standard diagnostic prefix. Borrows the
 * cinematic and archive; returns 1 on completion or 0 on a failed operation or input stop,
 * with the cinematic result retaining the cause.
 */
int clouds_hands(Cinematic *r, CC *archive, int close, int fade, int darkside_flag);
/* Animate the opening or closing hands with the selected fade and Darkside cleanup behavior.
 * Borrows the cinematic, archive and diagnostic prefix, loads and releases temporary art, and
 * returns 1 on completion or 0 on load, draw, present or input stop.
 */
int clouds_hands_named(Cinematic *r, CC *archive, int close, int fade, int darkside_flag,
                       const char *prefix);
#endif
