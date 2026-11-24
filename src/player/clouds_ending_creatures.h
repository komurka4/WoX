/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Compose and play the creature parade between royal dialogue and score display. The parade
 * entry points use runtime, the creature table and shared sky banks; legacy playback retains
 * its explicit diagnostic stop guard.
 */
#ifndef XEEN_CLOUDS_ENDING_CREATURES_H
#define XEEN_CLOUDS_ENDING_CREATURES_H
#include "runtime.h"
/* Play the full creature parade against the moving sky and mirror. Borrows the cinematic and
 * archive, retains the score argument for interface compatibility, and updates the caller's
 * sky offset. Frees temporary assets; returns 1 on completion or 0 on failure or input stop.
 */
int clouds_ending_creatures(Cinematic *r, CC *cc, uint32_t score, unsigned *sky_offset);
/* Play the creature parade using the legacy tuple restriction. Borrows the cinematic and
 * archive and updates the caller's sky offset; the score argument is retained for interface
 * compatibility. Releases temporary assets and returns 1 on completion or 0 on a failed
 * operation, input stop or disallowed tuple.
 */
int clouds_ending_creatures_legacy_prefix(Cinematic *r, CC *cc, uint32_t score,
                                          unsigned *sky_offset);
#endif
