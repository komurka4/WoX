/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the Clouds score animation, input pages and final royal closing. The score and page
 * entry points use runtime, ending_payload, text and clouds_closing while retaining the
 * scrolling sky state.
 */
#include "telemetry.h"
#include "clouds_ending_score.h"
#include "clouds_closing.h"
#include "clouds_ending.h"
#include "subtitles.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

/* Advance the cinematic selector and return a score-art frame from twenty through twenty-nine.
 * Borrows and updates clock and selector state; clock failure returns twenty and remains
 * recorded in the cinematic. Approximate mode retains the selected value for the next choice.
 */
static unsigned clouds_ending_score_select_digit(Cinematic *cinematic)
{
    uint16_t time_word_high;
    uint16_t time_word_low;
    uint16_t accumulator = 0x2c00;
    unsigned selected;
    if (cinematic->approximate) {
        accumulator = (uint16_t)(0x2c00 | (cinematic->selector_accumulator & 255));
        if (!cine_dos_time(cinematic, accumulator, &time_word_high, &time_word_low)) {
            return 20;
        }
    } else {
        queen_clock(cinematic->clock_origin, cinematic->now - cinematic->clock_start,
                    &time_word_high, &time_word_low);
    }
    selected = queen_select_range(&cinematic->selector, accumulator, time_word_high, time_word_low,
                                  20, 29);
    /* Preserve the accumulator result between consecutive digit choices. */
    if (cinematic->approximate) {
        cinematic->selector_accumulator = (uint16_t)selected;
    }
    return selected;
}

/* Restore the scrolling sky, advance its caller-owned offset and draw the score backing and
 * mirror. Borrows art and cinematic state; returns 1 on success or 0 on draw failure, with
 * offset and pixels possibly already updated.
 */
static int clouds_ending_score_backing(Cinematic *cinematic, const Blob *back, const Blob *mirror,
                                       unsigned *offset)
{
    cine_restore_horizontal(cinematic, *offset);
    *offset = (*offset + 1) % 320;
    return cine_draw_at(cinematic, back, 0, 0, 0, 0) && cine_draw_at(cinematic, mirror, 0, 0, 0, 0);
}

/* Draw ten score symbols at the fixed spacing on the requested row. Borrows cinematic state,
 * art and digit indices; returns 1 on success or 0 on a failed draw, with partial pixels
 * possible.
 */
static int clouds_ending_score_digit_line(Cinematic *cinematic, const Blob *art,
                                          const unsigned digits[10], int y)
{
    unsigned index;
    for (index = 0; index < 10; index++) {
        if (!cine_draw_at(cinematic, art, digits[index], 64 + 19 * (int)index, y, 0)) {
            return 0;
        }
    }
    return 1;
}

/* Animate rolling score digits over the moving sky and mirror. Borrows the cinematic and
 * archive and updates the caller's sky offset. Frees temporary art and returns 1 on completion
 * or 0 on failure or input stop.
 */
int clouds_ending_score_roll(Cinematic *cinematic, CC *archive, unsigned *offset)
{
    Blob back = {0};
    Blob mirror = {0};
    Blob art = {0};
    unsigned frame;
    unsigned index;
    unsigned repeat;
    unsigned serial;
    unsigned digits[10];
    char label[128];
    int ok = 0;
    if (!offset || *offset >= 320) {
        return 0;
    }
    if (!cine_load(archive, "mirrback.end", &back) || !cine_load(archive, "mirror.end", &mirror) ||
        !cine_load(archive, "endtext.end", &art)) {
        goto done;
    }
    cine_stop_voice(cinematic);
    if (!clouds_hands_named(cinematic, archive, 1, 0, 0, "clouds-ending-score-cover")) {
        goto done;
    }
    /* Preparing the covered backdrop must not advance the sky offset. */
    cine_restore_horizontal(cinematic, *offset);
    if (!cine_draw_at(cinematic, &back, 0, 0, 0, 0) ||
        !cine_draw_at(cinematic, &mirror, 0, 0, 0, 0) ||
        !clouds_hands_named(cinematic, archive, 0, 0, 0, "clouds-ending-score-uncover")) {
        goto done;
    }
    for (frame = 0; frame < 20; frame++) {
        cine_reset(cinematic);
        serial = 0;
        do {
            if (!clouds_ending_score_backing(cinematic, &back, &mirror, offset) ||
                !cine_draw_at(cinematic, &art, frame, 0, 0, 0)) {
                goto done;
            }
            snprintf(label, sizeof(label), "clouds-ending-score-title-%02u-%02u", frame, serial++);
            if (!cine_retrace(cinematic) || !cine_present(cinematic, label, label) ||
                !clouds_ending_poll(cinematic)) {
                goto done;
            }
        } while (!cine_elapsed(cinematic));
    }
    /* The first batch inherits the title loop's final counter. Later batches
     * inherit the last elapsed result, and each inner pick retains its result.
     */
    if (cinematic->approximate) {
        cinematic->selector_accumulator = 19;
    }
    for (frame = 0; frame < 30; frame++) {
        for (index = 0; index < 10; index++) {
            digits[index] = clouds_ending_score_select_digit(cinematic);
        }
        cine_reset(cinematic);
        if (!clouds_ending_score_backing(cinematic, &back, &mirror, offset) ||
            !cine_draw_at(cinematic, &art, 19, 0, 0, 0)) {
            goto done;
        }
        /* The diagnostic catalogue composes each allowed digit over the same
         * backing. It describes separate cells, not simultaneous random choices.
         */
        if (cinematic->dump && getenv("XEEN_ENDING_SELECTOR_CATALOG")) {
            uint8_t *saved = malloc(XEEN_PIXELS);
            if (!saved) {
                goto done;
            }
            memcpy(saved, cinematic->pixels, XEEN_PIXELS);
            for (index = 0; index < 10; index++) {
                for (repeat = 20; repeat < 30; repeat++) {
                    memcpy(cinematic->pixels, saved, XEEN_PIXELS);
                    if (!cine_draw_at(cinematic, &art, repeat, 64 + 19 * (int)index, 73, 0)) {
                        free(saved);
                        goto done;
                    }
                    snprintf(label, sizeof(label), "selector-16fc1-%02u-%u-%u", frame, index,
                             repeat);
                    if (!cine_present(cinematic, label, NULL)) {
                        free(saved);
                        goto done;
                    }
                }
            }
            memcpy(cinematic->pixels, saved, XEEN_PIXELS);
            free(saved);
        }
        if (cinematic->approximate) {
            cine_coverage_state(cinematic, 0x16fc1, "score-digits", frame, *offset);
        }
        for (index = 0; index < 10; index++) {
            if (!cine_draw_at(cinematic, &art, digits[index], 64 + 19 * (int)index, 73, 0)) {
                goto done;
            }
        }
        snprintf(label, sizeof(label), "clouds-ending-score-roll-%02u", frame);
        if (!cine_retrace(cinematic) || !cine_present(cinematic, label, label) ||
            !clouds_ending_poll(cinematic) || !cine_wait_ticks(cinematic, 2, 0)) {
            goto done;
        }
    }
    cine_record(cinematic, "clouds-ending-score-boundary-17164");
    ok = 1;
done:
    free(back.data);
    free(mirror.data);
    free(art.data);
    return ok;
}

/* Settle the rolling digits on the payload's final score. Borrows the cinematic, archive and
 * payload and updates the caller's sky offset. Frees temporary art; returns 1 on completion or
 * 0 on failure or input stop.
 */
int clouds_ending_score_fix(Cinematic *cinematic, CC *archive, const EndingPayload *payload,
                            unsigned *offset)
{
    Blob back = {0};
    Blob mirror = {0};
    Blob art = {0};
    unsigned step;
    unsigned index;
    unsigned repeat;
    unsigned digits[10];
    char label[128];
    int ok = 0;
    if (!payload || !offset || *offset >= 320) {
        return 0;
    }
    if (!cine_load(archive, "mirrback.end", &back) || !cine_load(archive, "mirror.end", &mirror) ||
        !cine_load(archive, "endtext.end", &art)) {
        goto done;
    }
    /* Choose ten fresh digits before fixing a growing suffix from right to
     * left. Fallthrough deliberately fixes every digit in the last iteration.
     */
    for (step = 0; step < 10; step++) {
        for (index = 0; index < 10; index++) {
            digits[index] = clouds_ending_score_select_digit(cinematic);
        }
        for (index = 9 - step; index < 10; index++) {
            digits[index] = ending_payload_frame(payload, index);
        }
        cine_reset(cinematic);
        if (!clouds_ending_score_backing(cinematic, &back, &mirror, offset) ||
            !cine_draw_at(cinematic, &art, 19, 0, 0, 0)) {
            goto done;
        }
        if (cinematic->dump && getenv("XEEN_ENDING_SELECTOR_CATALOG")) {
            uint8_t *saved = malloc(XEEN_PIXELS);
            if (!saved) {
                goto done;
            }
            memcpy(saved, cinematic->pixels, XEEN_PIXELS);
            /* Keep fixed cells intact when composing diagnostic alternatives. */
            for (index = 0; index < 9 - step; index++) {
                for (repeat = 20; repeat < 30; repeat++) {
                    memcpy(cinematic->pixels, saved, XEEN_PIXELS);
                    if (!cine_draw_at(cinematic, &art, repeat, 64 + 19 * (int)index, 73, 0)) {
                        free(saved);
                        goto done;
                    }
                    snprintf(label, sizeof(label), "selector-1716f-%02u-%u-%u", step, index,
                             repeat);
                    if (!cine_present(cinematic, label, NULL)) {
                        free(saved);
                        goto done;
                    }
                }
            }
            memcpy(cinematic->pixels, saved, XEEN_PIXELS);
            free(saved);
        }
        if (cinematic->approximate) {
            cine_coverage_state(cinematic, 0x1716f, "score-fixation", step, *offset);
        }
        if (!clouds_ending_score_digit_line(cinematic, &art, digits, 73)) {
            goto done;
        }
        snprintf(label, sizeof(label), "clouds-ending-score-fixed-%02u", step);
        if (!cine_retrace(cinematic) || !cine_present(cinematic, label, label) ||
            !clouds_ending_poll(cinematic) || !cine_wait_ticks(cinematic, 2, 0)) {
            goto done;
        }
    }
    /* The rising title and fixed digits redraw without a tick wait or
     * another selector call, so this ascent runs at the machine's drawing speed.
     */
    for (step = 0; step < 38; step++) {
        if (!clouds_ending_score_backing(cinematic, &back, &mirror, offset) ||
            !cine_draw_at(cinematic, &art, 19, 0, 0, 0) ||
            !clouds_ending_score_digit_line(cinematic, &art, digits, 73 + (int)step)) {
            goto done;
        }
        snprintf(label, sizeof(label), "clouds-ending-score-ascent-%02u", step);
        if (!cine_retrace(cinematic) || !cine_present(cinematic, label, label)) {
            goto done;
        }
    }
    cine_record(cinematic, "clouds-ending-score-boundary-17523");
    ok = 1;
done:
    free(back.data);
    free(mirror.data);
    free(art.data);
    return ok;
}

/* The page's inner text bounds are inset by eight pixels. Glyphs can
 * extend beyond that rectangle; it controls layout rather than clipping.
 */
static void clouds_ending_score_page_glyph(const Blob *font, unsigned c, int x, int y,
                                           unsigned color, uint8_t *pixels)
{
    unsigned row;
    unsigned col;
    int down = c == 'g' || c == 'p' || c == 'q' || c == 'y';
    for (row = 0; row < 8; row++) {
        unsigned bits = font->data[c * 16 + row * 2] | (unsigned)font->data[c * 16 + row * 2 + 1]
                                                           << 8;
        for (col = 0; col < 8; col++) {
            int px = x + (int)col;
            int py = y + (int)row + down;
            if (((bits >> (col * 2)) & 3) && px >= 0 && px < 320 && py >= 0 && py < 200) {
                pixels[py * 320 + px] = (uint8_t)color;
            }
        }
    }
}

static int clouds_ending_score_page_width(const Blob *font, const char *cursor, size_t length)
{
    size_t index;
    int width = 0;
    for (index = 0; index < length; index++) {
        width += cursor[index] == ' ' ? 4 : font->data[4096 + (unsigned char)cursor[index]];
    }
    return width;
}

static void clouds_ending_score_page_line(const Blob *font, const char *cursor, size_t length,
                                          int x, int y, unsigned color, uint8_t *pixels)
{
    size_t index;
    for (index = 0; index < length; index++) {
        unsigned c = (unsigned char)cursor[index];
        if (c != ' ') {
            clouds_ending_score_page_glyph(font, c, x, y, color, pixels);
        }
        x += c == ' ' ? 4 : font->data[4096 + c];
    }
}

/* Draw the selected Clouds ending message into the caller's indexed framebuffer using a
 * borrowed font. Returns 1 on success or 0 for invalid inputs or page selection. No memory
 * ownership is transferred.
 */
int clouds_ending_page_text(const Blob *font, unsigned page, uint8_t *pixels)
{
    static const char *const body[2] = {"Please send this score to the Ancient's Headquarters "
                                        "where you'll be added to the Hall of Legends!",
                                        "Ancient's Headquarters\nNew World Computing, Inc.\nP.O. "
                                        "Box 4302\nHollywood, CA 90078-4302"};
    static const char prompt[] = "Press a Key";
    const char *cursor;
    int y = 68;
    if (!font || !font->data || font->size != 4352 || !pixels || page > 1) {
        return 0;
    }
    cursor = body[page];
    while (*cursor) {
        const char *end = cursor;
        const char *space = NULL;
        int width = 0;
        /* Fit strictly before the right edge, then wrap at the last blank
         * and consume the whole run of blanks at that boundary.
         */
        while (*end && *end != '\n') {
            unsigned c = (unsigned char)*end;
            int next = width + (c == ' ' ? 4 : font->data[4096 + c]);
            if (next >= 175) {
                break;
            }
            if (c == ' ') {
                space = end;
            }
            width = next;
            end++;
        }
        if (*end && *end != '\n') {
            if (!space || space == cursor) {
                return 0;
            }
            end = space;
            while (end > cursor && end[-1] == ' ') {
                end--;
            }
        }
        if (y + 9 > 152) {
            return 0;
        }
        clouds_ending_score_page_line(font, cursor, (size_t)(end - cursor), 71, y, 128, pixels);
        cursor = end;
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor == '\n') {
            cursor++;
        }
        if (*cursor) {
            y += 10;
        }
    }

    clouds_ending_score_page_line(
        font, prompt, sizeof(prompt) - 1,
        (319 - clouds_ending_score_page_width(font, prompt, sizeof(prompt) - 1)) / 2, 138, 189,
        pixels);
    return 1;
}

/* Present the Clouds score and message pages over the moving ending background. Borrows the
 * cinematic, archive and payload and updates the caller's sky offset. Frees temporary assets;
 * returns 1 on completion or 0 on a failed operation or input stop.
 */
int clouds_ending_score_pages(Cinematic *cinematic, CC *archive, const EndingPayload *payload,
                              unsigned *offset)
{
    Blob back = {0};
    Blob mirror = {0};
    Blob art = {0};
    Blob font = {0};
    unsigned page;
    unsigned index;
    unsigned serial;
    unsigned digits[10];
    int ok = 0;
    char label[128];
    if (!payload || !offset || *offset >= 320) {
        return 0;
    }
    if (!cine_load(archive, "mirrback.end", &back) || !cine_load(archive, "mirror.end", &mirror) ||
        !cine_load(archive, "endtext.end", &art) || !cine_load(archive, "fnt", &font)) {
        goto done;
    }
    for (index = 0; index < 10; index++) {
        digits[index] = ending_payload_frame(payload, index);
    }
    for (page = 0; page < 2; page++) {
        serial = 0;
        /* Discard previous input so each page requires a fresh key. */
        if (!cinematic->dump) {
            int key;
            while ((key = platform_pending_key()) != 0) {
                if (key == 2) {
                    cinematic->aborted = 1;
                    cinematic->result = XEEN_FAILED;
                    goto done;
                }
            }
        }
        for (;;) {
            int key;
            if (!clouds_ending_score_backing(cinematic, &back, &mirror, offset) ||
                !cine_draw_at(cinematic, &art, 19, 0, 0, 0) ||
                !clouds_ending_score_digit_line(cinematic, &art, digits, 110) ||
                !clouds_ending_page_text(&font, page, cinematic->pixels)) {
                goto done;
            }
            snprintf(label, sizeof(label), "clouds-ending-score-page-%u-%04u", page, serial++);
            if (!cine_retrace(cinematic) || !cine_present(cinematic, label, label)) {
                goto done;
            }
            if (!cinematic->dump && serial == 1 && xeen_trace_enabled()) {
                uint64_t start = platform_cycles();
                xeen_trace_modal("clouds-score", page, start);
                xeen_trace_main("diagnostics", platform_cycles(), start, 0, 0, "modal-marker");
            }
            if (cinematic->dump) {
                break; /* Headless tests advance only after recording one complete page state. */
            }
            key = platform_pending_key();
            if (key == 2) {
                cinematic->aborted = 1;
                cinematic->result = XEEN_FAILED;
                goto done;
            }
            if (key) {
                break;
            }
        }
    }
    if (!cine_fade(cinematic, 0, 4, "clouds-ending-score-pages-out")) {
        goto done;
    }
    cine_record(cinematic, "clouds-ending-score-boundary-17865");
    ok = 1;
done:
    free(back.data);
    free(mirror.data);
    free(art.data);
    free(font.data);
    return ok;
}

/* Compose the final royal portrait and optional subtitle window. Borrows art, font and caption
 * state and writes cinematic pixels; returns 1 on success or 0 on draw or text failure, with
 * possible partial output.
 */
static int clouds_ending_score_final_frame(Cinematic *cinematic, const Blob *king, const Blob *font,
                                           const Blob *box, const Subtitles *text,
                                           unsigned selected)
{
    if (!cine_draw_at(cinematic, king, 0, 0, 0, 0) ||
        !cine_draw_at(cinematic, king, 1, 160, 0, 0)) {
        return 0;
    }
    if (selected > 1 && !cine_draw_at(cinematic, king, selected, 0, 0, 0)) {
        return 0;
    }
    return !cinematic->subtitles || (cine_draw_at(cinematic, box, 0, 36, 189, 0) &&
                                     text_draw_subtitle(font, text->window, cinematic->pixels));
}

/* Play the final royal farewell and close the Clouds ending. Borrows the cinematic and archive
 * and frees temporary assets; returns 1 on completion or 0 on failure or input stop.
 */
int clouds_ending_final_royal(Cinematic *cinematic, CC *archive)
{
    Blob king = {0};
    Blob pool = {0};
    Blob font = {0};
    Blob box = {0};
    Blob voc = {0};
    Voice voice = {0};
    Subtitles text = {0};
    unsigned serial = 0;
    unsigned selected;
    unsigned frame;
    char label[128];
    int ok = 0;
    if (!cine_load(archive, "king.end", &king) || !cine_load(archive, "special.bin", &pool) ||
        !cine_load(archive, "fnt", &font) || !cine_load(archive, "box.vga", &box) ||
        !subtitles_select(&text, &pool, 10, 1, 0)) {
        goto done;
    }
    /* The caller supplies the current mirror palette and caption baseline.
     * Copy the final speech from the text pool independently of score digits.
     */
    if (!cine_draw_at(cinematic, &king, 0, 0, 0, 0) ||
        !cine_draw_at(cinematic, &king, 1, 160, 0, 0) || !cine_retrace(cinematic) ||
        !cine_present(cinematic, "clouds-ending-final-royal-black",
                      "clouds-ending-final-royal-black") ||
        !cine_fade(cinematic, 1, 4, "clouds-ending-final-royal-in")) {
        goto done;
    }

    if (!cine_load(archive, "king4.voc", &voc) || !player_voc_decode(&voc, &voice)) {
        goto done;
    }
    if (!cinematic->subtitles) {
        cine_voice(cinematic, &voice, "king4.voc");
    }
    do {
        uint16_t time_word_high;
        uint16_t time_word_low;
        cine_reset(cinematic);
        if (cinematic->approximate) {
            if (!cine_dos_time(cinematic, 0x2c1f, &time_word_high, &time_word_low)) {
                goto done;
            }
        } else {
            queen_clock(cinematic->clock_origin, cinematic->now - cinematic->clock_start,
                        &time_word_high, &time_word_low);
        }
        selected =
            queen_select_range(&cinematic->selector, cinematic->approximate ? 0x2c1f : 0x2c00,
                               time_word_high, time_word_low, 1, 6);
        if (cinematic->approximate) {
            cine_coverage_state(cinematic, 0x1792e, "final-royal-selector", selected, text.counter);
        }
        if (cinematic->subtitles) {
            if (!clouds_ending_ticker(cinematic, &text)) {
                goto done;
            }
        } else {
            text.counter = 0;
        }
        if (cinematic->dump && getenv("XEEN_ENDING_SELECTOR_CATALOG")) {
            for (frame = 1; frame <= 6; frame++) {
                if (!clouds_ending_score_final_frame(cinematic, &king, &font, &box, &text, frame)) {
                    goto done;
                }
                snprintf(label, sizeof(label), "selector-1792e-%04u-%u", serial, frame);
                if (!cine_present(cinematic, label, NULL)) {
                    goto done;
                }
            }
        }
        if (!clouds_ending_score_final_frame(cinematic, &king, &font, &box, &text, selected)) {
            goto done;
        }
        snprintf(label, sizeof(label), "clouds-ending-final-royal-dialogue-%04u", serial++);
        if (!cine_retrace(cinematic) || !cine_present(cinematic, label, label) ||
            !clouds_ending_poll(cinematic) || !cine_wait_ticks(cinematic, 3, 0)) {
            goto done;
        }
    } while (cine_sound_busy(cinematic) || text.counter);
    if (!cine_draw_at(cinematic, &king, 0, 0, 0, 0) ||
        !cine_draw_at(cinematic, &king, 1, 160, 0, 0) || !cine_retrace(cinematic) ||
        !cine_present(cinematic, "clouds-ending-final-royal-neutral",
                      "clouds-ending-final-royal-neutral")) {
        goto done;
    }
    cine_reset(cinematic);
    if (!cine_wait_ticks(cinematic, 1, 0)) {
        goto done;
    }

    if (!clouds_hands_named(cinematic, archive, 1, 0, 0, "clouds-ending-final")) {
        goto done;
    }
    cine_record(cinematic, "clouds-ending-final-royal-boundary-179fa");
    ok = 1;
done:
    free(king.data);
    free(pool.data);
    free(font.data);
    free(box.data);
    free(voc.data);
    voice_free(&voice);
    return ok;
}
