/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Draw ending score and modal pages and service their fresh-key waits. The ending page entry
 * points share runtime, text, ending_payload and the World ending's borrowed animation assets.
 */
#include "telemetry.h"
#include "ending_pages.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

/* Draw a two-pass ending caption into the caller's framebuffer using a borrowed font and text.
 * The world flag selects the caption colors. Returns 1 on success or 0 on null text,
 * formatting overflow or text failure; partial pixels can remain.
 */
int ending_caption_text(const Blob *font, const char *text, int world, uint8_t *pixels)
{
    char formatted[2048];
    int length;
    int center = 1;
    if (!text) {
        return 0;
    }
    /* Reset text position before applying color and alignment. World pages
     * can inherit centered alignment, even for an empty measured span before
     * a control character.
     */
    length = snprintf(formatted, sizeof(formatted), "\013000\011000\014%02u\003c%s", world ? 38 : 5,
                      text);
    if (length < 0 || (size_t)length >= sizeof(formatted) ||
        !text_draw_ending_context_state(font, formatted, 10, 165, 312, 210, 10, 165, 25, pixels,
                                        &center)) {
        return 0;
    }
    length = snprintf(formatted, sizeof(formatted), "\013000\011000\014%02u\003c%s",
                      world ? 39 : 11, text);
    return length >= 0 && (size_t)length < sizeof(formatted) &&
           text_draw_ending_context_state(font, formatted, 9, 164, 311, 208, 9, 164, 25, pixels,
                                          &center);
}

/* Draw a modal page and key prompt in two passes, preserving alignment through the caller's
 * center state. Borrows font and body and writes caller pixels. Returns 1 on success or 0 on
 * invalid input, formatting overflow or text failure; partial output can remain.
 */
int ending_modal_text_state(const Blob *font, const char *body, uint8_t *pixels, int *center)
{
    char formatted[4096];
    int length;
    if (!body || !center) {
        return 0;
    }
    length = snprintf(formatted, sizeof(formatted),
                      "\014d\013001\011001%s\003c\011000\013180Press a Key!\003l", body);
    if (length < 0 || (size_t)length >= sizeof(formatted) ||
        !text_draw_ending_context_state(font, formatted, 12, 8, 162, 198, 12, 8, 25, pixels,
                                        center)) {
        return 0;
    }
    length = snprintf(formatted, sizeof(formatted),
                      "\01404\013000\011000%s\003c\011000\013180Press a Key!\003l\014d", body);
    return length >= 0 && (size_t)length < sizeof(formatted) &&
           text_draw_ending_context_state(font, formatted, 12, 8, 162, 198, 12, 8, 25, pixels,
                                          center);
}

/* Draw a modal page and key prompt with initially left-aligned text. Borrows font and body and
 * writes caller pixels; returns 1 on success or 0 on formatting or text failure, with possible
 * partial output.
 */
int ending_modal_text(const Blob *font, const char *body, uint8_t *pixels)
{
    int center = 0;
    return ending_modal_text_state(font, body, pixels, &center);
}

/* Format the final-score message from a borrowed payload into the caller's bounded buffer.
 * Returns 1 when the complete terminated message fits or 0 for invalid inputs, formatting
 * failure or truncation; a partial string can remain.
 */
int ending_score_text(const EndingPayload *payload, char *out, size_t capacity)
{
    int length;
    if (!payload || !out) {
        return 0;
    }
    length =
        snprintf(out, capacity,
                 "\n\003cCongratulations\n\nYour Final Score is:\n\n%010" PRIu32
                 "\n\003l\nPlease send this score to the Ancient's Headquarters where you'll be "
                 "added to the Hall of Legends!\n\nAncient's Headquarters\nNew World Computing, "
                 "Inc.\nP.O. Box 4302\nHollywood, CA 90078",
                 payload->score);
    return length >= 0 && (size_t)length < capacity;
}

/* Return the immutable Castleview continuation message. The static string is borrowed for the
 * lifetime of the process and must not be freed.
 */
const char *ending_castleview_text(void)
{
    return "\nAdventurers,\n\nI have saved your game in Castleview.\n\nThe World of Xeen still "
           "needs you!\n\nLoad your game and come visit me in the Great Pyramid for further "
           "instructions";
}

/* Return the immutable unification ceremony message. The static string is borrowed for the
 * lifetime of the process and must not be freed.
 */
const char *ending_world_opening_text(void)
{
    return "\n\n\n\n\n\n\nCongratulations Adventurers!\n\nLet the unification ceremony begin!";
}

/* Return the immutable bonus-badge phrase, or null for no badge or an invalid selection. The
 * returned static string is borrowed and must not be freed.
 */
const char *ending_badge_message(unsigned badge)
{
    static const char *const message[] = {NULL, "I am a Goober!", "I am a Super Goober!"};
    return badge < 3 ? message[badge] : NULL;
}

/* Format the selected bonus-badge message into the caller's bounded buffer. Returns 1 when the
 * complete message fits or 0 for no badge, invalid input, formatting failure or truncation; a
 * partial string can remain.
 */
int ending_world_badge_text(unsigned badge, char *out, size_t capacity)
{
    int length;
    const char *s;
    if (!badge || badge > 2 || !out) {
        return 0;
    }
    s = ending_badge_message(badge);
    length =
        snprintf(out, capacity,
                 "\n\n\n\n\n\n\nBut wait... there's more!\n\n\nInclude the message\n\"%s\"\nwith "
                 "your final score and receive a special bonus.",
                 s);
    return length >= 0 && (size_t)length < capacity;
}

static const short dragon_y[32] = {-3, -3, -3, -3, -3, -3, -3, -3, -1, 0,  0,  0,  0,  0,  0,  0,
                                   -1, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3};
static const short dragon_frame[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 2, 1,
                                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const short claw5_x[32] = {146, 145, 143, 141, 141, 141, 141, 141, 141, 141, 141,
                                  142, 143, 144, 145, 146, 146, 146, 146, 146, 146, 146,
                                  146, 146, 146, 146, 146, 146, 146, 146, 146, 146};
static const short claw5_y[32] = {143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143,
                                  143, 143, 143, 143, 143, 143, 143, 144, 145, 145, 145,
                                  145, 145, 145, 145, 145, 144, 143, 143, 143, 143};
static const short claw_x[32] = {223, 222, 220, 219, 219, 219, 219, 219, 219, 219, 219,
                                 219, 219, 219, 219, 219, 219, 219, 220, 220, 220, 220,
                                 220, 220, 220, 220, 220, 220, 220, 220, 220, 221};
static const short claw_y[32] = {116, 116, 116, 117, 117, 117, 117, 117, 118, 118, 118,
                                 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118,
                                 118, 118, 118, 118, 118, 118, 118, 118, 118, 118};

/* Compose the dragon and claw ornament for a modal page. Borrows cinematic and art storage and
 * takes an in-range animation frame and initial-placement flag. Returns 1 on success or 0 on
 * draw failure, with partial pixels possible.
 */
static int ending_pages_modal_art(Cinematic *cinematic, const Blob *claw, const Blob *dragon,
                                  unsigned frame, int initial)
{
    return cine_draw_effect(cinematic, claw, 5, claw5_x[frame], claw5_y[frame], 0x800) &&
           cine_draw_at(cinematic, claw, 6, initial ? 149 : 145, initial ? 184 : 185, 0) &&
           cine_draw_effect(cinematic, dragon, (unsigned)dragon_frame[frame], 139, dragon_y[frame],
                            0x800) &&
           cine_draw_effect(cinematic, claw, frame % 5, claw_x[frame], claw_y[frame], 0x800);
}

/* Present borrowed message pages with the dragon and claw animation, preserving the supplied
 * initial alignment across pages. Borrows the cinematic, archive and diagnostic prefix;
 * releases temporary art and font. Returns 1 on completion or 0 on resource, text, present or
 * input failure.
 */
int ending_modal_play_aligned(Cinematic *cinematic, CC *intro, const char *const *pages,
                              unsigned count, const char *prefix, int initially_centered)
{
    Blob claw = {0};
    Blob dragon = {0};
    Blob raw = {0};
    Blob pal = {0};
    Blob font = {0};
    uint8_t *saved = NULL;
    unsigned frame = 0;
    unsigned page;
    unsigned serial;
    int ok = 0;
    int center = !!initially_centered;
    char label[192];
    if (!cinematic || !intro || !pages || !count || count > 3 || !prefix) {
        return 0;
    }
    if (!cine_load(intro, "claw.int", &claw) || !cine_load(intro, "dragon1.int", &dragon) ||
        !cine_load(intro, "3room.raw", &raw) || !cine_load(intro, "dark.pal", &pal) ||
        !cine_load(intro, "fnt", &font) || raw.size != XEEN_PIXELS ||
        pal.size != XEEN_PALETTE_BYTES) {
        goto done;
    }
    saved = malloc(XEEN_PIXELS);
    if (!saved) {
        goto done;
    }
    memcpy(saved, raw.data, XEEN_PIXELS);
    memcpy(cinematic->palette, pal.data, XEEN_PALETTE_BYTES);
    memcpy(cinematic->pixels, saved, XEEN_PIXELS);
    if (!ending_pages_modal_art(cinematic, &claw, &dragon, frame, 1)) {
        goto done;
    }
    frame = (frame + 1) % 32;
    snprintf(label, sizeof(label), "%s-initial", prefix);
    if (!cine_retrace(cinematic) || !cine_present(cinematic, label, label)) {
        goto done;
    }
    snprintf(label, sizeof(label), "%s-in", prefix);
    if (!cine_fade(cinematic, 1, 4, label)) {
        goto done;
    }
    if (!cinematic->dump) {
        int key;
        while ((key = platform_pending_key()) != 0) {
            if (key == 2) {
                cinematic->aborted = 1;
                goto done;
            }
        }
    }
    for (page = 0; page < count; page++) {
        serial = 0;
        for (;;) {
            int key;
            cine_reset(cinematic);
            memcpy(cinematic->pixels, saved, XEEN_PIXELS);
            if (!ending_pages_modal_art(cinematic, &claw, &dragon, frame, 0) ||
                !ending_modal_text_state(&font, pages[page], cinematic->pixels, &center)) {
                goto done;
            }
            frame = (frame + 1) % 32;
            snprintf(label, sizeof(label), "%s-page-%u-%04u", prefix, page, serial++);
            if (!cine_retrace(cinematic) || !cine_present(cinematic, label, label)) {
                goto done;
            }
            if (!cinematic->dump && serial == 1 && xeen_trace_enabled()) {
                uint64_t start = platform_cycles();
                xeen_trace_modal(prefix, page, start);
                xeen_trace_main("diagnostics", platform_cycles(), start, 0, 0, "modal-marker");
            }
            if (!cine_wait_ticks(cinematic, 3, 0)) {
                goto done;
            }
            if (cinematic->dump) {
                /* Catalogue mode records a complete animation cycle. Other headless
                 * runs advance after recording the first full page state.
                 */
                if (!getenv("XEEN_MODAL_CATALOG") || serial >= 32) {
                    cine_record(cinematic, "test-only-modal-key");
                    break;
                }
                continue;
            }
            key = platform_pending_key();
            if (key == 2) {
                cinematic->aborted = 1;
                goto done;
            }
            if (key) {
                break;
            }
        }
    }
    ok = 1;
done:
    free(saved);
    free(claw.data);
    free(dragon.data);
    free(raw.data);
    free(pal.data);
    free(font.data);
    return ok;
}

/* Present borrowed message pages with initially left-aligned text and animated dragon art.
 * Borrows the cinematic, archive and diagnostic prefix; returns 1 on completion or 0 on a
 * failed operation or input stop. Temporary assets are released by the aligned implementation.
 */
int ending_modal_play(Cinematic *cinematic, CC *intro, const char *const *pages, unsigned count,
                      const char *prefix)
{
    return ending_modal_play_aligned(cinematic, intro, pages, count, prefix, 0);
}
