/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the Clouds narrator and animated dialogue with speech, captions and credits.
 * clouds_narration uses runtime, subtitles and text while preserving paragraph state across
 * chained voice clips.
 */
#include "clouds_narration.h"
#include "subtitles.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

static const unsigned mouth_frame[8][31] = {
    {4, 2, 3, 0, 2, 3, 2, 0, 1, 1, 3, 4, 3, 2, 4, 2, 3, 4, 3, 4, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 3, 2, 3, 2, 4, 3, 0, 3, 2, 2, 3, 1, 2, 3, 3, 3, 2, 3, 2, 3, 2, 0, 3, 2, 0, 0, 0, 0, 0, 0},
    {2, 4, 3, 1, 2, 3, 0, 3, 4, 3, 2, 3, 0, 3, 2, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {2, 4, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {2, 1, 2, 3, 4, 2, 2, 3, 2, 3, 3, 4, 2, 4, 2, 0, 3, 2, 3, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {2, 3, 0, 2, 3, 2, 0, 2, 3, 2, 4, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {4, 0, 3, 2, 3, 1, 3, 2, 0, 2, 4, 2, 3, 2, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {2, 3, 4, 3, 4, 0, 2, 3, 2, 4, 1, 2, 4, 3, 2, 3, 0, 2, 2, 0, 3, 2, 3, 2, 0, 0, 0, 0, 0, 0, 0}};
static const unsigned mouth_ticks[8][31] = {
    {2,  5,  6,  9,  10, 11, 12, 13, 14, 23, 25, 29, 31, 35, 38, 41,
     42, 45, 50, 52, 55, 56, 57, 0,  0,  0,  0,  0,  0,  0,  0},
    {0,  1, 4,  6, 8,  9, 11, 13, 15, 17, 18, 19, 22, 28, 29, 30,
     31, 0, 39, 0, 44, 0, 50, 51, 0,  54, 0,  0,  0,  0,  0},
    {0,  4, 6, 9, 11, 13, 15, 19, 21, 23, 25, 27, 28, 31, 35, 39,
     40, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {2, 5, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0,  1,  3,  4,  5,  9, 10, 11, 13, 15, 18, 23, 26, 31, 33, 36,
     37, 41, 43, 45, 48, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0},
    {0, 0, 9, 0, 12, 14, 17, 20, 23, 27, 29, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0},
    {0, 2, 4, 8, 11, 13, 15, 16, 17, 19, 21, 24, 24, 27, 34, 35,
     0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {0,  0,  5,  7,  10, 11, 13, 17, 19, 22, 23, 26, 30, 32, 34, 40,
     43, 47, 52, 53, 55, 57, 60, 62, 0,  0,  0,  0,  0,  0,  0}};
static const unsigned mouth_row[14] = {0, 1, 0, 2, 0, 3, 4, 5, 0, 0, 6, 7, 0, 0};
static const char *const voice_names[14] = {
    "crodo1.voc",  "crodo2.voc",  "iamking.voc",  "crodo3.voc",   "ya1.voc",
    "crodo4a.voc", "crodo4b.voc", "crodo4c.voc",  "xeenlaff.voc", "tiger2&.voc",
    "crodo5.voc",  "crodo6.voc",  "xeenlaff.voc", "tiger2&.voc"};

/* Load and decode the named speech asset from the borrowed archive into the supplied voice
 * slot. Temporary archive bytes are freed before return; decoded samples belong to the slot
 * and must be released with voice_free. Returns 1 on success or 0 on read or decode failure.
 */
static int clouds_narration_load_voice(CC *archive, const char *name, Voice *voice)
{
    Blob resource = {0};
    int ok;
    voice_free(voice);
    if (!cine_load(archive, name, &resource)) {
        return 0;
    }
    ok = player_voc_decode(&resource, voice);
    free(resource.data);
    return ok;
}

/* Advance the caption counter through the overlapping counter and text state. Borrows and
 * updates subtitle state; full_text selects the text-window behavior. Returns 1 on success or
 * 0 on text-state failure. The overlapping character boundary is intentional.
 */
static int clouds_narration_advance_caption_counter(Subtitles *subtitle, int full_text)
{
    unsigned start;
    unsigned length;
    if (!subtitle->length) {
        return 0;
    }
    subtitle->counter = (subtitle->counter + 1) % subtitle->length;
    start = subtitle->counter > 40 ? subtitle->counter - 40 : 0;
    length = subtitle->counter - start;
    memcpy(subtitle->window, subtitle->text + start, length);
    subtitle->window[length] = 0;
    /* The terminator is indexed by the total counter, not window length. At
     * character 243 it overlaps and clears the low counter byte, reproducing
     * the original long-caption buffer alias.
     */
    if (!full_text && subtitle->counter == SUBTITLE_COUNTER_ALIAS_CHARACTER) {
        subtitle->counter &= 0xff00u;
    }
    return 1;
}

/* Compose and present one narration frame with optional scrolling text and a diagnostic scene
 * label. Borrows art, font and caption state and updates the cinematic. Returns 1 on success
 * or 0 on draw, text, retrace or present failure.
 */
static int clouds_narration_show(Cinematic *cinematic, const Blob *font, const Blob *box,
                                 Subtitles *subtitle, unsigned scene, unsigned serial)
{
    char label[80];
    /* Escape is checked before changing the caption or presenting an image. */
    if (!cine_poll(cinematic)) {
        return 0;
    }
    if (!text_draw_clouds_credits(font, cinematic->pixels)) {
        return 0;
    }
    if (cinematic->subtitles) {
        if (cinematic->approximate) {
            uint64_t elapsed = cine_secondary_elapsed(cinematic);
            /* Advance once after a signed comparison, then reset the caption
             * timer. Late calls deliberately do not catch up several characters.
             */
            if ((int16_t)(uint16_t)elapsed > 1) {
                if (!clouds_narration_advance_caption_counter(subtitle,
                                                              0)) {
                    return 0;
                }
                if (!cine_secondary_reset(cinematic)) {
                    return 0;
                }
            }
        } else if (cine_tick(cinematic) - subtitle->reset_tick > 1) {
            if (!clouds_narration_advance_caption_counter(subtitle, 0)) {
                return 0;
            }
            subtitle->reset_tick = cine_tick(cinematic);
        }
        if (!cine_draw_at(cinematic, box, 0, 36, 189, 0) ||
            !text_draw_subtitle(font, subtitle->window, cinematic->pixels)) {
            return 0;
        }
    } else {
        subtitle->counter = 0;
    }

    if (!cine_retrace(cinematic)) {
        return 0;
    }
    snprintf(label, sizeof(label), "clouds-narration-%02u-%04u", scene, serial);
    return cine_present(cinematic, label, label);
}

/* Play Crodo's narration with portraits, speech and optional captions. Borrows the cinematic,
 * archive, art and font; owns and releases temporary voice and caption assets. Returns 1 on
 * completion or 0 on load, draw, text, present or input failure.
 */
int clouds_narration(Cinematic *cinematic, CC *archive, const Blob *groupo, const Blob *group,
                     const Blob *crodo, const Blob *font)
{
    Blob pool = {0};
    Blob box = {0};
    Voice voice = {0};
    Voice chain[2] = {{0}};
    Subtitles subtitle = {0};
    unsigned scene;
    unsigned king = 0;
    unsigned royal = 0;
    unsigned xeen = 0;
    unsigned pet = 0;
    unsigned crodo_frame = 0;
    unsigned blink = 0;
    int ok = 0;
    if (!cine_load(archive, "special.bin", &pool) || !cine_load(archive, "box.vga", &box) ||
        !subtitles_select(&subtitle, &pool, 0, 1, cinematic->now)) {
        goto done;
    }
    if (cinematic->approximate && !cine_secondary_reset(cinematic)) {
        goto done;
    }
    for (scene = 0; scene < 14; scene++) {
        unsigned index = 0;
        unsigned serial = 0;
        unsigned row = mouth_row[scene];
        int silent_crodo =
            scene == 2 || scene == 4 || scene == 8 || scene == 9 || scene == 12 || scene == 13;
        /* Approximate dispatches sound starts even when speech is muted.
         * Legacy retains its mute shortcut for diagnostic compatibility.
         */
        if (!cinematic->subtitles || cinematic->approximate) {
            if (scene < 5 || scene > 7) {
                if (!clouds_narration_load_voice(archive, voice_names[scene], &voice)) {
                    goto done;
                }
                cine_voice(cinematic, &voice, voice_names[scene]);
            } else if (scene == 5) {
                if (!clouds_narration_load_voice(archive, voice_names[5], &voice) ||
                    !clouds_narration_load_voice(archive, voice_names[6], &chain[0]) ||
                    !clouds_narration_load_voice(archive, voice_names[7], &chain[1])) {
                    goto done;
                }
                cine_voice(cinematic, &voice, voice_names[5]);
            }
            /* These clips started at the end of the preceding segment. */
        }
        crodo_frame = 0;
        cine_reset(cinematic);
        while (cine_sound_busy(cinematic) ||
               subtitle.counter) {
            if (!cine_draw_at(cinematic, groupo, 0, 0, 0, 0) ||
                !cine_draw_at(cinematic, groupo, 1, 160, 0, 0)) {
                goto done;
            }

            if (scene == 2) {
                royal = (royal + 1) % 5;
                if (!cine_draw_at(cinematic, group, royal, 0, 0, 0)) {
                    goto done;
                }
                pet = (pet + 1) % 9;
            } else if (scene == 4) {
                pet = (pet + 1) % 9 + 9;
            } else if (scene == 8 || scene == 12) {
                xeen = (xeen + 1) % 3;
                pet = (pet + 1) % 9;
                king = (king + 1) % 6 + 3;
            } else if (scene == 9 || scene == 13) {
                king = (king + 1) % 3;
                if (!cine_draw_at(cinematic, group, king + 43, 178, 134, 0)) {
                    goto done;
                }
                pet = (pet + 1) % 9;
                xeen = (xeen + 1) % 15 + 3;
            } else {
                pet = (pet + 1) % 9;
                xeen = (xeen + 1) % 15 + 3;
                king = (king + 1) % 6 + 3;
            }
            if (!cine_draw_at(cinematic, group, pet + 5, 0, 99, 0) ||
                !cine_draw_at(cinematic, group, xeen + 24, 202, 12, 0)) {
                goto done;
            }
            blink = (blink + 1) & 65535u;
            if ((int16_t)blink % 30 == 0 && !cine_draw_at(cinematic, group, 43, 178, 134, 0)) {
                goto done;
            }
            if (!cine_draw_at(cinematic, crodo, silent_crodo ? 0 : crodo_frame, 0, -5, 0) ||
                !clouds_narration_show(cinematic, font, &box, &subtitle, scene, serial++)) {
                goto done;
            }
            if (!silent_crodo) {
                uint64_t deadline;
                unsigned bounded = index > 30 ? 30 : index;
                crodo_frame = mouth_frame[row][bounded];
                deadline = tick_deadline(cinematic->reset, mouth_ticks[row][bounded]);

                if (cinematic->approximate) {
                    /* Read elapsed at least once, even for a zero threshold. Query
                     * sound busy only while the unsigned elapsed value is below it.
                     */
                    for (;;) {
                        uint16_t elapsed = (uint16_t)cine_elapsed(cinematic);
                        if (elapsed >= mouth_ticks[row][bounded] || !cine_sound_busy(cinematic)) {
                            break;
                        }
                    }
                } else if (cine_sound_busy(cinematic) && deadline > cinematic->now) {
                    if (deadline > cinematic->voice_end) {
                        deadline = cinematic->voice_end;
                    }
                    if (!cine_wait_cycles(cinematic, deadline, 0)) {
                        goto done;
                    }
                }
                index = bounded + 1;
                if (cinematic->subtitles && index > 30) {
                    index = 0;
                }
            }
        }
        if (cinematic->events) {
            fprintf(cinematic->events, "checkpoint,clouds-narration-%02u,%llu\n", scene,
                    (unsigned long long)cinematic->now);
        }
        if (cinematic->subtitles) {
            break;
        }
        if (scene == 5) {
            cine_voice(cinematic, &chain[0], voice_names[6]);
        } else if (scene == 6) {
            cine_voice(cinematic, &chain[1], voice_names[7]);
        }
    }
    ok = 1;
done:
    free(pool.data);
    free(box.data);
    voice_free(&voice);
    voice_free(&chain[0]);
    voice_free(&chain[1]);
    return ok;
}
