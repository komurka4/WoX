/*
 * The event UI's presenter (Phase 246, from game.c): ui_present and ui_poll, the callbacks through which
 * the event interpreter's UI requests are shown and answered in a continuous session, and their owners:
 * the NPC speech of 3000D (npc_speech, window 40) with its yes/no form (confirm.icn, the button tables),
 * the Who-will prompt (window 36), the treasure window (10), the NPC pages (3000D's face, window 11) and
 * the event text (window 38). The interpreter's other host services are game_events.c's.
 */
#include "game_event.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The NPC's yes/no form (3000D mode 0): the button set is saved without being cleared (30016), then confirm.icn is
 * loaded and the yes/no tables are installed; the enabled table keeps the saved set's pointer. Original:
 * 3005E..300A2 (190A3(confirm.icn) [96C1, 95A8, 98D4]; the tables 98AC..989C = 1C20, 1C26, 1C28, 1C2A, 1C2C; 9896 =
 * 98D4). */
static int npc_confirm_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t h = 0;
    xeen_button_stack_save(ds);
    if (!handle_push(g, ds_text(g, XEEN_TEXT_CONFIRM_SPRITES), &h)) {
        ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(ds[XEEN_DS_HANDLE_DEPTH] - 1u);
        xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
        return 0;
    }
    yesno_buttons_install(m);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, h);
    g->npc_confirm = 1;
    return 1;
}
/* An NPC speech's yes/no sprites are released (when it loaded them): the handle stack popped (the sprites freed).
 * Original: 190CF (4AA3E, 98D4 popped). */
void npc_confirm_end(xeen_game *g)
{
    if (!g->npc_confirm) {
        return;
    }
    handle_pop(g);
    g->npc_confirm = 0;
}
/* The last page's yes/no buttons in the icon box: their row is 75, or 101 with the text off (the speech form then
 * redraws the speech window too, npc_speech). Original: 30290..303E0, [BP-2] = F384 x 1Ah + 4Bh into 1C26/1C27. */
static void npc_confirm_buttons(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t y = (uint16_t)(ds[XEEN_DS_TEXT_OFF] * 26u + 75u);
    exit_draw(g, xeen_read16(m, XEEN_DS_GLOBAL_SPRITES), 7, 232, 74, 0);
    ds[XEEN_DS_YESNO_BUTTON_Y + 1u] = ds[XEEN_DS_YESNO_BUTTON_Y] = (uint8_t)y;
    exit_draw(g, xeen_read16(m, XEEN_DS_SPRITE_HANDLE), 0, 235, (int16_t)y, 0);
    exit_draw(g, xeen_read16(m, XEEN_DS_SPRITE_HANDLE), 2, 260, (int16_t)y, 0);
}
/* With the text off and a speaking NPC's window still open (the speech's modes 0 and 2 keep it), the shared UI
 * restoration redraws that window from the name the speech kept: the name, the frame and the face, its border, then
 * the window shown; the same redraw as the face forms' (face_form). Every native owner of the restoration calls it
 * after the icons, before the icon box is shown.
 * Original: 28D80's branch 28D9E..28E2B (9894): 4E38C(FA92, 28h), frame.fac (8D58) at (F8h, 0Ch) with 8D56 set, the
 * face (8D56, frame 8D32) at (8D30, 8D2E), 4F6F8(28h), 50BF6(28h); the face forms 2F35B and 2F60D. */
void speech_face_tail(xeen_game *g)
{
    if (g->presentation && g->movement.ds[XEEN_DS_TEXT_OFF] && g->movement.ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH)]) {
        face_form(g, g->movement.ds + XEEN_DS_SPEECH_NAME);
    }
}
/* After the NPC's yes/no: the shared UI restoration (the event text and the bottom panel closed, the exploration
 * icons drawn, the speech face, the icon box shown), presentation only as yesno_end's.
 * Original: 28D80: 18765(26h), 18765(0Ch), 4ADB1(0, DS:2A5C), 50BF6(22h). */
void npc_confirm_restore(xeen_game *g)
{
    xeen_presentation_trace_extra(
            "npcsub begin 28d80"); /* evidence: the executor substitutes 28D80 */
    restore_after_dialog(g);
    xeen_presentation_trace_extra("npcsub end");
}
/** Shows the NPC's yes/no buttons (the icon box) and redraws the speaker's faces. Original: 30377 50BF6(22h). */
static void npc_confirm_shown(xeen_game *g)
{
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_ICONS);
    (void)hed_faces(g);
}
/* The speech window's rectangle (x, y, width, height): opening it saves the pixels under it and draws its frame; a
 * close restores them. Original: 1871B(28h); 18765(28h), 186C0. */
const uint16_t window40[4] = {226, 0, 93, 100};
/** Restores the pixels under the speech window and shows it. */
void face_restore(xeen_game *g)
{
    uint8_t *p = g->presentation->framebuffer;
    for (unsigned row = 0; row < 100u; ++row) {
        memcpy(p + row * XEEN_PRESENT_WIDTH + 226u, g->face_saved + row * 93u, 93);
    }
    g->face_active = 0;
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SPEECH);
}
/* The speech layout (the text off; the speech's modes 1 and 2): the text routing pushed; the face's animation state
 * reset and its place set; the face and the frame sprites loaded unless already; the view's window (an operation
 * 53's or a text-layout NPC's) closed and the speech window opened. Only the name is written (the text is spoken): a
 * tab's digits other than "0.." become "000", and a name that runs past the bottom is written again in the reduced
 * font; the name is kept (the UI restoration redraws the window from it). The mouth's frame count is twice the
 * text's spaces. The frame at (248, 12), the face at its place, the window shown. Mode 1 then waits in the speech
 * loop (speech_wait), mode 2 returns; mode 0 (the yes/no form) is below.
 * Original: 3000D with F384: 30016..300A9 190ED/1919B, 8D36 = 4, 8D34 = 7, 8D32 = 0, 8D30 = FFh, 8D2E = 12h;
 * face%02u.fac with frame.fac into 8D56/8D58; 300EC 18765(0Bh), 1871B(28h); 30145 AC0F = DS:6290; 3016B; 4E38C(AC0F,
 * 28h); AC10 = 2; FA92 = strncpy(AC0F, 63h) (28DAF); 301DE 8D2C; 50BF6(28h); mode 1 the 30442 loop, mode 2 303E3. */
/* Evidence only (XEEN_TRACE_TEXT_STATE): the state a speech-layout 4E38C(28h) text starts from;
 * KIND is the call (name: 301A6, confirm: 30306), FIRST the name text right after 1871B(28h)
 * framed window 40. */
static void speech_text_state(const xeen_game *g, const char *kind, int first)
{
    if (xeen_presentation_tracing_text_state()) {
        fprintf(stderr, "textstate 28 %s first=%d justify=%u reduced=%u colour=%u background=%u\n", kind, first,
                (unsigned)g->layout.justify, (unsigned)g->layout.reduced, (unsigned)g->layout.colour,
                (unsigned)g->layout.background);
    }
}
/*
 * A speech text after the window's first: the layout is initialized over window 40 again (font 8) with
 * the pen position, the justification, the reduced font, the background and the colour the previous
 * 4E38C(28h) left.
 */
static void speech_layout_carry(xeen_game *g)
{
    xeen_text_layout l = g->layout;
    xeen_text_layout_init(&g->layout, window40, 8);
    layout_carry(g, &l, 1);
}
/*
 * Loads an NPC's face NAME and the face frame (frame.fac), when no face is resident. Returns 1 when both are loaded;
 * 0 when one is missing: a face already loaded is freed, and a speech's (mode 0) yes/no sprites are released.
 * Original: 300C9/300DA: two 18931 loads (47B7D, the FB06 replay); 3005E's preparation undone.
 */
static int speech_load_face(xeen_game *g, const xeen_ui_request *q, const char *name, uint16_t *face,
                            uint16_t *frame)
{
    if (!sprite_load(g, name, face)) {
        if (!q->mode) {
            npc_confirm_end(g);
        }
        return 0;
    }
    if (!sprite_load(g, ds_text(g, XEEN_TEXT_FACE_FRAME_SPRITES), frame)) {
        xeen_sprite_free_from(&g->sprites, *face);
        if (!q->mode) {
            npc_confirm_end(g);
        }
        return 0;
    }
    return 1;
}
/* Closes the view's dialogue window (11: an operation 53's or a text-layout NPC's dialogue) when it is open, with its
 * pixels back. Returns 0, or -1 when its restoration fails. Original: 300EC: 18765(0Bh). */
static int speech_close_window11(xeen_game *g)
{
    xeen_presentation_trace_extra("evclose 11"); /* Evidence: 300EC */
    if (g->movement.ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)]) {
        if (!window_close_restore(g, 0xb)) {
            return -1;
        }
        if (g->dialogue_window.active &&
            xeen_dialogue_close(&g->dialogue_window, g->presentation) != XEEN_OK) {
            return -1;
        }
    }
    return 0;
}
/* The talking face's animation (8D2E..8D36, 8D56/8D58): four frames of the face kind from frame 0 at (X, Y), the
 * FACE and FRAME sprites, and the speaker's NAME into F5E8. */
static void face_animation(xeen_game *g, uint16_t x, uint16_t y, uint16_t face, uint16_t frame, const char *name)
{
    xeen_movement *m = &g->movement;
    xeen_write16(m, XEEN_DS_ANIMATION_FRAME_COUNT, 4);
    xeen_write16(m, XEEN_DS_ANIMATION_KIND, XEEN_ANIMATION_FACE);
    xeen_write16(m, XEEN_DS_ANIMATION_FRAME, 0);
    xeen_write16(m, XEEN_DS_ANIMATION_X, x);
    xeen_write16(m, XEEN_DS_ANIMATION_Y, y);
    xeen_write16(m, XEEN_DS_ANIMATION_SPRITES, face);
    xeen_write16(m, XEEN_DS_ANIMATION_SPRITE(1), frame);
    ds_put_text(g, XEEN_DS_FILE_NAME, name);
}
/*
 * Opens the speech window (40) for a speaking NPC: modes 1 and 2 first save the buttons; the face animation is set
 * up (face_animation: its sprites FACE and FRAME, the name NAME); a window not yet open has its pixels saved, its
 * frame drawn and the text state reset (justification and font cleared, colour and background kept), while an
 * already open window (a second speech) carries everything. Returns 1 when this call framed the window.
 * Original: 30103: the face animation's words (4 frames at FFh, 12h), F5E8 = NAME, 1871B(28h); with window 40 closed
 * (18723) 4F2A1 frames it and its tail 4F562/4F567 resets the text state (Phase 241).
 */
static int speech_open_window40(xeen_game *g, const xeen_ui_request *q, const char *name, uint16_t face,
                                uint16_t frame, int loaded)
{
    uint8_t *ds = g->movement.ds, *p;
    int framed = 0;
    xeen_presentation_trace_extra("evopen 40");
    if (q->mode) {
        xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    }
    face_animation(g, 0xff, 0x12, face, frame, name);
    if (loaded) {
        g->dialogue_first = face;
    }
    p = g->presentation->framebuffer;
    if (!ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH)]) {
        for (unsigned row = 0; row < 100u; ++row) {
            memcpy(g->face_saved + row * 93u, p + row * XEEN_PRESENT_WIDTH + 226u, 93);
        }
        (void)xeen_text_frame(p, XEEN_PRESENT_PIXELS, window40);
        g->face_active = 1;
        text_frame_reset(g);
        framed = 1;
    }
    xeen_window_open(&g->movement, XEEN_WINDOW_SPEECH);
    return framed;
}
/*
 * Draws the speaker's name, centred in the speech window (at the name text's own position code). A tab code in the
 * name is followed by three digits, which are forced to "000" unless they already start with '0'. A name that runs
 * past the window's bottom is drawn again in the reduced font. FRAMED: this call framed the window (for the text
 * state's trace). Returns 0, or -1 when the formatted name does not fit the text buffer.
 * Original: 30145..301A6: AC0F = the name in DS:6290's form, 3016B the digits, 4E38C(AC0F, 28h); the reduced font
 * AC10 = 2.
 */
static int speech_name(xeen_game *g, const xeen_ui_request *q, int framed)
{
    uint8_t *ds = g->movement.ds, *p = g->presentation->framebuffer;
    size_t rest = XEEN_TEXT_LAYOUT_END;
    for (uint8_t font = 1; font <= 2; ++font) {
        uint8_t *t = ds + XEEN_DS_TEXT_BUFFER_2;
        size_t i = 0;
        int length = snprintf((char *)t, XEEN_TEXT_BUFFER_BYTES, ds_text(g, XEEN_TEXT_SPEAKER_NAME_LAYOUT), (const char *)q->primary);
        if (length < 0 || length >= 1000) {
            return -1;
        }
        t[1] = font;
        do {
            if (t[i] == 9 && t[i + 1] != '0') {
                t[i + 1] = t[i + 2] = t[i + 3] = '0';
                i += 3;
            }
            ++i;
        } while (i < XEEN_TEXT_BUFFER_BYTES - 1u && t[i]);
        if (!g->layout_ready) {
            xeen_text_layout_init(&g->layout, window40, 8);
            g->layout_ready = 1;
        } else {
            speech_layout_carry(g);
        }
        speech_text_state(g, "name", framed && font == 1u);
        trace_event_text(0x28, t, 1000);
        rest = xeen_text_layout_run(&g->layout, &g->text_window, p, t, XEEN_TEXT_BUFFER_BYTES, 0);
        if (rest == XEEN_TEXT_LAYOUT_END) {
            break;
        }
    }
    return 0;
}
/* Keeps the drawn name for the speech window's redraws (XEEN_DS_SPEECH_NAME, as strncpy: the rest zeroed), and
 * stores twice the number of spaces of the spoken text in DS 8D2C (written only, by this and two other sites: no
 * direct reader is known). Original: 301D3 strncpy(FA92, AC0F, 63h); 301DE. */
static void speech_keep_name(xeen_game *g, const xeen_ui_request *q)
{
    uint8_t *ds = g->movement.ds;
    size_t words = 0, i = 0;
    while (i < XEEN_DS_SPEECH_NAME_BYTES && ds[XEEN_DS_TEXT_BUFFER_2 + i]) {
        ds[XEEN_DS_SPEECH_NAME + i] = ds[XEEN_DS_TEXT_BUFFER_2 + i];
        ++i;
    }
    memset(ds + XEEN_DS_SPEECH_NAME + i, 0, XEEN_DS_SPEECH_NAME_BYTES - i);
    for (size_t k = 0; k + 1u < q->secondary_size && q->secondary[k]; ++k) {
        if (q->secondary[k] == ' ') {
            ++words;
        }
    }
    xeen_write16(&g->movement, 0x8d2c, (uint16_t)(words * 2u));
}
/*
 * Draws the face's frame at (248, 12) and the face at (255, 18) and shows the speech window. Mode 0 (a question)
 * then adds the confirm buttons, draws the text again with the frame and face over it, the window's border alone,
 * and shows the window and the confirm form. Returns 0, or -1 when the presentation fails.
 * Original: 50BF6(28h); mode 0: 30290's buttons (row 65h), 302FB's 4E38C(28h, AC0F), 4F6F8(28h), 50BF6(28h),
 * 50BF6(22h), the .hed.
 */
static int speech_draw(xeen_game *g, const xeen_ui_request *q, uint16_t face, uint16_t frame)
{
    uint8_t *ds = g->movement.ds, *p = g->presentation->framebuffer;
    xeen_sprite_draw draws[2] = {{frame, 0, 248, 12, 0, 0, 0}, {face, 0, 255, 18, 0, 0, 0}};
    if (xeen_presentation_draw_sprites(g->presentation, draws, 2) != XEEN_OK ||
        xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SPEECH) != XEEN_OK) {
        return -1;
    }
    if (!q->mode) {
        npc_confirm_buttons(g);
        speech_layout_carry(g);
        speech_text_state(g, "confirm", 0);
        trace_event_text(0x28, ds + XEEN_DS_TEXT_BUFFER_2, 1000);
        (void)xeen_text_layout_run(&g->layout, &g->text_window, p, ds + XEEN_DS_TEXT_BUFFER_2, XEEN_TEXT_BUFFER_BYTES, 0);
        if (xeen_presentation_draw_sprites(g->presentation, draws, 2) != XEEN_OK) {
            return -1;
        }
        /* The border alone (a full frame would fill the interior over the text and face just drawn).
         * Original: 302FB's 4F6F8(28h). */
        (void)xeen_text_border(p, XEEN_PRESENT_PIXELS, window40);
        if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SPEECH) != XEEN_OK) {
            return -1;
        }
        npc_confirm_shown(g);
    }
    return 0;
}
/*
 * The speech of 3000D (the stages are described above): refused (0) for a mode above 2, a window 11
 * whose DS flag and dialogue disagree, a speech already waiting, a missing text font or face; -1 on a
 * presentation failure; else 1, mode 1 (and mode 0's form) then waiting in the 30442 loop.
 */
static int npc_speech(xeen_game *g, const xeen_ui_request *q)
{
    uint8_t *ds = g->movement.ds;
    uint16_t face = xeen_read16(&g->movement, XEEN_DS_ANIMATION_SPRITES),
             frame = xeen_read16(&g->movement, XEEN_DS_ANIMATION_SPRITE(1));
    char name[16];
    int loaded = 0, framed;
    if (q->mode > 2u || (ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)] != 0) != (g->dialogue_window.active != 0) ||
        g->speech_wait) {
        return 0;
    }
    if (!g->text_window.loaded && xeen_text_window_load(&g->text_window, &g->assets) != XR_OK) {
        return 0;
    }
    if (snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_FACE_SPRITES_FORMAT), q->face) < 0) {
        return -1;
    }
    /* Mode 0's 190A3(confirm.icn) and button tables come before
     * the face (3005E); then 300EC's 18765(0Bh) closes an open window 11
     * (an operation 53's or a text-layout NPC's). */
    if (!q->mode && !npc_confirm_begin(g)) {
        return 0;
    }
    if (!face) {
        if (!speech_load_face(g, q, name, &face, &frame)) {
            return 0;
        }
        loaded = 1;
    }
    if (speech_close_window11(g) < 0) {
        return -1;
    }
    framed = speech_open_window40(g, q, name, face, frame, loaded);
    if (speech_name(g, q, framed) < 0) {
        return -1;
    }
    speech_keep_name(g, q);
    if (speech_draw(g, q, face, frame) < 0) {
        return -1;
    }
    if (q->mode == 2) {
        return 1;
    }
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0); /* 30407: 189BC */
    cd_voice_start(g);
    g->speech_wait = SPEECH_PASS;
    g->speech_first = 1;
    g->modal_poll_pending = MODAL_POLL_START;
    return 1;
}
/**
 * Shows the Who-will prompt: the pixels under its window (225, 93, 94 x 80) saved, the window framed, the prompt
 * written in it and presented, then the Who-will wait. Returns 0 while a modal or another Who-will is up or the text
 * font is missing, -1 when the presentation fails. Original: 2F9BA's window 36 (4E38C of the prompt; XUI_CHARACTER).
 */
static int present_who_will(xeen_game *g, const xeen_ui_request *q)
{
    /* 2F9BA's window 36 (225, 93, 94 x 80; 4E38C of the prompt);
     * its DS was set by the service. An NPC's window 11 may stay open. */
    static const uint16_t window36[4] = {225, 93, 94, 80};
    uint8_t *p = g->presentation->framebuffer;
    xeen_text_layout l;
    if (g->modal.active || g->who_wait) {
        return 0;
    }
    if (!g->text_window.loaded && xeen_text_window_load(&g->text_window, &g->assets) != XR_OK) {
        return 0;
    }
    for (unsigned row = 0; row < 80u; ++row) {
        memcpy(g->who_saved + row * 94u, p + (93u + row) * XEEN_PRESENT_WIDTH + 225u, 94);
    }
    (void)xeen_text_frame(p, XEEN_PRESENT_PIXELS, window36);
    xeen_text_layout_init(&l, window36, 8);
    (void)xeen_text_layout_run(&l, &g->text_window, p, q->primary, q->primary_size, 0);
    if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_WHO_WILL) != XEEN_OK) {
        return -1;
    }
    g->who_wait = 1;
    g->modal_scene_ready = 0;
    g->modal_poll_pending = MODAL_POLL_START;
    g->who_repoll = 0;
    return 1;
}
/**
 * The treasure dialog's window (XUI_TREASURE): begins the modal, sets the UI mode and the portrait clicks from it and
 * opens the right column with "The Party Found:" (unless treasure_full_window already showed it). Returns 0 while a
 * dialogue, the treasure window or a modal is up or the font is missing, -1 when the presentation fails (the window
 * is closed and the modal finished again). Original: 2EA68 (9A1C, 98D6; window 10).
 */
static int present_treasure(xeen_game *g, const xeen_ui_request *q)
{
    uint16_t window = XEEN_WINDOW_RIGHT_COLUMN;
    if (g->dialogue_window.active || g->treasure.window_owner == TREASURE_WINDOW_DIALOG || g->modal.active) {
        return 0;
    }
    if (!g->text_window.loaded && xeen_text_window_load(&g->text_window, &g->assets) != XR_OK) {
        return 0;
    }
    if (xeen_modal_begin(&g->modal, &g->movement, XEEN_DS_SEGMENT) != XEEN_OK) {
        return -1;
    }
    /* The dialog sets no UI mode of its own beyond the treasure's (7, unless in combat) and leaves the portrait clicks
     * as every owner had them (the Phases 50..54 replays had masked the native modal's 0 at the command owners'
     * dialogs as presentation). Original: 2EC30; 2EBD4 is 190ED/1919B only (98D6 kept). */
    g->movement.ds[XEEN_DS_UI_MODE] = g->modal.mode;
    g->movement.ds[XEEN_DS_PORTRAIT_CLICKS] = g->modal.faces;
    /* Opening the right column saves the pixels under it, frames it and clears its inside (the cursor at its origin);
     * "The Party Found:" is written there and the window shown. (An earlier native drew the text in a box at the view's
     * place, from the carried cursor, over the minimap, and neither framed nor cleared the column.)
     * The column may still be up from the Cast dialog of a combat spell whose kill ended the fight (Cast closes it
     * only after the treasure): it is closed first and what was under it restored before the new save, as the
     * original's events show (evclose 10, evrestore 10, evopen 10; p116x-combat-implosion-clouds). This is the pixel
     * side; the DS side is the service's. (An earlier native saved the Cast window as the background, and its close
     * restored the Cast window to both display pages; the redraw then left a blinking line at y 138.)
     * Shown already before the "BackPacks Full!" scroll (TREASURE_WINDOW_EARLY, treasure_full_window): not again.
     * Original: 2EC30's 1871B(0Ah), 4F2A1 (C0C = C0D = 0), 4E38C(AC0F, 0Ah), 50BF6(0Ah); 2EBFB's first 18765(0Ah)
     * with 4F57B; 3E12F's 18765(0Ah); 2EC0D..2EC23 before 2EC44. */
    if (g->treasure.window_owner == TREASURE_WINDOW_EARLY) {
        g->treasure.window_owner = TREASURE_WINDOW_DIALOG;
    } else {
        if (g->window_saves[10].shown) {
            panel_under(g, 10, 1);
        }
        panel_under(g, 10, 0);
        text_frame_reset(g);
        g->treasure.window_owner = TREASURE_WINDOW_DIALOG;
        menu_window_text(g, XEEN_WINDOW_RIGHT_COLUMN, WINDOW_FRAMED, (const char *)q->primary);
    }
    if (xeen_presentation_update_window(g->presentation, window) != XEEN_OK) {
        panel_under(g, 10, 1);
        g->treasure.window_owner = TREASURE_WINDOW_NONE;
        (void)xeen_modal_finish(&g->modal, &g->movement, XEEN_DS_SEGMENT);
        return -1;
    }
    return 1;
}
/** The first NPC page's face, its frame and the face file name (FACE was not loaded when LOADED). */
typedef struct npc_face {
    uint16_t face, frame;
    char name[16];
    int loaded;
} npc_face;
/*
 * A first page that cannot be shown: what npc_page_load_face prepared is undone, so that the request, presented
 * again, starts from the handle stack it started from: a newly loaded face is freed, and a yes/no form's (mode 0)
 * confirm.icn released (npc_confirm_end). For the native refusals only (a face or frame.fac that cannot be loaded,
 * the AC0F text past its 1,000 bytes, window 11 or the face not drawn): the original's 3000D has no refusal branch
 * there (its 18931 loads are stored untested).
 */
static void npc_page_abandon(xeen_game *g, const xeen_ui_request *q, const npc_face *f)
{
    if (f->loaded) {
        xeen_sprite_free_from(&g->sprites, f->face);
    }
    if (!q->mode) {
        npc_confirm_end(g);
    }
}
/**
 * The first NPC page's setup (npc_page 0, 3000D..300DA): the face file name, the yes/no form's
 * confirm.icn (mode 0, npc_confirm_begin), the face and frame (300BC: a face still in 8D56, as the
 * arena's mode-2 prompts keep theirs, is not loaded again; else face%02u.fac and frame.fac through
 * sprite_load, 300C9/300DA), then AC0F = the name and the text. Returns 1 when the page can be drawn,
 * 0 when it cannot (what was loaded is freed again, the confirm form ended), -1 on a format error.
 */
static int npc_page_load_face(xeen_game *g, const xeen_ui_request *q, npc_face *f)
{
    uint8_t *ds = g->movement.ds;
    if (snprintf(f->name, sizeof f->name, ds_text(g, XEEN_TEXT_FACE_SPRITES_FORMAT), q->face) < 0) {
        return -1;
    }
    if (!q->mode && !npc_confirm_begin(g)) {
        return 0;
    }
    f->face = xeen_read16(&g->movement, XEEN_DS_ANIMATION_SPRITES);
    f->frame = xeen_read16(&g->movement, XEEN_DS_ANIMATION_SPRITE(1));
    if (!f->face) {
        if (!sprite_load(g, f->name, &f->face)) {
            npc_page_abandon(g, q, f);
            return 0;
        }
        f->loaded = 1;
        if (!sprite_load(g, ds_text(g, XEEN_TEXT_FACE_FRAME_SPRITES), &f->frame)) {
            npc_page_abandon(g, q, f);
            return 0;
        }
    }
    int length = snprintf((char *)ds + XEEN_DS_TEXT_BUFFER_2, XEEN_TEXT_BUFFER_BYTES, ds_text(g, XEEN_TEXT_NPC_PAGE_LAYOUT), q->primary,
                          q->secondary);
    if (length < 0 || length >= 1000) {
        npc_page_abandon(g, q, f);
        return 0;
    }
    return 1;
}
/**
 * Opens (or keeps) window 11 for the page in AC0F: xeen_dialogue_show_layout measures the page from
 * the carried layout state, which it leaves unchanged (the page itself is drawn by 4E38C's port
 * afterwards). It matches the original on all 168 distinct window-11 calls of the original traces
 * (xeen_dat_evidence/phase154/textstate). Returns 0 when the window cannot open (a first page's
 * preparation undone: npc_page_abandon).
 */
static int npc_page_open_window(xeen_game *g, const xeen_ui_request *q, const npc_face *f)
{
    uint8_t *ds = g->movement.ds;
    size_t length = 0;
    while (length < XEEN_TEXT_BUFFER_BYTES - 1u && ds[XEEN_DS_TEXT_BUFFER_2 + length]) {
        ++length;
    }
    xeen_text_layout before = g->layout;
    if (xeen_dialogue_show_layout(&g->dialogue_window, &g->text_window, g->presentation,
                                  ds + XEEN_DS_TEXT_BUFFER_2, length + 1u, &g->layout, NULL) != XEEN_OK) {
        g->layout = before;
        if (!g->npc_page) {
            npc_page_abandon(g, q, f);
        }
        return 0;
    }
    g->layout = before;
    return 1;
}
/**
 * The spaces in an NPC page's source (the speech's word count): the whole text on the first page, else the text
 * buffer from the page's start (npc_source) up to REST, where the window writer stopped. Original: 301DE.
 */
static size_t npc_page_words(const xeen_game *g, const xeen_ui_request *q, size_t rest)
{
    const uint8_t *ds = g->movement.ds;
    size_t words = 0;
    if (!g->npc_page) {
        for (size_t i = 0; i + 1u < q->secondary_size; ++i) {
            if (q->secondary[i] == ' ') {
                ++words;
            }
        }
    } else {
        for (size_t source = g->npc_source; source < XEEN_TEXT_BUFFER_BYTES && ds[XEEN_DS_TEXT_BUFFER_2 + source] && source != rest;
             ++source) {
            if (ds[XEEN_DS_TEXT_BUFFER_2 + source] == ' ') {
                ++words;
            }
        }
    }
    return words;
}
/**
 * Draws the page's frame and face. A later page (30109 again) draws the frame (8D58) at (16, 16)
 * and the face (8D56, frame 8D32) at (8D30, 8D2E). The first page draws F's frame and face at
 * (16, 16) and (23, 22), pushes window 11 when it is not open, pushes the buttons (mode 1 and 2),
 * and stores the face animation state: 8D36 = 4 frames, 8D34 = kind 7, 8D32 = 0, the position
 * (23, 22), 8D56/8D58 the face and frame, F5E8 the file name. Returns 1, 0 when the first page's
 * draw fails (the window closes, the page's preparation undone: npc_page_abandon), -1 on a
 * presentation or window push failure.
 */
static int npc_page_draw_face(xeen_game *g, const xeen_ui_request *q, const npc_face *f)
{
    uint8_t *ds = g->movement.ds;
    if (g->npc_page) {
        xeen_sprite_draw draws[2] = {
                {xeen_read16(&g->movement, XEEN_DS_ANIMATION_SPRITE(1)), 0, 16, 16, 0, 0, 0},
                {xeen_read16(&g->movement, XEEN_DS_ANIMATION_SPRITES),
                 xeen_read16(&g->movement, XEEN_DS_ANIMATION_FRAME),
                 xeen_read16(&g->movement, XEEN_DS_ANIMATION_X),
                 xeen_read16(&g->movement, XEEN_DS_ANIMATION_Y), 0, 0, 0}};
        if (xeen_presentation_draw_sprites(g->presentation, draws, 2) != XEEN_OK) {
            return -1;
        }
        return 1;
    }
    xeen_sprite_draw draws[2] = {{f->frame, 0, 16, 16, 0, 0, 0}, {f->face, 0, 23, 22, 0, 0, 0}};
    if (xeen_presentation_draw_sprites(g->presentation, draws, 2) != XEEN_OK) {
        xeen_dialogue_close(&g->dialogue_window, g->presentation);
        npc_page_abandon(g, q, f);
        return 0;
    }
    if (!ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)] && window_push(&g->movement, ds, 11) < 0) {
        return -1;
    }
    if (q->mode) {
        xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    }
    face_animation(g, 23, 22, f->face, f->frame, f->name);
    g->dialogue_first = f->face;
    return 1;
}
/**
 * One page of an NPC's speech in the 3D view's window (11), when event texts are not switched off (a dismissal, a
 * presentation or a yes/no question with a name). The first page loads the speaker's face and its frame, opens
 * window 11 and formats the name and the text into the text buffer; a later page shows the rest the previous page
 * left. The page's words up to where the window filled are counted (into 8D2C). With text left, the page waits for a
 * key before the next. The yes/no form (mode 0) loads the question's icons before the face and its own button table,
 * and on its last page draws the Yes and No buttons; mode 2 shows the page and returns, with no wait. A window 11
 * already open (an earlier NPC, or operation 53's text) is not framed again and gets no stack entry.
 *
 * The page's steps, in order: the first page's face (npc_page_load_face), the carried layout
 * (page_layout), window 11 (npc_page_open_window), the page's text (4E38C) and its end position,
 * the words (npc_page_words), the face (npc_page_draw_face), then the wait. Returns 1, 0 when the
 * page cannot be shown, -1 on a presentation failure.
 * Original: 3000D's page with F384 clear (XUI_DISMISS/PRESENT/CONFIRM with a name). The first page: the 190ED push,
 * 8D36..8D2E, face%02u.fac and frame.fac (8D56, 8D58), 1871B(0Bh), AC0F = DS:6278 of the name and the text; a later
 * page 30109; 4E38C (window 11) returns the rest; 301DE counts the spaces up to it (8D2C = 2 x). With text left 30419:
 * 189D7 (F5FE = 0), 190ED/1919B. Mode 0: confirm.icn at 3005E (190A3 [96C1, 95A8, 98D4], before the face), the 1C20..
 * button tables and 9896 = 98D4 instead of 1919B (npc_confirm_begin); its last page F458 frame 7 at (E8h, 4Ah), the
 * 98D4 frames 0 and 2 at (EBh, 4Bh) and (104h, 4Bh), 1C26/1C27 = 4Bh, 50BF6(22h), F467/F466 from the .hed
 * (30290..303E0). An open window 11: 1871B does nothing. Mode 2 returns at 303E3, before 30407.
 */
static int present_npc_page(xeen_game *g, const xeen_ui_request *q)
{
    static const uint16_t window11[4] = {8, 8, 216, 132};
    npc_face f = {0, 0, {0}, 0};
    uint8_t *ds = g->movement.ds;
    size_t rest, words;
    int status;
    if (q->mode > 2u || ds[XEEN_DS_TEXT_OFF]) {
        return 0;
    }
    if (!g->npc_page && (ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)] != 0) != (g->dialogue_window.active != 0)) {
        return 0;
    }
    if (!g->text_window.loaded && xeen_text_window_load(&g->text_window, &g->assets) != XR_OK) {
        return 0;
    }
    if (!g->npc_page && (status = npc_page_load_face(g, q, &f)) != 1) {
        return status;
    }
    /* 30103 1871B(0Bh): only with window 11 closed (18723) does 4F2A1 frame it, and its tail
     * (4F562/4F567) clears the justification and the font; the colour and background are kept, and
     * the cursor it homes is homed again by the page's CR. A later page, or a first page with window
     * 11 already open, carries everything (owner decision D1 = C, Phase 239). */
    if (!g->npc_page && !ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)]) {
        text_frame_reset(g);
    }
    page_layout(g, window11);
    if (xeen_presentation_tracing_text_state()) { /* evidence: the state the page's 4E38C starts from */
        fprintf(stderr, "textstate 11 first=%d justify=%u reduced=%u colour=%u background=%u\n",
                !g->npc_page, (unsigned)g->layout.justify, (unsigned)g->layout.reduced,
                (unsigned)g->layout.colour, (unsigned)g->layout.background);
    }
    if (!npc_page_open_window(g, q, &f)) {
        return 0;
    }
    if (!g->npc_page) {
        xeen_presentation_trace_extra("evopen 11"); /* Evidence: 30103 */
    }
    trace_event_text(11, ds + XEEN_DS_TEXT_BUFFER_2, 1000);
    rest = xeen_text_layout_run(&g->layout, &g->text_window, NULL, ds + XEEN_DS_TEXT_BUFFER_2, XEEN_TEXT_BUFFER_BYTES, 0);
    /* 4E38C keeps ONE write position (BE4/BE6) for every
     * call: after the page it is where the page's text ended, and the
     * next 4E38C with no CR or 0B continues there. The arena's number
     * entry 28ABA(2, C8h, 0Bh) (360C4, after the mode-2 "What level of
     * monsters? (1-10)" prompt) draws its line DS:5871/587F there: on
     * the line after the prompt, in window 0Bh. Before, the NPC page
     * left the input line's carried position as the previous panel
     * text had (window_text), so the entry and its digits were drawn at
     * a stale place: the window's top over its frame, or over the party
     * bar (y 166, outside window 0Bh, so on one display page only: a
     * blinking strip). */
    {
        static const uint16_t inner11[4] = {16, 16, 200, 116};
        text_cursor_carry(g, inner11);
    }
    words = npc_page_words(g, q, rest);
    if ((status = npc_page_draw_face(g, q, &f)) != 1) {
        return status;
    }
    xeen_write16(&g->movement, 0x8d2c, (uint16_t)(words * 2u));
    if (q->mode == 2) {
        g->npc_rest = XEEN_TEXT_LAYOUT_END;
        g->dialogue_waiting = 0;
        if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW) != XEEN_OK) {
            return -1;
        }
        return 1;
    }
    if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW) != XEEN_OK) {
        return -1; /* 30275: 50BF6(0Bh) */
    }
    if (!q->mode && rest == XEEN_TEXT_LAYOUT_END) {
        npc_confirm_buttons(g);
        npc_confirm_shown(g);
    }
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0); /* 30407: 189BC */
    if (!g->npc_page) {
        cd_voice_start(g); /* 3042E, the first page */
    }
    if (rest != XEEN_TEXT_LAYOUT_END) {
        xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT); /* 30419: 189D7, 190ED, 1919B */
    }
    g->npc_rest = rest;
    g->dialogue_waiting = 1;
    return 1;
}
/**
 * Shows an event's text: the bottom window (an open question's) closed, the event text window opened unless it is
 * up, and the text written in it (with the evidence observer's lines). Returns 0 when that window belongs to another
 * owner or the font or the text window fails, -1 when a window restore or the presentation fails.
 * Original: 315F4 (XUI_TEXT): 31608 18765(0Ch), 1871B(26h) when 9892 is clear, 4E38C(26h, AC0F).
 */
static int present_event_text(xeen_game *g, const xeen_ui_request *q)
{
    if (g->movement.ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_EVENT_TEXT)] && !g->text_window.active) {
        return 0;
    }
    if (!g->text_window.loaded && xeen_text_window_load(&g->text_window, &g->assets) != XR_OK) {
        return 0;
    }
    /* 315F4 first closes window 0Ch (31608 18765(0Ch): an open
     * question's window 12), then 1871B(26h) when 9892 is clear and
     * 4E38C(26h, AC0F); evidence lines as the 'events' observer's. */
    xeen_presentation_trace_extra("evclose 12");
    if (!window_close_restore(g, XEEN_WINDOW_BOTTOM)) {
        return -1;
    }
    if (!g->movement.ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_EVENT_TEXT)]) {
        xeen_presentation_trace_extra("evopen 38");
    }
    trace_event_text(38, g->movement.ds + XEEN_DS_TEXT_BUFFER_2, q->primary_size + 3u);
    if (xeen_text_window_show(&g->text_window, g->presentation, g->movement.ds + XEEN_DS_TEXT_BUFFER_2,
                              q->primary_size + 3u) != XEEN_OK) {
        return 0;
    }
    if (!g->movement.ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_EVENT_TEXT)] &&
        window_push(&g->movement, g->movement.ds, XEEN_WINDOW_EVENT_TEXT) < 0) {
        return -1;
    }
    if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_EVENT_TEXT) != XEEN_OK) {
        return -1;
    }
    return 1;
}
/**
 * Present callback of the event UI: shows a request through the host's present function or, in a
 * continuous session, the game's own windows. One owner per request kind, tested in this order:
 * the Who-will prompt (present_who_will, window 36), the treasure window (present_treasure, 10), a
 * named NPC page (npc_speech with F384 set, window 40; else present_npc_page, window 11), the event
 * text (present_event_text, 38); a yes/no, a choice or a dismiss whose modal or page wait is already
 * drawn needs nothing more. Returns 1 when shown, 0 when it cannot be (the request stays pending),
 * -1 on a presentation failure.
 */
int ui_present(void *u, const xeen_ui_request *q)
{
    xeen_game *g = u;
    if (g->ui_present) {
        return g->ui_present(g->present_user, q);
    }
    if (!g->continuous || !g->presentation) {
        return 0;
    }
    if (q->kind == XUI_CHARACTER) {
        return present_who_will(g, q);
    }
    if (q->kind == XUI_TREASURE) {
        return present_treasure(g, q);
    }
    if ((q->kind == XUI_DISMISS || q->kind == XUI_PRESENT || q->kind == XUI_CONFIRM) &&
        q->secondary_size > 1u) {
        /* 3000D (a named request): a speech NPC (F384 set) speaks in window 40, else window 11. */
        return g->movement.ds[XEEN_DS_TEXT_OFF] ? npc_speech(g, q) : present_npc_page(g, q);
    }
    if (q->kind == XUI_TEXT) {
        return present_event_text(g, q);
    }
    if (q->kind == XUI_CONFIRM && g->yesno && g->modal.active) {
        return 1; /* 2F2FF(0), drawn by yesno_begin */
    }
    if (q->kind == XUI_CHOICE && g->choice && g->modal.active) {
        return 1; /* 2F5B1, drawn by choice_begin */
    }
    return q->kind == XUI_DISMISS && (g->modal.active || g->page_wait) ? 1 : 0;
}
/**
 * Poll callback of the event UI: answers the active request from the session's key input; returns 0
 * while no answer is available.
 */
int ui_poll(void *u, xeen_ui_response *r)
{
    xeen_game *g = u;
    if (g->continuous && g->ui.active.kind == XUI_TEXT && g->text_window.active) {
        r->input = 0;
        return 1;
    }
    if (g->continuous && g->ui.active.kind == XUI_PRESENT) {
        r->input = 0;
        return 1;
    } /* 3000D mode 2: no input */
    if (!g->response_ready) {
        return 0;
    }
    r->input = g->response;
    g->response_ready = 0;
    return 1;
}
