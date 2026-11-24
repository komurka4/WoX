/* Phase 286: the remaining original music drivers but the PC speaker's, through the audio engine (the drivers' own
 * event streams are compared with the unmodified drivers in Unicorn by phase285_canmus: 472 cases).
 *
 *   test engine DOS ROMS      the engine's routing: music_override = adlib / sb / pas play admus / blastmus / specmus on one
 *                             YM3812 (mono, audible, no ROMs); pas's stream is sb's; XEEN.CFG's 8D4C reaches the init;
 *                             no INTRO.CC: no music, the error names the driver, nothing stands in; wave without SC-55
 *                             ROMs: no music, the reason names the SC-55
 *   test sc55 DOS ROMS        the same, and music_override = wave / tap10 play wavemus / tapmus on Nuked-SC55 (ROMS/sc55; the
 *                             UART set-up, audible PCM; reported skipped without ROMs)
 *   test voice CASES          Phase 285A's Q1: the voice model of adsnd (XEEN.CFG sound 2) and source (5) against the
 *                             unmodified drivers in Unicorn (tests/phase286_voice_cases.txt): the interrupts until
 *                             187E9(1, 0) answers 0 equal xeen_voc_driver_count, and the model's answer changes at
 *                             exactly that many PIT cycles (busy the tick before, idle the tick after)
 *   test block CASES          Phase 287: ibmsnd's blocking start with a real music driver (tests/phase287_block_cases.txt)
 *   test voiceflag DOS        Phase 287: the engine's voice window on the FM driver's word 0Fh (ibmsnd/adsnd/source):
 *                             admus leaves channel 7 alone exactly from the voice's start to its model length, or to a
 *                             stop; the SB drivers' voices (and none) change nothing; sb/pas/sbpro streams change
 * Exit 2 (skip) when DOS lacks XEEN.CC/DARK.CC/INTRO.CC. */
#include "xeen/audio.h"
#include "xeen/audio_engine.h"
#include "audio/audio_archive.h"
#include "audio/music.h"
#include "audio/sbpro.h"
#include "xeen/resources.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* struct CC is audio/audio_archive.h's (Phase 308). */
int cc_read(CC *cc, const char *name, Blob *out);

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static uint8_t *readfile(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    long end;
    uint8_t *p = NULL;
    if (f && !fseek(f, 0, SEEK_END) && (end = ftell(f)) >= 0 && !fseek(f, 0, SEEK_SET)) {
        *n = (size_t)end;
        if ((p = malloc(*n + 1)) && fread(p, 1, *n, f) != *n) {
            free(p);
            p = NULL;
        }
    }
    if (f) {
        fclose(f);
    }
    return p;
}
static xeen_archive xeen_cc, dark_cc, intro_cc;
static uint8_t *bytes[3];
static int open_archives(const char *dos)
{
    static const char *const names[3] = {"XEEN.CC", "DARK.CC", "INTRO.CC"};
    xeen_archive *a[3] = {&xeen_cc, &dark_cc, &intro_cc};
    for (unsigned i = 0; i < 3u; i++) {
        char path[1024];
        size_t n;
        snprintf(path, sizeof path, "%s/%s", dos, names[i]);
        if (!(bytes[i] = readfile(path, &n)) || xeen_archive_open(a[i], bytes[i], n) != XR_OK) {
            return 0;
        }
    }
    return 1;
}

/* The engine's events (offline): all kept, hashed. */
typedef struct {
    unsigned n, mode, mixer, mpu, opl_other_chip;
    uint64_t fnv;
    uint8_t first[8][4];
} events;
static void on_event(void *u, uint64_t pit, const uint8_t *b, size_t n)
{
    events *e = u;
    (void)pit;
    if (e->n < 8u) {
        memcpy(e->first[e->n], b, n < 4 ? n : 4);
    }
    e->n++;
    if (n == 4 && b[0] == SBPRO_EVENT_MODE) {
        e->mode++;
    } else if (n == 4 && b[0] == SBPRO_EVENT_MIXER) {
        e->mixer++;
    } else if (n == 4 && b[0] == MUSIC_EVENT_MPU_COMMAND) {
        e->mpu++;
    } else if (n == 4 && b[0] == SBPRO_EVENT_OPL && b[1] != 0) {
        e->opl_other_chip++;
    }
    for (size_t i = 0; i < n; i++) {
        e->fnv = (e->fnv ^ b[i]) * UINT64_C(1099511628211);
    }
}
static xeen_wox_config cfg;
static xeen_audio_engine *engine(xeen_music_system system, const xeen_archive *intro, const char *roms,
                                 const uint8_t *xeen_cfg, events *ev, char *why, size_t n)
{
    xeen_audio_engine_options o;
    xeen_wox_config_defaults(&cfg, ".");
    cfg.system = system;
    snprintf(cfg.sc55_roms, sizeof cfg.sc55_roms, "%s/sc55", roms);
    memset(&o, 0, sizeof o);
    memset(ev, 0, sizeof *ev);
    ev->fnv = UINT64_C(1469598103934665603);
    o.config = &cfg;
    o.drivers = &xeen_cc;
    o.intro_drivers = intro;
    o.cc[0] = &xeen_cc;
    o.cc[1] = &dark_cc;
    o.mode = XEEN_AUDIO_OFFLINE;
    o.on_event = on_event;
    o.on_event_context = ev;
    o.xeen_cfg = xeen_cfg;
    o.xeen_cfg_size = xeen_cfg ? 29u : 0u;
    return xeen_audio_engine_open(&o, why, n);
}
static int16_t pcm[2u * 64000u];
/* Plays newbrigh.m (Darkside) and cue 14h for FRAMES (at most two seconds); returns the peak, sets *mono when L == R
 * throughout. */
static long play(xeen_audio_engine *e, int *mono, size_t frames)
{
    struct CC dark = {&dark_cc};
    Blob song = {0};
    long pk = 0;
    *mono = 1;
    CHECK(cc_read(&dark, "newbrigh.m", &song));
    xeen_audio_engine_sink(e)->song(xeen_audio_engine_sink(e)->context, song.data, song.size);
    xeen_audio_engine_sink(e)->effect(xeen_audio_engine_sink(e)->context, 0x14u);
    free(song.data);
    xeen_audio_engine_render(e, pcm, frames);
    for (size_t i = 0; i < frames; i++) {
        long l = pcm[2 * i] < 0 ? -(long)pcm[2 * i] : pcm[2 * i];
        pk = l > pk ? l : pk;
        if (pcm[2 * i] != pcm[2 * i + 1]) {
            *mono = 0;
        }
    }
    return pk;
}
static int engine_checks(const char *roms, int sc55)
{
    static const xeen_music_system fm[3] = {XEEN_MUSIC_ADLIB, XEEN_MUSIC_SB, XEEN_MUSIC_PAS};
    static uint8_t cfg0[29], cfg1[29];
    uint64_t fnv[3] = {0, 0, 0};
    char why[512];
    cfg1[0x14] = 1; /* XEEN.CFG 8D4C = 1 (the genuine file), cfg0 8D4C = 0 */
    for (unsigned k = 0; k < 3u; k++) {
        events ev;
        int mono;
        xeen_audio_engine *e = engine(fm[k], &intro_cc, roms, cfg1, &ev, why, sizeof why);
        CHECK(e && xeen_audio_engine_music_ready(e));
        if (!e || !xeen_audio_engine_music_ready(e)) {
            printf("%s: %s\n", xeen_music_system_name(fm[k]), e ? xeen_audio_engine_music_error(e) : why);
            xeen_audio_engine_close(e);
            continue;
        }
        /* The init: the renderer's mono mode, the four SB Pro mixer writes (8D4C = 1), then the OPL set-up; no write to
         * a second chip ever. */
        CHECK(ev.first[0][0] == SBPRO_EVENT_MODE && ev.first[0][2] == SBPRO_MODE_MONO && ev.mixer == 4u);
        CHECK(ev.first[1][0] == SBPRO_EVENT_MIXER && ev.first[1][2] == 0x2e && ev.first[4][2] == 0x04 &&
              ev.first[4][3] == 0x99 && ev.first[5][0] == SBPRO_EVENT_OPL && ev.first[5][2] == 0x01);
        long pk = play(e, &mono, 64000u);
        printf("%s (%s): peak %ld, %s, %u events\n", xeen_music_system_name(fm[k]), xeen_music_system_driver(fm[k]), pk,
               mono ? "mono" : "NOT MONO", ev.n);
        CHECK(pk > 300 && mono && ev.opl_other_chip == 0u);
        fnv[k] = ev.fnv;
        xeen_audio_engine_close(e);
        /* 8D4C = 0: no mixer write. */
        e = engine(fm[k], &intro_cc, roms, cfg0, &ev, why, sizeof why);
        CHECK(e && xeen_audio_engine_music_ready(e) && ev.mixer == 0u && ev.mode == 1u);
        xeen_audio_engine_close(e);
        /* No INTRO.CC: no music, the error names the driver, no event (nothing stands in). */
        e = engine(fm[k], NULL, roms, cfg1, &ev, why, sizeof why);
        CHECK(e && !xeen_audio_engine_music_ready(e) &&
              strstr(xeen_audio_engine_music_error(e), xeen_music_system_driver(fm[k])) && ev.n == 0u);
        xeen_audio_engine_close(e);
    }
    /* Phase 285A Q1: the voices as adsnd (trim 22h at 14731 Hz: L - 21h samples) and source (8 samples of every 17
     * bytes per interrupt, about 6998 Hz) play them: each ends at its own length (the SB layout would last
     * L - 9 samples at 14706 Hz). */
    {
        struct CC xeen = {&xeen_cc};
        Blob v = {0};
        CHECK(cc_read(&xeen, "banker.voc", &v) && v.size > 0x1du);
        unsigned length = v.data ? (unsigned)(v.data[0x1b] | v.data[0x1c] << 8) : 0u;
        for (int source = 0; source < 2 && v.data; ++source) {
            xeen_audio_engine_options o;
            xeen_wox_config_defaults(&cfg, ".");
            cfg.system = XEEN_MUSIC_OFF;
            memset(&o, 0, sizeof o);
            o.config = &cfg;
            o.drivers = &xeen_cc;
            o.cc[0] = &xeen_cc;
            o.cc[1] = &dark_cc;
            o.mode = XEEN_AUDIO_OFFLINE;
            o.voc_trim = source ? 0x0au : 0x22u;
            o.voc_rate_hz = source ? 0u : XEEN_VOC_PAS_RATE_HZ;
            o.voc_source = (unsigned)source;
            xeen_audio_engine *e = xeen_audio_engine_open(&o, why, sizeof why);
            CHECK(e != NULL);
            if (!e) {
                continue;
            }
            double seconds = source ? (double)(((length - 0x20u) / 17u + 1u) * 8u) / (1193182.0 * 8.0 / 1364.0)
                                    : (double)(length - 0x21u) / XEEN_VOC_PAS_RATE_HZ;
            size_t before = (size_t)(seconds * 32000.0) - 400u, after = (size_t)(seconds * 32000.0) + 400u;
            xeen_audio_engine_stats st;
            xeen_audio_engine_sink(e)->voice(xeen_audio_engine_sink(e)->context, v.data, v.size);
            for (size_t done = 0; done < before;) {
                size_t chunk = before - done < 64000u ? before - done : 64000u;
                xeen_audio_engine_render(e, pcm, chunk);
                done += chunk;
            }
            xeen_audio_engine_get_stats(e, &st);
            CHECK(st.voice_active);
            xeen_audio_engine_render(e, pcm, after - before);
            xeen_audio_engine_get_stats(e, &st);
            CHECK(!st.voice_active && st.voices == 1u && !st.voice_errors);
            printf("%s voice: banker.voc (L %u) ends at %.3f s\n", source ? "source" : "adsnd", length, seconds);
            xeen_audio_engine_close(e);
        }
        free(v.data);
    }
    /* specmus is blastmus byte for byte in INTRO.CC: the same stream; admus differs (its levels and selector). */
    CHECK(fnv[1] && fnv[1] == fnv[2] && fnv[0] != fnv[1]);
    /* The SB Pro (promus) still plays two chips in stereo (unchanged). */
    {
        events ev;
        int mono;
        xeen_audio_engine *e = engine(XEEN_MUSIC_SBPRO, &intro_cc, roms, cfg1, &ev, why, sizeof why);
        CHECK(e && xeen_audio_engine_music_ready(e) && ev.mode == 0u);
        if (e && xeen_audio_engine_music_ready(e)) {
            long pk = play(e, &mono, 64000u);
            CHECK(pk > 300 && !mono && ev.opl_other_chip > 0u);
        }
        xeen_audio_engine_close(e);
    }
    /* wave and tap10 on Nuked-SC55 (mode sc55 only: each engine boots the emulated module). */
    if (sc55) {
        static const xeen_music_system gs[2] = {XEEN_MUSIC_WAVE, XEEN_MUSIC_TAP10};
        int ran = 0;
        for (unsigned k = 0; k < 2u; k++) {
            events ev;
            int mono;
            xeen_audio_engine *e = engine(gs[k], &intro_cc, roms, cfg1, &ev, why, sizeof why);
            if (!e || !xeen_audio_engine_music_ready(e)) {
                const char *err = e ? xeen_audio_engine_music_error(e) : why;
                printf("%s: SC-55 unavailable (skipped): %s\n", xeen_music_system_name(gs[k]), err);
                CHECK(strstr(err, "SC-55") || strstr(err, "sc55") || strstr(err, "synthesizer"));
                xeen_audio_engine_close(e);
                continue;
            }
            ++ran;
            /* The UART set-up: the six SB16 mixer writes (8D4C = 1), then the MPU reset (wavemus FFh FFh 3Fh,
             * tapmus FFh). */
            CHECK(ev.mixer == 6u && ev.mpu == (k ? 1u : 3u) && ev.first[0][0] == SBPRO_EVENT_MIXER);
            long pk = play(e, &mono, 16000u); /* half a second: the emulated module is slow in Debug and ASan */
            printf("%s (%s): peak %ld, %u events\n", xeen_music_system_name(gs[k]), xeen_music_system_driver(gs[k]),
                   pk, ev.n);
            CHECK(pk > 100);
            xeen_audio_engine_close(e);
        }
        printf("phase286 engine: wave/tap10 %s\n", ran == 2 ? "rendered" : "skipped (no SC-55 ROMs)");
    }
    /* No SC-55 ROMs: no music, the reason names the SC-55, nothing stands in (no module boots). */
    {
        events ev;
        xeen_audio_engine *e = engine(XEEN_MUSIC_WAVE, &intro_cc, "/nonexistent-roms", cfg1, &ev, why, sizeof why);
        CHECK(e && !xeen_audio_engine_music_ready(e) && strstr(xeen_audio_engine_music_error(e), "SC-55") &&
              ev.n == 0u);
        xeen_audio_engine_close(e);
    }
    printf("phase286 engine: %d failures\n", failures);
    return failures != 0;
}

/* Phase 287: the engine's voice window on the FM music driver's word 0Fh. Channel 7's frequency writes (OPL A7h) with a
 * PIT time, recorded around a voice. */
typedef struct {
    uint64_t at[4096];
    unsigned n;
    uint64_t fnv;
} a7_events;
static void on_a7(void *u, uint64_t pit, const uint8_t *b, size_t n)
{
    a7_events *e = u;
    if (n == 4 && b[0] == SBPRO_EVENT_OPL && b[2] == 0xa7u && e->n < 4096u) {
        e->at[e->n++] = pit;
    }
    for (size_t i = 0; i < n; i++) {
        e->fnv = (e->fnv ^ b[i]) * UINT64_C(1099511628211);
    }
}
/* Renders FRAMES at 32 kHz in chunks. */
static void render_frames(xeen_audio_engine *e, size_t frames)
{
    while (frames) {
        size_t chunk = frames < 64000u ? frames : 64000u;
        xeen_audio_engine_render(e, pcm, chunk);
        frames -= chunk;
    }
}
/* Music SYSTEM, voice driver KIND: a cue running channel 7 (effect 14h, restarted every 0.125 s), banker.voc started at
 * 0.25 s, optionally stopped at STOP_FRAME; returns the A7h writes. */
static void voice_window_run(xeen_music_system system, unsigned kind, size_t stop_frame, a7_events *ev, uint64_t *start,
                             uint64_t *length)
{
    xeen_audio_engine_options o;
    struct CC xeen = {&xeen_cc};
    Blob v = {0};
    char why[512];
    static uint8_t cfg1[29] = {[0x14] = 1};
    memset(ev, 0, sizeof *ev);
    ev->fnv = UINT64_C(1469598103934665603);
    xeen_wox_config_defaults(&cfg, ".");
    cfg.system = system;
    memset(&o, 0, sizeof o);
    o.config = &cfg;
    o.drivers = &xeen_cc;
    o.intro_drivers = &intro_cc;
    o.cc[0] = &xeen_cc;
    o.cc[1] = &dark_cc;
    o.mode = XEEN_AUDIO_OFFLINE;
    o.on_event = on_a7;
    o.on_event_context = ev;
    o.xeen_cfg = cfg1;
    o.xeen_cfg_size = sizeof cfg1;
    o.voc_driver = kind; /* as the host sets it (explore.c open_window_audio) */
    o.voc_trim = kind == XEEN_VOICE_MODEL_ADLIB ? 0x22u : 0x0au;
    o.voc_source = kind == XEEN_VOICE_MODEL_SOURCE;
    o.voc_none = kind == XEEN_VOICE_MODEL_PCSPEAKER || kind == XEEN_VOICE_MODEL_NONE;
    xeen_audio_engine *e = xeen_audio_engine_open(&o, why, sizeof why);
    CHECK(e && xeen_audio_engine_music_ready(e) && cc_read(&xeen, "banker.voc", &v));
    if (!e || !v.data) {
        xeen_audio_engine_close(e);
        free(v.data);
        return;
    }
    const xeen_audio_sink *k = xeen_audio_engine_sink(e);
    *start = (uint64_t)8000u * 1193182u / 32000u;
    *length = xeen_voc_driver_pit(v.data, v.size, kind, o.voc_trim);
    for (size_t f = 0; f < 64000u; f += 4000u) { /* two seconds */
        if (f % 4000u == 0) {
            k->effect(k->context, 0x14u);
        }
        if (f == 8000u) {
            k->voice(k->context, v.data, v.size);
        }
        if (stop_frame && f == stop_frame) {
            k->voice_stop(k->context);
        }
        render_frames(e, 4000u);
    }
    xeen_audio_engine_close(e);
    free(v.data);
}
/* A7h writes inside [FROM, TO). */
static unsigned a7_between(const a7_events *ev, uint64_t from, uint64_t to)
{
    unsigned c = 0;
    for (unsigned i = 0; i < ev->n; i++) {
        c += ev->at[i] >= from && ev->at[i] < to;
    }
    return c;
}
static int voiceflag_checks(void)
{
    static const unsigned kinds[3] = {XEEN_VOICE_MODEL_ADLIB, XEEN_VOICE_MODEL_SOURCE, XEEN_VOICE_MODEL_PCSPEAKER};
    a7_events control, none, ev;
    uint64_t start = 0, length = 0, s0, l0;
    /* Controls: the SB drivers never write the word 0Fh: their voice changes nothing; channel 7 is written meanwhile. */
    voice_window_run(XEEN_MUSIC_ADLIB, XEEN_VOICE_MODEL_SB, 0, &control, &s0, &l0);
    voice_window_run(XEEN_MUSIC_ADLIB, XEEN_VOICE_MODEL_NONE, 0, &none, &s0, &l0);
    CHECK(control.fnv == none.fnv && a7_between(&control, s0, s0 + 300000u) > 0u);
    for (unsigned i = 0; i < 3u; i++) {
        voice_window_run(XEEN_MUSIC_ADLIB, kinds[i], 0, &ev, &start, &length);
        uint64_t end = start + length;
        unsigned in = a7_between(&ev, start + 1u, end), after = a7_between(&ev, end, end + 20u * 16390u);
        printf("adlib + voice kind %u: window %llu..%llu PIT (%.3f s): A7h %u inside, %u in the 20 ticks after; "
               "control %u inside\n", kinds[i], (unsigned long long)start, (unsigned long long)end,
               (double)length / 1193182.0, in, after, a7_between(&control, start + 1u, end));
        CHECK(length > 16390u * 20u && in == 0u && after > 0u && a7_between(&control, start + 1u, end) > 0u &&
              a7_between(&ev, 0, start) == a7_between(&control, 0, start));
    }
    /* A stop ends the window (adsnd/source entry 3(0)): channel 7 is written again after it. */
    voice_window_run(XEEN_MUSIC_ADLIB, XEEN_VOICE_MODEL_ADLIB, 12000u, &ev, &start, &length);
    {
        uint64_t stop = (uint64_t)12000u * 1193182u / 32000u;
        CHECK(stop < start + length && a7_between(&ev, start + 1u, stop) == 0u &&
              a7_between(&ev, stop, start + length) > 0u);
    }
    /* sb / pas / sbpro: the voice changes the FM stream (their own paths: tests/phase287_voice_flag); the SB voice not. */
    {
        static const xeen_music_system others[3] = {XEEN_MUSIC_SB, XEEN_MUSIC_PAS, XEEN_MUSIC_SBPRO};
        for (unsigned i = 0; i < 3u; i++) {
            voice_window_run(others[i], XEEN_VOICE_MODEL_SB, 0, &control, &s0, &l0);
            voice_window_run(others[i], XEEN_VOICE_MODEL_NONE, 0, &none, &s0, &l0);
            voice_window_run(others[i], XEEN_VOICE_MODEL_SOURCE, 0, &ev, &start, &length);
            CHECK(control.fnv == none.fnv && ev.fnv != control.fnv);
        }
    }
    printf("phase287 voice engine: %d failures\n", failures);
    return failures != 0;
}

/* Phase 287: ibmsnd's blocking start with a real music driver (tests/phase287_block_cases.txt, block_oracle.py): the
 * interrupts, PIT cycles, music-driver calls and BIOS ticks of the hold; the word 0Fh 2 during it and 0 after; never
 * busy after. ibmmus's clock stands. */
static int block_checks(const char *cases)
{
    FILE *f = fopen(cases, "r");
    char line[256];
    unsigned rows = 0;
    if (!f) {
        fprintf(stderr, "cannot read %s\n", cases);
        return 1;
    }
    while (fgets(line, sizeof line, f)) {
        char music[16];
        unsigned length, during, after, query;
        unsigned long irqs, pit, calls, ticks;
        uint8_t voc[0x40] = {0};
        if (line[0] == '#' || sscanf(line, "%15s %u %lu %lu %lu %lu %u %u %u", music, &length, &irqs, &pit, &calls, &ticks,
                                     &during, &after, &query) != 9) {
            continue;
        }
        voc[0x1b] = (uint8_t)length;
        voc[0x1c] = (uint8_t)(length >> 8);
        uint32_t n = xeen_voc_driver_count(voc, sizeof voc, XEEN_VOICE_MODEL_PCSPEAKER, 0);
        int stands = !strcmp(music, "ibmmus");
        if (n != irqs || xeen_voc_driver_pit(voc, sizeof voc, XEEN_VOICE_MODEL_PCSPEAKER, 0) != pit ||
            xeen_voc_pcspeaker_music_calls(n) != calls || xeen_voc_pcspeaker_bios_ticks(n, stands) != ticks ||
            during != 2u || after != 0u || query != 0u ||
            xeen_voc_busy_at_kind(0, 0, n, XEEN_VOICE_MODEL_PCSPEAKER)) {
            fprintf(stderr, "%s L %u: native %u interrupts, %u calls, %u ticks; original %lu, %lu, %lu\n", music, length,
                    (unsigned)n, (unsigned)xeen_voc_pcspeaker_music_calls(n),
                    (unsigned)xeen_voc_pcspeaker_bios_ticks(n, stands), irqs, calls, ticks);
            ++failures;
        }
        ++rows;
    }
    fclose(f);
    printf("phase287 block: %u cases, %d failures\n", rows, failures);
    return failures != 0 || rows < 50u;
}

static int voice_checks(const char *cases)
{
    FILE *f = fopen(cases, "r");
    char line[256];
    unsigned rows = 0;
    if (!f) {
        fprintf(stderr, "cannot read %s\n", cases);
        return 1;
    }
    while (fgets(line, sizeof line, f)) {
        char kind[16], irqs_text[24];
        unsigned length, period;
        uint8_t voc[0x40] = {0};
        if (line[0] == '#' || sscanf(line, "%15s %u %23s %u", kind, &length, irqs_text, &period) != 4) {
            continue;
        }
        unsigned k = !strcmp(kind, "adsnd")    ? XEEN_VOICE_MODEL_ADLIB
                     : !strcmp(kind, "source") ? XEEN_VOICE_MODEL_SOURCE
                     : !strcmp(kind, "ibmsnd") ? XEEN_VOICE_MODEL_PCSPEAKER
                                               : 0;
        if (!k) {
            continue;
        }
        voc[0x1b] = (uint8_t)length;
        voc[0x1c] = (uint8_t)(length >> 8);
        uint32_t count = xeen_voc_driver_count(voc, sizeof voc, k, 0);
        if (k == XEEN_VOICE_MODEL_PCSPEAKER) {
            /* Phase 287: ibmsnd's start returns after IRQS interrupts of 81 cycles (the hold), then never busy. */
            unsigned long irqs = strtoul(irqs_text, NULL, 10);
            if (count != irqs || period != XEEN_VOC_PCSPEAKER_SAMPLE_PIT ||
                xeen_voc_driver_pit(voc, sizeof voc, k, 0) != (uint64_t)irqs * period ||
                xeen_voc_busy_at_kind(100, 100, count, k) || xeen_voc_busy_at_kind(100, 101, count, k) ||
                !xeen_voc_driver_marks_music(k)) {
                fprintf(stderr, "ibmsnd L %u: native count %u, original %lu interrupts of %u cycles\n", length,
                        (unsigned)count, irqs, period);
                ++failures;
            }
            ++rows;
            continue;
        }
        if (!strcmp(irqs_text, "never")) {
            CHECK(count == UINT32_MAX && xeen_voc_busy_at_kind(0, 1000000, count, k));
            ++rows;
            continue;
        }
        unsigned long irqs = strtoul(irqs_text, NULL, 10);
        uint64_t pit = (uint64_t)irqs * period, busy_tick = (pit - 1u) / XEEN_BIOS_TICK_PIT,
                 idle_tick = (pit + XEEN_BIOS_TICK_PIT - 1u) / XEEN_BIOS_TICK_PIT;
        if (count != irqs || period != (k == XEEN_VOICE_MODEL_ADLIB ? XEEN_VOC_ADLIB_SAMPLE_PIT : XEEN_VOC_SOURCE_IRQ_PIT) ||
            !xeen_voc_busy_at_kind(100, 100 + busy_tick, count, k) || xeen_voc_busy_at_kind(100, 100 + idle_tick, count, k)) {
            fprintf(stderr, "%s L %u: native count %u, original %lu interrupts of %u cycles\n", kind, length,
                    (unsigned)count, irqs, period);
            ++failures;
        }
        ++rows;
    }
    fclose(f);
    printf("phase286 voice: %u cases, %d failures\n", rows, failures);
    return failures != 0 || rows < 270u; /* Phase 287: the ibmsnd rows too */
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "voice")) {
        return voice_checks(argv[2]);
    }
    if (argc == 3 && !strcmp(argv[1], "block")) {
        return block_checks(argv[2]);
    }
    if (argc == 3 && !strcmp(argv[1], "voiceflag")) {
        if (!open_archives(argv[2])) {
            fprintf(stderr, "voiceflag: no XEEN.CC/DARK.CC/INTRO.CC in %s (skipped)\n", argv[2]);
            return 2;
        }
        return voiceflag_checks();
    }
    if (argc < 4 || (strcmp(argv[1], "engine") && strcmp(argv[1], "sc55"))) {
        fprintf(stderr, "usage: %s engine|sc55 DOS ROMS\n", argv[0]);
        return 1;
    }
    if (!open_archives(argv[2])) {
        fprintf(stderr, "no XEEN.CC/DARK.CC/INTRO.CC in %s: skipped\n", argv[2]);
        return 2;
    }
    return engine_checks(argv[3], !strcmp(argv[1], "sc55"));
}
