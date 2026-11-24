/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Resolve the CUE track bounds used by animation selection. disc_read_cue and
 * disc_discover_cue read metadata through file_io; player supplies an explicit or automatic
 * track count without mounting a disc.
 */
#include "audio/file_io.h"
#include "disc.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DISC_CUE_LINE_BYTES 1024
#define DISC_PATH_BYTES 4096
#define DISC_MAX_TRACK_NUMBER 99

/* Read track bounds from a borrowed CUE path into caller-owned info. Return one for valid
 * increasing track declarations; on I/O or format failure return zero without changing info.
 * No track or image data is opened.
 */
int disc_read_cue(const char *path, DiscInfo *info)
{
    FILE *file;
    char line[DISC_CUE_LINE_BYTES];
    DiscInfo result = {0, 0, 0};
    int ok = 1;
    if (!path || !info || !(file = xeen_fopen(path, "rb"))) {
        return 0;
    }
    while (fgets(line, sizeof(line), file)) {
        char *cursor = line;
        char *track_end;
        long track;
        const char token[] = "TRACK";
        unsigned token_index;
        if (!strchr(line, '\n') && !feof(file)) {
            int next = fgetc(file);
            if (next != EOF) {
                ok = 0;
                break;
            }
        }
        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        for (token_index = 0; token_index < sizeof(token) - 1 && cursor[token_index];
             token_index++) {
            if (toupper((unsigned char)cursor[token_index]) != token[token_index]) {
                break;
            }
        }
        if (token_index != sizeof(token) - 1 ||
            (cursor[token_index] && !isspace((unsigned char)cursor[token_index]))) {
            continue;
        }
        cursor += token_index;
        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (!isdigit((unsigned char)*cursor)) {
            ok = 0;
            break;
        }
        errno = 0;
        track = strtol(cursor, &track_end, 10);
        if (errno || track < 1 || track > DISC_MAX_TRACK_NUMBER ||
            !isspace((unsigned char)*track_end) ||
            (result.track_count && track <= result.last_track)) {
            ok = 0;
            break;
        }
        while (isspace((unsigned char)*track_end)) {
            track_end++;
        }
        if (!*track_end) {
            ok = 0;
            break;
        }
        if (!result.track_count) {
            result.first_track = (uint8_t)track;
        }
        result.last_track = (uint8_t)track;
        result.track_count++;
    }
    if (ferror(file)) {
        ok = 0;
    }
    if (fclose(file)) {
        ok = 0;
    }
    if (!ok || !result.track_count) {
        return 0;
    }
    *info = result;
    return 1;
}

/* Resolve CUE metadata from an explicit path, environment or data-directory candidates. Borrow
 * all inputs. Return one and update info on success, zero when automatic discovery finds
 * nothing, or minus one for invalid explicit metadata; failure preserves info.
 */
int disc_discover_cue(const char *option, const char *data, DiscInfo *info)
{
    static const char *const candidates[] = {"WOX1.cue", "../WOX1.cue", "CD/WOX1.cue",
                                             "../CD/WOX1.cue", "../emulator/CD/WOX1.cue"};
    char environment[DISC_PATH_BYTES];
    char path[DISC_PATH_BYTES];
    const char *value;
    size_t i;
    if (!info) {
        return -1;
    }
    if (option) {
        return disc_read_cue(option, info) ? 1 : -1;
    }
    if (!xeen_environment("XEEN_CUE", environment, sizeof(environment), &value)) {
        return -1;
    }
    if (value && *value) {
        return disc_read_cue(value, info) ? 1 : -1;
    }
    if (!data || !*data) {
        return 0;
    }
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        int n = snprintf(path, sizeof(path), "%s/%s", data, candidates[i]);
        if (n >= 0 && (size_t)n < sizeof(path) && disc_read_cue(path, info)) {
            return 1;
        }
    }
    return 0;
}
