/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the remaining Darkside ending, including transport, Corak's arrival, battle, collapse
 * and score page. darkside_ending_later borrows the caller's archives and ending clock and
 * uses ending_pages for the final input screen.
 */
#include "darkside_ending_later.h"
#include "ending_pages.h"
#include "subtitles.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

/* The caller owns the ending clock; these later scenes inherit its epoch
 * and carry state without reinitializing it.
 */
typedef struct {
    Cinematic *cinematic;
    CC *intro;
    CC *dark;
    Blob art[40];
    Blob pool;
    Blob font;
    Blob box;
    Blob pan[2];
    Voice sound[4];
    const char *voice_name[4];
    Subtitles text;
    unsigned serial;
    uint8_t choice_base[XEEN_PIXELS];
    unsigned choice_slot;
    unsigned choice_upper;
    int choice_active;
} Ending;

/* Replace the indexed art slot with an owned archive member. Borrows and updates the scene
 * record and supplied inputs; newly loaded resources stay attached for its common cleanup.
 * Returns 1 on success or 0 on a failed operation or input stop. State or pixels may already
 * have advanced when it stops.
 */
static int darkside_ending_load_asset(Ending *ending, unsigned slot, const char *name)
{
    free(ending->art[slot].data);
    memset(&ending->art[slot], 0, sizeof(Blob));
    return cine_load(ending->intro, name, &ending->art[slot]);
}

/* Load a full-screen background into cinematic pixels. Borrows and updates the scene record
 * and supplied inputs; newly loaded resources stay attached for its common cleanup. Returns 1
 * on success or 0 on a failed operation or input stop. State or pixels may already have
 * advanced when it stops.
 */
static int darkside_ending_load_background(Ending *ending, const char *name)
{
    Blob b = {0};
    int ok = 0;
    if (cine_load(ending->intro, name, &b) && b.size == XEEN_PIXELS) {
        memcpy(ending->cinematic->pixels, b.data, XEEN_PIXELS);
        ok = 1;
    }
    free(b.data);
    return ok;
}

/* Copy the current framebuffer into the selected saved bank. Borrows and updates the scene
 * record; slot indices must be valid. Does not transfer ownership of art or voice data and has
 * no immediate failure status.
 */
static void darkside_ending_save_background(Ending *ending, unsigned bank)
{
    memcpy(ending->cinematic->background[bank], ending->cinematic->pixels, XEEN_PIXELS);
}

/* Replace both owned panorama images from the scene archive. Borrows and updates the scene
 * record and supplied inputs; newly loaded resources stay attached for its common cleanup.
 * Returns 1 on success or 0 on a failed operation or input stop. State or pixels may already
 * have advanced when it stops.
 */
static int darkside_ending_load_panorama(Ending *ending)
{
    unsigned k;
    for (k = 0; k < 2; k++) {
        free(ending->pan[k].data);
        memset(&ending->pan[k], 0, sizeof(Blob));
        if (!cine_load(ending->intro, k ? "scene4-1.raw" : "scene4.raw", &ending->pan[k]) ||
            ending->pan[k].size != XEEN_PIXELS) {
            return 0;
        }
    }
    return 1;
}

/* Join the two saved panorama images into the framebuffer at the clamped split. Borrows and
 * updates the scene record; slot indices must be valid. Does not transfer ownership of art or
 * voice data and has no immediate failure status.
 */
static void darkside_ending_restore_panorama(Ending *ending, unsigned split)
{
    unsigned row;
    if (split > 320) {
        split = 320;
    }
    for (row = 0; row < 200; row++) {
        memcpy(ending->cinematic->pixels + row * 320, ending->pan[0].data + row * 320 + split,
               320 - split);
        memcpy(ending->cinematic->pixels + row * 320 + 320 - split, ending->pan[1].data + row * 320,
               split);
    }
}

/* Stop live speech and clear its modeled deadline at cached time. Borrows and updates the
 * scene record; slot indices must be valid. Does not transfer ownership of art or voice data
 * and has no immediate failure status.
 */
static void darkside_ending_stop_voice(Ending *ending)
{
    cine_stop_voice(ending->cinematic);
}

/* Load and decode the named speech asset from the borrowed archive into the supplied voice
 * slot. Temporary archive bytes are freed before return; decoded samples belong to the slot
 * and must be released with voice_free. Returns 1 on success or 0 on read or decode failure.
 */
static int darkside_ending_load_voice(Ending *ending, unsigned slot, CC *cc, const char *name)
{
    Blob b = {0};
    int ok;
    voice_free(&ending->sound[slot]);
    ending->voice_name[slot] = name;
    if (!cine_load(cc, name, &b)) {
        return 0;
    }
    ok = player_voc_decode(&b, &ending->sound[slot]);
    free(b.data);
    return ok;
}

/* Start the selected borrowed voice slot. Borrows and updates the scene record; slot indices
 * must be valid. Does not transfer ownership of art or voice data and has no immediate failure
 * status.
 */
static void darkside_ending_play_voice(Ending *ending, unsigned slot)
{
    if (!ending->cinematic->subtitles ||
        ending->cinematic->approximate) {
        cine_voice(ending->cinematic, &ending->sound[slot], ending->voice_name[slot]);
    }
}

/* Draw the requested sprite slot, frame and position. Borrows and updates the scene record and
 * supplied inputs; newly loaded resources stay attached for its common cleanup. Returns 1 on
 * success or 0 on a failed operation or input stop. State or pixels may already have advanced
 * when it stops.
 */
static int darkside_ending_draw_sprite(Ending *ending, unsigned slot, unsigned frame, int x, int y)
{
    return cine_draw_at(ending->cinematic, &ending->art[slot], frame, x, y, 0);
}

/* Draw the requested sprite slot with its ending draw effect. Borrows and updates the scene
 * record and supplied inputs; newly loaded resources stay attached for its common cleanup.
 * Returns 1 on success or 0 on a failed operation or input stop. State or pixels may already
 * have advanced when it stops.
 */
static int darkside_ending_draw_effect(Ending *ending, unsigned slot, unsigned frame, int x, int y)
{
    return cine_draw_effect(ending->cinematic, &ending->art[slot], frame, x, y, 0x800);
}

/* Discard ordinary keys during ending animation and stop only for global abort. Borrows and
 * updates the cinematic; returns 1 to continue or 0 after marking an abort failure. Headless
 * playback continues without polling.
 */
static int darkside_ending_poll_abort(Ending *ending)
{
    if (!ending->cinematic->dump && platform_pending_key() == 2) {
        ending->cinematic->aborted = 1;
        ending->cinematic->result = XEEN_FAILED;
        return 0;
    }
    return 1;
}

/* Compose full alternative images for selector diagnostics, then restore
 * the actual image. Alternatives use allowed mouth frames and do not become
 * subsequent playback inputs.
 */
static int darkside_ending_choice_catalog(Ending *ending, const char *name, int text)
{
    uint8_t actual[XEEN_PIXELS];
    unsigned candidate;
    char label[160];
    if (!ending->choice_active || !ending->cinematic->dump ||
        !getenv("XEEN_ENDING_SELECTOR_CATALOG")) {
        return 1;
    }
    memcpy(actual, ending->cinematic->pixels, XEEN_PIXELS);
    for (candidate = 0; candidate <= ending->choice_upper; candidate++) {
        memcpy(ending->cinematic->pixels, ending->choice_base, XEEN_PIXELS);
        if (!darkside_ending_draw_sprite(ending, ending->choice_slot, candidate, 0, 0)) {
            return 0;
        }
        if (text && ending->cinematic->subtitles &&
            (!cine_draw_at(ending->cinematic, &ending->box, 0, 36, 189, 0) ||
             !text_draw_subtitle(&ending->font, ending->text.window, ending->cinematic->pixels))) {
            return 0;
        }
        snprintf(label, sizeof(label), "%s-allowed%02u", name, candidate);
        if (!cine_present(ending->cinematic, label, NULL)) {
            return 0;
        }
    }
    memcpy(ending->cinematic->pixels, actual, XEEN_PIXELS);
    return 1;
}

/* Apply optional text and diagnostic composition rules, retrace and present the scene, then
 * poll input. Borrows and updates the scene record and supplied inputs; newly loaded resources
 * stay attached for its common cleanup. Returns 1 on success or 0 on a failed operation or
 * input stop. State or pixels may already have advanced when it stops.
 */
static int darkside_ending_present_scene(Ending *ending, const char *scene, int text)
{
    char name[120];
    if (text) {
        if (ending->cinematic->subtitles) {

            if (ending->cinematic->approximate) {
                uint64_t elapsed = cine_secondary_elapsed(ending->cinematic);
                if ((int16_t)(uint16_t)elapsed > 1) {
                    unsigned start;
                    unsigned n;
                    if (!ending->text.length) {
                        return 0;
                    }
                    ending->text.counter = (ending->text.counter + 1) % ending->text.length;
                    start = ending->text.counter > 40 ? ending->text.counter - 40 : 0;
                    n = ending->text.counter - start;
                    memcpy(ending->text.window, ending->text.text + start, n);
                    ending->text.window[n] = 0;
                    /* The terminator uses the total counter and can overlap its low byte. */
                    if (ending->text.counter == SUBTITLE_COUNTER_ALIAS_CHARACTER) {
                        ending->text.counter &= 0xff00u;
                    }
                    if (!cine_secondary_reset(ending->cinematic)) {
                        return 0;
                    }
                }
            } else {
                subtitles_update_tick(&ending->text, cine_tick(ending->cinematic));
            }
            if (!cine_draw_at(ending->cinematic, &ending->box, 0, 36, 189, 0) ||
                !text_draw_subtitle(&ending->font, ending->text.window,
                                    ending->cinematic->pixels)) {
                return 0;
            }
        } else {
            ending->text.counter = 0;
        }
    }
    snprintf(name, sizeof(name), "darkside-ending-%s-%05u", scene, ending->serial++);
    return cine_retrace(ending->cinematic) && darkside_ending_choice_catalog(ending, name, text) &&
           cine_present(ending->cinematic, name, name) && darkside_ending_poll_abort(ending);
}

/* Present the scene and wait the requested primary ticks. Borrows and updates the scene record
 * and supplied inputs; newly loaded resources stay attached for its common cleanup. Returns 1
 * on success or 0 on a failed operation or input stop. State or pixels may already have
 * advanced when it stops.
 */
static int darkside_ending_present_step(Ending *ending, const char *scene, int text, unsigned ticks)
{
    return darkside_ending_present_scene(ending, scene, text) &&
           cine_wait_fire_ticks(ending->cinematic, ticks);
}

/* Select the requested caption and reset its independent timer baseline. Borrows and updates
 * the scene record and supplied inputs; newly loaded resources stay attached for its common
 * cleanup. Returns 1 on success or 0 on a failed operation or input stop. State or pixels may
 * already have advanced when it stops.
 */
static int darkside_ending_select_text(Ending *ending, unsigned entry, unsigned counter)
{
    if (!subtitles_select(&ending->text, &ending->pool, entry, counter, ending->cinematic->now)) {
        return 0;
    }
    /* Reset the caption timer after selecting text. The animation's
     * primary reset remains independent of this secondary baseline.
     */
    if (!cine_secondary_reset(ending->cinematic)) {
        return 0;
    }
    ending->text.reset_tick = cine_tick(ending->cinematic);
    return 1;
}

/* Select an inclusive index from zero through upper using mutable cinematic selector and time
 * state. The busy-call flag preserves the alternate accumulator path. Returns zero on clock
 * failure as well as a legitimate zero selection; failure is recorded in the cinematic. The
 * reason this caller uses that alternate accumulator is unknown.
 */
unsigned darkside_ending_select(Cinematic *cinematic, unsigned upper, int yes_busy_call)
{
    uint16_t time_word_high;
    uint16_t time_word_low;
    uint16_t accumulator = (uint16_t)(yes_busy_call ? 0x2c01 : 0x2c00);
    unsigned selected;
    if (!cine_dos_time(cinematic, accumulator, &time_word_high, &time_word_low)) {
        return 0;
    }
    selected = queen_select_range(&cinematic->selector, accumulator, time_word_high, time_word_low,
                                  0, (uint16_t)upper);
    cine_coverage_state(cinematic, 0x29dac, "darkside-ending-selector", selected, upper);
    return selected;
}

/* Choose a frame from zero through the inclusive upper bound using the ending clock and
 * selector. Borrows and updates the scene record; approximate mode preserves the alternate
 * busy-call accumulator path. Clock failure returns zero and remains recorded in the
 * cinematic.
 */
static unsigned darkside_ending_random_frame(Ending *ending, unsigned upper)
{
    uint16_t time_word_high;
    uint16_t time_word_low;
    if (ending->cinematic->approximate) {
        return darkside_ending_select(ending->cinematic, upper, ending->choice_slot == 11);
    }
    queen_clock(ending->cinematic->clock_origin,
                ending->cinematic->now - ending->cinematic->clock_start, &time_word_high,
                &time_word_low);
    return queen_select_range(&ending->cinematic->selector, 0x2c00, time_word_high, time_word_low,
                              0, (uint16_t)upper);
}

/* Present caption frames until the caption counter wraps to zero. Borrows and updates the
 * scene record and supplied inputs; newly loaded resources stay attached for its common
 * cleanup. Returns 1 on success or 0 on a failed operation or input stop. State or pixels may
 * already have advanced when it stops.
 */
static int darkside_ending_finish_text(Ending *ending, const char *scene)
{
    while (ending->text.counter) {
        if (!darkside_ending_present_scene(ending, scene, 1)) {
            return 0;
        }
    }
    return 1;
}

/* Play a mouth animation followed by its static caption tail. Some closeups
 * draw in place while others restore the saved background on each frame.
 */
static int darkside_ending_play_closeup(Ending *ending, const char *scene, unsigned slot,
                                        unsigned entry, unsigned upper, unsigned cue, int restore,
                                        int tail_sprite, int fade)
{
    unsigned k;
    if (!darkside_ending_select_text(ending, entry, 1)) {
        return 0;
    }
    for (k = 0; k < 15; k++) {
        if (k == cue) {
            darkside_ending_play_voice(ending, 0);
        }
        cine_reset(ending->cinematic);
        if (restore) {
            cine_restore(ending->cinematic, 0);
        }
        /* Only Yes conditions this selector on voice busy. Other closeups
         * select unconditionally; legacy keeps its diagnostic choice sequence.
         */
        ending->choice_active = 1;
        ending->choice_slot = slot;
        ending->choice_upper =
            (ending->cinematic->approximate ? (slot == 11 && !cine_sound_busy(ending->cinematic))
                                            : (!cine_sound_busy(ending->cinematic) && slot == 11))
                ? 0
                : upper;
        memcpy(ending->choice_base, ending->cinematic->pixels, XEEN_PIXELS);
        if (!darkside_ending_draw_sprite(
                ending, slot,
                ending->choice_upper ? darkside_ending_random_frame(ending, upper) : 0, 0, 0) ||
            !darkside_ending_present_scene(ending, scene, 1)) {
            return 0;
        }
        if (!k && fade) {
            unsigned level;
            for (level = 0; level <= 128; level += 4) {
                char label[96];
                palette_rgb(ending->cinematic->palette, level, ending->cinematic->rgb);
                snprintf(label, sizeof(label), "darkside-ending-talk-in%03u", level / 4);
                if (!cine_retrace(ending->cinematic) ||
                    !darkside_ending_choice_catalog(ending, label, 1) ||
                    !cine_present(ending->cinematic, label, label)) {
                    return 0;
                }
            }
        }
        ending->choice_active = 0;
        if (!cine_wait_fire_ticks(ending->cinematic, 3)) {
            return 0;
        }
    }
    do {
        if (restore) {
            cine_restore(ending->cinematic, 0);
        }
        if (tail_sprite && !darkside_ending_draw_sprite(ending, slot, 0, 0, 0)) {
            return 0;
        }
        if (!darkside_ending_present_scene(ending, scene, 1)) {
            return 0;
        }
    } while (ending->text.counter);
    return 1;
}

/* Keep loop state and the filename buffer alive across scene helpers.
 * Scene transitions must not reset the shared ending clock.
 */
typedef struct {
    unsigned index;
    unsigned repeat;
    unsigned frame;
    int offset_y;
    char asset_name[64];
} DarksideEndingWork;

/* Play the door portion of the Darkside ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int darkside_ending_scene_door(Ending *ending, DarksideEndingWork *work)
{

    if (!(darkside_ending_load_voice(ending, 0, ending->intro, "door.voc") &&
          darkside_ending_load_background(ending, "mainback.raw") &&
          darkside_ending_load_asset(ending, 0, "sc03a.end") &&
          darkside_ending_load_asset(ending, 1, "sc03b.end") &&
          darkside_ending_load_asset(ending, 2, "sc03c.end"))) {
        return 0;
    }
    if (!(darkside_ending_draw_sprite(ending, 0, 0, 250, 0))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_present_scene(ending, "door-static", 0))) {
        return 0;
    }
    cine_reset(ending->cinematic);
    if (!(cine_wait_fire_ticks(ending->cinematic, 30))) {
        return 0;
    }
    darkside_ending_play_voice(ending, 0);
    for (work->index = 0; work->index < 6; work->index++) {
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, 1, work->index, 72, 125) &&
              darkside_ending_present_step(ending, "door-opening", 0, 4))) {
            return 0;
        }
    }
    for (work->index = 0; work->index < 19; work->index++) {
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, 2, work->index, 72, 125) &&
              darkside_ending_present_scene(ending, "door-wheel", 0))) {
            return 0;
        }
        if (work->index == 3 || work->index == 11) {
            cine_music(ending->cinematic, 7);
        }
        if (work->index == 7 || work->index == 16) {
            cine_music(ending->cinematic, 8);
        }
        if (!(cine_wait_fire_ticks(ending->cinematic, 4))) {
            return 0;
        }
    }
    return 1;
}

/* Play the transport portion of the Darkside ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int darkside_ending_scene_transport(Ending *ending, DarksideEndingWork *work)
{

    if (!(darkside_ending_load_voice(ending, 0, ending->intro, "whoosh.voc") &&
          darkside_ending_load_voice(ending, 1, ending->intro, "thud.voc") &&
          darkside_ending_load_panorama(ending))) {
        return 0;
    }
    if (!(darkside_ending_load_asset(ending, 3, "disk.end"))) {
        return 0;
    }
    darkside_ending_restore_panorama(ending, 0);
    if (!(darkside_ending_present_scene(ending, "disk-start", 0))) {
        return 0;
    }
    work->offset_y = 101;
    work->repeat = 0;
    work->frame = 0;
    for (work->index = 0; work->index < 320; work->index += 2) {
        cine_reset(ending->cinematic);
        darkside_ending_restore_panorama(ending, work->index);
        if (!(darkside_ending_draw_sprite(ending, 3, work->frame, (int)work->index / 2,
                                          work->offset_y))) {
            return 0;
        }
        if (work->index < 44) {
            if (!(darkside_ending_draw_effect(ending, 3, 11, -(int)work->index, 22))) {
                return 0;
            }
        }
        if (work->index % 22 == 0) {
            darkside_ending_play_voice(ending, 0);
        }
        if (!(darkside_ending_draw_sprite(ending, 3, 10, 0, 134) &&
              darkside_ending_present_step(ending, "disk-traverse", 0, 1))) {
            return 0;
        }
        work->repeat++;
        if (work->repeat % 2) {
            work->frame = (work->frame + 1) % 10;
        }
        if (work->index < 100) {
            work->offset_y--;
        } else if (work->index > 150) {
            work->offset_y++;
        }
    }
    cine_reset(ending->cinematic);
    if (!(cine_wait_fire_ticks(ending->cinematic, 10))) {
        return 0;
    }
    darkside_ending_play_voice(ending, 1);
    while (cine_sound_busy(ending->cinematic)) {
        cine_reset(ending->cinematic);
        if (!(cine_wait_fire_ticks(ending->cinematic, 1))) {
            return 0;
        }
    }
    return 1;
}

/* Play the materialization portion of the Darkside ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_ending_scene_materialization(Ending *ending, DarksideEndingWork *work)
{

    cine_reset(ending->cinematic);
    if (!(darkside_ending_load_voice(ending, 0, ending->intro, "click.voc") &&
          darkside_ending_load_voice(ending, 1, ending->intro, "explosio.voc"))) {
        return 0;
    }
    if (!(cine_wait_fire_ticks(ending->cinematic, 20) &&
          cine_fade(ending->cinematic, 0, 4, "darkside-ending-disk-mechanism-out") &&
          darkside_ending_load_background(ending, "sc050001.raw"))) {
        return 0;
    }
    if (!(darkside_ending_present_scene(ending, "disk-mechanism-static", 0))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(cine_fade(ending->cinematic, 1, 4, "darkside-ending-disk-mechanism-in"))) {
        return 0;
    }
    cine_reset(ending->cinematic);
    for (work->index = 0; work->index < 7; work->index++) {
        snprintf(work->asset_name, sizeof(work->asset_name),
                 work->index < 5 ? "disk%c.end" : "sc06%c.end",
                 work->index < 5 ? 'a' + (int)work->index : 'a' + (int)work->index - 5);
        if (!(darkside_ending_load_asset(ending, 4 + work->index, work->asset_name))) {
            return 0;
        }
    }
    if (!(cine_wait_fire_ticks(ending->cinematic, 30))) {
        return 0;
    }
    for (work->index = 0; work->index < 34; work->index++) {
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, 4 + work->index / 8, work->index % 8, 28, 11) &&
              darkside_ending_present_step(ending, "disk-panels", 0, 3))) {
            return 0;
        }
        if (work->index == 0 || work->index == 2 || work->index == 4 || work->index == 6) {
            darkside_ending_play_voice(ending, 0);
        }
        if (work->index == 18) {
            darkside_ending_play_voice(ending, 1);
        }
        if (work->index >= 9 && work->index <= 12) {
            cine_music(ending->cinematic, 30);
        }
        if (work->index == 13) {
            cine_music(ending->cinematic, 14);
        }
    }
    if (!(darkside_ending_load_background(ending, "mainback.raw") &&
          darkside_ending_load_voice(ending, 0, ending->dark, "alien2.voc"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_draw_sprite(ending, 9, 0, 0, 0) &&
          darkside_ending_present_scene(ending, "corak-materialization-start", 0))) {
        return 0;
    }
    darkside_ending_play_voice(ending, 0);
    for (work->index = 0; work->index < 20; work->index++) {
        if (work->index == 6 || work->index == 8 || work->index == 9 || work->index == 10 ||
            work->index == 13 || work->index == 15 || work->index == 16) {
            cine_music(ending->cinematic, 3);
        }
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, 9 + work->index / 10, work->index % 10, 0, 0) &&
              darkside_ending_present_step(ending, "corak-materialization", 0, 3))) {
            return 0;
        }
    }
    return 1;
}

/* Play the arrival portion of the Darkside ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int darkside_ending_scene_arrival(Ending *ending, DarksideEndingWork *work)
{

    if (!(cine_fade(ending->cinematic, 0, 4, "darkside-ending-corak-arrival-out") &&
          darkside_ending_load_voice(ending, 0, ending->intro, "windstor.voc") &&
          darkside_ending_load_voice(ending, 1, ending->intro, "corak2.voc") &&
          darkside_ending_load_background(ending, "sc070001.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    for (work->index = 0; work->index < 8; work->index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "sc07%c.end", 'a' + (int)work->index);
        if (!(darkside_ending_load_asset(ending, work->index, work->asset_name))) {
            return 0;
        }
    }
    if (!(darkside_ending_present_scene(ending, "corak-arrival-backdrop", 0) &&
          cine_fade(ending->cinematic, 1, 4, "darkside-ending-corak-arrival-in"))) {
        return 0;
    }
    cine_reset(ending->cinematic);
    if (!(cine_wait_fire_ticks(ending->cinematic, 2))) {
        return 0;
    }
    if (!(darkside_ending_select_text(ending, 22, 0))) {
        return 0;
    }
    for (work->index = 0; work->index < 45; work->index++) {
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, work->index / 6, work->index % 6, 61, 12) &&
              darkside_ending_present_step(ending, "corak-arrival", ending->text.counter != 0,
                                           2))) {
            return 0;
        }
        if (work->index == 40) {
            ending->text.counter = 1;
        }
        if (work->index == 1 || work->index == 19) {
            darkside_ending_play_voice(ending, 0);
        }
    }
    darkside_ending_play_voice(ending, 1);
    cine_reset(ending->cinematic);
    /* Sample elapsed before the first tail image. The primary clock read
     * must precede the caption's secondary-clock read.
     */
    if (ending->cinematic->approximate) {
        while (cine_elapsed_before(ending->cinematic, 30) || ending->text.counter) {
            if (!(darkside_ending_poll_abort(ending) &&
                  darkside_ending_present_scene(ending, "corak-arrival-tail", 1))) {
                return 0;
            }
        }
    } else {
        do {
            if (!(darkside_ending_present_scene(ending, "corak-arrival-tail", 1))) {
                return 0;
            }
        } while (cine_elapsed_before(ending->cinematic, 30) || ending->text.counter);
    }
    cine_music(ending->cinematic, 0);
    return 1;
}

/* Play the return dialogue portion of the Darkside ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_ending_scene_return_dialogue(Ending *ending)
{

    if (!(darkside_ending_load_voice(ending, 0, ending->intro, "yes1.voc") &&
          darkside_ending_load_asset(ending, 11, "sc08.end"))) {
        return 0;
    }
    if (!(darkside_ending_play_closeup(ending, "yes", 11, 23, 2, 2, 0, 1, 0))) {
        return 0;
    }
    if (!(darkside_ending_load_background(ending, "sc090001.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_asset(ending, 12, "sc09.end") &&
          darkside_ending_load_voice(ending, 0, ending->intro, "nowre1.voc") &&
          darkside_ending_present_scene(ending, "now-return-backdrop", 0))) {
        return 0;
    }
    if (!(darkside_ending_play_closeup(ending, "now-return", 12, 24, 8, 2, 1, 0, 0))) {
        return 0;
    }
    if (!(darkside_ending_load_asset(ending, 13, "sc10.end") &&
          darkside_ending_load_voice(ending, 0, ending->intro, "nordo2.voc") &&
          darkside_ending_play_closeup(ending, "nor-do-i", 13, 25, 1, 3, 0, 1, 0))) {
        return 0;
    }
    cine_music_control(ending->cinematic, 0);
    cine_music_volume(ending->cinematic, 95);
    cine_music_load(ending->cinematic, ending->dark, "dngon2.m");
    return 1;
}

/* Play the spell lever portion of the Darkside ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_ending_scene_spell_lever(Ending *ending, DarksideEndingWork *work)
{

    if (!(cine_fade(ending->cinematic, 0, 4, "darkside-ending-instrument-out") &&
          darkside_ending_load_background(ending, "blank.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_voice(ending, 0, ending->intro, "click.voc") &&
          darkside_ending_load_voice(ending, 1, ending->dark, "airmon.voc") &&
          darkside_ending_load_asset(ending, 14, "sc11.end"))) {
        return 0;
    }
    for (work->index = 0; work->index < 15; work->index++) {
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, 14, work->index, 100, 44) &&
              darkside_ending_present_scene(ending, "instrument", 0))) {
            return 0;
        }
        if (!work->index) {
            if (!(cine_fade(ending->cinematic, 1, 4, "darkside-ending-instrument-in"))) {
                return 0;
            }
        }
        if (!(cine_wait_fire_ticks(ending->cinematic, 3))) {
            return 0;
        }
        if (work->index == 3) {
            darkside_ending_play_voice(ending, 0);
        }
        if (work->index == 5) {
            darkside_ending_play_voice(ending, 1);
        }
    }
    if (!(cine_fade(ending->cinematic, 0, 4, "darkside-ending-lever-out"))) {
        return 0;
    }
    for (work->offset_y = 80; work->offset_y > 20; work->offset_y -= 2) {
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, 14, 15, 60, 80) &&
              darkside_ending_draw_sprite(ending, 14, 16, 61, work->offset_y) &&
              darkside_ending_draw_sprite(ending, 14, 17, 0, 0) &&
              darkside_ending_present_scene(ending, "lever", 0))) {
            return 0;
        }
        if (work->offset_y == 80) {
            if (!(cine_fade(ending->cinematic, 1, 4, "darkside-ending-lever-in"))) {
                return 0;
            }
        }
        if (!(cine_wait_fire_ticks(ending->cinematic, 3))) {
            return 0;
        }
    }
    return 1;
}

/* Play the ready closeup portion of the Darkside ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_ending_scene_ready_closeup(Ending *ending)
{

    if (!(cine_fade(ending->cinematic, 0, 4, "darkside-ending-ready-card-out") &&
          darkside_ending_load_background(ending, "scene12.raw") &&
          darkside_ending_present_scene(ending, "ready-card", 0) &&
          cine_fade(ending->cinematic, 1, 4, "darkside-ending-ready-card-in"))) {
        return 0;
    }
    cine_reset(ending->cinematic);
    if (!(cine_wait_fire_ticks(ending->cinematic, 30) &&
          cine_fade(ending->cinematic, 0, 4, "darkside-ending-ready-out"))) {
        return 0;
    }
    cine_music_volume(ending->cinematic, 48);
    if (!(darkside_ending_load_asset(ending, 15, "sc13.end") &&
          darkside_ending_load_voice(ending, 0, ending->intro, "ready2.voc") &&
          darkside_ending_play_closeup(ending, "ready", 15, 26, 3, 1, 0, 1, 1))) {
        return 0;
    }
    return 1;
}

/* Play the fight portion of the Darkside ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int darkside_ending_scene_fight(Ending *ending, DarksideEndingWork *work)
{

    if (!(cine_fade(ending->cinematic, 0, 4, "darkside-ending-fight-out") &&
          darkside_ending_load_background(ending, "blank.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_voice(ending, 0, ending->intro, "fight2.voc") &&
          darkside_ending_load_asset(ending, 16, "sc14.end"))) {
        return 0;
    }
    cine_restore(ending->cinematic, 0);
    if (!(darkside_ending_draw_sprite(ending, 16, 0, 37, 37) &&
          darkside_ending_present_scene(ending, "fight-static", 0) &&
          cine_fade(ending->cinematic, 1, 4, "darkside-ending-fight-in") &&
          darkside_ending_select_text(ending, 27, 1))) {
        return 0;
    }
    for (work->repeat = 0; work->repeat < 2; work->repeat++) {
        for (work->index = 0; work->index < 10; work->index++) {
            if (!work->repeat && work->index == 3) {
                darkside_ending_play_voice(ending, 0);
            }
            cine_reset(ending->cinematic);
            cine_restore(ending->cinematic, 0);
            if (!(darkside_ending_draw_sprite(ending, 16, work->index, 37, 37) &&
                  darkside_ending_present_step(ending, "fight", 1, 3))) {
                return 0;
            }
        }
    }
    if (!(darkside_ending_finish_text(ending, "fight-tail"))) {
        return 0;
    }
    return 1;
}

/* Play the disintegration portion of the Darkside ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_ending_scene_disintegration(Ending *ending, DarksideEndingWork *work)
{

    if (!(darkside_ending_load_background(ending, "mainback.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_voice(ending, 0, ending->dark, "disint.voc") &&
          darkside_ending_load_voice(ending, 1, ending->intro, "explosio.voc") &&
          darkside_ending_load_asset(ending, 17, "sc15.end"))) {
        return 0;
    }
    if (!(darkside_ending_draw_sprite(ending, 17, 0, 52, 84) &&
          darkside_ending_present_scene(ending, "disintegrate-static", 0))) {
        return 0;
    }
    for (work->index = 0; work->index < 21; work->index++) {
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, 17, work->index, 52, 84) &&
              darkside_ending_present_step(ending, "disintegrate", 0, 3))) {
            return 0;
        }
        if (work->index == 4) {
            darkside_ending_play_voice(ending, 0);
        }
        if (work->index == 12) {
            darkside_ending_play_voice(ending, 1);
        }
    }
    return 1;
}

/* Play the first impact portion of the Darkside ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_ending_scene_first_impact(Ending *ending, DarksideEndingWork *work)
{

    darkside_ending_stop_voice(ending);
    if (!(darkside_ending_load_voice(ending, 0, ending->intro, "fail1.voc") &&
          darkside_ending_load_voice(ending, 1, ending->dark, "alien2.voc") &&
          darkside_ending_load_panorama(ending) &&
          darkside_ending_load_asset(ending, 18, "sc16.end") &&
          darkside_ending_load_background(ending, "sc170001.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_asset(ending, 19, "sc17xa.end") &&
          darkside_ending_load_asset(ending, 20, "sc17xb.end") &&
          darkside_ending_select_text(ending, 28, 1))) {
        return 0;
    }
    darkside_ending_restore_panorama(ending, 0);
    if (!(darkside_ending_draw_sprite(ending, 18, 0, 7, 29) &&
          darkside_ending_present_scene(ending, "failure-static", 1))) {
        return 0;
    }
    darkside_ending_play_voice(ending, 0);
    for (work->repeat = 0; work->repeat < 5; work->repeat++) {
        cine_reset(ending->cinematic);
        darkside_ending_restore_panorama(ending, 0);
        if (!(darkside_ending_draw_sprite(ending, 18, work->repeat, 7, 29) &&
              darkside_ending_present_step(ending, "failure-cast", 1, 4))) {
            return 0;
        }
    }
    work->repeat = 5;
    for (work->index = 0; work->index < 320; work->index += 16) {
        cine_reset(ending->cinematic);
        darkside_ending_restore_panorama(ending, work->index);
        work->frame = work->repeat < 12 ? work->repeat++ : 11;
        if (!(darkside_ending_draw_effect(ending, 18, work->frame, 7 - (int)work->index, 29))) {
            return 0;
        }
        if (work->repeat > 10) {
            if (!(darkside_ending_draw_sprite(ending, 18, 12, 273, 70))) {
                return 0;
            }
        }
        if (!(darkside_ending_present_step(ending, "failure-pan", 1, 2))) {
            return 0;
        }
        if (work->index == 48) {
            darkside_ending_play_voice(ending, 1);
        }
    }
    if (!(darkside_ending_load_voice(ending, 0, ending->dark, "thud1.voc"))) {
        return 0;
    }
    cine_restore(ending->cinematic, 0);
    if (!(darkside_ending_present_scene(ending, "failure-impact-static", 0))) {
        return 0;
    }
    for (work->index = 0; work->index < 16; work->index++) {
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, 19 + work->index / 8, work->index % 8, 44, 19) &&
              darkside_ending_present_step(ending, "failure-impact", 0, 3))) {
            return 0;
        }
        if (work->index == 9) {
            darkside_ending_play_voice(ending, 0);
        }
    }
    return 1;
}

/* Play the gas recoil portion of the Darkside ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int darkside_ending_scene_gas_recoil(Ending *ending, DarksideEndingWork *work)
{

    if (!(cine_fade(ending->cinematic, 0, 4, "darkside-ending-gas-out") &&
          darkside_ending_load_background(ending, "sc180001.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_voice(ending, 0, ending->intro, "gascompr.voc"))) {
        return 0;
    }
    for (work->index = 0; work->index < 4; work->index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "sc18%c.end", 'a' + (int)work->index);
        if (!(darkside_ending_load_asset(ending, work->index, work->asset_name))) {
            return 0;
        }
    }
    if (!(darkside_ending_present_scene(ending, "gas-static", 0) &&
          cine_fade(ending->cinematic, 1, 4, "darkside-ending-gas-in"))) {
        return 0;
    }
    for (work->index = 0; work->index < 31; work->index++) {
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, work->index / 8, work->index % 8, 29, 12) &&
              darkside_ending_present_step(ending, "gas", 0, 3))) {
            return 0;
        }
        if (work->index == 2) {
            darkside_ending_play_voice(ending, 0);
        }
    }
    if (!(darkside_ending_load_background(ending, "sc190001.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_asset(ending, 0, "sc19.end") &&
          darkside_ending_present_scene(ending, "recoil-static", 0))) {
        return 0;
    }
    for (work->index = 0; work->index < 28; work->index++) {
        cine_reset(ending->cinematic);
        if (work->index == 20) {
            cine_music_control(ending->cinematic, 207);
        }
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, 0, work->index, 33, 10) &&
              darkside_ending_present_step(ending, "recoil", 0, 3))) {
            return 0;
        }
    }
    cine_music_control(ending->cinematic, 0);
    cine_music_volume(ending->cinematic, 95);
    cine_music_load(ending->cinematic, ending->intro, "sf17.m");
    return 1;
}

/* Play the retaliation portion of the Darkside ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_ending_scene_retaliation(Ending *ending, DarksideEndingWork *work)
{

    if (!(cine_fade(ending->cinematic, 0, 4, "darkside-ending-cast-out") &&
          darkside_ending_load_background(ending, "blank.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_voice(ending, 0, ending->intro, "cast.voc") &&
          darkside_ending_load_asset(ending, 0, "sc20.end") &&
          darkside_ending_present_scene(ending, "cast-static", 0) &&
          cine_fade(ending->cinematic, 1, 4, "darkside-ending-cast-in"))) {
        return 0;
    }
    for (work->repeat = 0; work->repeat < 2; work->repeat++) {
        darkside_ending_play_voice(ending, 0);
        for (work->index = 0; work->index < 9; work->index++) {
            cine_reset(ending->cinematic);
            cine_restore(ending->cinematic, 0);
            if (!(darkside_ending_draw_sprite(ending, 0, work->index, 80, 18) &&
                  darkside_ending_present_step(ending, "cast", 0, 3))) {
                return 0;
            }
        }
    }
    if (!(darkside_ending_load_background(ending, "mainback.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_voice(ending, 0, ending->dark, "alien2.voc") &&
          darkside_ending_load_asset(ending, 0, "sc21.end") &&
          darkside_ending_draw_sprite(ending, 0, 0, 133, 81) &&
          darkside_ending_present_scene(ending, "retaliation-static", 0))) {
        return 0;
    }
    for (work->repeat = 0; work->repeat < 4; work->repeat++) {
        darkside_ending_play_voice(ending, 0);
        for (work->index = work->repeat ? 3 : 0; work->index < 13; work->index++) {
            cine_reset(ending->cinematic);
            cine_restore(ending->cinematic, 0);
            if (!(darkside_ending_draw_sprite(ending, 0, work->index, 133, 81) &&
                  darkside_ending_present_step(ending, "retaliation", 0, 2))) {
                return 0;
            }
        }
    }
    cine_restore(ending->cinematic, 0);
    if (!(darkside_ending_draw_sprite(ending, 0, 0, 133, 81) &&
          darkside_ending_present_scene(ending, "retaliation-final", 0))) {
        return 0;
    }
    return 1;
}

/* Play the laughter portion of the Darkside ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int darkside_ending_scene_laughter(Ending *ending, DarksideEndingWork *work)
{

    if (!(darkside_ending_load_voice(ending, 0, ending->dark, "laff1.voc"))) {
        return 0;
    }
    darkside_ending_play_voice(ending, 0);
    if (!(darkside_ending_load_panorama(ending) &&
          darkside_ending_load_voice(ending, 1, ending->intro, "gascompr.voc") &&
          darkside_ending_load_asset(ending, 18, "sc22.end") &&
          darkside_ending_load_background(ending, "sc230001.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_asset(ending, 19, "sc23a.end") &&
          darkside_ending_load_asset(ending, 20, "sc23b.end"))) {
        return 0;
    }
    darkside_ending_restore_panorama(ending, 320);
    while (cine_sound_busy(ending->cinematic)) {
        cine_reset(ending->cinematic);
        if (!(cine_wait_fire_ticks(ending->cinematic, 1))) {
            return 0;
        }
    }
    if (!(darkside_ending_draw_sprite(ending, 18, 0, 156, 28) &&
          darkside_ending_present_scene(ending, "reverse-cast-static", 0) &&
          darkside_ending_load_voice(ending, 0, ending->intro, "cast.voc"))) {
        return 0;
    }
    darkside_ending_play_voice(ending, 0);
    for (work->repeat = 0; work->repeat < 5; work->repeat++) {
        cine_reset(ending->cinematic);
        darkside_ending_restore_panorama(ending, 320);
        if (!(darkside_ending_draw_sprite(ending, 18, work->repeat, 156, 28) &&
              darkside_ending_present_step(ending, "reverse-cast", 0, 4))) {
            return 0;
        }
    }
    darkside_ending_play_voice(ending, 1);
    work->frame = 5;
    for (work->index = 320, work->repeat = 0; work->index > 0;
         work->index -= 16, work->repeat += 16) {
        cine_reset(ending->cinematic);
        darkside_ending_restore_panorama(ending, work->index);
        if (!(darkside_ending_draw_effect(ending, 18, work->frame < 8 ? work->frame++ : 7,
                                          156 + (int)work->repeat, 28))) {
            return 0;
        }
        if (work->frame > 7) {
            if (!(darkside_ending_draw_sprite(ending, 18, 8, 136, 64))) {
                return 0;
            }
        }
        if (!(darkside_ending_present_step(ending, "reverse-pan", 0, 2))) {
            return 0;
        }
    }
    cine_restore(ending->cinematic, 0);
    if (!(darkside_ending_load_voice(ending, 0, ending->intro, "padspell.voc") &&
          darkside_ending_load_voice(ending, 1, ending->intro, "cast.voc") &&
          darkside_ending_present_scene(ending, "gas-response-static", 0))) {
        return 0;
    }
    darkside_ending_play_voice(ending, 1);
    for (work->index = 0; work->index < 19; work->index++) {
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, 19 + work->index / 12, work->index % 12, 72, 4) &&
              darkside_ending_present_step(ending, "gas-response", 0, 3))) {
            return 0;
        }
        if (work->index == 5) {
            darkside_ending_play_voice(ending, 0);
        }
        if (work->index == 11) {
            cine_music(ending->cinematic, 9);
        }
    }
    cine_restore(ending->cinematic, 0);
    if (!(darkside_ending_draw_sprite(ending, 19, 0, 72, 4) &&
          darkside_ending_present_scene(ending, "gas-response-final", 0))) {
        return 0;
    }
    return 1;
}

/* Play the counterattack portion of the Darkside ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_ending_scene_counterattack(Ending *ending, DarksideEndingWork *work)
{

    if (!(cine_fade(ending->cinematic, 0, 4, "darkside-ending-counterspell-out") &&
          darkside_ending_load_background(ending, "mainback.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_voice(ending, 0, ending->intro, "cast.voc") &&
          darkside_ending_load_voice(ending, 1, ending->dark, "thud1.voc") &&
          darkside_ending_load_voice(ending, 2, ending->intro, "padspell.voc") &&
          darkside_ending_load_voice(ending, 3, ending->intro, "click.voc") &&
          darkside_ending_load_asset(ending, 0, "sc24a.end") &&
          darkside_ending_load_asset(ending, 1, "sc24b.end"))) {
        return 0;
    }
    if (!(darkside_ending_draw_sprite(ending, 0, 0, 148, 0) &&
          darkside_ending_present_scene(ending, "counterspell-static", 0) &&
          cine_fade(ending->cinematic, 1, 4, "darkside-ending-counterspell-in"))) {
        return 0;
    }
    darkside_ending_play_voice(ending, 0);
    for (work->index = 0; work->index < 30; work->index++) {
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, work->index / 15, work->index % 15, 148, 0) &&
              darkside_ending_present_step(ending, "counterspell", 0, 3))) {
            return 0;
        }
        if (work->index == 6) {
            darkside_ending_play_voice(ending, 2);
        }
        if (work->index == 11 || work->index == 14) {
            darkside_ending_play_voice(ending, 3);
        }
        if (work->index == 17) {
            darkside_ending_play_voice(ending, 1);
        }
    }
    return 1;
}

/* Play the admit closeup portion of the Darkside ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_ending_scene_admit_closeup(Ending *ending, DarksideEndingWork *work)
{

    if (!(darkside_ending_load_background(ending, "sc250001.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_voice(ending, 0, ending->intro, "admit2.voc") &&
          darkside_ending_load_asset(ending, 0, "sc25a.end") &&
          darkside_ending_load_asset(ending, 1, "sc25b.end") &&
          darkside_ending_select_text(ending, 29, 1))) {
        return 0;
    }
    if (!(darkside_ending_draw_sprite(ending, 0, 0, 27, 30) &&
          darkside_ending_present_scene(ending, "admit-static", 0))) {
        return 0;
    }
    for (work->repeat = 0; work->repeat < 2; work->repeat++) {
        for (work->index = 0; work->index < 9; work->index++) {
            if (work->index == 2 && !work->repeat) {
                darkside_ending_play_voice(ending, 0);
            }
            cine_reset(ending->cinematic);
            cine_restore(ending->cinematic, 0);
            if (!(darkside_ending_draw_sprite(ending, work->index / 10, work->index % 10, 27, 30) &&
                  darkside_ending_present_step(ending, "admit", 1, 3))) {
                return 0;
            }
        }
    }
    if (!(darkside_ending_finish_text(ending, "admit-tail"))) {
        return 0;
    }
    return 1;
}

/* Play the reply closeup portion of the Darkside ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_ending_scene_reply_closeup(Ending *ending, DarksideEndingWork *work)
{

    if (!(darkside_ending_load_background(ending, "sc260001.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_voice(ending, 0, ending->intro, "ido2.voc") &&
          darkside_ending_select_text(ending, 30, 1))) {
        return 0;
    }
    for (work->index = 0; work->index < 4; work->index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "sc26%c.end", 'a' + (int)work->index);
        if (!(darkside_ending_load_asset(ending, work->index, work->asset_name))) {
            return 0;
        }
    }
    if (!(darkside_ending_present_scene(ending, "i-do-static", 1))) {
        return 0;
    }
    darkside_ending_play_voice(ending, 0);
    work->index = 0;
    do {
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, work->index / 8, work->index % 8, 58, 25) &&
              darkside_ending_present_step(ending, "i-do", 1, 2))) {
            return 0;
        }
        work->index = (work->index + 1) % 32;
        if (!work->index) {
            work->index = 22;
        }
    } while (cine_sound_busy(ending->cinematic) || ending->text.counter);
    return 1;
}

/* Play the question closeup portion of the Darkside ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_ending_scene_question_closeup(Ending *ending, DarksideEndingWork *work)
{

    while (cine_sound_busy(ending->cinematic)) {
        cine_reset(ending->cinematic);
        if (!(cine_wait_fire_ticks(ending->cinematic, 1))) {
            return 0;
        }
    }
    if (!(darkside_ending_load_background(ending, "sc270001.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_voice(ending, 0, ending->intro, "what3.voc") &&
          darkside_ending_select_text(ending, 31, 1) &&
          darkside_ending_load_asset(ending, 0, "sc27.end") &&
          darkside_ending_present_scene(ending, "what-static", 1))) {
        return 0;
    }
    for (work->index = 0; work->index < 12; work->index++) {
        if (work->index == 1) {
            darkside_ending_play_voice(ending, 0);
        }
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, 0, work->index, 65, 22) &&
              darkside_ending_present_step(ending, "what", 1, 3))) {
            return 0;
        }
    }
    if (!(darkside_ending_finish_text(ending, "what-tail"))) {
        return 0;
    }
    return 1;
}

/* Play the final spell portion of the Darkside ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_ending_scene_final_spell(Ending *ending, DarksideEndingWork *work)
{

    cine_music_load(ending->cinematic, ending->intro, "dungeon2.m");
    if (!(cine_fade(ending->cinematic, 0, 4, "darkside-ending-final-spell-out") &&
          darkside_ending_load_background(ending, "mainback.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_voice(ending, 0, ending->intro, "explosio.voc") &&
          darkside_ending_load_voice(ending, 1, ending->intro, "rumble.voc") &&
          darkside_ending_load_voice(ending, 2, ending->dark, "airmon.voc"))) {
        return 0;
    }
    for (work->index = 0; work->index < 11; work->index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "sc%c28.end", 'a' + (int)work->index);
        if (!(darkside_ending_load_asset(ending, work->index, work->asset_name))) {
            return 0;
        }
    }
    if (!(darkside_ending_draw_sprite(ending, 0, 0, 74, 0) &&
          darkside_ending_present_scene(ending, "final-spell-static", 0) &&
          cine_fade(ending->cinematic, 1, 4, "darkside-ending-final-spell-in"))) {
        return 0;
    }
    for (work->index = 0; work->index < 44; work->index++) {
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (!(darkside_ending_draw_sprite(ending, work->index / 4, work->index % 4, 74, 0) &&
              darkside_ending_present_step(ending, "final-spell", 0, 3))) {
            return 0;
        }
        if (work->index == 0 || work->index == 10 || work->index == 18) {
            darkside_ending_play_voice(ending, 1);
        }
        if (work->index == 27) {
            darkside_ending_play_voice(ending, 2);
        }
        if (work->index == 38) {
            darkside_ending_play_voice(ending, 0);
        }
    }
    return 1;
}

/* Play the collapse and score portion of the Darkside ending. Borrows the cinematic and
 * supplied scene records; updates framebuffer, timing and mutable work state, with loaded
 * assets retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0
 * on a failed operation or input stop.
 */
static int darkside_ending_scene_collapse_and_score(Ending *ending, DarksideEndingWork *work,
                                                    const EndingPayload *payload)
{

    if (!(cine_fade(ending->cinematic, 0, 4, "darkside-ending-collapse-out") &&
          darkside_ending_load_background(ending, "sc290001.raw"))) {
        return 0;
    }
    darkside_ending_save_background(ending, 0);
    if (!(darkside_ending_load_voice(ending, 1, ending->intro, "crash.voc"))) {
        return 0;
    }
    for (work->index = 0; work->index < 6; work->index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "sc29%c.end", 'a' + (int)work->index);
        if (!(darkside_ending_load_asset(ending, work->index, work->asset_name))) {
            return 0;
        }
    }
    if (!(darkside_ending_present_scene(ending, "collapse-static", 0) &&
          cine_fade(ending->cinematic, 1, 4, "darkside-ending-collapse-in"))) {
        return 0;
    }
    for (work->index = 0; work->index < 54; work->index++) {
        cine_reset(ending->cinematic);
        cine_restore(ending->cinematic, 0);
        if (work->index == 40) {
            cine_music_control(ending->cinematic, 207);
        }
        if (!(darkside_ending_draw_sprite(ending, work->index / 10, work->index % 10, 92, 10) &&
              darkside_ending_present_step(ending, "collapse", 0, 3))) {
            return 0;
        }
        if (work->index == 1 || work->index == 5 || work->index == 11 || work->index == 19 ||
            work->index == 34 || work->index == 35) {
            darkside_ending_play_voice(ending, 0);
        }
    }
    darkside_ending_play_voice(ending, 1);
    for (work->repeat = 0; work->repeat < 4; work->repeat++) {
        for (work->index = 49; work->index < 54; work->index++) {
            cine_reset(ending->cinematic);
            cine_restore(ending->cinematic, 0);
            if (!(darkside_ending_draw_sprite(ending, work->index / 10, work->index % 10, 92, 10) &&
                  darkside_ending_present_step(ending, "collapse-tail", 0, 3))) {
                return 0;
            }
        }
    }
    if (!(cine_fade(ending->cinematic, 0, 4, "darkside-ending-score-out"))) {
        return 0;
    }
    cine_music_control(ending->cinematic, 0);
    cine_music_volume(ending->cinematic, 95);
    cine_music_load(ending->cinematic, ending->intro, "outday3.m");
    {
        char score[2048];
        const char *pages[2];
        if (!(ending_score_text(payload, score, sizeof(score)))) {
            return 0;
        }
        pages[0] = score;
        pages[1] = ending_castleview_text();
        if (!(ending_modal_play_aligned(ending->cinematic, ending->intro, pages, 2,
                                        "darkside-ending-modal", ending->cinematic->subtitles))) {
            return 0;
        }
    }
    /* Darkside skips the hand imagery and its optional fade. Common
     * cleanup still runs before the final handoff boundary.
     */
    cine_record(ending->cinematic, "darkside-ending-source-return-1ccae");
    return 1;
}

/* Play the dialogue, battle, collapse and score portion of the Darkside ending. Borrows the
 * cinematic, both archives and payload; owns and releases local scene assets. Returns 1 on
 * completion or 0 on load, draw, present, clock or input failure.
 */
int darkside_ending_later(Cinematic *cinematic, CC *intro, CC *dark, const EndingPayload *payload)
{
    Ending ending = {0};
    DarksideEndingWork work;
    unsigned i;
    int ok = 0;
    ending.cinematic = cinematic;
    ending.intro = intro;
    ending.dark = dark;
    if (!(cine_load(intro, "special.bin", &ending.pool) && cine_load(intro, "fnt", &ending.font) &&
          cine_load(intro, "box.vga", &ending.box))) {
        goto done;
    }
    if (!darkside_ending_scene_door(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_transport(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_materialization(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_arrival(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_return_dialogue(&ending)) {
        goto done;
    }
    if (!darkside_ending_scene_spell_lever(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_ready_closeup(&ending)) {
        goto done;
    }
    if (!darkside_ending_scene_fight(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_disintegration(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_first_impact(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_gas_recoil(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_retaliation(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_laughter(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_counterattack(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_admit_closeup(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_reply_closeup(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_question_closeup(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_final_spell(&ending, &work)) {
        goto done;
    }
    if (!darkside_ending_scene_collapse_and_score(&ending, &work, payload)) {
        goto done;
    }
    ok = 1;
done:
    for (i = 0; i < 40; i++) {
        free(ending.art[i].data);
    }
    for (i = 0; i < 4; i++) {
        voice_free(&ending.sound[i]);
    }
    free(ending.pool.data);
    free(ending.font.data);
    free(ending.box.data);
    free(ending.pan[0].data);
    free(ending.pan[1].data);
    return ok;
}
