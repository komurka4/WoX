/* Phase175: the mouse flag CS:16BE in the presentation (4E231 / 4E06B) and
 * the window's composited cursor (the owner's black squares at the Load
 * fade). Headless and offscreen only.
 *
 * P1  4E231 (xeen_presentation_mouse_off): page toggle, full copy without the
 *     cursor, 16BE 0; a second call changes nothing.
 * P2  with the mouse off: 4E33F (present) changes nothing, the pointer is not
 *     read (4E295), 50BF6 copies without the cursor (full and window forms).
 * P3  the Load sequence: the menu page presented, 4E231, back.raw into the
 *     framebuffer, the 1B503(0) present at 1BECF: the shown page is still the
 *     menu's, with no square. NEGATIVE: the same without 4E231 (the Phase 174
 *     native) leaves the cursor rectangles of back.raw on the shown page.
 * P4  4E06B (xeen_presentation_select_cursor): on from off once (the press
 *     stack hook, mouse_resets), the handle; already on: no reset.
 * H1  the host compositor: a stationary cursor, moves (no trails), a return
 *     to an earlier point, overlapping points, the faded palette (the frame
 *     itself never changes), and show_cursor(0/1). NEGATIVE: the trail
 *     detector flags a frame that keeps an earlier cursor. */
#include "xeen/presentation.h"
#include "xeen/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
static xeen_presentation p, before;
static uint8_t fb[64000], menu[64000];
static uint8_t cur[16 * 12], mask[16 * 12];
static unsigned pointer_reads, resets;
static void pointer(void *u, uint16_t *x, uint16_t *y) { (void)u; ++pointer_reads; *x = 150; *y = 90; }
static void reset_hook(void *u) { (void)u; ++resets; }
static void setup(void) {
    unsigned i;
    memset(&p, 0, sizeof p);
    for (i = 0; i < 64000u; ++i) menu[i] = (uint8_t)(0x40u + (i * 13u + i / 320u) % 0x50u);   /* never 0 */
    memcpy(fb, menu, sizeof fb);
    for (i = 0; i < sizeof cur; ++i) { cur[i] = (uint8_t)(0xe0u + i % 8u); mask[i] = (uint8_t)((i % 16u) <= (i / 16u)); }  /* a triangle */
    p.framebuffer = fb; p.framebuffer_size = sizeof fb;
    p.cursor_pixels = cur; p.cursor_opacity = mask; p.cursor_size = sizeof cur;
    p.cursor_width = 16; p.cursor_height = 12;
    p.old_x = p.cursor_x = p.sampled_x = 160; p.old_y = p.cursor_y = p.sampled_y = 100;
    p.dirty = 0xffff; p.enabled = 1;
}
/* Pixels of the shown page that differ from REF inside the 24x14 box at (x, y). */
static unsigned damage(const uint8_t *page, const uint8_t *ref, unsigned x, unsigned y) {
    unsigned n = 0, r, c;
    for (r = y; r < y + 14u && r < 200u; ++r)
        for (c = x & ~3u; c < x + 24u && c < 320u; ++c) n += page[r * 320u + c] != ref[r * 320u + c];
    return n;
}
static int has_cursor(const uint8_t *page, unsigned x, unsigned y) {
    unsigned r, c;
    for (r = 0; r < 12u; ++r) for (c = 0; c < 16u; ++c)
        if (mask[r * 16u + c] && page[(y + r) * 320u + x + c] != cur[r * 16u + c]) return 0;
    return 1;
}
static void load_sequence(int mouse_off, uint8_t *shown) {
    setup();
    CHECK(xeen_presentation_update_window(&p, 0) == XEEN_OK);        /* 1700E 50BF6(0): the menu exit */
    CHECK(has_cursor(p.pages[p.displayed_page], 160, 100));
    if (mouse_off) xeen_presentation_mouse_off(&p);                   /* 15652 */
    memset(fb, 0, sizeof fb);                                         /* 179D9 back.raw: the view is 0 */
    p.sampled_x = 40; p.sampled_y = 150;                               /* the mouse moved during the load */
    CHECK(xeen_presentation_present(&p) == XEEN_OK);                  /* 1BECF 4E33F (pending FFFFh) */
    p.sampled_x = 200; p.sampled_y = 30;
    CHECK(xeen_presentation_present(&p) == XEEN_OK);                  /* a second present: a third position */
    memcpy(shown, p.pages[p.displayed_page], 64000);
}
static uint8_t a[64000], b[64000], game[64000], pal[768], faded[768], shown[64000], spal[768];
static void platform_tests(void) {
    char error[200]; xeen_platform *w = xeen_platform_open("Phase175", 2, 1, error, sizeof error);
    unsigned i, x, y, n; const uint8_t *now = pal;
    static const int path[][2] = {{20, 20}, {100, 50}, {20, 20}, {26, 24}, {200, 150}, {200, 150}, {60, 120}};
    CHECK(w != NULL); if (!w) return;
    for (i = 0; i < 64000u; ++i) game[i] = (uint8_t)(1u + i % 200u);
    for (i = 0; i < 768u; ++i) { pal[i] = (uint8_t)(i % 64u); faded[i] = (uint8_t)(pal[i] / 4u); }
    xeen_platform_set_cursor(w, cur, mask, 16, 12);
    CHECK(xeen_platform_present(w, game, pal));
    for (i = 0; i < sizeof path / sizeof path[0]; ++i) {
        CHECK(xeen_platform_mouse_event(w, path[i][0] * 2, path[i][1] * 2, 0, 0));
        if (i & 1u) { now = i == 3u ? faded : pal; CHECK(xeen_platform_present(w, game, now)); }   /* a fade level between moves */
        xeen_platform_shown(w, shown, spal);
        CHECK(!memcmp(spal, now, 768));
        /* exactly one cursor, at the live point; every other pixel is the frame's */
        n = 0;
        for (y = 0; y < 200u; ++y) for (x = 0; x < 320u; ++x) {
            unsigned dx = x - (unsigned)path[i][0], dy = y - (unsigned)path[i][1];
            int inside = x >= (unsigned)path[i][0] && y >= (unsigned)path[i][1] && dx < 16u && dy < 12u && mask[dy * 16u + dx];
            if (inside) CHECK(shown[y * 320u + x] == cur[dy * 16u + dx]);
            else n += shown[y * 320u + x] != game[y * 320u + x];
        }
        CHECK(n == 0);                                                 /* no trail, no square */
    }
    xeen_platform_show_cursor(w, 0);
    xeen_platform_shown(w, shown, spal);
    CHECK(!memcmp(shown, game, 64000));                               /* the mouse off: no cursor at all */
    CHECK(xeen_platform_mouse_event(w, 10 * 2, 10 * 2, 0, 0));
    xeen_platform_shown(w, shown, spal); CHECK(!memcmp(shown, game, 64000));
    xeen_platform_show_cursor(w, 1);
    xeen_platform_shown(w, shown, spal); CHECK(has_cursor(shown, 10, 10));
    /* NEGATIVE: a frame that kept the cursor at (20,20) while it is at (10,10) */
    memcpy(a, shown, 64000);
    for (y = 0; y < 12u; ++y) for (x = 0; x < 16u; ++x) if (mask[y * 16u + x]) a[(20u + y) * 320u + 20u + x] = cur[y * 16u + x];
    CHECK(damage(a, game, 20, 20) != 0);
    xeen_platform_close(w);
}
int main(void) {
    unsigned i;
    /* P1 */
    setup();
    xeen_presentation_mouse_off(&p);
    CHECK(!p.enabled && p.page_selector == 0x4000 && p.displayed_page == 1);
    CHECK(!memcmp(p.pages[1], menu, 64000) && !memcmp(fb, menu, 64000));
    before = p; xeen_presentation_mouse_off(&p); CHECK(!memcmp(&p, &before, sizeof p));
    /* P2 */
    p.pointer = pointer; pointer_reads = 0;
    before = p; CHECK(xeen_presentation_present(&p) == XEEN_OK); CHECK(!memcmp(&p, &before, sizeof p));
    CHECK(xeen_presentation_update_window(&p, 0) == XEEN_OK);
    CHECK(!memcmp(p.pages[p.displayed_page], menu, 64000) && pointer_reads == 0);
    fb[20 * 320 + 10] = 0x11;                                         /* a change inside window 34 */
    CHECK(xeen_presentation_update_window(&p, 34) == XEEN_OK);
    CHECK(!has_cursor(p.pages[p.displayed_page], 160, 100) && pointer_reads == 0);
    fb[20 * 320 + 10] = menu[20 * 320 + 10];
    /* P3 */
    load_sequence(1, a);
    CHECK(!memcmp(a, menu, 64000));                                   /* the menu exit's page, no cursor, no square */
    load_sequence(0, b);                                              /* NEGATIVE: the Phase 174 native */
    CHECK(damage(b, menu, 160, 100) + damage(b, menu, 40, 150) + damage(b, menu, 200, 30) > 0);
    {unsigned zero = 0; for (i = 0; i < 64000u; ++i) zero += !b[i];
     CHECK(zero > 0);}                                                /* black (back.raw) pixels reached the shown page */
    /* P4 */
    setup(); p.enabled = 0; p.mouse_reset = reset_hook; resets = 0;
    CHECK(xeen_presentation_select_cursor(&p, 0x1234, 0) == 1);
    CHECK(p.enabled && p.mouse_resets == 1 && resets == 1 && p.cursor_handle == 0x1234 && p.cursor_frame == 0);
    CHECK(xeen_presentation_select_cursor(&p, 0x1235, 0) == 0);
    CHECK(p.mouse_resets == 1 && resets == 1 && p.cursor_handle == 0x1235);
    /* H1 */
    platform_tests();
    printf(failures ? "FAILED %d\n" : "ALL PASS\n", failures);
    return failures != 0;
}
