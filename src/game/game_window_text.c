/*
 * The window text layer (Phase 248, from game_menu.c): a window's 4F6F8 frame and 4F1BE border, its text
 * through the 4E38C layout (with the carried write position of the 28876 input line), the input line's cursor
 * and the confirm text. Every session unit draws its window texts through it.
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* After a window's layout is initialised, carries the session's text state (WAS: the write state, and the cursor's
 * when CURSOR) into it (xeen_text_layout_carry). Original: 4E38C's one write state for every text call. */
void layout_carry(xeen_game *g, const xeen_text_layout *was, int cursor)
{
    xeen_text_layout_carry(&g->layout, was, cursor);
}
/* A window opened anew (its open flag was clear) resets the text state's justification and reduced font; the colour
 * is kept. Original: 4F2A1's tail (4F562/4F567) for 1871B with 986C[w] clear. */
void text_frame_reset(xeen_game *g)
{
    if (g->layout_ready) {
        g->layout.justify = 0;
        g->layout.reduced = 0;
    }
}
#define WINDOW_FLAGS 41u /* 986C's open flags: windows 0..40 (XEEN_DS_WINDOW_OPEN) */
/* Evidence: a location's (Quests', the Cast list's, the map's) 4E38C text bytes as "lwtext WINDOW HEX" (at most
 * 1,000 bytes). */
static void trace_window_text(uint16_t window, const char *text)
{
    char hex[2001];
    size_t k;
    for (k = 0; k < 1000u && text[k]; ++k) {
        (void)snprintf(hex + 2u * k, 3, "%02x", (unsigned)(uint8_t)text[k]);
    }
    hex[2u * k] = 0;
    xeen_presentation_trace_extra("lwtext %u %s", (unsigned)window, hex);
}
/**
 * Writes TEXT into WINDOW through the 4E38C text layout (4F6F8's frame first when FRAMED; presentation only). The
 * text starts inside the window's border (xeen_window_border, 4F1BE; 0 for some windows: window 0's texts, the
 * creation screen, start at its corner). A frame for a window not yet open resets the justification and the font
 * (text_frame_reset). The layout state (justification C0C, font C0D, colour BEA) carries over from the previous
 * text, as 4E38C's code-segment state does. The write position does not: the original keeps one (CS:0BE4/0BE6, set
 * only by CR, 09nnn and 0Bnnn, left by 4F6F8 at its last tile) across all its calls, but the native's other text
 * paths (layouts, dialogues, overlays) keep their own, so each call starts at the window's origin, or with CARRY at
 * the previous call's end (the 28876 input line, menu_window_text_on). A shown 28876 cursor is erased first (4E38C's
 * entry: 4E8DA), and the line entry's cursor follows the text's end (text_cursor_carry).
 */
static void window_text(xeen_game *g, uint16_t window, int framed, const char *text, int carry)
{
    uint16_t r[4], inner[4];
    xeen_presentation *p = g->presentation;
    unsigned border = xeen_window_border(window);
    xeen_presentation_trace_text(window, text); /* Evidence: each 4E38C */
    if (g->location_kind || g->quests_trace || g->list_caster_spells || g->map_active ||
        g->panel_trace) {
        trace_window_text(window, text);
    }
    if (!xeen_presentation_window(p, window, r)) {
        return;
    }
    inner[0] = (uint16_t)(r[0] + border);
    inner[1] = (uint16_t)(r[1] + border);
    inner[2] = (uint16_t)(r[2] - 2u * border);
    inner[3] = (uint16_t)(r[3] - 2u * border);
    if (g->text_cursor_shown) {
        xeen_text_cursor_erase(p->framebuffer, p->framebuffer_size, &g->text_cursor);
        g->text_cursor_shown = 0;
    }
    /* A frame drawn for a window not yet open is 1871B's 4F2A1
     * (986C[w] clear), which ends with C0C = C0D = 0 (4F562/4F567). */
    if (framed && window < WINDOW_FLAGS && !g->movement.ds[XEEN_DS_WINDOW_OPEN + window]) {
        text_frame_reset(g);
    }
    if (framed) {
        (void)xeen_text_frame(p->framebuffer, p->framebuffer_size, r);
    }
    if (!g->text_window.loaded) {
        (void)xeen_text_window_load(&g->text_window, &g->assets);
    }
    /* The text is 4E38C's own layout (xeen_text_layout_run, the listing port) from 4E38C's carried code-segment
     * state: its entry resets none of it, so the justification C0C, the font C0D and the colour BEA are those the
     * previous 4E38C call (any window) left, until the text's own 03/01/02/0C controls (4E542..4E6C8). The native's
     * single carried copy is g->layout (the paged, NPC and interface texts use it too). E.g. the guild/Cast spell
     * list (window 25h) is drawn in the reduced font that window 19h's "Spells for ..." heading (DS:7282) leaves;
     * the creation's "Exchange ... with..." and "Enter a Name" in the creation screen's last colour (4). Evidence:
     * xeen_dat_evidence/phase154/textstate. */
    {
        xeen_text_layout l = g->layout;
        int ready = g->layout_ready;
        uint16_t rect[4] = {r[0], r[1], r[2], r[3]};
        xeen_text_layout_init(&g->layout, rect, border);
        g->layout_ready = 1;
        if (ready) {
            layout_carry(g, &l, 0);
        }
        if (carry && g->text_cursor_valid) {
            g->layout.x = g->text_cursor.x;
            g->layout.y = g->text_cursor.y;
        }
        if (g->text_window.loaded) {
            (void)xeen_text_layout_run(&g->layout, &g->text_window, p->framebuffer,
                                       (const uint8_t *)text, strlen(text) + 1u, 0);
        }
        text_cursor_carry(g, inner);
    }
}
/* Places the line entry's cursor where the last text left the write position, in the current colours and the
 * window's INNER bounds (after a window text and the NPC window's page). Original: 28876's cursor (4E848) after a
 * 4E38C. */
void text_cursor_carry(xeen_game *g, const uint16_t inner[4])
{
    xeen_text_cursor_init(&g->text_cursor, inner);
    g->text_cursor.x = g->layout.x;
    g->text_cursor.y = g->layout.y;
    g->text_cursor.justify = g->layout.justify;
    g->text_cursor.reduced = g->layout.reduced;
    g->text_cursor.background = g->layout.background;
    xeen_text_cursor_set_colour(&g->text_cursor, g->layout.colour);
    g->text_cursor_valid = 1;
}
void menu_window_text(xeen_game *g, uint16_t window, int framed, const char *text)
{
    window_text(g, window, framed, text, 0);
}
/* Writes TEXT in WINDOW from where the last text left the write position (the line entry's line: an x but no y,
 * below the bank's "Amount" for example). Original: 28876's line (DS:5871 / 587F). */
void menu_window_text_on(xeen_game *g, uint16_t window, const char *text)
{
    window_text(g, window, 0, text, 1);
}
/* The line entry's cursor step: the typed line drawn again at the write position, the cursor's next frame drawn after
 * it (shown once it draws), and the entry's window presented; nothing without a line or a presentation.
 * Original: 28876's tick 288DC..288F6: 4E38C, 4E848, 50BF6. */
void input_cursor_step(xeen_game *g)
{
    xeen_presentation *p = g->presentation;
    if (!g->input_line[0] || !p) {
        return;
    }
    menu_window_text_on(g, g->input_window, g->input_line);
    if (g->text_window.loaded &&
        xeen_text_cursor_draw(&g->text_window, p->framebuffer, p->framebuffer_size, &g->text_cursor,
                              &g->text_cursor_frame) == XEEN_OK) {
        g->text_cursor_shown = 1;
    }
    (void)xeen_presentation_update_window(p, g->input_window);
}
/* Remembers LINE (empty for NULL) and its WINDOW as the entry line the line entry's cursor redraws. Original: the
 * line 28876 redraws (2894F's or 28ABA's 5871/587F line). */
void input_line(xeen_game *g, uint16_t window, const char *line)
{
    g->input_window = window;
    snprintf(g->input_line, sizeof g->input_line, "%s", line ? line : "");
}
/* Shows an item option's confirmation TEXT in the message box (window 15h), framed. Original: 2937A's text. */
void confirm_text(xeen_game *g, const char *text)
{
    menu_window_text(g, XEEN_WINDOW_MESSAGE_BOX, WINDOW_FRAMED, text);
}
