/* Phase 276: the input poll's click path (xeen_input_poll_run -> 18C0C's control test and the button feedback), by
 * observable outcome. No corpus scenario clicks (the 1,922 scenarios never reach it), so these cases pin what the
 * phase's base-vs-new fixture (xeen_dat_evidence/phase276/a) measured: the command, the stop, the poll's residue
 * word, the feedback's draws (pressed frame 2i+1, then released 2i) and retraces, and the drained clicks. The button
 * sets are the program data's own (the compiled-in initialized DS image; src/data/structs.json button_sets rows are
 * checked first), plus synthetic tables only where a boundary needs them (a negative x word, a row past DS's end, a
 * foreign segment). Needs no local data. */
#include "xeen/input_poll.h"
#include "xeen/ds_globals.h"
#include "xeen/program_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static xeen_movement *m;
static xeen_presentation *pr;
static uint8_t fb[64000], cursor[1] = {7}, opacity[1] = {1};
static xeen_input_poll poll;
static xeen_input_queue q;
static unsigned draws, frames[8], displays, mouse_calls, refuse_mouse;
static xeen_status raster(void *u, const xeen_sprite_draw *d, size_t n)
{
    (void)u;
    for (size_t i = 0; i < n; ++i) {
        if (draws < 8u) frames[draws] = d[i].frame;
        ++draws;
    }
    return XEEN_OK;
}
static void display(void *u, unsigned reason, const uint8_t *page, const uint8_t *pal)
{
    (void)u; (void)page; (void)pal;
    if (reason == XEEN_DISPLAY_RETRACE) ++displays;
}
static int kb(void *u, uint16_t *k) { return xeen_input_queue_provider(u).keyboard(u, k); }
static int mouse(void *u, xeen_mouse_sample *s)
{
    if (++mouse_calls == refuse_mouse) return 0;
    return xeen_input_queue_provider(u).mouse(u, s);
}
static unsigned addon_draws;
static void addon_draw(void *u, unsigned i, int pressed)
{
    (void)u; (void)i;
    addon_draws = addon_draws << 4 | (pressed ? 2u : 1u); /* pressed then released: 21h */
}
static void far(uint16_t slot, uint16_t seg, uint16_t off)
{
    xeen_write16(m, slot, off);
    xeen_write16(m, (uint16_t)(slot + 2u), seg);
}
/* button_tables' order: x, y, width, height, key, enabled */
static void tables(uint16_t seg, const uint16_t t[6])
{
    static const uint16_t slots[6] = {XEEN_DS_BUTTON_X_TABLE, XEEN_DS_BUTTON_Y_TABLE, XEEN_DS_BUTTON_WIDTH_TABLE,
                                      XEEN_DS_BUTTON_HEIGHT_TABLE, XEEN_DS_BUTTON_KEY_TABLE,
                                      XEEN_DS_BUTTON_ENABLED_TABLE};
    for (int i = 0; i < 6; ++i) far(slots[i], seg, t[i]);
}
static const uint16_t exploration[6] = {0x197c, 0x19a6, 0x19ba, 0x19ce, 0x19e2, 0x19f6};
static const uint16_t list10[6] = {0x190a, 0x192c, 0x193c, 0x194c, 0x195c, 0x196c};
static void fresh(void)
{
    memset(m, 0, sizeof *m);
    memcpy(m->ds, xeen_program_ds_image, XEEN_PROGRAM_DS_SIZE);
    xeen_input_poll_destroy(&poll);
    xeen_input_poll_init(&poll, XEEN_DS_SEGMENT);
    xeen_input_queue_init(&q);
    memset(pr, 0, sizeof *pr);
    pr->enabled = 1; pr->dirty = 0xffffu; pr->framebuffer = fb; pr->framebuffer_size = sizeof fb;
    pr->cursor_pixels = cursor; pr->cursor_opacity = opacity;
    pr->cursor_width = pr->cursor_height = 1; pr->cursor_size = 1;
    pr->raster_sprites = raster; pr->display = display;
    draws = displays = mouse_calls = refuse_mouse = 0;
    memset(frames, 0xff, sizeof frames);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, 0x1234u);
}
/* One click at (X, Y) under DRAINS queued samples; returns the poll's status, *R its result. */
static xeen_status click(uint16_t x, uint16_t y, unsigned drains, xeen_input_poll_result *r)
{
    xeen_input_provider p = {kb, mouse, &q};
    for (unsigned i = 0; i < drains; ++i) {
        xeen_mouse_sample s = {(uint16_t)(10u + i), (uint16_t)(20u + i), 1, 1};
        (void)xeen_input_queue_mouse(&q, s);
    }
    (void)xeen_input_queue_mouse(&q, (xeen_mouse_sample){x, y, 1, 1});
    return xeen_input_poll_run(&poll, m, &p, NULL, NULL, pr, r);
}
int main(void)
{
    xeen_input_poll_result r;
    xeen_poll_buttons native;
    m = malloc(sizeof *m);
    pr = malloc(sizeof *pr);
    CHECK(m && pr);
    fresh();
    /* The program data's rows (structs.json button_sets): exploration button 0 S at 235,75 24x20 and 1 C at 260,75, both
     * enabled; the list's button 2, key '1' at 40,28 147x9, disabled. */
    CHECK(xeen_read16(m, 0x197c) == 235 && m->ds[0x19a6] == 75 && m->ds[0x19ba] == 24 && m->ds[0x19ce] == 20 &&
          m->ds[0x19e2] == 'S' && m->ds[0x19f6] == 1 && xeen_read16(m, 0x197e) == 260 && m->ds[0x19e3] == 'C');
    CHECK(xeen_read16(m, 0x190e) == 40 && m->ds[0x192e] == 28 && m->ds[0x193e] == 147 && m->ds[0x194e] == 9 &&
          m->ds[0x195e] == '1' && m->ds[0x196e] == 0);

    /* An enabled button: pressed (frame 1), retrace, the drain (here only the pointer's own sample, without a
     * click: presented, retrace), released (frame 0), retrace; its key. */
    tables(XEEN_DS_SEGMENT, exploration);
    CHECK(click(235, 75, 0, &r) == XEEN_OK && r.command == 'S' && poll.last_residue == 0x581du);
    CHECK(draws == 2 && frames[0] == 1 && frames[1] == 0 && displays == 3 && mouse_calls == 2);
    CHECK(pr->runtime_window_valid[1] && pr->runtime_windows[1][0] == 235 && pr->runtime_windows[1][1] == 75);
    /* its last pixel (258, 94) is S; the next column (259 + 1 = 260) is button 1, C (frames 3 then 2) */
    fresh(); tables(XEEN_DS_SEGMENT, exploration);
    CHECK(click(258, 94, 0, &r) == XEEN_OK && r.command == 'S');
    fresh(); tables(XEEN_DS_SEGMENT, exploration);
    CHECK(click(260, 75, 0, &r) == XEEN_OK && r.command == 'C' && frames[0] == 3 && frames[1] == 2);
    /* a later row of the set: button 7, I at 260,117 (the rows are searched in order, at most 128) */
    fresh(); tables(XEEN_DS_SEGMENT, exploration);
    CHECK(m->ds[0x19e2 + 7] == 'I' && click(260, 117, 0, &r) == XEEN_OK && r.command == 'I' && frames[0] == 15);
    /* past the bottom edge (y 75 + 20): the button below (y 96) is not reached at 95: nothing */
    fresh(); tables(XEEN_DS_SEGMENT, exploration);
    CHECK(click(235, 95, 0, &r) == XEEN_OK && r.command == 0 && draws == 0 && poll.last_residue == 0);

    /* Queued clicks under the press are drained (newest first) before the release, each moving the pointer and
     * presenting with a retrace, until a sample without a click: the pointer's own position (the click's). */
    fresh(); tables(XEEN_DS_SEGMENT, exploration);
    CHECK(click(235, 75, 2, &r) == XEEN_OK && r.command == 'S' && q.mouse_count == 0 && mouse_calls == 4);
    CHECK(xeen_read16(m, XEEN_DS_MOUSE_X) == 235 && xeen_read16(m, XEEN_DS_MOUSE_Y) == 75 && displays == 5);
    /* A host that gives no sample to drain stops the feedback at 18D66 (pressed, not released, no command). */
    fresh(); tables(XEEN_DS_SEGMENT, exploration); refuse_mouse = 2;
    CHECK(click(235, 75, 0, &r) == XEEN_SERVICE_REQUIRED && r.boundary == XEEN_POLL_FEEDBACK_DRAIN && draws == 1);
    CHECK(poll.last_residue == 0x581du && r.command == 0);

    /* A disabled button gives its key without feedback (and no residue). */
    fresh(); tables(XEEN_DS_SEGMENT, list10);
    CHECK(click(40, 28, 0, &r) == XEEN_OK && r.command == '1' && draws == 0 && poll.last_residue == 0);

    /* The default tables (98AC = DS:1BC4): command 1 wherever the click is. */
    fresh(); far(XEEN_DS_BUTTON_X_TABLE, XEEN_DS_SEGMENT, 0x1bc4u);
    CHECK(click(300, 190, 0, &r) == XEEN_OK && r.command == 1 && draws == 0);

    /* Portraits first (98D6 set): member i's 32 x 32 square at x 17B2[i], y 150..181: F1 + i. */
    fresh(); tables(XEEN_DS_SEGMENT, exploration);
    m->ds[XEEN_DS_PORTRAIT_CLICKS] = 1; m->ds[XEEN_DS_PARTY_COUNT] = 3;
    {
        unsigned x2 = m->ds[XEEN_DS_PORTRAIT_X + 2];
        CHECK(click((uint16_t)(x2 + 31u), 181, 0, &r) == XEEN_OK && r.command == XEEN_KEY_F1 + 2u);
        fresh(); tables(XEEN_DS_SEGMENT, exploration);
        m->ds[XEEN_DS_PORTRAIT_CLICKS] = 1; m->ds[XEEN_DS_PARTY_COUNT] = 3;
        CHECK(click((uint16_t)(x2 + 31u), 182, 0, &r) == XEEN_OK && r.command != XEEN_KEY_F1 + 2u);
        fresh(); tables(XEEN_DS_SEGMENT, exploration);
        m->ds[XEEN_DS_PORTRAIT_CLICKS] = 1; m->ds[XEEN_DS_PARTY_COUNT] = 2; /* member 2 is not in the party */
        CHECK(click((uint16_t)(x2 + 31u), 181, 0, &r) == XEEN_OK && r.command != XEEN_KEY_F1 + 2u);
    }

    /* Combat (9A1C 2), a click on no button: a monster column, 0Eh + the column; with view slot 1 a monster and
     * slot 2 none (0), the two halves 3..4; slot 2 FFh counts as a monster (the slots are compared with 0). */
    /* (150, 119) is on none of the list's buttons; column 0 is x 72..159, column 3 (a half) x 116..203. */
    fresh(); tables(XEEN_DS_SEGMENT, list10); m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_COMBAT;
    CHECK(click(150, 119, 0, &r) == XEEN_OK && r.command == 0x0eu);
    fresh(); tables(XEEN_DS_SEGMENT, list10); m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_COMBAT;
    m->ds[XEEN_DS_VIEW_MONSTER(1)] = 5;
    CHECK(click(150, 119, 0, &r) == XEEN_OK && r.command == 0x0eu + 3u);
    fresh(); tables(XEEN_DS_SEGMENT, list10); m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_COMBAT;
    m->ds[XEEN_DS_VIEW_MONSTER(1)] = 5; m->ds[XEEN_DS_VIEW_MONSTER(2)] = 0xff;
    CHECK(click(150, 119, 0, &r) == XEEN_OK && r.command == 0x0eu);
    fresh(); tables(XEEN_DS_SEGMENT, list10); m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_COMBAT;
    CHECK(click(100, 127, 0, &r) == XEEN_OK && r.command == 0x0eu); /* the columns' last row (y 42..127) */
    fresh(); tables(XEEN_DS_SEGMENT, list10); m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_COMBAT;
    CHECK(click(100, 128, 0, &r) == XEEN_OK && r.command == 0);
    fresh(); tables(XEEN_DS_SEGMENT, list10); /* outside combat: no column */
    CHECK(click(150, 119, 0, &r) == XEEN_OK && r.command == 0);

    /* Synthetic rows: a negative x word (FFF0h, -16) with width 40 covers x -16..23 (signed words, wrapping);
     * an unterminated table running past DS's end is a bad row (18BFE); a foreign segment cannot be read (18C6B). */
    fresh();
    {
        static const uint16_t t[6] = {0xa000, 0xa100, 0xa120, 0xa140, 0xa160, 0xa180};
        tables(XEEN_DS_SEGMENT, t);
        xeen_write16(m, 0xa000, 0xfff0u); xeen_write16(m, 0xa002, 0xffffu);
        m->ds[0xa100] = 10; m->ds[0xa120] = 40; m->ds[0xa140] = 10; m->ds[0xa160] = 'Q'; m->ds[0xa180] = 0;
        CHECK(click(5, 12, 0, &r) == XEEN_OK && r.command == 'Q');
        fresh(); tables(XEEN_DS_SEGMENT, t);
        xeen_write16(m, 0xa000, 0xfff0u); xeen_write16(m, 0xa002, 0xffffu);
        m->ds[0xa100] = 10; m->ds[0xa120] = 40; m->ds[0xa140] = 10; m->ds[0xa160] = 'Q'; m->ds[0xa180] = 0;
        CHECK(click(24, 12, 0, &r) == XEEN_OK && r.command == 0);
        /* x words at FFFC, FFFE (rows 0, 1) and no row 2 word: the table ends there (no command) */
        fresh(); tables(XEEN_DS_SEGMENT, t);
        far(XEEN_DS_BUTTON_X_TABLE, XEEN_DS_SEGMENT, 0xfffcu);
        xeen_write16(m, 0xfffc, 0x0100u); xeen_write16(m, 0xfffe, 0x0100u);
        CHECK(click(5, 12, 0, &r) == XEEN_OK && r.command == 0);
        /* the y table at FFFF: row 1's y is past DS's end while its x word is readable: a bad row */
        fresh(); tables(XEEN_DS_SEGMENT, t);
        xeen_write16(m, 0xa000, 0x0100u); xeen_write16(m, 0xa002, 0x0100u); xeen_write16(m, 0xa004, 0xffffu);
        far(XEEN_DS_BUTTON_Y_TABLE, XEEN_DS_SEGMENT, 0xffffu);
        CHECK(click(5, 12, 0, &r) == XEEN_BAD_INPUT && r.boundary == XEEN_POLL_BUTTON_ROW);
    }
    fresh(); tables(XEEN_DS_SEGMENT, exploration); far(XEEN_DS_BUTTON_WIDTH_TABLE, 0x1234u, 0x10u);
    CHECK(click(235, 75, 0, &r) == XEEN_SERVICE_REQUIRED && r.boundary == XEEN_POLL_CLICK_TABLES);

    /* The owner-approved native add-on set (not original) replaces the tables: a click on no add-on button is no
     * command even over a live button; an enabled one is drawn pressed then released (its own callback) and gives
     * its key, leaving the residue 581Dh. */
    memset(&native, 0, sizeof native);
    native.count = 1; native.x[0] = 10; native.y[0] = 10; native.w[0] = 5; native.h[0] = 5; native.key[0] = 'K';
    native.enabled[0] = 1; native.draw = addon_draw;
    fresh(); tables(XEEN_DS_SEGMENT, exploration); poll.addon_buttons = &native;
    CHECK(click(235, 75, 0, &r) == XEEN_OK && r.command == 0 && draws == 0 && addon_draws == 0);
    fresh(); tables(XEEN_DS_SEGMENT, list10); poll.addon_buttons = &native; addon_draws = 0;
    CHECK(click(14, 14, 0, &r) == XEEN_OK && r.command == 'K' && addon_draws == 0x21u && poll.last_residue == 0x581du);
    /* disabled: its key, no drawing, no residue */
    native.enabled[0] = 0;
    fresh(); tables(XEEN_DS_SEGMENT, list10); poll.addon_buttons = &native; addon_draws = 0;
    CHECK(click(14, 14, 0, &r) == XEEN_OK && r.command == 'K' && addon_draws == 0 && poll.last_residue == 0);
    /* the portraits are tested before the add-on set */
    fresh(); tables(XEEN_DS_SEGMENT, list10); poll.addon_buttons = &native;
    m->ds[XEEN_DS_PORTRAIT_CLICKS] = 1; m->ds[XEEN_DS_PARTY_COUNT] = 1;
    CHECK(click(m->ds[XEEN_DS_PORTRAIT_X], 150, 0, &r) == XEEN_OK && r.command == XEEN_KEY_F1);
    xeen_input_poll_destroy(&poll);
    free(pr);
    free(m);
    puts("phase276 click control: PASS");
    return 0;
}
