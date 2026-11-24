/*
 * The sphinx's cutscene (Phase 248, from game_cutscene.c): its start, its far frame, its end and tail, and its
 * frame loop (sphinx_resume). The shared frame-loop services are game_cutscene.c's.
 */
#include "game_cutscene.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The original calls of the voice driver (187E9) and the CD status (47F3D) in this unit, each by the call's
 * return address: the audio model records which call asked (traces name them). Not stops, not waits. */
enum {
    SPHINX_STEP_VOICE_CALL = 0x341f0,/* a step: voice slot SI (0..2) */
    SPHINX_TALK_MOUTH_CALL = 0x3425f/* the talk's mouth follows the voice */
};
/*
 * Where native play stops when the sphinx's cutscene cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json).
 */
enum {
    SPHINX_STOP_SPRITES = 0x34001,           /* sphinx.vga */
    SPHINX_STOP_BOX = 0x34017,               /* box.vga */
    SPHINX_STOP_LINE = 0x340e2,
    SPHINX_STOP_TRAVEL = 0x34397,
    SPHINX_STOP_STAGE = 0x33fcb
};
/* Location kind 11, 33FCB (after 277E5's setup), the Sphinx:
 * 4E231, 33CD8, 190ED/1919B, F9A4 = 0, 2B632(special.bin), sphinx.vga,
 * 4EA5D, box.vga into FAF6; the destination by map: 2: F0AA -> 7Dh (7,6)
 * N in Darkside (96E2 = 1: a special map); 5: F07B -> 52h (7,5) N (DI =
 * 1); else the map in place and the position (DI = 0). The voice
 * (sphinx10.voc with DI, else sphinx13.voc) and the line (6, else 7) into
 * A827, its length, 186B2, and nine approach frames (SI 8 down to 0: the
 * restored screen, sphinx.vga frames 0 and 1 at 57F3/57F5[si], 57F7[si],
 * scale SI, 50BF6(0), 189E3) with no clock read; 187CB(30h). Then DI + 2
 * talk loops (SI 1: sphinx11.voc / sphinx14.voc, SI 2: sphinx12.voc): frames
 * 0, 1 at A0h and 504DA(2, 10) (AL 0: 4AC92's return), 185E3, 189E3, until
 * F9A4 is 0 (the silent voice: 187E9 reports none); with ED9A clear or 8D38
 * below 2 one loop (SI = 0Ah); frames 0 and 1 again, 50BF6(0). 187CB(5Fh);
 * without DI the retreat (SI 0..8). The end: 4AA3E, 33D36, 19E82, 4E99F,
 * the position, 2ABB8(0), 4ADB1(2A5C), 1B503(0) (cut_after_scene). */
static const int16_t sphinx_x0[9] = {0, -5, -10, -15, -20, -17, -12, -7, 0},
                     sphinx_x1[9] = {160, 145, 130, 115, 100, 93, 88, 83, 80},
                     sphinx_y[9] = {0, 0, 0, 6, 11, 16, 20, 23, 28};
/* The frames of those tables: the approach runs them from the last to the first, the retreat back. */
enum { SPHINX_STEPS = 9 };
/* The Sphinx's keys: on map 2 with flag F0AAh the end takes the party to map 7Dh, entered on its other side
 * (sphinx_begin); on map 5 with F07Bh to map 52h. */
static const cut_keyed_destination sphinx_keys[2] = {{2, 0xf0aa, {0x7d, 7, 6, XEEN_NORTH}},
                                                     {5, 0xf07b, {0x52, 7, 5, XEEN_NORTH}}};
xeen_status sphinx_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    int len;
    uint16_t map = ds[XEEN_DS_PARTY_MAP], h = 0;
    xeen_presentation_trace_extra("cutmark sphinx"); /* Evidence: the handler's entry */
    g->cut_kind = LOCATION_SPHINX;
    cut_enter(g);
    if (!sprite_load(g, ds_text(g, XEEN_TEXT_SPHINX_SPRITES), &g->cut_sprites[0])) {
        return cut_stop(g, SPHINX_STOP_SPRITES);
    }
    if (!sprite_load(g, ds_text(g, XEEN_TEXT_CUTSCENE_BOX), &h)) {
        return cut_stop(g, SPHINX_STOP_BOX); /* 4EA5D: cut_resume */
    }
    xeen_write16(m, XEEN_DS_SUBTITLE_BOX, h);
    cut_dest_here(g, map);
    if (cut_keyed_dest(g, map, sphinx_keys, 2u) && map == 2u) {
        ds[XEEN_DS_MAP_SIDE] = 1;
    }
    cut_voc_load(g, 0,
                 ds_text(g, g->cut_key ? XEEN_TEXT_SPHINX_VOICE_10 : XEEN_TEXT_SPHINX_VOICE_13)); /* sphinx10.voc, sphinx13.voc: BP-0C */
    if ((len = cut_line(g, g->cut_key ? 6u : 7u)) < 0) {
        return cut_stop(g, SPHINX_STOP_LINE);
    }
    g->cut_len = (uint16_t)len;
    g->cut_text[0] = 0;
    exit_save(g);
    g->cut_si = SPHINX_STEPS - 1;
    return cut_step(g, CUT_S_APPROACH);
}
/** Draws the Sphinx at distance STEP (the original's SI) and presents the frame. */
static void sphinx_far(xeen_game *g, int step)
{
    exit_restore(g);
    cut_draw(g, g->cut_sprites[0], 0, sphinx_x0[step], sphinx_y[step], step, 0);
    cut_draw(g, g->cut_sprites[0], 1, sphinx_x1[step], sphinx_y[step], step, 0);
    (void)exit_present(g);
}
xeen_status sphinx_end_tail(xeen_game *g)
{
    xeen_presentation_trace_extra("cutload"); /* Evidence: 19E82 */
    if (map_reload(g, g->cut_dest[0]) != XR_OK) {
        return cut_stop(g, SPHINX_STOP_TRAVEL);
    }
    return cut_arrive(g);
}
/* The Sphinx's encounter after its setup; cut_si counts the frames and then the talks. No stage reads a clock: each
 * flip's vertical retrace paces the frames.
 *   approach  CUT_S_APPROACH .. _POLL: the Sphinx grows through SPHINX_STEPS frames, farthest to nearest; then the
 *             song is lowered;
 *   step      CUT_S_STEP: talk cut_si's voice (slot cut_si: 0 the setup's sphinx10/13, 1 sphinx11/14, 2 sphinx12);
 *   talk      CUT_S_TALK .. _POLL: mouth frames while the voice plays or the subtitle runs; with the key three talks,
 *             without it two (without the voice only the first, cut_talk_ended); then the song volume restored, and
 *             with the key the party passes, else
 *   retreat   CUT_S_RETREAT .. _POLL: the Sphinx shrinks back through its frames, and the cutscene ends. */
/* Step: talk cut_si's voice loaded into its slot (1: sphinx11.voc with the key, sphinx14.voc without; 2:
 * sphinx12.voc; 0 holds the setup's) and started. Original: 34185 .. 341EB (187E9(BP-0C[SI], 0)). */
static void sphinx_step_voice(xeen_game *g)
{
    if (g->cut_si == 1) {
        cut_voc_load(g, 1, ds_text(g, g->cut_key ? XEEN_TEXT_SPHINX_VOICE_11 : XEEN_TEXT_SPHINX_VOICE_14));
    }
    if (g->cut_si == 2) {
        cut_voc_load(g, 2, ds_text(g, XEEN_TEXT_SPHINX_VOICE));
    }
    if (g->cut_si >= 0 && g->cut_si < 3) {
        cut_voc_start(g, (unsigned)g->cut_si, SPHINX_STEP_VOICE_CALL);
    }
}
/* A talk's last frame has been polled: the still frame shown, then the next talk, or after the last one the song
 * volume restored and the party passes or the Sphinx retreats. */
static xeen_status sphinx_talk_done(xeen_game *g)
{
    cut_talk_ended(g);
    cut_draw_still_frame(g);
    (void)exit_present(g);
    if (++g->cut_si < (g->cut_key ? 3 : 2)) {
        return cut_step(g, CUT_S_STEP);
    }
    cut_volume(g, CUT_SONG_VOLUME_END);
    return cut_pass_or_retreat(g, CUT_END_SPHINX, CUT_S_RETREAT);
}
/* Resumes the Sphinx's cutscene at its stage after a wait or key (the movements above); a stage outside them stops.
 * Original: 33FCB's frame loops, 34100 .. 34348. */
xeen_status sphinx_resume(xeen_game *g, xeen_timer_bios bios, void *user)
{
    switch (g->cut_stage) {
        case CUT_S_APPROACH:
            sphinx_far(g, g->cut_si);
            return cut_step(g, CUT_S_APPROACH_POLL);
        case CUT_S_APPROACH_POLL:
            cut_poll(g, CUT_S_APPROACH_POLL);
            if (--g->cut_si >= 0) {
                return cut_step(g, CUT_S_APPROACH);
            }
            cut_volume(g, CUT_SONG_VOLUME_TALK);
            g->cut_si = 0;
            return cut_step(g, CUT_S_STEP);
        case CUT_S_STEP:
            sphinx_step_voice(g);
            return cut_step(g, CUT_S_TALK);
        case CUT_S_TALK:
            cut_draw_still_frame(g);
            cut_draw(g, g->cut_sprites[0], cut_random(g, 2, 10), 0, 0, 0, 0);
            return cut_talk_next(g, CUT_S_TALK_SUB, CUT_S_TALK_POLL);
        case CUT_S_TALK_SUB:
            return cut_subtitle_stage(g, CUT_S_TALK_POLL, bios, user);
        case CUT_S_TALK_POLL:
            cut_poll(g, CUT_S_TALK_POLL);
            if (cut_talking(g, SPHINX_TALK_MOUTH_CALL)) {
                return cut_step(g, CUT_S_TALK);
            }
            return sphinx_talk_done(g);
        case CUT_S_RETREAT:
            sphinx_far(g, g->cut_si);
            return cut_step(g, CUT_S_RETREAT_POLL);
        case CUT_S_RETREAT_POLL:
            cut_poll(g, CUT_S_RETREAT_POLL);
            if (++g->cut_si < SPHINX_STEPS) {
                return cut_step(g, CUT_S_RETREAT);
            }
            return cut_retreat_done(g, CUT_END_SPHINX);
        default:
            return cut_stop(g, SPHINX_STOP_STAGE);
    }
}
