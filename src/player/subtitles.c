/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Manage copied caption text and its scrolling character window. subtitles_select and the
 * update entry points keep ticker state separate from rendering; narration and ending scenes
 * provide the appropriate clock sample.
 */
#include "subtitles.h"
#include <string.h>

/* Copy one NUL-separated text-pool entry into the borrowed ticker and set its starting counter
 * and PIT-time baseline. Return one when the counter lies inside the selected entry; return
 * zero for an invalid entry or out-of-range counter. No allocation is returned.
 */
int subtitles_select(Subtitles *subtitles, const Blob *pool, unsigned entry, unsigned counter,
                     uint64_t now)
{
    size_t entry_start = 0;
    size_t entry_end;
    if (!pool || !pool->data) {
        return 0;
    }
    while (entry--) {
        while (entry_start < pool->size && pool->data[entry_start]) {
            entry_start++;
        }
        if (entry_start == pool->size) {
            return 0;
        }
        entry_start++;
    }
    entry_end = entry_start;
    while (entry_end < pool->size && pool->data[entry_end]) {
        entry_end++;
    }
    if (entry_end == pool->size || entry_end == entry_start ||
        entry_end - entry_start >= sizeof(subtitles->text)) {
        return 0;
    }
    memcpy(subtitles->text, pool->data + entry_start, entry_end - entry_start);
    subtitles->text[entry_end - entry_start] = 0;
    subtitles->length = (unsigned)(entry_end - entry_start);
    subtitles->counter = counter;
    subtitles->reset_tick = tick_at(now);
    return counter < subtitles->length;
}

/* Advance the borrowed caption ticker using PIT-cycle time. Return one when its visible window
 * changes, zero when empty or not yet due. Missed character steps are not replayed.
 */
int subtitles_update(Subtitles *subtitles, uint64_t now)
{
    return subtitles_update_tick(subtitles, tick_at(now));
}

/* Advance the borrowed ticker once when more than one BIOS tick has elapsed, then rebuild its
 * trailing text window. Return one for an update, zero when empty or not due. The ticker owns
 * its fixed text buffers.
 */
int subtitles_update_tick(Subtitles *subtitles, uint64_t tick)
{
    unsigned window_start;
    unsigned window_length;
    if (!subtitles->length) {
        return 0;
    }
    if (tick - subtitles->reset_tick <= 1) {
        return 0;
    }
    subtitles->counter = (subtitles->counter + 1) % subtitles->length;
    window_start = subtitles->counter > SUBTITLE_WINDOW_CHARACTERS
                       ? subtitles->counter - SUBTITLE_WINDOW_CHARACTERS
                       : 0;
    window_length = subtitles->counter - window_start;
    memcpy(subtitles->window, subtitles->text + window_start, window_length);
    subtitles->window[window_length] = 0;
    subtitles->reset_tick = tick;
    return 1;
}
