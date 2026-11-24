/*
 * The town gatekeepers' cutscene (Phase 248, from game_cutscene.c): location kinds 10 and 13 (35992), its frame
 * coordinate and walk tables, its start, its mouth frame and its frame loop (gate_resume and its movements: the
 * walk, the steps, the talk). The shared frame-loop services are game_cutscene.c's.
 */
#include "game_cutscene.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The original calls of the voice driver (187E9) and the CD status (47F3D) in this unit, each by the call's
 * return address: the audio model records which call asked (traces name them). Not stops, not waits. */
enum {
    GATE_TALK_MOUTH_CALL = 0x35fad,/* the talk's mouth follows the voice */
    GATE_TALK_VOICE_CALL = 0x35f3e/* the gatekeeper's voice */
};
/*
 * Where native play stops when a gatekeeper's cutscene cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json). The gatekeepers' own sprite files stop at 35B20 + 1Fh x the file.
 */
enum {
    GATE_STOP_MAP = 0x35b0c,                 /* a gatekeeper on none of Clouds maps 0Eh, 12h and 17h */
    GATE_STOP_BOX = 0x35b9a,                 /* box.vga */
    GATE_STOP_LINE = 0x35c03,
    GATE_STOP_STAGE = 0x35992,
    /* the first of the three sprite loads; the original runs them one after another, each 1Fh bytes of code past
     * the last, so sprite I stops at this address + 1Fh x I (stops_variable.json lists all three) */
    GATE_STOP_SPRITE = 0x35b20
};
/* The gatekeeper's frame coordinate tables of the image (phase77/cutscene-tables.txt): 57DE:20h/54h/88h + 1Ah*w,
 * 57DE:0, and the walk's 57D0:54h/94h + 20h*w. */
static const int16_t gate_x0[2][13] = {{0, -5, -7, -8, -11, -9, -3, 1, 6, 10, 15, 18, 23},
                                       {0, 4, 6, 8, 11, 12, 15, 17, 19, 22, 25, 0, 0}};
static const int16_t gate_x1[2][13] = {{160, 145, 133, 122, 109, 101, 97, 91, 86, 80, 75, 68, 63},
                                       {160, 154, 146, 138, 131, 122, 115, 107, 99, 92, 85, 0, 0}};
static const int16_t gate_y[2][13] = {{0, 0, 4, 9, 13, 15, 20, 24, 30, 37, 45, 51, 58},
                                      {0, 12, 25, 36, 38, 40, 41, 42, 44, 45, 50, 0, 0}};
static const int16_t gate_x2[13] = {0, -1, -4, -7, -9, -13, -15, -18, -21, -23, -25, 0, 0};
static const int16_t gate_walk_x[2][16] = {
        {0, -2, -4, -6, -8, -10, -12, -14, -16, -18, -20, -20, -20, -20, -20, -20},
        {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150}};
static const int16_t gate_walk_y[2][16] = {
        {0, 12, 25, 37, 50, 62, 75, 87, 100, 112, 125, 137, 150, 162, 175, 187},
        {0, 12, 25, 37, 50, 62, 75, 87, 100, 112, 125, 137, 150, 162, 175, 186}};
/* The frames of the approach tables each world uses (Clouds 13, Darkside 11) and of the walk: each runs them from the
 * last to the first. The talks: Clouds' keeper speaks three times, Darkside's twice. */
static const int gate_steps[2] = {13, 11}, gate_talks[2] = {3, 2};
enum { GATE_WALK_STEPS = 16 };
/* Darkside's gates (CS:23E0, by map): with the map's pass (one game flag per map) the gatekeeper admits the party to
 * that gate's destination. */
static const cut_keyed_destination gate_passes[5] = {{4, 0xf09a, {0x1d, 15, 31, XEEN_SOUTH}},
                                                     {6, 0xf09d, {0x23, 15, 8, XEEN_WEST}},
                                                     {0x13, 0xf09b, {0x1f, 31, 16, XEEN_WEST}},
                                                     {0x16, 0xf09c, {0x21, 0, 3, XEEN_EAST}},
                                                     {0x62, 0xf09e, {0x25, 7, 0, XEEN_NORTH}}};
/* Clouds' castle gates (359C6), which admit without a pass: map 0Eh to 25h; map 12h from x 9 to 23h, else to 24h;
 * map 17h from x 5 to 21h, else to 22h. NULL on another map: the original leaves DI and BP-10h..14h unset there (no
 * shipped record has one), not reconstructed. */
static const cut_destination *gate_castle(uint16_t map, uint8_t x)
{
    static const cut_destination to_25 = {0x25, 1, 4, XEEN_EAST}, to_23 = {0x23, 1, 0xc, XEEN_EAST},
                                 to_24 = {0x24, 7, 1, XEEN_NORTH}, to_21 = {0x21, 7, 1, XEEN_NORTH},
                                 to_22 = {0x22, 7, 0x1e, XEEN_SOUTH};
    switch (map) {
        case 0xe:
            return &to_25;
        case 0x12:
            return x == 9u ? &to_23 : &to_24;
        case 0x17:
            return x == 5u ? &to_21 : &to_22;
        default:
            return NULL;
    }
}
/* Begins the gatekeeper's cutscene (after the location's setup): the mouse reset, the resident resources unloaded
 * (DOS memory), the button set pushed, the cutscene's lines loaded and the subtitle position 0. Where the party goes:
 * on Darkside, holding the map's pass, to that gate's destination (gate_passes); without it (or on another map) the
 * party stays; Clouds' castle gates admit without a pass (gate_castle), and on another Clouds map the cutscene stops
 * before it starts. Then the dwarf's sprites, the voice, the subtitle clock marked, the box, the spoken line (7 with
 * the pass, else 8; Clouds 1) with its length, the saved screen and the approach. Stops when a resource is missing.
 * Original: 35992 (after 277E5): 4E231, 33CD8, 190ED/1919B, 2B632, F9A4 = 0; Darkside's CS:23E0 (BP-E = 1); Clouds'
 * 359C6; town1..3.zom (dwarf1/3/2.vga), pass2.voc / dwarf10.voc, 4EA5D, box.vga, 186B2, the approach (SI 12 - 2w). */
xeen_status gate_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned w = ds[XEEN_DS_WORLD] ? 1u : 0u, i;
    int len;
    uint16_t map = ds[XEEN_DS_PARTY_MAP];
    xeen_presentation_trace_extra("cutmark gate"); /* Evidence: the handler's entry */
    const cut_destination *castle = NULL;
    if (!w && !(castle = gate_castle(map, ds[XEEN_DS_PARTY_X]))) {
        return cut_stop(g, GATE_STOP_MAP);
    }
    g->cut_kind = (uint8_t)g->location_kind;
    cut_start(g, map);
    if (castle) {
        cut_set_dest(g, castle); /* BP-E is read on Darkside only */
    } else {
        (void)cut_keyed_dest(g, map, gate_passes, 5u);
    }
    for (i = 0; i < 3u; ++i) {
        uint16_t name = xeen_read16(m, (uint16_t)(XEEN_DS_CUTSCENE_FILES + 4u * 8u + 4u * i + 12u * w));
        if (!sprite_load(g, ds_text(g, name), &g->cut_sprites[i])) {
            return cut_stop(g, GATE_STOP_SPRITE + 0x1fu * i);
        }
    }
    location_voc_load(
            g,
            ds_text(g,
                    w ? XEEN_TEXT_GATE_PASS_VOICE
                      : XEEN_TEXT_GATE_DWARF_VOICE)); /* pass2.voc, dwarf10.voc (BP-0C; started at 35F39); 4EA5D: cut_resume */
    {
        uint16_t h = 0;
        if (!sprite_load(g, ds_text(g, XEEN_TEXT_CUTSCENE_BOX), &h)) {
            return cut_stop(g, GATE_STOP_BOX);
        }
        xeen_write16(m, XEEN_DS_SUBTITLE_BOX, h);
    }
    if ((len = cut_line(g, !w ? 1u : g->cut_key ? 7u : 8u)) < 0) {
        return cut_stop(g, GATE_STOP_LINE);
    }
    g->cut_len = (uint16_t)len;
    g->cut_text[0] = 0;
    exit_save(g);
    g->cut_si = gate_steps[w] - 1;
    return cut_step(g, CUT_G_APPROACH);
}
/* The gatekeeper's mouth: the second sprite's frame 0 and a random frame of the third (0..9, 0..8 on Darkside).
 * Original: 35F41 (4EA35; 504DA(0, 9 - w)). */
static void gate_mouth(xeen_game *g)
{
    cut_draw(g, g->cut_sprites[1], 0, 0, 0, 0, 0);
    cut_draw(g, g->cut_sprites[2],
             cut_random(g, 0, (uint16_t)(9u - (g->movement.ds[XEEN_DS_WORLD] ? 1u : 0u))), 0, 0, 0, 0);
}
/* The talk's start, which each of the keeper's steps reaches with its voice loaded (BP-0C: the entry's pass2/dwarf10,
 * a step's dwarf11/dwarf12, the last step's ok2/back2): the frame clock marked (4EA0D), the voice started
 * (187E9(BP-0C, 0): the voice model), then the talk loop. Original: 35F2F. */
static xeen_status gate_talk_start(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status s;
    if ((s = cut_sample(g, XEEN_TIMER_FRAME_MARK, bios, user)) != XEEN_OK) {
        return s;
    }
    voc_start(g, GATE_TALK_VOICE_CALL);
    return cut_step(g, CUT_G_TALK);
}
/* The gatekeeper's encounter after its setup, one function per movement; cut_si counts each movement's frames and then
 * the talks.
 *   approach  CUT_G_APPROACH .. _POLL: the gate grows through gate_steps' frames (no clock read: each flip's vertical
 *             retrace paces it); the screen is saved;
 *   walk      CUT_G_WALK .. _POLL: the keeper walks up through GATE_WALK_STEPS frames; at the talk's volume the
 *             keeper stands at the gate;
 *   step      CUT_G_STEP .. _STEP_TICK: talk cut_si's voice is loaded (step 0 the setup's; Clouds' 1 and 2 dwarf11
 *             and dwarf12; Darkside's 1 first shows the keeper with the subtitle for seven scene ticks, then ok2 or
 *             back2 by the pass);
 *   talk      CUT_G_TALK .. _TALK_WAIT: mouth frames, two scene ticks each, while the voice plays or the subtitle runs;
 *             three frame ticks after the talk began, the next step, or after the last talk (gate_talks; without the
 *             voice the first, cut_talk_ended) the closing frame and the end. */
/* The approach frame STEP: the gate's halves (and on Darkside the day/night frame) from the restored screen, shown.
 * Original: 35C2B. */
static void gate_far(xeen_game *g, unsigned w, int step)
{
    exit_restore(g);
    cut_draw(g, g->cut_sprites[0], 0, gate_x0[w][step], gate_y[w][step], step, 0);
    cut_draw(g, g->cut_sprites[0], 1, gate_x1[w][step], gate_y[w][step], step, 0);
    if (w) {
        cut_draw(g, g->cut_sprites[0], (uint16_t)(xeen_read16(&g->movement, XEEN_DS_NIGHT) + 2u), gate_x2[step],
                 gate_y[w][step], step, 0);
    }
    (void)exit_present(g);
}
/* The keeper's frame 0 at (X, Y) with SCALE over the restored screen, shown: the walk's frames and the last pose.
 * Original: 35DAE, 35E45 (4E99F, 4AC92, 50BF6). */
static void gate_keeper(xeen_game *g, int x, int y, int scale)
{
    exit_restore(g);
    cut_draw(g, g->cut_sprites[1], 0, x, y, scale, 0);
    (void)exit_present(g);
}
/* Walk: the keeper walks up from the far end (cut_si 15 down to 0, no clock read); then the song is lowered, the
 * keeper stands at the gate and the first talk step begins. */
static xeen_status gate_walk(xeen_game *g, unsigned w, int stage)
{
    if (stage == CUT_G_WALK) {
        gate_keeper(g, gate_walk_x[w][g->cut_si], gate_walk_y[w][g->cut_si], g->cut_si);
        return cut_step(g, CUT_G_WALK_POLL);
    }
    cut_poll(g, CUT_G_WALK_POLL);
    if (--g->cut_si >= 0) {
        return cut_step(g, CUT_G_WALK);
    }
    cut_volume(g, CUT_SONG_VOLUME_TALK);
    gate_keeper(g, 0, 0, 0);
    g->cut_si = 0;
    return cut_step(g, CUT_G_STEP);
}
/* Step: talk cut_si's voice, then its talk (gate_talk_start). Darkside's step 1 is the answer to the pass: the
 * keeper and the mouth's frame 0 drawn, the subtitle's frame while it runs, seven scene ticks, then ok2.voc with the
 * pass or back2.voc without. Original: 35E73 .. 35F2F. */
static xeen_status gate_step(xeen_game *g, unsigned w, int stage, xeen_timer_bios bios, void *user)
{
    xeen_status s;
    switch (stage) {
        case CUT_G_STEP:
            if (g->cut_si == 1 && w) {
                cut_draw(g, g->cut_sprites[1], 0, 0, 0, 0, 0);
                cut_draw(g, g->cut_sprites[2], 0, 0, 0, 0, 0);
                if (cut_subtitles(g)) {
                    return cut_step(g, CUT_G_STEP_SUB);
                }
                if ((s = cut_sample(g, XEEN_TIMER_SCENE_MARK, bios, user)) != XEEN_OK) {
                    return s;
                }
                return cut_step(g, CUT_G_STEP_TICK);
            }
            if (g->cut_si == 1) {
                location_voc_load(g, ds_text(g, XEEN_TEXT_GATE_DWARF_VOICE_1)); /* dwarf11.voc */
            } else if (g->cut_si == 2) {
                location_voc_load(g, ds_text(g, XEEN_TEXT_GATE_DWARF_VOICE_2)); /* dwarf12.voc (Clouds) */
            }
            return gate_talk_start(g, bios, user);
        case CUT_G_STEP_SUB:
            if ((s = cut_subtitle_tick(g, bios, user)) != XEEN_OK) {
                return s;
            }
            if ((s = cut_sample(g, XEEN_TIMER_SCENE_MARK, bios, user)) != XEEN_OK) {
                return s;
            }
            return cut_step(g, CUT_G_STEP_TICK);
        default: /* CUT_G_STEP_TICK */
            if (!cut_ticks(g, XEEN_TIMER_SCENE_ELAPSED, CUT_G_STEP_TICK, 7, bios, user, &s)) {
                return s;
            }
            location_voc_load(g, ds_text(g, g->cut_key ? XEEN_TEXT_GATE_OK_VOICE : XEEN_TEXT_GATE_BACK_VOICE));
            return gate_talk_start(g, bios, user);
    }
}
/* Talk: the scene clock marked and a mouth frame, the subtitle's frame while it runs, a poll, two scene ticks; while
 * the voice plays or the subtitle runs, again. Three frame ticks after the talk's start the next step follows, or
 * after the last talk the keeper's closing frame (on Clouds with the mouth's frame 1) and the end. */
static xeen_status gate_talk(xeen_game *g, unsigned w, int stage, xeen_timer_bios bios, void *user)
{
    xeen_status s;
    switch (stage) {
        case CUT_G_TALK:
            if ((s = cut_sample(g, XEEN_TIMER_SCENE_MARK, bios, user)) != XEEN_OK) {
                return s;
            }
            gate_mouth(g);
            return cut_talk_next(g, CUT_G_TALK_SUB, CUT_G_TALK_POLL);
        case CUT_G_TALK_SUB:
            return cut_subtitle_stage(g, CUT_G_TALK_POLL, bios, user);
        case CUT_G_TALK_POLL:
            cut_poll(g, CUT_G_TALK_POLL);
            return cut_step(g, CUT_G_TALK_TICK);
        case CUT_G_TALK_TICK:
            if (!cut_ticks(g, XEEN_TIMER_SCENE_ELAPSED, CUT_G_TALK_TICK, 2, bios, user, &s)) {
                return s;
            }
            if (cut_talking(g, GATE_TALK_MOUTH_CALL)) {
                return cut_step(g, CUT_G_TALK);
            }
            cut_talk_ended(g);
            return cut_step(g, CUT_G_TALK_WAIT);
        default: /* CUT_G_TALK_WAIT */
            if (!cut_ticks(g, XEEN_TIMER_FRAME_ELAPSED, CUT_G_TALK_WAIT, 3, bios, user, &s)) {
                return s;
            }
            if (++g->cut_si < gate_talks[w]) {
                return cut_step(g, CUT_G_STEP);
            }
            cut_draw(g, g->cut_sprites[1], 0, 0, 0, 0, 0);
            if (!w) {
                cut_draw(g, g->cut_sprites[2], 1, 0, 0, 0, 0);
            }
            (void)exit_present(g);
            return cut_end(g, CUT_END_GATE);
    }
}
/* Resumes the gatekeeper's cutscene at its stage after a wait or key (the movements above); a stage outside them
 * stops. Original: 35992's frame loops, 35C2B .. 35FED. */
xeen_status gate_resume(xeen_game *g, xeen_timer_bios bios, void *user)
{
    unsigned w = g->movement.ds[XEEN_DS_WORLD] ? 1u : 0u;
    int stage = g->cut_stage;
    switch (stage) {
        case CUT_G_APPROACH:
            gate_far(g, w, g->cut_si);
            return cut_step(g, CUT_G_APPROACH_POLL);
        case CUT_G_APPROACH_POLL:
            cut_poll(g, CUT_G_APPROACH_POLL);
            if (--g->cut_si >= 0) {
                return cut_step(g, CUT_G_APPROACH);
            }
            exit_save(g);
            g->cut_si = GATE_WALK_STEPS - 1;
            return cut_step(g, CUT_G_WALK);
        case CUT_G_WALK:
        case CUT_G_WALK_POLL:
            return gate_walk(g, w, stage);
        case CUT_G_STEP:
        case CUT_G_STEP_SUB:
        case CUT_G_STEP_TICK:
            return gate_step(g, w, stage, bios, user);
        case CUT_G_TALK:
        case CUT_G_TALK_SUB:
        case CUT_G_TALK_POLL:
        case CUT_G_TALK_TICK:
        case CUT_G_TALK_WAIT:
            return gate_talk(g, w, stage, bios, user);
        default:
            return cut_stop(g, GATE_STOP_STAGE);
    }
}
