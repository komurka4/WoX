/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the Clouds royal dialogue, room zoom and mirror transition. clouds_ending_royal
 * inherits the caller's caption baseline and pan increment, then passes sky state to the
 * creature parade.
 */
#include "clouds_ending_royal.h"
#include "clouds_ending.h"
#include "subtitles.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

/* Load and decode the named speech asset from the borrowed archive into the supplied voice
 * slot. Temporary archive bytes are freed before return; decoded samples belong to the slot
 * and must be released with voice_free. Returns 1 on success or 0 on read or decode failure.
 */
static int clouds_ending_royal_load_voice(CC *archive, const char *name, Voice *voice)
{
    Blob resource = {0};
    int ok = 0;
    voice_free(voice);
    if (cine_load(archive, name, &resource)) {
        ok = player_voc_decode(&resource, voice);
    }
    free(resource.data);
    return ok;
}

/* Retrace and present the current royal composition under the borrowed label, then poll global
 * abort. Borrows and updates the cinematic; returns 1 to continue or 0 on present or abort
 * failure.
 */
static int clouds_ending_royal_show(Cinematic *cinematic, const char *label)
{
    return cine_retrace(cinematic) && cine_present(cinematic, label, label) &&
           clouds_ending_poll(cinematic);
}

/* Compose the selected royal portrait and optional caption over the saved background. Borrows
 * all art, font and text and writes cinematic pixels. Returns 1 on success or 0 on draw or
 * text failure; partial pixels can remain.
 */
static int clouds_ending_royal_royal_frame(Cinematic *cinematic, const Blob *king, const Blob *font,
                                           const Blob *box, Subtitles *text, unsigned selected)
{
    if (!cine_draw_at(cinematic, king, 0, 0, 0, 0) ||
        !cine_draw_at(cinematic, king, 1, 160, 0, 0)) {
        return 0;
    }
    if (selected > 1 && !cine_draw_at(cinematic, king, selected, 0, 0, 0)) {
        return 0;
    }
    if (cinematic->subtitles && (!cine_draw_at(cinematic, box, 0, 36, 189, 0) ||
                                 !text_draw_subtitle(font, text->window, cinematic->pixels))) {
        return 0;
    }
    return 1;
}

/* Diagnostic alternatives use complete compositions with allowed mouth
 * choices. The actual selected image is restored before playback continues.
 */
static int clouds_ending_royal_royal_show(Cinematic *cinematic, const Blob *king, const Blob *font,
                                          const Blob *box, Subtitles *text, unsigned serial)
{
    uint16_t time_word_high;
    uint16_t time_word_low;
    unsigned selected;
    unsigned frame;
    char label[96];
    if (cinematic->approximate) {
        if (!cine_dos_time(cinematic, 0x2c1f, &time_word_high, &time_word_low)) {
            return 0;
        }
    } else {
        queen_clock(cinematic->clock_origin, cinematic->now - cinematic->clock_start,
                    &time_word_high, &time_word_low);
    }
    selected = queen_select_range(&cinematic->selector, cinematic->approximate ? 0x2c1f : 0x2c00,
                                  time_word_high, time_word_low, 1, 6);
    if (cinematic->approximate) {
        cine_coverage_state(cinematic, 0x165d2, "royal-selector", selected, text->counter);
    }
    if (cinematic->subtitles) {
        if (!clouds_ending_ticker(cinematic, text)) {
            return 0;
        }
    } else {
        text->counter = 0;
    }
    if (cinematic->dump && getenv("XEEN_ENDING_SELECTOR_CATALOG")) {
        for (frame = 1; frame <= 6; frame++) {
            if (!clouds_ending_royal_royal_frame(cinematic, king, font, box, text, frame)) {
                return 0;
            }
            snprintf(label, sizeof(label), "selector-165d2-%04u-%u", serial, frame);
            if (!cine_present(cinematic, label, NULL)) {
                return 0;
            }
        }
    }
    if (!clouds_ending_royal_royal_frame(cinematic, king, font, box, text, selected)) {
        return 0;
    }
    snprintf(label, sizeof(label), "clouds-ending-royal-dialogue-%04u", serial);
    return clouds_ending_royal_show(cinematic, label);
}

/* The royal scene's art, caption and voice, loaded and owned for the whole scene: royal_close frees them (NULL ones
 * included). THRONE is the zoom's saved room composition. */
typedef struct {
    Blob king;
    Blob room;
    Blob mirror;
    Blob sky;
    Blob cord;
    Blob back;
    Blob pool;
    Blob font;
    Blob box;
    Blob pal;
    Blob raw;
    Voice voice;
    Subtitles text;
    uint8_t *throne;
} RoyalScene;

/* The scene's resources in their load order (king.end .. box.vga, then caption 10 of special.bin): 1, or 0 on an
 * allocation, read or caption failure. The caption keeps the earlier baseline: its first update is already due after
 * the long card and throne sequence. */
static int royal_open(CC *archive, RoyalScene *s)
{
    s->throne = malloc(XEEN_PIXELS);
    if (!s->throne || !cine_load(archive, "king.end", &s->king) || !cine_load(archive, "room.end", &s->room) ||
        !cine_load(archive, "mirror.end", &s->mirror) || !cine_load(archive, "bigsky.end", &s->sky) ||
        !cine_load(archive, "kingcord.end", &s->cord) || !cine_load(archive, "mirrback.end", &s->back) ||
        !cine_load(archive, "special.bin", &s->pool) || !cine_load(archive, "fnt", &s->font) ||
        !cine_load(archive, "box.vga", &s->box) || !subtitles_select(&s->text, &s->pool, 10, 1, 0)) {
        return 0;
    }
    s->text.reset_tick = 0;
    return 1;
}

/* Frees what royal_open and the later stages loaded. */
static void royal_close(RoyalScene *s)
{
    free(s->throne);
    free(s->king.data);
    free(s->room.data);
    free(s->mirror.data);
    free(s->sky.data);
    free(s->cord.data);
    free(s->back.data);
    free(s->pool.data);
    free(s->font.data);
    free(s->box.data);
    free(s->pal.data);
    free(s->raw.data);
    voice_free(&s->voice);
}

/* The king's three speeches (king1..king3.voc) with the voice, or the one caption: each line's portrait frames every
 * three ticks while the voice or the caption runs, then the neutral portrait and one tick. Captions play only the
 * first line (the caption holds the whole dialogue). 1, or 0 on a failure or an input stop. */
static int royal_dialogue(Cinematic *cinematic, CC *archive, RoyalScene *s)
{
    static const char *voices[3] = {"king1.voc", "king2.voc", "king3.voc"};
    unsigned serial = 0;
    unsigned scene;
    char label[96];
    for (scene = 0; scene < 3; scene++) {
        if (!cinematic->subtitles) {
            if (!clouds_ending_royal_load_voice(archive, voices[scene], &s->voice)) {
                return 0;
            }
            cine_voice(cinematic, &s->voice, voices[scene]);
        }
        do {
            cine_reset(cinematic);
            if (!clouds_ending_royal_royal_show(cinematic, &s->king, &s->font, &s->box, &s->text, serial++) ||
                !cine_wait_ticks(cinematic, 3, 0)) {
                return 0;
            }
        } while (cine_sound_busy(cinematic) || s->text.counter);
        if (!cine_draw_at(cinematic, &s->king, 0, 0, 0, 0) || !cine_draw_at(cinematic, &s->king, 1, 160, 0, 0)) {
            return 0;
        }
        snprintf(label, sizeof(label), "clouds-ending-royal-neutral-%u", scene);
        if (!clouds_ending_royal_show(cinematic, label)) {
            return 0;
        }
        cine_reset(cinematic);
        if (!cine_wait_ticks(cinematic, 1, 0)) {
            return 0;
        }
        if (cinematic->subtitles) {
            break;
        }
    }
    return 1;
}

/* The fade to the mirror room (mirror.pal, miror-s.raw as both backgrounds and the page), the room drawn over it and
 * faded in, then panned left by STEP per frame to offset 83. 1, or 0 on a failure or an input stop. */
static int royal_room(Cinematic *cinematic, CC *archive, RoyalScene *s, int step)
{
    int index;
    char label[96];
    if (!cine_fade(cinematic, 0, 4, "clouds-ending-royal-mirror-out") ||
        !cine_load(archive, "mirror.pal", &s->pal) || s->pal.size != XEEN_PALETTE_BYTES ||
        !cine_load(archive, "miror-s.raw", &s->raw) || s->raw.size != XEEN_PIXELS) {
        return 0;
    }
    memcpy(cinematic->palette, s->pal.data, XEEN_PALETTE_BYTES);
    memcpy(cinematic->background[0], s->raw.data, XEEN_PIXELS);
    memcpy(cinematic->background[1], s->raw.data, XEEN_PIXELS);
    memcpy(cinematic->pixels, s->raw.data, XEEN_PIXELS);
    if (!cine_draw_at(cinematic, &s->room, 0, 0, 0, 0) || !cine_draw_at(cinematic, &s->room, 1, 160, 0, 0) ||
        !clouds_ending_royal_show(cinematic, "clouds-ending-royal-room-black") ||
        !cine_fade(cinematic, 1, 4, "clouds-ending-royal-room-in")) {
        return 0;
    }
    for (index = 0; index < 83; index += step) {
        cine_reset(cinematic);
        cine_restore_horizontal(cinematic, (unsigned)index);
        if (!cine_draw_at(cinematic, &s->room, 0, 0, 0, 0) || !cine_draw_at(cinematic, &s->room, 1, 160, 0, 0)) {
            return 0;
        }
        snprintf(label, sizeof(label), "clouds-ending-royal-room-pan-%03d", index);
        if (!clouds_ending_royal_show(cinematic, label) || !cine_wait_ticks(cinematic, 1, 0)) {
            return 0;
        }
    }
    return 1;
}

/* The mirror's zoom over the panned room (saved in THRONE): sky, frame and glass at scales 8..0, then the whole sky
 * as both backgrounds. 1, or 0 on a failure or an input stop. */
static int royal_mirror_zoom(Cinematic *cinematic, RoyalScene *s)
{
    static const int x[9] = {0, -5, -10, -15, -24, -30, -39, -50, -59};
    static const int y[9] = {0, 12, 25, 37, 46, 52, 59, 64, 68};
    int index;
    char label[96];
    memcpy(s->throne, cinematic->pixels, XEEN_PIXELS);
    for (index = 8; index >= 0; index--) {
        memcpy(cinematic->pixels, s->throne, XEEN_PIXELS);
        if (!cine_draw_at(cinematic, &s->sky, 0, x[index], y[index], (unsigned)index) ||
            !cine_draw_at(cinematic, &s->back, 0, x[index], y[index], (unsigned)index) ||
            !cine_draw_at(cinematic, &s->mirror, 0, x[index], y[index], (unsigned)index)) {
            return 0;
        }
        snprintf(label, sizeof(label), "clouds-ending-royal-mirror-zoom-%02d", index);
        if (!clouds_ending_royal_show(cinematic, label)) {
            return 0;
        }
    }
    if (!cine_draw_at(cinematic, &s->sky, 0, 0, 0, 0)) {
        return 0;
    }
    memcpy(cinematic->background[0], cinematic->pixels, XEEN_PIXELS);
    memcpy(cinematic->background[1], cinematic->pixels, XEEN_PIXELS);
    return 1;
}

/* One frame of the cord's travel: the sky scrolled to *SKY_OFFSET (then advanced one column, modulo 320) under the
 * mirror, the two cord halves at LEFT and RIGHT, height VERTICAL, presented as LABEL. 1, or 0 on a failure or an
 * input stop. */
static int royal_cord_frame(Cinematic *cinematic, RoyalScene *s, unsigned *sky_offset, int left, int right,
                            int vertical, const char *label)
{
    cine_restore_horizontal(cinematic, *sky_offset);
    if (!cine_draw_at(cinematic, &s->back, 0, 0, 0, 0) || !cine_draw_at(cinematic, &s->mirror, 0, 0, 0, 0)) {
        return 0;
    }
    *sky_offset = (*sky_offset + 1) % 320;
    return cine_draw_effect(cinematic, &s->cord, 0, left, vertical, 0x800) &&
           cine_draw_effect(cinematic, &s->cord, 1, right, vertical, 0x800) && clouds_ending_royal_show(cinematic, label);
}

/* The king's cords travel in from the corners by STEP per frame over 115, then the final frame at (0, 85) and
 * (220, 85). *SKY_OFFSET counts the sky's columns from 0. 1, or 0 on a failure or an input stop. */
static int royal_cord(Cinematic *cinematic, RoyalScene *s, int step, unsigned *sky_offset)
{
    int left = -115;
    int right = 335;
    int vertical = 200;
    int travel;
    char label[96];
    for (travel = 0; travel < 115; travel += step) {
        snprintf(label, sizeof(label), "clouds-ending-royal-cord-%03d", travel);
        if (!royal_cord_frame(cinematic, s, sky_offset, left, right, vertical, label)) {
            return 0;
        }
        left += step;
        right -= step;
        vertical -= step;
    }
    return royal_cord_frame(cinematic, s, sky_offset, 0, 220, 85, "clouds-ending-royal-cord-final");
}

/* Play the royal dialogue and sky transition before the creature parade. Borrows the cinematic
 * and archive, takes the score and incoming panorama step, and writes the outgoing sky offset.
 * Frees temporary assets; returns 1 on completion or 0 on failure or input stop.
 * Stages: royal_open, royal_dialogue, royal_room (the pan by PAN_STEP), royal_mirror_zoom, royal_cord (the zoom
 * inherits the earlier card and throne pan increment, scaled by four; it is not resampled from the current clock).
 */
int clouds_ending_royal(Cinematic *cinematic, CC *archive, uint32_t score, unsigned pan_step,
                        unsigned *sky_offset)
{
    RoyalScene s = {0};
    unsigned current_sky_offset = 0;
    int step = (int)pan_step;
    int ok;
    (void)score;
    if (!step) {
        return 0;
    }
    ok = royal_open(archive, &s) && royal_dialogue(cinematic, archive, &s) && royal_room(cinematic, archive, &s, step) &&
         royal_mirror_zoom(cinematic, &s) && royal_cord(cinematic, &s, step * 4, &current_sky_offset);
    if (ok) {
        if (sky_offset) {
            *sky_offset = current_sky_offset;
        }
        cine_record(cinematic, "clouds-ending-royal-boundary-169c1");
    }
    royal_close(&s);
    return ok;
}
