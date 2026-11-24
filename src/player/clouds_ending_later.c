/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the Clouds mirror reveal, cards, throne and room pan after the helmet dialogue.
 * clouds_ending_later uses runtime and returns the sampled pan increment needed by
 * clouds_ending_royal.
 */
#include "clouds_ending_later.h"
#include "clouds_ending.h"
#include <stdlib.h>
#include <string.h>

/* Play the later Clouds ending transition into the royal scenes. Borrows the cinematic and
 * archive, takes the final score, and writes the caller's panorama step. Frees temporary
 * assets; returns 1 on completion or 0 on a failed operation or input stop.
 */
int clouds_ending_later(Cinematic *cinematic, CC *archive, uint32_t score, unsigned *pan_step)
{
    Blob pal = {0};
    Blob raw = {0};
    Blob king = {0};
    Blob people = {0};
    Blob crodo = {0};
    static const int left[13] = {0, -5, -10, -15, -20, -25, -30, -33, -27, -22, -17, -11, -6};
    static const int right[13] = {160, 145, 130, 115, 100, 85, 70, 57, 53, 48, 42, 39, 34};
    static const int y[13] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 4};
    int offset = 0;
    int remaining = 320;
    int man = 117;
    int other = 320;
    int step = 1;
    int first = 1;
    int index;
    int ok = 0;
    unsigned serial = 0;
    char label[96];
    (void)score;
    cine_stop_voice(cinematic);
    cine_music_load(cinematic, archive, "endgame.m");
    if (!cine_fade(cinematic, 0, 1, "clouds-ending-later-card-out")) {
        goto done;
    }
    if (!cine_load(archive, "endgame.pal", &pal) || pal.size != XEEN_PALETTE_BYTES ||
        !cine_load(archive, "later.raw", &raw) || raw.size != XEEN_PIXELS) {
        goto done;
    }
    memcpy(cinematic->palette, pal.data, XEEN_PALETTE_BYTES);
    memcpy(cinematic->pixels, raw.data, XEEN_PIXELS);
    if (!cine_retrace(cinematic) ||
        !cine_present(cinematic, "clouds-ending-later-card", "clouds-ending-later-card") ||
        !cine_fade(cinematic, 1, 4, "clouds-ending-later-card-in")) {
        goto done;
    }
    cine_reset(cinematic);
    if (!cine_load(archive, "king.end", &king) || !cine_load(archive, "people.end", &people) ||
        !cine_load(archive, "crodo.end", &crodo)) {
        goto done;
    }
    if (!cine_wait_fire_ticks(cinematic, 100) ||
        !cine_fade(cinematic, 0, 4, "clouds-ending-later-throne-out")) {
        goto done;
    }
    free(raw.data);
    raw.data = NULL;
    if (!cine_load(archive, "throne1.raw", &raw) || raw.size != XEEN_PIXELS) {
        goto done;
    }
    memcpy(cinematic->background[0], raw.data, XEEN_PIXELS);
    free(raw.data);
    raw.data = NULL;
    if (!cine_load(archive, "throne2.raw", &raw) || raw.size != XEEN_PIXELS) {
        goto done;
    }
    memcpy(cinematic->background[1], raw.data, XEEN_PIXELS);
    while (remaining > 0) {
        cine_reset(cinematic);
        cine_restore_horizontal(cinematic, (unsigned)offset);
        if (!cine_draw_effect(cinematic, &people, 0, man, 68, 0x800)) {
            goto done;
        }
        if (offset > 250) {
            if (!cine_draw_effect(cinematic, &crodo, 0, other, 68, 0x800)) {
                goto done;
            }
            other -= step * 2;
        }
        snprintf(label, sizeof(label), "clouds-ending-later-throne-pan-%03u", serial++);
        if (!cine_retrace(cinematic) || !cine_present(cinematic, label, label) ||
            !clouds_ending_poll(cinematic)) {
            goto done;
        }
        if (first) {
            step = (int)cine_elapsed(cinematic);
            if (!step) {
                step = 1;
            }
        }
        offset += step;
        remaining -= step;
        man -= step * 2;
        if (other < 181) {
            other = 181;
        }
        if (first) {
            if (!cine_fade(cinematic, 1, 4, "clouds-ending-later-throne-in")) {
                goto done;
            }
            first = 0;
        }
    }
    cine_restore_horizontal(cinematic, 320);
    if (!cine_draw_effect(cinematic, &crodo, 0, other, 68, 0x800) || !cine_retrace(cinematic) ||
        !cine_present(cinematic, "clouds-ending-later-throne-pan-final",
                      "clouds-ending-later-throne-pan-final")) {
        goto done;
    }
    cine_reset(cinematic);
    if (!cine_wait_fire_ticks(cinematic, 5)) {
        goto done;
    }

    memcpy(cinematic->background[0], cinematic->pixels, XEEN_PIXELS);
    for (index = 12; index >= 0; index--) {
        cine_reset(cinematic);
        cine_restore(cinematic, 0);
        if (!cine_draw_at(cinematic, &king, 0, left[index], y[index], (unsigned)index) ||
            !cine_draw_at(cinematic, &king, 1, right[index], y[index], (unsigned)index)) {
            goto done;
        }
        snprintf(label, sizeof(label), "clouds-ending-later-royal-reveal-%02d", index);
        if (!cine_retrace(cinematic) || !cine_present(cinematic, label, label) ||
            !clouds_ending_poll(cinematic) || !cine_wait_ticks(cinematic, 1, 0)) {
            goto done;
        }
    }
    if (pan_step) {
        *pan_step = (unsigned)step;
    }
    cine_record(cinematic, "clouds-ending-later-boundary-1653e");
    ok = 1;
done:
    free(pal.data);
    free(raw.data);
    free(king.data);
    free(people.data);
    free(crodo.data);
    return ok;
}
