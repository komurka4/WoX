/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Play the World ending ceremony, joining of Xeen and final score and badge pages.
 * world_ending_play shares runtime and ending_pages; its headless test entry follows the same
 * choreography.
 */
#include "audio/file_io.h"
#include "world_ending.h"
#include "ending_pages.h"
#include "world_ending_text.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    Cinematic *cinematic;
    CC *cc;
    CC dark;
    Blob art[40];
    Blob font;
    uint8_t saved[XEEN_PIXELS];
    uint8_t displayed[XEEN_PIXELS];
    uint8_t held[XEEN_PIXELS];
    Voice voice[3];
    unsigned serial;
    unsigned bottom;
} World;

/* Replace the indexed art slot with an owned archive member. Borrows and updates the scene
 * record and supplied inputs; newly loaded resources stay attached for its common cleanup.
 * Returns 1 on success or 0 on a failed operation or input stop. State or pixels may already
 * have advanced when it stops.
 */
static int world_ending_load_asset(World *world, unsigned n, const char *name)
{
    free(world->art[n].data);
    world->art[n] = (Blob){0};
    return cine_load(world->cc, name, &world->art[n]);
}

/* Load a full-screen background into cinematic pixels. Borrows and updates the scene record
 * and supplied inputs; newly loaded resources stay attached for its common cleanup. Returns 1
 * on success or 0 on a failed operation or input stop. State or pixels may already have
 * advanced when it stops.
 */
static int world_ending_load_background(World *world, const char *name)
{
    Blob b = {0};
    int ok = cine_load(world->cc, name, &b) && b.size == XEEN_PIXELS;
    if (ok) {
        memcpy(world->cinematic->pixels, b.data, XEEN_PIXELS);
    }
    free(b.data);
    return ok;
}

/* Copy the current framebuffer into the selected saved bank. Borrows and updates the scene
 * record; slot indices must be valid. Does not transfer ownership of art or voice data and has
 * no immediate failure status.
 */
static void world_ending_save_background(World *world, unsigned b)
{
    if (!b) {
        memcpy(world->saved, world->cinematic->pixels, XEEN_PIXELS);
    } else {
        memcpy(world->cinematic->background[1], world->cinematic->pixels, XEEN_PIXELS);
    }
}

/* Load the named palette into cinematic palette storage. Borrows and updates the scene record
 * and supplied inputs; newly loaded resources stay attached for its common cleanup. Returns 1
 * on success or 0 on a failed operation or input stop. State or pixels may already have
 * advanced when it stops.
 */
static int world_ending_load_palette(World *world, const char *name)
{
    Blob b = {0};
    int ok = cine_load(world->cc, name, &b) && b.size == XEEN_PALETTE_BYTES;
    if (ok) {
        memcpy(world->cinematic->palette, b.data, XEEN_PALETTE_BYTES);
    }
    free(b.data);
    return ok;
}

/* Draw the requested sprite slot, frame and position. Borrows and updates the scene record and
 * supplied inputs; newly loaded resources stay attached for its common cleanup. Returns 1 on
 * success or 0 on a failed operation or input stop. State or pixels may already have advanced
 * when it stops.
 */
static int world_ending_draw_sprite(World *world, unsigned n, unsigned f, int x, int y,
                                    unsigned flags)
{
    if (f == 65535) {
        return 1;
    }
    if (flags == 0x4000) {
        return sprite_draw_windowed(&world->art[n], f, x, y, world->bottom,
                                    world->cinematic->pixels);
    }
    return cine_draw_effect(world->cinematic, &world->art[n], f, x, y, flags);
}

/* Draw the borrowed caption text with the requested layout. Borrows and updates the scene
 * record and supplied inputs; newly loaded resources stay attached for its common cleanup.
 * Returns 1 on success or 0 on a failed operation or input stop. State or pixels may already
 * have advanced when it stops.
 */
static int world_ending_draw_caption(World *world, const char *s, int upper)
{
    return ending_caption_text(&world->font, s, upper, world->cinematic->pixels);
}

/* Apply optional text and diagnostic composition rules, retrace and present the scene, then
 * poll input. Borrows and updates the scene record and supplied inputs; newly loaded resources
 * stay attached for its common cleanup. Returns 1 on success or 0 on a failed operation or
 * input stop. State or pixels may already have advanced when it stops.
 */
static int world_ending_present_scene(World *world, const char *scene, unsigned frame)
{
    char label[96];
    snprintf(label, sizeof(label), "world-ending-%s-%03u-%05u", scene, frame, world->serial++);
    if (!cine_retrace(world->cinematic) || !cine_present(world->cinematic, label, label)) {
        return 0;
    }
    memcpy(world->displayed, world->cinematic->pixels, XEEN_PIXELS);
    return 1;
}

/* Wait the requested ticks while ignoring ordinary keys and preserving global abort. Borrows
 * and updates the scene record and supplied inputs; newly loaded resources stay attached for
 * its common cleanup. Returns 1 on success or 0 on a failed operation or input stop. State or
 * pixels may already have advanced when it stops.
 */
static int world_ending_wait_ticks(World *world, unsigned n)
{
    return cine_wait_fire_ticks(world->cinematic, n);
}

/* Load and decode the named speech asset from the borrowed archive into the supplied voice
 * slot. Temporary archive bytes are freed before return; decoded samples belong to the slot
 * and must be released with voice_free. Returns 1 on success or 0 on read or decode failure.
 */
static int world_ending_load_voice(World *world, unsigned slot, const char *name, int dark)
{
    Blob b = {0};
    voice_free(&world->voice[slot]);
    int ok =
        cine_load(dark ? &world->dark : world->cc, name, &b) && player_voc_decode(&b, &world->voice[slot]);
    free(b.data);
    return ok;
}

/* Start the selected borrowed voice slot with its cue label. Borrows and updates the scene
 * record; slot indices must be valid. Does not transfer ownership of art or voice data and has
 * no immediate failure status.
 */
static void world_ending_start_voice(World *world, unsigned slot, const char *name)
{
    if (!world->cinematic->subtitles) {
        cine_voice(world->cinematic, &world->voice[slot], name);
    }
}

/* Apply the requested palette fade while preserving ending input handling. Borrows and updates
 * the scene record and supplied inputs; newly loaded resources stay attached for its common
 * cleanup. Returns 1 on success or 0 on a failed operation or input stop. State or pixels may
 * already have advanced when it stops.
 */
static int world_ending_fade(World *world, int in, unsigned step, const char *name)
{
    memcpy(world->held, world->cinematic->pixels, XEEN_PIXELS);
    memcpy(world->cinematic->pixels, world->displayed, XEEN_PIXELS);
    int ok;
    /* An increment larger than the palette range uploads only full brightness. */
    if (in && step == 129) {
        char label[192];
        palette_rgb(world->cinematic->palette, 128, world->cinematic->rgb);
        snprintf(label, sizeof(label), "%s-full", name);
        ok = cine_retrace(world->cinematic) && cine_present(world->cinematic, label, label);
    } else {
        ok = cine_fade(world->cinematic, in, step, name);
    }
    memcpy(world->cinematic->pixels, world->held, XEEN_PIXELS);
    return ok;
}

/* Keep shared scene state alive through the final modal pages, which borrow
 * its arrays. Initialize fields only when the ordered script reaches them.
 */
typedef struct {
    int index;
    int repeat;
    int third_phase;
    int sprite_phase;
    int fourth_phase;
    int pan_offset;
    char asset_name[64];
    char path[4096];
    char score[1024];
    char badge_page[1024];
    const char *pages[2];
} WorldEndingWork;

/* Play the entry portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_entry(World *world, WorldEndingWork *work)
{
    if (world->cinematic->dump && !world->cinematic->events) {
        if (snprintf(work->path, sizeof(work->path), "%s/events.csv", world->cinematic->dump) >=
                (int)sizeof(work->path) ||
            (world->cinematic->events = xeen_fopen(work->path, "wb")) == NULL) {
            return 0;
        }
    }
    if (snprintf(work->path, sizeof(work->path), "%s/DARK.CC", world->cinematic->data) >=
        (int)sizeof(work->path)) {
        return 0;
    }
    if (!(cc_open(&world->dark, work->path))) {
        return 0;
    }
    if (!(cine_load(world->cc, "fnt", &world->font))) {
        return 0;
    }
    cine_music_load(
        world->cinematic, world->cc,
        "outday3.m"); /* Loading starts the song; a separate restart would duplicate its events. */
    work->pages[0] = ending_world_opening_text();
    if (!(ending_modal_play(world->cinematic, world->cc, work->pages, 1, "world-opening"))) {
        return 0;
    }
    cine_music_control(world->cinematic, 223);
    memcpy(world->displayed, world->cinematic->pixels, XEEN_PIXELS);
    return 1;
}

/* Play the sky portion of the World ending. Borrows the cinematic and supplied scene records;
 * updates framebuffer, timing and mutable work state, with loaded assets retained in the
 * caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed operation or
 * input stop.
 */
static int world_ending_scene_sky(World *world, WorldEndingWork *work)
{
    if (!(world_ending_load_background(world, "skymain.raw"))) {
        return 0;
    }
    world_ending_save_background(world, 0);
    if (!(world_ending_load_background(world, "twrsky1.raw"))) {
        return 0;
    }
    memcpy(world->cinematic->background[0], world->cinematic->pixels, XEEN_PIXELS);
    world_ending_save_background(world, 1);
    if (!(world_ending_load_palette(world, "skymain.pal"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 0, "sc02.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 1, "tower1.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 2, "tower2.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 3, "sc3a.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 4, "sc3b1.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 5, "sc3b2.eg2"))) {
        return 0;
    }
    if (!(world_ending_fade(world, 0, 4,
                            "world-ending-"
                            "sky-out"))) {
        return 0;
    }
    if (!(cine_music_wait_fade(world->cinematic))) {
        return 0;
    }
    cine_music_load(world->cinematic, world->cc, "nwblksmt.m");
    if (!(world_ending_load_voice(world, 0, "whoosh.voc", 0))) {
        return 0;
    }
    if (!(world_ending_draw_caption(world, WORLD_CAPTION_PROPHECY_CALL, 1))) {
        return 0;
    }
    memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
    if (!(world_ending_draw_caption(world, WORLD_CAPTION_PROPHECY_CALL, 1))) {
        return 0;
    }
    if (!(world_ending_present_scene(world, "sky", (unsigned)(0)))) {
        return 0;
    }
    if (!(world_ending_fade(world, 1, 4,
                            "world-ending-"
                            "sky-in"))) {
        return 0;
    }
    cine_reset(world->cinematic);
    if (!(world_ending_wait_ticks(world, 60))) {
        return 0;
    }
    for (work->index = 0; work->index < 50; work->index++) {
        if (work->index == 9 || work->index == 22 || work->index == 33 || work->index == 44) {
            world_ending_start_voice(world, 0, "whoosh.voc");
        }
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, 0, (unsigned)(work->index), 0, 0, 0))) {
            return 0;
        }
        if (!(world_ending_draw_caption(world, WORLD_CAPTION_PROPHECY_CALL, 1))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "approach", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
    }
    for (work->index = 0; work->index < 40; work->index++) {
        cine_restore_horizontal(world->cinematic, 0);
        cine_reset(world->cinematic);
        if (!(world_ending_draw_sprite(world, 1, (unsigned)(0), 0, 0, 0x800))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 3, (unsigned)(work->index), 91, 86, 0x800))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "tower", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
    }
    work->fourth_phase = 40;
    work->sprite_phase = 0;
    work->repeat = 0;
    for (work->pan_offset = 0; work->pan_offset < 320; work->pan_offset += 5) {
        cine_reset(world->cinematic);
        cine_restore_horizontal(world->cinematic, (unsigned)work->sprite_phase);
        if (!(world_ending_draw_sprite(world, 1, (unsigned)(0), work->pan_offset, 0, 0x800))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 3, (unsigned)(work->fourth_phase),
                                       work->pan_offset + 91, 86, 0x800))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 2, (unsigned)(0), work->pan_offset - 320, 0,
                                       0x800))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 2, (unsigned)(1), work->pan_offset - 160, 0,
                                       0x800))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 4 + work->repeat / 30, (unsigned)(work->repeat % 30),
                                       work->pan_offset - 277, 65, 0x800))) {
            return 0;
        }
        work->fourth_phase = (work->fourth_phase + 1) % 57;
        if (work->pan_offset > 160) {
            work->repeat = (work->repeat + 1) % 60;
        }
        work->sprite_phase -= 2;
        if (work->sprite_phase < 1) {
            work->sprite_phase = 320;
        }
        if (work->pan_offset > 120) {
            if (!(world_ending_draw_caption(world, WORLD_CAPTION_GATHERING, 1))) {
                return 0;
            }
        }
        if (!(world_ending_present_scene(world, "tower-pan", (unsigned)(work->pan_offset)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 1))) {
            return 0;
        }
    }
    for (; work->repeat < 60; work->repeat++) {
        cine_reset(world->cinematic);
        cine_restore_horizontal(world->cinematic, (unsigned)work->sprite_phase);
        if (!(world_ending_draw_sprite(world, 2, (unsigned)(0), 0, 0, 0x800))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 2, (unsigned)(1), 160, 0, 0x800))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 4 + work->repeat / 30, (unsigned)(work->repeat % 30),
                                       43, 65, 0x800))) {
            return 0;
        }
        if (!(world_ending_draw_caption(world, WORLD_CAPTION_GATHERING, 1))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "tower-close", (unsigned)(work->repeat)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
    }
    return 1;
}

/* Play the corridor portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_corridor(World *world, WorldEndingWork *work)
{

    if (!(world_ending_load_palette(world, "cd10001.pal"))) {
        return 0;
    }
    for (work->index = 0; work->index < 4; work->index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "cd1%c1.eg2", 'a' + work->index);
        if (!(world_ending_load_asset(world, work->index, work->asset_name))) {
            return 0;
        }
    }
    if (!(world_ending_load_background(world, "cd10001.raw"))) {
        return 0;
    }
    world_ending_save_background(world, 0);
    if (!(world_ending_fade(world, 1, 129,
                            "world-ending-"
                            "cd1-in"))) {
        return 0;
    }
    if (!(world_ending_present_scene(world, "cd1-initial", (unsigned)(0)))) {
        return 0;
    }
    for (work->index = 0; work->index < 59; work->index++) {
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, work->index / 16, (unsigned)(work->index % 16), 0, 0,
                                       0))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "corridor", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
    }
    return 1;
}

/* Play the gate portion of the World ending. Borrows the cinematic and supplied scene records;
 * updates framebuffer, timing and mutable work state, with loaded assets retained in the
 * caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed operation or
 * input stop.
 */
static int world_ending_scene_gate(World *world, WorldEndingWork *work)
{
    if (!(world_ending_load_palette(world, "cd2orig.pal"))) {
        return 0;
    }
    for (work->index = 0; work->index < 26; work->index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "cd2%c1.eg2", 'a' + work->index);
        if (!(world_ending_load_asset(world, work->index, work->asset_name))) {
            return 0;
        }
    }
    for (work->index = 0; work->index < 8; work->index++) {
        snprintf(work->asset_name, sizeof(work->asset_name), "cd2%c2.eg2", 'a' + work->index);
        if (!(world_ending_load_asset(world, 26 + work->index, work->asset_name))) {
            return 0;
        }
    }
    if (!(world_ending_load_asset(world, 34, "cd2i2.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_background(world, "cd2orig.raw"))) {
        return 0;
    }
    world_ending_save_background(world, 0);
    if (!(world_ending_fade(world, 1, 129,
                            "world-ending-"
                            "cd2-in"))) {
        return 0;
    }
    for (work->index = 0; work->index < 273; work->index++) {
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, work->index / 8, (unsigned)(work->index % 8), 0, 0,
                                       0))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "gate", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
    }
    return 1;
}

/* Play the shaft portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_shaft(World *world, WorldEndingWork *work)
{
    if (!(world_ending_load_asset(world, 0, "cd3i2.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_palette(world, "cd30001.pal"))) {
        return 0;
    }
    if (!(world_ending_load_background(world, "cd30001.raw"))) {
        return 0;
    }
    if (!(world_ending_fade(world, 1, 129,
                            "world-ending-"
                            "cd3-in"))) {
        return 0;
    }
    for (work->index = 0; work->index < 268; work->index++) {
        if (work->index % 4 == 0) {
            snprintf(work->asset_name, sizeof(work->asset_name), "cd3%c%d.eg2",
                     'a' + (work->index / 4) % 26, 1 + (work->index / 4) / 26);
            if (!(world_ending_load_asset(world, 0, work->asset_name))) {
                return 0;
            }
        }
        cine_reset(world->cinematic);
        if (!(world_ending_draw_sprite(world, 0, (unsigned)(work->index % 4), 101, 0, 0))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "shaft", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
    }
    return 1;
}

/* Play the court portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_court(World *world, WorldEndingWork *work)
{
    if (!(world_ending_fade(world, 0, 4,
                            "world-ending-"
                            "court-out"))) {
        return 0;
    }
    if (!(world_ending_load_palette(world, "skymain.pal"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 6, "sc06.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 7, "sc14.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 8, "sc13.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 9, "sc17.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 10, "cube.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 11, "hands.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 12, "sc20a.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 13, "sc20b.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 14, "sc20c.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 15, "sc20d.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 16, "sc22a.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 17, "sc22b.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 18, "sc10.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 19, "staff.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_background(world, "foura.raw"))) {
        return 0;
    }
    world_ending_save_background(world, 0);
    if (!(world_ending_draw_sprite(world, 6, (unsigned)(0), 26, 75, 0))) {
        return 0;
    }
    if (!(world_ending_present_scene(world, "court-initial", (unsigned)(0)))) {
        return 0;
    }
    if (!(world_ending_fade(world, 1, 4,
                            "world-ending-"
                            "court-in"))) {
        return 0;
    }
    for (work->index = 0; work->index < 26; work->index++) {
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, 6, (unsigned)(work->index), 26, 75, 0))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "court", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 3))) {
            return 0;
        }
    }
    return 1;
}

/* Play the queen narration portion of the World ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int world_ending_scene_queen_narration(World *world, WorldEndingWork *work)
{
    if (!(world_ending_fade(world, 0, 4,
                            "world-ending-"
                            "court-queen-out"))) {
        return 0;
    }
    if (!(world_ending_load_background(world, "eg140001.raw"))) {
        return 0;
    }
    world_ending_save_background(world, 0);
    if (!(world_ending_present_scene(world, "queen-black", (unsigned)(0)))) {
        return 0;
    }
    if (!(world_ending_draw_caption(world, WORLD_CAPTION_PHARAOH_CEREMONY, 1))) {
        return 0;
    }
    if (!(world_ending_fade(world, 1, 4,
                            "world-ending-"
                            "queen-in"))) {
        return 0;
    }
    for (work->repeat = 0; work->repeat < 2; work->repeat++) {
        for (work->index = 0; work->index < 15; work->index++) {
            cine_reset(world->cinematic);
            memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
            if (!(world_ending_draw_sprite(world, 7, (unsigned)(work->index), 141, 63, 0))) {
                return 0;
            }
            if (!(world_ending_draw_caption(world, WORLD_CAPTION_PHARAOH_CEREMONY, 1))) {
                return 0;
            }
            if (!(world_ending_present_scene(world, "queen-narration",
                                             (unsigned)(work->repeat * 15 + work->index)))) {
                return 0;
            }
            if (!(world_ending_wait_ticks(world, 3))) {
                return 0;
            }
        }
    }
    return 1;
}

/* Play the kalindra portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_kalindra(World *world, WorldEndingWork *work)
{
    if (!(world_ending_load_background(world, "eg100001.raw"))) {
        return 0;
    }
    memcpy(world->cinematic->background[0], world->cinematic->pixels, XEEN_PIXELS);
    world_ending_save_background(world, 0);
    for (work->repeat = 0; work->repeat < 3; work->repeat++) {
        for (work->index = 0; work->index < 6; work->index++) {
            cine_reset(world->cinematic);
            memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
            if (!(world_ending_draw_sprite(world, 8, (unsigned)(work->index), 26, 21, 0))) {
                return 0;
            }
            if (!(world_ending_draw_caption(world, WORLD_CAPTION_KALINDRA_CUBE, 1))) {
                return 0;
            }
            if (!(world_ending_present_scene(world, "kalindra",
                                             (unsigned)(work->repeat * 6 + work->index)))) {
                return 0;
            }
            if (!(world_ending_wait_ticks(world, 2))) {
                return 0;
            }
        }
    }
    return 1;
}

/* Play the cube rise portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_cube_rise(World *world, WorldEndingWork *work)
{
    memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
    if (!(world_ending_draw_sprite(world, 8, (unsigned)(5), 26, 21, 0))) {
        return 0;
    }
    world_ending_save_background(world, 0);
    if (!(world_ending_draw_caption(world, WORLD_CAPTION_KALINDRA_CUBE, 1))) {
        return 0;
    }
    if (!(world_ending_present_scene(world, "cube-initial", (unsigned)(0)))) {
        return 0;
    }
    if (!(world_ending_load_voice(world, 0, "cast.voc", 0))) {
        return 0;
    }
    work->sprite_phase = 0;
    for (work->index = 185; work->index > 68; work->index -= 3) {
        if (!cine_sound_busy(world->cinematic)) {
            world_ending_start_voice(world, 0, "cast.voc");
        }
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, 9, (unsigned)(0), 33, work->index, 0x4000))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 9, (unsigned)(work->sprite_phase), 33, work->index,
                                       0x4000))) {
            return 0;
        }
        if (!(world_ending_draw_caption(world, WORLD_CAPTION_KALINDRA_CUBE, 1))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "cube-rise", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
        work->sprite_phase = (work->sprite_phase + 1) % 17;
    }
    for (work->index = 0; work->index < 17; work->index++) {
        if (!cine_sound_busy(world->cinematic)) {
            world_ending_start_voice(world, 0, "cast.voc");
        }
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, 9, (unsigned)(0), 33, 68, 0x4000))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 9, (unsigned)(work->index), 33, 68, 0x4000))) {
            return 0;
        }
        if (!(world_ending_draw_caption(world, WORLD_CAPTION_KALINDRA_CUBE, 1))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "cube-glow", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
    }
    memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
    if (!(world_ending_draw_sprite(world, 9, (unsigned)(0), 33, 68, 0x4000))) {
        return 0;
    }
    if (!(world_ending_draw_caption(world, WORLD_CAPTION_KALINDRA_CUBE, 1))) {
        return 0;
    }
    if (!(world_ending_present_scene(world, "cube-still", (unsigned)(0)))) {
        return 0;
    }
    return 1;
}

/* Play the royal narration portion of the World ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int world_ending_scene_royal_narration(World *world, WorldEndingWork *work)
{
    if (!(world_ending_fade(world, 0, 4,
                            "world-ending-"
                            "cube-out"))) {
        return 0;
    }
    if (!(world_ending_load_background(world, "eg140001.raw"))) {
        return 0;
    }
    world_ending_save_background(world, 0);
    if (!(world_ending_present_scene(world, "royal-black", (unsigned)(0)))) {
        return 0;
    }
    if (!(world_ending_fade(world, 1, 4,
                            "world-ending-"
                            "royal-in"))) {
        return 0;
    }
    for (work->repeat = 0; work->repeat < 2; work->repeat++) {
        for (work->index = 0; work->index < 15; work->index++) {
            cine_reset(world->cinematic);
            memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
            if (!(world_ending_draw_sprite(world, 7, (unsigned)(work->index), 141, 63, 0))) {
                return 0;
            }
            if (!(world_ending_present_scene(world, "royal-narration",
                                             (unsigned)(work->repeat * 15 + work->index)))) {
                return 0;
            }
            if (!(world_ending_wait_ticks(world, 3))) {
                return 0;
            }
        }
    }
    return 1;
}

/* Play the staff rise portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_staff_rise(World *world, WorldEndingWork *work)
{
    cine_restore_horizontal(world->cinematic, 0);
    world_ending_save_background(world, 0);
    for (work->repeat = 0; work->repeat < 3; work->repeat++) {
        for (work->index = 0; work->index < 5; work->index++) {
            cine_reset(world->cinematic);
            memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
            if (!(world_ending_draw_sprite(world, 18, (unsigned)(work->index), 26, 21, 0))) {
                return 0;
            }
            if (!(world_ending_draw_caption(world, WORLD_CAPTION_ROLAND_SCEPTRE, 1))) {
                return 0;
            }
            if (!(world_ending_present_scene(world, "roland",
                                             (unsigned)(work->repeat * 5 + work->index)))) {
                return 0;
            }
            if (!(world_ending_wait_ticks(world, 2))) {
                return 0;
            }
        }
    }
    memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
    if (!(world_ending_draw_caption(world, WORLD_CAPTION_ROLAND_SCEPTRE, 1))) {
        return 0;
    }
    if (!(world_ending_present_scene(world, "staff-initial", (unsigned)(0)))) {
        return 0;
    }
    for (work->index = 185; work->index > 13; work->index -= 6) {
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, 19, (unsigned)(0), 196, work->index, 0x4000))) {
            return 0;
        }
        if (!(world_ending_draw_caption(world, WORLD_CAPTION_ROLAND_SCEPTRE, 1))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "staff-rise", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
    }
    cine_reset(world->cinematic);
    if (!(world_ending_wait_ticks(world, 30))) {
        return 0;
    }
    return 1;
}

/* Play the altar royal portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_altar_royal(World *world, WorldEndingWork *work)
{
    if (!(world_ending_fade(world, 0, 4,
                            "world-ending-"
                            "staff-out"))) {
        return 0;
    }
    if (!(world_ending_load_background(world, "eg140001.raw"))) {
        return 0;
    }
    world_ending_save_background(world, 0);
    if (!(world_ending_present_scene(world, "altar-royal-black", (unsigned)(0)))) {
        return 0;
    }
    if (!(world_ending_fade(world, 1, 4,
                            "world-ending-"
                            "altar-royal-in"))) {
        return 0;
    }
    for (work->repeat = 0; work->repeat < 2; work->repeat++) {
        for (work->index = 0; work->index < 15; work->index++) {
            cine_reset(world->cinematic);
            memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
            if (!(world_ending_draw_sprite(world, 7, (unsigned)(work->index), 141, 63, 0))) {
                return 0;
            }
            if (!(world_ending_present_scene(world, "altar-royal",
                                             (unsigned)(work->repeat * 15 + work->index)))) {
                return 0;
            }
            if (!(world_ending_wait_ticks(world, 3))) {
                return 0;
            }
        }
    }
    return 1;
}

/* Play the altar cube portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_altar_cube(World *world, WorldEndingWork *work)
{
    world->bottom = 118;
    if (!(world_ending_load_background(world, "tablmain.raw"))) {
        return 0;
    }
    world_ending_save_background(world, 0);
    world_ending_save_background(world, 1);
    if (!(world_ending_load_voice(world, 0, "cast.voc", 0))) {
        return 0;
    }
    if (!(world_ending_draw_sprite(world, 12, (unsigned)(0), 26, 55, 0))) {
        return 0;
    }
    if (!(world_ending_draw_sprite(world, 11, (unsigned)(0), 58, 17, 0))) {
        return 0;
    }
    if (!(world_ending_draw_sprite(world, 10, (unsigned)(0), 101, 11, 0x4000))) {
        return 0;
    }
    if (!(world_ending_present_scene(world, "altar-initial", (unsigned)(0)))) {
        return 0;
    }
    world_ending_start_voice(world, 0, "cast.voc");
    for (work->index = 0; work->index < 5; work->index++) {
        if (!cine_sound_busy(world->cinematic)) {
            world_ending_start_voice(world, 0, "cast.voc");
        }
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, 12, (unsigned)(0), 26, 55, 0))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 11, (unsigned)(0), 58, 17, 0))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 10, (unsigned)(work->index), 101, 11, 0x4000))) {
            return 0;
        }
        if (!(world_ending_draw_caption(world, WORLD_CAPTION_PLACE_CUBE, 1))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "altar-cube", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
    }
    return 1;
}

/* Play the altar hands portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_altar_hands(World *world, WorldEndingWork *work)
{
    work->sprite_phase = 0;
    for (work->index = 0; work->index < 16; work->index++) {
        if (!cine_sound_busy(world->cinematic)) {
            world_ending_start_voice(world, 0, "cast.voc");
        }
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, 12, (unsigned)(0), 26, 55, 0))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 11, (unsigned)(work->index), 58, 17, 0))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 10, (unsigned)(work->sprite_phase), 101, 11,
                                       0x4000))) {
            return 0;
        }
        if (!(world_ending_draw_caption(world, WORLD_CAPTION_PLACE_CUBE, 1))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "altar-hands", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
        work->sprite_phase = (work->sprite_phase + 1) % 5;
    }
    return 1;
}

/* Play the altar drop portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_altar_drop(World *world, WorldEndingWork *work)
{
    work->sprite_phase = 0;
    for (work->index = 11; work->index < 82; work->index++) {
        if (!cine_sound_busy(world->cinematic)) {
            world_ending_start_voice(world, 0, "cast.voc");
        }
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, 12, (unsigned)(0), 26, 55, 0))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 10, (unsigned)(work->sprite_phase), 101, work->index,
                                       0x4000))) {
            return 0;
        }
        if (!(world_ending_draw_caption(world, WORLD_CAPTION_PLACE_CUBE, 1))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "altar-cube-drop", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
        work->sprite_phase = (work->sprite_phase + 1) % 5;
    }
    return 1;
}

/* Play the altar close portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_altar_close(World *world, WorldEndingWork *work)
{
    if (!(world_ending_load_voice(world, 0, "click.voc", 0))) {
        return 0;
    }
    if (!(world_ending_load_voice(world, 1, "padspell.voc", 0))) {
        return 0;
    }
    world_ending_start_voice(world, 0, "click.voc");
    world_ending_start_voice(world, 1, "padspell.voc");
    for (work->index = 0; work->index < 14; work->index++) {
        if (work->index == 10) {
            world_ending_start_voice(world, 1, "padspell.voc");
        }
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, 12 + work->index / 7, (unsigned)(work->index % 7), 26,
                                       55, 0))) {
            return 0;
        }
        if (!(world_ending_draw_caption(world, WORLD_CAPTION_PLACE_CUBE, 1))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "altar-close", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
    }
    return 1;
}

/* Play the altar flare portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_altar_flare(World *world, WorldEndingWork *work)
{
    for (work->index = 0; work->index < 17; work->index++) {
        if (work->index == 10) {
            world_ending_start_voice(world, 1, "padspell.voc");
        }
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, 14, (unsigned)(work->index), 26, 55, 0))) {
            return 0;
        }
        if (!(world_ending_draw_caption(world, WORLD_CAPTION_PLACE_CUBE, 1))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "altar-flare", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 3))) {
            return 0;
        }
    }
    return 1;
}

/* Play the altar finish portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_altar_finish(World *world, WorldEndingWork *work)
{
    world_ending_start_voice(world, 1, "padspell.voc");
    for (work->index = 0; work->index < 6; work->index++) {
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, 15, (unsigned)(work->index), 26, 55, 0))) {
            return 0;
        }
        if (!(world_ending_draw_caption(world, WORLD_CAPTION_PLACE_CUBE, 1))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "altar-finish", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 4))) {
            return 0;
        }
    }
    memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
    if (!(world_ending_draw_caption(world, WORLD_CAPTION_PLACE_CUBE, 1))) {
        return 0;
    }
    if (!(world_ending_present_scene(world, "altar-empty", (unsigned)(0)))) {
        return 0;
    }
    cine_reset(world->cinematic);
    if (!(world_ending_wait_ticks(world, 30))) {
        return 0;
    }
    return 1;
}

/* Play the sceptre join portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_sceptre_join(World *world, WorldEndingWork *work)
{
    cine_restore_horizontal(world->cinematic, 320);
    world_ending_save_background(world, 0);
    if (!(world_ending_load_voice(world, 0, "click.voc", 0))) {
        return 0;
    }
    if (!(world_ending_load_voice(world, 1, "elect.voc", 1))) {
        return 0;
    }
    if (!(world_ending_load_voice(world, 2, "explosio.voc", 0))) {
        return 0;
    }
    for (work->index = 0; work->index < 35; work->index++) {
        if (work->index == 3) {
            world_ending_start_voice(world, 0, "click.voc");
        }
        if (work->index == 5 || work->index == 15) {
            world_ending_start_voice(world, 1, "elect.voc");
        }
        if (work->index == 22) {
            world_ending_start_voice(world, 2, "explosio.voc");
        }
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, 16 + work->index / 20, (unsigned)(work->index % 20),
                                       112, 17, 0))) {
            return 0;
        }
        if (!(world_ending_draw_caption(world, WORLD_CAPTION_PLACE_SCEPTRE, 1))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "sceptre-join", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 3))) {
            return 0;
        }
    }
    return 1;
}

/* Play the tower transform portion of the World ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int world_ending_scene_tower_transform(World *world, WorldEndingWork *work)
{
    if (!(world_ending_load_asset(world, 0, "sc23a.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 1, "sc23b.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 2, "sc23c.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 3, "sc23d.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 4, "sc23e.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 5, "sc23f.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 6, "sc23g.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 7, "sc23h.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 8, "sc24.eg2"))) {
        return 0;
    }
    if (!(world_ending_fade(world, 0, 4,
                            "world-ending-"
                            "transform-out"))) {
        return 0;
    }
    if (!(world_ending_load_background(world, "eg23prt2.raw"))) {
        return 0;
    }
    world_ending_save_background(world, 0);
    if (!(world_ending_draw_sprite(world, 0, (unsigned)(0), 0, 0, 0))) {
        return 0;
    }
    if (!(world_ending_present_scene(world, "transform-black", (unsigned)(0)))) {
        return 0;
    }
    if (!(world_ending_load_voice(world, 0, "gascompr.voc", 0))) {
        return 0;
    }
    if (!(world_ending_fade(world, 1, 4,
                            "world-ending-"
                            "transform-in"))) {
        return 0;
    }
    for (work->index = 0; work->index < 61; work->index++) {
        if (work->index == 2 || work->index == 15 || work->index == 25 || work->index == 33 ||
            work->index == 41) {
            world_ending_start_voice(world, 0, "gascompr.voc");
        }
        cine_reset(world->cinematic);
        if (!(world_ending_draw_sprite(world, work->index / 8, (unsigned)(work->index % 8), 0, 0,
                                       0))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "tower-transform", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 4))) {
            return 0;
        }
    }
    return 1;
}

/* Play the world island portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_world_island(World *world, WorldEndingWork *work)
{
    if (!(world_ending_load_background(world, "skymain.raw"))) {
        return 0;
    }
    if (!(world_ending_load_voice(world, 0, "photon.voc", 1))) {
        return 0;
    }
    world_ending_save_background(world, 0);
    if (!(world_ending_draw_sprite(world, 8, (unsigned)(0), 103, 6, 0))) {
        return 0;
    }
    if (!(world_ending_present_scene(world, "world-initial", (unsigned)(0)))) {
        return 0;
    }
    for (work->index = 0; work->index < 35; work->index++) {
        if (work->index == 2 || work->index == 15 || work->index == 21) {
            world_ending_start_voice(world, 0, "photon.voc");
        }
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, 8, (unsigned)(work->index), 103, 6, 0))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "world-island", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 3))) {
            return 0;
        }
    }
    for (work->index = 20; work->index < 35; work->index++) {
        cine_reset(world->cinematic);
        memcpy(world->cinematic->pixels, world->saved, XEEN_PIXELS);
        if (!(world_ending_draw_sprite(world, 8, (unsigned)(work->index), 103, 6, 0))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "world-hold", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 3))) {
            return 0;
        }
    }
    return 1;
}

/* Play the elements portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_elements(World *world, WorldEndingWork *work)
{
    if (!(world_ending_load_asset(world, 9, "sc25.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 10, "sc261a.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 11, "sc261b.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 12, "sc262.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 13, "sc263.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 14, "sc264.eg2"))) {
        return 0;
    }
    if (!(world_ending_load_asset(world, 15, "sc27.eg2"))) {
        return 0;
    }
    if (!(world_ending_fade(world, 0, 4,
                            "world-ending-"
                            "elements-out"))) {
        return 0;
    }
    if (!(world_ending_load_background(world, "eg250001.raw"))) {
        return 0;
    }
    if (!(world_ending_load_palette(world, "eg250001.pal"))) {
        return 0;
    }
    if (!(world_ending_load_voice(world, 0, "comet.voc", 0))) {
        return 0;
    }
    if (!(world_ending_load_voice(world, 1, "click.voc", 0))) {
        return 0;
    }
    if (!(world_ending_present_scene(world, "elements-black", (unsigned)(0)))) {
        return 0;
    }
    if (!(world_ending_fade(world, 1, 4,
                            "world-ending-"
                            "elements-in"))) {
        return 0;
    }
    world_ending_start_voice(world, 0, "comet.voc");
    for (work->index = 0; work->index < 52; work->index++) {
        if (work->index == 28) {
            world_ending_start_voice(world, 1, "click.voc");
        }
        if (!cine_sound_busy(world->cinematic)) {
            world_ending_start_voice(world, 0, "comet.voc");
        }
        cine_reset(world->cinematic);
        if (!(world_ending_draw_sprite(world, 9, (unsigned)(work->index), 0, 0, 0))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "elements", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
    }
    return 1;
}

/* Play the map join portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_map_join(World *world, WorldEndingWork *work)
{
    if (!(world_ending_load_background(world, "blank.raw"))) {
        return 0;
    }
    if (!(world_ending_load_palette(world, "skymain.pal"))) {
        return 0;
    }
    if (!(world_ending_load_voice(world, 0, "photon.voc", 1))) {
        return 0;
    }
    if (!(world_ending_draw_sprite(world, 10, (unsigned)(0), 7, 4, 0))) {
        return 0;
    }
    if (!(world_ending_draw_sprite(world, 12, (unsigned)(0), 86, 4, 0))) {
        return 0;
    }
    if (!(world_ending_draw_sprite(world, 13, (unsigned)(0), 164, 4, 0))) {
        return 0;
    }
    if (!(world_ending_draw_sprite(world, 14, (unsigned)(0), 242, 4, 0))) {
        return 0;
    }
    if (!(world_ending_present_scene(world, "map-initial", (unsigned)(0)))) {
        return 0;
    }
    if (!(world_ending_fade(world, 1, 129,
                            "world-ending-"
                            "map-in"))) {
        return 0;
    }
    work->sprite_phase = 0;
    work->repeat = 0;
    work->third_phase = 0;
    work->fourth_phase = 0;
    for (work->index = 0; work->index < 78; work->index++) {
        cine_reset(world->cinematic);
        if (!(world_ending_draw_sprite(world, 10 + work->sprite_phase / 14,
                                       (unsigned)(work->sprite_phase % 14), 7, 4, 0))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 12, (unsigned)(work->repeat), 86, 4, 0))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 13, (unsigned)(work->third_phase), 164, 4, 0))) {
            return 0;
        }
        if (!(world_ending_draw_sprite(world, 14, (unsigned)(work->fourth_phase), 242, 4, 0))) {
            return 0;
        }
        if (work->index == 10 || work->index == 28 || work->index == 43 || work->index == 56) {
            world_ending_start_voice(world, 0, "photon.voc");
        }
        work->sprite_phase = (work->sprite_phase + 1) % 28;
        work->repeat = (work->repeat + 1) % 9;
        work->third_phase = (work->third_phase + 1) % 19;
        work->fourth_phase = (work->fourth_phase + 1) % 10;
        if (work->index > 12 && work->sprite_phase < 13) {
            work->sprite_phase = 13;
        }
        if (work->index < 23) {
            work->repeat = 0;
        } else if (work->index > 26 && work->repeat < 5) {
            work->repeat = 5;
        }
        if (work->index < 34) {
            work->third_phase = 0;
        } else if (work->index > 43 && work->third_phase < 12) {
            work->third_phase = 12;
        }
        if (work->index < 53) {
            work->fourth_phase = 0;
        } else if (work->index > 57 && work->fourth_phase < 5) {
            work->fourth_phase = 5;
        }
        if (!(world_ending_present_scene(world, "map-joining", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
    }
    return 1;
}

/* Play the globe lattice portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_globe_lattice(World *world, WorldEndingWork *work)
{

    if (!(world_ending_fade(world, 0, 4,
                            "world-ending-"
                            "globe-out"))) {
        return 0;
    }
    if (!(world_ending_load_background(world, "eg270001.raw"))) {
        return 0;
    }
    if (!(world_ending_load_palette(world, "eg250001.pal"))) {
        return 0;
    }
    if (!(world_ending_load_voice(world, 0, "comet.voc", 0))) {
        return 0;
    }
    if (!(world_ending_load_voice(world, 1, "click.voc", 0))) {
        return 0;
    }
    if (!(world_ending_present_scene(world, "globe-black", (unsigned)(0)))) {
        return 0;
    }
    if (!(world_ending_fade(world, 1, 4,
                            "world-ending-"
                            "globe-in"))) {
        return 0;
    }
    for (work->index = 0; work->index < 89; work->index++) {
        if (!cine_sound_busy(world->cinematic)) {
            world_ending_start_voice(world, 0, "comet.voc");
        }
        if (work->index == 19 || work->index == 60) {
            world_ending_start_voice(world, 1, "click.voc");
        }
        cine_reset(world->cinematic);
        if (!(world_ending_draw_sprite(world, 15, (unsigned)(work->index), 0, 0, 0))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "globe-lattice", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
    }
    return 1;
}

/* Play the globe complete portion of the World ending. Borrows the cinematic and supplied
 * scene records; updates framebuffer, timing and mutable work state, with loaded assets
 * retained in the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a
 * failed operation or input stop.
 */
static int world_ending_scene_globe_complete(World *world, WorldEndingWork *work)
{
    if (!(world_ending_fade(world, 0, 4,
                            "world-ending-"
                            "globe-banks-out"))) {
        return 0;
    }
    {
        static const char *bank[] = {"sc28a.eg2", "sc28b1.eg2", "sc28c.eg2", "sc28d.eg2",
                                     "sc28e.eg2", "sc28f.eg2",  "sc28g.eg2", "sc28h.eg2",
                                     "sc28i.eg2", "sc28j.eg2",  "sc28k.eg2", "sc28l.eg2",
                                     "sc28m.eg2", "sc28n.eg2",  "sc30.eg2"};
        for (work->index = 0; work->index < 15; work->index++) {
            if (!(world_ending_load_asset(world, work->index, bank[work->index]))) {
                return 0;
            }
        }
    }
    if (!(world_ending_load_voice(world, 0, "rumble.voc", 0))) {
        return 0;
    }
    if (!(world_ending_load_background(world, "eg280001.raw"))) {
        return 0;
    }
    world_ending_save_background(world, 0);
    if (!(world_ending_present_scene(world, "globe-banks-black", (unsigned)(0)))) {
        return 0;
    }
    if (!(world_ending_fade(world, 1, 4,
                            "world-ending-"
                            "globe-banks-in"))) {
        return 0;
    }
    for (work->index = 0; work->index < 138; work->index++) {
        if (!cine_sound_busy(world->cinematic) && work->index > 98) {
            world_ending_start_voice(world, 0, "rumble.voc");
        }
        cine_reset(world->cinematic);
        if (!(world_ending_draw_sprite(world, work->index / 10, (unsigned)(work->index % 10), 52,
                                       15, 0))) {
            return 0;
        }
        if (!(world_ending_present_scene(world, "globe-complete", (unsigned)(work->index)))) {
            return 0;
        }
        if (!(world_ending_wait_ticks(world, 2))) {
            return 0;
        }
    }
    return 1;
}

/* Play the white flash portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_white_flash(World *world)
{
    if (!(world_ending_load_palette(world, "white.pal"))) {
        return 0;
    }
    if (!(world_ending_load_voice(world, 0, "explosio.voc", 0))) {
        return 0;
    }
    if (!(world_ending_fade(world, 1, 4,
                            "world-ending-"
                            "white-in"))) {
        return 0;
    }
    world_ending_start_voice(world, 0, "explosio.voc");
    cine_reset(world->cinematic);
    if (!(world_ending_wait_ticks(world, 10))) {
        return 0;
    }
    if (!(world_ending_load_palette(world, "eg250001.pal"))) {
        return 0;
    }
    if (!(world_ending_fade(world, 0, 4,
                            "world-ending-"
                            "white-out"))) {
        return 0;
    }
    return 1;
}

/* Play the final caption portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_final_caption(World *world, WorldEndingWork *work)
{
    for (work->repeat = 0; work->repeat < 20; work->repeat++) {
        for (work->index = 0; work->index < 4; work->index++) {
            cine_reset(world->cinematic);
            if (!(world_ending_draw_sprite(world, 14, (unsigned)(work->index), 0, 0, 0))) {
                return 0;
            }
            if (!(world_ending_draw_caption(world, WORLD_CAPTION_XEEN_UNITED, 0))) {
                return 0;
            }
            if (!(world_ending_present_scene(world, "final-caption",
                                             (unsigned)(work->repeat * 4 + work->index)))) {
                return 0;
            }
            if (work->repeat == 0 && work->index == 0) {
                if (!(world_ending_fade(world, 1, 4,
                                        "world-ending-"
                                        "final-caption-in"))) {
                    return 0;
                }
            }
            if (work->repeat == 17) {
                cine_music_control(world->cinematic, 207);
            }
            if (!(world_ending_wait_ticks(world, 2))) {
                return 0;
            }
        }
    }
    return 1;
}

/* Play the score portion of the World ending. Borrows the cinematic and supplied scene
 * records; updates framebuffer, timing and mutable work state, with loaded assets retained in
 * the caller-owned asset record for cleanup. Returns 1 on completion or 0 on a failed
 * operation or input stop.
 */
static int world_ending_scene_score(World *world, WorldEndingWork *work,
                                    const EndingPayload *payload, unsigned badge)
{
    if (!(world_ending_fade(world, 0, 4,
                            "world-ending-"
                            "score-out"))) {
        return 0;
    }
    if (!(cine_music_wait_fade(world->cinematic))) {
        return 0;
    }
    cine_music_load(
        world->cinematic, world->cc,
        "outday3.m"); /* Loading starts the song; a separate restart would duplicate its events. */
    if (!(ending_score_text(payload, work->score, sizeof(work->score)))) {
        return 0;
    }
    work->pages[0] = work->score;
    work->repeat = 1;
    if (badge) {
        if (!(ending_world_badge_text(badge, work->badge_page, sizeof(work->badge_page)))) {
            return 0;
        }
        work->pages[work->repeat++] = work->badge_page;
    }
    if (!(ending_modal_play_aligned(world->cinematic, world->cc, work->pages,
                                    (unsigned)work->repeat, "world-score", 1))) {
        return 0;
    }
    cine_record(world->cinematic, "world-ending-source-return-1e637");
    cine_record(world->cinematic, "ending-barrier-14d6c");
    return 1;
}

/* Run the complete World unification ending with the requested score and badge. Borrows the
 * cinematic, archive and payload; owns temporary art, voices and secondary archive and
 * releases them before returning the cinematic result.
 */
static int world_ending_run(Cinematic *cinematic, CC *intro, const EndingPayload *payload,
                            unsigned badge)
{
    World world = {0};
    WorldEndingWork work;
    work.sprite_phase = 0;
    work.fourth_phase = 40;
    int i;
    int ok = 0;
    world.cinematic = cinematic;
    world.cc = intro;
    world.bottom = 185;
    if (badge > 2 || !payload) {
        goto done;
    }
    if (!world_ending_scene_entry(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_sky(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_corridor(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_gate(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_shaft(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_court(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_queen_narration(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_kalindra(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_cube_rise(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_royal_narration(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_staff_rise(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_altar_royal(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_altar_cube(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_altar_hands(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_altar_drop(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_altar_close(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_altar_flare(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_altar_finish(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_sceptre_join(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_tower_transform(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_world_island(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_elements(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_map_join(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_globe_lattice(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_globe_complete(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_white_flash(&world)) {
        goto done;
    }
    if (!world_ending_scene_final_caption(&world, &work)) {
        goto done;
    }
    if (!world_ending_scene_score(&world, &work, payload, badge)) {
        goto done;
    }
    ok = 1;
done:
    for (i = 0; i < 40; i++) {
        free(world.art[i].data);
    }
    free(world.font.data);
    for (i = 0; i < 3; i++) {
        voice_free(&world.voice[i]);
    }
    cc_close(&world.dark);
    cinematic->result = ok ? XEEN_COMPLETED : XEEN_FAILED;
    return cinematic->result;
}

/* Play the World unification ending, final score and optional badge page. Borrows the
 * cinematic, intro archive and payload, takes the badge selection, and releases local assets
 * and the secondary archive. Returns the cinematic result: completed or failed.
 */
int world_ending_play(Cinematic *cinematic, CC *intro, const EndingPayload *payload, unsigned badge)
{
    return world_ending_run(cinematic, intro, payload, badge);
}

/* Play the World ending through the headless-only compatibility entry point. Borrows the
 * cinematic, archive and payload and takes the badge selection. Returns failure for live
 * playback; otherwise returns the ending result after releasing local resources.
 */
int world_ending_play_headless(Cinematic *cinematic, CC *intro, const EndingPayload *payload,
                               unsigned badge)
{
    if (!cinematic->dump) {
        return XEEN_FAILED;
    }
    return world_ending_run(cinematic, intro, payload, badge);
}
