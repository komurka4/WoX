/* SPDX-License-Identifier: GPL-3.0-or-later
 */
/* Phase 308: cc_read for the reused XEEN.EXE modules (the audio engine's music drivers and VOC names, and the movie
 * player in src/player/), moved here from audio_engine.c so that it needs no SDL. */
#include "audio/audio_archive.h"
#include <stdlib.h>
#include <string.h>

/* cc_read for the player's modules over XEEN.DAT's archive reader (payload
 * XOR 35h, as the player's cc.c). */
int cc_read(CC *cc, const char *name, Blob *out)
{
    xeen_resource_info info;
    size_t got = 0;
    memset(out, 0, sizeof *out);
    if (!cc || !cc->archive || !name) {
        return 0;
    }
    if (xeen_archive_find_name(cc->archive, name, &info) != XR_OK) {
        return 0;
    }
    if (!(out->data = malloc(info.size ? info.size : 1u))) {
        return 0;
    }
    if (xeen_archive_extract(cc->archive, XR_BANK_ASSETS_XOR35, &info, out->data, info.size,
                             &got) != XR_OK) {
        free(out->data);
        out->data = NULL;
        return 0;
    }
    out->size = got;
    return 1;
}
