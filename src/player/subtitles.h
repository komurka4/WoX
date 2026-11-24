/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Manage copied caption text and its scrolling character window. subtitles_select and the
 * update entry points keep ticker state separate from rendering; narration and ending scenes
 * provide the appropriate clock sample.
 */
#ifndef XEEN_SUBTITLES_H
#define XEEN_SUBTITLES_H
#include "xeen.h"
#define SUBTITLE_WINDOW_CHARACTERS 40
#define SUBTITLE_COUNTER_ALIAS_CHARACTER 243
#define SUBTITLE_TEXT_CAPACITY_BYTES 1000
#define SUBTITLE_WINDOW_CAPACITY_BYTES 41 /* Includes the terminator. */

typedef struct {
    char text[SUBTITLE_TEXT_CAPACITY_BYTES];
    char window[SUBTITLE_WINDOW_CAPACITY_BYTES];
    unsigned length;
    unsigned counter;
    uint64_t reset_tick;
} Subtitles;

/* Copy one NUL-separated text-pool entry into the borrowed ticker and set its starting counter
 * and PIT-time baseline. Return one when the counter lies inside the selected entry; return
 * zero for an invalid entry or out-of-range counter. No allocation is returned.
 */
int subtitles_select(Subtitles *s, const Blob *pool, unsigned entry, unsigned counter,
                     uint64_t now);
/* Advance the borrowed caption ticker using PIT-cycle time. Return one when its visible window
 * changes, zero when empty or not yet due. Missed character steps are not replayed.
 */
int subtitles_update(Subtitles *s, uint64_t now);
/* Advance the borrowed ticker once when more than one BIOS tick has elapsed, then rebuild its
 * trailing text window. Return one for an update, zero when empty or not due. The ticker owns
 * its fixed text buffers.
 */
int subtitles_update_tick(Subtitles *s, uint64_t tick);
#endif
