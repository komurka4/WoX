/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Continue the Darkside introduction through ball dialogue, flight, bird, title and fire
 * scenes. darkside_later borrows earlier ball and caption resources and uses runtime until the
 * common cleanup boundary.
 */
#include "darkside_later.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    Voice v;
    const char *name;
} Sound;

typedef struct {
    Cinematic *cinematic;
    Subtitles *s;
    const Blob *pool;
    const Blob *font;
    const Blob *box;
    unsigned serial;
} Later;

/* Read and decode the named sound into the supplied slot and retain its cue name. Borrows
 * archive and name; the name must remain valid while the slot is used. Frees temporary archive
 * bytes; returns 1 on success or 0 on read or decode failure. The caller releases decoded
 * voice data.
 */
static int darkside_later_load_sound(CC *archive, const char *name, Sound *s)
{
    Blob b = {0};
    int ok;
    voice_free(&s->v);
    s->name = name;
    if (!cine_load(archive, name, &b)) {
        return 0;
    }
    ok = player_voc_decode(&b, &s->v);
    free(b.data);
    return ok;
}

/* Play the borrowed sound slot through the scene's cinematic and preserve its cue label. Does
 * not transfer voice ownership; output errors follow cine_voice's diagnostic behavior.
 */
static void darkside_later_play_sound(Later *later, const Sound *s)
{
    if (!later->cinematic->subtitles ||
        later->cinematic->approximate) {
        cine_voice(later->cinematic, &s->v, s->name);
    }
}

/* Sample the secondary clock once and advance at most one caption
 * character. Drawing and catalogue diagnostics must not advance it again.
 */
static int darkside_later_update_text(Later *later)
{
    if (later->cinematic->approximate) {
        uint64_t elapsed = cine_secondary_elapsed(later->cinematic);
        if ((int16_t)(uint16_t)elapsed > 1) {
            unsigned start;
            unsigned n;
            if (!later->s->length) {
                return 0;
            }
            later->s->counter = (later->s->counter + 1) % later->s->length;
            start = later->s->counter > 40 ? later->s->counter - 40 : 0;
            n = later->s->counter - start;
            memcpy(later->s->window, later->s->text + start, n);
            later->s->window[n] = 0;
            if (later->s->counter == SUBTITLE_COUNTER_ALIAS_CHARACTER) {
                later->s->counter &= 0xff00u;
            }
            return cine_secondary_reset(later->cinematic);
        }
    } else {
        subtitles_update(later->s, later->cinematic->now);
    }
    return 1;
}

/* Update optional caption state and present the current crystal-ball composition under the
 * borrowed scene label. Borrows and updates scene state; returns 1 on success or 0 on clock,
 * text, draw, retrace or present failure.
 */
static int darkside_later_present(Later *later, const char *scene, int text)
{
    char n[96];
    Cinematic *cinematic = later->cinematic;
    snprintf(n, sizeof(n), "%s-%04u", scene, later->serial++);
    if (cinematic->subtitles && text) {
        if (!darkside_later_update_text(later)) {
            return 0;
        }
        if (!cine_draw_at(cinematic, later->box, 0, 36, 189, 0) ||
            !text_draw_subtitle(later->font, later->s->window, cinematic->pixels) ||
            !cine_retrace(cinematic)) {
            return 0;
        }
    } else if (!cinematic->subtitles) {
        later->s->counter = 0;
    }
    return cine_present(cinematic, n, n);
}

/* Select a caption pool entry and initialize its counter, resetting the secondary timer in
 * approximate mode. Borrows and updates scene text state; returns 1 on success or 0 on
 * selection or clock failure.
 */
static int darkside_later_select_text(Later *later, unsigned entry, unsigned counter)
{
    if (later->cinematic->subtitles &&
        !subtitles_select(later->s, later->pool, entry, counter, later->cinematic->now)) {
        return 0;
    }
    return !later->cinematic->approximate || cine_secondary_reset(later->cinematic);
}

/* Present one crystal-ball frame, poll input and wait the requested primary ticks. Borrows
 * scene state and label; returns 1 on success or 0 on a failed present, wait or input stop.
 */
static int darkside_later_step(Later *later, const char *scene, int text, unsigned ticks)
{
    return darkside_later_present(later, scene, text) && cine_poll(later->cinematic) &&
           cine_wait_ticks(later->cinematic, ticks, 0);
}

/* Read the named full-screen background into cinematic pixels. Borrows the archive and name
 * and frees temporary bytes before return. Returns 1 on success or 0 on read or size failure.
 */
static int darkside_later_replace_raw(CC *archive, const char *name, Cinematic *cinematic)
{
    Blob b = {0};
    if (!cine_load(archive, name, &b)) {
        return 0;
    }
    if (b.size != XEEN_PIXELS) {
        free(b.data);
        return 0;
    }
    memcpy(cinematic->background[0], b.data, XEEN_PIXELS);
    memcpy(cinematic->pixels, b.data, XEEN_PIXELS);
    free(b.data);
    return 1;
}

/* Read the named palette into cinematic palette storage. Borrows the archive and name and
 * frees temporary bytes before return. Returns 1 on success or 0 on read or size failure.
 */
static int darkside_later_replace_palette(CC *archive, const char *name, Cinematic *cinematic)
{
    Blob b = {0};
    if (!cine_load(archive, name, &b)) {
        return 0;
    }
    if (b.size != XEEN_PALETTE_BYTES) {
        free(b.data);
        return 0;
    }
    memcpy(cinematic->palette, b.data, XEEN_PALETTE_BYTES);
    free(b.data);
    return 1;
}

/* Choose a dialogue frame from zero through three using the changing accumulator byte and time
 * words. Borrows and updates the cinematic; returns zero on clock failure as well as a valid
 * zero choice, with failure recorded in the cinematic.
 */
static unsigned darkside_later_select_frame(Cinematic *cinematic, unsigned p)
{
    uint16_t time_word_high;
    uint16_t time_word_low;
    static const unsigned selector_bytes[5] = {0xab, 0xe0, 0xe0, 0xab, 0xa4};
    if (cinematic->approximate) {
        if (!cine_dos_time(cinematic, (uint16_t)(0x2c00 | selector_bytes[p % 5]), &time_word_high,
                           &time_word_low)) {
            return 0;
        }
    } else {
        queen_clock(cinematic->clock_origin, cinematic->now - cinematic->clock_start,
                    &time_word_high, &time_word_low);
    }
    return queen_select_range(&cinematic->selector, (uint16_t)(0x2c00 | selector_bytes[p % 5]),
                              time_word_high, time_word_low, 0, 3);
}

static const int end_x0[12] = {0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55};
static const int end_x1[12] = {160, 155, 150, 145, 140, 135, 130, 125, 120, 115, 110, 105};
static const int end_y[12] = {0, 5, 10, 15, 20, 25, 30, 36, 44, 51, 57, 62};
static const int dragon_y[32] = {-3, -3, -3, -3, -3, -3, -3, -3, -1, 0,  0,  0,  0,  0,  0,  0,
                                 -1, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3};
static const int dragon_frame[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 2, 1,
                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const int claw5_x[32] = {146, 145, 143, 141, 141, 141, 141, 141, 141, 141, 141,
                                142, 143, 144, 145, 146, 146, 146, 146, 146, 146, 146,
                                146, 146, 146, 146, 146, 146, 146, 146, 146, 146};
static const int claw5_y[32] = {143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143,
                                143, 143, 143, 143, 143, 143, 143, 144, 145, 145, 145,
                                145, 145, 145, 145, 145, 144, 143, 143, 143, 143};
static const int dialogue_claw_x[32] = {223, 222, 220, 219, 219, 219, 219, 219, 219, 219, 219,
                                        219, 219, 219, 219, 219, 219, 219, 220, 220, 220, 220,
                                        220, 220, 220, 220, 220, 220, 220, 220, 220, 221};
static const int dialogue_claw_y[32] = {116, 116, 116, 117, 117, 117, 117, 117, 118, 118, 118,
                                        118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118,
                                        118, 118, 118, 118, 118, 118, 118, 118, 118, 118};
static const int sweep_claw_x[32] = {223, 208, 187, 158, 129, 104, 85,  70,  61,  53, 49,
                                     46,  46,  49,  53,  56,  61,  67,  72,  82,  91, 103,
                                     117, 130, 142, 156, 170, 184, 199, 210, 221, 227};
static const int sweep_claw_y[32] = {116, 111, 105, 103, 101, 96,  93,  92,  92,  95,  98,
                                     100, 104, 107, 107, 105, 102, 100, 98,  99,  101, 104,
                                     107, 109, 110, 111, 114, 115, 115, 116, 116, 118};

/* Compose crystal-ball, claw and dragon art for the requested animation position and sweep
 * variant. Borrows cinematic and art storage; callers supply an in-range position. Returns 1
 * on success or 0 on draw failure, with partial pixels possible.
 */
static int darkside_later_draw_ball(Cinematic *cinematic, Blob b[9], unsigned p, int sweep)
{
    cine_restore(cinematic, 0);
    return (!p || cine_draw_at(cinematic, &b[0], p - 1, 0, 0, 0)) &&
           cine_draw_effect(cinematic, &b[7], 5, claw5_x[p], claw5_y[p], 0x0800) &&
           cine_draw_at(cinematic, &b[7], 6, 149, 184, 0) &&
           cine_draw_effect(cinematic, &b[1], (unsigned)dragon_frame[p], 139, dragon_y[p],
                            0x0800) &&
           cine_draw_effect(cinematic, &b[7], p % 5, sweep ? sweep_claw_x[p] : dialogue_claw_x[p],
                            sweep ? sweep_claw_y[p] : dialogue_claw_y[p], 0x0800);
}

/* Draw the requested character slot and frame into the crystal-ball composition. Borrows
 * cinematic and art storage; callers supply valid indices. Returns the draw status, with
 * partial output possible on failure.
 */
static int darkside_later_draw_character(Cinematic *cinematic, Blob b[9], unsigned which,
                                         unsigned f)
{
    return cine_draw_at(cinematic, &b[which], f, 9, 57, 0);
}

/* Choose and draw a dialogue portrait while preserving the selector and saved-background
 * paths. Borrows the scene state, art and labels and writes cinematic pixels. Returns 1 on
 * success or 0 on clock, selector-state or draw failure.
 */
static int darkside_later_draw_selected_character(Later *later, Blob b[9], unsigned which,
                                                  unsigned p, int fizzle_frame, const char *scene,
                                                  const char *site)
{
    Cinematic *cinematic = later->cinematic;
    unsigned chosen = darkside_later_select_frame(cinematic, p);
    if (cinematic->events) {
        fprintf(cinematic->events, "selector,%s,%llu,%u\n", site,
                (unsigned long long)cinematic->now, chosen);
    }
    if (cinematic->approximate) {
        cine_coverage_state(cinematic, 0x29dac, "darkside-intro-selector", chosen, 3);
    }
    if (cinematic->dump && getenv("XEEN_DUMP_QUEEN_CANDIDATES")) {
        uint8_t before[XEEN_PIXELS];
        unsigned candidate;
        memcpy(before, cinematic->pixels, XEEN_PIXELS);
        if (cinematic->subtitles && !cinematic->approximate) {
            subtitles_update(later->s, cinematic->now);
        }
        for (candidate = 0; candidate < 4; candidate++) {
            char n[128];
            memcpy(cinematic->pixels, before, XEEN_PIXELS);
            if (!darkside_later_draw_character(cinematic, b, which, candidate) ||
                (fizzle_frame >= 0 &&
                 !darkside_later_draw_character(cinematic, b, 6, (unsigned)fizzle_frame))) {
                return 0;
            }
            if (cinematic->subtitles &&
                (!cine_draw_at(cinematic, later->box, 0, 36, 189, 0) ||
                 !text_draw_subtitle(later->font, later->s->window, cinematic->pixels))) {
                return 0;
            }
            snprintf(n, sizeof(n), "%s-%04u-candidate%u", scene, later->serial, candidate);
            if (!cine_present(cinematic, n, NULL)) {
                return 0;
            }
        }
        memcpy(cinematic->pixels, before, XEEN_PIXELS);
    }
    return darkside_later_draw_character(cinematic, b, which, chosen);
}

/* Animate the crystal ball while the supplied speech remains busy. Borrows the scene state,
 * art, sound and label and updates the caller's ball position. Returns 1 when speech ends or 0
 * on a failed draw, present, wait or input stop.
 */
static int darkside_later_wait_ball(Later *later, Blob b[9], unsigned *position, unsigned who,
                                    const char *scene)
{
    Cinematic *cinematic = later->cinematic;
    do {
        unsigned p = *position;
        cine_reset(cinematic);
        if (!darkside_later_draw_ball(cinematic, b, p, 0) ||
            (who < 9 && !darkside_later_draw_character(cinematic, b, who, 0))) {
            return 0;
        }
        *position = (p + 1) % 32;
        if (!darkside_later_step(later, scene, 1, 3)) {
            return 0;
        }
    } while (cine_sound_busy(cinematic) || later->s->counter);

    return 1;
}

/* Animate a crystal-ball character reveal or disappearance with the requested cue, onset and
 * frame selection. Borrows all art, sound and scene inputs and updates the caller's ball
 * position. Returns 1 on completion or 0 on a failed operation or input stop.
 */
static int darkside_later_fizzle(Later *later, Blob b[9], unsigned *position, unsigned who,
                                 int reveal, unsigned onset, Sound *cue, int random,
                                 const char *scene, int text)
{
    Cinematic *cinematic = later->cinematic;
    int f;
    int started = 0;
    for (f = reveal ? 0 : 10; reveal ? f < 11 : f >= 0; f += reveal ? 1 : -1) {
        unsigned p = *position;
        cine_reset(cinematic);
        if (!darkside_later_draw_ball(cinematic, b, p, 0)) {
            return 0;
        }
        if (!reveal && !cine_sound_busy(cinematic) && !started && cue) {
            darkside_later_play_sound(later, cue);
            started = 1;
        }
        if (reveal ? f > 3 : f > (random ? 3 : 4)) {
            if (random) {
                if (!darkside_later_draw_selected_character(later, b, who, p, f, scene, "19016")) {
                    return 0;
                }
            } else if (!darkside_later_draw_character(cinematic, b, who, 0)) {
                return 0;
            }
        }
        if (reveal && (unsigned)f == onset && cue) {
            darkside_later_play_sound(later, cue);
        }
        if (!darkside_later_draw_character(cinematic, b, 6, (unsigned)f)) {
            return 0;
        }
        *position = (p + 1) % 32;
        if (!darkside_later_step(later, scene, text && (!reveal || f > 6), 3)) {
            return 0;
        }
    }
    return 1;
}

typedef struct {
    Later later;
    Sound speech;
    Sound dialogue[3];
    Sound effects[3];
    Blob art[11];
    Blob drops[4];
    CC dark;
} LaterAssets;

typedef struct {
    char path[4096];
    char asset_name[64];
    unsigned asset_index;
    unsigned frame;
    unsigned voice_chain;
    unsigned mouth_frame;
    unsigned position;
    int result;
} LaterWork;

/* Play the open dialogue portion of the Darkside introduction. Borrows the cinematic and
 * supplied scene records; updates framebuffer, timing and mutable work state, with loaded
 * assets retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0
 * on a failed operation or input stop.
 */
static int darkside_later_open_dialogue(Cinematic *cinematic, CC *archive, Blob ball[9],
                                        LaterAssets *assets, LaterWork *work)
{

    if (snprintf(work->path, sizeof(work->path), "%s/DARK.CC", cinematic->data) >=
            (int)sizeof(work->path) ||
        !cc_open(&assets->dark, work->path)) {
        return 0;
    }
    /* Start the current voice before replacing its slot for later starts. */
    if (!darkside_later_load_sound(archive, "pharoh4.voc", &assets->speech)) {
        return 0;
    }
    darkside_later_play_sound(&assets->later, &assets->speech);
    if (!darkside_later_load_sound(archive, "dark3.voc", &assets->speech) ||
        !darkside_later_load_sound(archive, "pharoh5a.voc", &assets->dialogue[1]) ||
        !darkside_later_load_sound(archive, "pharoh5b.voc", &assets->dialogue[2]) ||
        !darkside_later_select_text(&assets->later, 11, 1)) {
        return 0;
    }
    if (!darkside_later_wait_ball(&assets->later, ball, &work->position, 4, "ball-pharaoh4")) {
        work->result = cinematic->result;
        return 0;
    }

    return 1;
}

/* Play the darklord portion of the Darkside introduction. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_later_darklord(Cinematic *cinematic, Blob ball[9], Subtitles *subtitle,
                                   LaterAssets *assets, LaterWork *work)
{

    /* Start the current voice before replacing its slot with laughter. */
    darkside_later_play_sound(&assets->later, &assets->speech);
    if (!darkside_later_load_sound(&assets->dark, "laff1.voc", &assets->speech) ||
        !darkside_later_select_text(&assets->later, 12, 1)) {
        return 0;
    }
    work->voice_chain = 1;
    do {
        unsigned p = work->position;
        cine_reset(cinematic);
        if (!darkside_later_draw_ball(cinematic, ball, p, 0)) {
            return 0;
        }
        if (work->voice_chain == 1) {
            if (!darkside_later_draw_selected_character(&assets->later, ball, 2, p, -1,
                                                        "ball-darklord", "18e56")) {
                return 0;
            }
        } else if (!darkside_later_draw_character(cinematic, ball, 2, 0)) {
            return 0;
        }
        work->position = (p + 1) % 32;
        if (!darkside_later_step(&assets->later, "ball-darklord", 1, 3)) {
            work->result = cinematic->result;
            return 0;
        }
        if (!cine_sound_busy(cinematic)) {
            if (work->voice_chain < 3) {
                darkside_later_play_sound(&assets->later, &assets->dialogue[work->voice_chain]);
            } else {
                darkside_later_play_sound(&assets->later, &assets->speech);
            }
            work->voice_chain++;
        }
    } while (work->voice_chain < 4 || subtitle->counter);
    cine_music(cinematic, 42);
    if (!darkside_later_select_text(&assets->later, 13, 1)) {
        return 0;
    }
    if (!darkside_later_fizzle(&assets->later, ball, &work->position, 2, 0, 0, &assets->speech, 1,
                               "ball-darklord-out", 1)) {
        work->result = cinematic->result;
        return 0;
    }
    cine_music(cinematic, 0);
    if (!darkside_later_wait_ball(&assets->later, ball, &work->position, 9, "ball-darklord-tail")) {
        work->result = cinematic->result;
        return 0;
    }

    return 1;
}

/* Play the goon portion of the Darkside introduction. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int darkside_later_goon(Cinematic *cinematic, CC *archive, Blob ball[9], Subtitles *subtitle,
                               LaterAssets *assets, LaterWork *work)
{

    if (!darkside_later_load_sound(archive, "pharoh6w.voc", &assets->speech) ||
        !darkside_later_load_sound(archive, "alamar1.voc", &assets->dialogue[0]) ||
        !darkside_later_load_sound(archive, "pharoh7t.voc", &assets->dialogue[1])) {
        return 0;
    }
    /* Approximate stops the current voice explicitly; legacy preserves its
     * diagnostic voice lifetime.
     */
    if (cinematic->approximate) {
        cine_stop_voice(cinematic);
    } else {
        cinematic->voice_end = cinematic->now;
    }
    for (work->frame = 0; work->frame < 32; work->frame++) {
        cine_reset(cinematic);
        if (!darkside_later_draw_ball(cinematic, ball, work->frame, 1) ||
            !darkside_later_step(&assets->later, "ball-goon-transition", 0, 3)) {
            work->result = cinematic->result;
            return 0;
        }
    }
    if (!darkside_later_select_text(&assets->later, 14, 1)) {
        return 0;
    }
    cine_music(cinematic, 42);
    work->position = 0;
    if (!darkside_later_fizzle(&assets->later, ball, &work->position, 3, 1, 6, &assets->speech, 0,
                               "ball-goon-in", 1)) {
        work->result = cinematic->result;
        return 0;
    }
    work->voice_chain = 0;
    work->mouth_frame = 0;
    do {
        unsigned p = work->position;
        cine_reset(cinematic);
        if (!darkside_later_draw_ball(cinematic, ball, p, 0)) {
            return 0;
        }
        if (!cine_sound_busy(cinematic) && !work->voice_chain && !subtitle->counter) {
            if (!darkside_later_select_text(&assets->later, 15, 1)) {
                return 0;
            }
            darkside_later_play_sound(&assets->later, &assets->dialogue[0]);
            work->voice_chain = 1;
            cine_music(cinematic, 0);
        }
        if (!darkside_later_draw_character(cinematic, ball, 3,
                                           work->voice_chain ? work->mouth_frame : 0)) {
            return 0;
        }
        work->mouth_frame = (work->mouth_frame + 1) % 17;
        work->position = (p + 1) % 32;
        if (!darkside_later_step(&assets->later, "ball-goon", 1, 3)) {
            work->result = cinematic->result;
            return 0;
        }
    } while (!work->voice_chain || cine_sound_busy(cinematic) || subtitle->counter);
    if (!darkside_later_load_sound(archive, "pharoh8.voc", &assets->speech) ||
        !darkside_later_select_text(&assets->later, 16, 1)) {
        return 0;
    }
    cine_music(cinematic, 42);
    if (!darkside_later_fizzle(&assets->later, ball, &work->position, 3, 0, 0, &assets->dialogue[1],
                               0, "ball-goon-out", 1)) {
        work->result = cinematic->result;
        return 0;
    }
    cine_music(cinematic, 0);

    return 1;
}

/* Play the wizard portion of the Darkside introduction. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_later_wizard(Cinematic *cinematic, CC *archive, Blob ball[9],
                                 Subtitles *subtitle, LaterAssets *assets, LaterWork *work)
{

    if (!darkside_later_load_sound(archive, "ellinger.voc", &assets->dialogue[0])) {
        return 0;
    }
    for (work->frame = 0; work->frame < 32; work->frame++) {
        cine_reset(cinematic);
        if (!darkside_later_draw_ball(cinematic, ball, work->frame, 1) ||
            !darkside_later_step(&assets->later, "ball-wizard-transition", subtitle->counter != 0,
                                 3)) {
            work->result = cinematic->result;
            return 0;
        }
    }
    if (!darkside_later_load_sound(archive, "pharoh9a.voc", &assets->dialogue[1]) ||
        !darkside_later_load_sound(archive, "pharoh9b.voc", &assets->dialogue[2]) ||
        !darkside_later_select_text(&assets->later, 17, 1)) {
        return 0;
    }
    cine_music(cinematic, 42);
    work->position = 0;
    if (!darkside_later_fizzle(&assets->later, ball, &work->position, 5, 1, 5, &assets->speech, 0,
                               "ball-wizard-in", 1)) {
        work->result = cinematic->result;
        return 0;
    }
    work->voice_chain = 0;
    do {
        unsigned p = work->position;
        cine_reset(cinematic);
        if (!darkside_later_draw_ball(cinematic, ball, p, 0)) {
            return 0;
        }
        if (!cine_sound_busy(cinematic) && work->voice_chain < 2 && !subtitle->counter) {
            if (!work->voice_chain) {
                if (!darkside_later_select_text(&assets->later, 18, 1)) {
                    return 0;
                }
            } else {
                subtitle->counter = 0;
                subtitle->length = 1;
            }
            darkside_later_play_sound(&assets->later, &assets->dialogue[work->voice_chain++]);
            cine_music(cinematic, 0);
        }
        if (work->voice_chain == 1) {
            if (!darkside_later_draw_selected_character(&assets->later, ball, 5, p, -1,
                                                        "ball-wizard", "19bf7")) {
                return 0;
            }
        } else if (!darkside_later_draw_character(cinematic, ball, 5, 0)) {
            return 0;
        }
        work->position = (p + 1) % 32;
        if (!darkside_later_step(&assets->later, "ball-wizard", 1, 3)) {
            work->result = cinematic->result;
            return 0;
        }
    } while (work->voice_chain < 2 || cine_sound_busy(cinematic) || subtitle->counter);
    cine_music(cinematic, 0);
    if (!darkside_later_fizzle(&assets->later, ball, &work->position, 5, 0, 0, NULL, 0,
                               "ball-wizard-out", 0)) {
        work->result = cinematic->result;
        return 0;
    }
    if (!darkside_later_select_text(&assets->later, 19, 1)) {
        return 0;
    }
    work->voice_chain = 0;
    do {
        unsigned p = work->position;
        cine_reset(cinematic);
        if (!cine_sound_busy(cinematic) && !work->voice_chain) {
            darkside_later_play_sound(&assets->later, &assets->dialogue[2]);
            work->voice_chain = 1;
        }
        if (!darkside_later_draw_ball(cinematic, ball, p, 0)) {
            return 0;
        }
        work->position = (p + 1) % 32;
        if (!darkside_later_step(&assets->later, "ball-pharaoh9", 1, 3)) {
            work->result = cinematic->result;
            return 0;
        }
    } while (!work->voice_chain || cine_sound_busy(cinematic) || subtitle->counter);
    cine_music(cinematic, 0);
    if (!cine_fade(cinematic, 0, 4, "ball-final-fade-out")) {
        work->result = cinematic->result;
        return 0;
    }
    if (cinematic->events) {
        fprintf(cinematic->events, "checkpoint,ball-dialogue-completed,%llu\n",
                (unsigned long long)cinematic->now);
    }

    return 1;
}

/* Play the flight portion of the Darkside introduction. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_later_flight(Cinematic *cinematic, CC *archive, LaterAssets *assets,
                                 LaterWork *work)
{

    if (!darkside_later_load_sound(archive, "pharoh11.voc", &assets->speech) ||
        !darkside_later_replace_palette(archive, "fly.pal", cinematic) ||
        !darkside_later_replace_raw(archive, "fly.raw", cinematic) ||
        !cine_load(archive, "fly.int", &assets->art[0]) ||
        !darkside_later_select_text(&assets->later, 6, 0)) {
        return 0;
    }
    if (!darkside_later_present(&assets->later, "flight-black", 1) ||
        !cine_fade(cinematic, 1, 4, "flight-fade-in")) {
        work->result = cinematic->result;
        return 0;
    }
    darkside_later_play_sound(&assets->later, &assets->speech);
    for (work->frame = 0; work->frame < 35; work->frame++) {
        cine_reset(cinematic);
        cine_restore(cinematic, 0);
        if (!cine_draw_at(cinematic, &assets->art[0], work->frame, 0, 0, 0) ||
            !darkside_later_present(&assets->later, "flight", 1)) {
            return 0;
        }
        if (work->frame == 16 || work->frame == 28) {
            cine_music(cinematic, 43);
        }
        if (!cine_poll(cinematic) || !cine_wait_ticks(cinematic, 2, 0)) {
            work->result = cinematic->result;
            return 0;
        }
    }
    cine_music(cinematic, 0);
    if (!cine_fade(cinematic, 0, 4, "flight-fade-out")) {
        work->result = cinematic->result;
        return 0;
    }
    cine_music_volume(cinematic, 95);
    free(assets->art[0].data);
    memset(&assets->art[0], 0, sizeof(assets->art[0]));
    if (cinematic->events) {
        fprintf(cinematic->events, "checkpoint,flight-completed,%llu\n",
                (unsigned long long)cinematic->now);
    }

    return 1;
}

/* Play the bird portion of the Darkside introduction. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int darkside_later_bird(Cinematic *cinematic, CC *archive, LaterAssets *assets,
                               LaterWork *work)
{

    if (!darkside_later_replace_raw(archive, "bird.raw", cinematic)) {
        return 0;
    }
    for (work->asset_index = 0; work->asset_index < 11; work->asset_index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "bird%02u.int", work->asset_index + 1);
        if (!cine_load(archive, work->asset_name, &assets->art[work->asset_index])) {
            return 0;
        }
    }
    for (work->asset_index = 0; work->asset_index < 4; work->asset_index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "drop%u.int", work->asset_index + 1);
        if (!cine_load(archive, work->asset_name, &assets->drops[work->asset_index])) {
            return 0;
        }
    }
    if (!darkside_later_load_sound(archive, "comet.voc", &assets->speech) ||
        !darkside_later_load_sound(&assets->dark, "screech.voc", &assets->effects[0]) ||
        !darkside_later_load_sound(&assets->dark, "disint.voc", &assets->effects[1]) ||
        !darkside_later_load_sound(&assets->dark, "laff1.voc", &assets->effects[2])) {
        return 0;
    }
    if (!darkside_later_present(&assets->later, "bird-black", 0) ||
        !cine_fade(cinematic, 1, 4, "bird-fade-in")) {
        work->result = cinematic->result;
        return 0;
    }
    for (work->frame = 0; work->frame < 81; work->frame++) {
        cine_reset(cinematic);
        cine_restore(cinematic, 0);
        if (!cine_draw_at(cinematic, &assets->art[work->frame / 8], work->frame % 8, 0, 0, 0) ||
            !darkside_later_present(&assets->later, "bird", 0)) {
            return 0;
        }
        if (work->frame == 1 || work->frame == 21) {
            darkside_later_play_sound(&assets->later, &assets->speech);
        }
        if (work->frame == 1 || work->frame == 16 || work->frame == 39) {
            cine_music(cinematic, 43);
        }
        if (work->frame == 56) {
            darkside_later_play_sound(&assets->later, &assets->effects[0]);
        }
        if (work->frame == 61) {
            darkside_later_play_sound(&assets->later, &assets->effects[1]);
        }
        if (work->frame == 73) {
            darkside_later_play_sound(&assets->later, &assets->effects[2]);
        }
        if (!cine_poll(cinematic) || !cine_wait_ticks(cinematic, 2, 0)) {
            work->result = cinematic->result;
            return 0;
        }
    }
    cine_music(cinematic, 0);
    if (cinematic->events) {
        fprintf(cinematic->events, "checkpoint,bird-completed,%llu\n",
                (unsigned long long)cinematic->now);
    }

    return 1;
}

/* Play the drop portion of the Darkside introduction. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int darkside_later_drop(Cinematic *cinematic, CC *archive, LaterAssets *assets,
                               LaterWork *work)
{

    if (!darkside_later_replace_raw(archive, "drop.raw", cinematic) ||
        !darkside_later_load_sound(&assets->dark, "branch.voc", &assets->speech) ||
        !darkside_later_load_sound(archive, "vine.voc", &assets->effects[0]) ||
        !darkside_later_load_sound(archive, "thud.voc", &assets->effects[1]) ||
        !darkside_later_present(&assets->later, "drop-background", 0)) {
        return 0;
    }
    for (work->frame = 0; work->frame < 39; work->frame++) {
        cine_reset(cinematic);
        cine_restore(cinematic, 0);
        if (!cine_draw_at(cinematic, &assets->drops[work->frame / 12], work->frame % 12, 0, 0, 0) ||
            !darkside_later_present(&assets->later, "drop", 0)) {
            return 0;
        }
        if (work->frame == 9) {
            darkside_later_play_sound(&assets->later, &assets->speech);
        }
        if (work->frame == 22) {
            darkside_later_play_sound(&assets->later, &assets->effects[0]);
        }
        if (!cine_poll(cinematic) || !cine_wait_ticks(cinematic, 2, 0)) {
            work->result = cinematic->result;
            return 0;
        }
    }
    darkside_later_play_sound(&assets->later, &assets->effects[1]);
    if (!cine_fade(cinematic, 0, 24, "drop-fade-out")) {
        work->result = cinematic->result;
        return 0;
    }
    if (cinematic->events) {
        fprintf(cinematic->events, "checkpoint,drop-completed,%llu\n",
                (unsigned long long)cinematic->now);
    }

    return 1;
}

/* Play the title portion of the Darkside introduction. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int darkside_later_title(Cinematic *cinematic, CC *archive, LaterAssets *assets,
                                LaterWork *work)
{

    for (work->asset_index = 0; work->asset_index < 11; work->asset_index++) {
        free(assets->art[work->asset_index].data);
        memset(&assets->art[work->asset_index], 0, sizeof(assets->art[work->asset_index]));
    }
    if (!darkside_later_replace_raw(archive, "end.raw", cinematic)) {
        return 0;
    }
    for (work->asset_index = 0; work->asset_index < 3; work->asset_index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "end%u.int", work->asset_index + 1);
        if (!cine_load(archive, work->asset_name, &assets->art[work->asset_index])) {
            return 0;
        }
    }
    if (!darkside_later_present(&assets->later, "end-black", 0) ||
        !cine_fade(cinematic, 1, 4, "end-fade-in")) {
        work->result = cinematic->result;
        return 0;
    }
    cine_music_control(cinematic, 16);
    for (work->frame = 0; work->frame < 19; work->frame++) {
        cine_reset(cinematic);
        cine_restore(cinematic, 0);
        if (!cine_draw_at(cinematic, &assets->art[work->frame / 8], work->frame % 8, 0, 0, 0) ||
            !darkside_later_step(&assets->later, "end", 0, 4)) {
            work->result = cinematic->result;
            return 0;
        }
    }
    memcpy(cinematic->background[0], cinematic->pixels, XEEN_PIXELS);
    cine_reset(cinematic);
    free(assets->art[0].data);
    memset(&assets->art[0], 0, sizeof(assets->art[0]));
    if (!cine_load(archive, "title2.int", &assets->art[0]) || !cine_wait_ticks(cinematic, 20, 1)) {
        work->result = cinematic->result;
        return 0;
    }
    for (int scale = 11; scale >= 0; scale--) {
        cine_reset(cinematic);
        cine_restore(cinematic, 0);
        if (!cine_draw_at(cinematic, &assets->art[0], 0, end_x0[scale], end_y[scale],
                          (unsigned)scale) ||
            !cine_draw_at(cinematic, &assets->art[0], 1, end_x1[scale], end_y[scale],
                          (unsigned)scale) ||
            !darkside_later_step(&assets->later, "end-title", 0, 2)) {
            work->result = cinematic->result;
            return 0;
        }
    }
    memcpy(cinematic->background[0], cinematic->pixels, XEEN_PIXELS);
    if (cinematic->events) {
        fprintf(cinematic->events, "checkpoint,end-completed,%llu\n",
                (unsigned long long)cinematic->now);
    }

    return 1;
}

/* Play the fire portion of the Darkside introduction. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int darkside_later_fire(Cinematic *cinematic, CC *archive, LaterAssets *assets,
                               LaterWork *work)
{

    /* 1A515..1A544 (Phase 308, owner decision D2): DS 4298 (XEEN.CFG byte 26) = 1, then the 30
     * bytes from DS 427E written to xeen.cfg (1A52F; its result not tested). The intro then
     * plays in its short form on the next launch (17E2A, 17F0D). 1A545: DS 4188 (fire_enabled,
     * 1 since startup 14464) selects the fire segment, else 1A75F. */
    darkside_intro_write_cfg(cinematic);
    if (cinematic->fire_enabled) {
        if (!cine_fade(cinematic, 0, 4, "fire-fade-out") ||
            !darkside_later_replace_palette(archive, "firemain.pal", cinematic) ||
            !darkside_later_replace_raw(archive, "firemain.raw", cinematic)) {
            work->result = cinematic->result;
            return 0;
        }
        if (!darkside_later_load_sound(archive, "explosio.voc", &assets->speech) ||
            !darkside_later_load_sound(archive, "rumble.voc", &assets->effects[0]) ||
            !darkside_later_load_sound(archive, "thud.voc", &assets->effects[1]) ||
            !darkside_later_present(&assets->later, "fire-black", 0) ||
            !cine_fade(cinematic, 1, 4, "fire-fade-in")) {
            work->result = cinematic->result;
            return 0;
        }
        cine_reset(cinematic);
        for (work->asset_index = 0; work->asset_index < 11; work->asset_index++) {
            free(assets->art[work->asset_index].data);
            memset(&assets->art[work->asset_index], 0, sizeof(assets->art[work->asset_index]));
        }
        for (work->asset_index = 0; work->asset_index < 7; work->asset_index++) {
            snprintf(work->asset_name, sizeof(work->asset_name), "wfire%u.int",
                     work->asset_index + 1);
            if (!cine_load(archive, work->asset_name, &assets->art[work->asset_index])) {
                return 0;
            }
        }
        if (!cine_wait_key_ticks(cinematic, 10)) {
            work->result = cinematic->result;
            return 0;
        }
        for (work->frame = 0; work->frame < 28; work->frame++) {
            if (work->frame == 17) {
                darkside_later_play_sound(&assets->later, &assets->speech);
            }
            if (work->frame < 17 && !cine_sound_busy(cinematic)) {
                darkside_later_play_sound(&assets->later, &assets->effects[0]);
            }
            cine_reset(cinematic);
            cine_restore(cinematic, 0);
            if (!cine_draw_at(cinematic, &assets->art[work->frame / 5], work->frame % 5, 0, 45,
                              0) ||
                !darkside_later_present(&assets->later, "fire", 0) ||
                !cine_wait_fire_ticks(cinematic, 2)) {
                work->result = cinematic->result;
                return 0;
            }
        }
        memcpy(cinematic->background[0], cinematic->pixels, XEEN_PIXELS);
        for (work->asset_index = 0; work->asset_index < 2; work->asset_index++) {
            for (work->frame = 0; work->frame < 21; work->frame++) {
                cine_reset(cinematic);
                cine_restore(cinematic, 0);
                if (!cine_draw_at(cinematic, &assets->art[6], work->frame, 0, 45, 0) ||
                    !darkside_later_present(&assets->later, "fire-title", 0)) {
                    return 0;
                }
                if (work->frame == 0 || work->frame == 11) {
                    darkside_later_play_sound(&assets->later, &assets->effects[1]);
                }
                if (work->frame == 3) {
                    cine_music(cinematic, 60);
                }
                if (!cine_wait_fire_ticks(cinematic, 2)) {
                    work->result = cinematic->result;
                    return 0;
                }
            }
        }
        if (!cine_wait_key_ticks(cinematic, 10)) {
            work->result = cinematic->result;
            return 0;
        }
    }
    cine_music(cinematic, 0);
    if (cinematic->events) {
        fprintf(cinematic->events, "checkpoint,darkside-intro-completed-1a7d8,%llu\n",
                (unsigned long long)cinematic->now);
    }

    return 1;
}

/* 17F1B..17F34 then 1A515 (Phase 308): the short Darkside intro's end. The original frees
 * pharoh1a/pharoh1b (this player loads them later) and jumps to the configuration write and the
 * fire segment without the pyramid's fade-out. Borrows the cinematic, archive and caption
 * state; returns the cinematic result. */
int darkside_later_fire_only(Cinematic *cinematic, CC *archive, Subtitles *subtitle,
                             const Blob *text_pool, const Blob *font, const Blob *box)
{
    LaterAssets assets = {0};
    LaterWork work;
    assets.later = (Later){cinematic, subtitle, text_pool, font, box, 0};
    work.position = 0;
    work.result = XEEN_FAILED;
    if (darkside_later_fire(cinematic, archive, &assets, &work)) {
        work.result = XEEN_COMPLETED;
    }
    voice_free(&assets.speech.v);
    for (work.asset_index = 0; work.asset_index < 3; work.asset_index++) {
        voice_free(&assets.dialogue[work.asset_index].v);
        voice_free(&assets.effects[work.asset_index].v);
    }
    for (work.asset_index = 0; work.asset_index < 11; work.asset_index++) {
        free(assets.art[work.asset_index].data);
    }
    cc_close(&assets.dark);
    return work.result;
}

/* Continue the Darkside introduction through the crystal-ball dialogue, flight and closing
 * sequence. Borrows the cinematic, archive, ball art, caption state, text pool, font and box;
 * takes the incoming ball position. Releases local assets and returns the cinematic result.
 */
int darkside_later(Cinematic *cinematic, CC *archive, Blob ball[9], unsigned position,
                   Subtitles *subtitle, const Blob *text_pool, const Blob *font, const Blob *box)
{
    LaterAssets assets = {0};
    assets.later = (Later){cinematic, subtitle, text_pool, font, box, 0};
    LaterWork work;
    work.position = position;
    work.result = XEEN_FAILED;
    if (!darkside_later_open_dialogue(cinematic, archive, ball, &assets, &work)) {
        goto done;
    }
    if (!darkside_later_darklord(cinematic, ball, subtitle, &assets, &work)) {
        goto done;
    }
    if (!darkside_later_goon(cinematic, archive, ball, subtitle, &assets, &work)) {
        goto done;
    }
    if (!darkside_later_wizard(cinematic, archive, ball, subtitle, &assets, &work)) {
        goto done;
    }
    if (!darkside_later_flight(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (!darkside_later_bird(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (!darkside_later_drop(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (!darkside_later_title(cinematic, archive, &assets, &work)) {
        goto done;
    }
    if (!darkside_later_fire(cinematic, archive, &assets, &work)) {
        goto done;
    }
    work.result = XEEN_COMPLETED;
done:
    voice_free(&assets.speech.v);
    for (work.asset_index = 0; work.asset_index < 3; work.asset_index++) {
        voice_free(&assets.dialogue[work.asset_index].v);
        voice_free(&assets.effects[work.asset_index].v);
    }
    for (work.asset_index = 0; work.asset_index < 11; work.asset_index++) {
        free(assets.art[work.asset_index].data);
    }
    for (work.asset_index = 0; work.asset_index < 4; work.asset_index++) {
        free(assets.drops[work.asset_index].data);
    }
    cc_close(&assets.dark);
    return work.result;
}
