/*
 * The guards' cutscene (Phase 248, from game_cutscene.c): its start, its still, far and mouth frames (with their
 * AL-ranged random draw), its end and tail, and its frame loop (guard_resume and its movements: the slide, the speech,
 * the answer, the farewell). The shared frame-loop services are game_cutscene.c's.
 */
#include "game_cutscene.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The original calls of the voice driver (187E9) and the CD status (47F3D) in this unit, each by the call's
 * return address: the audio model records which call asked (traces name them). Not stops, not waits. */
enum {
    GUARD_APPROACH_VOICE_CALL = 0x347e4, /* the approach: voice slot 2 */
    GUARD_SLIDE_VOICE_QUERY_CALL = 0x348cb,/* the slide (Clouds): is the voice still playing? */
    GUARD_SLIDE_VOICE_CALL = 0x348dc,    /* the slide: the ogre again */
    GUARD_SPEECH_VOICE_CALL = 0x349b2,   /* the first wait's speech: voice slot 0 */
    GUARD_TALK_MOUTH_CALL = 0x34aac,     /* the talk's mouth follows the voice */
    GUARD_OGRE_VOICE_CALL = 0x34b95,     /* the pause: the ogre's voice (slot 2) */
    GUARD_ANSWER_VOICE_CALL = 0x34be7,   /* the second wait: go2.voc or key2.voc (slot 1) */
    GUARD_ANSWER_VOICE_QUERY_CALL = 0x34cdc/* the second talk waits for that voice */
};
/*
 * Where native play stops when the guardians' cutscene cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json).
 */
enum {
    GUARD_STOP_SPRITES = 0x34435,            /* the guardian's sprites */
    GUARD_STOP_SECOND_SPRITES = 0x34454,
    GUARD_STOP_ZOMBIE_SPRITES = 0x3446c,     /* dung3.zom */
    GUARD_STOP_BOX = 0x34482,                /* box.vga */
    GUARD_STOP_LINE = 0x3467a,               /* the cutscene's line could not be formed */
    GUARD_STOP_TRAVEL = 0x34f1b,             /* the destination map did not load */
    GUARD_STOP_STAGE = 0x343f6               /* a resume in no stage of the cutscene */
};
/*
 * The guardians (location kind 9): a guardian at a gate (Clouds: the golem, then the ogre's voice; Darkside: the
 * zombie) speaks to the party, and the party passes only when the map's key flag is set (g->cut_key).
 *
 * Setup: the guardian's two sprites (on Darkside also dung3.zom) and box.vga as the backdrop. The destination, by
 * the party's map: Darkside maps 12, 14, 19 and 22 (flags F0A6, F0A8, F0A9, F0A7) lead to map 73 (0, 7) facing east,
 * 83 (11, 1) north, 121 (18, 0) north and 78 (8, 14) south; Clouds maps 8, 12, 19 and 20 (F07D, F07C, F0A9, F07E) to
 * 81 (1, 17) east, 80 (29, 16) west, Darkside's 121 (18, 0) north and 79 (5, 16) east. With the flag clear, or on
 * another map, the party stays. The voices and the line spoken (line 5 with the key, else 4) depend on it too.
 *
 * Frames: the approach (the guardian grows from scale 11, or 8, to 0), the slide (from 200 pixels left, 16 a frame;
 * on Clouds the second sprite's frame jitters by a random 0..9 in each axis), the first talk (random mouth frames
 * while the voice plays or the subtitle still runs: cut_talking), the still frames, on Darkside the answer voice
 * (go2.voc with the key, key2.voc without), the second talk (once); without the key the guardian retreats. The end
 * restores the screen, moves the party (cut_after_scene) and redraws.
 *
 * The random draws keep an original quirk: the generator is entered with AL, which the caller leaves in it. After a
 * sprite draw AL is 0 (that routine's return), and the second draw of Clouds' pair enters with the first draw's
 * offset value (r1 - 5 in the slide, r1 - 3 in the talk). cut_random_al takes that AL explicitly.
 * Original: 343F6 (after 277E5's setup): 4E231, 33CD8, 190ED/1919B, 2B632(special.bin), F9A4 = 0, the sprites, 4EA5D,
 * box.vga into FAF6; the destinations CS:12C4 (Darkside, map - 0Ch) and CS:12AA (Clouds, map - 8), DI = 1 with the
 * flag (map 121 on Clouds sets 96E2 = 1); the voices what2.voc (Darkside), golem15/16.voc with DI else golem13/14.voc
 * and ogre.voc (Clouds); 186B2; the approach SI 11 / 8 down to 0 (4EA0D, the back frames 0 and 1 at 57DE:BCh/ECh and
 * 11Ch + 18h x world + 2 x SI, scale SI, 50BF6(0), 4EA1C to 1, 189E3; Clouds 187CB(30h)); the slide SI -200 by 16
 * (frame 0 at SI, the second sprite's frame 1 - world at SI + A0h, flags 800h; Clouds frame 2 at (SI + r2 - 5,
 * r1 - 5), r1 = 504DA(0, 9), r2 = 504DA(0, 9) with AL = r1 - 5); talk 1 (Darkside SI = 504DA(0, 6), frames SI and
 * SI at A0h; Clouds frames 0, 1 at A0h and 2 at (r2 - 3, r1 - 3), r1 = 504DA(0, 9), r2 = 504DA(0, 5) with
 * AL = r1 - 3; 185E3, 4EA1C to 1, 189E3; 187E9 none); 4EA1C to 1 (Clouds) or 10 (Darkside); talk 2 (4EA1C to 1,
 * 189E3), 187CB(5Fh), the retreat SI 0 to 11 / 8; the end 4AA3E, 33D36, 19E82, 4E99F, the position, 2ABB8(0),
 * 4ADB1, 1B503(0). A 504DA after a 4AC92 enters with AL 0.
 */
static const int16_t guard_x0[2][12] = {{0, -5, 0, 6, 10, 13, 17, 20, 23, 26, 29, 31},
                                        {0, 0, 1, 1, 1, 0, -9, -20, -21, 0, 0, 0}},
                     guard_x1[2][12] = {{160, 145, 140, 136, 130, 123, 117, 110, 103, 96, 89, 81},
                                        {160, 150, 141, 131, 121, 110, 91, 70, 57, 0, 0, 0}},
                     guard_y[2][12] = {{0, 0, 0, 0, 0, 5, 10, 15, 20, 25, 30, 35},
                                       {0, 6, 12, 18, 24, 30, 29, 23, 25, 0, 0, 0}};
/* The frames of those tables each world uses (Clouds 12, Darkside 9): the approach runs them from the last to the
 * first, the retreat back. */
static const int guard_steps[2] = {12, 9};
/** Draws a cutscene random number LO..HI with the caller's AL (LO without an RNG). */
static uint16_t cut_random_al(xeen_game *g, uint16_t lo, uint16_t hi, uint16_t al)
{
    uint16_t v = lo;
    if (g->event_random) {
        (void)xeen_entity_rng_range(g->event_random(g->event_random_user, (uint16_t)(al & 255u)),
                                    lo, hi, &v);
    }
    return v;
}
/* The guardians' keys by world (Clouds, Darkside): on the map, holding its key, the end takes the party to that
 * destination. Map 19's key (flag F0A9 in both worlds) leads to map 121, which the Clouds guardian's end enters on its
 * other side (guard_begin). */
static const cut_keyed_destination guard_keys[2][4] = {
        {{8, 0xf07d, {81, 1, 17, XEEN_EAST}},
         {12, 0xf07c, {80, 29, 16, XEEN_WEST}},
         {19, 0xf0a9, {121, 18, 0, XEEN_NORTH}},
         {20, 0xf07e, {79, 5, 16, XEEN_EAST}}},
        {{12, 0xf0a6, {73, 0, 7, XEEN_EAST}},
         {14, 0xf0a8, {83, 11, 1, XEEN_NORTH}},
         {19, 0xf0a9, {121, 18, 0, XEEN_NORTH}},
         {22, 0xf0a7, {78, 8, 14, XEEN_SOUTH}}}};
/* The guardians' cutscene from its setup (the description and the original's addresses above guard_x0); the second
 * sprite's name is the far text slot 6 + world read as a word, as the original reads it. */
xeen_status guard_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned w = ds[XEEN_DS_WORLD] ? 1u : 0u;
    int len;
    uint16_t map = ds[XEEN_DS_PARTY_MAP], h = 0;
    xeen_presentation_trace_extra("cutmark guardian"); /* Evidence: the handler's entry */
    g->cut_kind = LOCATION_GUARDIANS;
    cut_enter(g);
    if (!sprite_load(g, far_text(g, XEEN_DS_CUTSCENE_FILES, 4u + w),
                   &g->cut_sprites[0])) {
        return cut_stop(g, GUARD_STOP_SPRITES);
    }
    if (!sprite_load(g, ds_text(g, xeen_read16(m, (uint16_t)(XEEN_DS_CUTSCENE_FILES + 4u * (6u + w)))),
                   &g->cut_sprites[1])) {
        return cut_stop(g, GUARD_STOP_SECOND_SPRITES);
    }
    g->cut_sprites[2] = 0;
    if (w && !sprite_load(g, ds_text(g, XEEN_TEXT_GUARD_ANIMATION), &g->cut_sprites[2])) {
        return cut_stop(g, GUARD_STOP_ZOMBIE_SPRITES);
    }
    if (!sprite_load(g, ds_text(g, XEEN_TEXT_CUTSCENE_BOX), &h)) {
        return cut_stop(g, GUARD_STOP_BOX); /* 4EA5D: cut_resume */
    }
    xeen_write16(m, XEEN_DS_SUBTITLE_BOX, h);
    cut_dest_here(g, map);
    if (cut_keyed_dest(g, map, guard_keys[w], 4u) && !w && g->cut_dest[0] == 121u) {
        ds[XEEN_DS_MAP_SIDE] = 1;
    }
    if (w) {
        cut_voc_load(g, 0, ds_text(g, XEEN_TEXT_GUARD_WHAT_VOICE)); /* what2.voc: BP-0C */
    } else {
        cut_voc_load(g, 0, ds_text(g, g->cut_key ? XEEN_TEXT_GUARD_GOLEM_VOICE_15 : XEEN_TEXT_GUARD_GOLEM_VOICE_13));
        cut_voc_load(g, 1, ds_text(g, g->cut_key ? XEEN_TEXT_GUARD_GOLEM_VOICE_16 : XEEN_TEXT_GUARD_GOLEM_VOICE_14));
        cut_voc_load(g, 2, ds_text(g, XEEN_TEXT_GUARD_OGRE_VOICE));
    } /* golem15/16 BP-0C, golem13/14 BP-0A, ogre.voc BP-8 */
    if ((len = cut_line(g, g->cut_key ? 5u : 4u)) < 0) {
        return cut_stop(g, GUARD_STOP_LINE);
    }
    g->cut_len = (uint16_t)len;
    g->cut_text[0] = 0;
    exit_save(g);
    g->cut_si = guard_steps[w] - 1;
    return cut_step(g, CUT_K_APPROACH);
}
/* The guards' still frame: the back, the guardian's frame 0 and the second figure (Darkside the third sprite's frame
 * 0 at x 160; Clouds the second sprite's frames 1 at x 160 and 2); then the screen shown.
 * Original: 348F3, 34AC0, 34CE6. */
static void guard_still(xeen_game *g)
{
    unsigned w = g->movement.ds[XEEN_DS_WORLD] ? 1u : 0u;
    cut_draw_still_frame(g);
    cut_draw(g, g->cut_sprites[1], 0, 0, 0, 0, 0);
    cut_draw(g, g->cut_sprites[w ? 2 : 1], (uint16_t)(1u - w), 0xa0, 0, 0, 0);
    if (!w) {
        cut_draw(g, g->cut_sprites[1], 2, 0, 0, 0, 0);
    }
    (void)exit_present(g);
}
/** Draws the guard at distance STEP (the original's SI) for the current world and presents the frame. */
static void guard_far(xeen_game *g, int step)
{
    unsigned w = g->movement.ds[XEEN_DS_WORLD] ? 1u : 0u;
    cut_draw(g, g->cut_sprites[0], 0, guard_x0[w][step], guard_y[w][step], step, 0);
    cut_draw(g, g->cut_sprites[0], 1, guard_x1[w][step], guard_y[w][step], step, 0);
    (void)exit_present(g);
}
/* A slide frame: the guardian's sprites at X (the second one 160 pixels right of it), drawn with the original's
 * flags 800h; on Clouds the second sprite's frame 2 jitters by two random draws, the second entered with the
 * first's offset as AL (cut_random_al). Then the frame is presented. Original: 347ED. */
static void guard_slide_frame(xeen_game *g, int x)
{
    unsigned w = g->movement.ds[XEEN_DS_WORLD] ? 1u : 0u;
    cut_draw_still_frame(g);
    cut_draw(g, g->cut_sprites[1], 0, x, 0, 0, 0x800);
    cut_draw(g, g->cut_sprites[w ? 2 : 1], (uint16_t)(1u - w), x + 0xa0, 0, 0, 0x800);
    if (!w) {
        uint16_t r1 = cut_random_al(g, 0, 9, 0), r2 = cut_random_al(g, 0, 9, (uint16_t)(r1 - 5u));
        cut_draw(g, g->cut_sprites[1], 2, x + (int16_t)r2 - 5, (int16_t)r1 - 5, 0, 0x800);
    }
    (void)exit_present(g);
}
/* One talk frame of the guards: the clock marked, the still frame; on Darkside a random mouth frame 0..6 drawn on
 * both halves; on Clouds both halves' first frames and a random-placed third frame (x 0..5 and y 0..9, less 3, the
 * x draw's AX the y less 3). Original: 349B5 / 34BEA. */
static xeen_status guard_mouth(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status s;
    uint16_t r1, r2;
    if ((s = cut_sample(g, XEEN_TIMER_FRAME_MARK, bios, user)) != XEEN_OK) {
        return s;
    }
    cut_draw_still_frame(g);
    if (g->movement.ds[XEEN_DS_WORLD]) {
        r1 = cut_random_al(g, 0, 6, 0);
        cut_draw(g, g->cut_sprites[1], r1, 0, 0, 0, 0);
        cut_draw(g, g->cut_sprites[2], r1, 0xa0, 0, 0, 0);
    } else {
        cut_draw(g, g->cut_sprites[1], 0, 0, 0, 0, 0);
        cut_draw(g, g->cut_sprites[1], 1, 0xa0, 0, 0, 0);
        r1 = cut_random_al(g, 0, 9, 0);
        r2 = cut_random_al(g, 0, 5, (uint16_t)(r1 - 3u));
        cut_draw(g, g->cut_sprites[1], 2, (int16_t)r2 - 3, (int16_t)r1 - 3, 0, 0);
    }
    return XEEN_OK;
}
xeen_status guard_end_tail(xeen_game *g)
{
    xeen_presentation_trace_extra("cutload"); /* Evidence: 19E82 */
    if (map_reload(g, g->cut_dest[0]) != XR_OK) {
        return cut_stop(g, GUARD_STOP_TRAVEL);
    }
    return cut_arrive(g);
}
/* The guardian's encounter after its setup, one function per movement; cut_si counts each movement's frames.
 *   approach  CUT_K_APPROACH .. _POLL: the guardian grows through guard_steps' frames, farthest to nearest (one frame
 *             tick each); on Clouds the ogre's voice then starts;
 *   slide     CUT_K_SLIDE .. _TICK: the pair slides in from x -200 by GUARD_SLIDE_STEP (the key poll before the tick,
 *             and on Clouds the ogre's voice again whenever it has stopped);
 *   speech    CUT_K_WAIT1, CUT_K_TALK .. _POLL: once the earlier voice ends, the guardian speaks (voice slot 0) with
 *             the subtitle until both are done (cut_talking);
 *   pause     CUT_K_PAUSE: 1 frame tick (Darkside 10), on Clouds the ogre's voice;
 *   answer    CUT_K_WAIT2, CUT_K_TALK2 .. _POLL: the answer (voice slot 1: Darkside's go2.voc or key2.voc) while it
 *             plays, without a subtitle;
 *   farewell  CUT_K_WAIT3: the song volume restored once the voice ends; with the key the party passes, else
 *   retreat   CUT_K_RETREAT .. _POLL: the guardian shrinks back through its frames, and the cutscene ends.
 * A voice wait (WAIT1..3) is a stage of its own: the previous stage goes on into it in the same call, as the
 * original's code runs on into its 187E9(1, 0) loop. */
enum { GUARD_SLIDE_START = -200, GUARD_SLIDE_STEP = 16 };
static xeen_status guard_speech(xeen_game *g, int stage, xeen_timer_bios bios, void *user);
/* Slide: on Clouds the ogre's voice starts again whenever it has stopped (asked after the key poll). */
static xeen_status guard_slide(xeen_game *g, unsigned w, int stage, xeen_timer_bios bios, void *user)
{
    xeen_status s;
    switch (stage) {
        case CUT_K_SLIDE:
            if ((s = cut_sample(g, XEEN_TIMER_FRAME_MARK, bios, user)) != XEEN_OK) {
                return s;
            }
            guard_slide_frame(g, g->cut_si);
            return cut_step(g, CUT_K_SLIDE_POLL);
        case CUT_K_SLIDE_POLL:
            cut_poll(g, CUT_K_SLIDE_POLL);
            if (!w && !voice_busy(g, GUARD_SLIDE_VOICE_QUERY_CALL)) {
                cut_voc_start(g, 2, GUARD_SLIDE_VOICE_CALL);
            }
            return cut_step(g, CUT_K_SLIDE_TICK);
        default: /* CUT_K_SLIDE_TICK */
            if (!cut_ticks(g, XEEN_TIMER_FRAME_ELAPSED, CUT_K_SLIDE_TICK, 1, bios, user, &s)) {
                return s;
            }
            if ((g->cut_si += GUARD_SLIDE_STEP) < 0) {
                return cut_step(g, CUT_K_SLIDE);
            }
            guard_still(g);
            return guard_speech(g, CUT_K_WAIT1, bios, user); /* the slide's last frame runs on into the voice wait */
    }
}
/* Speech: the earlier voice (Clouds' ogre) is waited out, then the guardian's speech (slot 0) starts at the talk's
 * volume; its frames run while the voice plays or the subtitle still scrolls, then the still frame and the pause's
 * clock mark. */
static xeen_status guard_speech(xeen_game *g, int stage, xeen_timer_bios bios, void *user)
{
    xeen_status s;
    switch (stage) {
        case CUT_K_WAIT1:
            if (cut_voice_busy(g, CUT_K_WAIT1)) {
                return cut_step(g, CUT_K_WAIT1);
            }
            cut_volume(g, CUT_SONG_VOLUME_TALK);
            cut_voc_start(g, 0, GUARD_SPEECH_VOICE_CALL);
            return cut_step(g, CUT_K_TALK);
        case CUT_K_TALK:
            if ((s = guard_mouth(g, bios, user)) != XEEN_OK) {
                return s;
            }
            return cut_talk_next(g, CUT_K_TALK_SUB, CUT_K_TALK_TICK);
        case CUT_K_TALK_SUB:
            return cut_subtitle_stage(g, CUT_K_TALK_TICK, bios, user);
        case CUT_K_TALK_TICK:
            return cut_frame_stage(g, CUT_K_TALK_TICK, 1, CUT_K_TALK_POLL, bios, user);
        default: /* CUT_K_TALK_POLL */
            cut_poll(g, CUT_K_TALK_POLL);
            if (cut_talking(g, GUARD_TALK_MOUTH_CALL)) {
                return cut_step(g, CUT_K_TALK);
            }
            guard_still(g);
            if ((s = cut_sample(g, XEEN_TIMER_FRAME_MARK, bios, user)) != XEEN_OK) {
                return s;
            }
            return cut_step(g, CUT_K_PAUSE);
    }
}
/* Farewell: once the answer's voice has ended the song volume is restored; with the key the party passes, else the
 * guardian retreats. */
static xeen_status guard_farewell(xeen_game *g)
{
    if (cut_voice_busy(g, CUT_K_WAIT3)) {
        return cut_step(g, CUT_K_WAIT3);
    }
    cut_volume(g, CUT_SONG_VOLUME_END);
    return cut_pass_or_retreat(g, CUT_END_GUARD, CUT_K_RETREAT);
}
/* The answer (voice slot 1, Darkside's chosen by the key) after the pause: Clouds waits out the ogre's voice first;
 * its frames run while the voice plays (no subtitle). Darkside never waits here: its slot 2 holds no voice. */
static xeen_status guard_answer(xeen_game *g, unsigned w, int stage, xeen_timer_bios bios, void *user)
{
    xeen_status s;
    switch (stage) {
        case CUT_K_PAUSE:
            if (!cut_ticks(g, XEEN_TIMER_FRAME_ELAPSED, CUT_K_PAUSE, w ? 10 : 1, bios, user, &s)) {
                return s;
            }
            if (!w) {
                cut_voc_start(g, 2, GUARD_OGRE_VOICE_CALL);
            }
            return guard_answer(g, w, CUT_K_WAIT2, bios, user); /* the pause runs on into the voice wait */
        case CUT_K_WAIT2:
            if (!w && cut_voice_busy(g, CUT_K_WAIT2)) {
                return cut_step(g, CUT_K_WAIT2);
            }
            if (w) {
                cut_voc_load(g, 1, ds_text(g, g->cut_key ? XEEN_TEXT_GUARD_GO_VOICE : XEEN_TEXT_GUARD_KEY_VOICE));
            }
            cut_voc_start(g, 1, GUARD_ANSWER_VOICE_CALL);
            return cut_step(g, CUT_K_TALK2);
        case CUT_K_TALK2:
            if ((s = guard_mouth(g, bios, user)) != XEEN_OK) {
                return s;
            }
            (void)exit_present(g);
            return cut_step(g, CUT_K_TALK2_TICK);
        case CUT_K_TALK2_TICK:
            return cut_frame_stage(g, CUT_K_TALK2_TICK, 1, CUT_K_TALK2_POLL, bios, user);
        default: /* CUT_K_TALK2_POLL */
            cut_poll(g, CUT_K_TALK2_POLL);
            if (voice_busy(g, GUARD_ANSWER_VOICE_QUERY_CALL)) {
                return cut_step(g, CUT_K_TALK2);
            }
            guard_still(g);
            return guard_farewell(g); /* the answer's end runs on into the voice wait */
    }
}
/* Resumes the guardian's cutscene at its stage after a wait or key (the movements above); a stage outside them
 * stops. Original: 343F6's frame loops, 346A7 .. 34EB2. */
xeen_status guard_resume(xeen_game *g, xeen_timer_bios bios, void *user)
{
    unsigned w = g->movement.ds[XEEN_DS_WORLD] ? 1u : 0u;
    int stage = g->cut_stage;
    xeen_status s;
    switch (stage) {
        case CUT_K_APPROACH:
            if ((s = cut_sample(g, XEEN_TIMER_FRAME_MARK, bios, user)) != XEEN_OK) {
                return s;
            }
            exit_restore(g);
            guard_far(g, g->cut_si);
            return cut_step(g, CUT_K_APPROACH_TICK);
        case CUT_K_APPROACH_TICK:
            return cut_frame_stage(g, CUT_K_APPROACH_TICK, 1, CUT_K_APPROACH_POLL, bios, user);
        case CUT_K_APPROACH_POLL:
            cut_poll(g, CUT_K_APPROACH_POLL);
            if (--g->cut_si >= 0) {
                return cut_step(g, CUT_K_APPROACH);
            }
            if (!w) {
                cut_volume(g, CUT_SONG_VOLUME_TALK);
                cut_voc_start(g, 2, GUARD_APPROACH_VOICE_CALL); /* the ogre */
            }
            g->cut_si = GUARD_SLIDE_START;
            return cut_step(g, CUT_K_SLIDE);
        case CUT_K_SLIDE:
        case CUT_K_SLIDE_POLL:
        case CUT_K_SLIDE_TICK:
            return guard_slide(g, w, stage, bios, user);
        case CUT_K_WAIT1:
        case CUT_K_TALK:
        case CUT_K_TALK_SUB:
        case CUT_K_TALK_TICK:
        case CUT_K_TALK_POLL:
            return guard_speech(g, stage, bios, user);
        case CUT_K_PAUSE:
        case CUT_K_WAIT2:
        case CUT_K_TALK2:
        case CUT_K_TALK2_TICK:
        case CUT_K_TALK2_POLL:
            return guard_answer(g, w, stage, bios, user);
        case CUT_K_WAIT3:
            return guard_farewell(g);
        case CUT_K_RETREAT:
            exit_restore(g);
            guard_far(g, g->cut_si);
            return cut_step(g, CUT_K_RETREAT_POLL);
        case CUT_K_RETREAT_POLL:
            cut_poll(g, CUT_K_RETREAT_POLL);
            if (++g->cut_si < guard_steps[w]) {
                return cut_step(g, CUT_K_RETREAT);
            }
            return cut_retreat_done(g, CUT_END_GUARD);
        default:
            return cut_stop(g, GUARD_STOP_STAGE);
    }
}
