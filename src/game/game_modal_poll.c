/*
 * The modal waits' polls (Phase 248, from game_input.c): the shared 189E3 key poll of a modal wait
 * (modal_key_poll), the yes/no confirmation (294A2..294D9), 3000D's speech loop with its face, the NPC dialogue
 * window and its Who-will prompt (304CF / 2F9BA: modal_poll_pending's steps), and the modal loop (poll_modal: its
 * scene, the kept face's animation, the Who-will form, the 4EA1C wait and the key's owner). xeen_game_poll_input
 * selects them (poll_wait_owner, game_input.c).
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Where native play stops when this unit cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json, stops_variable.json). */
enum {
    FACE_STOP_KIND = 0x26978,                /* an animation other than the face: not served here */
    ANIMATION_STOP_NO_FRAMES = 0x26da6       /* a keeper animation of zero frames (the original divides by it) */
};
/*
 * A modal wait's 189E3 key poll: the poll's evidence mark (modal_poll_mark), the CD clock, the poll itself with
 * AUDIO as its sink (the yes/no passes NULL without native audio, the other waits the session's), and the key it
 * read (modal_poll_key). Returns XEEN_OK, or the poll's status (the session stops at the poll's boundary when it
 * has one).
 */
static xeen_status modal_key_poll(xeen_game *g, xeen_native_audio *audio)
{
    xeen_input_poll_result result;
    xeen_sprite_archives archives = sprite_archives(g, NULL);
    xeen_status status;
    modal_poll_mark(g);
    g->audio.cd_now = g->timer.clock;
    status = xeen_input_poll_run(&g->input_poll, &g->movement, &g->input_provider, audio, &archives,
                                 g->presentation, &result);
    if (status != XEEN_OK) {
        if (result.boundary) {
            g->lifecycle_boundary = result.boundary;
            g->phase = XG_STOPPED;
        }
        return status;
    }
    g->modal_poll_key = xeen_read16(&g->movement, XEEN_DS_INPUT_CODE);
    return XEEN_OK;
}
/**
 * xeen_game_poll_input's owner for a yes/no confirmation: polls until a key. Esc and N answer no, Y yes; any other
 * key (or none) polls again, without the button release. A confirmation that only informs (confirm_info: the
 * guild's spell description) closes at any key. A click still held from the question is waited out first.
 * Original: 294A2..294D9 (189E3; no 189BC for other keys; 294B2 any key).
 */
xeen_status poll_confirm(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    uint16_t command;
    xeen_mouse_sample mouse = {0, 0, 0, 0};
    (void)bios;
    (void)user;
    if (g->confirm_release == CONFIRM_RELEASE_ANSWERED) {
        return confirm_finish(g);
    }
    if (g->confirm_release) {
        if (!g->input_provider.mouse(g->input_provider.user, &mouse)) {
            return XEEN_BAD_INPUT;
        }
        if (mouse.click) {
            return XEEN_SERVICE_REQUIRED;
        }
        xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
        g->confirm_release = CONFIRM_RELEASE_NONE;
    }
    if ((status = modal_key_poll(g, game_audio(g))) != XEEN_OK) {
        return status;
    }
    command = xeen_read16(&g->movement, XEEN_DS_INPUT_CODE);
    if (command && g->confirm_info) {
        return confirm_answer(g, 0);
    }
    if (command == XEEN_KEY_ESCAPE || command == 'N') {
        return confirm_answer(g, 0);
    }
    if (command == 'Y') {
        return confirm_answer(g, 1);
    }
    if (command) {
        ++g->confirm_ignored;
    }
    return XEEN_SERVICE_REQUIRED;
}
/*
 * A speaking face's mouth frame: the CD status decides the mouth at SITE (cd_mouth: moving while the voice plays,
 * else closed), then the face's frame is drawn at the animation's position. The speech loop and a Who-will pass's
 * face form share it. Returns the draw's status.
 * Original: 47F3D (busy 504DA(0, 3), idle 0) into 8D32; the face 8D56[8D32 / 8], frame 8D32 % 8 at (8D30, 8D2E);
 * 30454, 2FB31.
 */
static xeen_status face_mouth_draw(xeen_game *g, uint32_t site)
{
    uint16_t f = cd_mouth(g, site);
    xeen_sprite_draw draw = {xeen_read16(&g->movement, XEEN_DS_ANIMATION_SPRITE(f / 8u)),
                             (uint16_t)(f % 8u),
                             xeen_read16(&g->movement, XEEN_DS_ANIMATION_X),
                             xeen_read16(&g->movement, XEEN_DS_ANIMATION_Y),
                             0,
                             0,
                             0};
    xeen_write16(&g->movement, XEEN_DS_ANIMATION_FRAME, f);
    return xeen_presentation_draw_sprites(g->presentation, &draw, 1);
}
/**
 * xeen_game_poll_input's owner during an NPC's speech while the text is off: the loop's scene, the CD status and the
 * face between its key polls, until a key ends the speech. Original: 3000D's loop with F384 set.
 */
xeen_status poll_speech(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    uint16_t command;
    /* The stages are enum speech_wait's: a pass's whole scene (the view's window is closed; the event UI mode
     * refreshes the scripts), the face with its mouth (moving while the CD voice plays), the speech window and the
     * view shown; the key poll; then the timer: a key ends the wait, else a tick or more starts the next pass and
     * less polls again.
     * Original: 30442: 4EA0D (3040C's first), 1B503(0) (9A1C 9 at 1C19B); 47F3D (busy 30458: 8D32 = 504DA(0, 3);
     * idle 30469: 0); the face 8D56[8D32 / 8] frame 8D32 % 8 at (8D30, 8D2E), 50BF6(28h), 50BF6(3); 304AD 189E3,
     * 4EA1C; a key 304EF. */
    if (g->speech_wait == SPEECH_PASS) {
        if (g->speech_first) {
            status = frame_base(g, bios, user);
            if (status != XEEN_OK) {
                return status;
            }
            g->speech_first = 0;
        }
        g->speech_wait = SPEECH_FACE;
        return scene_wait(g, 0, XEEN_TIMER_FRAME_MARK);
    }
    if (g->speech_wait == SPEECH_FACE) {
        if (face_mouth_draw(g, SPEECH_CD_STATUS_CALL) != XEEN_OK ||
            xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SPEECH) != XEEN_OK ||
            xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW_FRAME) != XEEN_OK) {
            return XEEN_SERVICE_REQUIRED;
        }
        g->speech_wait = SPEECH_POLL;
    }
    if (g->speech_wait == SPEECH_POLL) {
        if ((status = modal_key_poll(g, &g->audio)) != XEEN_OK) {
            return status;
        }
        g->modal_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
        g->speech_wait = SPEECH_TICK;
    }
    if ((status = timer_ticks(g, &g->modal_elapsed, bios, user)) != XEEN_OK) {
        return status;
    }
    command = xeen_read16(&g->movement, XEEN_DS_INPUT_CODE);
    /* A speech that asks yes/no (mode 0) takes only Esc, N and Y; any other key starts the loop again (with the
     * button release and the frame mark). Original: 3051A; back to 30407 (189BC, 3040C's 4EA0D, the 30442 loop). */
    if (command && g->npc_confirm && command != XEEN_KEY_ESCAPE && command != 'N' && command != 'Y') {
        xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
        g->speech_wait = SPEECH_PASS;
        g->speech_first = 1;
        ++g->confirm_ignored;
        return XEEN_SERVICE_REQUIRED;
    }
    if (command) {
        g->speech_wait = SPEECH_NONE;
        g->response = command;
        g->response_ready = 1;
        drive(g);
        return XEEN_SERVICE_REQUIRED;
    }
    g->speech_wait = g->timer_registers.ax >= 1u && g->timer_registers.ax < 0x8000u
                             ? SPEECH_PASS
                             : SPEECH_POLL; /* the tick count compared signed (the original's JGE) */
    return XEEN_SERVICE_REQUIRED;
}
/*
 * The dialogue's wait (MODAL_POLL_TICKS): three timer ticks (compared signed) since the pass's frame mark. Returns
 * XEEN_OK when they have passed, XEEN_SERVICE_REQUIRED while waiting, or the timer's failure. WHO: a Who-will pass
 * over the window, whose next pass starts with its own frame mark. Original: 304CF; 4EA1C after 4EA0D.
 */
static xeen_status dialogue_ticks(xeen_game *g, xeen_timer_bios bios, void *user, int who)
{
    xeen_status status;
    status = xeen_timer_elapsed(&g->timer, &g->modal_elapsed, &g->timer_registers, palette_port, g, bios, user);
    if (status != XEEN_OK) {
        return status;
    }
    if (g->port_error) {
        return XEEN_BAD_INPUT;
    }
    if (g->timer_registers.ax < 3u || g->timer_registers.ax >= 0x8000u) {
        g->modal_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
        return XEEN_SERVICE_REQUIRED;
    }
    if (who) {
        /* Each pass of the Who-will loop marks the frame clock before its face, so the three ticks count from that
         * frame. (An earlier native kept the entry's mark: after the first three ticks it drew a new face at every
         * poll, the mouth far too fast.) Original: 2FBA9 JMP 2FAE3, 4EA0D before 26978; 2FB97's 4EA1C. */
        status = frame_base(g, bios, user);
        if (status != XEEN_OK) {
            return status;
        }
    }
    return XEEN_OK;
}
/* A talking face's next mouth (an NPC's dialogue portrait, a town keeper's): the toggle at DS 8D2A flips (its
 * signed remainder by 2, as the original's IDIV: only 0 and 1 are ever stored), and while the CD voice plays the
 * mouth is a random frame 0..3, else it closes (frame 0, and the companion byte cleared).
 * Original: 26D57's 8D2A (26D65 INC, 26D69, 26D72 IDIV), 47F3D: busy 26D7F (8D32 = 504DA(0, 3), 8D2C kept), idle
 * 26D90 (8D32 = 8D2C = 0); reached from 26978. */
void talk_mouth(xeen_game *g)
{
    xeen_write16(&g->movement, 0x8d2a, (uint16_t)((int16_t)(xeen_read16(&g->movement, 0x8d2a) + 1u) % 2));
    if (cd_status(g, TALK_CD_STATUS_CALL)) {
        xeen_write16(&g->movement, XEEN_DS_ANIMATION_FRAME, location_random(g, 0, 3));
    } else {
        xeen_write16(&g->movement, XEEN_DS_ANIMATION_FRAME, 0);
        xeen_write16(&g->movement, 0x8d2c, 0);
    }
}
/*
 * The dialogue's portrait frame: the NPC's portrait in its current mouth frame, then the next mouth (talk_mouth) and
 * the view frame shown; a Who-will pass then draws its window's frame. Returns XEEN_OK with the poll next (directly
 * for WHO, else after a frame mark), else XEEN_SERVICE_REQUIRED (a frame above 7 stops at the face routine; a failed
 * draw or update waits).
 * Original: 26978 with argument 0 (town kind 7); 47F3D; a Who-will pass's 2FAF9 4F6F8(24h).
 */
static xeen_status dialogue_portrait(xeen_game *g, int who)
{
    uint16_t frame = xeen_read16(&g->movement, XEEN_DS_ANIMATION_FRAME);
    if (frame >= 8u) {
        g->phase = XG_STOPPED;
        g->lifecycle_boundary = PORTRAIT_ANIMATION_BOUNDARY;
        return XEEN_SERVICE_REQUIRED;
    }
    xeen_sprite_draw draw = {g->dialogue_first, frame, 23, 22, 0, 0, 0};
    if (xeen_presentation_draw_sprites(g->presentation, &draw, 1) != XEEN_OK) {
        return XEEN_SERVICE_REQUIRED;
    }
    talk_mouth(g);
    if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW_FRAME) != XEEN_OK) {
        return XEEN_SERVICE_REQUIRED;
    }
    /* A Who-will pass draws the Who-will window's frame after the face, every pass (on the draw page; the next
     * update shows it). Original: 2FAF9 4F6F8(24h) after 26978(0). */
    if (who) {
        (void)xeen_presentation_frame(g->presentation, XEEN_WINDOW_WHO_WILL);
    }
    g->modal_poll_pending = who ? DIALOGUE_POLL_KEY : DIALOGUE_POLL_SAMPLE;
    return XEEN_OK;
}
/*
 * The key COMMAND the poll read (0: none, the wait goes on): the speech's yes/no (mode 0's last page) takes only
 * Esc, N and Y (any other key starts its loop again); the Who-will prompt takes Esc and F1..F6 of an able member (a
 * refused member gets its message, any other key starts the prompt's pass again); the key then answers the request
 * and the game drives on. Original: 3051A (back to 30407); the Who-will table CS:42B8, 2FBB2..2FBC5 (back to 2FAE3).
 */
static xeen_status dialogue_key(xeen_game *g, uint16_t command, int who)
{
    /* The yes/no page of a mode-0 speech: Esc, N and Y answer; any other key starts the wait again. Original:
     * 3051A, back to 30407 (189BC, 4EA0D, the 304CF wait). */
    if (command && !who && g->npc_confirm && g->npc_rest == XEEN_TEXT_LAYOUT_END &&
        command != XEEN_KEY_ESCAPE && command != 'N' && command != 'Y') {
        g->modal_poll_pending = MODAL_POLL_START;
        ++g->confirm_ignored;
        return XEEN_SERVICE_REQUIRED;
    }
    /* The Who-will prompt takes Esc and F1..F6; a refused member gets its message, any other key starts the prompt's
     * pass again. Original: 2FBB2..2FBC5 (the table CS:42B8), back to 2FAE3 (4EA0D, 26978 with window 11 open). */
    if (command && who) {
        xeen_status st;
        if (who_refused(g, command, &st)) {
            g->modal_poll_pending = MODAL_POLL_START;
            return st;
        }
    }
    if (command) {
        if (who) {
            g->modal_poll_pending = MODAL_POLL_START;
        }
        g->response = command;
        g->response_ready = 1;
        drive(g);
    }
    return XEEN_SERVICE_REQUIRED;
}
/**
 * Runs the modal's 189E3 key poll (with its evidence mark and the CD clock), records the key it
 * read (modal_poll_key) and starts the 4EA1C wait that follows it. Returns XEEN_OK, or the poll's
 * status (the session stops at the poll's boundary when it has one).
 */
static xeen_status start_modal_key_poll(xeen_game *g)
{
    xeen_status status;
    if ((status = modal_key_poll(g, &g->audio)) != XEEN_OK) {
        return status;
    }
    g->modal_poll_pending = MODAL_POLL_TICKS;
    g->modal_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
    return XEEN_OK;
}
/**
 * The input poll while an NPC's dialogue window (or a Who-will prompt drawn over it) waits: its steps by
 * modal_poll_pending: the pass's frame mark (START); the dialogue's three ticks, then the portrait (TICKS); the
 * Who-will pass's portrait at once (WHO_PORTRAIT); the frame mark after it (SAMPLE); the key poll and its key (KEY).
 * Original: 3000D's full layout (304CF) and 2F9BA with window 11 open.
 */
xeen_status poll_dialogue(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    /* The dialogue waits three ticks, then its portrait and the poll (no exploration scene runs under the NPC
     * window); a Who-will pass over it draws the portrait at once, then polls, then waits three ticks a frame.
     * Original: 3000D's full layout mode 1: 304CF; 2F9BA with window 11 open (2FAEF), 26978. */
    int who = !g->dialogue_waiting;
    if (g->modal_poll_pending == MODAL_POLL_START) {
        status = frame_base(g, bios, user);
        if (status != XEEN_OK) {
            return status;
        }
        if (!who) {
            xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
        }
        g->modal_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
        g->modal_poll_pending = who ? DIALOGUE_POLL_WHO_PORTRAIT : MODAL_POLL_TICKS;
    }
    if (g->modal_poll_pending == MODAL_POLL_TICKS || g->modal_poll_pending == DIALOGUE_POLL_WHO_PORTRAIT) {
        g->port_error = 0;
        if (g->modal_poll_pending == MODAL_POLL_TICKS && (status = dialogue_ticks(g, bios, user, who)) != XEEN_OK) {
            return status;
        }
        if ((status = dialogue_portrait(g, who)) != XEEN_OK) {
            return status;
        }
    }
    if (g->modal_poll_pending == DIALOGUE_POLL_SAMPLE) {
        status = frame_base(g, bios, user);
        if (status != XEEN_OK) {
            return status;
        }
        g->modal_poll_pending = DIALOGUE_POLL_KEY;
    }
    if ((status = start_modal_key_poll(g)) != XEEN_OK) {
        return status;
    }
    return dialogue_key(g, xeen_read16(&g->movement, XEEN_DS_INPUT_CODE), who);
}
/**
 * Whether the open modal wait's scene is combat's: the treasure dialog in combat, combat's rest message, and a combat
 * spell's treasure dialog (owner 4 with combat mode 2). Original: 2F170 (owner 3), the scroll (owner 13), 287FF
 * (FAF8 = 2).
 */
static int modal_combat_scene_owner(const xeen_game *g)
{
    return g->treasure.owner == TREASURE_COMBAT || g->scroll_owner == SCROLL_COMBAT_REST ||
           (g->treasure.owner == TREASURE_SPELL_VOLLEY && g->movement.ds[XEEN_DS_COMBAT_MODE] == 2u);
}
/**
 * The combat side's modal scene: 4EA0D, then 1B503(1) as the combat-side semantic scene (mode 2:
 * 96C0 countdown, effects, selector). Marks the scene ready and counts a scene return; returns the
 * timer's status, the combat scene's stop, or XEEN_SERVICE_REQUIRED.
 */
static xeen_status modal_combat_scene(xeen_game *g, xeen_timer_bios bios, void *user)
{
    uint32_t boundary = 0;
    xeen_status status = frame_base(g, bios, user);
    if (status != XEEN_OK) {
        return status;
    }
    if (xeen_combat_scene(&g->combat, &g->movement, g->monster_database, g->monster_database_size,
                          &boundary) != XEEN_OK) {
        return game_stop(g, boundary);
    }
    g->modal_scene_ready = 1;
    ++g->scene_returns;
    return XEEN_SERVICE_REQUIRED;
}
/**
 * A kept speech face's frame, F the animation's frame counter as the scene read it: the face drawn in frame F; a
 * choice (Y/N) first clears the border frames and redraws the border. With the view or the speech window open the
 * face talks (talk_mouth); else the counter steps on through the animation's frames (Clouds' 32- and 26-frame
 * cycles restart at frames 17 and 20). Then the speech window or the view's frame is shown. Returns XEEN_OK, or
 * XEEN_SERVICE_REQUIRED after a stop (another animation kind; a zero frame count, the original's division by zero)
 * or a failed draw.
 * Original: 26978(0)'s kept face (8D34 = 7), F = 8D32 before its 4EA1C; the face 8D56[F / 8] at (8D30, 8D2E); a
 * choice (2F77A, 2F5B1's 26978(1)) clears 26D3C's F468, F469, 974F, 974E, 974D and redraws the border (26306); 8D32
 * + 1 modulo 8D36 (26DAE: 20h -> 11h, 1Ah -> 14h); 26E4F: 50BF6(9894 ? 28h : 3).
 */
static xeen_status animate_kept_face(xeen_game *g, uint16_t f)
{
    xeen_movement *m = &g->movement;
    uint16_t h;
    if (xeen_read16(m, XEEN_DS_ANIMATION_KIND) !=
        XEEN_ANIMATION_FACE) { /* other kinds: not served here */
        return game_stop(g, FACE_STOP_KIND);
    }
    h = xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(f / 8u));
    if (h) {
        xeen_sprite_draw draw = {h,
                                 (uint16_t)(f % 8u),
                                 xeen_read16(m, XEEN_DS_ANIMATION_X),
                                 xeen_read16(m, XEEN_DS_ANIMATION_Y),
                                 0,
                                 0,
                                 0};
        if (xeen_presentation_draw_sprites(g->presentation, &draw, 1) != XEEN_OK) {
            return XEEN_SERVICE_REQUIRED;
        }
    }
    if (g->choice) {
        xeen_border_frames_clear(m->ds);
        location_border(g);
    }
    if (m->ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)] || m->ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH)]) {
        talk_mouth(g);
    } else {
        uint16_t d = xeen_read16(m, XEEN_DS_ANIMATION_FRAME_COUNT);
        if (!d) { /* the original would divide by zero here */
            return game_stop(g, ANIMATION_STOP_NO_FRAMES);
        }
        f = (uint16_t)((f + 1u) % d);
        if (!m->ds[XEEN_DS_WORLD] && !f && (d == CLOUDS_GUILD_FRAMES || d == CLOUDS_TEMPLE_FRAMES)) {
            f = d == CLOUDS_GUILD_FRAMES ? CLOUDS_GUILD_RESTART : CLOUDS_TEMPLE_RESTART;
        }
        xeen_write16(m, XEEN_DS_ANIMATION_FRAME, f);
    }
    (void)xeen_presentation_update_window(g->presentation,
                                          m->ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH)]
                                                  ? XEEN_WINDOW_SPEECH
                                                  : XEEN_WINDOW_VIEW_FRAME); /* 26E4F: 50BF6(9894 ? 28h : 3) */
    return XEEN_OK;
}
/**
 * The modal wait's scene when a face is kept (a speaking NPC's): the frame clock marked, then the face animated
 * instead of a scene pass (three ticks to the next); for the tavern's food question the location's own animation
 * (location_animate, with its window updates). Marks the scene ready as a kept face's. Returns XEEN_OK to go on with
 * the poll, else the status poll_modal returns.
 * Original: 2F51C with 8D56 set: 4EA0D, 26978([BP+8], 0 on the served forms) instead of 1B503(1), DI = 3 for 4EA1C;
 * the tavern's 2F2FF(0, 1) (8D34 = 3) runs 26978(1); the others animate_kept_face.
 */
static xeen_status modal_face_scene(xeen_game *g, xeen_timer_bios bios, void *user)
{
    uint16_t f = xeen_read16(&g->movement, XEEN_DS_ANIMATION_FRAME);
    xeen_status status = frame_base(g, bios, user);
    if (status != XEEN_OK) {
        return status;
    }
    if (xeen_read16(&g->movement, XEEN_DS_ANIMATION_KIND) == 3u && g->tavern_food_step == TAVERN_FOOD_ASKING) {
        location_animate(g, 1);
    } else if ((status = animate_kept_face(g, f)) != XEEN_OK) {
        return status;
    }
    g->modal_scene_ready = 1;
    g->modal_face = 1;
    g->modal_fresh = 0;
    return XEEN_OK;
}
/**
 * Asks the host for the modal loop's own scene: the session stops at the frame clock's mark with the scene's
 * argument (0 for a plain Who-will pass, else 1), the evidence site naming the loop's own scene return (a scroll's,
 * a page wait's). The Who-will pass's scene is a quiet one; the prompt presents itself after its frame and faces.
 * Returns XEEN_SERVICE_REQUIRED.
 * Original: 4EA0D; 18FA1's 1B503(1) at 18FAD, 31786's at 31792; 2F9BA's 2FB07 1B503(0) (no 50BF6(1)/(3), no
 * 4E33F), then 50BF6(3) at 2FB84.
 */
static xeen_status request_modal_scene(xeen_game *g)
{
    g->modal_fresh = 0;
    if (!g->modal.active && g->scroll_owner) {
        g->scene_site = 0x18fadu;
    } else if (!g->modal.active && g->page_wait && !g->nothing_wait) {
        g->scene_site = 0x31792u;
    }
    g->phase = XG_STOPPED;
    g->scene_argument = who_plain(g) ? 0 : 1;
    g->lifecycle_boundary = XEEN_TIMER_FRAME_MARK;
    return XEEN_SERVICE_REQUIRED;
}
/**
 * A Who-will pass's presentation before its key poll (not when a poll is pending or repeating). Over a speaking
 * NPC's window (kept open by the speech's mode 2) the face form draws the prompt again with the face: its mouth moves
 * while the CD voice plays (a random frame 0..3), else it is shut; the right column is shown. A plain pass shows the
 * view's frame. Returns XEEN_OK, or XEEN_SERVICE_REQUIRED when a draw or a present fails.
 * Original: 2FB1C's face form: 47F3D, 2FB35 8D32 = 504DA(0, 3) (idle 0), the face 8D56[8D32 / 8] frame 8D32 % 8 at
 * (8D30, 8D2E), 50BF6(0Ah); a plain pass 50BF6(3) at 2FB84.
 */
static xeen_status present_who_will_form(xeen_game *g)
{
    if (g->modal_poll_pending != MODAL_POLL_START || g->who_repoll) {
        return XEEN_OK;
    }
    if (g->who_wait && g->movement.ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH)]) {
        if (face_mouth_draw(g, FACE_FORM_CD_STATUS_CALL) != XEEN_OK ||
            xeen_presentation_update_window(g->presentation, XEEN_WINDOW_RIGHT_COLUMN) != XEEN_OK) {
            return XEEN_SERVICE_REQUIRED;
        }
    }
    if (who_plain(g) && xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW_FRAME) != XEEN_OK) {
        return XEEN_SERVICE_REQUIRED;
    }
    return XEEN_OK;
}
/* A modal kept face's wait is running (a speaking NPC's face on an open modal). */
static int kept_face_wait(const xeen_game *g)
{
    return g->modal_face && g->modal.active;
}
/* The Who-will prompt (no scroll over it) has waited fewer ticks than its pass needs: 3 with the view's window
 * open, else 1 (compared signed). */
static int who_will_too_soon(const xeen_game *g)
{
    return g->who_wait && !g->scroll_owner && !kept_face_wait(g) &&
           (int16_t)g->timer_registers.ax < (g->movement.ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)] ? 3 : 1);
}
/* One of the other waits is open (the treasure's, a scroll, the yes/no or its any-key form, the numeric choice, a
 * page, the Who-will prompt, a pending treasure request), not a kept face's. */
static int other_wait_open(const xeen_game *g)
{
    return !kept_face_wait(g) &&
           (g->treasure.owner || g->scroll_owner || g->yesno || g->event_modal_open || g->choice || g->page_wait ||
            g->who_wait || (g->ui.pending && g->ui.active.kind == XUI_TREASURE));
}
/**
 * The modal's timer wait ended without a key. In the order the original tests them:
 * - Who-will: below its tick count (three with the view's window open, else one; compared signed) a keyless poll
 *   repeats without a redraw until the next pass (who_repoll). An earlier native redrew the face (and its moving
 *   mouth) after one tick, or at every poll below one: up to three times too fast with the view's window open.
 * - The other waits: ticks without a key go back to the wait's scene; so do the yes/no and its any-key form (see
 *   xeen_dat_evidence/phase105/EVENT_PRESENTATION_CORRECTION.md). A kept face's wait decides by its own count.
 * - A kept face: below three ticks the original polls again; a keyless poll changes nothing, so the native waits the
 *   frame out on the timer and polls once per face animation (the replay's timer answers 5: one animation per poll).
 * Returns XEEN_SERVICE_REQUIRED.
 * Original: Who-will 2FB92 4EA1C, 2FB97 JGE SI (SI 3 with window 11 at 2FAFF after 26978(0), else 1 at 2FB8A after
 * 1B503(0) and the face form), the poll 2FB8D, the pass 2FAE3; 2F17D back to the 2F170 scene; 2F2FF's 2F551 (the
 * any-key form: 2F556 JE 2F517); the kept face 2F546 JGE DI (3), 26978.
 */
static xeen_status modal_no_key(xeen_game *g)
{
    if (who_will_too_soon(g)) {
        g->who_repoll = 1;
        return XEEN_SERVICE_REQUIRED;
    }
    if (other_wait_open(g) && g->timer_registers.ax) {
        g->modal_scene_ready = 0;
    }
    if (kept_face_wait(g)) {
        if (g->timer_registers.ax < 3u || g->timer_registers.ax >= 0x8000u) {
            g->modal_poll_pending = MODAL_POLL_TICKS;
            g->modal_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
            return XEEN_SERVICE_REQUIRED;
        }
        g->modal_scene_ready = 0;
    }
    return XEEN_SERVICE_REQUIRED;
}
/**
 * The modal wait read COMMAND (non-zero): the next pass starts a new scene. Each wait takes only its
 * own keys; any other returns to its scene and poll:
 * - the yes/no (2F561..2F571): Esc, N and Y;
 * - Who-will (2FBB2..2FBC5, CS:42B8): Esc and F1..F6 (who_refused; its scene is 2FAE3: 4EA0D,
 *   1B503(0), 4F6F8(24h), 50BF6(3));
 * - the choice (2F7AE..2F7D6): '1'..'9' up to its SI (F5FE - 30h kept for a digit above it).
 * The key then goes to its owner, in this order: the scroll (scroll_key), the treasure
 * (treasure_key), the lock picking (unlock_key), else the event UI's response. Returns the owner's
 * status or XEEN_SERVICE_REQUIRED.
 */
static xeen_status route_modal_key(xeen_game *g, uint16_t command)
{
    g->modal_scene_ready = 0;
    g->modal_face = 0;
    if (g->yesno && command != XEEN_KEY_ESCAPE && command != 'N' && command != 'Y') {
        ++g->confirm_ignored;
        return XEEN_SERVICE_REQUIRED;
    }
    if (g->who_wait && !g->scroll_owner) {
        xeen_status st;
        if (who_refused(g, command, &st)) {
            return st;
        }
    }
    if (g->choice && (command < '1' || command > '9' || command - (unsigned)'0' > g->choice)) {
        if (command >= '1' && command <= '9') {
            xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, (uint16_t)(command - 0x30u));
        }
        ++g->confirm_ignored;
        return XEEN_SERVICE_REQUIRED;
    }
    if (g->scroll_owner) {
        return scroll_key(g);
    }
    if (g->treasure.owner) {
        return treasure_key(g, command);
    }
    if (g->unlock_stage == UNLOCK_WHO) {
        return unlock_key(g, command);
    }
    g->response = command;
    g->response_ready = 1;
    drive(g);
    return XEEN_SERVICE_REQUIRED;
}
/**
 * xeen_game_poll_input's owner for the other modal waits: a modal dialog, a scroll, a page wait or a
 * Who-will prompt. One call is one step of the wait's cycle, in order:
 * 1. the scene, when none is ready: the combat side's (modal_combat_scene), a kept face's
 *    (modal_face_scene, which goes on to the poll), else the host's scene (request_modal_scene);
 * 2. a Who-will pass's face form or present (present_who_will_form);
 * 3. the 189E3 key poll (start_modal_key_poll), unless one is pending;
 * 4. the 4EA1C wait after it (the palette port writes it makes go to palette_port);
 * 5. the outcome: no key (modal_no_key), else the key's owner (route_modal_key).
 * Returns XEEN_SERVICE_REQUIRED while the wait goes on, a timer or poll status, or XEEN_BAD_INPUT
 * after a bad palette port write.
 */
xeen_status poll_modal(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    uint16_t command;
    if (!g->modal_scene_ready && modal_combat_scene_owner(g)) {
        return modal_combat_scene(g, bios, user);
    }
    if (!g->modal_scene_ready && g->event_modal_open && g->modal.active &&
        xeen_read16(&g->movement, XEEN_DS_ANIMATION_SPRITES)) {
        status = modal_face_scene(g, bios, user);
        if (status != XEEN_OK) {
            return status;
        }
    }
    if (!g->modal_scene_ready) {
        return request_modal_scene(g);
    }
    status = present_who_will_form(g);
    if (status != XEEN_OK) {
        return status;
    }
    if (g->modal_poll_pending == MODAL_POLL_START) {
        status = start_modal_key_poll(g);
        if (status != XEEN_OK) {
            return status;
        }
    }
    if ((status = timer_ticks(g, &g->modal_elapsed, bios, user)) != XEEN_OK) {
        return status;
    }
    g->modal_poll_pending = MODAL_POLL_START;
    command = xeen_read16(&g->movement, XEEN_DS_INPUT_CODE);
    g->who_repoll = 0;
    if (!command) {
        return modal_no_key(g);
    }
    return route_modal_key(g, command);
}
