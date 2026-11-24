/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Begin the Darkside ending with its opening face and dialogue. darkside_ending_play owns the
 * scene archives and passes the same ending clock and payload to darkside_ending_later.
 */
#include "audio/file_io.h"
#include "darkside_ending.h"
#include "darkside_ending_later.h"
#include "subtitles.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

/* Discard ordinary keys during ending animation and stop only for global abort. Borrows and
 * updates the cinematic; returns 1 to continue or 0 after marking an abort failure. Headless
 * playback continues without polling.
 */
static int darkside_ending_poll_abort(Cinematic *cinematic)
{
    if (!cinematic->dump && platform_pending_key() == 2) {
        cinematic->aborted = 1;
        cinematic->result = XEEN_FAILED;
        return 0;
    }
    return 1;
}

/* Retrace and present the current Darkside ending frame under the borrowed name, then poll
 * global abort. Borrows and updates the cinematic; returns 1 to continue or 0 on present or
 * abort failure.
 */
static int darkside_ending_show(Cinematic *cinematic, const char *name)
{
    return cine_retrace(cinematic) && cine_present(cinematic, name, name) &&
           darkside_ending_poll_abort(cinematic);
}

/* Load the named full-screen raw image into cinematic pixels. Borrows archive and name;
 * temporary archive bytes are freed before return. Returns 1 on success or 0 on read or size
 * failure.
 */
static int darkside_ending_raw(Cinematic *cinematic, CC *archive, const char *name)
{
    Blob resource = {0};
    int ok = cine_load(archive, name, &resource) && resource.size == XEEN_PIXELS;
    if (ok) {
        memcpy(cinematic->pixels, resource.data, XEEN_PIXELS);
    }
    free(resource.data);
    return ok;
}

/* Load and decode the named speech asset from the borrowed archive into the supplied voice
 * slot. Temporary archive bytes are freed before return; decoded samples belong to the slot
 * and must be released with voice_free. Returns 1 on success or 0 on read or decode failure.
 */
static int darkside_ending_voice(CC *archive, const char *name, Voice *voice)
{
    Blob resource = {0};
    int ok = cine_load(archive, name, &resource) && player_voc_decode(&resource, voice);
    free(resource.data);
    return ok;
}

/* Play the Darkside ending and score sequence from the opening face and dialogue onward.
 * Borrows the cinematic, intro archive and payload; opens and closes the needed secondary
 * archive and temporary assets. Returns the cinematic result: completed, failed or skipped.
 */
int darkside_ending_play(Cinematic *cinematic, CC *intro, const EndingPayload *payload)
{
    CC dark = {0};
    Blob pal = {0};
    Blob face = {0};
    Blob box = {0};
    Blob font = {0};
    Blob pool = {0};
    Voice come = {0};
    Subtitles text = {0};
    int y;
    int ok = 0;
    unsigned serial = 0;
    char path[4096];
    char name[96];
    if (snprintf(path, sizeof(path), "%s/DARK.CC", cinematic->data) >= (int)sizeof(path) ||
        !cc_open(&dark, path)) {
        goto done;
    }
    if (!cine_load(intro, "endgame.pal", &pal) || pal.size != XEEN_PALETTE_BYTES ||
        !cine_load(intro, "box.vga", &box) || !cine_load(intro, "face.end", &face) ||
        !cine_load(intro, "fnt", &font) || !cine_load(intro, "special.bin", &pool) ||
        !darkside_ending_voice(&dark, "come2.voc", &come)) {
        goto done;
    }
    memcpy(cinematic->palette, pal.data, XEEN_PALETTE_BYTES);
    cine_music_control(cinematic, 0);
    cine_music_load(cinematic, &dark, "dngon3.m");
    if (!darkside_ending_raw(cinematic, intro, "scene1.raw") ||
        !darkside_ending_show(cinematic, "darkside-ending-scene1-black") ||
        !cine_fade(cinematic, 1, 4, "darkside-ending-scene1-in")) {
        goto done;
    }
    cine_reset(cinematic);
    if (!cine_wait_fire_ticks(cinematic, 30) ||
        !cine_fade(cinematic, 0, 4, "darkside-ending-scene1-out")) {
        goto done;
    }
    if (!darkside_ending_raw(cinematic, intro, "scene2-b.raw") ||
        !darkside_ending_show(cinematic, "darkside-ending-scene2-black")) {
        goto done;
    }
    memcpy(cinematic->background[0], cinematic->pixels, XEEN_PIXELS);
    cine_reset(cinematic);
    cine_restore(cinematic, 0);
    if (!sprite_draw_windowed(&face, 0, 29, 76, 171, cinematic->pixels) ||
        !darkside_ending_show(cinematic, "darkside-ending-face-y076") ||
        !cine_fade(cinematic, 1, 4, "darkside-ending-face-in") ||
        !cine_wait_fire_ticks(cinematic, 1)) {
        goto done;
    }

    if (!subtitles_select(&text, &pool, 21, 1, cinematic->now)) {
        goto done;
    }
    text.reset_tick = cine_tick(cinematic);

    if (cinematic->approximate && !cine_secondary_reset(cinematic)) {
        goto done;
    }
    for (y = 74; y > 20; y -= 2) {
        if (y == 60) {
            cine_music_control(cinematic, 207);
        }
        if (y == 22) {
            cine_music_control(cinematic, 0);
        }
        cine_reset(cinematic);
        cine_restore(cinematic, 0);
        if (!sprite_draw_windowed(&face, 0, 29, y, 171, cinematic->pixels)) {
            goto done;
        }
        snprintf(name, sizeof(name), "darkside-ending-face-y%03d", y);
        if (!darkside_ending_show(cinematic, name) || !cine_wait_fire_ticks(cinematic, 1)) {
            goto done;
        }
    }

    if (!cinematic->subtitles || cinematic->approximate) {
        cine_voice(cinematic, &come, "come2.voc");
    }
    cine_reset(cinematic);
    /* Continue until both the minimum hold and caption are complete.
     * Voice-busy state does not extend this particular animation loop.
     */
    while (cine_elapsed_before(cinematic, 27) || text.counter) {
        if (cinematic->subtitles) {
            if (cinematic->approximate) {
                uint64_t elapsed = cine_secondary_elapsed(cinematic);

                if ((int16_t)(uint16_t)elapsed > 1) {
                    unsigned start;
                    unsigned length;
                    if (!text.length) {
                        goto done;
                    }
                    text.counter = (text.counter + 1) % text.length;
                    start = text.counter > 40 ? text.counter - 40 : 0;
                    length = text.counter - start;
                    memcpy(text.window, text.text + start, length);
                    text.window[length] = 0;
                    if (!cine_secondary_reset(cinematic)) {
                        goto done;
                    }
                }
            } else {
                subtitles_update_tick(&text, cine_tick(cinematic));
            }
            if (!cine_draw_at(cinematic, &box, 0, 36, 189, 0) ||
                !text_draw_subtitle(&font, text.window, cinematic->pixels)) {
                goto done;
            }
        } else {
            text.counter = 0;
        }
        snprintf(name, sizeof(name), "darkside-ending-come-text-%04u", serial++);
        if (!darkside_ending_show(cinematic, name)) {
            goto done;
        }
    }
    cine_record(cinematic, "darkside-ending-source-1aa63");
    if (!darkside_ending_later(cinematic, intro, &dark, payload)) {
        goto done;
    }
    cinematic->result = XEEN_COMPLETED;
    ok = 1;
done:
    if (!ok) {
        cinematic->result = XEEN_FAILED;
    }
    free(pal.data);
    free(face.data);
    free(box.data);
    free(font.data);
    free(pool.data);
    voice_free(&come);
    cc_close(&dark);
    return cinematic->result;
}
