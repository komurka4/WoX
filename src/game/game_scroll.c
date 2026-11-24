/*
 * The message scroll: a framed one-message window over the party bar that waits for a key (open_scroll), and
 * what closing it continues (scroll_key: each enum scroll_owner resumes the command that opened it). Its users
 * here: the "The Party needs rest!" scrolls of a clock charge (rest_scroll, desert_scroll) and the Who-will
 * prompt's refusal of a member who cannot act (who_refused).
 * Original: 18F2F with mode 1 (window 6, the DS:4B7A format, the text routing pushed); its key 18FBF.
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Where the Who-will refusal stops (tools/xeen_replay/stops.json). */
enum {
    WHO_REFUSED_STOP_TEXT = 0x28d32,         /* the refusal does not fit its buffer, or the party is at a location */
    WHO_REFUSED_STOP_SCROLL = 0x28d4c        /* the refusal's scroll could not be opened */
};
/*
 * Opens the message scroll for OWNER with TEXT (already formatted): window 6, (52, 149) 216 x 48 over the party
 * bar, its pixels saved, framed and written from its inner origin. While it is open the button stack is pushed,
 * the input code cleared and the UI mode saved and set to FFh (the scene passes it runs then have no monster
 * countdown and no contact combat); TICK is the suspended clock charge the owner resumes when the key closes it.
 * The wait is the shared modal loop (a scene, then polls until a key or a timer tick) at
 * SCROLL_MESSAGE_POLL_BOUNDARY. Only the Who-will refusal opens over an NPC's view window; otherwise an open
 * dialogue, modal or scroll refuses. Returns 0 when the scroll cannot be presented (the caller keeps its own stop),
 * 1 when it is open.
 * Original: 18F2F(text, 1): 190ED/1919B push the text routing, F5FE = 0, 9A1C saved and FFh, 14BBA with the
 * DS:4B7A format, 1871B(6) opens window 6 (986C+6, the 3064/9843 window stack); the loop 18FA1 (4EA0D/1B503(1),
 * then 189E3 until a key or a 4EA1C tick). 30DB3 opens it with DS:629B.
 */
int open_scroll(xeen_game *g, int owner, const char *text, xeen_gameplay_tick tick)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    /* The Who-will refusal may open over an NPC's view window: window 6 (rows 149..196) lies below the view
     * window (rows 8..139), and the loop's scene returns early while the view window is open.
     * Original: 28CFC's 18F2F with 9877 set; 18FA1's 1B503(1) returns at 1B517 (the scene's 1C1B3 path). */
    int over11 = owner == SCROLL_WHO_REFUSED && g->dialogue_window.active && ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)];
    if (!g->presentation || g->modal.active || (g->dialogue_window.active && !over11) ||
        g->scroll_owner) {
        return 0;
    }
    if (!g->text_window.loaded && xeen_text_window_load(&g->text_window, &g->assets) != XR_OK) {
        return 0;
    }
    /* Window 6 over the party bar: the scene the wait runs does not redraw the bar, so the scroll stays until
     * the key restores the saved pixels. Original: 1871B(6) and 4E38C(6) (4F2A1 saves; 1BF75 skips the bar;
     * 18765(6) restores). */
    {
        xeen_presentation *p = g->presentation;
        uint16_t r[4];
        if (!p->framebuffer || p->framebuffer_size < XEEN_PRESENT_PIXELS ||
            !xeen_presentation_window(p, 6, r)) {
            return 0;
        }
        panel_under(g, 6, 0);
        xeen_presentation_trace_text(6, text); /* Evidence: 18F75's 4E38C */
        if (xeen_text_frame(p->framebuffer, p->framebuffer_size, r) != XEEN_OK ||
            !g->text_window.loaded) {
            panel_under(g, 6, 1);
            return 0;
        }
        /* The frame, then the text from the carried colour (page_layout), unjustified and full size.
         * Original: 1871B(6)'s frame (4F2A1: C0C = C0D = 0), 4E38C (xeen_dat_evidence/phase154). */
        page_layout(g, r);
        g->layout.justify = 0;
        g->layout.reduced = 0;
        g->layout.x = g->layout.left;
        g->layout.y = g->layout.top;
        (void)xeen_text_layout_run(&g->layout, &g->text_window, p->framebuffer,
                                   (const uint8_t *)text, strlen(text) + 1u, 0);
        if (xeen_presentation_update_window(p, XEEN_WINDOW_SCROLL) != XEEN_OK) {
            panel_under(g, 6, 1);
            return 0;
        }
    }
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    g->scroll_mode = ds[XEEN_DS_UI_MODE];
    ds[XEEN_DS_UI_MODE] = 0xff;
    xeen_window_open(m, XEEN_WINDOW_SCROLL);
    g->scroll_owner = owner;
    g->scroll_tick = tick;
    ++g->ui.presentations;
    g->modal_scene_ready = 0;
    g->modal_poll_pending = MODAL_POLL_START;
    g->navigation = tick.navigation;
    (void)input_wait(g, SCROLL_MESSAGE_POLL_BOUNDARY);
    return 1;
}
/** Opens the "The Party needs rest!" scroll for OWNER. */
int rest_scroll(xeen_game *g, int owner, xeen_gameplay_tick tick)
{
    return open_scroll(g, owner, "\003c\v010\t000\v012The Party needs rest!", tick);
}
/*
 * The "needs rest" scroll of the desert's clock charge (the surface step after a command), which can follow a
 * move, Shoot, the Space wait or Rest's wake: CALLER (DESERT_*) is the command whose tail continues once the scroll
 * closes (desert_scroll_closed). Bash's forward step shares the Space wait's path, but only indoors, where the
 * desert charge never runs. Returns 0, opening nothing, outside continuous play or when the scroll cannot open.
 * Original: 17264's 172CD charge reaching 30DB3; its callers 17EFA (Shoot's 17EF4 tail), 17D51 (Space,
 * 17DB2..17D50), 2911F (Rest's wake) and the movement; the close 30DC2, 30DCA, RETF 172D5.
 */
int desert_scroll(xeen_game *g, int caller, xeen_gameplay_tick tick)
{
    if (!g->continuous || !rest_scroll(g, SCROLL_DESERT_REST, tick)) {
        return 0;
    }
    g->desert_caller = caller;
    return 1;
}
/* The Who-will wait in its own loop: no NPC view window open and no scroll, modal or page wait nested in it.
 * Original: 2F9BA's pass 2FB05..2FB8A (9877 clear). */
int who_plain(const xeen_game *g)
{
    return g->who_wait && !g->movement.ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)] && !g->scroll_owner &&
           !g->modal.active && !g->page_wait;
}
/*
 * The Who-will prompt's key COMMAND. Esc and an F key of a member able to act are the service's (returns 0). Any
 * other key, and an F key past the party, is ignored: the loop polls again (returns 1). A member whose worst
 * condition forbids acting (xeen_training_refused: asleep, paralysed, unconscious, dead, stone, eradicated) is refused
 * with "<name> is not in any condition to perform actions!" in the message scroll (SCROLL_WHO_REFUSED), after
 * which the loop polls again (returns 1). The refusal stops (WHO_REFUSED_STOP_*, *STATUS left SERVICE_REQUIRED)
 * when its text does not fit or the party is at a town location, or when the scroll cannot open.
 * Original: 2FBB2..2FC02 (the CS:42B8 key table; 2FBC5 and 2FBE7 back to 2FAE3); 2FBF6 28CFC (worst condition 8
 * or 11..15): A43F = 14BBA(DS:[18CA], the name), 18F2F(A43F, 1) (mode 2 with 9A1C 11h), 2FC02 JMP 2FAE3.
 */
int who_refused(xeen_game *g, uint16_t command, xeen_status *status)
{
    uint8_t *ds = g->movement.ds;
    unsigned member;
    char text[0x80], out[0xa0];
    int k;
    *status = XEEN_SERVICE_REQUIRED;
    if (command == XEEN_KEY_ESCAPE) {
        return 0;
    }
    if (command < XEEN_KEY_F1 || command > XEEN_KEY_F6 ||
        (member = command - XEEN_KEY_F1) >= ds[XEEN_DS_PARTY_COUNT]) {
        ++g->confirm_ignored;
        return 1;
    }
    if (!xeen_training_refused(ds + XEEN_PARTY_RECORDS + member * XEEN_CHARACTER_BYTES)) {
        return 0;
    }
    k = snprintf(text, sizeof text, (const char *)ds + xeen_read16(&g->movement, 0x18ca),
                 (const char *)ds + XEEN_PARTY_RECORDS + member * XEEN_CHARACTER_BYTES);
    if (k < 0 || (size_t)k >= sizeof text || ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_LOCATION) {
        g->phase = XG_STOPPED;
        g->lifecycle_boundary = WHO_REFUSED_STOP_TEXT;
        g->command_status = XGC_UNSUPPORTED;
        return 1;
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER_0, text, (size_t)k + 1u);
    k = snprintf(out, sizeof out, (const char *)ds + XEEN_DS_MESSAGE_FORMAT, text);
    /* Also over an NPC's view window (open_scroll): mode 1, its loop without a scene. */
    if (k < 0 || (size_t)k >= sizeof out ||
        !open_scroll_message(g, SCROLL_WHO_REFUSED, out)) {
        g->phase = XG_STOPPED;
        g->lifecycle_boundary = WHO_REFUSED_STOP_SCROLL;
        g->command_status = XGC_UNSUPPORTED;
        return 1;
    }
    return 1;
}
/* The desert's "needs rest" scroll closed: the dawn is no longer pending, the rest of the surface step runs, then
 * the tail of the command that opened it (Shoot, the Space wait, Rest's wake, or the move's post-action and
 * navigation).
 * Original: 30DC2 2ABB8(1) (presentation), 30DCA 9842 = 0, RETF to 172D5 (17264's surface); the tails Shoot
 * 17EFF, Space 17D56, Rest's wake 29124. */
static xeen_status desert_scroll_closed(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_caller_result c;
    xeen_gameplay_tick tick = g->scroll_tick;
    int caller = g->desert_caller;
    m->ds[XEEN_DS_DAWN_PENDING] = 0;
    g->desert_caller = DESERT_MOVEMENT;
    c = xeen_post_action_surface_after_desert_native(m);
    if (caller == DESERT_SHOOT) {
        return shoot_after_surface(g, c); /* 17EFF onward */
    }
    if (caller == DESERT_SPACE) {
        return space_after_surface(g, c); /* 17D56 onward */
    }
    if (caller == DESERT_REST_WAKE) {
        return rest_wake_after_surface(g, c); /* 29124 onward */
    }
    if (c.status == XEEN_OK) {
        c = xeen_post_action_after_surface_native(m, game_audio(g));
    }
    tick.boundary = c.boundary;
    if (c.status != XEEN_SERVICE_REQUIRED) {
        tick.status = XT_INPUT_ERROR;
    }
    (void)navigation_finish(g, tick);
    g->scene_argument = 1;
    return XEEN_SERVICE_REQUIRED;
}
/* A clock charge's "needs rest" scroll closed (OWNER below SCROLL_REST_DANGER, or the Space wait's): the suspended
 * charge resumes with SERVICES and its command continues (the move's post-action, the Space wait, Shoot or Bash).
 * Original: the tick resumes at 30DC2; Shoot 17EF4, Bash 17CEA. */
static xeen_status tick_scroll_closed(xeen_game *g, int owner, xeen_gameplay_services *services)
{
    xeen_gameplay_tick tick = xeen_gameplay_tick_resume_rest(&g->movement, g->scroll_tick, g->monster_database,
                                                             g->monster_database_size, services);
    if (owner == SCROLL_SPACE_REST) {
        return space_after_tick(g, tick); /* The Space wait's charge */
    }
    if (owner == SCROLL_SHOOT_REST) {
        return shoot_after_tick(g, tick);
    }
    if (owner == SCROLL_BASH_REST) {
        return bash_after_tick(g, tick);
    }
    (void)navigation_tail(g, tick);
    g->scene_argument = 1;
    return XEEN_SERVICE_REQUIRED;
}
/*
 * The key that closes the message scroll: the window's pixels restored, the input code cleared, the button stack
 * popped and the UI mode restored; then the owner continues. A clock charge's scroll resumes the charge and its
 * command; the event, payment and dispatcher scrolls hand control back to the interpreter's driver; the Cast,
 * Dismiss, packs-full, combat-rest and desert scrolls have their own continuations; Rest's completion scroll first
 * checks the party (xeen_combat_party_check), then both Rest scrolls return to Rest's caller; the Who-will refusal
 * polls again.
 * Original: 18FBF: 18765(6), 189BC (buttons released, F5FE cleared), 18FFF, 9A1C restored; the tick at 30DC2;
 * Rest's 17F07 after 29164 (danger) and after 2F869 (completion).
 */
xeen_status scroll_key(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    int owner = g->scroll_owner;
    xeen_gameplay_services services = session_services(g);
    if (owner == SCROLL_PAYMENT || owner == SCROLL_NO_SAVE) {
        xeen_presentation_trace_extra("evclose 6");
        xeen_presentation_trace_extra("evrestore 6");
    } /* Evidence: 18765(6), 4F57B */
    panel_under(g, 6, 1);
    xeen_window_close(m, XEEN_WINDOW_SCROLL);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    ds[XEEN_DS_UI_MODE] = g->scroll_mode;
    g->scroll_owner = 0;
    g->modal_scene_ready = 0;
    g->modal_poll_pending = MODAL_POLL_START;
    ++g->ui.completions;
    if (owner == SCROLL_REST_DONE) {
        xeen_combat_party_check(m);
    }
    switch (owner) {
    case SCROLL_CAST_REFUSED:
    case SCROLL_SPELL_FAILED:
        return cast_scroll_closed(g, owner); /* The Cast dialog's */
    case SCROLL_EVENT: /* 18F2F's return to 2DECE */
        g->event_scroll = EVENT_MESSAGE_CLOSED;
        g->phase = XG_STOPPED;
        drive(g);
        return XEEN_SERVICE_REQUIRED;
    case SCROLL_NO_SAVE: /* 2952D 1871B(17h), 296DA: the S key's 2987D */
        xeen_presentation_trace_extra("evend");
        panel_window(g, XEEN_WINDOW_CONTROL_PANEL, 1);
        if (g->panel_return == SAVE_BACK_TO_DIALOG) {
            return panel_dialog_done(g, g->menu_selected); /* 2B368 -> 2B268 (the Save dialog) */
        }
        return panel_redraw(g);
    case SCROLL_PAYMENT: /* 29278, 291FB's 18FFF */
        xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
        xeen_presentation_trace_extra("evend");
        g->event_payment = EVENT_MESSAGE_CLOSED;
        g->phase = XG_STOPPED;
        drive(g);
        return XEEN_SERVICE_REQUIRED;
    case SCROLL_PACKS_FULL:
        return treasure_full_closed(g);
    case SCROLL_DISPATCH_FULL: /* The dispatcher's 2EC49 */
        xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
        g->treasure.dispatch_full = DISPATCH_FULL_CLOSED;
        g->phase = XG_STOPPED;
        drive(g);
        return XEEN_SERVICE_REQUIRED;
    case SCROLL_COMBAT_REST:
        return combat_rest_closed(g); /* 30DB8 in combat */
    case SCROLL_DISMISS_REFUSED:
        return dismiss_scroll_closed(g); /* 2B8A2, Dismiss's refusal */
    case SCROLL_WHO_REFUSED:  /* 28CFC's 18F2F, back to 2FAE3 */
        ++g->confirm_ignored; /* the loop polls again (the host's queue) */
        g->phase = XG_WAITING;
        g->lifecycle_boundary = WHO_WILL_POLL_BOUNDARY;
        g->modal_scene_ready = 0;
        g->modal_poll_pending = MODAL_POLL_START;
        g->command_status = XGC_PENDING_INPUT;
        return XEEN_SERVICE_REQUIRED;
    case SCROLL_DESERT_REST:
        return desert_scroll_closed(g);
    case SCROLL_REST_DANGER:
    case SCROLL_REST_DONE:
        return rest_return(g); /* Rest's danger and completion scrolls */
    default:
        if (owner >= SCROLL_REST_DANGER && owner != SCROLL_SPACE_REST) {
            return command_tail(g);
        }
        return tick_scroll_closed(g, owner, &services);
    }
}
