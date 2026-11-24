/*
 * The yes/no confirmation 2937A (Phase 246, from game.c): its two layouts (window 15h, the guild's 16h),
 * the window and buttons it opens, 294DD's close and the answer's return to its owner (Rest's 28E9F call,
 * the control panel's dialogs).
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Where native play stops when a confirmation's answer has no owner; the value is the original address of the
 * step (tools/xeen_replay/stops.json). */
enum {
    CONFIRM_STOP_OWNER = 0x29501             /* an answer for an owner that no confirmation serves */
};
/* The confirm dialog's two layouts: 2937A(text, 0)'s window 15h and 2937A(text, nonzero)'s (the guild,
 * 3F604) window 16h (65, 23, 185 x 140): the window, the pixels its box keeps, and the 'Y' and 'N'
 * buttons' position (the 4AC92 draws and the 1C10/1C12 x words, 1C16/1C17 y bytes). */
typedef struct {
    unsigned window;
    uint16_t x, y, w, h;
    uint16_t yes_x, no_x, button_y;
} confirm_layout;
static const confirm_layout confirm_layouts[2] = {
        {0x15u, 99, 59, 138, 82, 0x81, 0xb9, 0x70},
        {0x16u, 65, 23, 185, 140, 0x78, 0xb0, 0x85},
};
/*
 * Shows the yes/no's window L: the window opened first (before the buttons and the text), the pixels under it kept
 * in the box for the close, then the text through the window writer (every text, plain or in the control language
 * such as Buy's "%s for %lu gold?"; an earlier native drew a plain text in a native box instead) and the buttons.
 * Returns 1 when presented; 0 when nothing could be (the window's DS state is put back).
 * Original: 1871B(15h/16h); 2947A..29481's 4E38C; the box for 18765's 4F57B.
 */
static int confirm_window(xeen_game *g, const confirm_layout *l, uint16_t text, const xeen_sprite_draw draws[2])
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned w = l->window, row;
    uint16_t top = xeen_read16(m, XEEN_DS_WINDOW_DEPTH);
    uint8_t was = ds[XEEN_DS_WINDOW_OPEN + w], slot = ds[(uint16_t)(XEEN_DS_WINDOW_STACK + top + 1u)];
    xeen_text_box *b = &g->confirm_box;
    xeen_window_open(m, w);
    b->x = l->x;
    b->y = l->y;
    b->w = l->w;
    b->h = l->h;
    for (row = 0; row < b->h; ++row) {
        memcpy(b->saved + row * b->w, g->presentation->framebuffer + (b->y + row) * XEEN_PRESENT_WIDTH + b->x, b->w);
    }
    b->active = 1;
    if (g->confirm_wide) {
        menu_window_text(g, XEEN_WINDOW_CONFIRM, WINDOW_FRAMED, (const char *)ds + text);
    } else {
        confirm_text(g, (const char *)ds + text);
    }
    if ((!g->confirm_info && xeen_presentation_draw_sprites(g->presentation, draws, 2) != XEEN_OK) ||
        xeen_presentation_update_window(g->presentation, (uint16_t)w) != XEEN_OK) {
        /* nothing presented: the window's DS state is put back */
        (void)xeen_text_box_close(b, g->presentation);
        if (!was) {
            ds[XEEN_DS_WINDOW_OPEN + w] = 0;
            xeen_write16(m, XEEN_DS_WINDOW_DEPTH, top);
            ds[(uint16_t)(XEEN_DS_WINDOW_STACK + top + 1u)] = slot;
        }
        return 0;
    }
    return 1;
}
/* The yes/no's buttons: the button set pushed and pointed at the yes/no's table (Y and N, 24 x 20) with HANDLE's
 * sprites; an information box (confirm_info) gets no buttons at all. Either way the two buttons' positions are the
 * layout's. Original: 190ED; the 1C10.. table (x words 1C10/1C12, y bytes 1C16/1C17); 293EC's 1919B (9896 FFFFh). */
static void confirm_buttons(xeen_game *g, const confirm_layout *l, uint16_t handle)
{
    /* the arrays of XEEN_BUTTONS_CONFIRM */
    static const uint16_t buttons[6][2] = {
            {XEEN_DS_BUTTON_X_TABLE, 0x1c10},     {XEEN_DS_BUTTON_Y_TABLE, 0x1c16},
            {XEEN_DS_BUTTON_WIDTH_TABLE, 0x1c18}, {XEEN_DS_BUTTON_HEIGHT_TABLE, 0x1c1a},
            {XEEN_DS_BUTTON_KEY_TABLE, 0x1c1c},   {XEEN_DS_BUTTON_ENABLED_TABLE, 0x1c1e}};
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (g->confirm_info) {
        xeen_button_tables_clear(ds, XEEN_DS_SEGMENT); /* Original: 293EC 1919B (9896 FFFFh) */
    } else {
        for (unsigned i = 0; i < 6u; ++i) {
            xeen_write16(m, buttons[i][0], buttons[i][1]);
            xeen_write16(m, (uint16_t)(buttons[i][0] + 2u), XEEN_DS_SEGMENT);
        }
        xeen_write16(m, XEEN_DS_BUTTON_SPRITES, handle);
    }
    xeen_write16(m, XEEN_BUTTON_X(XEEN_BUTTONS_CONFIRM, 0), l->yes_x);
    xeen_write16(m, XEEN_BUTTON_X(XEEN_BUTTONS_CONFIRM, 1), l->no_x);
    ds[XEEN_BUTTON_Y(XEEN_BUTTONS_CONFIRM, 0)] = ds[XEEN_BUTTON_Y(XEEN_BUTTONS_CONFIRM, 1)] = (uint8_t)l->button_y;
}
/**
 * Opens the original's confirm dialog with message TEXT for OWNER: draws its window and buttons and
 * sets up their key table. Returns 0 with the DS unchanged when the native window or the sprite cannot be presented.
 * Original: 2937A(text, 0): 18931 confirm.icn into DI; 190ED pushes the button pointers, which then select the
 * 1C10.. table (x words 1C10/1C12, y bytes 1C16/1C17, 18h x 14h, keys 'Y'/'N') with 9896 = DI; 1871B(15h) opens
 * window 15h, two 4AC92 buttons (frames 0 and 2 at 81h/B9h, 70h), the 1C10/1C12/1C16/1C17 writes, 4E38C text,
 * 50BF6(15h); 98D6 is saved and cleared; 189BC waits for the mouse buttons and clears F5FE. The 189E3 loop runs no
 * scene and no timer.
 */
int open_confirm(xeen_game *g, int owner, uint16_t text)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    const confirm_layout *l = &confirm_layouts[g->confirm_wide ? 1 : 0];
    xeen_sprite_archives archives;
    xeen_sprite_draw draws[2];
    xeen_mouse_sample mouse = {0, 0, 0, 0};
    uint16_t handle = 0;
    if (!g->presentation || g->modal.active || g->dialogue_window.active || g->scroll_owner ||
        g->confirm_owner || !g->input_provider.mouse || !memchr(ds + text, 0, XEEN_TEXT_BUFFER_BYTES)) {
        return 0; /* another dialog holds the screen, no mouse, or a TEXT longer than the text buffer (the guild's long text) */
    }
    if (!g->text_window.loaded && xeen_text_window_load(&g->text_window, &g->assets) != XR_OK) {
        return 0;
    }
    archives = sprite_archives(g, NULL);
    if (xeen_sprite_load(&g->sprites, &archives, ds[XEEN_DS_WORLD], ds_text(g, XEEN_TEXT_YES_NO_SPRITES), 0, &handle) != XR_OK) {
        return 0;
    }
    draws[0] = (xeen_sprite_draw){handle, 0, l->yes_x, l->button_y, 0, 0, (uint16_t)l->window};
    draws[1] = (xeen_sprite_draw){handle, 2, l->no_x, l->button_y, 0, 0, (uint16_t)l->window};
    if (!g->presentation->framebuffer || g->presentation->framebuffer_size < XEEN_PRESENT_PIXELS ||
        g->confirm_box.active) {
        (void)xeen_sprite_free_from(&g->sprites, handle);
        return 0;
    }
    if (!confirm_window(g, l, text, draws)) {
        (void)xeen_sprite_free_from(&g->sprites, handle);
        return 0;
    }
    confirm_buttons(g, l, handle);
    g->confirm_faces = ds[XEEN_DS_PORTRAIT_CLICKS];
    ds[XEEN_DS_PORTRAIT_CLICKS] = 0;
    g->confirm_owner = owner;
    g->confirm_handle = handle;
    g->confirm_answer = 0;
    g->confirm_release = CONFIRM_RELEASE_OPEN;
    /* 189BC: 4E18F samples the mouse until no button is held. */
    if (g->input_provider.mouse(g->input_provider.user, &mouse) && !mouse.click) {
        xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
        g->confirm_release = CONFIRM_RELEASE_NONE;
    }
    ++g->ui.presentations;
    g->navigation = XSN_INVALID;
    (void)input_wait(g, CONFIRM_POLL_BOUNDARY);
    return 1;
}
/* The yes/no's end, once no mouse button is held (else it waits again): the key cleared, the button set popped, the
 * portrait clicks back, its sprites freed; then its owner continues with the answer: Rest sleeps on yes and returns on
 * no; the control panel's owners (from Save over on) go through panel_confirmed. Fails without a mouse sample; an
 * unknown owner stops.
 * Original: 294DD: 18765(15h), 189BC (F5FE cleared), 18FFF (9896 too), 98D6, 4AA3E; Rest 28EBE on yes, 29164 ->
 * 17F07 on no. */
xeen_status confirm_finish(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    int owner = g->confirm_owner;
    xeen_mouse_sample mouse = {0, 0, 0, 0};
    if (!g->input_provider.mouse || !g->input_provider.mouse(g->input_provider.user, &mouse)) {
        return XEEN_BAD_INPUT;
    }
    if (mouse.click) {
        g->confirm_release = CONFIRM_RELEASE_ANSWERED;
        return XEEN_SERVICE_REQUIRED;
    }
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    ds[XEEN_DS_PORTRAIT_CLICKS] = g->confirm_faces;
    (void)xeen_sprite_free_from(&g->sprites, g->confirm_handle);
    g->confirm_owner = 0;
    g->confirm_release = CONFIRM_RELEASE_NONE;
    ++g->ui.completions;
    g->phase = XG_STOPPED;
    if (owner == CONFIRM_REST) {
        return g->confirm_answer ? rest_sleep(g) : rest_return(g);
    }
    if (owner >= CONFIRM_SAVE_OVER) {
        return panel_confirmed(g, owner, g->confirm_answer);
    }
    return game_stop(g, CONFIRM_STOP_OWNER);
}
/** Closes the confirm dialog with ANSWER and continues its owner. */
xeen_status confirm_answer(xeen_game *g, int answer)
{
    unsigned w = g->confirm_wide ? 0x16u : 0x15u;
    xeen_window_close(&g->movement, w);
    g->confirm_wide = 0;
    g->confirm_info = 0;
    if (g->location_kind) {
        xeen_presentation_trace_extra("lwrestore %u", w); /* Evidence: 294DD's 18765 (4F57B) */
    }
    /* 4F57B puts the pixels back on the draw page (CS:1672, where
     * 4F2A1 took them) with no 50BF6: the next update shows them (its
     * pending-window copy), as in the original. */
    if (xeen_text_box_close(&g->confirm_box, g->presentation) != XEEN_OK) {
        return XEEN_BAD_INPUT;
    }
    g->confirm_answer = answer;
    return confirm_finish(g);
}
