/*
 * The event interpreter's host services (Phase 246, from game.c): event_service, the callback through
 * which an event operation runs the original routines it calls (by their addresses, in
 * event_service_inner's dispatch), the serve_* routines (damage, the treasure dialog, the member flash, the window
 * restorations, the yes/no wait, the choice window, the random and item services and the others). The NPC
 * dialogue form 3000D and the Who-will prompt are game_event_dialogue.c's. The event windows and the
 * text displays are game_event_windows.c's, the presentation of the UI requests game_event_ui.c's;
 * drive() completes the scenes they request.
 */
#include "game_event.h"
#include "xeen/monster_record.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int event_service_inner(void *, const xeen_event_request *, uint8_t *, size_t, uint16_t *);
/* The interpreter's host requests (event_service_inner), with an evidence line ('npcend') when an NPC dialogue, a
 * numeric choice or a yes/no completes (presentation tracing only). Original: their RETFs 305E7, 2F833, 2F5B0. */
int event_service(void *u, const xeen_event_request *r, uint8_t *ds, size_t n, uint16_t *ax)
{
    int handled = event_service_inner(u, r, ds, n, ax);
    if (handled == 1 &&
        (r->address == XEEN_REQUEST_NPC_DIALOGUE || r->address == XEEN_REQUEST_NUMERIC_CHOICE || r->address == XEEN_REQUEST_YES_NO)) {
        xeen_presentation_trace_extra("npcend");
    }
    return handled;
}
/*
 * The dispatcher's "Nothing here." (Space at an object's cell that has no record for any direction) is a request the
 * dispatcher repeats until it is answered: its first pass shows the text in window 38 (nothing_here_open); every
 * pass then asks the event UI for the key, as a page of displayed text waits (the session's modal poll redraws the
 * scene and polls the keyboard meanwhile); the key closes the window (nothing_here_close). Nothing but the window's
 * bytes and the key code changes.
 */
#define NOTHING_HERE_TEXT_MAX 64u /* the program data's text: its NUL within 64 bytes */
/* The "Nothing here." wait's flags: ON while its key is awaited (nothing_wait, and page_wait, which the session's
 * poll serves as any page wait); either way the modal poll starts its pass afresh. */
static void nothing_here_wait(xeen_game *g, int on)
{
    g->nothing_wait = on;
    g->page_wait = on;
    g->modal_scene_ready = 0;
    g->modal_poll_pending = MODAL_POLL_START;
}
/* The first pass: window 38 opened with the text and presented, then the wait begun. Returns 1 when the wait has
 * begun; 0 when the font or the text window cannot be loaded or the text not shown (window 38 may already be open
 * then: the next pass opens it again, a no-op, and shows the text again); -1 when the text has no NUL within its
 * bounds or the window cannot be presented. Original: 337F0..3380D: 1871B(26h), 4E38C(DS:63B1, 26h), 50BF6(26h). */
static int nothing_here_open(xeen_game *g, const uint8_t *ds)
{
    const uint8_t *text = ds + XEEN_TEXT_NOTHING_HERE;
    uint8_t shown[NOTHING_HERE_TEXT_MAX + 4u];
    size_t length = 0;
    while (length < NOTHING_HERE_TEXT_MAX && text[length]) {
        ++length;
    }
    if (text[length]) {
        return -1;
    }
    if (!g->text_window.loaded && xeen_text_window_load(&g->text_window, &g->assets) != XR_OK) {
        return 0;
    }
    xeen_presentation_trace_extra("evmark event-nothing");
    xeen_presentation_trace_extra("evopen 38");
    xeen_window_open(&g->movement, XEEN_WINDOW_EVENT_TEXT);
    trace_event_text(38, text, length + 1u);
    /* The text has no CR: its 0B 010 sets y and 03 c centres the line, so the carried 4E38C cursor does not change
     * the draw; the window-38 renderer takes its CR-headed form. */
    shown[0] = 13;
    memcpy(shown + 1, text, length + 1u);
    if (xeen_text_window_show(&g->text_window, g->presentation, shown, length + 2u) != XEEN_OK) {
        return 0;
    }
    if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_EVENT_TEXT) != XEEN_OK) {
        return -1;
    }
    nothing_here_wait(g, 1);
    return 1;
}
/* The key came: the wait ends (before the close, so a failed restore leaves no wait behind), window 38 is closed and
 * the screen under it restored. Returns 1, or -1 when the restore fails. Original: 33834 18765(26h). */
static int nothing_here_close(xeen_game *g)
{
    nothing_here_wait(g, 0);
    xeen_presentation_trace_extra("evclose 38");
    if (!window_close_restore(g, XEEN_WINDOW_EVENT_TEXT)) {
        return -1;
    }
    xeen_presentation_trace_extra("evend");
    return 1;
}
/* Serves "Nothing here.": the opening on the first pass, the key on every pass (the event UI's answer, AX its key,
 * returned until it is 1), then the close.
 * Original: 337D7 (337F0..3383B after 186C0); the key 3380E..33832: the 4EA0D/1B503(1) scene and the 3381B 189E3
 * poll (4EA1C between polls) until F5FE is nonzero, as 316DC's 31786 page wait. */
static int serve_nothing_here(xeen_game *g, uint8_t *ds, size_t n, uint16_t *ax)
{
    xeen_event_request key = {XEEN_REQUEST_NOTHING_HERE_KEY, 0, 0, 0, 0};
    int handled;
    if (!g->nothing_wait) {
        int opened = nothing_here_open(g, ds);
        if (opened != 1) {
            return opened;
        }
    }
    handled = xeen_event_ui_service(&g->ui, &key, ds, n, ax);
    if (handled != 1) {
        return handled;
    }
    return nothing_here_close(g);
}

/*
 * The event scene handshake (event_scene, EVENT_SCENE_*): a service that needs the full
 * exploration scene 1B503(1) asks for it and suspends (answers 0); drive() runs the scene
 * (REQUESTED -> RUNNING -> COMPLETED) and re-enters the interpreter, whose repeated request then
 * takes the completion and continues. The 1B503 service itself, the party damage's scene
 * (2E8B3) and the dispatcher treasure's (2EBB3) use it; the member flash's frames keep their own.
 */
/** Asks for the event scene: an idle handshake becomes REQUESTED (a pending one is kept). */
static void scene_request(xeen_game *g)
{
    if (g->event_scene == EVENT_SCENE_IDLE) {
        g->event_scene = EVENT_SCENE_REQUESTED;
    }
}
/** Takes a completed event scene: returns 1 and makes the handshake idle, else 0 (still pending). */
static int scene_take_completion(xeen_game *g)
{
    if (g->event_scene != EVENT_SCENE_COMPLETED) {
        return 0;
    }
    g->event_scene = EVENT_SCENE_IDLE;
    return 1;
}

/* Whether window W's pixels are restored by the window-restore service when the damage closes it: the windows it
 * models while they show (the event text, the bottom panel, the speech face, the NPC dialogue in the view's
 * window); any other window keeps the damage's DS-only close. */
static int damage_restores_window(const xeen_game *g, unsigned w)
{
    return (w == XEEN_WINDOW_EVENT_TEXT && g->text_window.active) || (w == XEEN_WINDOW_BOTTOM && g->bottom_active) ||
           (w == XEEN_WINDOW_SPEECH && g->face_active) || (w == XEEN_WINDOW_VIEW && g->dialogue_window.active);
}
/* The pixel half of the damage's closing of every open window of the window stack, from the top slot (40) down: each
 * open window the restore service models (damage_restores_window) is restored through it;
 * xeen_combat_party_damage_hits does the DS half (xeen_window_close_all). Returns 1, or -1 at the first restore
 * that fails. (An earlier native left an operation 1 text on screen through the damage frames: Clouds map 32's
 * bottle, "On closer inspection..." then 15 damage.) Original: 2E68D's 186C0 at 2E696 over the 9843 stack, 4F57B. */
static int damage_restore_windows(xeen_game *g, uint8_t *ds, size_t n)
{
    for (int slot = 40; slot >= 0; --slot) {
        unsigned w = ds[XEEN_DS_WINDOW_STACK + (unsigned)slot];
        xeen_event_request restore = {XEEN_REQUEST_WINDOW_RESTORE, (uint16_t)w, 0, 0, 0};
        uint16_t unused = 0;
        if (!ds[(uint16_t)(XEEN_DS_WINDOW_OPEN + w)] || !damage_restores_window(g, w)) {
            continue;
        }
        if (event_service_inner(g, &restore, ds, n, &unused) != 1) {
            return -1;
        }
    }
    return 1;
}
/**
 * Serves a script's party damage (operation 22) with its scene: the open windows' pixels restored
 * (damage_restore_windows), the members' damage and hit frames, then the two-tick scene as the event scene (a frame
 * mark and one whole scene pass); after it the party bar redrawn and the party checked. A stop inside the damage
 * (its boundary) suspends here.
 * Original: 32F41's 2E68D(damage, type, member); 2E8B3's 30E2D(2) (4EA0D, 1B503(1)); 2ABB8(1), 2F869.
 */
static int serve_party_damage(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n)
{
    uint32_t boundary = 0;
    if (!g->damage_event) {
        if (damage_restore_windows(g, ds, n) != 1) {
            return -1;
        }
        if (xeen_combat_party_damage_hits(&g->combat, &g->movement, g->monster_database,
                                          g->monster_database_size, r->argument,
                                          (unsigned)(int16_t)r->second_argument, r->destination,
                                          g->event_random, g->event_random_user,
                                          game_audio(g),
                                          g->presentation, &boundary) != XEEN_OK) {
            return 0;
        }
        g->damage_event = 1;
        g->scene_site = 0x30e3cu;
        scene_request(g);
        return 0;
    }
    if (!scene_take_completion(g)) {
        return 0;
    }
    g->damage_event = 0;
    bar_redraw(g);
    xeen_combat_party_check(&g->movement);
    return 1;
}


/*
 * The dispatcher's own treasure dialog before its prompt (treasure_dialog_head) runs in three stages, each a
 * request of the service: the opening scene (asked for, or the treasure deferred), the dialog's opening after that
 * scene (which may stop at the "BackPacks Full!" scroll for drive() to open), and the return from that scroll.
 * Original: 2EA68; its head 2EA98..2EC44; the scroll's return 2EC49.
 */
/* The return from the dispatcher's "BackPacks Full!" scroll: the item loop begins with nothing discarded yet (its
 * checks discard what no pack takes), as for the command owners; the message buffer as the dialog's opening leaves
 * it ("Hit a key"; the item lines write their own first). Returns whether item lines come before the key.
 * Original: 2EC49, on at 2EC4C with [BP-2] = 0; the loop 2F141 (2EC59); AC0F from 2F14A (DS:61FC). */
static int treasure_full_scroll_closed(xeen_game *g, uint8_t *ds)
{
    g->treasure.dispatch_full = DISPATCH_FULL_NONE;
    int stage_items = xeen_treasure_has_items(&g->movement);
    memmove(ds + XEEN_DS_TEXT_BUFFER_2, ds_text(g, XEEN_TEXT_HIT_A_KEY), strlen(ds_text(g, XEEN_TEXT_HIT_A_KEY)) + 1u);
    return stage_items;
}
/* The dialog's opening scene: a monster in view defers the treasure, nothing written (unless the UI mode is 9):
 * answers 1. Otherwise the missiles are cleared and one whole exploration scene pass is asked for (the event scene,
 * re-entering at 2EBB8): answers 0. Original: 2EA98..2EB99 (9A1C, the monsters 98B4..98CD; deferred at 2F22F);
 * 2EB9C: 9744..974B and 97F9 cleared, 1B503(1) at 2EBB3 (its own 4EA35, no 4EA0D). */
static int treasure_opening_scene(xeen_game *g, uint8_t *ds)
{
    if (xeen_treasure_check(&g->movement) == XEEN_TREASURE_DEFERRED) {
        return 1;
    }
    memset(ds + XEEN_DS_MISSILE_SLOTS, 0, 8);
    ds[XEEN_DS_MISSILES_FLYING] = 0;
    g->treasure.head = 1;
    g->scene_site = 0x2ebb8u;
    scene_request(g);
    return 0;
}
/* Whether the pending treasure has gold or gems (either dword nonzero). */
static int treasure_currency_pending(xeen_game *g)
{
    return xeen_read16(&g->movement, XEEN_DS_TREASURE_GEMS) || xeen_read16(&g->movement, XEEN_DS_TREASURE_GEMS + 2u) ||
           xeen_read16(&g->movement, XEEN_DS_TREASURE_GOLD) || xeen_read16(&g->movement, XEEN_DS_TREASURE_GOLD + 2u);
}
/* The dialog's opening after its scene: the treasure's sound when there is gold or gems; the dialog opened as the
 * command owners' is in treasure_dialog (the message buffer, UI mode 7) and the right column (window 10) reopened.
 * Then every pack full stops the head for the "BackPacks Full!" scroll (2EA68 is one routine for every caller, the
 * command owners' too; drive() opens it, dispatch_full_open, and its key re-enters the head): answers 0 with
 * *RESULT 0. Else the dialog goes on to its prompt (1), *STAGE_ITEMS set when items are pending (a 31111 stash
 * leaves the forty records in place): they get the item lines before the key, as the command owners' dialogs, and
 * drive() runs item_next. A failure answers 0 with *RESULT -1.
 * Original: 2EBB9..2EBCA (9A12..9A18), 2EBCC 1880E(36h); 33698 phase 5; 2EB9C/2EC30 (AC0F, 9A1C = 7), window
 * 10's 18765 and 1871B; 2EC37 2F27C, 2EC44 18F2F(DS:6183, 1) "Warning!  BackPacks Full!"; the items 2EC54..2F131. */
static int treasure_dialog_opening(xeen_game *g, int *stage_items, int *result)
{
    if (treasure_currency_pending(g) && sound_effect(g, 54u) != XEEN_OK) {
        *result = -1;
        return 0;
    }
    if (xeen_treasure_open(&g->movement) != XEEN_OK) {
        *result = -1;
        return 0;
    }
    xeen_window_close(&g->movement, XEEN_WINDOW_RIGHT_COLUMN);
    xeen_window_open(&g->movement, XEEN_WINDOW_RIGHT_COLUMN);
    if (xeen_treasure_packs_full(&g->movement)) {
        g->treasure.dispatch_full = DISPATCH_FULL_TO_OPEN;
        return 0;
    }
    *stage_items = xeen_treasure_has_items(&g->movement);
    return 1;
}
/*
 * The dispatcher's own treasure dialog before its prompt (nothing settled, no request pending), by its stage: the
 * return from its full scroll, the opening scene, the opening once that scene has completed. Returns 1 when the
 * dialog goes on to its prompt, with *STAGE_ITEMS set when item lines come before the key; else 0 with *RESULT the
 * service's answer: 1 the treasure is deferred, 0 while the opening's scene or the full scroll runs, -1 a failure.
 */
static int treasure_dialog_head(xeen_game *g, uint8_t *ds, int *stage_items, int *result)
{
    *result = 0;
    if (g->treasure.dispatch_full == DISPATCH_FULL_CLOSED) {
        *stage_items = treasure_full_scroll_closed(g, ds);
        return 1;
    }
    if (!g->treasure.head) {
        *result = treasure_opening_scene(g, ds);
        return 0;
    }
    if (!scene_take_completion(g)) {
        return 0;
    }
    g->treasure.head = 0;
    return treasure_dialog_opening(g, stage_items, result);
}
/*
 * Writes the treasure prompt into the event text buffer, for the event UI to show: the found text ("The Party
 * Found:" with the pending gold and gems; the original's form has no item-count line) in the program data's form,
 * DS:6157. The treasure library owns the text's arguments and the pending test; the dialog's AC0F copy is the
 * library's (xeen_treasure_open). Returns 1 when written; 0 when nothing is pending (no item waiting, no gold, no
 * gems: nothing written); -1 when the text does not fit. Original: 2EBFB's form (there into AC0F).
 */
static int treasure_prompt(xeen_game *g, uint8_t *ds)
{
    if (!xeen_treasure_pending(ds)) {
        return 0;
    }
    int length = xeen_treasure_found_format(ds, ds_text(g, XEEN_TEXT_PARTY_FOUND), (char *)ds + XEEN_DS_TEXT_BUFFER,
                                            XEEN_TEXT_BUFFER_BYTES);
    if (length < 0 || length >= 1000) {
        return -1;
    }
    return 1;
}
/*
 * Settles the treasure after its key: an exploration owner's event text restored (unless item lines were staged);
 * the right column closed; with staged item lines (every item already placed) only the currency is finished, else
 * the pending treasure is collected for the Who-will member (a refusal returns 0); the pixels under window 10
 * restored when the dialog took it; the dialogue window closed, the right column shown, the modal finished and its
 * poll restarted. Returns 1, 0 when refused, -1 on a failure. Original: 2F1A4 (2F196's collection, 18765(0Ah)); the
 * external preference selector 2FC12 is never entered on this path (the preference starts at zero, 310D2 resets it).
 */
static int treasure_settle(xeen_game *g, uint8_t *ds)
{
    /* An exploration owner restores the A827 its window staging used
     * before the settlement's own item-name writes. */
    if (g->treasure.owner && !g->treasure.items_staged) {
        memcpy(ds + XEEN_DS_TEXT_BUFFER, g->event_text, sizeof g->event_text);
    }
    xeen_window_close(&g->movement, XEEN_WINDOW_RIGHT_COLUMN);
    /* The item lines already placed every item (the
     * items_staged dialog); the key settles the currency. */
    if (g->treasure.items_staged) {
        xeen_treasure_finish(&g->movement);
        g->treasure.placed = 0;
        g->treasure.items_staged = 0;
    } else if (xeen_treasure_collect_placed(&g->movement, g->who_will_member,
                                            &g->treasure.placed) != XEEN_OK) {
        return 0;
    }
    /* 2F1A4's 18765(0Ah): 4F57B restores window 10. */
    if (g->treasure.window_owner) {
        panel_under(g, XEEN_WINDOW_RIGHT_COLUMN, 1);
        g->treasure.window_owner = TREASURE_WINDOW_NONE;
    }
    if ((g->dialogue_window.active &&
         xeen_dialogue_close(&g->dialogue_window, g->presentation) != XEEN_OK) ||
        xeen_presentation_update_window(g->presentation, XEEN_WINDOW_RIGHT_COLUMN) != XEEN_OK ||
        xeen_modal_finish(&g->modal, &g->movement, XEEN_DS_SEGMENT) != XEEN_OK) {
        return -1;
    }
    g->modal_scene_ready = 0;
    g->modal_poll_pending = MODAL_POLL_START;
    return 1;
}
/*
 * Presents the written treasure prompt through the event UI (request R) and returns its answer. With HIT_A_KEY (the
 * dispatcher's dialog without item lines), the request that has just presented it, now waiting at its key, writes
 * "Hit a key" into the right column when the dialog has framed it; item lines draw their own.
 * Original: 2F14A..2F16A (AC0F = DS:61FC, "Hit a key", in window 10) before the 2F170 loop.
 */
static int treasure_prompt_present(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n, uint16_t *ax,
                                   int hit_a_key)
{
    size_t shown = g->ui.presentations;
    int handled = xeen_event_ui_service(&g->ui, r, ds, n, ax);
    if (hit_a_key && handled == 0 && g->ui.pending && g->ui.presentations != shown && g->treasure.window_owner) {
        treasure_window(g);
    }
    return handled;
}
/*
 * Serves the treasure dialog of an event or of the dispatcher (a request with any argument is not this service: 0).
 * The dispatcher's own dialog first runs its head (treasure_dialog_head: the deferral, the opening scene, the full
 * packs' scroll). The prompt is written (nothing pending answers at once) and shown through the event UI; the
 * dispatcher keeps the event text buffer as it was, and its first presentation without item lines gets the "Hit a
 * key" window. Item lines due before the key are handed to drive(); the key settles (treasure_settle).
 * Original: 2EA68.
 */
static int serve_treasure_dialog(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n,
                                 uint16_t *ax)
{
    int handled, prompt, stage_items = 0;
    uint8_t kept[1000];
    /* Every request but the command owners' opening (treasure_dialog_open stages it) is 2EA68's own: the
     * dispatcher's, and the key's repeated request of either owner. */
    int dispatcher = !g->treasure.staged;
    if (r->argument || r->second_argument || r->destination || r->count) {
        return 0;
    }
    if (dispatcher && !g->ui.pending && !treasure_dialog_head(g, ds, &stage_items, &handled)) {
        return handled;
    }
    memcpy(kept, ds + XEEN_DS_TEXT_BUFFER, sizeof kept);
    prompt = treasure_prompt(g, ds);
    if (prompt <= 0) {
        return prompt;
    }
    handled = treasure_prompt_present(g, r, ds, n, ax, dispatcher && !stage_items);
    /* The prompt went to the window through A827; 2EA68 itself leaves A827 (an event's last common text copy) as
     * it was. */
    if (dispatcher) {
        memcpy(ds + XEEN_DS_TEXT_BUFFER, kept, sizeof kept);
    }
    /* Waiting at its key with item lines due first: drive() runs them (item_next). */
    if (stage_items && handled == 0 && g->ui.pending) {
        g->treasure.items_staged = 1;
        g->treasure.item_cursor = 0;
        g->treasure.dispatch_items = 1;
    }
    return handled == 1 ? treasure_settle(g, ds) : handled;
}

/*
 * The member flash (an event's, 2B9C7(member)): a start, four frames and an end. The frames run across the event
 * scene handshake: a frame whose scene pass is due draws its sprite, asks for the scene and suspends (the service
 * answers 0); drive() runs the scene, and the repeated request shows that frame and goes on. flash_stage is set
 * while the flash runs, flash_frame is the next frame, flash_countdown the held monster countdown. The heal's flash
 * (cast_flash_frame) is the same routine reached from 3977A, with its own continuation.
 */
enum {
    MEMBER_FLASH_FRAMES = 4,  /* 2BAAC: CMP DI, 4 */
    MEMBER_FLASH_SOUND = 20,  /* 2BA45: 1880E(14h) */
    MEMBER_FLASH_SPRITE_Y = 150 /* 2BA5A: PUSH 96h */
};
/* The flash's start: the bottom panel closed if it is open (with its evidence marks), the monsters' countdown held at
 * 0 (its value kept for the end), the flash's sound. The flash runs from frame 0 only once the sound has played; a
 * failed sound leaves it not started (with the countdown already 0). Original: 2BA06..2BA14 18765(0Ch); 2BA38..2BA40
 * 96C0 into [BP-2] and 0; 2BA45 1880E(14h). */
static xeen_status member_flash_start(xeen_game *g, uint8_t *ds)
{
    if (ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_BOTTOM)]) {
        xeen_presentation_trace_extra("evclose 12"); /* Evidence: 2BA0F's 18765(12) */
        if (g->bottom_active) {
            xeen_presentation_trace_extra("evrestore 12");
        }
        bottom_close(g);
    }
    g->flash_countdown = ds[XEEN_DS_MONSTER_COUNTDOWN];
    ds[XEEN_DS_MONSTER_COUNTDOWN] = 0;
    xeen_status sound = sound_effect(g, MEMBER_FLASH_SOUND);
    if (sound != XEEN_OK) {
        return sound;
    }
    g->flash_stage = 1;
    g->flash_frame = 0;
    return XEEN_OK;
}
/* Readies the next frame (flash_frame) of MEMBER's flash for showing. Returns 1 when it is to be shown now, 0 when
 * the service suspends for its scene pass, -1 when its sprite cannot be drawn:
 *   - its scene asked for or running: 0, nothing done;
 *   - its scene completed: the handshake made idle, 1 (the sprite was drawn before the scene was asked for);
 *   - else the flash sprite's frame drawn at the portrait's x, y 150; then with the view's window closed the scene
 *     is asked for (0), with it open the frame is shown at once (1).
 * (An earlier native drew no sprite here, so the portrait never flashed: Vern the Guildman's membership, the
 * fountains.) Original: 2BA4F 4EA0D; 4AC92(8C7E, DI, 17B2[member], 96h) at 2BA54..2BA69; 1B503(0) at 2BA7C unless
 * 9877 (window 11) is set. */
static int member_flash_frame_ready(xeen_game *g, unsigned member, const uint8_t *ds)
{
    if (g->event_scene == EVENT_SCENE_REQUESTED || g->event_scene == EVENT_SCENE_RUNNING) {
        return 0;
    }
    if (g->event_scene == EVENT_SCENE_COMPLETED) {
        g->event_scene = EVENT_SCENE_IDLE;
        return 1;
    }
    xeen_sprite_draw d = {xeen_read16(&g->movement, XEEN_DS_SPELLFX_SPRITES),
                          (uint16_t)g->flash_frame,
                          (int16_t)ds[(uint16_t)(XEEN_DS_PORTRAIT_X + member)],
                          MEMBER_FLASH_SPRITE_Y,
                          0,
                          0,
                          0};
    if (g->presentation && xeen_presentation_draw_sprites(g->presentation, &d, 1) != XEEN_OK) {
        return -1;
    }
    if (!ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)]) {
        g->event_scene = EVENT_SCENE_REQUESTED; /* idle here: the handshake's request (scene_request) */
        return 0;
    }
    return 1;
}
/* Shows a readied frame: the screen presented between the display hook's scene marks. The frame is held for one tick
 * (two with the view's window open): a window shows it through the display hook; it is no gameplay wait, and
 * headless shows nothing. Original: 2BA82 50BF6(0); 2BA8C..2BAA9, 4E33F until 4EA1C. */
static xeen_status member_flash_show_frame(xeen_game *g)
{
    if (g->presentation) {
        xeen_presentation_display(g->presentation, XEEN_DISPLAY_SCENE_START);
    }
    xeen_status shown = xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
    if (shown != XEEN_OK) {
        return shown;
    }
    if (g->presentation) {
        xeen_presentation_display(g->presentation, XEEN_DISPLAY_SCENE_END);
    }
    return XEEN_OK;
}
/* The flash's end: the party bar redrawn (faces, HP gems, highlight), so the last frame does not stay over the
 * portrait (it once did, until a later redraw: sweep row p149-who-other-key-pick); the countdown restored; MEMBER's
 * flash counter raised (the operation has tested it); the flash no longer running.
 * Original: 2BAB1 2ABB8(1); 2BAB9 96C0 = [BP-2]; 8C78[member] += 1. */
static void member_flash_end(xeen_game *g, unsigned member, uint8_t *ds)
{
    bar_redraw(g);
    ds[XEEN_DS_MONSTER_COUNTDOWN] = g->flash_countdown;
    ++ds[XEEN_DS_MEMBER_FLASHED + member];
    g->flash_stage = 0;
}
/**
 * Serves the flash over a member's portrait (the request's argument, a present member 0..5; another is a failure,
 * -1, with nothing done): its start on the first request, its frames (answering 0 while a frame's scene pass runs),
 * its end (1). Any failure answers -1 where it occurs.
 * Original: 2B9C7(member): 18765(12); 96C0 saved and 0; 1880E(14h); four frames of 4EA0D, the 8C7E sprite at
 * 17B2[member], 1B503(0), 50BF6(0), 4EA1C; 2ABB8(1); 8C78[member] += 1.
 */
static int serve_member_flash(xeen_game *g, const xeen_event_request *r, uint8_t *ds)
{
    unsigned member = r->argument;
    if (member >= ds[XEEN_DS_PARTY_COUNT] || member > 5u) {
        return -1;
    }
    if (!g->flash_stage && member_flash_start(g, ds) != XEEN_OK) {
        return -1;
    }
    while (g->flash_frame < MEMBER_FLASH_FRAMES) {
        int ready = member_flash_frame_ready(g, member, ds);
        if (ready <= 0) {
            return ready;
        }
        if (member_flash_show_frame(g) != XEEN_OK) {
            return -1;
        }
        ++g->flash_frame;
    }
    member_flash_end(g, member, ds);
    return 1;
}

/* Closes an event window (the request's argument) and restores the pixels under it: during a combat cell event the
 * fight's own window from its saved pixels; the bottom panel from the event's bottom text or from the pixels a
 * panel window saved; the speech face; the NPC dialogue window; the map load's "Please Wait..." box (presentation
 * only); any other window through the event text window. Returns 1 when served and presented, else 0.
 * Original: 4F57B (with 186C0, 25B9E..25BE7). */
static int serve_window_restore(xeen_game *g, const xeen_event_request *r)
{
    xeen_presentation_trace_extra("evrestore %u", r->argument);
    /* During the combat cell event (combat_cell_event) the windows are the fight's own (opened through
     * combat_window_service, their pixels saved); the flag is already clear. Original: 186C0. */
    if (g->combat_cell) {
        panel_under(g, r->argument, 1);
        return xeen_presentation_update_window(g->presentation, r->argument) == XEEN_OK;
    }
    /* The bottom panel: the event's bottom text restores its own pixels; a bottom panel opened through panel_window
     * (the tavern's food text under its yes/no) has its pixels in panel_under, restored as for any window. (An
     * earlier native restored only the event's text: the food text stayed over the party bar after the answer;
     * owner report.) Original: 25B9E..25BE7 under 2F2FF; 4F2A1, 4F57B. */
    if (r->argument == XEEN_WINDOW_BOTTOM) {
        if (g->bottom_active) {
            bottom_restore(g);
        } else if (g->window_saves[XEEN_WINDOW_BOTTOM].shown) {
            panel_under(g, XEEN_WINDOW_BOTTOM, 1);
        }
        return xeen_presentation_update_window(g->presentation, XEEN_WINDOW_BOTTOM) == XEEN_OK;
    }
    if (r->argument == XEEN_WINDOW_SPEECH) { /* the speech face */
        if (g->face_active) {
            face_restore(g);
        }
        return 1;
    }
    if (r->argument == XEEN_WINDOW_VIEW && g->dialogue_window.active) {
        if (xeen_dialogue_close(&g->dialogue_window, g->presentation) != XEEN_OK) {
            return 0;
        }
        return xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW) == XEEN_OK;
    }
    /* The "Please Wait..." box of a map load closes at the world entry's dispatcher cleanup: presentation only.
     * Original: 19E82's window 9 (28850). */
    if (r->argument == XEEN_WINDOW_WAIT && !g->text_window.active) {
        return xeen_presentation_update_window(g->presentation, XEEN_WINDOW_WAIT) == XEEN_OK;
    }
    if (xeen_text_window_close(&g->text_window, g->presentation, r->argument) != XEEN_OK) {
        return 0;
    }
    return xeen_presentation_update_window(g->presentation, r->argument) == XEEN_OK;
}

/*
 * An event's yes/no (argument 0) or any-key wait (argument 1) is a modal the interpreter requests until it is
 * answered: the first request opens it (yesno_open), every request asks the event UI for the key (the UI takes Y, N
 * and Esc for the yes/no, any key for the wait), and the answer closes it (yesno_finish).
 * Original: 2F2FF ([BP+8] 1 only from 25BF6).
 */
/* Opens the modal: the DS state it changes saved (xeen_modal_begin), the session's modal flags set and, for the
 * yes/no, its form drawn (confirm.icn, the buttons, the .hed face states). Returns 1 when open; -1 when the modal
 * cannot begin; 0 when the form cannot be drawn (a variant it does not cover stays a stop at the request): the
 * modal is then ended again, its finish's own result not needed (it was just begun). As before, modal_fresh is left
 * set on that refusal (nothing reads it while no modal is open: the next opening sets it again).
 * Original: 2F2FF's entry, 190ED (xeen_modal_begin), then 2F2FF(0)'s form (yesno_begin). */
static int yesno_open(xeen_game *g, const xeen_event_request *r)
{
    xeen_presentation_trace_extra("npcmark yesno"); /* Evidence: 2F2FF's entry */
    if (xeen_modal_begin(&g->modal, &g->movement, XEEN_DS_SEGMENT) != XEEN_OK) {
        return -1;
    }
    g->event_modal_open = 1;
    g->modal_fresh = 1;
    g->modal_scene_ready = 0;
    g->modal_face = 0;
    if (!r->argument && !yesno_begin(g)) {
        (void)xeen_modal_finish(&g->modal, &g->movement, XEEN_DS_SEGMENT);
        g->event_modal_open = 0;
        return 0;
    }
    return 1;
}
/* Closes the answered modal: the saved DS state back, the session's flags cleared, the yes/no's form taken down
 * (yesno_end) and the CD voice stopped; *AX becomes the script's answer: the yes/no's key (Y 1; N, Esc 0) as the
 * event UI left it, the any-key wait's 1 whatever the key. Returns 1, or -1 when the modal cannot be ended (*AX then
 * the UI's). Original: the answer 2F558..2F575 (SI, the argument, nonzero: 1; else Esc and N 0, Y 1, any other key
 * back to the wait 2F517); 2F577..2F58D and 2F59E the saved state (xeen_modal_finish); 2F594 190CF and 28D80 for the
 * yes/no only (yesno_end); 2F5A6 483B4, the CD stop (on the silent backend only recorded). */
static int yesno_finish(xeen_game *g, const xeen_event_request *r, uint16_t *ax)
{
    uint16_t answer = *ax;
    if (xeen_modal_finish(&g->modal, &g->movement, XEEN_DS_SEGMENT) != XEEN_OK) {
        return -1;
    }
    g->event_modal_open = 0;
    g->modal_fresh = 0;
    if (g->yesno) {
        yesno_end(g);
    }
    xeen_native_audio_cd_stop(&g->audio);
    *ax = r->argument ? 1u : answer;
    return 1;
}
/* Serves the yes/no or any-key wait. The second argument is 2F2FF's [BP+8], passed on to 26978 in the wait: 1 only
 * from the tavern's food question (25BF6: 2F2FF(0, 1)), whose wait animates the location; any other shape, or a
 * session without native audio, is not served (0). Returns 1 when answered, else the opening's refusal or the event
 * UI's answer (0 while the key is awaited; the modal stays open for the next request). */
static int serve_yesno_wait(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n,
                            uint16_t *ax)
{
    int handled;
    if (r->argument > 1u || r->second_argument > (g->tavern_food_step == TAVERN_FOOD_ASKING ? 1u : 0u) ||
        !g->native_audio_enabled) {
        return 0;
    }
    if (!g->modal.active) {
        int opened = yesno_open(g, r);
        if (opened != 1) {
            return opened;
        }
    }
    handled = xeen_event_ui_service(&g->ui, r, ds, n, ax);
    if (handled != 1) {
        return handled;
    }
    return yesno_finish(g, r, ax);
}


/* Operation 48's numeric choice (the request's argument N, 1..9): on its first pass the choice opens as a modal;
 * then the event UI's modal service runs it (keys '1'.. N - 1 answer their number, N answers 0) and closes it when
 * it is answered. Another argument shape is not served (0). Returns the modal service's result.
 * Original: 2F5B1 (2F7C1..2F7D8). */
static int serve_numeric_choice(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n,
                                uint16_t *ax)
{
    int handled;

    /* 2F5B1(n), the numeric choice (operation 48): keys '1'..
     * n-1 return their number, n returns 0 (2F7C1..2F7D8). */
    if (!r->argument || r->argument > 9u || r->second_argument) {
        return 0;
    }
    if (!g->modal.active) {
        xeen_presentation_trace_extra("npcmark choice"); /* Evidence: 2F5B1's entry */
        if (!choice_begin(g, r->argument)) {
            return 0;
        }
        g->event_modal_open = 1;
        g->modal_fresh = 1;
        g->modal_scene_ready = 0;
        g->modal_face = 0;
    }
    handled = xeen_event_ui_service(&g->ui, r, ds, n, ax);
    if (handled == 1) {
        g->event_modal_open = 0;
        g->modal_fresh = 0;
        choice_end(g);
    }
    return handled;
}

/**
 * A random number inside an event: the host's draw reduced to the range, with the call's AX as the original passes
 * it, for the argument shapes the scripts use (the money tree's random gold, operation 23's 1..p6, operation 16's
 * 0..7 with the caller's AX in DESTINATION and COUNT 1). Returns 1 with the roll in *AX, 0 for an argument shape it
 * does not serve (or no draw), -1 when the range is invalid. Original: 504DA(min, max) (2DECE mode 100, op 23, op 16).
 */
static int serve_random(xeen_game *g, const xeen_event_request *r, uint16_t *ax)
{
    uint16_t raw;
    /* The scripts' random numbers: the money tree's random gold (give mode 100) with any bound, JumpRandom's
     * 1..p6 and Spawn's 0..7. Original: 504DA(1, n) from 2DECE mode 100, op 23's 504DA(1, p6), op 16's
     * 504DA(0, 7). */
    if (!g->event_random || r->argument > 1u || !r->second_argument ||
        (r->destination && r->count != 1u) || r->count > 1u) {
        return 0;
    }
    /* The generator's call receives the caller's AX at the call: the requested bound itself (the gold's), or with
     * count 1 the destination argument (Spawn's p6 x 20). Original: 2E0C6..2E0C9 keeps the bound in AX; op 16's
     * 323C9 has CBW(p6) * 14h; 50525 takes AL in. */
    raw = g->event_random(g->event_random_user, r->count ? r->destination : r->second_argument);
    if (!xeen_entity_rng_range(raw, r->argument, r->second_argument, ax)) {
        return -1;
    }
    return 1;
}

#define CHEST_SPRITES 16u /* the .mob header's object sprites (50D4:677C), searched from the first */
/* The open chest's sprite for an object of SPRITE in the world WORLD (DS 94: 0 Clouds, else Darkside), or 0 when the
 * object is no chest that opens: the world's own chest (Clouds 10h, Darkside 0Fh) opens as 3Eh / 39h, and the chest
 * 49h of either world as 77h. Original: 336E6..3370C and 33723..33732 (the world's chest), 33770..33785 (49h). */
static uint8_t open_chest_sprite(uint8_t world, uint8_t sprite)
{
    if (sprite == (world ? 0x0fu : 0x10u)) {
        return world ? 0x39u : 0x3eu;
    }
    return sprite == 0x49u ? 0x77u : 0u;
}
/**
 * A picked lock's chest opens: the cell's chest object takes the open chest's sprite and its slot among the map's
 * sixteen object sprites (Darkside 0Fh becomes 39h, Clouds 10h becomes 3Eh, 49h becomes 77h; unchanged when that
 * sprite is not among the map's), and the chest count advances. Returns 1, or -1 without the .mob header.
 * Original: 336DC after a picked lock (98D9; the .mob header at 50D4:677C; +2 the slot, +4 the sprite).
 */
static int serve_opened_chest(xeen_game *g, uint8_t *ds)
{
    /* 336DC after a picked lock (9755 > 0, an object at the cell): the object takes the open chest's sprite and its
     * slot k among the map's object sprites (the object's id byte, +2, = k; its sprite, +4, the new one) when that
     * sprite is there (33713..3376C, 3378C..337C5); else it stays as it is. */
    uint8_t mob[48];
    unsigned object = ds[XEEN_DS_EVENT_OBJECT];
    uint8_t *record = ds + (uint16_t)(XEEN_DS_NUMBERED_OBJECTS + XEEN_MAP_OBJECT_BYTES * object);
    uint8_t open = open_chest_sprite(ds[XEEN_DS_WORLD], record[XEEN_MAP_OBJECT_SPRITE]);
    if (!map_mob_header(g, mob)) {
        return -1;
    }
    for (unsigned k = 0; open && k < CHEST_SPRITES; ++k) {
        if (mob[k] == open) {
            record[XEEN_MAP_OBJECT_SLOT] = (uint8_t)k;
            record[XEEN_MAP_OBJECT_SPRITE] = open;
            break;
        }
    }
    ++g->chests;
    return 1;
}

/**
 * A script's If condition computed from a member's record by the game's rules: the armour class (10), the maximum
 * HP (78) and SP (81), a statistic with its bonuses (86..92), a party skill (99), thievery (102); the result in
 * *AX. Returns 1, or -1 for another condition or a record outside DS. Original: 2BF6E.
 */
static int serve_condition_value(xeen_game *g, const xeen_event_request *r, const uint8_t *ds,
                                 size_t n, uint16_t *ax)
{
    /* The If condition's record-derived values (2BF6E):
     * 10 4660F(record, 0), 78 2AAC2, 81 46470, 86..92 2BD94(record,
     * condition - 86, 0), 99 2BEF9(value), 102 3CF5B; the member is a
     * word (FFFFh for BP-C 0), the record wraps in DS like the far
     * pointer's offset. */
    const uint8_t *ch = ds + (uint16_t)(XEEN_PARTY_RECORDS +
                                        (uint16_t)(r->second_argument * XEEN_CHARACTER_BYTES));
    unsigned cond = r->argument;
    if (n < 65536u ||
        (uint16_t)(XEEN_PARTY_RECORDS + (uint16_t)(r->second_argument * XEEN_CHARACTER_BYTES)) >
                65536u - XEEN_CHARACTER_BYTES) {
        return -1;
    }
    if (cond == 10u) {
        *ax = xeen_combat_armor_class(&g->movement, ch, 0);
    } else if (cond == 78u) {
        *ax = xeen_combat_max_hp(&g->movement, ch);
    } else if (cond == 81u) {
        *ax = xeen_combat_max_sp(&g->movement, ch);
    } else if (cond >= 86u && cond <= 92u) {
        *ax = xeen_combat_stat(&g->movement, ch, cond - 86u, 0);
    } else if (cond == 99u) {
        *ax = (uint16_t)xeen_party_skill(&g->movement, r->destination);
    } else if (cond == 102u) {
        *ax = (uint16_t)xeen_combat_thievery(&g->movement, ch);
    } else {
        return -1;
    }
    return 1;
}

/**
 * Serves 305E8(level, BC6A, 0, 0), op 12's give 66: an item generated into the BC6A slot, AX its
 * category (its 504DA draws with the caller's AL). Returns 1, or 0 for another form or without the
 * event's random generator.
 */
static int serve_generated_item(xeen_game *g, const xeen_event_request *r, uint16_t *ax)
{
    uint8_t item[3];
    if ((r->second_argument && r->second_argument != 12u) || r->destination != XEEN_DS_SCRATCH_RECORD ||
        !g->event_random) {
        return 0;
    }
    *ax = (uint16_t)xeen_item_generate_reason(&g->movement, (uint8_t)r->argument,
                                              r->second_argument, newgame_draw, g, item);
    return 1;
}

/**
 * Serves op 12's gives 78 and 81: 2AAC2(record) the maximum HP, 46470(record) the maximum SP in
 * *AX (argument: the record's DS offset). Returns 1, or -1 for an offset that is not a record.
 */
static int serve_maximum_points(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n,
                                uint16_t *ax)
{
    if (n < 65536u || r->argument < XEEN_PARTY_RECORDS ||
        r->argument > XEEN_PARTY_RECORDS + 5u * XEEN_CHARACTER_BYTES ||
        (r->argument - XEEN_PARTY_RECORDS) % XEEN_CHARACTER_BYTES) {
        return -1;
    }
    *ax = r->address == XEEN_REQUEST_MAXIMUM_HP ? xeen_combat_max_hp(&g->movement, ds + r->argument)
                                 : xeen_combat_max_sp(&g->movement, ds + r->argument);
    return 1;
}

/*
 * The drive()-mediated waits: the service answers 0 (the interpreter suspends at the request),
 * drive() opens the message and runs its key, sets the state to 2 (closed), and the resumed
 * interpreter repeats the request, which now answers 1 and returns to its caller.
 */
/**
 * An event's message scroll (the text buffer, mode 1): the game loop opens it (event_scroll_open) and its key
 * resumes the interpreter; once closed the request answers 1. Another form, or a scroll still open: 0.
 * Original: 18F2F(A827, 1) inside an event (2E053, 2E06C).
 */
static int serve_event_scroll(xeen_game *g, const xeen_event_request *r)
{
    if (r->argument != 1u || r->destination != XEEN_DS_TEXT_BUFFER) {
        return 0;
    }
    if (g->event_scroll == EVENT_MESSAGE_CLOSED) {
        g->event_scroll = EVENT_MESSAGE_NONE;
        return 1;
    }
    return 0;
}

/**
 * Serves 291FB(kind, 0, 0, 3), operation 12's refused take: drive() opens the message
 * (event_payment_open) for the kind recorded here; its key resumes the interpreter, and the call
 * returns 1 (2C992). Another form: 0.
 */
static int serve_payment_refusal(xeen_game *g, const xeen_event_request *r)
{
    if (r->argument > 1u || r->second_argument || r->destination != 3u) {
        return 0;
    }
    if (g->event_payment == EVENT_MESSAGE_CLOSED) {
        g->event_payment = EVENT_MESSAGE_NONE;
        return 1;
    }
    g->payment_kind = r->argument;
    return 0;
}

/**
 * Serves 1B503(1), an event's full exploration scene, through the scene handshake: 1 once the
 * scene has completed, else asks for it and answers 0. Another form: 0.
 */
static int serve_event_scene(xeen_game *g, const xeen_event_request *r)
{
    if (r->argument != 1 || r->second_argument) {
        return 0;
    }
    if (scene_take_completion(g)) {
        return 1;
    }
    scene_request(g);
    return 0;
}

/**
 * Serves 30CF3(minutes), op 12's give 76, with the session's services (41D20's wares draws, the
 * interest, 9842). *AX 1: it stopped at 30DB3 ("The Party needs rest!": the caller's stop), else 0.
 * Returns 1, or -1 for any other stop.
 */
static int serve_clock_advance(xeen_game *g, const xeen_event_request *r, uint16_t *ax)
{
    xeen_gameplay_services services = session_services(g);
    uint32_t stop = xeen_gameplay_clock_add(&g->movement, r->argument, &services);
    if (stop && stop != XEEN_TICK_UNFED_DAWN) {
        return -1;
    }
    *ax = stop ? 1u : 0u;
    return 1;
}

/**
 * Operation 36: the party's cell in the loaded .hed takes the two bytes (the faces the cell's later yes/no, Who-will
 * prompt and speech read). Returns 1, or 0 without a cell or for a value above a byte.
 * Original: 32E6D (hed_cell; read into F467/F466 by 2F2FF, 2F9BA, 3000D).
 */
static int serve_hed_cell(xeen_game *g, const xeen_event_request *r)
{
    uint8_t *cell = hed_cell(g);
    if (!cell || r->argument > 255u || r->second_argument > 255u) {
        return 0;
    }
    cell[0] = (uint8_t)r->argument;
    cell[1] = (uint8_t)r->second_argument;
    return 1;
}

/**
 * Serves operation 16's question: the hit points monster r->argument of the database starts with, in *AX. Returns 1,
 * or -1 past the database. Original: 3242C's 1065A/1083B, the word at +14h of the 3Ch-byte records at 50D4:0000.
 */
static int serve_monster_word(xeen_game *g, const xeen_event_request *r, uint16_t *ax)
{
    size_t at = (size_t)r->argument * XEEN_MONSTER_RECORD_BYTES + XEEN_MONSTER_HP;
    if (at + 2u > g->monster_database_size) {
        return -1;
    }
    *ax = xeen_get_le16(g->monster_database + at);
    return 1;
}

/* Operation 19's 2DE1D(x, y, side, wall) and operation 35's 1C844(x, y) on the game's maze (the door code's shared
 * setter); a cache miss is an explicit stop at the call (-1). */
static int serve_set_wall(xeen_game *g, const xeen_event_request *r)
{
    return xeen_maze_set_wall(&g->movement, (int16_t)r->argument, (int16_t)r->second_argument, r->destination,
                              r->count) == XEEN_OK
                   ? 1
                   : -1;
}
static int serve_cell_metadata(xeen_game *g, const xeen_event_request *r)
{
    return xeen_metadata_at(&g->movement, (int16_t)r->argument, (int16_t)r->second_argument) == XEEN_OK ? 1 : -1;
}
/* Operation 40's 187E9(0, 0), 18975 into F42C, 187E9(F42C, 1): the location's voice (the silent backend, as the town
 * voices). */
static int serve_voice(xeen_game *g, const xeen_event_request *r, const uint8_t *ds)
{
    voice_cancel(g, 0);
    location_voc(g, (const char *)ds + r->destination);
    exit_event(g, "voice");
    return 1;
}
/* 2ABB8(1) after an operation 12 (3369E): the party bar is redrawn (the faces by condition, the HP gems, the
 * highlight) and presented (50BF6(21h)), so an event's HP or condition change shows on the portraits at once. Any
 * other argument is not served. */
static int serve_party_bar(xeen_game *g, const xeen_event_request *r)
{
    if (r->argument != 1u) {
        return 0;
    }
    return xeen_world_entry_party_bar(&g->movement, g->presentation, 1) == XEEN_OK;
}
/* 33AF0: 4AA3E(8D56), the kept NPC face and frame (the driver's handles from 8D56 up). The dispatcher clears 8D56
 * itself. */
static int serve_free_sprites(xeen_game *g, const xeen_event_request *r)
{
    xeen_sprite_free_from(&g->sprites, r->argument);
    g->dialogue_first = 0;
    return 1;
}
/*
 * The services any session serves: the take/give core's item rules (2C94C: 43385(record, row, category) and
 * 43428(record, category) for take 21, 305E8(level, BC6A, 0, 0) for give 66 (AX the category; its 504DA draws with the
 * caller's AL), 29168 for give 85), the condition values and maxima, and the maze. *LISTED is 0 for any other request.
 */
static int serve_any_session(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n, uint16_t *ax,
                             int *listed)
{
    *listed = 1;
    switch (r->address) {
        case XEEN_REQUEST_ITEM_CLEAR:
            xeen_item_clear(&g->movement, r->argument, r->destination, r->second_argument);
            return 1;
        case XEEN_REQUEST_ITEM_SORT:
            xeen_item_sort(&g->movement, r->argument, r->destination);
            return 1;
        case XEEN_REQUEST_GENERATE_ITEM:
            return serve_generated_item(g, r, ax);
        case XEEN_REQUEST_RESET_TEMPORARIES:
            xeen_gameplay_reset_temps(&g->movement);
            return 1;
        case XEEN_REQUEST_CONDITION_VALUE:
            return serve_condition_value(g, r, ds, n, ax);
        case XEEN_REQUEST_MAXIMUM_HP:
        case XEEN_REQUEST_MAXIMUM_SP:
            return serve_maximum_points(g, r, ds, n, ax);
        case XEEN_REQUEST_SET_WALL:
            return serve_set_wall(g, r);
        case XEEN_REQUEST_CELL_METADATA:
            return serve_cell_metadata(g, r);
        default:
            *listed = 0;
            return 0;
    }
}
/*
 * The services only a continuous session serves: the endings, the sounds, the dispatcher's notices and treasure, the
 * random and clock services, the dialogues, prompts and windows, and the presentation. *LISTED is 0 for any other
 * request.
 */
static int serve_continuous_session(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n, uint16_t *ax,
                                    int *listed)
{
    *listed = 1;
    switch (r->address) {
        case XEEN_REQUEST_ENDING: /* 33518, the endings' shared tail (synchronous; a failed save stops) */
            return ending_tail(g, r->argument, ds) ? -1 : 1;
        case XEEN_REQUEST_SOUND: /* the dispatcher's 1880E(15h) at 3385A (records facing another way, F482 clear) */
        case XEEN_REQUEST_EVENT_SOUND: /* operation 6 (31929): 1880E(effect) with ED9A set */
            return sound_effect(g, r->argument) == XEEN_OK;
        case XEEN_REQUEST_NOTHING_HERE:
            return serve_nothing_here(g, ds, n, ax);
        case XEEN_REQUEST_VOICE:
            return serve_voice(g, r, ds);
        case XEEN_REQUEST_RANDOM:
            return serve_random(g, r, ax);
        case XEEN_REQUEST_SCROLL:
            return serve_event_scroll(g, r);
        case XEEN_REQUEST_PAYMENT_REFUSAL:
            return serve_payment_refusal(g, r);
        case XEEN_REQUEST_OPENED_CHEST:
            return serve_opened_chest(g, ds);
        case XEEN_REQUEST_CLOCK_ADVANCE:
            return serve_clock_advance(g, r, ax);
        case XEEN_REQUEST_HED_CELL:
            return serve_hed_cell(g, r);
        case XEEN_REQUEST_PARTY_DAMAGE:
            return serve_party_damage(g, r, ds, n);
        case XEEN_REQUEST_MONSTER_WORD:
            return serve_monster_word(g, r, ax);
        case XEEN_REQUEST_WHO_WILL:
            return serve_who_will_prompt(g, r, ds, n, ax);
        case XEEN_REQUEST_TREASURE_DIALOG:
            return serve_treasure_dialog(g, r, ds, n, ax);
        case XEEN_REQUEST_DRAW_SCENE:
            return serve_event_scene(g, r);
        case XEEN_REQUEST_NPC_DIALOGUE:
            return run_npc_dialogue(g, r, ds, n, ax, ds + XEEN_DS_TEXT_BUFFER, ds + XEEN_DS_TEXT_BUFFER_0);
        case XEEN_REQUEST_PARTY_BAR:
            return serve_party_bar(g, r);
        case XEEN_REQUEST_FREE_SPRITES:
            return serve_free_sprites(g, r);
        case XEEN_REQUEST_MEMBER_FLASH:
            return serve_member_flash(g, r, ds);
        case XEEN_REQUEST_TWO_LINES_WINDOW:
            return serve_two_lines_window(g, r, ds);
        case XEEN_REQUEST_STAT_LINE_WINDOW:
            return serve_stat_line_window(g, r, ds);
        case XEEN_REQUEST_MAP_TEXT:
            return serve_map_text(g, r, ds, n, ax);
        case XEEN_REQUEST_PAGED_TEXT:
            return serve_paged_text(g, r, ds, n, ax);
        case XEEN_REQUEST_WINDOW_RESTORE:
            return serve_window_restore(g, r);
        case XEEN_REQUEST_NUMERIC_CHOICE:
            return serve_numeric_choice(g, r, ds, n, ax);
        case XEEN_REQUEST_YES_NO:
            return serve_yesno_wait(g, r, ds, n, ax);
        default:
            *listed = 0;
            return 0;
    }
}
/**
 * Event service callback: runs the original routines an event operation requests (item rules,
 * generation, conditions, presentation, sound and the others) and returns their AX in *AX; returns
 * 0 for a request the game cannot serve. The services (enum xeen_event_service) come in two tiers: those any
 * session serves, then, in a continuous session, those only it serves; every other request is the event UI's
 * (xeen_event_ui_service). Each service is served in one place.
 */
static int event_service_inner(void *u, const xeen_event_request *r, uint8_t *ds, size_t n,
                               uint16_t *ax)
{
    xeen_game *g = u;
    int listed, answer = serve_any_session(g, r, ds, n, ax, &listed);
    if (!listed && g->continuous) {
        answer = serve_continuous_session(g, r, ds, n, ax, &listed);
    }
    return listed ? answer : xeen_event_ui_service(&g->ui, r, ds, n, ax);
}
