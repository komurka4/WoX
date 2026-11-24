/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Open or close the Clouds hand and marble frame around a saved composition. clouds_hands and
 * clouds_closing use runtime drawing and input; Darkside callers can bypass imagery while
 * retaining an optional fade.
 */
#include "clouds_closing.h"
#include <stdlib.h>
#include <string.h>

/* Animate the opening or closing hands with the selected fade and Darkside cleanup behavior.
 * Borrows the cinematic, archive and diagnostic prefix, loads and releases temporary art, and
 * returns 1 on completion or 0 on load, draw, present or input stop.
 */
int clouds_hands_named(Cinematic *cinematic, CC *archive, int close, int fade, int darkside_flag,
                       const char *prefix)
{
    static const int left_x[8] = {29, 23, 15, -5, -11, -23, -49, -71};
    static const int right_x[8] = {165, 171, 198, 218, 228, 245, 264, 281};
    Blob hand[16] = {{0}};
    Blob marble[4] = {{0}};
    uint8_t *saved = NULL;
    int hand_step;
    int pair_index = close ? 7 : 0;
    int ok = 0;
    unsigned index;
    char label[128];
    char fade_label[128];
    snprintf(fade_label, sizeof(fade_label), "%s-hands-in", prefix);
    /* Darkside omits the hands but retains an explicitly requested entry fade. */
    if (darkside_flag) {
        return !fade || cine_fade(cinematic, 1, 2, fade_label);
    }
    saved = malloc(XEEN_PIXELS);
    if (!saved) {
        goto done;
    }
    memcpy(saved, cinematic->pixels, XEEN_PIXELS);
    for (index = 0; index < 16; index++) {
        snprintf(label, sizeof(label), "hand%02u.vga", index);
        if (!cine_load(archive, label, &hand[index])) {
            goto done;
        }
    }
    for (index = 0; index < 4; index++) {
        snprintf(label, sizeof(label), "marb%02u.vga", index + 1);
        if (!cine_load(archive, label, &marble[index])) {
            goto done;
        }
    }
    /* Opening and closing intentionally have different endpoints. The final
     * composition is handled below, with hands always drawn before marble.
     */
    for (hand_step = close ? 22 : 0; close ? hand_step > 0 : hand_step < 22;
         hand_step += close ? -1 : 1) {
        int bank;
        int frame;
        cine_reset(cinematic);
        memcpy(cinematic->pixels, saved, XEEN_PIXELS);
        if (close ? hand_step <= 14 : hand_step < 14) {
            if (!cine_draw_at(cinematic, &hand[close ? hand_step - 1 : hand_step], 0, 0, 0, 0)) {
                goto done;
            }
        } else {

            if (!cine_draw_effect(cinematic, &hand[14], 0, left_x[pair_index], 0, 0x0800) ||
                !cine_draw_effect(cinematic, &hand[15], 0, right_x[pair_index], 0, 0x0800)) {
                goto done;
            }
            pair_index += close ? -1 : 1;
        }
        bank = (close ? hand_step - 1 : hand_step) / 5;
        frame = (close ? hand_step - 1 : hand_step) % 5;
        if ((close ? hand_step <= 20 : hand_step < 20) &&
            !cine_draw_at(cinematic, &marble[bank], (unsigned)frame, 0, 0, 0)) {
            goto done;
        }
        /* Wait before presentation so this hold belongs to the preceding image. */
        if (!cine_wait_ticks(cinematic, 1, 0)) {
            goto done;
        }
        snprintf(label, sizeof(label), "%s-%s-%02u", prefix, close ? "closing" : "hands",
                 close ? 22 - (unsigned)hand_step : (unsigned)hand_step);
        if (!cine_present(cinematic, label, label)) {
            goto done;
        }
        if (hand_step == 0 && fade && !cine_fade(cinematic, 1, 2, fade_label)) {
            goto done;
        }
        if (!cinematic->dump) {
            int key = platform_pending_key();
            if (key == 2) {
                cinematic->aborted = 1;
                cinematic->result = XEEN_FAILED;
                goto done;
            }
            if (key) {
                break; /* An ordinary key ends the loop but still runs its final composition. */
            }
        }
    }
    /* Closing keeps its final image; opening restores the revealed background. */
    if (close) {
        if (!cine_draw_at(cinematic, &hand[0], 0, 0, 0, 0) ||
            !cine_draw_at(cinematic, &marble[0], 0, 0, 0, 0)) {
            goto done;
        }
    } else {
        memcpy(cinematic->pixels, saved, XEEN_PIXELS);
    }
    snprintf(label, sizeof(label), "%s-%s-final", prefix, close ? "closing" : "hands");
    if (!cine_present(cinematic, label, label)) {
        goto done;
    }
    ok = 1;
done:
    free(saved);
    for (index = 0; index < 16; index++) {
        free(hand[index].data);
    }
    for (index = 0; index < 4; index++) {
        free(marble[index].data);
    }
    if (!ok) {
        cinematic->result = XEEN_FAILED;
    }
    return ok;
}

/* Animate the opening or closing hands using the standard diagnostic prefix. Borrows the
 * cinematic and archive; returns 1 on completion or 0 on a failed operation or input stop,
 * with the cinematic result retaining the cause.
 */
int clouds_hands(Cinematic *cinematic, CC *archive, int close, int fade, int darkside_flag)
{
    return clouds_hands_named(cinematic, archive, close, fade, darkside_flag, "clouds");
}

/* Play the closing hands and their music cleanup using the requested edition behavior. Borrows
 * the cinematic and archive; returns 1 on completion or 0 on failure or input stop.
 */
int clouds_closing(Cinematic *cinematic, CC *archive, int darkside_flag)
{
    return clouds_hands(cinematic, archive, 1, 0, darkside_flag);
}
