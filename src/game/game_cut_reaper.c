/*
 * The Reaper's cutscene (Phase 248, from game_cutscene.c): location kind 8 (34FAA), its frame coordinate tables,
 * its start, its mouth and still frames and its frame loop (reaper_resume and its movements: the slide, the talk, the
 * answer). The shared frame-loop services are game_cutscene.c's.
 */
#include "game_cutscene.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The original calls of the voice driver (187E9) and the CD status (47F3D) in this unit, each by the call's
 * return address: the audio model records which call asked (traces name them). Not stops, not waits. */
enum {
    REAPER_TALK_MOUTH_CALL = 0x355e7,     /* the talk's mouth follows the voice */
    REAPER_ANSWER_VOICE_CALL = 0x356a8,   /* the pause: goin1, reaper13, needkey1 or reaper15 */
    REAPER_ANSWER_VOICE_QUERY_CALL = 0x35748,/* the second talk waits for that voice */
    REAPER_TOWER_VOICE_CALL = 0x35542     /* the tower's voice */
};
/* Where native play stops when the Reaper's cutscene cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json). */
enum {
    REAPER_STOP_FIRST_SPRITES = 0x3515c,     /* the cutscene's first sprite file did not load */
    REAPER_STOP_SECOND_SPRITES = 0x3517b,    /* its second sprite file did not load */
    REAPER_STOP_BOX = 0x3519b,               /* box.vga */
    REAPER_STOP_LINE = 0x3525d,
    REAPER_STOP_STAGE = 0x34faa              /* a resume in no stage of the cutscene */
};
/* The Reaper's frame coordinate tables of the image (phase77/cutscene-tables.txt): 57CA:1Ch*w and 57D0:1Ch+1Ch*w
 * by world, 57CE:0 and 57D0:0. */
static const int16_t reaper_x0[2][14] = {
        {0, -10, -20, -30, -40, -49, -49, -49, -49, -49, -49, -49, -49, -49},
        {0, 2, 6, 8, 11, 14, 17, 21, 27, 35, 43, 51, 60, 67}};
static const int16_t reaper_y[2][14] = {{0, 12, 25, 37, 45, 50, 56, 61, 67, 72, 78, 83, 89, 94},
                                        {0, 6, 12, 17, 23, 29, 36, 42, 49, 54, 61, 68, 73, 77}};
static const int16_t reaper_x1[14] = {160, 152, 146, 138, 131, 124, 117,
                                      111, 107, 105, 103, 101, 100, 97};
static const int16_t reaper_x2[14] = {0, -3, -4, -7, -9, -11, -13, -14, -13, -10, -7, -4, 0, -1};
/* The frames of those tables: the approach runs them from the last to the first, the retreat back. */
enum { REAPER_STEPS = 14 };
/* The towers' keys by world: on the map, holding its key, the end takes the party to that tower. Clouds' CS:1C9A
 * (35084): two of its towers (from maps 7 and 0Dh) are Darkside's special maps 71h and 75h, entered on their other
 * side (96E2 = 1); Darkside's CS:1CAE. */
static const cut_keyed_destination reaper_keys[2][5] = {
        {{7, 0xf0a5, {0x71, 7, 4, XEEN_NORTH}},
         {0xc, 0xf07a, {0x37, 3, 8, XEEN_EAST}},
         {0xd, 0xf0a4, {0x75, 7, 4, XEEN_NORTH}},
         {0xf, 0xf079, {0x3b, 0xb, 8, XEEN_WEST}},
         {0x18, 0xf078, {0x33, 7, 0xc, XEEN_SOUTH}}},
        {{3, 0xf09f, {0x39, 0xb, 8, XEEN_WEST}},
         {0xd, 0xf0a2, {0x45, 7, 4, XEEN_NORTH}},
         {0x10, 0xf0a0, {0x3d, 7, 0xc, XEEN_SOUTH}},
         {0x17, 0xf0a1, {0x41, 3, 8, XEEN_EAST}},
         {0x1d, 0xf0a3, {0x35, 0xb, 8, XEEN_WEST}}}};
/* Begins the Reaper's cutscene (after the location's setup): the mouse reset, the resident resources unloaded (DOS
 * memory), the button set pushed, the cutscene's lines loaded and the subtitle position 0. Where the party goes: on
 * Darkside, holding the map's key (one game flag per map), to that tower's destination; on Clouds likewise by its
 * own table (two of them into Darkside's special maps; reaper_keys); without the key the party stays where it is. Then the
 * tower's sprites, the saved screen, the subtitle clock marked, the subtitle box, the voice and the spoken line (2
 * with the key, else 3) with its length, and the 14 approach frames. Stops when a resource is missing.
 * Original: 34FAA (after 277E5): 4E231, 33CD8, 190ED/1919B, 2B632(special.bin), F9A4 = 0; the key tables CS:1CAE and
 * CS:1C9A (reaper_keys); tower1/tower2.zom (tower.vga/freap.vga), 186B2, 4EA5D, box.vga into FAF6, A827. */
xeen_status reaper_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned w = ds[XEEN_DS_WORLD] ? 1u : 0u, i;
    int len;
    uint16_t map = ds[XEEN_DS_PARTY_MAP];
    xeen_presentation_trace_extra("cutmark reaper"); /* Evidence: the handler's entry */
    g->cut_kind = LOCATION_REAPER;
    cut_start(g, map);
    if (cut_keyed_dest(g, map, reaper_keys[w], 5u) && !w && g->cut_dest[0] >= 113u) { /* 71h, 75h */
        ds[XEEN_DS_MAP_SIDE] = 1;
    }
    for (i = 0; i < 2u; ++i) {
        uint16_t name = xeen_read16(m, (uint16_t)(XEEN_DS_CUTSCENE_FILES + 8u * i + 4u * w));
        if (!sprite_load(g, ds_text(g, name), &g->cut_sprites[i])) {
            return cut_stop(g, i ? REAPER_STOP_SECOND_SPRITES : REAPER_STOP_FIRST_SPRITES);
        }
    }
    exit_save(g); /* 186B2(8C90); 4EA5D: cut_resume */
    {
        uint16_t h = 0;
        if (!sprite_load(g, ds_text(g, XEEN_TEXT_CUTSCENE_BOX), &h)) {
            return cut_stop(g, REAPER_STOP_BOX);
        }
        xeen_write16(m, XEEN_DS_SUBTITLE_BOX, h);
    }
    g->cut_voice = 0; /* BP-0C */
    if (g->cut_key && w) {
        location_voc_load(g, ds_text(g, XEEN_TEXT_REAPER_VOICE));
        g->cut_voice = g->voc_kept;
    } /* howdid1.voc */
    if (!w) {
        location_voc_load(g, ds_text(g, g->cut_key ? XEEN_TEXT_REAPER_VOICE_12 : XEEN_TEXT_REAPER_VOICE_14));
        g->cut_voice = g->voc_kept;
    } /* reaper12.voc, reaper14.voc */
    if ((len = cut_line(g, g->cut_key ? 2u : 3u)) < 0) {
        return cut_stop(g, REAPER_STOP_LINE);
    }
    g->cut_len = (uint16_t)len;
    g->cut_text[0] = 0;
    g->cut_si = REAPER_STEPS - 1;
    return cut_step(g, CUT_R_APPROACH);
}
/* One talk frame of the Reaper: the clock marked, then a random mouth frame 0..4: on Clouds over frame 0; on
 * Darkside on both halves (the right half's frame + 5 at x 160). Original: 35545 / 356AB (4EA0D, 504DA(0, 4)). */
static xeen_status reaper_mouth(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status s;
    uint16_t h = g->cut_sprites[1], r;
    if ((s = cut_sample(g, XEEN_TIMER_FRAME_MARK, bios, user)) != XEEN_OK) {
        return s;
    }
    if (!g->movement.ds[XEEN_DS_WORLD]) {
        cut_draw(g, h, 0, 0, 0, 0, 0);
        r = cut_random(g, 0, 4);
        cut_draw(g, h, r, 0, 0, 0, 0);
    } else {
        r = cut_random(g, 0, 4);
        cut_draw(g, h, r, 0, 0, 0, 0);
        cut_draw(g, h, (uint16_t)(r + 5u), 0xa0, 0, 0, 0);
    }
    return XEEN_OK;
}
/* The Reaper's closed mouth: its frame 0, on Darkside also frame 5 at x 160; then the screen shown.
 * Original: 355FB, 35752. */
static void reaper_closed(xeen_game *g)
{
    cut_draw(g, g->cut_sprites[1], 0, 0, 0, 0, 0);
    if (g->movement.ds[XEEN_DS_WORLD]) {
        cut_draw(g, g->cut_sprites[1], 5, 0xa0, 0, 0, 0);
    }
    (void)exit_present(g);
}
/* The tower behind (the slide and the talk start): tower1 frames 0, 1 at
 * A0h and the day/night frame 8C92 + 2 (Darkside). */
static void reaper_tower(xeen_game *g)
{
    uint16_t h = g->cut_sprites[0];
    cut_draw(g, h, 0, 0, 0, 0, 0);
    if (g->movement.ds[XEEN_DS_WORLD]) {
        cut_draw(g, h, 1, 0xa0, 0, 0, 0);
        cut_draw(g, h, (uint16_t)(xeen_read16(&g->movement, XEEN_DS_NIGHT) + 2u), 0, 0, 0, 0);
    }
}
/* The approach and retreat frames: tower1 frame 0 (and 1, 8C92 + 2) at the
 * world's tables, scale STEP (the step the original keeps in SI). */
static void reaper_far(xeen_game *g, int step)
{
    uint16_t h = g->cut_sprites[0];
    unsigned w = g->movement.ds[XEEN_DS_WORLD] ? 1u : 0u;
    cut_draw(g, h, 0, reaper_x0[w][step], reaper_y[w][step], step, 0);
    if (w) {
        cut_draw(g, h, 1, reaper_x1[step], reaper_y[w][step], step, 0);
        cut_draw(g, h, (uint16_t)(xeen_read16(&g->movement, XEEN_DS_NIGHT) + 2u), reaper_x2[step],
                 reaper_y[w][step], step, 0);
    }
}
/* The talk's start after either slide (Darkside's from the left, Clouds' from the right): the volume 30h, the tower,
 * the first voice started when the entry loaded one (BP-0C: the voice model), then the talk loop.
 * Original: 354D8 (187CB(30h); 187E9(BP-0C, 0)). */
static xeen_status reaper_talk_start(xeen_game *g)
{
    cut_volume(g, CUT_SONG_VOLUME_TALK);
    reaper_tower(g);
    if (g->cut_voice) {
        voc_start(g, REAPER_TOWER_VOICE_CALL);
    }
    return cut_step(g, CUT_R_TALK);
}
/* The Reaper's encounter after its setup, one function per movement; cut_si counts each movement's frames.
 *   approach  CUT_R_APPROACH .. _POLL: the tower (and the Reaper) grows through REAPER_STEPS frames, farthest to
 *             nearest, one frame tick each;
 *   slide     Darkside CUT_R_SLIDE .. _POLL: the Reaper slides in from the left (x -200, +16 a frame, one frame tick
 *             each); Clouds CUT_R_CSLIDE .. _POLL: from the right (x 200, -16 a frame, no clock read);
 *   talk      CUT_R_TALK .. _TICK: at the talk's volume the first voice (if the setup loaded one) and the subtitle,
 *             two frame ticks a frame, until both are done; then the closed mouth;
 *   answer    CUT_R_PAUSE_TICK, CUT_R_TALK2 .. _TICK: after seven frame ticks the answer's voice (by the key and the
 *             world), its frames while it plays, two frame ticks each; with the key the party passes, else
 *   retreat   CUT_R_RETREAT .. _POLL: the tower shrinks back through its frames, and the cutscene ends.
 * The song volume stays at the talk's through the retreat; cut_end restores it (unlike the Sphinx and the guards). */
enum { REAPER_SLIDE_START = 200, REAPER_SLIDE_STEP = 16 };
/* Darkside's slide frame: the tower, then the Reaper's two halves at X (the right one 160 further), mirrored
 * (flags 800h), shown. Original: 353D4. */
static void reaper_slide_frame(xeen_game *g, int x)
{
    reaper_tower(g);
    cut_draw(g, g->cut_sprites[1], 0, x, 0, 0, 0x800);
    cut_draw(g, g->cut_sprites[1], 5, x + 0xa0, 0, 0, 0x800);
    (void)exit_present(g);
}
/* Clouds' slide frame: the tower's frame 0, then the Reaper's frame 0 at X, mirrored; the original draws that frame
 * twice (354A5 and 354BC, the same arguments), kept for the draw trace. Original: 3547F. */
static void reaper_clouds_slide_frame(xeen_game *g, int x)
{
    cut_draw(g, g->cut_sprites[0], 0, 0, 0, 0, 0);
    cut_draw(g, g->cut_sprites[1], 0, x, 0, 0, 0x800);
    cut_draw(g, g->cut_sprites[1], 0, x, 0, 0, 0x800);
    (void)exit_present(g);
}
/* Slide: the Reaper comes in, on Darkside from the left with a frame tick a frame, on Clouds from the right without
 * a clock read; both end at the talk's start. */
static xeen_status reaper_slide(xeen_game *g, int stage, xeen_timer_bios bios, void *user)
{
    xeen_status s;
    switch (stage) {
        case CUT_R_SLIDE:
            if ((s = cut_sample(g, XEEN_TIMER_FRAME_MARK, bios, user)) != XEEN_OK) {
                return s;
            }
            reaper_slide_frame(g, g->cut_si);
            return cut_step(g, CUT_R_SLIDE_TICK);
        case CUT_R_SLIDE_TICK:
            return cut_frame_stage(g, CUT_R_SLIDE_TICK, 1, CUT_R_SLIDE_POLL, bios, user);
        case CUT_R_SLIDE_POLL:
            cut_poll(g, CUT_R_SLIDE_POLL);
            if ((g->cut_si += REAPER_SLIDE_STEP) < 0) {
                return cut_step(g, CUT_R_SLIDE);
            }
            return reaper_talk_start(g);
        case CUT_R_CSLIDE:
            reaper_clouds_slide_frame(g, g->cut_si);
            return cut_step(g, CUT_R_CSLIDE_POLL);
        default: /* CUT_R_CSLIDE_POLL */
            cut_poll(g, CUT_R_CSLIDE_POLL);
            if ((g->cut_si -= REAPER_SLIDE_STEP) >= 0) {
                return cut_step(g, CUT_R_CSLIDE);
            }
            return reaper_talk_start(g);
    }
}
/* Talk: mouth frames (with the subtitle's own step while it scrolls), each polled and then held two frame ticks,
 * while the voice plays or the subtitle runs; then the closed mouth and the pause's clock mark. */
static xeen_status reaper_talk(xeen_game *g, int stage, xeen_timer_bios bios, void *user)
{
    xeen_status s;
    switch (stage) {
        case CUT_R_TALK:
            if ((s = reaper_mouth(g, bios, user)) != XEEN_OK) {
                return s;
            }
            return cut_talk_next(g, CUT_R_TALK_SUB, CUT_R_TALK_POLL);
        case CUT_R_TALK_SUB:
            return cut_subtitle_stage(g, CUT_R_TALK_POLL, bios, user);
        case CUT_R_TALK_POLL:
            cut_poll(g, CUT_R_TALK_POLL);
            return cut_step(g, CUT_R_TALK_TICK);
        default: /* CUT_R_TALK_TICK */
            if (!cut_ticks(g, XEEN_TIMER_FRAME_ELAPSED, CUT_R_TALK_TICK, 2, bios, user, &s)) {
                return s;
            }
            if (cut_talking(g, REAPER_TALK_MOUTH_CALL)) {
                return cut_step(g, CUT_R_TALK);
            }
            reaper_closed(g);
            if ((s = cut_sample(g, XEEN_TIMER_FRAME_MARK, bios, user)) != XEEN_OK) {
                return s;
            }
            return cut_step(g, CUT_R_PAUSE_TICK);
    }
}
/* The answer's voice: with the key Darkside's goin1.voc or Clouds' reaper13.voc, without it needkey1.voc or
 * reaper15.voc. Original: 3565B .. 35693 (DI the key, DS:94 the world), into 18975. */
static uint16_t reaper_answer_voice(const xeen_game *g, unsigned w)
{
    if (g->cut_key) {
        return w ? XEEN_TEXT_REAPER_GOIN_VOICE : XEEN_TEXT_REAPER_VOICE_13;
    }
    return w ? XEEN_TEXT_REAPER_NEED_KEY_VOICE : XEEN_TEXT_REAPER_VOICE_15;
}
/* Answer: after seven frame ticks the answer's voice is loaded and started; mouth frames, each shown, polled and
 * held two frame ticks, while it plays (no subtitle); then the closed mouth, and the party passes or the Reaper
 * retreats. */
static xeen_status reaper_answer(xeen_game *g, unsigned w, int stage, xeen_timer_bios bios, void *user)
{
    xeen_status s;
    switch (stage) {
        case CUT_R_PAUSE_TICK:
            if (!cut_ticks(g, XEEN_TIMER_FRAME_ELAPSED, CUT_R_PAUSE_TICK, 7, bios, user, &s)) {
                return s;
            }
            location_voc_load(g, ds_text(g, reaper_answer_voice(g, w)));
            voc_start(g, REAPER_ANSWER_VOICE_CALL);
            return cut_step(g, CUT_R_TALK2);
        case CUT_R_TALK2:
            if ((s = reaper_mouth(g, bios, user)) != XEEN_OK) {
                return s;
            }
            (void)exit_present(g);
            return cut_step(g, CUT_R_TALK2_POLL);
        case CUT_R_TALK2_POLL:
            cut_poll(g, CUT_R_TALK2_POLL);
            return cut_step(g, CUT_R_TALK2_TICK);
        default: /* CUT_R_TALK2_TICK */
            if (!cut_ticks(g, XEEN_TIMER_FRAME_ELAPSED, CUT_R_TALK2_TICK, 2, bios, user, &s)) {
                return s;
            }
            if (voice_busy(g, REAPER_ANSWER_VOICE_QUERY_CALL)) {
                return cut_step(g, CUT_R_TALK2);
            }
            reaper_closed(g);
            return cut_pass_or_retreat(g, CUT_END_REAPER, CUT_R_RETREAT);
    }
}
/* Resumes the Reaper's cutscene at its stage after a wait or key (the movements above); a stage outside them stops.
 * Original: 34FAA's frame loops, 35272 .. 358E1. */
xeen_status reaper_resume(xeen_game *g, xeen_timer_bios bios, void *user)
{
    unsigned w = g->movement.ds[XEEN_DS_WORLD] ? 1u : 0u;
    int stage = g->cut_stage;
    xeen_status s;
    switch (stage) {
        case CUT_R_APPROACH:
            if ((s = cut_sample(g, XEEN_TIMER_FRAME_MARK, bios, user)) != XEEN_OK) {
                return s;
            }
            reaper_far(g, g->cut_si);
            (void)exit_present(g);
            return cut_step(g, CUT_R_APPROACH_TICK);
        case CUT_R_APPROACH_TICK:
            return cut_frame_stage(g, CUT_R_APPROACH_TICK, 1, CUT_R_APPROACH_POLL, bios, user);
        case CUT_R_APPROACH_POLL:
            cut_poll(g, CUT_R_APPROACH_POLL);
            if (--g->cut_si >= 0) {
                return cut_step(g, CUT_R_APPROACH);
            }
            g->cut_si = w ? -REAPER_SLIDE_START : REAPER_SLIDE_START;
            return cut_step(g, w ? CUT_R_SLIDE : CUT_R_CSLIDE);
        case CUT_R_SLIDE:
        case CUT_R_SLIDE_TICK:
        case CUT_R_SLIDE_POLL:
        case CUT_R_CSLIDE:
        case CUT_R_CSLIDE_POLL:
            return reaper_slide(g, stage, bios, user);
        case CUT_R_TALK:
        case CUT_R_TALK_SUB:
        case CUT_R_TALK_POLL:
        case CUT_R_TALK_TICK:
            return reaper_talk(g, stage, bios, user);
        case CUT_R_PAUSE_TICK:
        case CUT_R_TALK2:
        case CUT_R_TALK2_POLL:
        case CUT_R_TALK2_TICK:
            return reaper_answer(g, w, stage, bios, user);
        case CUT_R_RETREAT:
            exit_restore(g);
            reaper_far(g, g->cut_si);
            (void)exit_present(g);
            return cut_step(g, CUT_R_RETREAT_POLL);
        case CUT_R_RETREAT_POLL:
            cut_poll(g, CUT_R_RETREAT_POLL);
            if (++g->cut_si < REAPER_STEPS) {
                return cut_step(g, CUT_R_RETREAT);
            }
            return cut_retreat_done(g, CUT_END_REAPER);
        default:
            return cut_stop(g, REAPER_STOP_STAGE);
    }
}
