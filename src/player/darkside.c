/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the Darkside introduction from the NWC and JVC logos through pyramid, panorama and
 * queen dialogue. darkside_play coordinates runtime and darkside_later with independent
 * animation and caption baselines.
 */
#include "audio/file_io.h"
#include "runtime.h"
#include "player_host.h"
#include "subtitles.h"
#include "text.h"
#include "darkside_later.h"
#include <stdlib.h>
#include <string.h>

/* Caption presentation waits for retrace even inside a tight sound-busy
 * loop. Keep that advance local to captions so voice-only timing is preserved.
 */
static int darkside_present_subtitles(Cinematic *cinematic, Subtitles *s, const Blob *font,
                                      const Blob *box, const char *name)
{

    if (cinematic->approximate && !cinematic->subtitles) {
        s->counter = 0;
        return cine_retrace(cinematic) && cine_present(cinematic, name, name);
    }
    if (cinematic->approximate) {
        uint64_t elapsed = cine_secondary_elapsed(cinematic);
        /* A signed secondary-clock delta greater than one advances exactly
         * one character, then resets the caption baseline.
         */
        if ((int16_t)(uint16_t)elapsed > 1) {
            unsigned start;
            unsigned n;
            if (!s->length) {
                return 0;
            }
            s->counter = (s->counter + 1) % s->length;
            start = s->counter > 40 ? s->counter - 40 : 0;
            n = s->counter - start;
            memcpy(s->window, s->text + start, n);
            s->window[n] = 0;
            /* The long-caption terminator overlaps the low counter byte here. */
            if (s->counter == SUBTITLE_COUNTER_ALIAS_CHARACTER) {
                s->counter &= 0xff00u;
            }
            if (!cine_secondary_reset(cinematic)) {
                return 0;
            }
        }
    } else {
        subtitles_update(s, cinematic->now);
    }
    if (!cine_draw_at(cinematic, box, 0, 36, 189, 0) ||
        !text_draw_subtitle(font, s->window, cinematic->pixels)) {
        return 0;
    }
    return cine_retrace(cinematic) && cine_present(cinematic, name, name);
}

/* Keep presenting the current Darkside composition while the primary timer is below the
 * requested hold. Borrows art, font, prefix and caption state and updates the caller's serial.
 * Returns 1 on completion or 0 on draw, text, present, wait or input stop.
 */
static int darkside_hold_subtitles(Cinematic *cinematic, Subtitles *s, const Blob *font,
                                   const Blob *box, unsigned ticks, const char *prefix,
                                   unsigned *serial)
{
    while (cine_elapsed_before(cinematic, ticks)) {
        char label[64];
        snprintf(label, sizeof(label), "%s-text%04u", prefix, (*serial)++);
        if (!darkside_present_subtitles(cinematic, s, font, box, label) || !cine_poll(cinematic)) {
            return 0;
        }
    }
    return 1;
}

typedef struct {
    Blob jvc;
    Blob pal;
    Blob back[2];
    Blob sprite[4];
    Blob voc[3];
    Blob pyramid[2];
    Blob title;
    Blob pyratop;
    Blob pharaoh;
    Blob speech[2];
    Voice pharaoh_voice[2];
    Blob room[3];
    Blob dragon;
    Blob panorama_speech[5];
    Voice panorama_voice[5];
    Blob font;
    Blob text_pool;
    Blob text_box;
    Subtitles subtitle;
    Blob ballraw;
    Blob ballsprite[9];
    Blob ballspeech;
    Blob queen_speech[3];
    Voice ballvoice;
    Voice queen_voice[3];
    Voice voice[3];
} DarksideOpeningAssets;

typedef struct {
    unsigned text_serial;
    unsigned index;
    unsigned frame;
    unsigned sprite_bank;
    unsigned sprite_frame;
    unsigned reveal_step;
    int bypass; /* Phase 308: 17F0D took the short intro's bypass */
    unsigned ball_position;
    char asset_name[64];
    char frame_label[32];
    int result;
} DarksideOpeningWork;

static const char *DARKSIDE_BALL_ASSETS[9] = {"ball.int",   "dragon1.int", "darklord.int",
                                              "goon.int",   "queen.int",   "wizard.int",
                                              "fizzle.int", "claw.int",    "wizmth.int"};
static const int DARKSIDE_BALL_DRAGON_Y[32] = {-3, -3, -3, -3, -3, -3, -3, -3, -1, 0,  0,
                                               0,  0,  0,  0,  0,  -1, -3, -3, -3, -3, -3,
                                               -3, -3, -3, -3, -3, -3, -3, -3, -3, -3};
static const int DARKSIDE_BALL_DRAGON_FRAME[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 2, 1,
                                                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const int DARKSIDE_BALL_CLAW5_X[32] = {146, 145, 143, 141, 141, 141, 141, 141, 141, 141, 141,
                                              142, 143, 144, 145, 146, 146, 146, 146, 146, 146, 146,
                                              146, 146, 146, 146, 146, 146, 146, 146, 146, 146};
static const int DARKSIDE_BALL_CLAW5_Y[32] = {143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143,
                                              143, 143, 143, 143, 143, 143, 143, 144, 145, 145, 145,
                                              145, 145, 145, 145, 145, 144, 143, 143, 143, 143};
static const int DARKSIDE_BALL_DIALOGUE_CLAW_X[32] = {
    223, 222, 220, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219,
    219, 219, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 221};
static const int DARKSIDE_BALL_DIALOGUE_CLAW_Y[32] = {
    116, 116, 116, 117, 117, 117, 117, 117, 118, 118, 118, 118, 118, 118, 118, 118,
    118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118};
static const int DARKSIDE_BALL_REVEAL_CLAW_X[32] = {
    223, 208, 187, 158, 129, 104, 85,  70,  61,  53,  49,  46,  46,  49,  53,  56,
    61,  67,  72,  82,  91,  103, 117, 130, 142, 156, 170, 184, 199, 210, 221, 227};
static const int DARKSIDE_BALL_REVEAL_CLAW_Y[32] = {
    116, 111, 105, 103, 101, 96,  93,  92,  92,  95,  98,  100, 104, 107, 107, 105,
    102, 100, 98,  99,  101, 104, 107, 109, 110, 111, 114, 115, 115, 116, 116, 118};

/* Play the load opening portion of the Darkside introduction. Borrows the cinematic and
 * supplied scene records; updates framebuffer, timing and mutable work state, with loaded
 * assets retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0
 * on a failed operation or input stop.
 */
static int darkside_scene_load_opening(Cinematic *cinematic, CC *archive,
                                       DarksideOpeningAssets *assets, DarksideOpeningWork *work)
{
    if (cinematic->dump) {
        {
            char path[4096];
            if (snprintf(path, sizeof(path), "%s/events.csv", cinematic->dump) >=
                    (int)sizeof(path) ||
                (cinematic->events = xeen_fopen(path, "wb")) == NULL) {
                return 0;
            }
        }
    }
    if (cinematic->subtitles && (!cine_load(archive, "fnt", &assets->font) ||
                                 !cine_load(archive, "special.bin", &assets->text_pool) ||
                                 !cine_load(archive, "box.vga", &assets->text_box))) {
        return 0;
    }
    if (!cine_load(archive, "dark.pal", &assets->pal) || assets->pal.size != XEEN_PALETTE_BYTES) {
        return 0;
    }
    memcpy(cinematic->palette, assets->pal.data, XEEN_PALETTE_BYTES);
    for (work->index = 0; work->index < 2; work->index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "nwc%u.raw", work->index + 1);
        if (!cine_load(archive, work->asset_name, &assets->back[work->index]) ||
            assets->back[work->index].size != XEEN_PIXELS) {
            return 0;
        }
        memcpy(cinematic->background[work->index], assets->back[work->index].data, XEEN_PIXELS);
    }
    for (work->index = 0; work->index < 4; work->index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "nwc%u.int", work->index + 1);
        if (!cine_load(archive, work->asset_name, &assets->sprite[work->index])) {
            return 0;
        }
    }
    for (work->index = 0; work->index < 3; work->index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "dragon%u.voc", work->index + 1);
        if (!cine_load(archive, work->asset_name, &assets->voc[work->index]) ||
            !player_voc_decode(&assets->voc[work->index], &assets->voice[work->index])) {
            return 0;
        }
    }
    return 1;
}

/* Play the nwc logo portion of the Darkside introduction. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_scene_nwc_logo(Cinematic *cinematic, DarksideOpeningAssets *assets,
                                   DarksideOpeningWork *work)
{
    cine_restore(cinematic, 0);
    /* Show the background before fading it in. Legacy retains its separate
     * diagnostic presentation stream.
     */
    if (cinematic->approximate && !cine_present(cinematic, "nwc-initial", "nwc-initial")) {
        return 0;
    }
    if (!cine_fade(cinematic, 1, 4, "nwc-fade-in")) {
        work->result = cinematic->result;
        return 0;
    }
    cine_music(cinematic, 1);
    for (work->frame = 0; work->frame < 55; work->frame++) {
        cine_reset(cinematic);
        cine_restore(cinematic, 0);
        if (!cine_draw(cinematic, &assets->sprite[work->sprite_bank], work->sprite_frame)) {
            return 0;
        }
        snprintf(work->asset_name, sizeof(work->asset_name), "frame%03u", work->frame);
        snprintf(work->frame_label, sizeof(work->frame_label), "%u", work->frame);
        if (!cine_present(cinematic, work->asset_name, work->frame_label)) {
            return 0;
        }

        if (work->frame == 34 || work->frame == 44) {
            work->sprite_frame = 0;
            work->sprite_bank++;
        } else if (work->frame != 17 && work->frame != 35) {
            work->sprite_frame++;
        }

        if (work->frame == 17 &&
            (!cinematic->subtitles || cinematic->approximate)) {
            cine_voice(cinematic, &assets->voice[0], "dragon1.voc");
        }
        if (work->frame == 35 &&
            (!cinematic->subtitles || cinematic->approximate)) {
            cine_voice(cinematic, &assets->voice[1], "dragon2.voc");
        }
        if (!cine_wait_ticks(cinematic, 2, 1)) {
            work->result = cinematic->result;
            return 0;
        }
    }
    for (work->frame = 0; work->frame < 42; work->frame++) {
        cine_reset(cinematic);
        cine_restore(cinematic, 1);
        if (!cine_draw(cinematic, &assets->sprite[3], work->frame)) {
            return 0;
        }
        snprintf(work->asset_name, sizeof(work->asset_name), "frame%03u", work->frame + 55);
        snprintf(work->frame_label, sizeof(work->frame_label), "%u", work->frame + 55);
        if (!cine_present(cinematic, work->asset_name, work->frame_label)) {
            return 0;
        }
        if (work->frame == 3) {
            cine_music(cinematic, 40);
        }
        if (work->frame == 11) {
            cine_music(cinematic, 0);
        }
        if (work->frame == 27 || work->frame == 30) {
            cine_music(cinematic, 3);
        }
        if (work->frame == 31 &&
            (!cinematic->subtitles || cinematic->approximate)) {
            cine_voice(cinematic, &assets->voice[2], "dragon3.voc");
        }
        if (work->frame == 33) {
            cine_music(cinematic, 2);
        }
        if (!cine_wait_ticks(cinematic, 2, 1)) {
            work->result = cinematic->result;
            return 0;
        }
    }
    /* The hold inherits the final frame's reset instead of starting a new timer. */
    if (!cine_wait_ticks(cinematic, 10, 1)) {
        work->result = cinematic->result;
        return 0;
    }

    cine_music_volume(cinematic, 95);
    return 1;
}

/* Play the jvc portion of the Darkside introduction. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int darkside_scene_jvc(Cinematic *cinematic, CC *archive, DarksideOpeningAssets *assets,
                              DarksideOpeningWork *work)
{
    /* Loading JVC changes the offscreen buffer; fade the still-displayed NWC
     * image out before presenting the new background.
     */
    if (!cine_load(archive, "jvc.raw", &assets->jvc) || assets->jvc.size != XEEN_PIXELS) {
        return 0;
    }
    if (!cine_fade(cinematic, 0, 8, "jvc-fade-out")) {
        work->result = cinematic->result;
        return 0;
    }
    memcpy(cinematic->pixels, assets->jvc.data, XEEN_PIXELS);
    if (!cine_present(cinematic, "jvc-black", "jvc-black")) {
        return 0;
    }
    /* Keep the public diagnostic label while loading the selected game resource. */
    if (cinematic->events) {
        fprintf(cinematic->events, "music-load,17e40-resource-stub,%llu\n",
                (unsigned long long)cinematic->now);
    }
    /* 17E2A..17E40: with DS 4188 (1 since startup) and DS 4298 (XEEN.CFG byte 26) set, 1ee18
     * gets 0 instead of DS 42A2 (bigtheme.m): entry 6 with 0 stops the song (Phase 308, D2). */
    if (cinematic->fire_enabled && cinematic->short_intro) {
        cine_music_control(cinematic, 0);
    } else {
        cine_music_load(cinematic, archive, "bigtheme.m");
    }
    if (!cine_fade(cinematic, 1, 4, "jvc-fade-in")) {
        work->result = cinematic->result;
        return 0;
    }
    cine_reset(cinematic);
    return 1;
}

/* Play the pyramid reveal portion of the Darkside introduction. Borrows the cinematic and
 * supplied scene records; updates framebuffer, timing and mutable work state, with loaded
 * assets retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0
 * on a failed operation or input stop.
 */
static int darkside_scene_pyramid_reveal(Cinematic *cinematic, CC *archive,
                                         DarksideOpeningAssets *assets, DarksideOpeningWork *work)
{

    if (!cine_load(archive, "pyramid2.raw", &assets->pyramid[0]) ||
        assets->pyramid[0].size != XEEN_PIXELS ||
        !cine_load(archive, "pyramid3.raw", &assets->pyramid[1]) ||
        assets->pyramid[1].size != XEEN_PIXELS ||
        !cine_load(archive, "title.int", &assets->title) ||
        !cine_load(archive, "pyratop.int", &assets->pyratop)) {
        return 0;
    }
    if (!cine_wait_ticks(cinematic, 60, 1)) {
        work->result = cinematic->result;
        return 0;
    }
    if (cinematic->events) {
        fprintf(cinematic->events, "checkpoint,jvc-completed,%llu\n",
                (unsigned long long)cinematic->now);
    }
    /* 17F0D..17F34: DS 4188 and DS 4298 set: the dialogue is bypassed (Phase 308, D2). */
    if (cinematic->fire_enabled && cinematic->short_intro) {
        work->bypass = 1;
        return 1;
    }
    if (!cine_fade(cinematic, 0, 8, "pyramid-fade-out")) {
        work->result = cinematic->result;
        return 0;
    }
    memcpy(cinematic->background[0], assets->pyramid[0].data, XEEN_PIXELS);
    memcpy(cinematic->background[1], assets->pyramid[1].data, XEEN_PIXELS);
    {
        unsigned row = 0;
        unsigned title_frame = 0;
        unsigned title_counter = 0;
        unsigned step = 0;
        unsigned iteration = 0;
        do {
            uint64_t elapsed;
            cine_reset(cinematic);
            cine_restore_vertical(cinematic, row);
            if (!cine_draw(cinematic, &assets->title, 0) ||
                (title_frame && !cine_draw(cinematic, &assets->title, title_frame))) {
                return 0;
            }
            title_counter = (title_counter + 1) % 4;
            if (!title_counter) {
                title_frame = (title_frame + 1) % 10;
            }
            snprintf(work->asset_name, sizeof(work->asset_name), "pyramid-reveal%03u", iteration++);
            if (!cine_present(cinematic, work->asset_name, work->asset_name)) {
                return 0;
            }
            /* Derive the increment once from elapsed work. Virtual drawing adds no
             * ticks, so its result is the minimum increment.
             */
            if (!step) {
                elapsed = cine_elapsed(cinematic);
                /* Decrement with 16-bit wrap before applying the signed minimum clamp. */
                if (cinematic->approximate) {
                    int value = (int16_t)(uint16_t)(elapsed - 1);
                    step = value >= 1 ? (unsigned)value : 1;
                } else {
                    step = elapsed > 2 ? (unsigned)(elapsed - 1) : 1;
                }
                work->reveal_step = step;
            }
            row = step > 200 - row ? 200 : row + step;
            if (!cine_wait_ticks(cinematic, 1, 0) || !cine_poll(cinematic)) {
                work->result = cinematic->result;
                return 0;
            }
            if (iteration == 1 && !cine_fade(cinematic, 1, 4, "pyramid-fade-in")) {
                work->result = cinematic->result;
                return 0;
            }
        } while (row < 200);
    }
    cine_restore_vertical(cinematic, 200);

    memcpy(cinematic->background[0], cinematic->pixels, XEEN_PIXELS);
    if (!cine_present(cinematic, "pyramid-reveal-end", "pyramid-reveal-end")) {
        return 0;
    }
    cine_reset(cinematic);
    if (!cine_wait_ticks(cinematic, 30, 1)) {
        work->result = cinematic->result;
        return 0;
    }
    return 1;
}

/* Play the pyramid zoom portion of the Darkside introduction. Borrows the cinematic and
 * supplied scene records; updates framebuffer, timing and mutable work state, with loaded
 * assets retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0
 * on a failed operation or input stop.
 */
static int darkside_scene_pyramid_zoom(Cinematic *cinematic, DarksideOpeningAssets *assets,
                                       DarksideOpeningWork *work)
{
    {

        static const int x0[15] = {0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 58, 60, 62};
        static const int x1[15] = {160, 155, 150, 145, 140, 135, 130, 125,
                                   120, 115, 110, 105, 98,  90,  82};
        static const int y[15] = {0, 5, 10, 15, 20, 25, 30, 35, 40, 40, 39, 37, 35, 33, 31};
        int scale;
        for (scale = 14; scale >= 0; scale--) {
            cine_reset(cinematic);
            cine_restore(cinematic, 0);
            if (!cine_draw_at(cinematic, &assets->pyratop, 0, x0[scale], y[scale],
                              (unsigned)scale) ||
                !cine_draw_at(cinematic, &assets->pyratop, 1, x1[scale], y[scale],
                              (unsigned)scale)) {
                return 0;
            }
            snprintf(work->asset_name, sizeof(work->asset_name), "pyramid-zoom%02d", 14 - scale);
            if (!cine_present(cinematic, work->asset_name, work->asset_name)) {
                return 0;
            }
            if (scale == 2) {
                /* This diagnostic label is part of the output format; the music
                 * operation is a volume change rather than an effect cue.
                 */
                if (cinematic->events) {
                    fprintf(cinematic->events, "music,48,%llu\n",
                            (unsigned long long)cinematic->now);
                }
                cine_music_volume(cinematic, 48);
            }
            if (!cine_poll(cinematic) || !cine_wait_ticks(cinematic, 2, 0)) {
                work->result = cinematic->result;
                return 0;
            }
        }
    }
    return 1;
}

/* Play the pharaoh portion of the Darkside introduction. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_scene_pharaoh(Cinematic *cinematic, CC *archive, DarksideOpeningAssets *assets,
                                  DarksideOpeningWork *work)
{

    memcpy(cinematic->background[0], cinematic->pixels, XEEN_PIXELS);
    if (cinematic->events) {
        fprintf(cinematic->events, "checkpoint,pyramid-completed,%llu\n",
                (unsigned long long)cinematic->now);
    }
    /* Caption mode updates during the animation hold and continues until its
     * text completes. Muted sound is immediately idle, so caption completion
     * must remain a separate condition from the voice sample deadline.
     */
    if (!cine_load(archive, "pyramid.int", &assets->pharaoh)) {
        return 0;
    }
    for (work->index = 0; work->index < 2; work->index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "pharoh1%c.voc", 'a' + work->index);
        if (!cine_load(archive, work->asset_name, &assets->speech[work->index]) ||
            !player_voc_decode(&assets->speech[work->index], &assets->pharaoh_voice[work->index])) {
            return 0;
        }
    }

    if (!cinematic->subtitles || cinematic->approximate) {
        cine_voice(cinematic, &assets->pharaoh_voice[0], "pharoh1a.voc");
    }
    if (cinematic->subtitles &&
        !subtitles_select(&assets->subtitle, &assets->text_pool, 0, 8, cinematic->now)) {
        return 0;
    }
    if (cinematic->approximate && !cine_secondary_reset(cinematic)) {
        return 0;
    }
    {
        int chained = 0;
        for (work->frame = 0; work->frame < 19; work->frame++) {
            cine_reset(cinematic);
            cine_restore(cinematic, 0);
            if (!cine_draw_at(cinematic, &assets->pharaoh, work->frame, 132, 62, 0)) {
                return 0;
            }
            snprintf(work->asset_name, sizeof(work->asset_name), "pharaoh-frame%02u", work->frame);
            if (!(cinematic->subtitles
                      ? darkside_present_subtitles(cinematic, &assets->subtitle, &assets->font,
                                                   &assets->text_box, work->asset_name)
                      : cine_present(cinematic, work->asset_name, work->asset_name)) ||
                !cine_poll(cinematic)) {
                work->result = cinematic->result;
                return 0;
            }
            if (!cine_sound_busy(cinematic) && !chained) {
                if (!cinematic->subtitles ||
                    cinematic->approximate) {
                    cine_voice(cinematic, &assets->pharaoh_voice[1], "pharoh1b.voc");
                }
                chained = 1;
            }
            if (cinematic->subtitles) {
                if (!darkside_hold_subtitles(cinematic, &assets->subtitle, &assets->font,
                                             &assets->text_box, 4, "pharaoh", &work->text_serial)) {
                    work->result = cinematic->result;
                    return 0;
                }
            } else if (!cine_wait_ticks(cinematic, 4, 0)) {
                work->result = cinematic->result;
                return 0;
            }
        }
        /* Keep presenting the last image while sound remains busy. A headless
         * run can advance directly to the sample deadline without redrawing.
         */
        while (cine_sound_busy(cinematic) || (cinematic->subtitles && assets->subtitle.counter)) {
            if (cinematic->subtitles) {
                snprintf(work->asset_name, sizeof(work->asset_name), "pharaoh-text%04u",
                         work->text_serial++);
                if (!darkside_present_subtitles(cinematic, &assets->subtitle, &assets->font,
                                                &assets->text_box, work->asset_name) ||
                    !cine_poll(cinematic)) {
                    work->result = cinematic->result;
                    return 0;
                }
                continue;
            }
            uint64_t next = cinematic->now + 1193;
            if (next > cinematic->voice_end) {
                next = cinematic->voice_end;
            }
            if (!cine_poll(cinematic) || !cine_wait_cycles(cinematic, next, 0)) {
                work->result = cinematic->result;
                return 0;
            }
        }
    }
    if (!cine_fade(cinematic, 0, 4, "pharaoh-fade-out")) {
        work->result = cinematic->result;
        return 0;
    }

    if (!cine_middle_reset(cinematic)) {
        return 0;
    }
    if (cinematic->events) {
        fprintf(cinematic->events, "checkpoint,pharaoh-completed,%llu\n",
                (unsigned long long)cinematic->now);
    }
    return 1;
}

/* The panorama's dragon walk: the position in the X/Y path (10 steps), the frame in its 36-frame wing cycle, and the
 * passes at the current position (three per step). */
static const int DARKSIDE_PANORAMA_X[10] = {102, 103, 104, 104, 104, 103, 102, 101, 101, 101};
static const int DARKSIDE_PANORAMA_Y[10] = {30, 29, 28, 27, 26, 25, 24, 25, 26, 28};
static const unsigned DARKSIDE_PANORAMA_FRAMES[36] = {9, 9, 9, 9, 9, 8, 8, 8, 8, 9, 9, 9, 9, 9, 0, 1, 2, 3,
                                                      4, 5, 6, 7, 8, 9, 9, 9, 9, 9, 8, 8, 8, 8, 9, 9, 9, 9};
typedef struct {
    unsigned position;
    unsigned repeat;
    unsigned frame_index;
} DarksidePanoramaWalk;

/* One pass of the walk: every third pass steps the dragon on (position and wing frame wrap), with the music cues on
 * leaving positions 5 (cue 7) and 0 (cue 8). */
static void darkside_panorama_walk(Cinematic *cinematic, DarksidePanoramaWalk *walk)
{
    if (++walk->repeat > 2) {
        if (walk->position == 5) {
            cine_music(cinematic, 7);
        }
        if (walk->position == 0) {
            cine_music(cinematic, 8);
        }
        walk->position = (walk->position + 1) % 10;
        walk->repeat = 0;
        walk->frame_index = (walk->frame_index + 1) % 36;
    }
}

/* Presents the composition as LABEL, through the captions in caption mode. */
static int darkside_panorama_present(Cinematic *cinematic, DarksideOpeningAssets *assets, const char *label)
{
    return cinematic->subtitles
               ? darkside_present_subtitles(cinematic, &assets->subtitle, &assets->font, &assets->text_box, label)
               : cine_present(cinematic, label, label);
}

/* The panorama's resources: the three room images (all loaded and size-checked although only 2room and 3room are
 * drawn), dragon.int and the five pharoh2a..e voices. Replacing a voice slot affects later starts; already playing
 * clips retain their decoded storage until the scene releases them. 1, or 0 on a load or decode failure. */
static int darkside_panorama_load(CC *archive, DarksideOpeningAssets *assets, DarksideOpeningWork *work)
{
    for (work->index = 0; work->index < 3; work->index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "%uroom.raw", work->index + 1);
        if (!cine_load(archive, work->asset_name, &assets->room[work->index]) ||
            assets->room[work->index].size != XEEN_PIXELS) {
            return 0;
        }
    }
    if (!cine_load(archive, "dragon.int", &assets->dragon)) {
        return 0;
    }
    for (work->index = 0; work->index < 5; work->index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "pharoh2%c.voc", 'a' + work->index);
        if (!cine_load(archive, work->asset_name, &assets->panorama_speech[work->index]) ||
            !player_voc_decode(&assets->panorama_speech[work->index], &assets->panorama_voice[work->index])) {
            return 0;
        }
    }
    return 1;
}

/* The opening: the rooms as the two backgrounds, the view at split 320, the caption (text 1, 25 lines), the dragon's
 * first image over black and the fade in. 1, or 0 on a failure (an input stop in the fade sets work->result). */
static int darkside_panorama_open(Cinematic *cinematic, DarksideOpeningAssets *assets, DarksideOpeningWork *work)
{
    memcpy(cinematic->background[0], assets->room[2].data, XEEN_PIXELS);
    memcpy(cinematic->background[1], assets->room[1].data, XEEN_PIXELS);
    cine_restore_horizontal(cinematic, 320);
    if (cinematic->subtitles &&
        !subtitles_select(&assets->subtitle, &assets->text_pool, 1, 25, cinematic->now)) {
        return 0;
    }
    if (!cine_secondary_reset(cinematic)) {
        return 0;
    }
    if (!cine_draw_effect(cinematic, &assets->dragon, 9, DARKSIDE_PANORAMA_X[0], DARKSIDE_PANORAMA_Y[0], 0x0800) ||
        !darkside_panorama_present(cinematic, assets, "panorama-black")) {
        return 0;
    }
    if (!cine_fade(cinematic, 1, 4, "panorama-fade-in")) {
        work->result = cinematic->result;
        return 0;
    }
    return 1;
}

/* The reveal: the split moves from 320 to 0 by two reveal steps a tick while the dragon walks. The first frame starts
 * pharoh2a (voice mode, or approximate captions); each time the voice ends the next of pharoh2b..e starts, and the
 * reveal ends early once the fifth has ended (and the caption, in caption mode). 1, or 0 on a failure or an input
 * stop (the stop's result in work->result). */
static int darkside_panorama_reveal(Cinematic *cinematic, DarksideOpeningAssets *assets, DarksideOpeningWork *work,
                                    DarksidePanoramaWalk *walk)
{
    int voiced = !cinematic->subtitles || cinematic->approximate;
    unsigned chain = 0;
    unsigned iteration = 0;
    int split = 320;
    while (split >= 0) {
        cine_reset(cinematic);
        cine_restore_horizontal(cinematic, (unsigned)split);
        if (!cine_draw_effect(cinematic, &assets->dragon, DARKSIDE_PANORAMA_FRAMES[walk->frame_index],
                              DARKSIDE_PANORAMA_X[walk->position], DARKSIDE_PANORAMA_Y[walk->position], 0x0800)) {
            return 0;
        }
        snprintf(work->asset_name, sizeof(work->asset_name), "panorama-frame%03u", iteration++);
        if (!darkside_panorama_present(cinematic, assets, work->asset_name)) {
            return 0;
        }
        if (split == 320 && voiced) {
            cine_voice(cinematic, &assets->panorama_voice[0], "pharoh2a.voc");
        }
        if (!cine_poll(cinematic)) {
            work->result = cinematic->result;
            return 0;
        }
        if (!cine_sound_busy(cinematic) && chain < 4) {
            chain++;
            snprintf(work->asset_name, sizeof(work->asset_name), "pharoh2%c.voc", 'a' + chain);
            if (voiced) {
                cine_voice(cinematic, &assets->panorama_voice[chain], work->asset_name);
            }
        }
        if (chain == 4 && !cine_sound_busy(cinematic) &&
            (!cinematic->subtitles || !assets->subtitle.counter)) {
            break;
        }
        darkside_panorama_walk(cinematic, walk);
        if (!cine_wait_ticks(cinematic, 1, 0)) {
            work->result = cinematic->result;
            return 0;
        }
        split -= 2 * (int)work->reveal_step;
    }
    return 1;
}

/* One frame of the stationary tail: the view fully revealed, the dragon's image 0 at its position, presented through
 * the captions as panorama-textNNNN. 1, or 0 on a failure. */
static int darkside_panorama_tail_frame(Cinematic *cinematic, DarksideOpeningAssets *assets,
                                        DarksideOpeningWork *work, const DarksidePanoramaWalk *walk)
{
    cine_restore_horizontal(cinematic, 0);
    if (!cine_draw_effect(cinematic, &assets->dragon, 0, DARKSIDE_PANORAMA_X[walk->position],
                          DARKSIDE_PANORAMA_Y[walk->position], 0x0800)) {
        return 0;
    }
    snprintf(work->asset_name, sizeof(work->asset_name), "panorama-text%04u", work->text_serial++);
    return darkside_present_subtitles(cinematic, &assets->subtitle, &assets->font, &assets->text_box,
                                      work->asset_name);
}

/* The stationary tail after the reveal; it cannot start another clip. Three timings:
 * - approximate: tests sound and caption completion before drawing; tail frames while either runs (no poll), then,
 *   with captions, the walk goes on a tick at a time until the middle timer reaches 220 (voice mode bypasses that
 *   caption-only minimum hold);
 * - legacy captions: tail frames, with a poll each, until the caption ends;
 * - legacy voice: the tail drawn once (panorama-tail) while the voice runs, then a wait to its sample deadline.
 * 1, or 0 on a failure or an input stop (work->result set as the base did: not for the approximate tick wait). */
static int darkside_panorama_tail(Cinematic *cinematic, DarksideOpeningAssets *assets, DarksideOpeningWork *work,
                                  DarksidePanoramaWalk *walk)
{
    if (cinematic->approximate) {
        for (;;) {
            if (cine_sound_busy(cinematic) || assets->subtitle.counter) {
                if (!darkside_panorama_tail_frame(cinematic, assets, work, walk)) {
                    return 0;
                }
                continue;
            }
            if (!cinematic->subtitles || (int16_t)(uint16_t)cine_middle_elapsed(cinematic) >= 220) {
                return 1;
            }
            darkside_panorama_walk(cinematic, walk);
            if (!cine_wait_ticks(cinematic, 1, 0)) {
                return 0;
            }
        }
    }
    if (cinematic->subtitles) {
        while (assets->subtitle.counter) {
            if (!darkside_panorama_tail_frame(cinematic, assets, work, walk) || !cine_poll(cinematic)) {
                work->result = cinematic->result;
                return 0;
            }
        }
        return 1;
    }
    if (cine_sound_busy(cinematic)) {
        cine_restore_horizontal(cinematic, 0);
        if (!cine_draw_effect(cinematic, &assets->dragon, 0, DARKSIDE_PANORAMA_X[walk->position],
                              DARKSIDE_PANORAMA_Y[walk->position], 0x0800) ||
            !cine_present(cinematic, "panorama-tail", "panorama-tail")) {
            return 0;
        }
        if (!cine_wait_cycles(cinematic, cinematic->voice_end, 0)) {
            work->result = cinematic->result;
            return 0;
        }
    }
    return 1;
}

/* Play the panorama portion of the Darkside introduction. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 * Stages: darkside_panorama_load, _open (the fade in), _reveal (the split and the five voices), _tail, then the
 * approximate voice stop, the fade out and the panorama-completed checkpoint.
 */
static int darkside_scene_panorama(Cinematic *cinematic, CC *archive, DarksideOpeningAssets *assets,
                                   DarksideOpeningWork *work)
{
    DarksidePanoramaWalk walk = {0, 0, 0};
    if (!darkside_panorama_load(archive, assets, work) || !darkside_panorama_open(cinematic, assets, work) ||
        !darkside_panorama_reveal(cinematic, assets, work, &walk) ||
        !darkside_panorama_tail(cinematic, assets, work, &walk)) {
        return 0;
    }
    if (cinematic->approximate) {
        cine_stop_voice(cinematic);
    }
    if (!cine_fade(cinematic, 0, 4, "panorama-fade-out")) {
        work->result = cinematic->result;
        return 0;
    }
    if (cinematic->events) {
        fprintf(cinematic->events, "checkpoint,panorama-completed,%llu\n",
                (unsigned long long)cinematic->now);
    }
    return 1;
}

/* Play the ball reveal portion of the Darkside introduction. Borrows the cinematic and
 * supplied scene records; updates framebuffer, timing and mutable work state, with loaded
 * assets retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0
 * on a failed operation or input stop.
 */
static int darkside_scene_ball_reveal(Cinematic *cinematic, CC *archive,
                                      DarksideOpeningAssets *assets, DarksideOpeningWork *work)
{
    /* Restore the raw ball background for every composition. Keep the
     * character loads needed by the following dialogue even before drawing them.
     */
    if (!cine_load(archive, "ball.raw", &assets->ballraw) || assets->ballraw.size != XEEN_PIXELS) {
        return 0;
    }
    memcpy(cinematic->background[0], assets->ballraw.data, XEEN_PIXELS);
    for (work->index = 0; work->index < 9; work->index++) {
        if (!cine_load(archive, DARKSIDE_BALL_ASSETS[work->index],
                       &assets->ballsprite[work->index])) {
            return 0;
        }
    }
    if (!cine_load(archive, "pharoh3.voc", &assets->ballspeech) ||
        !player_voc_decode(&assets->ballspeech, &assets->ballvoice)) {
        return 0;
    }
    for (work->frame = 0; work->frame < 32; work->frame++) {
        cine_reset(cinematic);
        cine_restore(cinematic, 0);
        if ((work->frame &&
             !cine_draw_at(cinematic, &assets->ballsprite[0], work->frame - 1, 0, 0, 0)) ||
            !cine_draw_effect(cinematic, &assets->ballsprite[7], 5,
                              DARKSIDE_BALL_CLAW5_X[work->frame],
                              DARKSIDE_BALL_CLAW5_Y[work->frame], 0x0800) ||
            !cine_draw_at(cinematic, &assets->ballsprite[7], 6, 149, 184, 0) ||
            !cine_draw_effect(cinematic, &assets->ballsprite[1],
                              (unsigned)DARKSIDE_BALL_DRAGON_FRAME[work->frame], 139,
                              DARKSIDE_BALL_DRAGON_Y[work->frame], 0x0800) ||
            !cine_draw_effect(cinematic, &assets->ballsprite[7], work->frame % 5,
                              DARKSIDE_BALL_REVEAL_CLAW_X[work->frame],
                              DARKSIDE_BALL_REVEAL_CLAW_Y[work->frame], 0x0800)) {
            return 0;
        }
        snprintf(work->asset_name, sizeof(work->asset_name), "ball-reveal%02u", work->frame);
        if (!cine_present(cinematic, work->asset_name, work->asset_name)) {
            return 0;
        }
        if (!work->frame && !cine_fade(cinematic, 1, 4, "ball-fade-in")) {
            work->result = cinematic->result;
            return 0;
        }
        if (!cine_poll(cinematic) || !cine_wait_ticks(cinematic, 3, 0)) {
            work->result = cinematic->result;
            return 0;
        }
    }
    return 1;
}

/* Play the ball appearance portion of the Darkside introduction. Borrows the cinematic and
 * supplied scene records; updates framebuffer, timing and mutable work state, with loaded
 * assets retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0
 * on a failed operation or input stop.
 */
static int darkside_scene_ball_appearance(Cinematic *cinematic, DarksideOpeningAssets *assets,
                                          DarksideOpeningWork *work)
{
    if (cinematic->subtitles &&
        !subtitles_select(&assets->subtitle, &assets->text_pool, 9, 1, cinematic->now)) {
        return 0;
    }

    if (cinematic->approximate && !cine_secondary_reset(cinematic)) {
        return 0;
    }
    cine_music(cinematic, 42);
    for (work->frame = 0, work->ball_position = 0; work->frame < 11;
         work->frame++, work->ball_position = (work->ball_position + 1) % 32) {
        cine_reset(cinematic);
        cine_restore(cinematic, 0);
        /* The first position requests a negative frame, which draws nothing.
         * Keep the raw ball visible until a nonnegative sprite frame is reached.
         */
        if (work->ball_position &&
            !cine_draw_at(cinematic, &assets->ballsprite[0], work->ball_position - 1, 0, 0, 0)) {
            return 0;
        }
        if (work->frame == 6) {
            if (!cinematic->subtitles || cinematic->approximate) {
                cine_voice(cinematic, &assets->ballvoice, "pharoh3.voc");
            }
            cine_music(cinematic, 0);
        }
        if (!cine_draw_effect(cinematic, &assets->ballsprite[7], 5,
                              DARKSIDE_BALL_CLAW5_X[work->ball_position],
                              DARKSIDE_BALL_CLAW5_Y[work->ball_position], 0x0800) ||
            !cine_draw_at(cinematic, &assets->ballsprite[7], 6, 149, 184, 0) ||
            !cine_draw_effect(cinematic, &assets->ballsprite[1],
                              (unsigned)DARKSIDE_BALL_DRAGON_FRAME[work->ball_position], 139,
                              DARKSIDE_BALL_DRAGON_Y[work->ball_position], 0x0800) ||
            !cine_draw_effect(cinematic, &assets->ballsprite[7], work->ball_position % 5,
                              DARKSIDE_BALL_DIALOGUE_CLAW_X[work->ball_position],
                              DARKSIDE_BALL_DIALOGUE_CLAW_Y[work->ball_position], 0x0800) ||
            (work->frame > 3 && !cine_draw_at(cinematic, &assets->ballsprite[4], 0, 9, 57, 0)) ||
            !cine_draw_at(cinematic, &assets->ballsprite[6], work->frame, 9, 57, 0)) {
            return 0;
        }
        snprintf(work->asset_name, sizeof(work->asset_name), "ball-appearance%02u", work->frame);
        if (!(cinematic->subtitles && work->frame > 6
                  ? darkside_present_subtitles(cinematic, &assets->subtitle, &assets->font,
                                               &assets->text_box, work->asset_name)
                  : cine_present(cinematic, work->asset_name, work->asset_name)) ||
            !cine_poll(cinematic) || !cine_wait_ticks(cinematic, 3, 0)) {
            work->result = cinematic->result;
            return 0;
        }
    }
    if (cinematic->events) {
        fprintf(cinematic->events, "checkpoint,ball-composition-completed,%llu\n",
                (unsigned long long)cinematic->now);
    }
    return 1;
}

/* Play the queen portion of the Darkside introduction. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_scene_queen(Cinematic *cinematic, CC *archive, DarksideOpeningAssets *assets,
                                DarksideOpeningWork *work)
{
    if (!cinematic->disc_known) {
        player_log("ladder stop: queen startup requires CUE track bounds ([files] cue)");
        cinematic->evidence_stop = "18944: queen startup requires CUE track bounds";
        return 0;
    }
    {
        static const char *names[3] = {"queen1.voc", "queen2.voc", "queenhis.voc"};
        static const unsigned selector_bytes[5] = {0xab, 0xe0, 0xe0, 0xab, 0xa4};
        unsigned chain = 0;
        unsigned disappear = 0;
        unsigned iteration = 0;
        for (work->index = 0; work->index < 3; work->index++) {
            if (!cine_load(archive, names[work->index], &assets->queen_speech[work->index]) ||
                !player_voc_decode(&assets->queen_speech[work->index],
                            &assets->queen_voice[work->index])) {
                return 0;
            }
        }
        do {
            unsigned mouth = 0;
            int random_mouth = 0;
            cine_reset(cinematic);
            cine_restore(cinematic, 0);
            if (work->ball_position && !cine_draw_at(cinematic, &assets->ballsprite[0],
                                                     work->ball_position - 1, 0, 0, 0)) {
                return 0;
            }
            /* Both the sound and caption must finish before another voice can start. */
            if (!cine_sound_busy(cinematic) && chain < 3 &&
                (!cinematic->subtitles || !assets->subtitle.counter)) {
                if (cinematic->subtitles) {
                    if (!chain) {
                        if (!subtitles_select(&assets->subtitle, &assets->text_pool, 10, 1,
                                              cinematic->now)) {
                            return 0;
                        }
                    } else {
                        assets->subtitle.counter = 0;
                        assets->subtitle.length = 1;
                    }
                }

                if (cinematic->approximate && !chain && !cine_secondary_reset(cinematic)) {
                    return 0;
                }
                if (!cinematic->subtitles ||
                    cinematic->approximate) {
                    cine_voice(cinematic, &assets->queen_voice[chain], names[chain]);
                }
                chain++;
                if (chain == 3) {
                    disappear = 7;
                }
            }
            if (!cine_draw_effect(cinematic, &assets->ballsprite[7], 5,
                                  DARKSIDE_BALL_CLAW5_X[work->ball_position],
                                  DARKSIDE_BALL_CLAW5_Y[work->ball_position], 0x0800) ||
                !cine_draw_at(cinematic, &assets->ballsprite[7], 6, 149, 184, 0) ||
                !cine_draw_effect(cinematic, &assets->ballsprite[1],
                                  (unsigned)DARKSIDE_BALL_DRAGON_FRAME[work->ball_position], 139,
                                  DARKSIDE_BALL_DRAGON_Y[work->ball_position], 0x0800) ||
                !cine_draw_effect(cinematic, &assets->ballsprite[7], work->ball_position % 5,
                                  DARKSIDE_BALL_DIALOGUE_CLAW_X[work->ball_position],
                                  DARKSIDE_BALL_DIALOGUE_CLAW_Y[work->ball_position], 0x0800)) {
                return 0;
            }
            if (!disappear && chain) {
                uint16_t time_word_high;
                uint16_t time_word_low;
                if (cinematic->approximate) {
                    if (!cine_dos_time(cinematic,
                                       (uint16_t)(0x2c00 | selector_bytes[work->ball_position % 5]),
                                       &time_word_high, &time_word_low)) {
                        return 0;
                    }
                } else {
                    queen_clock(cinematic->clock_origin, cinematic->now - cinematic->clock_start,
                                &time_word_high, &time_word_low);
                }
                mouth = queen_select(&cinematic->selector,
                                     (uint16_t)(0x2c00 | selector_bytes[work->ball_position % 5]),
                                     time_word_high, time_word_low);
                random_mouth = 1;
                if (cinematic->approximate) {
                    cine_coverage_state(cinematic, 0x18aa1, "queen-selector", mouth,
                                        work->ball_position);
                }
                if (cinematic->events) {
                    fprintf(cinematic->events, "selector,18aa1,%llu,%u,%u,%u,%u\n",
                            (unsigned long long)cinematic->now,
                            0x2c00 | selector_bytes[work->ball_position % 5], time_word_high,
                            time_word_low, mouth);
                }
            } else if (disappear) {
                mouth = disappear < 17 ? disappear : 0;
                if (disappear < 17) {
                    disappear++;
                }
            }
            snprintf(work->asset_name, sizeof(work->asset_name), "queen-frame%04u", iteration++);
            /* Caption presentation owns the single ticker update for this image;
             * updating it here as well would advance the same paragraph twice.
             */
            if (cinematic->subtitles && !cinematic->approximate) {
                subtitles_update(&assets->subtitle, cinematic->now);
            }
            if (cinematic->dump && getenv("XEEN_DUMP_QUEEN_CANDIDATES")) {
                uint8_t before[XEEN_PIXELS];
                unsigned candidate;
                memcpy(before, cinematic->pixels, XEEN_PIXELS);
                for (candidate = 0; candidate < (random_mouth ? 6u : 1u); candidate++) {
                    char variant[96];
                    memcpy(cinematic->pixels, before, XEEN_PIXELS);
                    if (!cine_draw_at(cinematic, &assets->ballsprite[4],
                                      random_mouth ? candidate : mouth, 9, 57, 0)) {
                        return 0;
                    }
                    if (cinematic->subtitles &&
                        (!cine_draw_at(cinematic, &assets->text_box, 0, 36, 189, 0) ||
                         !text_draw_subtitle(&assets->font, assets->subtitle.window,
                                             cinematic->pixels))) {
                        return 0;
                    }
                    snprintf(variant, sizeof(variant), "%s-candidate%u", work->asset_name,
                             candidate);
                    if (!cine_present(cinematic, variant, NULL)) {
                        return 0;
                    }
                }
                memcpy(cinematic->pixels, before, XEEN_PIXELS);
            }
            if (!cine_draw_at(cinematic, &assets->ballsprite[4], mouth, 9, 57, 0)) {
                return 0;
            }
            work->ball_position = (work->ball_position + 1) % 32;
            if (!(cinematic->subtitles
                      ? darkside_present_subtitles(cinematic, &assets->subtitle, &assets->font,
                                                   &assets->text_box, work->asset_name)
                      : cine_present(cinematic, work->asset_name, work->asset_name)) ||
                !cine_poll(cinematic) || !cine_wait_ticks(cinematic, 3, 0)) {
                work->result = cinematic->result;
                return 0;
            }
        } while (chain < 3 || cine_sound_busy(cinematic) ||
                 (cinematic->subtitles && assets->subtitle.counter));
    }
    return 1;
}

/* Play the later portion of the Darkside introduction. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_scene_later(Cinematic *cinematic, CC *archive, DarksideOpeningAssets *assets,
                                DarksideOpeningWork *work)
{
    if (cinematic->events) {
        fprintf(cinematic->events, "checkpoint,queen-completed,%llu\n",
                (unsigned long long)cinematic->now);
    }
    work->result =
        darkside_later(cinematic, archive, assets->ballsprite, work->ball_position,
                       &assets->subtitle, &assets->text_pool, &assets->font, &assets->text_box);
    if (work->result != XEEN_COMPLETED) {
        return 0;
    }
    return 1;
}

/* Play the Darkside introduction from the publisher logos and pyramid through the queen and
 * later dialogue. Borrows the cinematic and archive, releases temporary scene assets and
 * returns the cinematic result: completed, failed or skipped.
 */
int darkside_play(Cinematic *cinematic, CC *archive)
{
    DarksideOpeningAssets assets = {0};
    DarksideOpeningWork work;
    unsigned i;
    work.text_serial = 0;
    work.sprite_bank = 0;
    work.sprite_frame = 0;
    work.reveal_step = 1;
    work.bypass = 0;
    work.result = XEEN_FAILED;
    if (!darkside_scene_load_opening(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (!darkside_scene_nwc_logo(cinematic, &assets, &work)) {
        goto done;
    }
    if (!darkside_scene_jvc(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (!darkside_scene_pyramid_reveal(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (work.bypass) {
        work.result = darkside_later_fire_only(cinematic, archive, &assets.subtitle,
                                               &assets.text_pool, &assets.font, &assets.text_box);
        if (work.result == XEEN_COMPLETED) {
            cine_record(cinematic, "completed");
        }
        goto done;
    }
    if (!darkside_scene_pyramid_zoom(cinematic, &assets, &work)) {
        goto done;
    }
    if (!darkside_scene_pharaoh(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (!darkside_scene_panorama(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (!darkside_scene_ball_reveal(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (!darkside_scene_ball_appearance(cinematic, &assets, &work)) {
        goto done;
    }
    if (!darkside_scene_queen(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (!darkside_scene_later(cinematic, archive, &assets, &work)) {
        goto done;
    }
    cine_record(cinematic, "completed");
    work.result = XEEN_COMPLETED;
    goto done;
done:
    free(assets.font.data);
    free(assets.text_pool.data);
    free(assets.text_box.data);
    free(assets.jvc.data);
    free(assets.pal.data);
    for (i = 0; i < 2; i++) {
        free(assets.back[i].data);
    }
    for (i = 0; i < 2; i++) {
        free(assets.pyramid[i].data);
    }
    free(assets.title.data);
    free(assets.pyratop.data);
    free(assets.pharaoh.data);
    for (i = 0; i < 2; i++) {
        free(assets.speech[i].data);
        voice_free(&assets.pharaoh_voice[i]);
    }
    for (i = 0; i < 3; i++) {
        free(assets.room[i].data);
    }
    free(assets.ballraw.data);
    for (i = 0; i < 9; i++) {
        free(assets.ballsprite[i].data);
    }
    free(assets.ballspeech.data);
    voice_free(&assets.ballvoice);
    for (i = 0; i < 3; i++) {
        free(assets.queen_speech[i].data);
        voice_free(&assets.queen_voice[i]);
    }
    free(assets.dragon.data);
    for (i = 0; i < 5; i++) {
        free(assets.panorama_speech[i].data);
        voice_free(&assets.panorama_voice[i]);
    }
    for (i = 0; i < 4; i++) {
        free(assets.sprite[i].data);
    }
    for (i = 0; i < 3; i++) {
        free(assets.voc[i].data);
        voice_free(&assets.voice[i]);
    }
    return work.result;
}
