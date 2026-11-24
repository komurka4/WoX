/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the Clouds ending from the castle fall through mirror, parade, score and final royal
 * dialogue. clouds_ending_play coordinates the later ending modules with one shared cinematic
 * and caller-owned score payload.
 */
#include "audio/file_io.h"
#include "clouds_ending.h"
#include "frame.h"
#include "clouds_ending_later.h"
#include "clouds_ending_royal.h"
#include "clouds_ending_creatures.h"
#include "clouds_ending_score.h"
#include "text.h"
#include "subtitles.h"
#include <stdlib.h>
#include <string.h>

/* Start or continue the vortex music cue according to the upcoming frame. Borrows and updates
 * cinematic music state without reading the clock; has no immediate error status.
 */
static void clouds_ending_vortex_cue(Cinematic *cinematic, unsigned next)
{

    static const unsigned cue[16] = {0, 34, 33, 0, 0, 34, 0, 33, 0, 34, 33, 0, 0, 33, 0, 34};
    if (next < 16 && cue[next]) {
        cine_music(cinematic, cue[next]);
    }
}

/* Load and decode the named speech asset from the borrowed archive into the supplied voice
 * slot. Temporary archive bytes are freed before return; decoded samples belong to the slot
 * and must be released with voice_free. Returns 1 on success or 0 on read or decode failure.
 */
static int clouds_ending_load_voice(CC *archive, const char *asset_name, Voice *v)
{
    Blob b = {0};
    int ok = 0;
    if (cine_load(archive, asset_name, &b)) {
        ok = player_voc_decode(&b, v);
    }
    free(b.data);
    return ok;
}

/* Advance the cinematic selector and return a frame in the inclusive requested range. Borrows
 * and updates the cinematic, using the supplied accumulator in approximate mode. Clock failure
 * records playback failure and returns zero; no allocation occurs.
 */
static unsigned clouds_ending_select_frame(Cinematic *cinematic, unsigned minimum, unsigned maximum,
                                           uint16_t previous_accumulator)
{
    uint16_t accumulator = 0x2c00;
    uint16_t time_word_high;
    uint16_t time_word_low;
    if (cinematic->approximate) {
        accumulator = previous_accumulator;
        if (!cine_dos_time(cinematic, accumulator, &time_word_high, &time_word_low)) {
            return minimum;
        }
    } else {
        queen_clock(cinematic->clock_origin, cinematic->now - cinematic->clock_start,
                    &time_word_high, &time_word_low);
    }
    return queen_select_range(&cinematic->selector, accumulator, time_word_high, time_word_low,
                              (uint16_t)minimum, (uint16_t)maximum);
}

/* Reset the first helmet caption baseline to avoid a counter stall until
 * the BIOS low word wraps. The timer-quirk option deliberately preserves the
 * original omitted reset; the shared clock itself is unchanged.
 */
static int clouds_ending_start_caption_timer(Cinematic *cinematic)
{
    if (cinematic->approximate && cinematic->subtitles && !cinematic->original_timer_quirks) {
        return cine_secondary_reset(cinematic);
    }
    return 1;
}

/* Compose complete alternative helmet images for selector diagnostics. Each
 * alternative shares the current vortex and text, with one allowed mouth
 * choice. Save and restore the actual image around this diagnostic catalogue.
 */
static int clouds_ending_show_helmet(Cinematic *cinematic, const Blob *helmet, const Blob *font,
                                     Subtitles *text, unsigned serial, unsigned next_vortex)
{
    uint8_t *base = NULL;
    unsigned selected = clouds_ending_select_frame(
        cinematic, 2, 6, (uint16_t)(0x2c00 | (next_vortex ? next_vortex + 1 : 21)));
    unsigned frame;
    char asset_name[96];
    int ok = 0;

    int catalogue =
        cinematic->dump && !cinematic->approximate && getenv("XEEN_ENDING_SELECTOR_CATALOG");
    if (cinematic->subtitles) {
        if (cinematic->approximate) {
            uint64_t elapsed = cine_secondary_elapsed(cinematic);
            /* Advance at most once after a signed secondary-clock comparison.
             * Suppressing the box does not suppress the caption ticker.
             */
            if ((int16_t)(uint16_t)elapsed > 1) {
                unsigned start;
                unsigned n;
                if (!text->length) {
                    return 0;
                }
                text->counter = (text->counter + 1) % text->length;
                start = text->counter > 40 ? text->counter - 40 : 0;
                n = text->counter - start;
                memcpy(text->window, text->text + start, n);
                text->window[n] = 0;
                /* The long-caption terminator overlaps the low counter byte here. */
                if (text->counter == SUBTITLE_COUNTER_ALIAS_CHARACTER) {
                    text->counter &= 0xff00u;
                }
                if (!cine_secondary_reset(cinematic)) {
                    return 0;
                }
            }
        } else {
            subtitles_update(text, cinematic->now);
        }
    } else {
        text->counter = 0;
    }
    if (catalogue) {
        base = malloc(XEEN_PIXELS);
        if (!base) {
            goto done;
        }
        memcpy(base, cinematic->pixels, XEEN_PIXELS);
        for (frame = 2; frame <= 6; frame++) {
            memcpy(cinematic->pixels, base, XEEN_PIXELS);
            if (!cine_draw_at(cinematic, helmet, frame, 0, 0, 0)) {
                goto done;
            }
            if (cinematic->subtitles &&
                (!text_draw_subtitle(font, text->window, cinematic->pixels))) {
                goto done;
            }
            snprintf(asset_name, sizeof(asset_name), "selector-160c2-%04u-%u", serial, frame);
            if (!cine_present(cinematic, asset_name, NULL)) {
                goto done;
            }
        }
        memcpy(cinematic->pixels, base, XEEN_PIXELS);
    }
    if (cinematic->approximate) {
        cine_coverage_state(cinematic, 0x160c2, "helmet-selector", selected, next_vortex);
    }
    if (!cine_draw_at(cinematic, helmet, selected, 0, 0, 0)) {
        goto done;
    }
    if (cinematic->subtitles && (!text_draw_subtitle(font, text->window, cinematic->pixels))) {
        goto done;
    }
    snprintf(asset_name, sizeof(asset_name), "clouds-ending-helmet-dialogue-%04u", serial);
    if (!cine_retrace(cinematic) || !cine_present(cinematic, asset_name, asset_name) ||
        !clouds_ending_poll(cinematic)) {
        goto done;
    }
    ok = 1;
done:
    free(base);
    return ok;
}

typedef struct {
    Blob art;
    Blob pal;
    Blob frame;
    Blob castle[7];
    Blob bottom[7];
    Blob helmet[3];
    Blob pool;
    Blob font;
    Blob box;
    Voice voice[5];
    Subtitles text;
    uint8_t *vortex;
} CloudsEndingAssets;

typedef struct {
    unsigned bank_index;
    unsigned frame_index;
    unsigned helmet_bank;
    unsigned serial;
    unsigned vortex_frame;
    unsigned pan_step;
    unsigned sky_offset;
    int active;
    char asset_name[96];
    uint32_t score;
} CloudsEndingWork;

static const unsigned CASTLE_FRAME_COUNTS[7] = {0, 9, 3, 2, 2, 3, 15};

/* Play the castle portion of the Clouds ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int clouds_ending_castle(Cinematic *cinematic, CC *archive, CloudsEndingAssets *assets,
                                CloudsEndingWork *work)
{

    if (cinematic->dump && !cinematic->events) {
        char path[4096];
        if (snprintf(path, sizeof(path), "%s/events.csv", cinematic->dump) >= (int)sizeof(path) ||
            (cinematic->events = xeen_fopen(path, "wb")) == NULL) {
            return 0;
        }
    }
    if (!cine_load(archive, "prec.end", &assets->art) ||
        !cine_load(archive, "mm4e.pal", &assets->pal) || assets->pal.size != XEEN_PALETTE_BYTES) {
        return 0;
    }
    memcpy(cinematic->palette, assets->pal.data, XEEN_PALETTE_BYTES);
    memset(cinematic->rgb, 0, XEEN_PALETTE_BYTES);
    for (work->bank_index = 1; work->bank_index < 42; work->bank_index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "prec00%02u.frm", work->bank_index);
        if (!cine_load(archive, work->asset_name, &assets->frame) ||
            !frame_decode(&assets->frame, cinematic->background[0], XEEN_PIXELS)) {
            return 0;
        }
        free(assets->frame.data);
        assets->frame.data = NULL;
        /* Fade the first frame before starting its hold. Legacy retains its
         * separate diagnostic timestamps for the same images.
         */
        if (!cinematic->approximate || work->bank_index != 1) {
            cine_reset(cinematic);
        }
        cine_restore(cinematic, 0);
        if (!cine_draw_at(cinematic, &assets->art, 0, 0, 0, 0) ||
            !cine_draw_at(cinematic, &assets->art, 1, 160, 0, 0)) {
            return 0;
        }
        snprintf(work->asset_name, sizeof(work->asset_name), "clouds-ending-castle-%02u",
                 work->bank_index);
        if (work->bank_index == 1) {
            if (!cine_present(cinematic, "clouds-ending-initial", "clouds-ending-initial") ||
                !cine_fade(cinematic, 1, 4, "clouds-ending-castle-in")) {
                return 0;
            }
            cine_reset(cinematic);
            if (!cine_wait_fire_ticks(cinematic, 15)) {
                return 0;
            }
            cine_music(cinematic, 1);
            cine_music(cinematic, 34);

            cine_reset(cinematic);
        }

        if (work->bank_index == 8 || work->bank_index == 18 || work->bank_index == 21) {
            cine_music(cinematic, 33);
        } else if (work->bank_index == 19 || work->bank_index == 25) {
            cine_music(cinematic, 34);
        }
        if (!cine_wait_fire_ticks(cinematic, 3) ||
            !cine_present(cinematic, work->asset_name, work->asset_name)) {
            return 0;
        }
    }

    return 1;
}

/* Play the load vortex portion of the Clouds ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int clouds_ending_load_vortex(CC *archive, CloudsEndingAssets *assets,
                                     CloudsEndingWork *work)
{

    assets->vortex = malloc(20 * XEEN_PIXELS);
    if (!assets->vortex) {
        return 0;
    }
    for (work->bank_index = 0; work->bank_index < 20; work->bank_index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "vort%02u.frm", work->bank_index + 1);
        if (!cine_load(archive, work->asset_name, &assets->frame) ||
            !frame_decode(&assets->frame, assets->vortex + work->bank_index * XEEN_PIXELS,
                          XEEN_PIXELS)) {
            return 0;
        }
        free(assets->frame.data);
        assets->frame.data = NULL;
    }
    for (work->bank_index = 1; work->bank_index < 7; work->bank_index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "cast%02u.end", work->bank_index);
        if (!cine_load(archive, work->asset_name, &assets->castle[work->bank_index])) {
            return 0;
        }
        snprintf(work->asset_name, sizeof(work->asset_name), "casb%02u.end", work->bank_index);
        if (!cine_load(archive, work->asset_name, &assets->bottom[work->bank_index])) {
            return 0;
        }
    }
    for (work->bank_index = 0; work->bank_index < 3; work->bank_index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "darklrd%u.end", work->bank_index + 1);
        if (!cine_load(archive, work->asset_name, &assets->helmet[work->bank_index])) {
            return 0;
        }
    }

    return 1;
}

/* Play the vortex hold portion of the Clouds ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int clouds_ending_vortex_hold(Cinematic *cinematic, CC *archive, CloudsEndingAssets *assets,
                                     CloudsEndingWork *work)
{

    cine_music(cinematic, 34);
    for (work->bank_index = 1; work->bank_index < 16; work->bank_index++) {
        cine_reset(cinematic);
        memcpy(cinematic->pixels, assets->vortex + work->vortex_frame * XEEN_PIXELS, XEEN_PIXELS);
        work->vortex_frame = (work->vortex_frame + 1) % 20;
        if (!cine_draw_at(cinematic, &assets->castle[1], 0, 0, 0, 0) ||
            !cine_draw_at(cinematic, &assets->bottom[1], 0, 0, 100, 0)) {
            return 0;
        }
        snprintf(work->asset_name, sizeof(work->asset_name), "clouds-ending-vortex-%03u",
                 work->serial++);
        if (!cine_wait_fire_ticks(cinematic, 3) ||
            !cine_present(cinematic, work->asset_name, work->asset_name)) {
            return 0;
        }
    }
    free(assets->pal.data);
    assets->pal.data = NULL;
    if (!cine_load(archive, "mm4.pal", &assets->pal) || assets->pal.size != XEEN_PALETTE_BYTES) {
        return 0;
    }
    memcpy(cinematic->palette, assets->pal.data, XEEN_PALETTE_BYTES);
    palette_rgb(cinematic->palette, 128, cinematic->rgb);
    /* An increment larger than the palette range uploads only full brightness. */
    if (!cine_retrace(cinematic)) {
        return 0;
    }
    if (!cine_present(cinematic, "clouds-ending-mm4-full", "clouds-ending-mm4-full")) {
        return 0;
    }
    for (work->bank_index = 0; work->bank_index < 5; work->bank_index++) {
        const char *voices[] = {"dark1.voc", "dark2.voc", "dark3.voc", "darklaff.voc",
                                "xeenlaff.voc"};
        if (!clouds_ending_load_voice(archive, voices[work->bank_index],
                                      &assets->voice[work->bank_index])) {
            return 0;
        }
    }

    return 1;
}

/* Play the castle fall portion of the Clouds ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int clouds_ending_castle_fall(Cinematic *cinematic, CloudsEndingAssets *assets,
                                     CloudsEndingWork *work)
{

    for (work->bank_index = 1; work->bank_index < 7; work->bank_index++) {
        for (work->frame_index = 0; work->frame_index < CASTLE_FRAME_COUNTS[work->bank_index];
             work->frame_index++) {
            cine_reset(cinematic);
            memcpy(cinematic->pixels, assets->vortex + work->vortex_frame * XEEN_PIXELS,
                   XEEN_PIXELS);
            work->vortex_frame = (work->vortex_frame + 1) % 20;
            if (work->active && !cine_sound_busy(cinematic)) {
                work->active = 0;
                cine_music(cinematic, 34);
            }
            /* The original replaces the first sound handle with the laugh before
             * playing it. Select that replacement directly; its playable extent
             * also determines how long the busy query remains true.
             */
            if (!work->active && work->bank_index == 1 && work->frame_index == 6) {
                if (!cinematic->subtitles ||
                    cinematic->approximate) {
                    cine_voice(cinematic, &assets->voice[4], "xeenlaff.voc");
                }
                work->active = 1;
            }
            clouds_ending_vortex_cue(cinematic, work->vortex_frame + 1);
            if (!cine_draw_at(cinematic, &assets->castle[work->bank_index], work->frame_index, 0, 0,
                              0) ||
                !cine_draw_at(cinematic, &assets->bottom[work->bank_index], work->frame_index, 0,
                              100, 0)) {
                return 0;
            }
            snprintf(work->asset_name, sizeof(work->asset_name),
                     "clouds-ending-castle-fall-%u-%02u", work->bank_index, work->frame_index);
            if (!cine_wait_fire_ticks(cinematic, 3) ||
                !cine_present(cinematic, work->asset_name, work->asset_name)) {
                return 0;
            }
        }
    }

    return 1;
}

/* Play the helmet appear portion of the Clouds ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int clouds_ending_helmet_appear(Cinematic *cinematic, CloudsEndingAssets *assets,
                                       CloudsEndingWork *work)
{

    for (work->bank_index = 0; work->bank_index < 16; work->bank_index++) {
        cine_reset(cinematic);
        memcpy(cinematic->pixels, assets->vortex + work->vortex_frame * XEEN_PIXELS, XEEN_PIXELS);
        work->vortex_frame = (work->vortex_frame + 1) % 20;
        work->helmet_bank = work->bank_index < 7 ? 0 : work->bank_index < 11 ? 1 : 2;
        work->frame_index = work->bank_index < 7    ? work->bank_index
                            : work->bank_index < 11 ? work->bank_index - 7
                                                    : work->bank_index - 11;
        if (!cine_draw_at(cinematic, &assets->helmet[work->helmet_bank], work->frame_index, 0, 0,
                          0)) {
            return 0;
        }
        clouds_ending_vortex_cue(cinematic, work->vortex_frame + 1);
        snprintf(work->asset_name, sizeof(work->asset_name), "clouds-ending-helmet-appear-%02u",
                 work->bank_index);
        if (!cine_wait_fire_ticks(cinematic, 3) ||
            !cine_present(cinematic, work->asset_name, work->asset_name)) {
            return 0;
        }
    }

    return 1;
}

/* Play the helmet dialogue portion of the Clouds ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int clouds_ending_helmet_dialogue(Cinematic *cinematic, CC *archive,
                                         CloudsEndingAssets *assets, CloudsEndingWork *work)
{

    cine_music_volume(cinematic, 75);
    if (!cine_load(archive, "special.bin", &assets->pool) ||
        !cine_load(archive, "fnt", &assets->font) || !cine_load(archive, "box.vga", &assets->box) ||
        !subtitles_select(&assets->text, &assets->pool, 8, 1, 0)) {
        return 0;
    }
    if (!clouds_ending_start_caption_timer(cinematic)) {
        return 0;
    }
    /* Legacy deliberately keeps a zero-origin caption baseline. */
    assets->text.reset_tick = tick_at(cinematic->clock_start);
    work->serial = 0;
    for (work->bank_index = 0; work->bank_index < 3; work->bank_index++) {
        const char *names[] = {"dark1.voc", "dark2.voc", "dark3.voc"};
        if (!cinematic->subtitles || cinematic->approximate) {
            cine_voice(cinematic, &assets->voice[work->bank_index], names[work->bank_index]);
        }
        do {
            cine_reset(cinematic);
            memcpy(cinematic->pixels, assets->vortex + work->vortex_frame * XEEN_PIXELS,
                   XEEN_PIXELS);
            work->vortex_frame = (work->vortex_frame + 1) % 20;
            clouds_ending_vortex_cue(cinematic, work->vortex_frame + 1);
            if (!clouds_ending_show_helmet(cinematic, &assets->helmet[2], &assets->font,
                                           &assets->text, work->serial++, work->vortex_frame) ||
                !cine_wait_ticks(cinematic, 3, 0)) {
                return 0;
            }
        } while (cine_sound_busy(cinematic) ||
                 assets->text.counter);
        if (cinematic->subtitles) {
            break;
        }
    }

    return 1;
}

/* Play the helmet disappear portion of the Clouds ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int clouds_ending_helmet_disappear(Cinematic *cinematic, CloudsEndingAssets *assets,
                                          CloudsEndingWork *work)
{

    if (!cinematic->subtitles || cinematic->approximate) {
        cine_voice(cinematic, &assets->voice[3], "darklaff.voc");
    }
    cine_music_volume(cinematic, 95);
    for (work->bank_index = 13; work->bank_index-- > 0;) {
        cine_reset(cinematic);
        memcpy(cinematic->pixels, assets->vortex + work->vortex_frame * XEEN_PIXELS, XEEN_PIXELS);
        work->vortex_frame = (work->vortex_frame + 1) % 20;
        work->helmet_bank = work->bank_index < 7 ? 0 : work->bank_index < 11 ? 1 : 2;
        work->frame_index = work->bank_index < 7    ? work->bank_index
                            : work->bank_index < 11 ? work->bank_index - 7
                                                    : work->bank_index - 11;
        if (!cine_draw_at(cinematic, &assets->helmet[work->helmet_bank], work->frame_index, 0, 0,
                          0)) {
            return 0;
        }
        clouds_ending_vortex_cue(cinematic, work->vortex_frame + 1);
        snprintf(work->asset_name, sizeof(work->asset_name), "clouds-ending-helmet-disappear-%02u",
                 work->bank_index);
        if (!cine_wait_fire_ticks(cinematic, 3) ||
            !cine_present(cinematic, work->asset_name, work->asset_name)) {
            return 0;
        }
    }

    return 1;
}

/* Play the later scenes portion of the Clouds ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int clouds_ending_later_scenes(Cinematic *cinematic, CC *archive,
                                      const EndingPayload *payload, CloudsEndingWork *work)
{

    cine_record(cinematic, "clouds-ending-boundary-16214");
    if (!clouds_ending_later(cinematic, archive, work->score, &work->pan_step) ||
        !clouds_ending_royal(cinematic, archive, work->score, work->pan_step, &work->sky_offset)) {
        return 0;
    }
    cine_record(cinematic, "clouds-ending-boundary-169c1");
    /* Approximate plays the complete ending; legacy keeps its bounded
     * first-creature diagnostic prefix.
     */
    if (cinematic->approximate) {
        if (!clouds_ending_creatures(cinematic, archive, work->score, &work->sky_offset) ||
            !clouds_ending_score_roll(cinematic, archive, &work->sky_offset) ||
            !clouds_ending_score_fix(cinematic, archive, payload, &work->sky_offset) ||
            !clouds_ending_score_pages(cinematic, archive, payload, &work->sky_offset) ||
            !clouds_ending_final_royal(cinematic, archive)) {
            return 0;
        }
        /* Cleanup presents the final image before stopping sound. */
        if (!cine_present(cinematic, "clouds-ending-cleanup-14cf7",
                          "clouds-ending-cleanup-14cf7")) {
            return 0;
        }
        cine_stop_voice(cinematic);
        cinematic->result = XEEN_COMPLETED;
    } else if (!clouds_ending_creatures_legacy_prefix(cinematic, archive, work->score,
                                                      &work->sky_offset)) {
        return 0;
    }
    return 1;
}

/* Play the Clouds ending from the falling castle through the royal farewell and score pages.
 * Borrows the cinematic, archive and ending payload; releases temporary assets before
 * returning the cinematic result: completed, failed or skipped.
 */
int clouds_ending_play(Cinematic *cinematic, CC *archive, const EndingPayload *payload)
{
    CloudsEndingAssets assets = {0};
    CloudsEndingWork work;
    work.serial = 0;
    work.vortex_frame = 0;
    work.pan_step = 1;
    work.sky_offset = 0;
    work.active = 0;
    work.score = payload->score;
    if (!clouds_ending_castle(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (!clouds_ending_load_vortex(archive, &assets, &work)) {
        goto done;
    }
    if (!clouds_ending_vortex_hold(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (!clouds_ending_castle_fall(cinematic, &assets, &work)) {
        goto done;
    }
    if (!clouds_ending_helmet_appear(cinematic, &assets, &work)) {
        goto done;
    }
    if (!clouds_ending_helmet_dialogue(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (!clouds_ending_helmet_disappear(cinematic, &assets, &work)) {
        goto done;
    }
    if (!clouds_ending_later_scenes(cinematic, archive, payload, &work)) {
        goto done;
    }
done:
    free(assets.art.data);
    free(assets.pal.data);
    free(assets.frame.data);
    free(assets.vortex);
    free(assets.pool.data);
    free(assets.font.data);
    free(assets.box.data);
    for (work.bank_index = 0; work.bank_index < 5; work.bank_index++) {
        voice_free(&assets.voice[work.bank_index]);
    }
    for (work.bank_index = 0; work.bank_index < 7; work.bank_index++) {
        free(assets.castle[work.bank_index].data);
        free(assets.bottom[work.bank_index].data);
    }
    for (work.bank_index = 0; work.bank_index < 3; work.bank_index++) {
        free(assets.helmet[work.bank_index].data);
    }
    return cinematic->result;
}
