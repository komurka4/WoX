/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Resolve the CUE track bounds used by animation selection. disc_read_cue and
 * disc_discover_cue read metadata through file_io; player supplies an explicit or automatic
 * track count without mounting a disc.
 */
#ifndef XEEN_DISC_H
#define XEEN_DISC_H
#include <stdint.h>

/* CUE track bounds include data tracks. The player uses only this metadata
 * to choose animation variants; it never reads tracks or mounts a disc.
 */
typedef struct {
    uint8_t first_track;
    uint8_t last_track;
    uint8_t track_count;
} DiscInfo;

/* Read track bounds from a borrowed CUE path into caller-owned info. Return one for valid
 * increasing track declarations; on I/O or format failure return zero without changing info.
 * No track or image data is opened.
 */
int disc_read_cue(const char *path, DiscInfo *info);
/* Resolve CUE metadata from an explicit path, environment or data-directory candidates. Borrow
 * all inputs. Return one and update info on success, zero when automatic discovery finds
 * nothing, or minus one for invalid explicit metadata; failure preserves info.
 */
int disc_discover_cue(const char *option, const char *data, DiscInfo *info);
#endif
