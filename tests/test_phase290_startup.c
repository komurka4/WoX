/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Phase 290 (owner-approved host start-up, not original behaviour): the window's SC-55 start-up.
 *
 *   lifecycle            the wait's decisions in order (a controllable boot latch and close request; no emulator)
 *   frame DOS [PPM]      the loading frame from XEEN.CC's fnt and mm4.pal: exact text, centring, colours, palette
 *   boot DOS ROMS        the SC-55 engine booted on the worker against the open's own boot, offline: the driver
 *                        messages and the PCM of songs on sc55, wave and tap10 identical; a failed boot (no ROMs)
 *                        reported as before and never retried; boot/close cycles; other systems start no worker
 *
 * The lifecycle checks are start-up order only; the audio identity is the boot mode's. */
#include "app/host_startup.h"
#include "audio/audio_archive.h"
#include "xeen/audio_engine.h"
#include "xeen/platform.h"
#include "xeen/text_window.h"
#define SDL_MAIN_HANDLED /* the test's own main (MinGW) */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Phase 334: a text wider than the page (199 W's). */
static const char *wide_text(void)
{
    static char wide[200];
    memset(wide, 'W', sizeof wide - 1);
    wide[sizeof wide - 1] = 0;
    return wide;
}

/* the audio engine's archive adapter (audio_engine.c) */
/* struct CC is audio/audio_archive.h's (Phase 308). */
int cc_read(CC *cc, const char *name, Blob *out);

static int failures;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

static uint8_t *readfile(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    long end;
    uint8_t *p;
    if (!f || fseek(f, 0, SEEK_END) || (end = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
        if (f) {
            fclose(f);
        }
        return NULL;
    }
    *n = (size_t)end;
    p = malloc(*n + 1);
    if (p && fread(p, 1, *n, f) != *n) {
        free(p);
        p = NULL;
    }
    fclose(f);
    return p;
}
static uint8_t *xeen_bytes, *dark_bytes, *intro_bytes;
static xeen_archive xeen_cc, dark_cc, intro_cc;
static int open_archives(const char *dos)
{
    char path[1024];
    size_t n;
    snprintf(path, sizeof path, "%s/XEEN.CC", dos);
    if (!(xeen_bytes = readfile(path, &n)) || xeen_archive_open(&xeen_cc, xeen_bytes, n) != XR_OK) {
        return 0;
    }
    snprintf(path, sizeof path, "%s/DARK.CC", dos);
    if (!(dark_bytes = readfile(path, &n)) || xeen_archive_open(&dark_cc, dark_bytes, n) != XR_OK) {
        return 0;
    }
    snprintf(path, sizeof path, "%s/INTRO.CC", dos);
    if (!(intro_bytes = readfile(path, &n)) || xeen_archive_open(&intro_cc, intro_bytes, n) != XR_OK) {
        return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------- lifecycle --- */
/* A controllable latch: the boot finishes at poll READY_AT, the window is closed at poll CLOSE_AT (0: never). */
typedef struct {
    unsigned polls, ready_at, close_at, pumps, waits, refreshes;
    int with_refresh;
    char trace[512];
} fake;
static void note(fake *f, const char *what)
{
    size_t k = strlen(f->trace);
    snprintf(f->trace + k, sizeof f->trace - k, "%s%s", k ? " " : "", what);
}
static int fake_finished(void *u)
{
    fake *f = u;
    return f->ready_at && f->polls >= f->ready_at;
}
static int fake_close(void *u)
{
    fake *f = u;
    return f->close_at && f->polls >= f->close_at;
}
static void fake_pump(void *u)
{
    fake *f = u;
    ++f->pumps;
    ++f->polls;
}
static void fake_wait(void *u, unsigned ms)
{
    fake *f = u;
    CHECK(ms == HOST_STARTUP_WAIT_MS);
    ++f->waits;
}
static void fake_trace(void *u, const char *what)
{
    note(u, what);
}
static void fake_refresh(void *u)
{
    fake *f = u;
    ++f->refreshes;
    note(f, "(refresh)");
}
static host_startup_outcome run_fake(fake *f)
{
    host_startup_wait_ops o = {f, fake_finished, fake_close, fake_pump, fake_wait, fake_trace,
                               f->with_refresh ? fake_refresh : NULL};
    return host_startup_wait(&o);
}
static int lifecycle(void)
{
    fake f;
    /* the boot finishes at the third pump: two waits, every pass pumps first */
    memset(&f, 0, sizeof f);
    f.ready_at = 3;
    CHECK(run_fake(&f) == HOST_STARTUP_READY);
    CHECK(!strcmp(f.trace, "pump wait pump wait pump ready"));
    CHECK(f.pumps == 3 && f.waits == 2);
    /* already finished: no wait at all */
    memset(&f, 0, sizeof f);
    f.ready_at = 1;
    CHECK(run_fake(&f) == HOST_STARTUP_READY && !strcmp(f.trace, "pump ready"));
    /* closed during the boot: cancelled at the pump that saw it, the boot not consulted after */
    memset(&f, 0, sizeof f);
    f.ready_at = 100;
    f.close_at = 4;
    CHECK(run_fake(&f) == HOST_STARTUP_CANCELLED);
    CHECK(!strcmp(f.trace, "pump wait pump wait pump wait pump close"));
    /* a close and the boot's end at the same pump: the close wins (no game starts for a closed window) */
    memset(&f, 0, sizeof f);
    f.ready_at = 2;
    f.close_at = 2;
    CHECK(run_fake(&f) == HOST_STARTUP_CANCELLED && !strcmp(f.trace, "pump wait pump close"));
    /* a long boot keeps pumping: 150 passes (about 1.5 s at 10 ms) each pumped and waited, none spun */
    memset(&f, 0, sizeof f);
    f.ready_at = 151;
    CHECK(run_fake(&f) == HOST_STARTUP_READY && f.pumps == 151 && f.waits == 150);
    /* the progress bar's refresh: once per pass after the pump, before the wait; not on a pass ending the wait */
    memset(&f, 0, sizeof f);
    f.with_refresh = 1;
    f.ready_at = 3;
    CHECK(run_fake(&f) == HOST_STARTUP_READY);
    CHECK(!strcmp(f.trace, "pump (refresh) wait pump (refresh) wait pump ready") && f.refreshes == 2);
    memset(&f, 0, sizeof f);
    f.with_refresh = 1;
    f.ready_at = 100;
    f.close_at = 2;
    CHECK(run_fake(&f) == HOST_STARTUP_CANCELLED && !strcmp(f.trace, "pump (refresh) wait pump close"));
    /* the backend matrix at the resolution: SC-55-backed exactly sc55, wave and tap10 (XEEN.CFG music 5, 8, 9 under
     * music_override = auto resolve to them: test_phase286_music_auto) */
    for (int s = 0; s < (int)XEEN_MUSIC_SYSTEMS; ++s) {
        int backed = s == XEEN_MUSIC_SC55 || s == XEEN_MUSIC_WAVE || s == XEEN_MUSIC_TAP10;
        CHECK(xeen_audio_sc55_backed((xeen_music_system)s) == backed);
    }
    for (int auto_music = 0; auto_music <= 9; ++auto_music) {
        static const xeen_music_system want[10] = {XEEN_MUSIC_OFF,  XEEN_MUSIC_ADLIB, XEEN_MUSIC_SB,
                                                   XEEN_MUSIC_SBPRO, XEEN_MUSIC_MT32, XEEN_MUSIC_SC55,
                                                   XEEN_MUSIC_PAS,  XEEN_MUSIC_OFF,   XEEN_MUSIC_WAVE,
                                                   XEEN_MUSIC_TAP10};
        xeen_wox_config c;
        uint8_t cfg[0x16];
        xeen_music_resolution r;
        char text[256];
        xeen_wox_config_defaults(&c, ".");
        c.music_auto = 1;
        memset(cfg, 0, sizeof cfg);
        cfg[4] = (uint8_t)auto_music;
        xeen_music_resolve(&c, cfg, sizeof cfg, &r, text, sizeof text);
        CHECK(r.system == want[auto_music]);
        CHECK(xeen_audio_sc55_backed(r.system) == (auto_music == 5 || auto_music == 8 || auto_music == 9));
    }
    /* the end of the wait (headless platform): keys and presses queued meanwhile never reach a poll; the pointer
     * stays; input after it is queued as usual */
    {
        char error[256];
        xeen_platform *p = xeen_platform_open("t", 1, 1, error, sizeof error);
        xeen_input_provider in;
        uint16_t key = 0, mx = 0, my = 0;
        CHECK(p != NULL);
        if (p) {
            in = xeen_platform_input(p);
            CHECK(xeen_platform_key_event(p, (xeen_native_key)'A', 1, 0) && xeen_platform_key_event(p, (xeen_native_key)'B', 1, 0));
            (void)xeen_platform_mouse_event(p, 40, 30, 0, 0); /* a motion: the headless mouse enabled at 40,30 */
            CHECK(xeen_platform_mouse_event(p, 40, 30, 1, 1));
            xeen_platform_startup_done(p);
            CHECK(in.keyboard(in.user, &key) == 0 || key == 0);
            {
                xeen_mouse_sample m;
                memset(&m, 0, sizeof m);
                CHECK(in.mouse && in.mouse(in.user, &m) == 1 && !m.click && m.x == 40u && m.y == 30u);
            }
            CHECK(xeen_platform_pointer(p, &mx, &my) && mx == 40u && my == 30u);
            CHECK(xeen_platform_key_event(p, (xeen_native_key)'C', 1, 0));
            CHECK(in.keyboard(in.user, &key) == 1 && key != 0);
            printf("after the wait: key %04x queued as usual\n", key);
            xeen_platform_close(p);
        }
    }
    printf("phase290 lifecycle: %d failures\n", failures);
    return failures != 0;
}

/* -------------------------------------------------------------------- frame --- */
static int frame(const char *ppm)
{
    static uint8_t page[64000], palette[768], font[4352], pal[768], reference[64000];
    xeen_resource_info info;
    size_t got = 0;
    unsigned x = 0, y = 0, w = 0, left = 320, right = 0, top = 200, bottom = 0, count = 0, at;
    const char *text = HOST_STARTUP_SC55_TEXT;
    xeen_text_cursor cursor;
    CHECK(!strcmp(text, "Starting Nuked-SC55, please wait..."));
    CHECK(host_startup_loading_frame(&xeen_cc, text, page, palette, &x, &y, &w) == 1);
    CHECK(xeen_archive_find_name(&xeen_cc, "fnt", &info) == XR_OK &&
          xeen_archive_extract(&xeen_cc, XR_BANK_ASSETS_XOR35, &info, font, sizeof font, &got) == XR_OK);
    CHECK(xeen_archive_find_name(&xeen_cc, "mm4.pal", &info) == XR_OK &&
          xeen_archive_extract(&xeen_cc, XR_BANK_ASSETS_XOR35, &info, pal, sizeof pal, &got) == XR_OK);
    CHECK(!memcmp(palette, pal, 768u)); /* the game's palette, unchanged */
    /* the advance width from the font's table; the origin centred */
    {
        unsigned sum = 0;
        for (const char *c = text; *c; ++c) {
            sum += font[0x1000u + (unsigned char)*c];
        }
        CHECK(w == sum && x == (320u - sum) / 2u && y == 96u);
    }
    /* an independent drawing: the same glyphs one by one at the same origin */
    memset(&cursor, 0, sizeof cursor);
    xeen_text_cursor_set_colour(&cursor, 4);
    CHECK(HOST_STARTUP_TEXT_COLOUR == 4u);
    CHECK(cursor.colours[1] == 1u && cursor.colours[2] == 1u && cursor.colours[3] == 1u);
    memset(reference, 0, sizeof reference);
    at = x;
    for (const char *c = text; *c; ++c) {
        unsigned adv = font[0x1000u + (unsigned char)*c];
        if (*c != ' ') {
            CHECK(xeen_text_glyph(font, sizeof font, reference, sizeof reference, at, y, (unsigned char)*c,
                                  cursor.colours, &adv) == XEEN_OK);
        }
        at += adv;
    }
    CHECK(!memcmp(page, reference, sizeof page));
    for (unsigned i = 0; i < 64000u; ++i) {
        if (page[i]) {
            unsigned px = i % 320u, py = i / 320u;
            CHECK(page[i] == 1u);
            left = px < left ? px : left;
            right = px > right ? px : right;
            top = py < top ? py : top;
            bottom = py > bottom ? py : bottom;
            ++count;
        }
    }
    /* centred: the ink's margins differ by at most the glyph cells' blank columns; rows in 96..104 (g one lower) */
    CHECK(count > 100u);
    CHECK(top >= 96u && bottom <= 104u && top <= 100u);
    CHECK(left + right + 1u >= 320u - 8u && left + right + 1u <= 320u + 8u);
    /* the original colour table's entries in mm4.pal (evidence for the choice of HOST_STARTUP_TEXT_COLOUR) */
    if (getenv("XEEN_TEST_COLOURS")) {
        for (unsigned k = 0; k < 40u; ++k) {
            xeen_text_cursor t;
            memset(&t, 0, sizeof t);
            xeen_text_cursor_set_colour(&t, k);
            printf("colour %2u: index %3u rgb %2u %2u %2u\n", k, t.colours[1], palette[3u * t.colours[1]],
                   palette[3u * t.colours[1] + 1u], palette[3u * t.colours[1] + 2u]);
        }
    }
    /* legible: index 1 is white, index 0 black */
    CHECK(palette[0] == 0 && palette[1] == 0 && palette[2] == 0);
    CHECK(palette[3] == 63u && palette[4] == 63u && palette[5] == 63u);
    printf("phase290 frame: text %u..%u x %u..%u (origin %u,%u width %u), %u ink pixels, index 1 = %u %u %u\n",
           left, right, top, bottom, x, y, w, count, palette[3], palette[4], palette[5]);
    /* the progress bar (owner-requested): below the text's rows, only its rectangle drawn; the outline with cut
     * corners, a black gap, then the fill's per mille of 236 columns */
    {
        static uint8_t bar[64000];
        static const unsigned per[] = {0u, 1u, 250u, 500u, 999u, 1000u, 5000u};
        CHECK(HOST_STARTUP_BAR_Y > 104u && HOST_STARTUP_BAR_Y + HOST_STARTUP_BAR_HEIGHT <= 200u);
        CHECK(HOST_STARTUP_BAR_X + HOST_STARTUP_BAR_WIDTH <= 320u && HOST_STARTUP_BAR_X * 2u + HOST_STARTUP_BAR_WIDTH == 320u);
        for (unsigned k = 0; k < sizeof per / sizeof per[0]; ++k) {
            unsigned want = (per[k] > 1000u ? 1000u : per[k]) * 236u / 1000u, lit = 0;
            memcpy(bar, page, sizeof bar);
            host_startup_progress_bar(bar, per[k]);
            for (unsigned i = 0; i < 64000u; ++i) {
                unsigned px = i % 320u, py = i / 320u;
                int inside = px >= 40u && px < 280u && py >= 112u && py < 122u;
                if (!inside) {
                    CHECK(bar[i] == page[i]);
                    continue;
                }
                CHECK(bar[i] == 0u || bar[i] == 1u);
                {
                    unsigned bx = px - 40u, by = py - 112u;
                    int edge = bx == 0 || by == 0 || bx == 239u || by == 9u;
                    int corner = (bx == 0 || bx == 239u) && (by == 0 || by == 9u);
                    int fill = bx >= 2u && by >= 2u && bx < 238u && by < 8u && bx - 2u < want;
                    CHECK(bar[i] == ((edge && !corner) || fill ? 1u : 0u));
                    lit += bx >= 2u && by == 4u && bx < 238u && bar[i];
                }
            }
            CHECK(lit == want);
        }
        CHECK(per[0] == 0u && 1u * 236u / 1000u == 0u && 250u * 236u / 1000u == 59u && 1000u * 236u / 1000u == 236u);
    }
    /* missing resources or an unfit text: 0 and a black page */
    {
        CHECK(host_startup_loading_frame(NULL, text, page, palette, NULL, NULL, NULL) == 0);
        CHECK(host_startup_loading_frame(&xeen_cc, "\x01", page, palette, NULL, NULL, NULL) == 0);
        {
            char wide[200];
            memset(wide, 'W', sizeof wide - 1);
            wide[sizeof wide - 1] = 0;
            CHECK(host_startup_loading_frame(&xeen_cc, wide, page, palette, NULL, NULL, NULL) == 0);
        }
        for (unsigned i = 0; i < 64000u; ++i) {
            if (page[i]) {
                CHECK(!"page not cleared");
                break;
            }
        }
        /* Phase 334: each refusal also leaves the palette black (the first after a loaded palette). */
        CHECK(host_startup_loading_frame(&xeen_cc, text, page, palette, NULL, NULL, NULL) == 1);
        CHECK(host_startup_loading_frame(&xeen_cc, wide_text(), page, palette, NULL, NULL, NULL) == 0);
        for (unsigned i = 0; i < 768u; ++i) {
            if (palette[i]) {
                CHECK(!"palette not cleared");
                break;
            }
        }
    }
    if (ppm) {
        FILE *f = fopen(ppm, "wb");
        CHECK(host_startup_loading_frame(&xeen_cc, text, page, palette, NULL, NULL, NULL) == 1);
        host_startup_progress_bar(page, 500u); /* the shown frame half-way through the boot */
        if (f) {
            fprintf(f, "P6\n640 400\n255\n");
            for (unsigned py = 0; py < 400u; ++py) {
                for (unsigned px = 0; px < 640u; ++px) {
                    const uint8_t *rgb = palette + 3u * page[(py / 2u) * 320u + px / 2u];
                    for (int k = 0; k < 3; ++k) {
                        fputc((rgb[k] << 2) | (rgb[k] >> 4), f);
                    }
                }
            }
            fclose(f);
        }
    }
    printf("phase290 frame: %d failures\n", failures);
    return failures != 0;
}

/* --------------------------------------------------------------------- boot --- */
typedef struct {
    unsigned long n;
    uint64_t fnv;
} events;
static void on_event(void *u, uint64_t pit, const uint8_t *b, size_t n)
{
    events *e = u;
    ++e->n;
    for (size_t i = 0; i < 8; ++i) {
        e->fnv = (e->fnv ^ (uint8_t)(pit >> (8u * i))) * UINT64_C(1099511628211);
    }
    for (size_t i = 0; i < n; ++i) {
        e->fnv = (e->fnv ^ b[i]) * UINT64_C(1099511628211);
    }
}
static xeen_wox_config config;
static int no_intro; /* the engine opens without INTRO.CC */
static xeen_audio_engine *engine(xeen_music_system system, const char *sc55, xeen_audio_sc55_boot *boot, events *ev,
                                 char *why, size_t n)
{
    xeen_audio_engine_options o;
    xeen_wox_config_defaults(&config, ".");
    config.system = system;
    snprintf(config.sc55_roms, sizeof config.sc55_roms, "%s", sc55);
    memset(&o, 0, sizeof o);
    o.config = &config;
    o.drivers = &xeen_cc;
    o.intro_drivers = no_intro ? NULL : &intro_cc;
    o.cc[0] = &xeen_cc;
    o.cc[1] = &dark_cc;
    o.mode = XEEN_AUDIO_OFFLINE;
    o.on_event = on_event;
    o.on_event_context = ev;
    o.sc55_boot = boot;
    return xeen_audio_engine_open(&o, why, n);
}
#define SECONDS 4u
static int16_t pcm_a[2u * 32000u * SECONDS], pcm_b[2u * 32000u * SECONDS];
/* A song, an effect, the F10 pause and resume, a fade (music control 3: the driver's fade) in one fixed script. */
static void script(xeen_audio_engine *e, const Blob *song, int16_t *pcm)
{
    const xeen_audio_sink *k = xeen_audio_engine_sink(e);
    k->song(k->context, song->data, song->size);
    xeen_audio_engine_render(e, pcm, 32000u);
    k->effect(k->context, 0x15u);
    xeen_audio_engine_render(e, pcm + 2u * 32000u, 16000u);
    xeen_audio_engine_pause(e, 1);
    xeen_audio_engine_pause(e, 0);
    xeen_audio_engine_render(e, pcm + 2u * 48000u, 16000u);
    k->music(k->context, 3u);
    xeen_audio_engine_render(e, pcm + 2u * 64000u, 64000u);
}
static double now_ms(void)
{
    return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
}
static int boot(const char *roms)
{
    char sc55[1000], why[600];
    struct CC dark = {&dark_cc};
    Blob song = {0};
    static const xeen_music_system systems[3] = {XEEN_MUSIC_SC55, XEEN_MUSIC_WAVE, XEEN_MUSIC_TAP10};
    int ran = 0;
    snprintf(sc55, sizeof sc55, "%s/sc55", roms);
    CHECK(cc_read(&dark, "outday1.m", &song));
    for (int s = 0; s < 3 && song.data; ++s) {
        events ea = {0, UINT64_C(1469598103934665603)}, eb = {0, UINT64_C(1469598103934665603)};
        xeen_audio_sc55_boot *b;
        unsigned long polls = 0;
        double t0, t1, t2, t3;
        xeen_audio_engine *a, *e;
        /* the open's own boot (the pre-Phase-290 path) */
        t0 = now_ms();
        a = engine(systems[s], sc55, NULL, &ea, why, sizeof why);
        t1 = now_ms();
        if (!a || !xeen_audio_engine_music_ready(a)) {
            printf("%s: SC-55 unavailable (skipped): %s\n", xeen_music_system_name(systems[s]),
                   a ? xeen_audio_engine_music_error(a) : why);
            xeen_audio_engine_close(a);
            continue;
        }
        ++ran;
        script(a, &song, pcm_a);
        /* the worker's boot: never blocks the poll; the open takes it */
        xeen_wox_config_defaults(&config, ".");
        config.system = systems[s];
        snprintf(config.sc55_roms, sizeof config.sc55_roms, "%s", sc55);
        t2 = now_ms();
        CHECK((b = xeen_audio_sc55_boot_start(&config)) != NULL);
        {
            /* the progress bar's source: -1 until reported, then never decreasing, 1000 at the end; seen moving */
            int last = -1, steps = 0;
            while (b && !xeen_audio_sc55_boot_finished(b)) {
                int now = xeen_audio_sc55_boot_progress(b);
                CHECK(now >= -1 && now <= 1000 && now >= last);
                steps += now > last && last >= 0;
                last = now;
                ++polls;
                SDL_Delay(10);
            }
            CHECK(xeen_audio_sc55_boot_progress(b) == 1000);
            CHECK(steps > 5);
            printf("progress: %d increases seen over %lu polls\n", steps, (unsigned long)polls);
        }
        t3 = now_ms();
        CHECK(polls > 10u); /* the boot ran while the caller kept polling (about 1.5 s) */
        CHECK(xeen_audio_sc55_boot_succeeded(b));
        e = engine(systems[s], sc55, b, &eb, why, sizeof why);
        CHECK(e && xeen_audio_engine_music_ready(e));
        CHECK(!xeen_audio_sc55_boot_succeeded(b)); /* taken: the handle holds no engine any more */
        xeen_audio_sc55_boot_close(b);
        if (e) {
            script(e, &song, pcm_b);
            CHECK(ea.n == eb.n && ea.fnv == eb.fnv);
            CHECK(!memcmp(pcm_a, pcm_b, sizeof pcm_a));
            {
                long pk = 0;
                for (size_t i = 0; i < sizeof pcm_a / sizeof pcm_a[0]; ++i) {
                    long v = pcm_a[i] < 0 ? -(long)pcm_a[i] : pcm_a[i];
                    pk = v > pk ? v : pk;
                }
                CHECK(pk > 100);
                printf("%s: open's boot %.0f ms, worker boot %.0f ms over %lu polls; %lu driver messages fnv %016llx "
                       "both; %u s PCM identical (peak %ld)\n",
                       xeen_music_system_name(systems[s]), t1 - t0, t3 - t2, polls, ea.n,
                       (unsigned long long)ea.fnv, SECONDS, pk);
            }
        }
        xeen_audio_engine_close(e);
        xeen_audio_engine_close(a);
    }
    free(song.data);
    if (!ran) {
        printf("phase290 boot: no SC-55 ROMs/library (skipped)\n");
        return 2;
    }
    /* a failed boot (a directory without ROMs): finished, no engine, the open reports the existing error and does not
     * boot again; the same error as the open's own boot */
    {
        events ea = {0, 0}, eb = {0, 0};
        char why_a[600], why_b[600], err_a[700], err_b[700];
        xeen_audio_sc55_boot *b;
        xeen_audio_engine *a, *e;
        a = engine(XEEN_MUSIC_SC55, ".", NULL, &ea, why_a, sizeof why_a);
        snprintf(err_a, sizeof err_a, "%s", a ? xeen_audio_engine_music_error(a) : why_a);
        xeen_wox_config_defaults(&config, ".");
        config.system = XEEN_MUSIC_SC55;
        snprintf(config.sc55_roms, sizeof config.sc55_roms, ".");
        CHECK((b = xeen_audio_sc55_boot_start(&config)) != NULL);
        CHECK(!xeen_audio_sc55_boot_succeeded(b) && xeen_audio_sc55_boot_finished(b));
        e = engine(XEEN_MUSIC_SC55, ".", b, &eb, why_b, sizeof why_b);
        snprintf(err_b, sizeof err_b, "%s", e ? xeen_audio_engine_music_error(e) : why_b);
        CHECK(a && e && !xeen_audio_engine_music_ready(a) && !xeen_audio_engine_music_ready(e));
        CHECK(!strcmp(err_a, err_b) && !strcmp(why_a, why_b));
        CHECK(ea.n == 0 && eb.n == 0);
        printf("failed boot: \"%s\" (same as the open's own)\n", err_b);
        xeen_audio_sc55_boot_close(b);
        xeen_audio_engine_close(e);
        xeen_audio_engine_close(a);
    }
    /* no INTRO.CC: the booted SC-55 is taken, then the existing driver error (music unavailable, the engine stays) */
    {
        events ev = {0, 0};
        xeen_audio_sc55_boot *b;
        xeen_audio_engine *e;
        xeen_wox_config_defaults(&config, ".");
        config.system = XEEN_MUSIC_SC55;
        snprintf(config.sc55_roms, sizeof config.sc55_roms, "%s", sc55);
        CHECK((b = xeen_audio_sc55_boot_start(&config)) != NULL);
        no_intro = 1;
        e = engine(XEEN_MUSIC_SC55, sc55, b, &ev, why, sizeof why);
        no_intro = 0;
        CHECK(e && !xeen_audio_engine_music_ready(e));
        CHECK(e && !strcmp(xeen_audio_engine_music_error(e), "INTRO.CC canmus driver missing or of an unknown build"));
        CHECK(!xeen_audio_sc55_boot_succeeded(b)); /* taken (and closed with the failed music) */
        printf("no INTRO.CC: \"%s\"\n", e ? xeen_audio_engine_music_error(e) : why);
        xeen_audio_sc55_boot_close(b);
        xeen_audio_engine_close(e);
    }
    /* boot, close (the window closed during the boot: joined, the engine closed unused), three times; a close right
     * after the start waits for the boot */
    for (int i = 0; i < 3; ++i) {
        double t0 = now_ms();
        xeen_audio_sc55_boot *b;
        xeen_wox_config_defaults(&config, ".");
        config.system = systems[i];
        snprintf(config.sc55_roms, sizeof config.sc55_roms, "%s", sc55);
        CHECK((b = xeen_audio_sc55_boot_start(&config)) != NULL);
        CHECK(!xeen_audio_sc55_boot_finished(b));
        xeen_audio_sc55_boot_close(b);
        printf("boot/close cycle %d: %.0f ms\n", i + 1, now_ms() - t0);
    }
    /* other systems start no worker; null is safe everywhere */
    for (int s = 0; s < (int)XEEN_MUSIC_SYSTEMS; ++s) {
        if (!xeen_audio_sc55_backed((xeen_music_system)s)) {
            xeen_wox_config_defaults(&config, ".");
            config.system = (xeen_music_system)s;
            CHECK(xeen_audio_sc55_boot_start(&config) == NULL);
        }
    }
    CHECK(xeen_audio_sc55_boot_start(NULL) == NULL);
    CHECK(xeen_audio_sc55_boot_finished(NULL) && !xeen_audio_sc55_boot_succeeded(NULL));
    xeen_audio_sc55_boot_close(NULL);
    printf("phase290 boot: %d systems, %d failures\n", ran, failures);
    return failures != 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "lifecycle")) {
        return lifecycle();
    }
    if (argc >= 3 && (!strcmp(argv[1], "frame") || !strcmp(argv[1], "boot"))) {
        if (!open_archives(argv[2])) {
            fprintf(stderr, "no XEEN.CC/DARK.CC/INTRO.CC in %s: skipped\n", argv[2]);
            return 2;
        }
        if (!strcmp(argv[1], "frame")) {
            return frame(argc >= 4 ? argv[3] : NULL);
        }
        if (argc >= 4) {
            return boot(argv[3]);
        }
    }
    fprintf(stderr, "usage: %s lifecycle | frame DOS [PPM] | boot DOS ROMS\n", argv[0]);
    return 1;
}
