/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the Clouds narrator and animated dialogue with speech, captions and credits.
 * clouds_narration uses runtime, subtitles and text while preserving paragraph state across
 * chained voice clips.
 */
#ifndef XEEN_CLOUDS_NARRATION_H
#define XEEN_CLOUDS_NARRATION_H
#include "runtime.h"
/* Play Crodo's narration with portraits, speech and optional captions. Borrows the cinematic,
 * archive, art and font; owns and releases temporary voice and caption assets. Returns 1 on
 * completion or 0 on load, draw, text, present or input failure.
 */
int clouds_narration(Cinematic *r, CC *cc, const Blob *groupo, const Blob *group, const Blob *crodo,
                     const Blob *font);
#endif
