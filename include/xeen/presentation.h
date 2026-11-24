#ifndef XEEN_PRESENTATION_H
#define XEEN_PRESENTATION_H
#include "xeen/movement.h"
#include "xeen/windows.h" /* enum xeen_window */
#include <stddef.h>
#define XEEN_PRESENT_WIDTH  320u
#define XEEN_PRESENT_HEIGHT 200u
#define XEEN_PRESENT_PIXELS 64000u
/* Original 4ADB1 traversal of 12-byte records: handle, frame, x, y, word+8,
 * flags. A zero handle ends the list. Handles with bit15 set, frame FFFF or
 * word+8 FFFF are skipped. Drawn entries are kept in original order. */
#define XEEN_DRAW_LIST_MAX 256u
/* The gameplay RNG one driver draw consumes (per entry of a call,
 * xeen_presentation.effects). 4AEB8's colour modes
 * 2 and 5 call the core 50525 twice at their entry, once per frame cell
 * (4CCDD/4CCE5, mirrored 4CF6D/4CF75; 4D187/4D18F, mirrored 4D5F1/4D5F9).
 * raw/carry are 50525's AX and carry flag, cell 0 in [0],[1], cell 1 in
 * [2],[3]; they seed the driver's pixel generator CS:18D2/18D4. */
typedef struct {
    uint16_t raw[4];
    uint8_t carry[4];
    uint8_t count;
} xeen_effect_rng;
typedef struct {
    uint16_t record, handle, frame, x, y, scale, flags;
} xeen_draw_command;
typedef struct {
    uint16_t window, list, records;
    size_t count;
    xeen_draw_command commands[XEEN_DRAW_LIST_MAX];
} xeen_draw_list;
/* One driver sprite draw (4AC92): handle, frame, x, y, scale, flags, window. */
typedef struct {
    uint16_t handle, frame, x, y, scale, flags, window;
} xeen_sprite_draw;
#define XEEN_SPRITE_DRAW_MAX 512u
#define XEEN_PRESENT_OPS     8u
#define XEEN_PRESENT_TEXTS   8u
enum {
    XEEN_PRESENT_OP_COPY = 1,
    XEEN_PRESENT_OP_FLIP = 2,
    XEEN_PRESENT_OP_FULL = 3
};
/* Headless native surfaces, not VGA memory. Caller owns this persistent object;
 * framebuffer and decoded cursor storage are borrowed and must outlive use.
 * Decoder/resource ownership is external: supplying diagnostic pixels does not
 * authenticate the game's sprite cache. Opacity is one byte per cursor pixel.
 * All buffers must be disjoint from this object and each other. */
/* The page selector's value for the second display page (the first is 0): each flip XORs it, and the draw page
 * is pages[selector != 0]. */
#define XEEN_PRESENT_PAGE_2 0x4000u
/* The pending-window word (dirty): FFFFh nothing pending, 0 the whole screen, else the window number times 2
 * (an odd value or one from 8000h is not the driver's). */
#define XEEN_PRESENT_DIRTY_NONE   0xffffu
#define XEEN_PRESENT_DIRTY_SCREEN 0u
typedef struct {
    uint8_t *framebuffer;
    size_t framebuffer_size;
    const uint8_t *cursor_pixels, *cursor_opacity;
    size_t cursor_size;
    uint16_t cursor_width, cursor_height;
    uint16_t old_x, old_y, cursor_x, cursor_y, sampled_x, sampled_y;
    uint16_t dirty, page_selector;
    uint8_t enabled, decoder, displayed_page;
    uint8_t pages[2][XEEN_PRESENT_PIXELS];
    /* Latest accepted scene draw list (renderer 4ADB1 submission). */
    xeen_draw_list scene;
    uint64_t scene_submissions;
    uint64_t completed_frames, synchronization_count, copied_pixels;
    /* Latest accepted
     * individual sprite draws, e.g. minimap 1E467. */
    size_t sprite_count;
    xeen_sprite_draw sprites[XEEN_SPRITE_DRAW_MAX];
    uint64_t sprite_submissions;
    /* Ordered rectangle copies (4ECDB argument order x, y, height, width, page)
     * and present steps (4EF71) of the latest present/update_window call. */
    size_t op_count;
    uint16_t ops[XEEN_PRESENT_OPS][6];
    /* Recorded window text (4E38C) and frame (4F6F8) requests; no font or
     * frame rasterisation. Latest XEEN_PRESENT_TEXTS kept, counters total. */
    size_t text_count;
    uint64_t texts_total, frames_total;
    uint16_t text_windows[XEEN_PRESENT_TEXTS];
    char texts[XEEN_PRESENT_TEXTS][256];
    uint16_t last_frame_window;
    /* VGA DAC state from original port writes (3C8 index, 3C9 RGB data). */
    uint8_t palette[768];
    uint16_t dac_index;
    uint8_t dac_component;
    uint64_t dac_writes;
    uint16_t runtime_windows[2][4]; /* windows28/39: x,y,width,height */
    uint8_t runtime_window_valid[2];
    uint64_t fade_requests;
    uint16_t last_fade_step;
    uint8_t last_fade_in;
    /* The DAC's fade level as 80h minus the 4A74F/4A75A level
     * (0: the full palette, 80h: black; zero-initialised = full), the scaled
     * palette the displays use while it is nonzero, and the fades run. */
    uint8_t fade_dim;
    uint8_t dac[768];
    uint64_t fades_run;
    uint16_t cursor_handle, cursor_frame; /* 4E06B resource selection */
    /* Optional native raster adapter. Called in submission order, before the
     * recorder changes. Callback must reject atomically. User is borrowed. */
    void *raster_user;
    xeen_status (*raster_scene)(void *, const xeen_draw_list *);
    xeen_status (*raster_sprites)(void *, const xeen_sprite_draw *, size_t);
    /* scene-list draws of a handle with no resident sprite (a stale
     * DS handle after a map load freed it, e.g. F452 in outdoor record 1742).
     * The driver's 4ADB1 indexes its handle table without validation and
     * draws stale memory, writing no game DS; the native raster skips them.
     * Single 4AC92 draws still reject (the driver exits fatally there). */
    uint64_t stale_sprite_draws;
    /* scene-list draws of a frame past the sprite's frame table
     * (unchecked by 4ADB1), skipped. */
    uint64_t invalid_frame_draws;
    /* scene-list draws in a special colour/effect mode (flags
     * 0001h..1FFFh, e.g. a monster's 36931 spell-hit effect through the
     * draw records), which the native raster does not render; skipped and
     * counted (presentation only). */
    uint64_t effect_mode_draws;
    /* The live pointer, read by every present as the driver's
     * 4E295 does (INT 33h fn 3 before each cursor draw); NULL keeps the
     * position the last 189E3 sample left (headless, no mouse). */
    void (*pointer)(void *, uint16_t *x, uint16_t *y);
    void *pointer_user;
    /* The driver's draws take gameplay RNG (xeen_effect_rng). The
     * game installs this; submit and draw_sprites call it for every drawn
     * entry in order, first with check != 0 for all (nothing consumed; a
     * rejection rejects the whole call), then to take the draws. NULL: no
     * draw consumes RNG (a presentation with no game behind it). */
    xeen_status (*effect_rng)(void *, uint16_t handle, uint16_t frame, uint16_t scale,
                              uint16_t flags, int check, xeen_effect_rng *);
    void *effect_rng_user;
    /* The effect RNG of the call in progress, by entry (filled
     * before the raster callback runs; zero without the hook). */
    xeen_effect_rng effects[XEEN_SPRITE_DRAW_MAX];
    /* Draws in 4AEB8 modes 1..6 that the raster rendered. */
    uint64_t effect_draws;
    /* An optional frontend display (the window), called with the
     * displayed page where the original's screen shows an intermediate frame
     * that a later flip of the same game step replaces: XEEN_DISPLAY_RETRACE
     * after each synchronization of 189E3/18C0C's button feedback (4EF71 or
     * 4EA85 wait for the next vertical retrace, 4E90A), XEEN_DISPLAY_SCENE_START
     * at a combat-module scene's 4EA35 and XEEN_DISPLAY_SCENE_END at its end
     * (1C1B3: 4EA44 until 2 BIOS ticks since that 4EA35). Display and pacing
     * only: no DS, RNG or presentation state. NULL (headless): no call. */
    void (*display)(void *, unsigned reason, const uint8_t *page, const uint8_t *palette);
    void *display_user;
    uint64_t displays;
    /* 'enabled' is the driver's mouse flag CS:16BE (0 in the
     * image: off until the first 4E06B). While it is 0, 4E33F presents
     * nothing, 4E18F samples no mouse, and 50BF6/4EA85/4EF71 copy the pages
     * without drawing the cursor (4E295/4E254 return at once). The 4E06B
     * calls that turned the mouse back on (each one clears the press stack
     * CS:5C40). */
    uint64_t mouse_resets;
    /* Called when 4E06B turns the mouse on (5C40 = 0: the press
     * stack is emptied; the presses made while the mouse was off are lost,
     * as the INT 33h reset of 4E231 removed the press handler). */
    void (*mouse_reset)(void *);
    void *mouse_reset_user;
} xeen_presentation;
#define XEEN_DISPLAY_RETRACE     1u
#define XEEN_DISPLAY_SCENE_START 2u
#define XEEN_DISPLAY_SCENE_END   3u
/* Phase 310 (owner decision, NOT original timing): the map load's box ("Please Wait..." / "Oops..."), held
 * XEEN_DISPLAY_WAIT_BOX_MS on screen. The original holds it from 50BF6(9)'s retrace until its load ends (no timer;
 * disk-bound); the native load takes milliseconds, so the owner chose a fixed native-only minimum (Phase 310:
 * 100 ms; Phase 310B: 200 ms). Display and pacing only: no DS, RNG, clock or input change (headless: no wait). */
#define XEEN_DISPLAY_WAIT_BOX    4u
#define XEEN_DISPLAY_WAIT_BOX_MS 200u
/* 4A74F(step) (in != 0) / 4A75A(step): the palette faded over
 * the DAC from level 80h (out) or 0 (in) by step per 4A7B6 write, one 4E90A
 * vertical retrace (XEEN_DISPLAY_RETRACE) per write, the last write at
 * level 0 (out) or 80h (in); 4A74F(81h) writes the full palette at once.
 * Each level is (palette * level * 2) >> 8 of every 6-bit component (4A796).
 * No input is polled meanwhile; display and pacing only (no DS, RNG or
 * page change). The level stays until the next fade. */
void xeen_presentation_fade(xeen_presentation *, int in, uint16_t step);
/* 4E231, the mouse off: with CS:16BE set, the INT 33h reset,
 * 16BE = 0, the page toggle and 4EA85's full copy (now without the cursor);
 * nothing when the mouse is already off. */
void xeen_presentation_mouse_off(xeen_presentation *);
/* 4E06B(handle, frame)'s cursor selection; with the mouse off it
 * also turns it on (16BE = 1, the press stack cleared: mouse_resets + 1).
 * Returns 1 when it turned the mouse on. The page toggle and full copy that
 * end 4E06B stay with each caller (as before this phase). */
int xeen_presentation_select_cursor(xeen_presentation *, uint16_t handle, uint16_t frame);
/* The palette the DAC shows (the palette scaled by the fade). */
const uint8_t *xeen_presentation_dac(xeen_presentation *);
/* The display hook with the displayed page (no hook: nothing). */
void xeen_presentation_display(xeen_presentation *, unsigned reason);
/* Phase 310: the display hook with a copy of the displayed page whose WINDOW rectangle is the framebuffer's, as a
 * window present would show it, without the present: no page, flip, pending word or counter changes but displays
 * (no hook, no window or no memory: nothing). For a frame the original shows across a session hand-over the native
 * draws with the outgoing session (xeen_game_show_session_wait). */
void xeen_presentation_display_window(xeen_presentation *, uint16_t window, unsigned reason);
/* Original 50BF6 updates the selected page. Window0 or pending0 performs
 * a full320x200 copy with cursor and retains window*2 as pending. Otherwise
 * the previous pending and current rectangles are copied before 4EF71.
 * Runtime windows28/39 must first be defined; invalid rectangles reject. */
/* Original 4EA85: cursor sample/draw, full framebuffer copy to selected page,
 * synchronization, restore. Does not toggle page or change pending state. */
xeen_status xeen_presentation_full_copy(xeen_presentation *);
/* A window restore (4F57B) writes the work
 * buffer only; the original's display shows the restored background, while
 * the two-page model would show it on one page at most (the next updates
 * copy other windows), leaving the closed window's pixels on the other page
 * (a blinking or frozen leftover in a window). This copies the framebuffer
 * rectangle into both pages; no page flip, no pending change. */
void xeen_presentation_restore_rect(xeen_presentation *, uint16_t x, uint16_t y, uint16_t width,
                                    uint16_t height);
xeen_status xeen_presentation_define_window(xeen_presentation *, uint16_t window, uint16_t x,
                                            uint16_t y, uint16_t height, uint16_t width);
xeen_status xeen_presentation_update_window(xeen_presentation *, uint16_t window);
/* Original 4E33F: enabled, pending FFFF or window*2, decoder0. Pointer sample is supplied
 * before invocation (does not dispatch input). Headless synchronization marks
 * ordered frame completion; wall-clock/retrace pacing belongs to a frontend.
 * Disabled service is a verified unchanged return. Unsupported input rejects
 * before all state mutation. No gameplay DS is read or written. */
xeen_status xeen_presentation_present(xeen_presentation *);
/* Renderer submission (original call 2469A -> 4ADB1). Copies the ordered draw
 * list as the pending scene frame for the frontend. Sprite decoding/raster
 * output is performed when the optional native raster callback is bound.
 * Otherwise this remains a recorder. The original renderer writes no gameplay DS. */
xeen_status xeen_presentation_submit(xeen_presentation *, const xeen_draw_list *);
/* Ordered individual sprite draws (driver 4AC92). The caller must have
 * resolved every handle and frame against its sprite cache. The optional
 * native raster callback runs before replacing/counting the recorded batch. */
/* Original 4A68A (scene caller 1BF66, argument W(F60C)) on the 216x132 3D
 * viewport at (8,8) of the software framebuffer: level >= 4 no change; 0
 * fills colour0; 1..3 remap every pixel through the original driver table.
 * Levels 0..3 require the borrowed framebuffer. No gameplay DS involved. */
xeen_status xeen_presentation_darken(xeen_presentation *, uint16_t level);
/* Original 4E38C window text and 4F6F8 window frame: recorded requests only
 * (verified to write no game DS and use no I/O). The recorder admits the
 * complete bounded native call-site strings, including three-monster combat
 * headings. */
xeen_status xeen_presentation_text(xeen_presentation *, uint16_t window, const char *text);
xeen_status xeen_presentation_frame(xeen_presentation *, uint16_t window);
/* Port-out adapter for original palette code (ports3C8/3C9; others reject). */
xeen_status xeen_presentation_port(xeen_presentation *, uint16_t port, uint8_t value);
xeen_status xeen_presentation_draw_sprites(xeen_presentation *, const xeen_sprite_draw *, size_t);
/* Driver window rectangle (x, y, width, height) for windows 0..47; runtime
 * windows 28/39 once defined. Returns 0 for a zero or unknown window. */
int xeen_presentation_window(const xeen_presentation *, uint16_t window, uint16_t rect[4]);
/* 4F1BE's border of WINDOW, the driver's word CS:01EE[window]: 0 for windows 0, 1, 3, 7, 8 and 20h, else 8
 * (a window's texts and draw lists start that far inside its rectangle). */
unsigned xeen_window_border(uint16_t window);
/* Evidence: the XEEN_DRAW_TRACE switch and its "draw ..." stdout
 * line (presentation, list, text, window and marker requests). */
int xeen_presentation_tracing(void);
void xeen_presentation_trace(const char *format, ...);
/* Evidence lines: only with XEEN_DRAW_TRACE and XEEN_DRAW_TRACE_EXTRA. */
int xeen_presentation_tracing_extra(void);
/* Nonzero when XEEN_TRACE_TEXT_STATE is set (evidence only: the NPC page's carried text state). */
int xeen_presentation_tracing_text_state(void);
void xeen_presentation_trace_extra(const char *format, ...);
void xeen_presentation_trace_bind(const uint8_t *ds);
void xeen_presentation_trace_unbind(const uint8_t *ds);
void xeen_presentation_trace_text(uint16_t window, const char *text);
void xeen_presentation_trace_sprite(const xeen_sprite_draw *);
#endif
