#include "xeen/presentation.h"
#include "xeen/text_window.h"
#include "xeen/ds_globals.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "darken_tables.inc"

/** Returns nonzero when the W x H rectangle at X, Y is non-empty and inside the 320x200 screen. */
static int rect_fits(uint16_t x, uint16_t y, unsigned w, unsigned h)
{
    return w && h && x < XEEN_PRESENT_WIDTH && y < XEEN_PRESENT_HEIGHT &&
           w <= XEEN_PRESENT_WIDTH - x && h <= XEEN_PRESENT_HEIGHT - y;
}
#include "window_table.inc"
/* The windows of the original's table (0..47). */
#define WINDOW_COUNT (sizeof window_rects / sizeof window_rects[0])
/* ON (first -1) caches whether the environment variable NAME is set. */
static int env_flag(int *on, const char *name)
{
    if (*on < 0) {
        *on = getenv(name) != NULL;
    }
    return *on;
}
/* One "draw ..." evidence line on stdout (the trace and trace_extra lines). */
static void trace_line(const char *format, va_list a)
{
    fputs("draw ", stdout);
    vprintf(format, a);
    fputc('\n', stdout);
}
/* Evidence: with XEEN_DRAW_TRACE set every accepted request is printed on stdout as one "draw ..." line (the form
 * phase79/draw_trace.py compares with the original's calls). Off by default; no state changes. */
int xeen_presentation_tracing(void)
{
    static int on = -1;
    return env_flag(&on, "XEEN_DRAW_TRACE");
}
/* Evidence: with XEEN_TRACE_TEXT_STATE set, the NPC page prints the carried text state its 4E38C starts from on
 * stderr ("textstate ..."), independent of the draw traces. */
int xeen_presentation_tracing_text_state(void)
{
    static int on = -1;
    return env_flag(&on, "XEEN_TRACE_TEXT_STATE");
}
/* Evidence lines (list bytes, delivery and loop-scene marks, the monster AI's targets and orders) need
 * XEEN_DRAW_TRACE_EXTRA as well, so the older draw traces stay line for line as their comparers read them. */
int xeen_presentation_tracing_extra(void)
{
    static int on = -1;
    if (on < 0) {
        on = xeen_presentation_tracing() && getenv("XEEN_DRAW_TRACE_EXTRA") != NULL;
    }
    return on;
}
void xeen_presentation_trace_extra(const char *format, ...)
{
    va_list a;
    if (!xeen_presentation_tracing_extra()) {
        return;
    }
    va_start(a, format);
    trace_line(format, a);
    va_end(a);
}
/* The DS whose handle words name a traced sprite's handle (98D4, F458,
 * 98CE, 9738, 9734, 97FC, 9806 + 2n), as phase79/draw_trace.py does. */
static const uint8_t *trace_ds;
void xeen_presentation_trace_bind(const uint8_t *ds)
{
    trace_ds = ds;
}
void xeen_presentation_trace_unbind(const uint8_t *ds)
{
    if (trace_ds == ds) {
        trace_ds = NULL;
    }
}
void xeen_presentation_trace_text(uint16_t window, const char *text)
{
    char out[1100];
    size_t i, k = 0, n;
    if (!xeen_presentation_tracing() || !text) {
        return;
    }
    n = strlen(text);
    for (i = 0; i < n && k + 5u < sizeof out; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c < 32u || c > 126u || c == '\\') {
            k += (size_t)snprintf(out + k, sizeof out - k, "\\x%02x", c);
        } else {
            out[k++] = (char)c;
        }
    }
    out[k] = 0;
    xeen_presentation_trace("text %u %s", window, out);
}
/**
 * Names sprite handle H for the draw trace: the DS word that holds it (from a fixed list of handle
 * variables) or "hNNNN"; writes into OUT and returns it.
 */
static const char *trace_name(uint16_t h, char *out, size_t n)
{
    static const uint16_t words[] = {0x98d4, 0xf458, 0x98ce, 0x9738, 0x9734, 0x97fc, 0xf456, 0x973a,
                                     0x9736, 0x9806, 0x9808, 0x980a, 0x980c, 0x980e, 0x9810};
    size_t i;
    for (i = 0; trace_ds && i < sizeof words / sizeof words[0]; ++i) {
        if ((uint16_t)(trace_ds[words[i]] | trace_ds[words[i] + 1u] << 8) == h) {
            snprintf(out, n, "%04X", words[i]);
            return out;
        }
    }
    snprintf(out, n, "h%04x", h);
    return out;
}
/* A 4AC92 call that the driver does not complete (its fatal
 * exit 4AD19): traced as a submitted sprite is, never drawn. */
void xeen_presentation_trace_sprite(const xeen_sprite_draw *d)
{
    char name[8];
    if (!xeen_presentation_tracing() || !d) {
        return;
    }
    xeen_presentation_trace("sprite %s %u %u %u %u %04x %u",
                            trace_name(d->handle, name, sizeof name), d->frame, d->x, d->y,
                            d->scale, d->flags, d->window);
}
void xeen_presentation_trace(const char *format, ...)
{
    va_list a;
    if (!xeen_presentation_tracing()) {
        return;
    }
    va_start(a, format);
    trace_line(format, a);
    va_end(a);
}
/* 4ECDB copies complete groups of four pixels, including left/right padding. */
static void copy_rect(xeen_presentation *p, unsigned page, uint16_t x, uint16_t y, uint16_t width,
                      uint16_t height)
{
    unsigned left = x & ~3u;
    unsigned extent = ((x & 3u) + width + 3u) & ~3u;
    unsigned row;
    if (left + extent > XEEN_PRESENT_WIDTH) {
        extent = XEEN_PRESENT_WIDTH - left;
    }
    for (row = 0; row < height; ++row) {
        memcpy(p->pages[page] + (y + row) * XEEN_PRESENT_WIDTH + left, p->framebuffer + (y + row) * XEEN_PRESENT_WIDTH + left,
               extent);
    }
    p->copied_pixels += (uint64_t)extent * height;
}
/** Records one presentation operation (kind and rectangle) in the bounded operation log. */
static void log_op(xeen_presentation *p, uint16_t kind, uint16_t x, uint16_t y, uint16_t h,
                   uint16_t w)
{
    uint16_t *e;
    if (p->op_count >= XEEN_PRESENT_OPS) {
        return;
    }
    e = p->ops[p->op_count++];
    e[0] = kind;
    e[1] = x;
    e[2] = y;
    e[3] = h;
    e[4] = w;
    e[5] = p->page_selector;
}
/**
 * Returns the rectangle of WINDOW: windows 28 and 39 use their runtime rectangle once one is set,
 * the others the original's window table.
 */
static const uint16_t *window_rect(const xeen_presentation *p, uint16_t window)
{
    return (window == 28u || window == 39u) && p->runtime_window_valid[window == 39u]
                   ? p->runtime_windows[window == 39u]
                   : window_rects[window];
}
/** Returns nonzero when WINDOW exists and its rectangle is non-empty and on screen. */
static int window_ok(const xeen_presentation *p, uint16_t window)
{
    const uint16_t *r;
    if (window >= WINDOW_COUNT) {
        return 0;
    }
    r = window_rect(p, window);
    return r[3] && rect_fits(r[0], r[1], r[2], r[3]);
}
/** Copies WINDOW's rectangle from the frame buffer to the current display page and logs it. */
static void copy_window(xeen_presentation *p, uint16_t window)
{
    const uint16_t *r = window_rect(p, window);
    log_op(p, XEEN_PRESENT_OP_COPY, r[0], r[1], r[3], r[2]);
    copy_rect(p, p->page_selector != 0, r[0], r[1], r[2], r[3]);
}
/**
 * Returns nonzero when the mouse cursor's pixels, page and old, current and sampled positions are
 * valid for drawing it.
 */
static int cursor_ok(const xeen_presentation *p)
{
    return p->framebuffer && p->framebuffer_size >= XEEN_PRESENT_PIXELS && p->cursor_pixels &&
           p->cursor_opacity && p->cursor_size >= (size_t)p->cursor_width * p->cursor_height &&
           (p->page_selector == 0 || p->page_selector == XEEN_PRESENT_PAGE_2) &&
           rect_fits(p->old_x, p->old_y, p->cursor_width, p->cursor_height) &&
           rect_fits(p->cursor_x, p->cursor_y, p->cursor_width, p->cursor_height) &&
           rect_fits(p->sampled_x, p->sampled_y, p->cursor_width, p->cursor_height);
}
/* The cursor at the sampled pointer (the old position kept for the next flip's rectangle): the frame buffer
 * under it saved into SAVED (cursor_width x cursor_height), then its opaque pixels drawn. */
static void cursor_draw(xeen_presentation *p, uint8_t *saved)
{
    unsigned x, y, at;
    p->old_x = p->cursor_x;
    p->old_y = p->cursor_y;
    p->cursor_x = p->sampled_x;
    p->cursor_y = p->sampled_y;
    for (y = 0; y < p->cursor_height; ++y) {
        for (x = 0; x < p->cursor_width; ++x) {
            at = y * p->cursor_width + x;
            saved[at] = p->framebuffer[(p->cursor_y + y) * XEEN_PRESENT_WIDTH + p->cursor_x + x];
            if (p->cursor_opacity[at]) {
                p->framebuffer[(p->cursor_y + y) * XEEN_PRESENT_WIDTH + p->cursor_x + x] = p->cursor_pixels[at];
            }
        }
    }
}
/* The frame buffer under the cursor back from SAVED once the page holds the cursor. */
static void cursor_restore(xeen_presentation *p, const uint8_t *saved)
{
    unsigned y;
    for (y = 0; y < p->cursor_height; ++y) {
        memcpy(p->framebuffer + (p->cursor_y + y) * XEEN_PRESENT_WIDTH + p->cursor_x, saved + y * p->cursor_width,
               p->cursor_width);
    }
}
/* The flip's synchronisation and display of PAGE (4EF71, 4EA85). */
static void show_page(xeen_presentation *p, unsigned page)
{
    ++p->synchronization_count;
    p->displayed_page = (uint8_t)page;
}
/* 4EF71: old cursor rectangle, pointer sample, cursor draw, new cursor
 * rectangle, synchronisation and display of the current page, restore. */
static void cursor_and_flip(xeen_presentation *p)
{
    unsigned page = p->page_selector != 0;
    uint8_t saved[XEEN_PRESENT_PIXELS];
    log_op(p, XEEN_PRESENT_OP_FLIP, 0, 0, 0, 0);
    copy_rect(p, page, p->old_x, p->old_y, p->cursor_width, p->cursor_height);
    /* With the mouse off (16BE 0) 4E295 and 4E254 return at once:
     * the two rectangles are copied from the draw page, no cursor. */
    if (!p->enabled) {
        copy_rect(p, page, p->cursor_x, p->cursor_y, p->cursor_width, p->cursor_height);
        show_page(p, page);
        ++p->completed_frames;
        return;
    }
    cursor_draw(p, saved);
    copy_rect(p, page, p->cursor_x, p->cursor_y, p->cursor_width, p->cursor_height);
    show_page(p, page);
    cursor_restore(p, saved);
    ++p->completed_frames;
}
/**
 * Copies the whole frame buffer to the current page with the mouse cursor drawn on it and makes
 * that page the displayed one (the 4EA85 path; without the cursor when presentation is disabled).
 */
static void full_and_flip(xeen_presentation *p)
{
    unsigned page = p->page_selector != 0;
    uint8_t saved[XEEN_PRESENT_PIXELS];
    if (!p->enabled) { /* 4EA85 with 16BE 0: no cursor */
        log_op(p, XEEN_PRESENT_OP_FULL, 0, 0, 200, 320);
        memcpy(p->pages[page], p->framebuffer, XEEN_PRESENT_PIXELS);
        p->copied_pixels += XEEN_PRESENT_PIXELS;
        show_page(p, page);
        ++p->completed_frames;
        return;
    }
    cursor_draw(p, saved);
    log_op(p, XEEN_PRESENT_OP_FULL, 0, 0, 200, 320);
    memcpy(p->pages[page], p->framebuffer, XEEN_PRESENT_PIXELS);
    p->copied_pixels += XEEN_PRESENT_PIXELS;
    show_page(p, page);
    cursor_restore(p, saved);
    ++p->completed_frames;
}
/* 4E295 (from 4EA85 and 4EF87): the pointer is read for each cursor draw. */
static void sample_pointer(xeen_presentation *p)
{
    uint16_t x, y;
    if (!p->pointer || !p->enabled) {
        return; /* 4E295 reads INT 33h only with 16BE set */
    }
    x = p->sampled_x;
    y = p->sampled_y;
    p->pointer(p->pointer_user, &x, &y);
    if (rect_fits(x, y, p->cursor_width, p->cursor_height)) {
        p->sampled_x = x;
        p->sampled_y = y;
    }
}
void xeen_presentation_display(xeen_presentation *p, unsigned reason)
{
    if (!p || !p->display) {
        return;
    }
    ++p->displays;
    p->display(p->display_user, reason, p->pages[p->displayed_page], xeen_presentation_dac(p));
}
void xeen_presentation_display_window(xeen_presentation *p, uint16_t window, unsigned reason)
{
    uint16_t r[4];
    uint8_t *page;
    unsigned y;
    if (!p || !p->display || !p->framebuffer || p->framebuffer_size < XEEN_PRESENT_PIXELS ||
        !xeen_presentation_window(p, window, r) || r[0] + r[2] > XEEN_PRESENT_WIDTH || r[1] + r[3] > XEEN_PRESENT_HEIGHT ||
        !(page = malloc(XEEN_PRESENT_PIXELS))) {
        return;
    }
    memcpy(page, p->pages[p->displayed_page], XEEN_PRESENT_PIXELS);
    for (y = r[1]; y < (unsigned)r[1] + r[3]; ++y) {
        memcpy(page + y * XEEN_PRESENT_WIDTH + r[0], p->framebuffer + y * XEEN_PRESENT_WIDTH + r[0], r[2]);
    }
    ++p->displays;
    p->display(p->display_user, reason, page, xeen_presentation_dac(p));
    free(page);
}
const uint8_t *xeen_presentation_dac(xeen_presentation *p)
{
    unsigned i, level;
    if (!p) {
        return NULL;
    }
    if (!p->fade_dim) {
        return p->palette;
    }
    level = 0x80u - p->fade_dim;
    for (i = 0; i < 768u; ++i) {
        p->dac[i] = (uint8_t)(((unsigned)p->palette[i] * level * 2u) >> 8);
    }
    return p->dac;
}
void xeen_presentation_fade(xeen_presentation *p, int in, uint16_t step)
{
    /* LEFT is 4A76F's CX, the levels still to fade, from 80h (full) down to 0 by STEP; a fade in shows
     * 80h - LEFT, a fade out LEFT. */
    int left = 0x80;
    if (!p) {
        return;
    }
    ++p->fades_run;
    p->last_fade_in = (uint8_t)(in != 0);
    p->last_fade_step = step;
    ++p->fade_requests;
    xeen_presentation_trace("fade %u %u", (unsigned)(in != 0), (unsigned)step);
    for (;;) {
        int level = in ? 0x80 - left : left, write = 1;
        if (in && step == 0x81u) {
            step = 0x80u;
            write = 0;
        }
        if (write) { /* 4A78B..4A7A0: 4A7B6 after 4E90A */
            p->fade_dim = (uint8_t)(0x80 - (level & 0xff));
            xeen_presentation_display(p, XEEN_DISPLAY_RETRACE);
        }
        if (!left) {
            break;
        }
        left -= step;
        if (left < 0) {
            left = 0;
        }
    }
}
void xeen_presentation_mouse_off(xeen_presentation *p)
{
    if (!p || !p->enabled) {
        return;
    }
    p->enabled = 0;                           /* 4E23E INT 33h fn 0, 4E240 */
    xeen_presentation_trace_extra("mouse 0"); /* evidence (cursor_check.py) */
    if (!p->framebuffer || p->framebuffer_size < XEEN_PRESENT_PIXELS || !cursor_ok(p)) {
        return;
    }
    p->page_selector ^= XEEN_PRESENT_PAGE_2;
    p->op_count = 0;
    full_and_flip(p); /* 4E24D 4EA85 */
}
int xeen_presentation_select_cursor(xeen_presentation *p, uint16_t handle, uint16_t frame)
{
    int on;
    if (!p) {
        return 0;
    }
    on = !p->enabled; /* 4E077: 16BE 0 */
    if (on) {         /* 4E0AA 5C40 = 0, 4E0AE */
        p->enabled = 1;
        ++p->mouse_resets;
        xeen_presentation_trace_extra("mouse 1");
        if (p->mouse_reset) {
            p->mouse_reset(p->mouse_reset_user);
        }
    }
    p->cursor_handle = handle;
    p->cursor_frame = frame; /* 4E0B3.. */
    return on;
}
void xeen_presentation_restore_rect(xeen_presentation *p, uint16_t x, uint16_t y, uint16_t width,
                                    uint16_t height)
{
    unsigned row, page;
    if (!p || !p->framebuffer || p->framebuffer_size < XEEN_PRESENT_PIXELS ||
        !rect_fits(x, y, width, height)) {
        return;
    }
    for (page = 0; page < 2u; ++page) {
        for (row = 0; row < height; ++row) {
            memcpy(p->pages[page] + (y + row) * XEEN_PRESENT_WIDTH + x, p->framebuffer + (y + row) * XEEN_PRESENT_WIDTH + x,
                   width);
        }
    }
}
xeen_status xeen_presentation_full_copy(xeen_presentation *p)
{
    if (p) {
        sample_pointer(p);
    }
    if (!p || !cursor_ok(p)) {
        return XEEN_BAD_INPUT;
    }
    p->op_count = 0;
    full_and_flip(p);
    return XEEN_OK;
}
int xeen_presentation_window(const xeen_presentation *p, uint16_t window, uint16_t rect[4])
{
    const uint16_t *r;
    if (!p || !rect || !window_ok(p, window)) {
        return 0;
    }
    r = window_rect(p, window);
    rect[0] = r[0];
    rect[1] = r[1];
    rect[2] = r[2];
    rect[3] = r[3];
    return 1;
}
xeen_status xeen_presentation_define_window(xeen_presentation *p, uint16_t window, uint16_t x,
                                            uint16_t y, uint16_t height, uint16_t width)
{
    unsigned n;
    if (!p || (window != 28u && window != 39u) || !rect_fits(x, y, width, height)) {
        return XEEN_BAD_INPUT;
    }
    xeen_presentation_trace("define %u %u %u %u %u", window, x, y, width, height);
    n = window == 39u;
    p->runtime_windows[n][0] = x;
    p->runtime_windows[n][1] = y;
    p->runtime_windows[n][2] = width;
    p->runtime_windows[n][3] = height;
    p->runtime_window_valid[n] = 1;
    return XEEN_OK;
}
/* The pending-window word that marks WINDOW (XEEN_PRESENT_DIRTY_SCREEN for window 0, the whole screen). */
static uint16_t dirty_mark(uint16_t window)
{
    return (uint16_t)(window * 2u);
}
/*
 * Whether PENDING is a pending-window word the driver copies: nothing pending (FFFFh), the whole screen (0), or an
 * existing window's number times 2. An odd word, one from 8000h, or a window past the table or off the page is
 * not one the native models (the caller refuses with XEEN_SERVICE_REQUIRED).
 */
static int pending_copyable(const xeen_presentation *p, uint16_t pending)
{
    if (pending == XEEN_PRESENT_DIRTY_NONE || pending == XEEN_PRESENT_DIRTY_SCREEN) {
        return 1;
    }
    return !(pending & 1u) && pending < 0x8000u && window_ok(p, (uint16_t)(pending / 2u));
}
/* The start of a flip once it is accepted: the operation log restarts and drawing moves to the other page. */
static void begin_flip(xeen_presentation *p)
{
    p->op_count = 0;
    p->page_selector ^= XEEN_PRESENT_PAGE_2;
}
/* The window that PENDING (not the whole screen) marks, copied to the draw page unless nothing is pending. */
static void copy_pending(xeen_presentation *p, uint16_t pending)
{
    if (pending != XEEN_PRESENT_DIRTY_NONE) {
        copy_window(p, (uint16_t)(pending / 2u));
    }
}
/*
 * The present: with the mouse flag off nothing happens. Else the pointer is sampled; a decoder request or a pending
 * word the driver does not copy is refused (XEEN_SERVICE_REQUIRED), an unusable cursor too (XEEN_BAD_INPUT); then
 * the pending word is taken (left FFFFh) and drawn: the whole screen by the full copy, a window by its copy and the
 * cursor flip, nothing pending by the cursor flip alone.
 */
xeen_status xeen_presentation_present(xeen_presentation *p)
{
    uint16_t pending;
    if (!p) {
        return XEEN_BAD_INPUT;
    }
    if (!p->enabled) {
        return XEEN_OK;
    }
    sample_pointer(p);
    if (p->decoder != 0 || !pending_copyable(p, p->dirty)) {
        return XEEN_SERVICE_REQUIRED;
    }
    if (!cursor_ok(p)) {
        return XEEN_BAD_INPUT;
    }
    begin_flip(p);
    pending = p->dirty;
    p->dirty = XEEN_PRESENT_DIRTY_NONE;
    if (pending == XEEN_PRESENT_DIRTY_SCREEN) {
        full_and_flip(p);
        return XEEN_OK;
    }
    copy_pending(p, pending);
    cursor_and_flip(p);
    return XEEN_OK;
}
/*
 * The window update (50BF6): the trace line and the pointer sample first, then the refusals (all
 * XEEN_SERVICE_REQUIRED): WINDOW not an existing on-page window (window 0 is the whole screen), a pending word
 * the driver does not copy (tested only when the update is a window copy and the word names another window), an
 * unusable cursor. Accepted, the flip starts and WINDOW becomes the pending word. Window 0 or a pending whole
 * screen is drawn by the full copy (4EA85); otherwise the previously pending window (when it differs) and WINDOW
 * are copied, then the cursor flip (4EF71). The mouse flag (16BE) is not tested here: with it off the copies run
 * and only the cursor is left out.
 */
xeen_status xeen_presentation_update_window(xeen_presentation *p, uint16_t window)
{
    uint16_t pending, mark;
    int full_screen;
    if (!p) {
        return XEEN_BAD_INPUT;
    }
    xeen_presentation_trace("update %u", window);
    sample_pointer(p);
    if (window != XEEN_WINDOW_SCREEN && !window_ok(p, window)) {
        return XEEN_SERVICE_REQUIRED;
    }
    pending = p->dirty;
    mark = dirty_mark(window);
    full_screen = window == XEEN_WINDOW_SCREEN || pending == XEEN_PRESENT_DIRTY_SCREEN;
    if (!full_screen && pending != mark && !pending_copyable(p, pending)) {
        return XEEN_SERVICE_REQUIRED;
    }
    if (!cursor_ok(p)) {
        return XEEN_SERVICE_REQUIRED;
    }
    begin_flip(p);
    if (full_screen) {
        p->dirty = mark;
        full_and_flip(p);
        return XEEN_OK;
    }
    if (pending != mark) {
        p->dirty = mark;
        copy_pending(p, pending);
    }
    copy_window(p, window);
    cursor_and_flip(p);
    return XEEN_OK;
}
/* The effect RNG hook's call for entry I of a draw call: record C[I] of a scene list, else request D[I]. */
static xeen_status entry_rng(xeen_presentation *p, const xeen_draw_command *c, const xeen_sprite_draw *d, size_t i,
                             int check)
{
    if (c) {
        return p->effect_rng(p->effect_rng_user, c[i].handle, c[i].frame, c[i].scale, c[i].flags, check,
                             &p->effects[i]);
    }
    return p->effect_rng(p->effect_rng_user, d[i].handle, d[i].frame, d[i].scale, d[i].flags, check,
                         &p->effects[i]);
}
/* The effect RNG of a draw call's N entries, in the driver's order: every entry is checked first (nothing is
 * taken, and a refusal refuses the call), then every entry takes its draws. */
static xeen_status effect_rng(xeen_presentation *p, const xeen_draw_command *c,
                              const xeen_sprite_draw *d, size_t n)
{
    size_t i;
    int check;
    xeen_status s;
    memset(p->effects, 0, n * sizeof p->effects[0]);
    if (!p->effect_rng) {
        return XEEN_OK;
    }
    for (check = 1; check >= 0; --check) {
        for (i = 0; i < n; ++i) {
            s = entry_rng(p, c, d, i, check);
            if (s != XEEN_OK) {
                return s;
            }
        }
    }
    return XEEN_OK;
}
unsigned xeen_window_border(uint16_t window)
{
    return window == XEEN_WINDOW_SCREEN || window == XEEN_WINDOW_MINIMAP || window == XEEN_WINDOW_VIEW_FRAME ||
                           window == 7 || window == 8 || window == XEEN_WINDOW_DICE
                   ? 0u
                   : 8u;
}
/* 4ADB1's 4F1BE places a list's records at the window's origin, its rect plus the window's border
 * (xeen_window_border). Windows 0 and 3 have origin 0 and keep their own clip in the raster; another
 * window's list (17h, the panel's icons; 18h, the view's) is rastered as whole-screen draws at that
 * origin (no clip to the window, as the earlier per-record panel draws). The trace and the stored scene
 * keep the original records. */
static xeen_status raster_list(xeen_presentation *p, const xeen_draw_list *list)
{
    xeen_draw_list *at;
    const uint16_t *rect;
    unsigned dx, dy;
    size_t i;
    xeen_status s;
    if (!list->window || list->window == 3u) {
        return p->raster_scene(p->raster_user, list);
    }
    at = malloc(sizeof *at);
    if (!at) {
        return XEEN_BAD_INPUT;
    }
    if (list->window >= WINDOW_COUNT) {
        free(at);
        return XEEN_SERVICE_REQUIRED;
    }
    rect = window_rect(p, list->window);
    dx = rect[0] + xeen_window_border(list->window);
    dy = rect[1] + xeen_window_border(list->window);
    *at = *list;
    at->window = 0;
    for (i = 0; i < at->count; ++i) {
        at->commands[i].x = (uint16_t)(at->commands[i].x + dx);
        at->commands[i].y = (uint16_t)(at->commands[i].y + dy);
    }
    s = p->raster_scene(p->raster_user, at);
    free(at);
    return s;
}
/* The most view records a list's byte trace shows (the extra "listbytes" line). */
#define TRACE_LIST_RECORDS 256u
/* The draw trace of an accepted list: its header, with XEEN_DRAW_TRACE_EXTRA the list's DS bytes as 4ADB1 reads
 * them (every record up to the zero handle that ends it, for the byte comparison), then one line per drawn record. */
static void trace_list(const xeen_draw_list *list)
{
    size_t i;
    char name[8];
    if (!xeen_presentation_tracing()) {
        return;
    }
    xeen_presentation_trace("list %04x window %u count %u", list->list, list->window, (unsigned)list->count);
    if (xeen_presentation_tracing_extra() && trace_ds &&
        (uint32_t)list->list + XEEN_VIEW_RECORD_BYTES * list->records + 2u <= 0x10000u) {
        char hex[XEEN_VIEW_RECORD_BYTES * TRACE_LIST_RECORDS * 2 + 8];
        size_t k, n = XEEN_VIEW_RECORD_BYTES * list->records + 2u; /* the records and the end word */
        if (n > XEEN_VIEW_RECORD_BYTES * TRACE_LIST_RECORDS) {
            n = XEEN_VIEW_RECORD_BYTES * TRACE_LIST_RECORDS;
        }
        for (k = 0; k < n; ++k) {
            snprintf(hex + 2 * k, 3, "%02x", trace_ds[list->list + k]);
        }
        xeen_presentation_trace_extra("listbytes %04x %s", list->list, hex);
    }
    for (i = 0; i < list->count; ++i) {
        xeen_presentation_trace("item %s %u %u %u %u %04x", trace_name(list->commands[i].handle, name, sizeof name),
                                list->commands[i].frame, list->commands[i].x, list->commands[i].y,
                                list->commands[i].scale, list->commands[i].flags);
    }
}
/* Accepts scene list LIST (4ADB1): its effect RNG, the raster's draws (when one is attached), then the stored scene
 * and the trace. A refusal at any stage leaves the stored scene and the submission count as they were. */
xeen_status xeen_presentation_submit(xeen_presentation *p, const xeen_draw_list *list)
{
    xeen_status s;
    if (!p || !list || list->count > XEEN_DRAW_LIST_MAX) {
        return XEEN_BAD_INPUT;
    }
    if ((s = effect_rng(p, list->commands, NULL, list->count)) != XEEN_OK) {
        return s;
    }
    if (p->raster_scene && (s = raster_list(p, list)) != XEEN_OK) {
        return s;
    }
    p->scene = *list;
    ++p->scene_submissions;
    trace_list(list);
    return XEEN_OK;
}
/* The draw trace of accepted single draws: one line per request. */
static void trace_sprites(const xeen_sprite_draw *d, size_t n)
{
    size_t i;
    char name[8];
    if (!xeen_presentation_tracing()) {
        return;
    }
    for (i = 0; i < n; ++i) {
        xeen_presentation_trace("sprite %s %u %u %u %u %04x %u", trace_name(d[i].handle, name, sizeof name),
                                d[i].frame, d[i].x, d[i].y, d[i].scale, d[i].flags, d[i].window);
    }
}
/* Accepts N single draws D (4AC92): their effect RNG, the raster's draws (when one is attached), then the stored
 * requests and the trace. */
xeen_status xeen_presentation_draw_sprites(xeen_presentation *p, const xeen_sprite_draw *d,
                                           size_t n)
{
    xeen_status s;
    if (!p || (!d && n) || n > XEEN_SPRITE_DRAW_MAX) {
        return XEEN_BAD_INPUT;
    }
    if ((s = effect_rng(p, NULL, d, n)) != XEEN_OK) {
        return s;
    }
    if (p->raster_sprites && (s = p->raster_sprites(p->raster_user, d, n)) != XEEN_OK) {
        return s;
    }
    if (n) {
        memcpy(p->sprites, d, n * sizeof *d);
    }
    p->sprite_count = n;
    ++p->sprite_submissions;
    trace_sprites(d, n);
    return XEEN_OK;
}
xeen_status xeen_presentation_darken(xeen_presentation *p, uint16_t level)
{
    unsigned row, col;
    if (!p) {
        return XEEN_BAD_INPUT;
    }
    if (level >= 4u) {
        return XEEN_OK;
    }
    if (!p->framebuffer || p->framebuffer_size < XEEN_PRESENT_PIXELS) {
        return XEEN_SERVICE_REQUIRED;
    }
    for (row = 0; row < 132u; ++row) {
        uint8_t *line = p->framebuffer + (8u + row) * XEEN_PRESENT_WIDTH + 8u;
        if (!level) {
            memset(line, 0, 216u);
        } else {
            for (col = 0; col < 216u; ++col) {
                line[col] = darken_tables[level - 1u][line[col]];
            }
        }
    }
    return XEEN_OK;
}
xeen_status xeen_presentation_text(xeen_presentation *p, uint16_t window, const char *text)
{
    size_t n, slot;
    if (!p || !text || (n = strlen(text)) >= sizeof p->texts[0]) {
        return XEEN_BAD_INPUT;
    }
    if (p->text_count == XEEN_PRESENT_TEXTS) { /* keep the latest entries */
        memmove(p->texts, p->texts + 1, sizeof p->texts - sizeof p->texts[0]);
        memmove(p->text_windows, p->text_windows + 1,
                sizeof p->text_windows - sizeof p->text_windows[0]);
        slot = XEEN_PRESENT_TEXTS - 1u;
    } else {
        slot = p->text_count++;
    }
    memcpy(p->texts[slot], text, n + 1u);
    p->text_windows[slot] = window;
    ++p->texts_total;
    return XEEN_OK;
}
/* 4F6F8(window): 4F1BE, then 4F24A's border tiles (CS:72D2) around the
 * window into the draw page; the interior is kept. 26306's tail
 * (26740..26754) calls it for an open window 0Ch (9878), else 28h (9894),
 * after the border sprites, so the bottom window's frame covers the corner
 * gems and the two-sword bar on every scene. */
xeen_status xeen_presentation_frame(xeen_presentation *p, uint16_t window)
{
    uint16_t r[4];
    if (!p) {
        return XEEN_BAD_INPUT;
    }
    p->last_frame_window = window;
    ++p->frames_total;
    if (p->framebuffer && p->framebuffer_size >= XEEN_PRESENT_PIXELS &&
        xeen_presentation_window(p, window, r)) {
        (void)xeen_text_border(p->framebuffer, p->framebuffer_size, r);
    }
    return XEEN_OK;
}
xeen_status xeen_presentation_port(xeen_presentation *p, uint16_t port, uint8_t value)
{
    if (!p) {
        return XEEN_BAD_INPUT;
    }
    if (port == 0x3c8) {
        p->dac_index = value;
        p->dac_component = 0;
    } else if (port == 0x3c9) {
        p->palette[(p->dac_index % 256u) * 3u + p->dac_component] = value;
        if (++p->dac_component == 3u) {
            p->dac_component = 0;
            p->dac_index = (uint16_t)((p->dac_index + 1u) % 256u);
        }
    } else {
        return XEEN_BAD_INPUT;
    }
    ++p->dac_writes;
    return XEEN_OK;
}
