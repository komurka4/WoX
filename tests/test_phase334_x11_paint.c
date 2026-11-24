/* SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * Phase 334 (tracked from Phase 332's evidence kit xeen_dat_evidence/phase332/h/paint_harness.c, itself from Phase
 * 330's fake Display): platform.c's X11 paint under a MOCK Xlib. The real unit is #included (SIDE_UNIT, default
 * ../src/platform/platform.c) with every Xlib call it makes replaced by a logging mock; paint() is called directly on
 * an opened window with set pages, palettes and window sizes. Covered: the window image's lifetime (kept for the same
 * size, rebuilt otherwise), the palette's 6-to-8-bit conversion into the visual's channel masks, the scaled page, the
 * direct 32 bpp LSB path and the XPutPixel path (32 bpp MSB, 16 bpp 565, a 30-bit 10:10:10 visual), three allocation
 * faults (XCreateImage NULL, its data's calloc, the column table's malloc) with the resources balanced after close,
 * and [display] locked-scale's geometry and minimum-size hints (Phase 332 L).
 *
 * Limits: no X server, window manager or real visual is involved; the mock XCreateImage chooses the image layout, so
 * this proves the paint's arithmetic and resource handling, not what a real display shows (AGENTS.md section 5: no
 * real window is opened). Event handling (XNextEvent and the key lookups) is outside: reaching it fails the test.
 *
 * usage: xeen_phase334_x11_paint_tests EXPECTED   compare the two transcripts' digests with EXPECTED
 *        xeen_phase334_x11_paint_tests dump       print both transcripts and their digest lines
 * EXPECTED (tests/data/phase334_x11_paint.txt) holds `paint LINES DIGEST` and `locked LINES DIGEST`; the paint
 * transcript equals the Phase 332 kit's base-vs-new reference (60 paints over four visuals).
 */
#define _POSIX_C_SOURCE 200809L
#define XEEN_HAVE_X11 1
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define XLIB_ILLEGAL_ACCESS 1
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
static FILE *transcript;
#define LOG(...) fprintf(transcript, __VA_ARGS__)
static int fail_calloc, fail_malloc, fail_display, n_calloc, n_malloc;
static long live_heap, live_window, live_gc, live_cursor, live_pixmap, live_display;
static unsigned long next_id = 0x100;
static void *heap[64];
static int heap_n;
static void heap_add(void *p) { if (p && heap_n < 64) heap[heap_n++] = p, ++live_heap; }
static void *mock_calloc(size_t a, size_t b)
{
    void *p;
    if (++n_calloc == fail_calloc) { LOG("calloc(%zu,%zu)=FAIL\n", a, b); return NULL; }
    p = calloc(a, b); heap_add(p); LOG("calloc(%zu,%zu)\n", a, b); return p;
}
static void *mock_malloc(size_t a)
{
    void *p;
    if (++n_malloc == fail_malloc) { LOG("malloc(%zu)=FAIL\n", a); return NULL; }
    p = malloc(a); heap_add(p); LOG("malloc(%zu)\n", a); return p;
}
static void mock_free(void *p)
{
    int i;
    if (!p) { LOG("free(NULL)\n"); return; }
    for (i = 0; i < heap_n; ++i) if (heap[i] == p) { heap[i] = heap[--heap_n]; --live_heap; break; }
    LOG("free\n");
    free(p);
}
static struct { struct _XDisplay display; Screen screen; } fake;
static int fake_screen_w, fake_screen_h;
Display *mock_XOpenDisplay(_Xconst char *n)
{
    (void)n;
    if (fail_display) { LOG("XOpenDisplay=NULL\n"); return NULL; }
    memset(&fake, 0, sizeof fake);
    fake.screen.root = 0x42;
    fake.screen.width = fake_screen_w; /* Phase 335: 0 (unknown: the scale is kept) unless a test sets it */
    fake.screen.height = fake_screen_h;
    fake.display.screens = &fake.screen;
    fake.display.nscreens = 1;
    fake.display.default_screen = 0;
    ++live_display; LOG("XOpenDisplay\n");
    return (Display *)&fake.display;
}
int mock_XCloseDisplay(Display *d) { (void)d; --live_display; LOG("XCloseDisplay\n"); return 0; }
Window mock_XCreateSimpleWindow(Display *d, Window p, int x, int y, unsigned w, unsigned h, unsigned b, unsigned long f, unsigned long g)
{ (void)d; (void)b; (void)f; (void)g; ++live_window; LOG("XCreateSimpleWindow(root %lx,%d,%d,%u,%u)=%lx\n", p, x, y, w, h, next_id); return next_id++; }
int mock_XDestroyWindow(Display *d, Window w) { (void)d; --live_window; LOG("XDestroyWindow(%lx)\n", w); return 1; }
GC mock_XCreateGC(Display *d, Drawable w, unsigned long m, XGCValues *v) { (void)d; (void)v; ++live_gc; LOG("XCreateGC(%lx,%lu)\n", w, m); return (GC)(uintptr_t)(next_id++); }
int mock_XFreeGC(Display *d, GC g) { (void)d; (void)g; --live_gc; LOG("XFreeGC\n"); return 1; }
Atom mock_XInternAtom(Display *d, _Xconst char *n, Bool o) { (void)d; LOG("XInternAtom(%s,%d)\n", n, o); return 100 + (Atom)strlen(n); }
Status mock_XSetWMProtocols(Display *d, Window w, Atom *a, int n) { (void)d; LOG("XSetWMProtocols(%lx,%lu,%d)\n", w, a[0], n); return 1; }
Pixmap mock_XCreateBitmapFromData(Display *d, Drawable w, _Xconst char *b, unsigned x, unsigned y) { (void)d; (void)b; ++live_pixmap; LOG("XCreateBitmapFromData(%lx,%u,%u)=%lx\n", w, x, y, next_id); return next_id++; }
int mock_XFreePixmap(Display *d, Pixmap x) { (void)d; --live_pixmap; LOG("XFreePixmap(%lx)\n", x); return 1; }
Cursor mock_XCreatePixmapCursor(Display *d, Pixmap s, Pixmap m, XColor *f, XColor *b, unsigned x, unsigned y)
{ (void)d; (void)f; (void)b; ++live_cursor; LOG("XCreatePixmapCursor(%lx,%lx,%u,%u)=%lx\n", s, m, x, y, next_id); return next_id++; }
int mock_XFreeCursor(Display *d, Cursor c) { (void)d; --live_cursor; LOG("XFreeCursor(%lx)\n", c); return 1; }
int mock_XDefineCursor(Display *d, Window w, Cursor c) { (void)d; LOG("XDefineCursor(%lx,%lx)\n", w, c); return 1; }
int mock_XUndefineCursor(Display *d, Window w) { (void)d; LOG("XUndefineCursor(%lx)\n", w); return 1; }
int mock_XStoreName(Display *d, Window w, _Xconst char *n) { (void)d; LOG("XStoreName(%lx,%s)\n", w, n); return 1; }
int mock_XSetIconName(Display *d, Window w, _Xconst char *n) { (void)d; LOG("XSetIconName(%lx,%s)\n", w, n); return 1; }
int mock_XChangeProperty(Display *d, Window w, Atom p, Atom t, int f, int m, _Xconst unsigned char *data, int n)
{ (void)d; (void)data; LOG("XChangeProperty(%lx,%lu,%lu,%d,%d,%d)\n", w, p, t, f, m, n); return 1; }
int mock_XSelectInput(Display *d, Window w, long m) { (void)d; LOG("XSelectInput(%lx,%lx)\n", w, m); return 1; }
Bool mock_XkbSetDetectableAutoRepeat(Display *d, Bool b, Bool *s) { (void)d; (void)s; LOG("XkbSetDetectableAutoRepeat(%d)\n", b); return True; }
int mock_XMapWindow(Display *d, Window w) { (void)d; LOG("XMapWindow(%lx)\n", w); return 1; }
int mock_XFlush(Display *d) { (void)d; LOG("XFlush\n"); return 1; }

static int visual_mode, fail_image;
static unsigned long fnv(const unsigned char *b, size_t n)
{
    unsigned long h = 1469598103934665603ul;
    while (n--) h = (h ^ *b++) * 1099511628211ul;
    return h;
}
static int put_pixel(XImage *im, int x, int y, unsigned long v)
{
    unsigned char *row = (unsigned char *)im->data + (size_t)y * (size_t)im->bytes_per_line;
    int bytes = im->bits_per_pixel / 8, k;
    for (k = 0; k < bytes; ++k) {
        int shift = im->byte_order == LSBFirst ? 8 * k : 8 * (bytes - 1 - k);
        row[x * bytes + k] = (unsigned char)(v >> shift);
    }
    return 1;
}
/* XDestroyImage frees the image's data, which the unit allocated through the mock calloc. */
static int destroy_image(XImage *im)
{
    int i;
    LOG("XDestroyImage(%dx%d)\n", im->width, im->height);
    for (i = 0; i < heap_n; ++i) {
        if (heap[i] == im->data) {
            heap[i] = heap[--heap_n];
            --live_heap;
            break;
        }
    }
    free(im->data);
    free(im);
    return 1;
}
XImage *mock_XCreateImage(Display *d, Visual *v, unsigned dp, int f, int o, char *data, unsigned w, unsigned h, int pad, int bpl)
{
    XImage *im;
    (void)d; (void)v; (void)data; (void)bpl;
    LOG("XCreateImage(depth %u, format %d, offset %d, %ux%u, pad %d)%s\n", dp, f, o, w, h, pad, fail_image ? "=NULL" : "");
    if (fail_image) return NULL;
    im = calloc(1, sizeof *im);
    im->width = (int)w; im->height = (int)h; im->format = f;
    im->byte_order = visual_mode == 1 ? MSBFirst : LSBFirst;
    im->bits_per_pixel = visual_mode == 2 ? 16 : 32;
    im->bytes_per_line = (int)w * (im->bits_per_pixel / 8);
    im->red_mask = visual_mode == 2 ? 0xf800ul : visual_mode == 3 ? 0x3ff00000ul : 0xff0000ul;
    im->green_mask = visual_mode == 2 ? 0x07e0ul : visual_mode == 3 ? 0x000ffc00ul : 0x00ff00ul;
    im->blue_mask = visual_mode == 2 ? 0x001ful : visual_mode == 3 ? 0x000003fful : 0x0000fful;
    im->f.put_pixel = put_pixel;
    im->f.destroy_image = destroy_image;
    return im;
}
static int bbox[4];
int mock_XPutImage(Display *d, Drawable w, GC g, XImage *im, int a, int b, int c, int e, unsigned x, unsigned y)
{
    int bpp = im->bits_per_pixel / 8, x0 = im->width, y0 = im->height, x1 = -1, y1 = -1, px, py, k;
    (void)d; (void)g;
    for (py = 0; py < im->height; ++py)          /* the painted picture: the extent of nonzero pixels */
        for (px = 0; px < im->width; ++px)
            for (k = 0; k < bpp; ++k)
                if (im->data[(size_t)py * (size_t)im->bytes_per_line + (size_t)(px * bpp + k)]) {
                    x0 = px < x0 ? px : x0; y0 = py < y0 ? py : y0; x1 = px > x1 ? px : x1; y1 = py > y1 ? py : y1;
                    break;
                }
    bbox[0] = x0; bbox[1] = y0; bbox[2] = x1; bbox[3] = y1;
    LOG("XPutImage(%lx, %d,%d,%d,%d, %ux%u) image %016lx picture %d,%d..%d,%d\n", w, a, b, c, e, x, y,
        fnv((const unsigned char *)im->data, (size_t)im->bytes_per_line * (size_t)im->height), x0, y0, x1, y1);
    return 1;
}
static int hints_w, hints_h;
int mock_XSetWMNormalHints(Display *d, Window w, XSizeHints *h)
{
    (void)d;
    hints_w = h->flags & PMinSize ? h->min_width : 0; hints_h = h->flags & PMinSize ? h->min_height : 0;
    LOG("XSetWMNormalHints(%lx, flags %lx, min %dx%d)\n", w, (unsigned long)h->flags, hints_w, hints_h);
    return 1;
}
#define UNREACHED(name) do { LOG("FAIL: %s reached\n", name); exit(3); } while (0)
#if defined(X11_MOCK_EVENTS)
/* Phase 335: an event queue for tests/test_phase335_x11_boss_wait.c, which #includes this file; a key event's
 * keycode is its keysym. This test itself is built without it: reaching an event call fails it. */
static XEvent mock_events[64];
static int mock_head, mock_tail;
static void mock_push(const XEvent *e) { if (mock_tail < 64) mock_events[mock_tail++] = *e; }
KeySym mock_XLookupKeysym(XKeyEvent *e, int i) { (void)i; return (KeySym)e->keycode; }
int mock_XLookupString(XKeyEvent *e, char *b, int n, KeySym *s, XComposeStatus *c)
{
    (void)c;
    if (s) *s = (KeySym)e->keycode;
    if (n > 0 && e->keycode < 0x80u) { b[0] = (char)e->keycode; return 1; }
    return 0;
}
int mock_XNextEvent(Display *d, XEvent *e) { (void)d; if (mock_head < mock_tail) *e = mock_events[mock_head++]; return 0; }
int mock_XPending(Display *d) { (void)d; return mock_tail - mock_head; }
int mock_XPeekEvent(Display *d, XEvent *e) { (void)d; if (mock_head < mock_tail) *e = mock_events[mock_head]; return 0; }
int mock_XEventsQueued(Display *d, int m) { (void)d; (void)m; return mock_tail - mock_head; }
#else
KeySym mock_XLookupKeysym(XKeyEvent *e, int i) { (void)e; (void)i; UNREACHED("XLookupKeysym"); }
int mock_XLookupString(XKeyEvent *e, char *b, int n, KeySym *s, XComposeStatus *c) { (void)e; (void)b; (void)n; (void)s; (void)c; UNREACHED("XLookupString"); }
int mock_XNextEvent(Display *d, XEvent *e) { (void)d; (void)e; UNREACHED("XNextEvent"); }
int mock_XPending(Display *d) { (void)d; UNREACHED("XPending"); }
int mock_XPeekEvent(Display *d, XEvent *e) { (void)d; (void)e; UNREACHED("XPeekEvent"); }
int mock_XEventsQueued(Display *d, int m) { (void)d; (void)m; UNREACHED("XEventsQueued"); }
#endif
Status mock_XSendEvent(Display *d, Window w, Bool p, long m, XEvent *e) { (void)d; (void)w; (void)p; (void)m; (void)e; UNREACHED("XSendEvent"); }
int mock_XResizeWindow(Display *d, Window w, unsigned a, unsigned b) { (void)d; LOG("XResizeWindow(%lx,%u,%u)\n", w, a, b); return 1; }
#define XOpenDisplay mock_XOpenDisplay
#define XCloseDisplay mock_XCloseDisplay
#define XCreateSimpleWindow mock_XCreateSimpleWindow
#define XDestroyWindow mock_XDestroyWindow
#define XCreateGC mock_XCreateGC
#define XFreeGC mock_XFreeGC
#define XInternAtom mock_XInternAtom
#define XSetWMProtocols mock_XSetWMProtocols
#define XCreateBitmapFromData mock_XCreateBitmapFromData
#define XFreePixmap mock_XFreePixmap
#define XCreatePixmapCursor mock_XCreatePixmapCursor
#define XFreeCursor mock_XFreeCursor
#define XDefineCursor mock_XDefineCursor
#define XUndefineCursor mock_XUndefineCursor
#define XStoreName mock_XStoreName
#define XSetIconName mock_XSetIconName
#define XChangeProperty mock_XChangeProperty
#define XSelectInput mock_XSelectInput
#define XkbSetDetectableAutoRepeat mock_XkbSetDetectableAutoRepeat
#define XMapWindow mock_XMapWindow
#define XFlush mock_XFlush
#define XLookupKeysym mock_XLookupKeysym
#define XLookupString mock_XLookupString
#define XNextEvent mock_XNextEvent
#define XSendEvent mock_XSendEvent
#define XResizeWindow mock_XResizeWindow
#define XPutImage mock_XPutImage
#define XPending mock_XPending
#define XPeekEvent mock_XPeekEvent
#define XEventsQueued mock_XEventsQueued
#define XCreateImage mock_XCreateImage
#define XSetWMNormalHints mock_XSetWMNormalHints
#define calloc mock_calloc
#define malloc mock_malloc
#define free mock_free
#ifndef SIDE_UNIT
#define SIDE_UNIT "../src/platform/platform.c"
#endif
#include SIDE_UNIT
#undef calloc
#undef malloc
#undef free

static unsigned long seed = 332;
static unsigned rnd(void) { seed = seed * 6364136223846793005ul + 1442695040888963407ul; return (unsigned)(seed >> 33); }
static void paint_case(xeen_platform *p, unsigned w, unsigned h, int boss, int fi, int fc, int fm)
{
    unsigned i;
    p->width = w; p->height = h; p->boss = boss;
    for (i = 0; i < sizeof p->pixels; ++i) p->pixels[i] = (uint8_t)rnd();
    for (i = 0; i < sizeof p->boss_pixels; ++i) p->boss_pixels[i] = (uint8_t)rnd();
    for (i = 0; i < sizeof p->palette; ++i) p->palette[i] = (uint8_t)(rnd() % 80u);
    for (i = 0; i < sizeof p->boss_palette; ++i) p->boss_palette[i] = (uint8_t)(rnd() % 64u);
    fail_image = fi; n_calloc = n_malloc = 0; fail_calloc = fc; fail_malloc = fm;
    LOG("-- paint %ux%u boss %d visual %d (fail image %d calloc #%d malloc #%d)\n", w, h, boss, visual_mode, fi, fc, fm);
    paint(p);
    LOG("painted %d,%d image %s\n", p->painted_x, p->painted_y, p->image ? "kept" : "none");
    fail_image = 0; fail_calloc = fail_malloc = 0;
}
/* The current unit only (the base has no lock): [display] locked-scale's geometry and minimum size. */
static int fails;
static void expect_box(const char *what, int x0, int y0, int x1, int y1)
{
    int ok = bbox[0] == x0 && bbox[1] == y0 && bbox[2] == x1 && bbox[3] == y1;
    LOG("%s %s: picture %d,%d..%d,%d (want %d,%d..%d,%d)\n", ok ? "PASS" : "FAIL", what, bbox[0], bbox[1], bbox[2],
        bbox[3], x0, y0, x1, y1);
    fails += !ok;
}
static void fill(xeen_platform *p)
{
    memset(p->pixels, 7, sizeof p->pixels);      /* every pixel visible: colour 7 */
    memset(p->palette, 40, sizeof p->palette);
    p->boss = 0;
}
static int run_locked(void)
{
    char error[160];
    xeen_platform *p;
    visual_mode = 0;
    p = xeen_platform_open("WoX", 2, 0, error, sizeof error);
    if (!p) return 1;
    fill(p);
    p->width = 1280; p->height = 800; paint(p);
    expect_box("unlocked 1280x800 at scale 2 shows 4x", 0, 0, 1279, 799);
    xeen_platform_set_locked_scale(p, 1);
    fails += !(hints_w == 640 && hints_h == 400);
    LOG("%s minimum %dx%d\n", hints_w == 640 && hints_h == 400 ? "PASS" : "FAIL", hints_w, hints_h);
    paint(p);
    expect_box("locked 1280x800 shows 2x centred", 320, 200, 959, 599);
    p->width = 1920; p->height = 1080; paint(p);
    expect_box("locked fullscreen 1920x1080 shows 2x centred", 640, 340, 1279, 739);
    p->width = 960; p->height = 600; paint(p);
    expect_box("locked 960x600 (one step above the lock) shows 2x centred", 160, 100, 799, 499);
    p->width = 400; p->height = 300; paint(p);
    expect_box("locked, a smaller screen fits (1x)", 40, 50, 359, 249);
    xeen_platform_set_aspect(p, 1);
    fails += !(hints_w == 640 && hints_h == 480);
    LOG("%s aspect minimum %dx%d\n", hints_w == 640 && hints_h == 480 ? "PASS" : "FAIL", hints_w, hints_h);
    p->width = 1280; p->height = 1000; paint(p);
    expect_box("locked with aspect 1280x1000 shows 640x480 centred", 320, 260, 959, 739);
    xeen_platform_set_locked_scale(p, 0);
    fails += !(hints_w == 0 && hints_h == 0);
    paint(p);
    expect_box("unlocked again 1280x1000 (aspect) shows 4x", 0, 20, 1279, 979);
    xeen_platform_close(p);
    LOG("locked: %d failures\n", fails);
    return fails;
}

static int run_paint(void)
{
    static const unsigned sizes[][2] = {{320, 200}, {640, 400}, {960, 600}, {333, 211}, {640, 480}, {1, 1}, {1200, 150}};
    char error[160];
    xeen_platform *p;
    unsigned s;
    int unbalanced = 0;
    for (visual_mode = 0; visual_mode < 4; ++visual_mode) {
        p = xeen_platform_open("WoX", 2, 0, error, sizeof error);
        if (!p) { LOG("open failed: %s\n", error); return 1; }
        for (s = 0; s < sizeof sizes / sizeof sizes[0]; ++s) {
            paint_case(p, sizes[s][0], sizes[s][1], 0, 0, 0, 0);
            paint_case(p, sizes[s][0], sizes[s][1], 1, 0, 0, 0); /* the same size: the image kept */
        }
        paint_case(p, 500, 300, 0, 1, 0, 0);                /* XCreateImage fails */
        paint_case(p, 500, 300, 0, 0, 1, 0);                /* its data's calloc fails */
        paint_case(p, 500, 300, 0, 0, 0, 1);                /* the column table's malloc fails */
        paint_case(p, 500, 300, 0, 0, 0, 0);
        xeen_platform_close(p);
        LOG("balance: heap %ld window %ld gc %ld cursor %ld pixmap %ld display %ld\n", live_heap, live_window, live_gc,
            live_cursor, live_pixmap, live_display);
        if (live_heap || live_window || live_gc || live_cursor || live_pixmap || live_display) {
            unbalanced = 1;
        }
        live_heap = live_window = live_gc = live_cursor = live_pixmap = live_display = 0; heap_n = 0;
    }
    return unbalanced;
}

/* Runs one transcript into memory; *TEXT (owned by the caller) and *SIZE receive it, the return the case's failures. */
static int capture(int (*run)(void), char **text, size_t *size)
{
    int fails;
    transcript = open_memstream(text, size);
    if (!transcript) {
        return -1;
    }
    fails = run();
    fclose(transcript);
    transcript = NULL;
    return fails;
}
static unsigned long count_lines(const char *t, size_t n)
{
    unsigned long lines = 0;
    while (n--) {
        lines += *t++ == '\n';
    }
    return lines;
}
#ifndef PHASE334_MAIN
#define PHASE334_MAIN main
#endif
int PHASE334_MAIN(int argc, char **argv)
{
    static const struct { const char *name; int (*run)(void); } runs[2] = {{"paint", run_paint}, {"locked", run_locked}};
    char got[2][96], want[2][160], line[160];
    int dump = argc == 2 && !strcmp(argv[1], "dump"), bad = 0, i;
    FILE *f;
    if (argc != 2) {
        fprintf(stderr, "usage: %s EXPECTED|dump\n", argv[0]);
        return 2;
    }
    for (i = 0; i < 2; ++i) {
        char *text = NULL;
        size_t size = 0;
        int fails = capture(runs[i].run, &text, &size);
        if (fails < 0) {
            return 2;
        }
        snprintf(got[i], sizeof got[i], "%s %lu %016lx", runs[i].name, count_lines(text, size),
                 fnv((const unsigned char *)text, size));
        if (dump) {
            fwrite(text, 1, size, stdout);
        }
        if (fails) {
            printf("FAIL %s: %d failed checks or unbalanced resources\n", runs[i].name, fails);
            if (!dump) {
                fwrite(text, 1, size, stdout);
            }
            bad = 1;
        }
        free(text);
    }
    if (dump) {
        printf("%s\n%s\n", got[0], got[1]);
        return bad;
    }
    if (!(f = fopen(argv[1], "r"))) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    for (i = 0; i < 2; ++i) {
        do {
            if (!fgets(line, sizeof line, f)) {
                fprintf(stderr, "%s: missing line %d\n", argv[1], i + 1);
                fclose(f);
                return 2;
            }
        } while (line[0] == '#');
        line[strcspn(line, "\n")] = 0;
        snprintf(want[i], sizeof want[i], "%s", line);
        if (strcmp(got[i], want[i])) {
            printf("FAIL transcript %s: got \"%s\", expected \"%s\"\n", runs[i].name, got[i], want[i]);
            bad = 1;
        } else {
            printf("PASS %s\n", got[i]);
        }
    }
    fclose(f);
    return bad;
}
