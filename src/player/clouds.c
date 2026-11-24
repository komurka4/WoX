/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the Clouds introduction through its logo, landscape reveals, narration and closing
 * hands. clouds_opening coordinates runtime, clouds_narration and clouds_closing while
 * preserving the selected timing mode.
 */
#include "audio/file_io.h"
#include "runtime.h"
#include "player_host.h"
#include "clouds_closing.h"
#include "clouds_narration.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    Blob pal;
    Blob jvc;
    Blob blank;
    Blob art[9];
    Blob groupo;
    Blob group;
    Blob crodo;
    Blob font;
} CloudsOpeningAssets;

typedef struct {
    unsigned index;
    unsigned serial;
    unsigned offset;
    unsigned remaining;
    unsigned speed;
    unsigned lake_frame;
    int reveal;
    char label[80];
} CloudsOpeningWork;

/* Play the startup portion of the Clouds introduction. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int clouds_scene_startup(Cinematic *cinematic, CC *archive, CloudsOpeningAssets *assets,
                                CloudsOpeningWork *work)
{
    if (cinematic->dump && !cinematic->events) {
        char path[4096];
        if (snprintf(path, sizeof(path), "%s/events.csv", cinematic->dump) >= (int)sizeof(path) ||
            (cinematic->events = xeen_fopen(path, "wb")) == NULL) {
            return 0;
        }
    }
    const char *names[] = {"stars.vga",    "intro1.vga", "lake.vga",  "xeen.vga", "wiztower.vga",
                           "wiztwer2.vga", "lake2.vga",  "lake3.vga", "xeen1.vga"};
    /* 14eb8..14ee9: the song follows the music selector (DS 4282, XEEN.CFG word 4): 4 bigtheme.m,
     * 5 cantheme.m, otherwise mm4theme.m (Phase 308, owner decision D3; the former native policy
     * played bigtheme.m on every backend but SB Pro). */
    cine_music_load(cinematic, archive,
                    cinematic->music_selector == 4u   ? "bigtheme.m"
                    : cinematic->music_selector == 5u ? "cantheme.m"
                                                      : "mm4theme.m");
    if (!cine_load(archive, "jvc.raw", &assets->jvc) || assets->jvc.size != XEEN_PIXELS ||
        !cine_fade(cinematic, 0, 8, "clouds-jvc-out") ||
        !cine_load(archive, "mm4.pal", &assets->pal) || assets->pal.size != XEEN_PALETTE_BYTES) {
        return 0;
    }
    memcpy(cinematic->pixels, assets->jvc.data, XEEN_PIXELS);
    memcpy(cinematic->palette, assets->pal.data, XEEN_PALETTE_BYTES);

    if (cinematic->approximate &&
        !cine_present(cinematic, "clouds-jvc-black", "clouds-jvc-black")) {
        return 0;
    }
    if (!cine_fade(cinematic, 1, 4, "clouds-jvc-in")) {
        return 0;
    }
    cine_reset(cinematic);
    for (work->index = 0; work->index < 9; work->index++) {
        if (!cine_load(archive, names[work->index], &assets->art[work->index])) {
            return 0;
        }
    }
    if (!cine_wait_ticks(cinematic, 60, 1) || !cine_poll(cinematic) ||
        !cine_fade(cinematic, 0, 8, "clouds-pan-out")) {
        return 0;
    }
    return 1;
}

/* Play the stars reveal portion of the Clouds introduction. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int clouds_scene_stars_reveal(Cinematic *cinematic, CC *archive, CloudsOpeningAssets *assets,
                                     CloudsOpeningWork *work)
{
    free(assets->pal.data);
    assets->pal.data = NULL;
    if (!cine_load(archive, "intro.pal", &assets->pal) || assets->pal.size != XEEN_PALETTE_BYTES ||
        !cine_load(archive, "blank.raw", &assets->blank) || assets->blank.size != XEEN_PIXELS) {
        return 0;
    }
    memcpy(cinematic->palette, assets->pal.data, XEEN_PALETTE_BYTES);
    memcpy(cinematic->pixels, assets->blank.data, XEEN_PIXELS);
    if (!cine_draw_at(cinematic, &assets->art[0], 0, 0, 0, 0) ||
        !cine_draw_at(cinematic, &assets->art[0], 1, 160, 0, 0)) {
        return 0;
    }
    memcpy(cinematic->background[0], cinematic->pixels, XEEN_PIXELS);
    if (!cine_draw_at(cinematic, &assets->art[1], 0, 0, 0, 0)) {
        return 0;
    }
    memcpy(cinematic->background[1], cinematic->pixels, XEEN_PIXELS);
    do {
        cine_reset(cinematic);
        cine_restore_vertical(cinematic, work->offset);
        if (work->remaining < 160 && !cine_draw_at(cinematic, &assets->art[3], 0, 0, 0, 0)) {
            return 0;
        }
        if (work->remaining < 100) {
            if (!cine_draw_at(cinematic, &assets->art[3], 0, 0, 0, 0)) {
                return 0;
            }
            if (++work->reveal < 14 &&
                !cine_draw_at(cinematic, &assets->art[8], (unsigned)work->reveal, 0, 0, 0)) {
                return 0;
            }
        }
        snprintf(work->label, sizeof(work->label), "clouds-stars-%03u", work->serial++);
        if (!cine_present(cinematic, work->label, work->label) || !cine_poll(cinematic)) {
            return 0;
        }
        /* Derive the pan increment from the first iteration's elapsed BIOS ticks,
         * with a minimum of one, so loading and drawing work can influence it.
         */
        if (!work->speed) {
            work->speed = (unsigned)cine_elapsed(cinematic);
            if (!work->speed) {
                work->speed = 1;
            }
        }
        work->offset += work->speed;
        work->remaining = work->remaining > work->speed ? work->remaining - work->speed : 0;
        if (work->serial == 1 && !cine_fade(cinematic, 1, 4, "clouds-stars-in")) {
            return 0;
        }
        if (!cine_wait_ticks(cinematic, 1, 0)) {
            return 0;
        }
    } while (work->remaining);
    return 1;
}

/* Play the lake reveal portion of the Clouds introduction. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int clouds_scene_lake_reveal(Cinematic *cinematic, CloudsOpeningAssets *assets,
                                    CloudsOpeningWork *work)
{
    /* Keep the same increment for both reveals. The lake enters over the
     * introduction image; both banks include the blank background.
     */
    memcpy(cinematic->pixels, assets->blank.data, XEEN_PIXELS);
    if (!cine_draw_at(cinematic, &assets->art[1], 0, 0, 0, 0)) {
        return 0;
    }
    memcpy(cinematic->background[0], cinematic->pixels, XEEN_PIXELS);
    if (!cine_draw_at(cinematic, &assets->art[2], 0, 0, 0, 0)) {
        return 0;
    }
    memcpy(cinematic->background[1], cinematic->pixels, XEEN_PIXELS);
    work->offset = 0;
    work->remaining = 200;
    work->serial = 0;
    work->reveal = 0;
    {
        unsigned passed = 0;
        do {
            cine_reset(cinematic);
            work->remaining = work->remaining > work->speed ? work->remaining - work->speed : 0;
            work->offset += work->speed;
            if (work->offset > 200) {
                work->offset = 200;
            }
            cine_restore_vertical(cinematic, work->offset);
            if (work->remaining < 95) {
                ++work->reveal;
                if (work->reveal < 44 && !passed) {
                    if (!cine_draw_effect(cinematic, &assets->art[7], (unsigned)work->reveal, 0,
                                          (int)work->remaining, 0x800)) {
                        return 0;
                    }
                } else {
                    work->reveal = 0;
                    ++passed;
                }
            }
            if (!cine_draw_at(cinematic, &assets->art[3], 0, 0, 0, 0)) {
                return 0;
            }
            snprintf(work->label, sizeof(work->label), "clouds-lake-%03u", work->serial++);
            if (!cine_present(cinematic, work->label, work->label) || !cine_poll(cinematic) ||
                !cine_wait_ticks(cinematic, 1, 0)) {
                return 0;
            }
        } while (work->remaining);
    }
    return 1;
}

/* Play the lake hold portion of the Clouds introduction. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int clouds_scene_lake_hold(Cinematic *cinematic, CloudsOpeningAssets *assets,
                                  CloudsOpeningWork *work)
{

    if (!cine_draw_effect(cinematic, &assets->art[2], 0, 0, 0, 0x800)) {
        return 0;
    }
    memcpy(cinematic->background[0], cinematic->pixels, XEEN_PIXELS);
    {
        work->serial = 0;
        cine_reset(cinematic);
        do {
            cine_restore(cinematic, 0);
            if (!cine_draw_effect(cinematic, &assets->art[6], work->lake_frame, 0, 0, 0x800)) {
                return 0;
            }
            snprintf(work->label, sizeof(work->label), "clouds-lake-hold-%03u", work->serial++);
            if (!cine_present(cinematic, work->label, work->label) || !cine_retrace(cinematic) ||
                !cine_poll(cinematic)) {
                return 0;
            }
            work->lake_frame = (work->lake_frame + 1) % 43;
        } while (cine_elapsed_before(cinematic, 100));
    }
    cine_record(cinematic, "clouds-boundary-15468");
    return 1;
}

/* Play the tower zoom portion of the Clouds introduction. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int clouds_scene_tower_zoom(Cinematic *cinematic, CloudsOpeningAssets *assets,
                                   CloudsOpeningWork *work)
{
    /* Legacy stops before composing a zoom tuple outside its allowed set.
     * The current frame and scale still come from this run's clock.
     */
    {
        int scale;
        unsigned sub;
        static const unsigned x0[16] = {0,  5,  10, 15, 20, 25, 30, 35,
                                        40, 44, 48, 52, 56, 60, 64, 68};
        static const unsigned x1[16] = {160, 155, 150, 145, 140, 135, 130, 125,
                                        120, 114, 108, 102, 96,  90,  84,  78};
        static const unsigned y[16] = {0,  6,  12, 17, 20, 23, 26, 29,
                                       32, 35, 38, 41, 44, 47, 50, 51};
        for (scale = 15; scale >= 0; --scale) {
            cine_reset(cinematic);
            sub = 0;
            do {
                if (!cinematic->approximate && (work->lake_frame != 42 || scale != 15)) {
                    cine_record(cinematic, "clouds-boundary-15493");
                    player_log("Clouds stops at source15493: zoom lake%u/scale%d lacks a matching "
                            "whole-image witness",
                            work->lake_frame, scale);
                    cinematic->evidence_stop =
                        "15493: next zoom tuple lacks a matching source-state witness";
                    cinematic->result = XEEN_FAILED;
                    return 0;
                }
                if (cinematic->approximate) {
                    cine_coverage_state(cinematic, 0x154e5, "lake-zoom", work->lake_frame,
                                        (unsigned)scale);
                }
                cine_restore(cinematic, 0);
                if (!cine_draw_effect(cinematic, &assets->art[6], work->lake_frame, 0, 0, 0x800)) {
                    return 0;
                }
                work->lake_frame = (work->lake_frame + 1) % 43;
                if (!cine_draw_at(cinematic, &assets->art[4], 0, (int)x0[scale], (int)y[scale],
                                  (unsigned)scale) ||
                    !cine_draw_at(cinematic, &assets->art[4], 1, (int)x1[scale], (int)y[scale],
                                  (unsigned)scale)) {
                    return 0;
                }
                snprintf(work->label, sizeof(work->label), "clouds-tower-zoom-%02d-%02u", scale,
                         sub++);
                if (!cine_present(cinematic, work->label, work->label) ||
                    !cine_retrace(cinematic) || !cine_poll(cinematic)) {
                    return 0;
                }
            } while (cine_elapsed_before(cinematic, 1));
        }
    }
    return 1;
}

/* Play the tower portion of the Clouds introduction. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int clouds_scene_tower(Cinematic *cinematic, CloudsOpeningAssets *assets,
                              CloudsOpeningWork *work)
{

    /* This transition deliberately keeps the preceding timer baseline. */
    if (!cine_draw_at(cinematic, &assets->art[4], 0, 0, 0, 0) ||
        !cine_draw_at(cinematic, &assets->art[4], 1, 160, 0, 0)) {
        return 0;
    }
    memcpy(cinematic->background[0], cinematic->pixels, XEEN_PIXELS);
    for (work->index = 0; work->index < 39; work->index++) {
        cine_restore(cinematic, 0);
        if (!cine_draw_at(cinematic, &assets->art[5], work->index, 0, 0, 0)) {
            return 0;
        }
        snprintf(work->label, sizeof(work->label), "clouds-tower-%02u", work->index);
        if (!cine_present(cinematic, work->label, work->label)) {
            return 0;
        }
        cine_reset(
            cinematic); /* Reset after presentation so its work is outside the following hold. */
        if (!cine_poll(cinematic) || !cine_wait_ticks(cinematic, 2, 0)) {
            return 0;
        }
    }
    /* Preserve the preceding timer baseline across the transition. */
    return 1;
}

/* Play the credits portion of the Clouds introduction. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int clouds_scene_credits(Cinematic *cinematic, CC *archive, CloudsOpeningAssets *assets)
{
    if (!cine_load(archive, "groupo.vga", &assets->groupo) ||
        !cine_load(archive, "group.vga", &assets->group) ||
        !cine_load(archive, "crodo.vga", &assets->crodo) ||
        !cine_load(archive, "fnt", &assets->font) ||
        !cine_fade(cinematic, 0, 4, "clouds-tower-out")) {
        return 0;
    }
    if (!cine_draw_at(cinematic, &assets->groupo, 0, 0, 0, 0) ||
        !cine_draw_at(cinematic, &assets->groupo, 1, 160, 0, 0) ||
        !cine_draw_at(cinematic, &assets->crodo, 0, 0, -5, 0) ||
        !text_draw_clouds_credits(&assets->font, cinematic->pixels)) {
        return 0;
    }
    /* The opening hands save this composition and reveal it through the
     * same hand and marble animation used when closing.
     */
    if (!clouds_hands(cinematic, archive, 0, 1, 0)) {
        return 0;
    }
    cine_music_volume(cinematic, 75);
    memcpy(cinematic->background[0], cinematic->pixels, XEEN_PIXELS);
    if (!cine_present(cinematic, "clouds-narration-initial", "clouds-narration-initial")) {
        return 0;
    }
    return 1;
}

/* Play the narration and closing portion of the Clouds introduction. Borrows the cinematic and
 * supplied scene records; updates framebuffer, timing and mutable work state, with loaded
 * assets retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0
 * on a failed operation or input stop.
 */
static int clouds_scene_narration_and_closing(Cinematic *cinematic, CC *archive,
                                              CloudsOpeningAssets *assets)
{

    if (!clouds_narration(cinematic, archive, &assets->groupo, &assets->group, &assets->crodo,
                          &assets->font)) {
        return 0;
    }
    cine_music_volume(cinematic, 95);
    cine_music(cinematic, 0);
    cine_music_control(cinematic, 50);
    if (!clouds_closing(cinematic, archive, 0)) {
        return 0;
    }

    /* Cleanup presents once more before stopping sound; retain this separate
     * presentation even though the hands helper has already shown its tail.
     */
    if (!cine_present(cinematic, "clouds-cleanup-14cf7", "clouds-cleanup-14cf7")) {
        return 0;
    }
    cinematic->result = XEEN_COMPLETED;
    return 1;
}

/* Play the Clouds introduction from the logo and lake reveal through Crodo's narration and the
 * closing hands. Borrows the cinematic and archive, owns temporary scene assets, and frees
 * them before returning the cinematic result: completed, failed or skipped.
 */
int clouds_opening(Cinematic *cinematic, CC *archive)
{
    CloudsOpeningAssets assets = {0};
    CloudsOpeningWork work;
    unsigned i;
    work.serial = 0;
    work.offset = 0;
    work.remaining = 200;
    work.speed = 0;
    work.lake_frame = 0;
    work.reveal = -1;
    if (!clouds_scene_startup(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (!clouds_scene_stars_reveal(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (!clouds_scene_lake_reveal(cinematic, &assets, &work)) {
        goto done;
    }
    if (!clouds_scene_lake_hold(cinematic, &assets, &work)) {
        goto done;
    }
    if (!clouds_scene_tower_zoom(cinematic, &assets, &work)) {
        goto done;
    }
    if (cinematic->approximate) {
        if (!clouds_scene_tower(cinematic, &assets, &work)) {
            goto done;
        }
        if (!clouds_scene_credits(cinematic, archive, &assets)) {
            goto done;
        }
        if (!clouds_scene_narration_and_closing(cinematic, archive, &assets)) {
            goto done;
        }
    }
done:
    /* Escape from a partial scene still runs the closing animation. A
     * diagnostic stop must remain a failure and must not request a handoff.
     */
    if (cinematic->result == XEEN_SKIPPED) {
        /* A live wait can end early. Synchronize command time before cleanup so
         * the sequencer cannot receive an earlier timestamp.
         */
        cine_reset(cinematic);
        cine_music(cinematic, 0);
        cine_music_control(cinematic, 50);
        if (!clouds_closing(cinematic, archive, 0)) {
            cinematic->result = XEEN_FAILED;
        }
    }
    free(assets.pal.data);
    free(assets.jvc.data);
    free(assets.blank.data);
    for (i = 0; i < 9; i++) {
        free(assets.art[i].data);
    }
    free(assets.groupo.data);
    free(assets.group.data);
    free(assets.crodo.data);
    free(assets.font.data);
    return cinematic->result;
}
