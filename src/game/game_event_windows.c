/*
 * The windows event scripts open and the texts they show: the bottom message window (12) and the view's dialogue
 * window (11) with their saved pixels, the window stack's bookkeeping, the event's message, payment and
 * "BackPacks Full" scrolls, the text layout that carries from one text to the next, and the interpreter's text
 * services: two lines and a stat line in the bottom window, a map's paged text and a paged message.
 * Original: 1871B (the window stack), 18F2F (the scrolls), 4E38C (the layout); the services 31804, 315AE, 31633
 * (operation 50) and 316DC.
 */
#include "game_event.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Where native play stops when this unit cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json, stops_variable.json). */
enum {
    EVENT_MESSAGE_STOP_SCROLL = 0x18f2f,     /* the event's message does not fit, or its scroll cannot open */
    DISPATCH_STOP_PACKS_FULL = 0x2ec44,      /* its "BackPacks Full!" does not fit or its scroll cannot open */
    PAYMENT_STOP_FACE_KIND = 0x26978,        /* window 11 is open over an animation other than the event's face */
    PAYMENT_STOP_TEXT = 0x18f2f,             /* the payment's message does not fit */
    PAYMENT_STOP_SCROLL = 0x18f2f            /* the payment's scroll cannot open */
};
/* The payment's text buffer: the original formats it in 18F2F's own frame (18F2F `enter 98h`; 14BBA sprintf at
 * [bp-98h]), unbounded; the native refuses a text that does not fit (the payment's text stop). */
#define PAYMENT_TEXT_BYTES 0x98u
/* Ends the window's command at unsupported BOUNDARY (the session stopped). Unlike game_stop it keeps no missing
 * archive member for the diagnostic: these stops never reported one (a "Cannot read" message would replace their
 * catalogued sentence). */
static void window_stop(xeen_game *g, uint32_t boundary)
{
    g->lifecycle_boundary = boundary;
    g->command_status = XGC_UNSUPPORTED;
    g->phase = XG_STOPPED;
}
/*
 * Opens window N on the window stack as the event windows do (the caller has tested its open flag): the flag set,
 * the depth up by one and N in the stack's slot at the new depth (MOVEMENT holds the depth; DS the flag and the
 * stack). Returns -1, with nothing written, when the depth would pass 40 (the stack's 41 slots), else 0.
 * Original: 1871B's stack bookkeeping.
 */
int window_push(xeen_movement *movement, uint8_t *ds, unsigned n)
{
    uint16_t top = (uint16_t)(xeen_read16(movement, XEEN_DS_WINDOW_DEPTH) + 1u);
    if (top > 40) {
        return -1;
    }
    ds[XEEN_DS_WINDOW_FLAG(n)] = 1;
    xeen_write16(movement, XEEN_DS_WINDOW_DEPTH, top);
    ds[XEEN_DS_WINDOW_STACK + top] = (uint8_t)n;
    return 0;
}
/* Opens the bottom window (12: (0, 143), 320 x 56): the 56 rows it covers saved, then its frame drawn. Its closes
 * (bottom_close, the dispatcher's window close) restore them. Original: 1871B(12); 4F57B, 18765(12). */
static void bottom_open(xeen_game *g)
{
    static const uint16_t rect[4] = {0, 143, 320, 56};
    uint8_t *p = g->presentation->framebuffer;
    for (unsigned row = 0; row < 56u; ++row) {
        memcpy(g->bottom_saved + row * XEEN_PRESENT_WIDTH, p + (143u + row) * XEEN_PRESENT_WIDTH, XEEN_PRESENT_WIDTH);
    }
    (void)xeen_text_frame(p, XEEN_PRESENT_PIXELS, rect);
    g->bottom_active = 1;
}
/* Opens the view's dialogue window (11: (8, 8), 216 x 132) for operation 53: the covered pixels saved in the
 * dialogue window's buffer, then the frame. Original: 1871B(0Bh); closed by 4F57B(11). */
static void main_open(xeen_game *g)
{
    static const uint16_t rect[4] = {8, 8, 216, 132};
    uint8_t *p = g->presentation->framebuffer;
    for (unsigned row = 0; row < 132u; ++row) {
        memcpy(g->dialogue_window.saved + row * 216u, p + (8u + row) * XEEN_PRESENT_WIDTH + 8u, 216);
    }
    (void)xeen_text_frame(p, XEEN_PRESENT_PIXELS, rect);
    g->dialogue_window.active = 1;
}
/* Sets the text layout for WINDOW. The layout carries from one text to the next: a new window keeps the cursor,
 * justification, font and colours of the last text (only a text's \r homes it); the first text ever starts
 * fresh. Original: 4E38C's state. */
void page_layout(xeen_game *g, const uint16_t window[4])
{
    if (!g->layout_ready) {
        xeen_text_layout_init(&g->layout, window, 8);
        g->layout_ready = 1;
    } else {
        xeen_text_layout l = g->layout;
        xeen_text_layout_init(&g->layout, window, 8);
        layout_carry(g, &l, 1);
    }
}
/*
 * Writes the scene's interface text (the compass letter of a party with Direction Sense, "\v139\t116") in the
 * whole-screen window 0, through the same carried layout as every other text: its end state (row 139, centred, its
 * colour) is where the next text starts. A later text that does not home first (operation 50's "\3l\n" in the
 * dialogue window) therefore breaks its first page at once, as the original does. Drawn on the page when one is
 * available.
 * Original: 266F8's 4E38C in window 0 (4F1BE: no border, so the line is row 139); 2BEF9(6) with 8C52 clear.
 */
void interface_text_cursor(xeen_game *g, const char *text)
{
    static const uint16_t window0[4] = {0, 0, 320, 200};
    xeen_text_layout l = g->layout;
    int ready = g->layout_ready;
    if (!g->text_window.loaded && xeen_text_window_load(&g->text_window, &g->assets) != XR_OK) {
        return;
    }
    xeen_text_layout_init(&g->layout, window0, 0);
    g->layout_ready = 1;
    if (ready) {
        layout_carry(g, &l, 1);
    }
    (void)xeen_text_layout_run(&g->layout, &g->text_window,
                               g->presentation && g->presentation->framebuffer &&
                                               g->presentation->framebuffer_size >= 64000u
                                       ? g->presentation->framebuffer
                                       : NULL,
                               (const uint8_t *)text, strlen(text) + 1u, 0);
}
/* Evidence only (XEEN_DRAW_TRACE_EXTRA): a text's window and its text bytes up to the NUL (at most 1,000), in the
 * form the original-byte 'events' observer records a 4E38C call. */
void trace_event_text(unsigned window, const uint8_t *t, size_t n)
{
    char hex[2001];
    size_t k;
    if (!xeen_presentation_tracing_extra()) {
        return;
    }
    for (k = 0; k < n && k < XEEN_TEXT_BUFFER_BYTES && t[k]; ++k) {
        snprintf(hex + 2u * k, 3, "%02x", t[k]);
    }
    hex[2u * k] = 0;
    xeen_presentation_trace_extra("evtext %u %s", window, hex);
}
/** Restores the 56 screen rows from row 143 that a bottom-window message covered. */
void bottom_restore(xeen_game *g)
{
    uint8_t *p = g->presentation->framebuffer;
    for (unsigned row = 0; row < 56u; ++row) {
        memcpy(p + (143u + row) * XEEN_PRESENT_WIDTH, g->bottom_saved + row * XEEN_PRESENT_WIDTH, XEEN_PRESENT_WIDTH);
    }
    g->bottom_active = 0;
}
/* Closes the bottom window: its open flag and stack slot cleared (xeen_window_close), then, when its pixels were
 * saved, the covered rows restored and presented. Original: 18765(12). */
void bottom_close(xeen_game *g)
{
    xeen_window_close(&g->movement, XEEN_WINDOW_BOTTOM);
    if (g->bottom_active) {
        bottom_restore(g);
        (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_BOTTOM);
    }
}
/* Copies the first 48 bytes of the current map's monster/object file (mazeNNN.mob, or mazexNNN.mob past map 99) from
 * the saved-state archive into OUT; returns 0 when it is missing, shorter, or cannot be read. The original reads the
 * resident header the map load read by the same name (DS:4EF0, 1A809); the native reads that file again. */
int map_mob_header(xeen_game *g, uint8_t out[48])
{
    xeen_resource_info mi;
    char name[24];
    uint8_t *mob;
    size_t mn = 0;
    int ok;
    (void)snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_MAP_MOB_FILE), g->map_id > 99u ? 'x' : '0',
                   (unsigned)g->map_id);
    if (xeen_archive_find_name(&g->state, name, &mi) != XR_OK ||
        mi.size < 48u) {
        return 0;
    }
    if (!(mob = malloc(mi.size))) {
        return 0;
    }
    ok = xeen_archive_extract(&g->state, XR_BANK_STATE, &mi, mob, mi.size, &mn) == XR_OK &&
         mn >= 48u;
    if (ok) {
        memcpy(out, mob, 48);
    }
    free(mob);
    return ok;
}
/* The interpreter's message (the text buffer in the message format) in the message scroll (SCROLL_EVENT); a text
 * that does not fit or a scroll that cannot open is the message's stop. Original: 18F2F(A827, 1). */
void event_scroll_open(xeen_game *g)
{
    char text[1100];
    int n = snprintf(text, sizeof text, (const char *)g->movement.ds + XEEN_DS_MESSAGE_FORMAT,
                     (const char *)g->movement.ds + XEEN_DS_TEXT_BUFFER);
    if (n < 0 || (size_t)n >= sizeof text ||
        !open_scroll_message(g, SCROLL_EVENT, text)) {
        g->lifecycle_boundary = EVENT_MESSAGE_STOP_SCROLL;
        g->command_status = XGC_UNSUPPORTED;
        g->phase = XG_STOPPED;
        return;
    }
    g->event_scroll = EVENT_MESSAGE_OPEN;
}
/* Before the "BackPacks Full" scroll, the treasure's right-column window (10) is framed with "The Party Found" (the
 * third text buffer), so it shows under the scroll; the treasure dialog then keeps it rather than drawing it again
 * (TREASURE_WINDOW_EARLY). Pixels only: the window stack is xeen_window_open's.
 * Original: 2EBFB..2EC23, before 2EC37's packs test (events: evopen 10, its text, evopen 6). */
void treasure_full_window(xeen_game *g)
{
    if (!g->presentation || g->treasure.window_owner) {
        return;
    }
    if (!g->text_window.loaded && xeen_text_window_load(&g->text_window, &g->assets) != XR_OK) {
        return;
    }
    if (g->window_saves[10].shown) {
        panel_under(g, 10, 1);
    }
    panel_under(g, 10, 0);
    text_frame_reset(g);
    menu_window_text(g, XEEN_WINDOW_RIGHT_COLUMN, WINDOW_FRAMED, (const char *)g->movement.ds + XEEN_DS_TEXT_BUFFER_2);
    if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_RIGHT_COLUMN) != XEEN_OK) {
        panel_under(g, 10, 1);
        return;
    }
    g->treasure.window_owner = TREASURE_WINDOW_EARLY;
}
/* The dispatcher's "BackPacks Full!" scroll (SCROLL_DISPATCH_FULL) after a treasure it could not hand out, over the
 * "The Party Found" window: the treasure dialog's button push is made here and popped by the key. A text that does
 * not fit or a scroll that cannot open is the scroll's stop, the push and the window undone.
 * Original: 2EC44 18F2F(DS:6183, 1); 2EBFB's text in AC0F; 2EA68's 190ED, popped at 2EC49. */
void dispatch_full_open(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    char text[200];
    int found = xeen_treasure_found_text(m), n = found;
    if (found >= 0 && (unsigned)found < XEEN_TEXT_BUFFER_BYTES) {
        n = snprintf(text, sizeof text, (const char *)m->ds + XEEN_DS_MESSAGE_FORMAT,
                     (const char *)m->ds + XEEN_TEXT_BACKPACKS_FULL);
    }
    if (n >= 0 && (size_t)n < sizeof text) {
        treasure_full_window(g);
        xeen_button_stack(m->ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
        if (open_scroll_message(g, SCROLL_DISPATCH_FULL, text)) {
            return;
        }
        xeen_button_stack(m->ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
        if (g->treasure.window_owner == TREASURE_WINDOW_EARLY) {
            panel_under(g, XEEN_WINDOW_RIGHT_COLUMN, 1);
            g->treasure.window_owner = TREASURE_WINDOW_NONE;
        }
    }
    g->treasure.dispatch_full = DISPATCH_FULL_NONE;
    window_stop(g, DISPATCH_STOP_PACKS_FULL);
}
/*
 * An event's payment refusal (operation 12's refused take): the refusal's button push and sound, "Not enough <gold
 * or gems> in the <party or bank>!" in the message scroll. With the view's dialogue window open the message runs
 * under the event face's location loop (location_message, LOCATION_MESSAGE_EVENT_PAYMENT), which only an event face
 * can animate (under another animation it stops); otherwise in the plain scroll (SCROLL_PAYMENT). The
 * key pops the scroll's push, then the refusal's. A text that does not fit or a scroll that cannot open stops.
 * Original: 291FB(kind, 0, 0, 3) (29281 mode 3, 2CCA6/2CCC4): 190ED/1919B, 1880E(15h), A827 = DS:597A with the
 * DS:2F44 and DS:29A4 names, 18F2F(A827, 3): with 9877 clear the mode-1 loop 18FA1 (18FD6 reads 18F49's FFh), with
 * window 11 open 18FC8's 26E60; the key's 18FFF, then 29278.
 */
void event_payment_open(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char text[PAYMENT_TEXT_BYTES];
    unsigned kind = g->payment_kind;
    int n;
    /* 18FC8 under window 11: 26E60's 26978(0) is served for the event
     * face's kind 7 (8D34) only. */
    if (ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)] &&
        xeen_read16(m, XEEN_DS_ANIMATION_KIND) != XEEN_ANIMATION_FACE) {
        window_stop(g, PAYMENT_STOP_FACE_KIND);
        return;
    }
    value_refused(g, kind, 0u, 0);
    /* Evidence: the 'events' observer's payment window (291FB
     * mode 3): 1871B(6), 4E38C(the 4B7A text, 6). */
    n = snprintf(text, sizeof text, ds_text(g, XEEN_DS_MESSAGE_FORMAT), ds_text(g, XEEN_DS_TEXT_BUFFER));
    if (n < 0 || (size_t)n >= sizeof text) {
        window_stop(g, PAYMENT_STOP_TEXT);
        return;
    }
    xeen_presentation_trace_extra("evmark event-payment");
    xeen_presentation_trace_extra("evopen 6");
    trace_event_text(6, (const uint8_t *)text, (size_t)n);
    if (ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)]) {
        /* 18FC8: the location loop (game_town.c's location_message, owner 6)
         * with 26978's kind 7: the face 8D56[8D32/8] at (8D30, 8D2E). */
        g->payment_loc_kind = g->location_kind;
        g->location_kind = LOCATION_EVENT_FACE;
        g->event_payment = EVENT_MESSAGE_OPEN;
        ++g->payments;
        (void)location_message(g, LOCATION_MESSAGE_EVENT_PAYMENT, XEEN_DS_TEXT_BUFFER);
        return;
    }
    if (!open_scroll_message(g, SCROLL_PAYMENT, text)) {
        xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
        window_stop(g, PAYMENT_STOP_SCROLL);
        return;
    }
    g->event_payment = EVENT_MESSAGE_OPEN;
    ++g->payments;
}
/* Keeps a copy of N bytes loaded into the F42C block (at most its size); a load longer than the part known so far
 * extends it. Original: an 18975 or 4A1B7 into F42C:0. */
void f42c_load(xeen_game *g, const uint8_t *b, size_t n)
{
    if (n > sizeof g->f42c) {
        n = sizeof g->f42c;
    }
    memcpy(g->f42c, b, n);
    if (n > g->f42c_known) {
        g->f42c_known = (uint32_t)n;
    }
}
/* Operations 49's and 37's one 4E38C(AC0F, 12) in the bottom window from the layout's cursor and its 50BF6(12):
 * 1, or -1 when the update fails. */
static int window12_text(xeen_game *g, uint8_t *ds)
{
    static const uint16_t window[4] = {0, 143, 320, 56};
    page_layout(g, window);
    trace_event_text(12, ds + XEEN_DS_TEXT_BUFFER_2, 1000);
    (void)xeen_text_layout_run(&g->layout, &g->text_window, g->presentation->framebuffer,
                               ds + XEEN_DS_TEXT_BUFFER_2, XEEN_TEXT_BUFFER_BYTES, 0);
    return xeen_presentation_update_window(g->presentation, XEEN_WINDOW_BOTTOM) == XEEN_OK ? 1 : -1;
}
/** Serves 31804, operation 49's two lines in window 12; returns 1, 0 (not served) or -1. */
int serve_two_lines_window(xeen_game *g, const xeen_event_request *r, uint8_t *ds)
{
    /* Operation 49's window 12: 18765(12) (its flag, stack
     * slot and pixels), 1871B(12) (9878 is clear after it), one
     * 4E38C(AC0F, 12): text past the window is not drawn (31804 never
     * pages), 50BF6(12). */
    if (r->argument != 12u || r->destination != XEEN_DS_TEXT_BUFFER_2) {
        return 0;
    }
    if (!g->text_window.loaded && xeen_text_window_load(&g->text_window, &g->assets) != XR_OK) {
        return 0;
    }
    xeen_presentation_trace_extra("evmark event-two-lines");
    xeen_presentation_trace_extra("evclose 12");
    bottom_close(g);
    if (window_push(&g->movement, ds, 12) < 0) {
        return -1;
    }
    xeen_presentation_trace_extra("evopen 12");
    bottom_open(g);
    return window12_text(g, ds);
}

/** Serves 315AE, operation 37's line in window 12 (opened only when it is closed); returns 1, 0 or -1. */
int serve_stat_line_window(xeen_game *g, const xeen_event_request *r, uint8_t *ds)
{
    /* Operation 37's window 12: 1871B(12) only when 9878 is
     * clear (no 18765 first), one 4E38C(AC0F, 12) from the layout's
     * cursor and 50BF6(12); no page and no wait. */
    if (r->argument != 12u || r->destination != XEEN_DS_TEXT_BUFFER_2) {
        return 0;
    }
    if (!g->text_window.loaded && xeen_text_window_load(&g->text_window, &g->assets) != XR_OK) {
        return 0;
    }
    xeen_presentation_trace_extra("evmark event-stat");
    if (!ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_BOTTOM)]) {
        if (window_push(&g->movement, ds, 12) < 0) {
            return -1;
        }
        xeen_presentation_trace_extra("evopen 12");
        bottom_open(g);
    }
    return window12_text(g, ds);
}

static const uint16_t map_text_rect[4] = {8, 8, 216, 132}; /* window 11's text bounds */
/* Operation 50's swap of the map's event texts: the text block's size and far pointer (9C52, 9C5C:9C5E) are kept in
 * g->large_saved and pointed at the large text in the file buffer (F42C:0, SIZE bytes), and INDEX's offsets and NUL
 * count (2C661's table) are installed over the map's. Original: 31633's 9C52 = 8582, 9C5C:9C5E = F42C:0, 2C661. */
static void large_texts_install(xeen_game *g, uint8_t *ds, size_t size, const xeen_event_index *index)
{
    g->large_saved.size = xeen_read16(&g->movement, XEEN_DS_EVENT_TEXTS_SIZE);
    g->large_saved.offset = xeen_read16(&g->movement, XEEN_DS_EVENT_TEXTS_OFFSET);
    g->large_saved.segment = xeen_read16(&g->movement, XEEN_DS_EVENT_TEXTS_SEGMENT);
    xeen_write16(&g->movement, XEEN_DS_EVENT_TEXTS_SIZE, (uint16_t)size);
    xeen_write16(&g->movement, XEEN_DS_EVENT_TEXTS_OFFSET, 0);
    xeen_write16(&g->movement, XEEN_DS_EVENT_TEXTS_SEGMENT, xeen_read16(&g->movement, XEEN_DS_FILE_BUFFER_SEGMENT));
    for (unsigned i = 0; i <= XEEN_EVENT_MAX_TEXTS; ++i) {
        xeen_write16(&g->movement, (uint16_t)(XEEN_DS_EVENT_TEXT_OFFSETS + 2u * i), index->text_offsets[i]);
    }
    ds[XEEN_DS_EVENT_TEXT_TERMINATORS] = (uint8_t)index->text_terminators;
}
/* The map's text block back where large_texts_install found it (its index is installed again by the caller).
 * Original: 31633's end, 9C52 and 9C5C:9C5E restored before 2C661. */
static void map_texts_location_restore(xeen_game *g)
{
    xeen_write16(&g->movement, XEEN_DS_EVENT_TEXTS_SIZE, g->large_saved.size);
    xeen_write16(&g->movement, XEEN_DS_EVENT_TEXTS_OFFSET, g->large_saved.offset);
    xeen_write16(&g->movement, XEEN_DS_EVENT_TEXTS_SEGMENT, g->large_saved.segment);
}
/*
 * Operation 50's large text: the map's "aaze2NNN.txt" loaded (and as sprites into a handle), the map's event texts
 * replaced by it (large_texts_install: their old place kept for the restore), and its pages started at text LINE
 * in the view's dialog window (the event text window closed first, the dialog opened unless open). Returns 1 when
 * the pages can begin, 0 when the file or its line cannot be served.
 * Original: 31633 after its 1B503 scene: F5E8 (DS:6300, ED99); 18931 (8582); 9C52 = 8582, 9C5C:9C5E = F42C:0
 * (4A1B7's copy); 2C661's table (9A1E.., 97F4); 18765(26h), 1871B(0Bh).
 */
static int map_text_open(xeen_game *g, uint8_t *ds, unsigned line)
{
    char name[16];
    size_t size = 0;
    uint16_t h = 0;
    xeen_event_index index;
    xeen_sprite_archives a = sprite_archives(g, NULL);
    (void)snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_EVENT_MAP_TEXTS_FILE), (unsigned)ds[XEEN_DS_PARTY_MAP]);
    memcpy(ds + XEEN_DS_FILE_NAME, name, strlen(name) + 1u);
    free(g->large);
    g->large = menu_load(g, name, &size);
    if (!g->large || size > 65535u || xeen_events_decode(NULL, 0, g->large, size, &index) != XE_OK) {
        return 0;
    }
    if (xeen_sprite_load(&g->sprites, &a, ds[XEEN_DS_WORLD], name, 0, &h) != XR_OK) {
        return 0;
    }
    g->large_size = size;
    g->large_handle = h;
    f42c_load(g, g->large, size);
    large_texts_install(g, ds, size, &index);
    g->large_start = index.text_offsets[line];
    if (g->large_start > size) {
        return 0;
    }
    xeen_presentation_trace_extra("evmark event-display");
    xeen_presentation_trace_extra("evclose 38");
    if (!g->dialogue_window.active) {
        xeen_presentation_trace_extra("evopen 11");
    }
    page_layout(g, map_text_rect);
    if (!g->dialogue_window.active) {
        main_open(g);
    }
    g->page_next = 0;
    g->page_stage = PAGE_WRITE;
    g->large_stage = 1;
    return 1;
}
/* The last page written: 9C52 and 9C5C:9C5E back, 2C661 over the map's texts again, 4AA3E(handle).
 * Returns 1, or -1 when the map's texts cannot be installed again. */
static int map_text_close(xeen_game *g, uint8_t *ds, size_t n)
{
    g->page_stage = PAGE_IDLE;
    g->large_stage = 0;
    map_texts_location_restore(g);
    if (xeen_event_text_install(&g->resources.texts, ds, n) != XE_OK) {
        return -1;
    }
    (void)xeen_sprite_free_from(&g->sprites, g->large_handle);
    free(g->large);
    g->large = NULL;
    return 1;
}
/* The paged text's key wait between two pages: the key cleared, the next page's start kept, and the wait handed to
 * the modal loop from its pass's start (PAGE_KEY_WAIT). Original: 31786's wait (189BC, F5FE = 0). */
static void page_key_wait_open(xeen_game *g, size_t next)
{
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
    g->page_next = next;
    g->page_stage = PAGE_KEY_WAIT;
    g->page_wait = 1;
    g->modal_scene_ready = 0;
    g->modal_poll_pending = MODAL_POLL_START;
}
/* The key wait WAIT (31793) served through the UI; returns the service's answer, and when the wait is over (1)
 * the modal loop's state is released for the next page. */
static int page_key_wait(xeen_game *g, const xeen_event_request *wait, uint8_t *ds, size_t n, uint16_t *ax)
{
    int handled = xeen_event_ui_service(&g->ui, wait, ds, n, ax);
    if (handled != 1) {
        return handled;
    }
    g->page_wait = 0;
    g->modal_scene_ready = 0;
    g->modal_poll_pending = MODAL_POLL_START;
    return 1;
}
/* After a page's key: the window's text bounds RECT taken again (each 4E38C(window) takes them; the key wait's 1B503
 * scenes write the compass text in window 0, interface_text_cursor, so a third page needs them again), the HOME text
 * (SIZE bytes) traced and written in WINDOW, and the next page's write. */
static void page_continue(xeen_game *g, unsigned window, const uint16_t rect[4], const uint8_t *home, size_t size)
{
    page_layout(g, rect);
    trace_event_text(window, home, size);
    (void)xeen_text_layout_run(&g->layout, &g->text_window, g->presentation->framebuffer, home, size, 0);
    g->page_stage = PAGE_WRITE;
}
/** Serves 31633, operation 50's text from the map's aaze2 file, paged in window 11; returns 1, 0, -1 or the page wait's result (a suspension). */
int serve_map_text(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n,
                          uint16_t *ax)
{
    int handled;
    /* Per page left (316DC's pages): 189D7 and the 31786 key loop, then "\r" (630E). */
    xeen_event_request wait = {XEEN_REQUEST_PAGED_TEXT_KEY, 0, 0, 0, 0};
    static const uint8_t home[] = {13, 0};
    if (r->argument > 255u) {
        return 0;
    }
    if (!g->text_window.loaded && xeen_text_window_load(&g->text_window, &g->assets) != XR_OK) {
        return 0;
    }
    if (!g->large_stage && !map_text_open(g, ds, (unsigned)r->argument)) {
        return 0;
    }
    for (;;) {
        const uint8_t *text = g->large + g->large_start;
        size_t length = g->large_size - g->large_start;
        if (g->page_stage == PAGE_WRITE) {
            size_t next;
            trace_event_text(11, text + g->page_next, length - g->page_next);
            next = xeen_text_layout_run(&g->layout, &g->text_window, g->presentation->framebuffer,
                                        text, length, g->page_next);
            if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW) != XEEN_OK) {
                return -1;
            }
            if (next == XEEN_TEXT_LAYOUT_END) {
                return map_text_close(g, ds, n);
            }
            page_key_wait_open(g, next);
        }
        handled = page_key_wait(g, &wait, ds, n, ax);
        if (handled != 1) {
            return handled;
        }
        page_continue(g, 11, map_text_rect, home, sizeof home); /* 4E38C(0Bh) after the wait's scenes, "\r" (630E) */
    }
}
/* The bounds of the paged text's WINDOW: window 11's (operation 53's dialogue, 1871B(0Bh)'s frame at (8, 8, 216,
 * 132), as operation 50's map text) or window 12's, the bottom (0, 143, 320, 56). */
static const uint16_t *paged_text_rect(unsigned window)
{
    static const uint16_t bottom[4] = {0, 143, 320, 56};
    return window == 11u ? map_text_rect : bottom;
}
/* The paged text's opening, once per text (PAGE_IDLE): the event display's mark and window 38's close traced, the
 * window's opening traced when it is not open yet (its covered pixels not yet saved), its bounds taken, the window
 * opened (12: the bottom; 11: the dialogue window), and the first page's write from the text's start.
 * Original: 316EB's mark, its 18765(26h), and 1871B when the window was not open. */
static void paged_text_open(xeen_game *g, unsigned window)
{
    xeen_presentation_trace_extra("evmark event-display");
    xeen_presentation_trace_extra("evclose 38");
    if (window == 12u ? !g->bottom_active : !g->dialogue_window.active) {
        xeen_presentation_trace_extra("evopen %u", window);
    }
    page_layout(g, paged_text_rect(window));
    if (window == 12u && !g->bottom_active) {
        bottom_open(g);
    }
    if (window == 11u && !g->dialogue_window.active) {
        main_open(g);
    }
    g->page_next = 0;
    g->page_stage = PAGE_WRITE;
}

/** Serves 316DC, the paged text of AC0F in window 12 or 11; returns 1, 0, -1 or the page wait's result (a suspension). */
int serve_paged_text(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n,
                            uint16_t *ax)
{
    int handled;

    /* 316DC (window 12, AC0F): 4E38C writes until the text ends or a page is full; then 50BF6, and with text left
     * 189D7 (F5FE = 0), the 31786 loop (4EA0D, 1B503(1), 189E3 until a key), 4E38C of "\r\3c" (DS:6310) and the
     * rest. Window 11 (operation 53's 316CB): its covered pixels kept by the dialogue window (4F57B(11) restores
     * them). */
    xeen_event_request wait = {XEEN_REQUEST_PAGED_TEXT_KEY, 0, 0, 0, 0};
    static const uint8_t home[] = {13, 3, 'c', 0};
    unsigned window = r->argument;
    if ((window != 12u && window != 11u) || r->destination != XEEN_DS_TEXT_BUFFER_2) {
        return 0;
    }
    if (!g->text_window.loaded && xeen_text_window_load(&g->text_window, &g->assets) != XR_OK) {
        return 0;
    }
    if (g->page_stage == PAGE_IDLE) {
        paged_text_open(g, window);
    }
    for (;;) {
        if (g->page_stage == PAGE_WRITE) {
            size_t next;
            trace_event_text(window, ds + XEEN_DS_TEXT_BUFFER_2 + g->page_next, (XEEN_TEXT_BUFFER_BYTES - g->page_next));
            next = xeen_text_layout_run(&g->layout, &g->text_window, g->presentation->framebuffer,
                                        ds + XEEN_DS_TEXT_BUFFER_2, XEEN_TEXT_BUFFER_BYTES, g->page_next);
            if (xeen_presentation_update_window(g->presentation, (uint16_t)window) != XEEN_OK) {
                return -1;
            }
            if (next == XEEN_TEXT_LAYOUT_END) {
                g->page_stage = PAGE_IDLE;
                return 1;
            }
            page_key_wait_open(g, next);
        }
        handled = page_key_wait(g, &wait, ds, n, ax);
        if (handled != 1) {
            return handled;
        }
        page_continue(g, window, paged_text_rect(window), home, sizeof home);
    }
}
