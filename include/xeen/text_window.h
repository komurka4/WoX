#ifndef XEEN_TEXT_WINDOW_H
#define XEEN_TEXT_WINDOW_H
#include "xeen/presentation.h"
#include "xeen/resources.h"
/* Bounded original English font and window38. Original font is owned here;
 * saved pixels permit close without a fabricated scene. No pagination token
 * is consumed: unsupported controls/overflow reject before framebuffer writes. */
typedef struct {
    uint8_t font[4352], saved[94u * 59u];
    int loaded, active;
} xeen_text_window;
xeen_resource_status xeen_text_window_load(xeen_text_window *, const xeen_archive *);
xeen_status xeen_text_window_show(xeen_text_window *, xeen_presentation *, const uint8_t *, size_t);
xeen_status xeen_text_window_close(xeen_text_window *, xeen_presentation *, uint16_t);
/* Independent glyph service, original 8x8 two-bit rows, proportional advance;
 * colour0 is transparent. Returns width through advance. */
xeen_status xeen_text_glyph(const uint8_t *, size_t, uint8_t *, size_t, unsigned x, unsigned y,
                            unsigned character, const uint8_t colours[4], unsigned *advance);
/* Bounded original reduced-font scene overlay, window3; no saved window. */
xeen_status xeen_text_overlay_show(xeen_text_window *, xeen_presentation *, const uint8_t *,
                                   size_t);

/*3000D full-layout single-page dialogue. Independent saved-pixel owner keeps
 * the earlier window38 ABI intact. Text past the bottom is not drawn (4E38C
 * returns it; 3000D mode 1 shows one page). the text is
 * xeen_dialogue_show_layout's (4E38C's port) from a neutral state. */
typedef struct {
    uint8_t saved[216u * 132u];
    int active;
} xeen_dialogue_window;
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
xeen_status xeen_dialogue_show(xeen_dialogue_window *, const xeen_text_window *,
                               xeen_presentation *, const uint8_t *, size_t);
xeen_status xeen_dialogue_close(xeen_dialogue_window *, xeen_presentation *);

/* The pixels under a framed window at an arbitrary rectangle (1871B frame,
 * 4F6F8 tiles), saved by its opener (the confirmation box) and restored by
 * xeen_text_box_close; presentation only, no DS. (Phase 275 removed the
 * unused xeen_text_box_show, which drew wrapped text into such a box.) */
typedef struct {
    uint8_t saved[64000];
    uint16_t x, y, w, h;
    int active;
} xeen_text_box;
xeen_status xeen_text_box_close(xeen_text_box *, xeen_presentation *);

/* Window text in the 4E38C control language, presentation only (writes no
 * DS). The control semantics are a labelled hypothesis taken from ScummVM's
 * FontSurface::writeString (not original-verified): CR clears the bounds to
 * the background (153) and homes; 01/02 normal/reduced font; 03 c|r|l
 * justification; 04nnn a background box; 07nnn background colour; 09nnn x
 * and 0Bnnn y from the bounds; 0A newline (10 rows, 9 reduced); 0Cnn or 0Cd
 * the original colour table entry; 05 is skipped, 06 is a space; any other
 * control below 20h ends the text. Long runs wrap at the last space; text
 * below the bounds is not drawn. The cursor carries position, colours and
 * modes between calls (NULL: a fresh cursor at the bounds' origin). */
typedef struct {
    int x, y;
    uint8_t justify, reduced, background, colours[4];
} xeen_text_cursor;
void xeen_text_cursor_init(xeen_text_cursor *, const uint16_t bounds[4]);
/* The cursor's colour as 4E38C's colour index (BEA, an entry of
 * the CS:1702 table: the entries are distinct, so the lookup is exact;
 * 0 for colours outside the table) and its setter (4E780). */
unsigned xeen_text_cursor_colour(const xeen_text_cursor *);
void xeen_text_cursor_set_colour(xeen_text_cursor *, unsigned index);
xeen_status xeen_text_write(const xeen_text_window *, uint8_t *pixels, size_t size,
                            const uint16_t bounds[4], const char *text, xeen_text_cursor *);
/* The text cursor of the 28876 key loop (driver CS:0BE4/0BE6;
 * original-verified listings). 4E8DA fills the 8x8 cell at the cursor with
 * the background; 4E848 draws the next frame of CS:17CE {32, 124, 126, 127,
 * 126, 124} (its index CS:0C10 counts down, 0 wraps to 5) there in the
 * cursor's colours, without moving it. Presentation only. */
void xeen_text_cursor_erase(uint8_t *pixels, size_t size, const xeen_text_cursor *);
xeen_status xeen_text_cursor_draw(const xeen_text_window *, uint8_t *pixels, size_t size,
                                  const xeen_text_cursor *, unsigned *frame);
/* 1871B's frame (4F2A1): the frame tiles of a window rectangle (x, y,
 * width, height) and its inner area cleared to 153 (4F138). Presentation
 * only. */
xeen_status xeen_text_frame(uint8_t *pixels, size_t size, const uint16_t window[4]);
/* 4F6F8, the frame tiles alone (4F24A; the inner area is kept). */
xeen_status xeen_text_border(uint8_t *pixels, size_t size, const uint16_t window[4]);

/* 4E38C's layout of one call into a window's text bounds (4F1BE:
 * the rectangle inset by the border), carrying x, y, justification, font
 * and colours between calls as its code-segment state does. Draws into
 * pixels (NULL measures only). Returns XEEN_TEXT_LAYOUT_END when the text
 * ends (4E38C's NULL), else the offset of the rest after a new line that
 * passed the bottom bound (its spaces skipped). Reads at or past n are NUL. */
#define XEEN_TEXT_LAYOUT_END ((size_t) - 1)
typedef struct {
    int left, right, top, bottom, x, y;
    uint8_t justify, reduced, background, colour;
} xeen_text_layout;
void xeen_text_layout_init(xeen_text_layout *, const uint16_t window[4], unsigned border);
/* 4E38C's text state is never reset (Phase 154): after a window's xeen_text_layout_init, WAS's justification,
 * reduced font, background and colour carry over into S, and with CURSOR also its end position (x, y). */
void xeen_text_layout_carry(xeen_text_layout *s, const xeen_text_layout *was, int cursor);
size_t xeen_text_layout_run(xeen_text_layout *, const xeen_text_window *, uint8_t *pixels,
                            const uint8_t *text, size_t n, size_t at);
/* xeen_dialogue_show with the text drawn by this port from the
 * caller's carried state S (its bounds set to window 11's inner area, 16..216
 * x 16..132); REST (optional) receives the layout's return. xeen_dialogue_show is now this with a neutral state. */
xeen_status xeen_dialogue_show_layout(xeen_dialogue_window *, const xeen_text_window *,
                                      xeen_presentation *, const uint8_t *text, size_t size,
                                      xeen_text_layout *s, size_t *rest);
#endif
