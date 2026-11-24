/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Coordinate indexed frames, palettes, speech, music, clocks and input for scene scripts. The
 * cine entry points share one Cinematic state with player, scene modules and player_host;
 * optional diagnostics record that same invocation. Phase 308: speech, music and effects go to
 * the game's audio sink (xeen_audio_sink), which interprets the configured original music
 * driver on its own clock; this file no longer sequences music.
 */
#include "audio/file_io.h"
#include "runtime.h"
#include "player_host.h"
#include "sha256.h"
#include "telemetry.h"
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <time.h>
#define CINE_PIT_HZ 1193182
#define CINE_PIT_HZ_U64 1193182ULL
#define CINE_SERVICE_PIT_CYCLES 16390
#define CINE_POLL_PIT_CYCLES 1193
#define CINE_VGA_DOT_HZ 25175000
#define CINE_VGA_DOTS_PER_LINE 800
#define CINE_VGA_LINES 449
#define CINE_LEGACY_RETRACE_MILLIHZ 70086
#define CINE_LEGACY_PIT_HZ_X1000 1193182000ULL
#define CINE_PATH_BYTES 4096

#ifdef _WIN32
#include <windows.h>
#endif
/* Synchronize the cinematic clock with the host and advance the active sequencer. Borrows the
 * initialized cinematic; virtual time is not debited here. Updates cached time and music state
 * without returning a status.
 */
void cine_sync(Cinematic *cinematic)
{
    if (cinematic->approximate) {
        pc_clock_sync(&cinematic->pc_clock,
                      cinematic->pc_clock.virtual_time ? 0 : platform_cycles());
        cinematic->now = cinematic->pc_clock.elapsed;
    } else if (!cinematic->dump) {
        cinematic->now = platform_cycles();
    }
}

/* Perform one clock read and advance music to the resulting time. Borrows the cinematic; site
 * is a reserved diagnostic tag currently ignored. Returns 1 on success, or 0 and marks
 * playback failed on clock overflow.
 */
int cine_clock_read(Cinematic *cinematic, uint32_t site)
{
    (void)site;
    if (!cinematic->approximate) {
        cine_sync(cinematic);
        return 1;
    }
    if (!pc_clock_read(&cinematic->pc_clock,
                       cinematic->pc_clock.virtual_time ? 0 : platform_cycles())) {
        cinematic->result = XEEN_FAILED;
        return 0;
    }
    cinematic->now = cinematic->pc_clock.elapsed;
    return 1;
}

/* Initialize approximate timing from borrowed date/time strings and a real or virtual clock
 * selection. Missing live values use the host calendar; virtual defaults use midnight on the
 * default date. Returns 1 after the initial read, or 0 on invalid input or clock failure.
 */
int cine_clock_start(Cinematic *cinematic, int virtual_time, const char *date, const char *dos_time)
{
    char date_buf[32];
    char time_buf[32];
    if (!virtual_time && (!date || !dos_time)) {
        time_t wall;
        unsigned hundredths;
        struct tm *local;
#ifdef _WIN32
        {
            FILETIME ft;
            ULARGE_INTEGER stamp;
            GetSystemTimeAsFileTime(&ft);
            stamp.LowPart = ft.dwLowDateTime;
            stamp.HighPart = ft.dwHighDateTime;
            wall = (time_t)(stamp.QuadPart / 10000000ULL - 11644473600ULL);
            hundredths = (unsigned)(stamp.QuadPart / 100000ULL % 100);
        }
#else
        {
            struct timespec stamp;
            if (timespec_get(&stamp, TIME_UTC) != TIME_UTC) {
                return 0;
            }
            wall = stamp.tv_sec;
            hundredths = (unsigned)(stamp.tv_nsec / 10000000);
        }
#endif
        local = localtime(&wall);
        if (!local) {
            return 0;
        }
        snprintf(date_buf, sizeof(date_buf), "%04u-%02u-%02u", (unsigned)local->tm_year + 1900,
                 (unsigned)local->tm_mon + 1, (unsigned)local->tm_mday);
        snprintf(time_buf, sizeof(time_buf), "%02u:%02u:%02u.%02u", (unsigned)local->tm_hour,
                 (unsigned)local->tm_min, (unsigned)local->tm_sec, hundredths);
        if (!date) {
            date = date_buf;
        }
        if (!dos_time) {
            dos_time = time_buf;
        }
    }
    if (!pc_clock_init(&cinematic->pc_clock, virtual_time, virtual_time ? 0 : platform_cycles(),
                       date ? date : "2000-01-01", dos_time ? dos_time : "00:00:00.00")) {
        return 0;
    }
    cinematic->approximate = 1;
    cinematic->now = cinematic->reset = cinematic->clock_start = 0;
    if (xeen_trace_enabled()) {
        xeen_trace_main("clock-start", platform_cycles(), cinematic->pc_clock.host_origin,
                        cinematic->pc_clock.bios_base, cinematic->pc_clock.divisor, NULL);
    }
    /* Sample BIOS time before starting the cinematic service clock. */
    if (!cine_clock_read(cinematic, 0x10110)) {
        return 0;
    }
    return 1;
}

/* Read packed hour/minute and second/hundredth words into caller outputs. The accumulator
 * input is ignored; callers retain their selector accumulator separately. Borrows the
 * cinematic, advances its clock, and returns 1 on success or 0 on clock failure.
 */
int cine_dos_time(Cinematic *cinematic, uint16_t previous_accumulator, uint16_t *time_word_high,
                  uint16_t *time_word_low)
{
    (void)previous_accumulator; /* Reading DOS time leaves the low accumulator byte unchanged. */
    if (cinematic->approximate) {
        if (!cine_clock_read(cinematic, 0x29dfb)) {
            return 0;
        }
        pc_clock_time(&cinematic->pc_clock, time_word_high, time_word_low);
    } else {
        cine_sync(cinematic);
        queen_clock(cinematic->clock_origin, cinematic->now - cinematic->clock_start,
                    time_word_high, time_word_low);
    }
    return 1;
}

/* Append a present index, site, kind and loop-state tuple to the optional coverage stream.
 * Borrows all inputs; no output is produced when coverage is disabled. Stream errors are
 * detected when the cinematic closes.
 */
void cine_coverage_state(Cinematic *cinematic, uint32_t site, const char *kind, unsigned a,
                         unsigned b)
{
    if (cinematic->coverage) {
        fprintf(cinematic->coverage, "%" PRIu64 ",%x,%s,%u,%u\n", cinematic->presents, site, kind,
                a, b);
    }
}

/* Read the clock and save the secondary timer baseline in the borrowed cinematic. A failed
 * read marks playback failed and leaves the baseline unchanged.
 */
int cine_secondary_reset(Cinematic *cinematic)
{
    if (!cine_clock_read(cinematic, 0x25fb3)) {
        return 0;
    }
    cinematic->secondary_reset_tick = cine_tick(cinematic);
    return 1;
}

/* Read the clock and save the middle timer baseline in the borrowed cinematic. A failed read
 * marks playback failed and leaves the baseline unchanged.
 */
int cine_middle_reset(Cinematic *cinematic)
{
    if (!cine_clock_read(cinematic, 0x25f8b)) {
        return 0;
    }
    cinematic->middle_reset_tick = cine_tick(cinematic);
    return 1;
}

/* Project tick subtraction to the timer word, preserving the special full-wrap sentinel. Takes
 * arithmetic tick values and returns the projected word without allocation or side effects.
 */
static uint16_t cine_elapsed_timer_word(uint64_t now, uint64_t reset)
{
    uint16_t delta = (uint16_t)(now - reset);
    return !delta && (uint16_t)(now >> 16) != (uint16_t)(reset >> 16) ? UINT16_MAX : delta;
}

/* Read elapsed ticks since the middle timer reset using timer-word wrap rules. Borrows and
 * advances the cinematic; returns zero and marks playback failed if the clock read fails.
 */
uint64_t cine_middle_elapsed(Cinematic *cinematic)
{
    if (!cine_clock_read(cinematic, 0x25f9d)) {
        return 0;
    }
    return cinematic->approximate
               ? cine_elapsed_timer_word(cine_tick(cinematic), cinematic->middle_reset_tick)
               : (uint16_t)(cine_tick(cinematic) - cinematic->middle_reset_tick);
}

/* Read elapsed ticks since the secondary timer reset using timer-word wrap rules. Borrows and
 * advances the cinematic; returns zero and marks playback failed if the clock read fails.
 */
uint64_t cine_secondary_elapsed(Cinematic *cinematic)
{
    if (!cine_clock_read(cinematic, 0x25fc5)) {
        return 0;
    }
    return cinematic->approximate
               ? cine_elapsed_timer_word(cine_tick(cinematic), cinematic->secondary_reset_tick)
               : (uint16_t)(cine_tick(cinematic) - cinematic->secondary_reset_tick);
}

/* Initialize caller-owned cinematic storage for live or headless playback. Borrows the dump
 * path, opens an owned timing stream when requested, and initializes the platform for live
 * use. Returns 1 on success or 0 on failure; call cine_close to release any attached partial
 * resources.
 */
int cine_open(Cinematic *cinematic, const char *dump)
{
    char path[CINE_PATH_BYTES];
    memset(cinematic, 0, sizeof(*cinematic));
    cinematic->dump = dump;
    cinematic->result = XEEN_FAILED;
    queen_selector_init(&cinematic->selector);
    if (dump) {
        if (snprintf(path, sizeof(path), "%s/timestamps.csv", dump) >= (int)sizeof(path) ||
            (cinematic->times = xeen_fopen(path, "wb")) == NULL) {
            player_log("movie: the dump directory must exist");
            return 0;
        }
        fprintf(cinematic->times, "frame,pit_cycles,tick,seconds\n");
    } else {
        cinematic->opened = 1;
        if (!platform_open()) {
            player_log("movie: no window to play in");
            return 0;
        }
        cinematic->now = platform_cycles();
    }
    cinematic->clock_start = cinematic->now;
    if (!dump) {
        time_t wall;
        unsigned hundredths = 0;
        struct tm *local;
#ifdef _WIN32
        {
            FILETIME ft;
            ULARGE_INTEGER stamp;
            GetSystemTimeAsFileTime(&ft);
            stamp.LowPart = ft.dwLowDateTime;
            stamp.HighPart = ft.dwHighDateTime;
            wall = (time_t)(stamp.QuadPart / 10000000ULL - 11644473600ULL);
            hundredths = (unsigned)(stamp.QuadPart / 100000ULL % 100);
        }
#else
        {
            struct timespec stamp;
            if (timespec_get(&stamp, TIME_UTC) == TIME_UTC) {
                wall = stamp.tv_sec;
                hundredths = (unsigned)(stamp.tv_nsec / 10000000);
            } else {
                wall = time(NULL);
            }
        }
#endif
        local = localtime(&wall);
        if (local) {
            cinematic->clock_origin = ((unsigned)local->tm_hour * 3600 +
                                       (unsigned)local->tm_min * 60 + (unsigned)local->tm_sec) *
                                          100ULL +
                                      hundredths;
        }
    }
    return 1;
}

/* Stop the movie's voice and close its diagnostic streams while retaining video.
 * Borrows initialized storage; close errors mark failure. Safe to call again during final
 * cine_close after a modal error dialog returns. */
int cine_close_services(Cinematic *cinematic)
{
    /* Phase 308: the game owns the audio engine; a movie leaves its music driver as the
     * original's 14cf7 cleanup leaves it (the game's 15CDB start loads its own song) and only
     * stops a voice it started. */
    if (cinematic->opened && cinematic->audio && cinematic->audio->voice_stop) {
        cinematic->audio->voice_stop(cinematic->audio->context);
    }
    if (cinematic->times && fclose(cinematic->times)) {
        cinematic->result = XEEN_FAILED;
    }
    if (cinematic->events && fclose(cinematic->events)) {
        cinematic->result = XEEN_FAILED;
    }
    if (cinematic->hashes && fclose(cinematic->hashes)) {
        cinematic->result = XEEN_FAILED;
    }
    if (cinematic->coverage && fclose(cinematic->coverage)) {
        cinematic->result = XEEN_FAILED;
    }
    cinematic->hashes = cinematic->coverage = NULL;
    cinematic->times = cinematic->events = NULL;
    return cinematic->result;
}

/* Release media first, then destroy any retained video/platform. Safe after services were
 * already closed to hold the final frame during an executable error dialog. */
int cine_close(Cinematic *cinematic)
{
    cine_close_services(cinematic);
    if (cinematic->opened) {
        platform_close();
    }
    cinematic->opened = 0;
    return cinematic->result;
}

/* Read an archive member into a caller-owned Blob, with optional tracing and an archive-aware
 * failure diagnostic. Borrows the cinematic, archive and name. Returns 1 on success or 0 on
 * read failure; the caller frees a successful Blob.
 */
int cine_load(CC *cc, const char *name, Blob *b)
{
    uint64_t start = xeen_trace_enabled() ? platform_cycles() : 0;
    int ok = cc_read(cc, name, b);
    if (xeen_trace_enabled()) {
        xeen_trace_main("decode-load", platform_cycles(), start, ok ? b->size : 0, 0, name);
    }
    if (!ok) {
        player_log("movie: cannot read %s from its archive (possible floppy/CD-ROM version mismatch)", name);
    }
    return ok;
}

/* Read the clock and set the primary timer baseline and cached reset time. Borrows the
 * cinematic; a failed read marks playback failed and leaves the baseline unchanged.
 */
void cine_reset(Cinematic *cinematic)
{
    if (!cine_clock_read(cinematic, 0x25f63)) {
        return;
    }
    cinematic->primary_reset_tick = cine_tick(cinematic);
    cinematic->reset = cinematic->now;
}

/* Initialize the ending timer service at the current cinematic time. Borrows and updates the
 * cinematic; returns 0 for null state or an already initialized legacy timeline, otherwise the
 * clock initialization status. Approximate mode programs its divisor without setting the
 * legacy timeline flag.
 */
int cine_ending_timeline_start(Cinematic *cinematic)
{
    if (!cinematic || cinematic->ending_timeline) {
        return 0;
    }
    if (cinematic->approximate) {
        int ok = pc_clock_program(&cinematic->pc_clock, CINE_SERVICE_PIT_CYCLES);
        if (xeen_trace_enabled()) {
            xeen_trace_main("clock-program", platform_cycles(),
                            cinematic->pc_clock.host_origin + cinematic->pc_clock.pit_origin,
                            cinematic->pc_clock.bios_base, cinematic->pc_clock.divisor, NULL);
        }
        return ok;
    }
    if (!cinematic->dump) {
        cinematic->now = platform_cycles();
    }
    if (cinematic->now > UINT64_MAX - CINEMATIC_CLOCK_QUANTUM ||
        !cinematic_clock_init(&cinematic->ending_clock, cinematic->now + CINEMATIC_CLOCK_QUANTUM, 0,
                              CINEMATIC_CLOCK_INITIAL_CARRY)) {
        return 0;
    }
    cinematic->ending_timeline = 1;
    return 1;
}

/* Return cached BIOS ticks without performing a fresh clock read. Borrows initialized
 * cinematic state; approximate time wraps daily, while legacy ending time projects the
 * carry-clock advances.
 */
uint64_t cine_tick(Cinematic *cinematic)
{
    CinematicClockSample sample;
    if (cinematic->approximate) {
        return pc_clock_bios(&cinematic->pc_clock);
    }
    if (!cinematic->ending_timeline) {
        return tick_at(cinematic->now);
    }
    return cinematic_clock_read(&cinematic->ending_clock, cinematic->now, &sample)
               ? sample.bios_advances
               : 0;
}

/* Convert a legacy tick interval and reset baseline to an absolute time. Borrows cinematic
 * state and returns the deadline, or the maximum integer when the carry-clock deadline is
 * unreachable.
 */
static uint64_t cine_deadline(Cinematic *cinematic, unsigned ticks)
{
    uint64_t deadline;
    if (!cinematic->ending_timeline) {
        return tick_deadline(cinematic->reset, ticks);
    }
    return cinematic_clock_deadline(&cinematic->ending_clock, cinematic->reset, ticks, &deadline)
               ? deadline
               : UINT64_MAX;
}

/* Read primary elapsed ticks and update the selector accumulator in approximate mode. Borrows
 * and advances the cinematic; returns the timer-word projection there or the legacy elapsed
 * count. Clock errors mark playback failed.
 */
uint64_t cine_elapsed(Cinematic *cinematic)
{
    uint64_t elapsed;
    if (cinematic->approximate) {
        if (!cine_clock_read(cinematic, 0x25f75)) {
            return 0;
        }
        cinematic->selector_accumulator =
            cine_elapsed_timer_word(cine_tick(cinematic), cinematic->primary_reset_tick);
        return cinematic->selector_accumulator;
    }
    cine_sync(cinematic);
    if (!cinematic->ending_timeline) {
        return tick_at(cinematic->now) - tick_at(cinematic->reset);
    }
    return cinematic_clock_elapsed(&cinematic->ending_clock, cinematic->reset, cinematic->now,
                                   &elapsed)
               ? elapsed
               : 0;
}

/* Read primary elapsed time and compare it with the requested tick threshold. Borrows and
 * advances the cinematic; approximate mode uses a signed timer-word comparison and legacy mode
 * uses the full elapsed count.
 */
int cine_elapsed_before(Cinematic *cinematic, unsigned ticks)
{
    uint64_t elapsed = cine_elapsed(cinematic);
    return cinematic->approximate ? (int16_t)(uint16_t)elapsed < (int)ticks : elapsed < ticks;
}

/* Poll live input and update the borrowed cinematic result on Escape or global abort. Returns
 * 1 to continue, including headless mode, or 0 to stop. Escape skips playback; global abort
 * marks failure and records the abort.
 */
int cine_poll(Cinematic *cinematic)
{
    int key;
    if (cinematic->dump) {
        return 1;
    }
    key = platform_escape();
    if (!key) {
        return 1;
    }
    cinematic->result = key == 1 ? XEEN_SKIPPED : XEEN_FAILED;
    if (key != 1) {
        cinematic->aborted = 1;
    }
    return 0;
}

/* Wait until an absolute time, optionally polling input, while servicing music. Borrows and
 * updates the cinematic; headless legacy mode advances directly to the deadline. Returns 1
 * after waiting or 0 on a clock or input stop.
 */
int cine_wait_cycles(Cinematic *cinematic, uint64_t deadline, int poll)
{
    uint64_t trace_start = xeen_trace_now();
    if (cinematic->approximate) {
        do {
            if (poll && !cine_poll(cinematic)) {
                return 0;
            }
            if (!cine_clock_read(cinematic, 0x25f75)) {
                return 0;
            }
            if (!cinematic->pc_clock.virtual_time && cinematic->now < deadline) {
                platform_wait(cinematic->pc_clock.host_origin + cinematic->now +
                              CINE_POLL_PIT_CYCLES);
            }
        } while (cinematic->now < deadline);
        if (xeen_trace_enabled()) {
            xeen_trace_main("wait-cycles", platform_cycles(),
                            cinematic->pc_clock.host_origin + deadline, trace_start, 0, NULL);
        }
        return 1;
    }
    if (cinematic->dump) {
        if (cinematic->now < deadline) {
            cinematic->now = deadline;
        }
        return 1;
    }
    if (!poll) {
        platform_wait(deadline);
    } else {
        while (platform_cycles() < deadline) {
            uint64_t next;
            if (poll && !cine_poll(cinematic)) {
                return 0;
            }
            next = platform_cycles() + CINE_POLL_PIT_CYCLES;
            if (next > deadline) {
                next = deadline;
            }
            platform_wait(next);
        }
    }
    cinematic->now = platform_cycles();
    return !poll || cine_poll(cinematic);
}

/* Wait for primary elapsed ticks while servicing the clock and music, optionally polling
 * input. Borrows and updates the cinematic and returns the wait status. Clock failures remain
 * recorded in its result.
 */
int cine_wait_ticks(Cinematic *cinematic, unsigned ticks, int poll)
{
    uint64_t trace_start = xeen_trace_now();
    if (cinematic->approximate) {
        while ((int16_t)(uint16_t)cine_elapsed(cinematic) < (int)ticks) {
            if (poll && !cine_poll(cinematic)) {
                return 0;
            }
            if (!cinematic->pc_clock.virtual_time) {
                platform_wait(cinematic->pc_clock.host_origin + cinematic->now +
                              CINE_POLL_PIT_CYCLES);
            }
        }
        if (xeen_trace_enabled()) {
            xeen_trace_main("wait-ticks", platform_cycles(), trace_start, ticks,
                            cinematic->primary_reset_tick, NULL);
        }
        return !poll || cine_poll(cinematic);
    }
    return cine_wait_cycles(cinematic, cine_deadline(cinematic, ticks), poll);
}

/* Wait for a key or the requested hold interval while servicing playback. Borrows the
 * cinematic; headless mode waits only for the interval. Returns 1 on completion or 0 on global
 * abort or a failed underlying wait.
 */
int cine_wait_key_ticks(Cinematic *cinematic, unsigned ticks)
{
    uint64_t deadline;
    if (cinematic->approximate) {
        for (;;) {
            if (!cinematic->dump) {
                int key = platform_pending_key();
                if (key == 2) {
                    cinematic->aborted = 1;
                    cinematic->result = XEEN_FAILED;
                    return 0;
                }
                if (key) {
                    return 1;
                }
            }
            if ((int16_t)(uint16_t)cine_elapsed(cinematic) >= (int)ticks) {
                return 1;
            }
            if (!cinematic->pc_clock.virtual_time) {
                platform_wait(cinematic->pc_clock.host_origin + cinematic->now +
                              CINE_POLL_PIT_CYCLES);
            }
        }
    }
    deadline = cine_deadline(cinematic, ticks);
    if (cinematic->dump) {
        return cine_wait_cycles(cinematic, deadline, 0);
    }
    while (platform_cycles() < deadline) {
        int key = platform_pending_key();
        uint64_t next;
        if (key == 2) {
            cinematic->aborted = 1;
            cinematic->result = XEEN_FAILED;
            return 0;
        }
        if (key) {
            cinematic->now = platform_cycles();
            return 1;
        }
        next = platform_cycles() + CINE_POLL_PIT_CYCLES;
        if (next > deadline) {
            next = deadline;
        }
        platform_wait(next);
    }
    cinematic->now = platform_cycles();
    return 1;
}

/* Wait for the requested interval while discarding ordinary keys and Escape. Borrows and
 * updates cinematic timing and music; returns 1 on completion or 0 on global abort or a failed
 * underlying wait.
 */
int cine_wait_fire_ticks(Cinematic *cinematic, unsigned ticks)
{
    uint64_t deadline;
    if (cinematic->approximate) {
        for (;;) {
            if (!cinematic->dump && platform_pending_key() == 2) {
                cinematic->aborted = 1;
                cinematic->result = XEEN_FAILED;
                return 0;
            }
            if ((int16_t)(uint16_t)cine_elapsed(cinematic) >= (int)ticks) {
                return 1;
            }
            if (!cinematic->pc_clock.virtual_time) {
                platform_wait(cinematic->pc_clock.host_origin + cinematic->now +
                              CINE_POLL_PIT_CYCLES);
            }
        }
    }
    deadline = cine_deadline(cinematic, ticks);
    if (cinematic->dump) {
        return cine_wait_cycles(cinematic, deadline, 0);
    }
    while (platform_cycles() < deadline) {
        uint64_t next;
        if (platform_pending_key() == 2) {
            cinematic->aborted = 1;
            cinematic->result = XEEN_FAILED;
            return 0;
        }
        next = platform_cycles() + CINE_POLL_PIT_CYCLES;
        if (next > deadline) {
            next = deadline;
        }
        platform_wait(next);
    }
    cinematic->now = platform_cycles();
    return 1;
}

/* Advance to the next nominal display frame while servicing the cinematic clock. Borrows and
 * updates playback state; live mode may wait on the host. Clock failures mark the cinematic
 * failed.
 */
int cine_retrace(Cinematic *cinematic)
{
    uint64_t n;
    uint64_t next;
    if (cinematic->approximate) {
        /* Wait for the next retrace using the VGA raster rates: 800 by 449
         * clocks at 25.175 MHz. In virtual mode only the clock read advances time.
         */
        cine_sync(cinematic);
        n = cinematic->now * CINE_VGA_DOT_HZ /
            (CINE_PIT_HZ_U64 * CINE_VGA_DOTS_PER_LINE * CINE_VGA_LINES);
        do {
            if (!cine_clock_read(cinematic, 0x25e2a)) {
                return 0;
            }
        } while (cinematic->now * CINE_VGA_DOT_HZ /
                     (CINE_PIT_HZ_U64 * CINE_VGA_DOTS_PER_LINE * CINE_VGA_LINES) ==
                 n);
        if (xeen_trace_enabled()) {
            xeen_trace_main(
                "retrace", platform_cycles(),
                cinematic->pc_clock.host_origin +
                    ((n + 1) * CINE_PIT_HZ_U64 * CINE_VGA_DOTS_PER_LINE * CINE_VGA_LINES +
                     CINE_VGA_DOT_HZ - 1) /
                        CINE_VGA_DOT_HZ,
                0, 0, NULL);
        }
        if (!cinematic->pc_clock.virtual_time) {
            platform_wait(cinematic->pc_clock.host_origin + cinematic->now);
        }
        return 1;
    }
    if (!cinematic->dump) {
        cinematic->now = platform_cycles();
    }
    n = cinematic->now * CINE_LEGACY_RETRACE_MILLIHZ / CINE_LEGACY_PIT_HZ_X1000 + 1;
    next = n * CINE_LEGACY_PIT_HZ_X1000 / CINE_LEGACY_RETRACE_MILLIHZ;
    if (next <= cinematic->now) {
        next = (n + 1) * CINE_LEGACY_PIT_HZ_X1000 / CINE_LEGACY_RETRACE_MILLIHZ;
    }
    return cine_wait_cycles(cinematic, next, 0);
}

/* Copy the selected saved image bank into the cinematic framebuffer. Borrows and updates the
 * cinematic; invalid bank numbers have no effect and no allocation occurs.
 */
void cine_restore(Cinematic *cinematic, unsigned bank)
{
    if (bank < 2) {
        memcpy(cinematic->pixels, cinematic->background[bank], XEEN_PIXELS);
    }
}

/* Compose the framebuffer from the tail of the first saved bank and the head of the second.
 * Borrows and updates the cinematic; row displacement is clamped to the image height.
 */
void cine_restore_vertical(Cinematic *cinematic, unsigned rows)
{
    size_t split;
    if (rows > XEEN_HEIGHT_PIXELS) {
        rows = XEEN_HEIGHT_PIXELS;
    }
    split = (size_t)rows * XEEN_WIDTH_PIXELS;
    memcpy(cinematic->pixels, cinematic->background[0] + split, XEEN_PIXELS - split);
    memcpy(cinematic->pixels + XEEN_PIXELS - split, cinematic->background[1], split);
}

/* Compose every framebuffer row from the right part of the first saved bank and the left part
 * of the second. Borrows and updates the cinematic; the split is clamped to the image width.
 */
void cine_restore_horizontal(Cinematic *cinematic, unsigned split)
{
    unsigned y;
    if (split > XEEN_WIDTH_PIXELS) {
        split = XEEN_WIDTH_PIXELS;
    }
    for (y = 0; y < XEEN_HEIGHT_PIXELS; y++) {
        memcpy(cinematic->pixels + y * XEEN_WIDTH_PIXELS,
               cinematic->background[0] + y * XEEN_WIDTH_PIXELS + split, XEEN_WIDTH_PIXELS - split);
        memcpy(cinematic->pixels + y * XEEN_WIDTH_PIXELS + XEEN_WIDTH_PIXELS - split,
               cinematic->background[1] + y * XEEN_WIDTH_PIXELS, split);
    }
}

/* Draw a borrowed sprite Blob into the cinematic framebuffer using its default position.
 * Returns 1 on success; decoder failure may leave partial pixels, marks playback failed and
 * emits a diagnostic.
 */
int cine_draw(Cinematic *cinematic, const Blob *b, unsigned frame)
{
    uint64_t start = xeen_trace_enabled() ? platform_cycles() : 0;
    int ok = sprite_draw(b, frame, cinematic->pixels);
    if (xeen_trace_enabled()) {
        xeen_trace_main("compose", platform_cycles(), start, frame, 0, "draw");
    }
    if (ok) {
        return 1;
    }
    player_log("Invalid sprite frame %u", frame);
    cinematic->result = XEEN_FAILED;
    return 0;
}

/* Draw a borrowed sprite Blob at the requested position in the cinematic framebuffer. Returns
 * 1 on success; decoder failure may leave partial pixels, marks playback failed and emits a
 * diagnostic.
 */
int cine_draw_at(Cinematic *cinematic, const Blob *b, unsigned frame, int x, int y, unsigned scale)
{
    uint64_t start = xeen_trace_enabled() ? platform_cycles() : 0;
    int ok = sprite_draw_scaled(b, frame, x, y, scale, cinematic->pixels);
    if (xeen_trace_enabled()) {
        xeen_trace_main("compose", platform_cycles(), start, frame, 0, "scaled");
    }
    if (ok) {
        return 1;
    }
    player_log("Invalid scaled sprite frame %u scale %u", frame, scale);
    cinematic->result = XEEN_FAILED;
    return 0;
}

/* Draw a borrowed sprite Blob with the requested effect into the cinematic framebuffer.
 * Returns 1 on success; decoder failure may leave partial pixels, marks playback failed and
 * emits a diagnostic.
 */
int cine_draw_effect(Cinematic *cinematic, const Blob *b, unsigned frame, int x, int y,
                     unsigned flags)
{
    uint64_t start = xeen_trace_enabled() ? platform_cycles() : 0;
    int ok = sprite_draw_effect(b, frame, x, y, 0, flags, cinematic->pixels);
    if (xeen_trace_enabled()) {
        xeen_trace_main("compose", platform_cycles(), start, frame, 0, "effect");
    }
    if (ok) {
        return 1;
    }
    player_log("Invalid effect sprite frame %u flags %x", frame, flags);
    cinematic->result = XEEN_FAILED;
    return 0;
}

/* Append a timing row and borrowed label to the optional timestamp stream. Borrows cinematic
 * state and allocates nothing; stream errors are detected at close.
 */
void cine_record(Cinematic *cinematic, const char *label)
{
    if (cinematic->times) {
        fprintf(cinematic->times, "%s,%" PRIu64 ",%" PRIu64 ",%.9f\n", label, cinematic->now,
                cine_tick(cinematic), (double)cinematic->now / (double)CINE_PIT_HZ);
    }
}

/* Write borrowed bytes to a named dump sidecar and close the temporary output stream. Returns
 * 1 on success or 0 on path, open, write or close failure; a partial output file can remain.
 */
static int cine_dump_sidecar(const char *directory, const char *stem, const char *suffix,
                             const uint8_t *data, size_t size)
{
    char path[CINE_PATH_BYTES];
    FILE *f;
    int ok;
    if (snprintf(path, sizeof(path), "%s/%s.%s", directory, stem, suffix) >= (int)sizeof(path) ||
        (f = xeen_fopen(path, "wb")) == NULL) {
        return 0;
    }
    ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

/* Convert a borrowed digest to lowercase hexadecimal in the caller's output buffer, including
 * the terminator. The output must hold the complete digest text; no allocation or failure
 * status is used.
 */
static void cine_digest_hex(const uint8_t d[32], char out[65])
{
    static const char h[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; i++) {
        out[2 * i] = h[d[i] >> 4];
        out[2 * i + 1] = h[d[i] & 15];
    }
    out[64] = 0;
}

/* Append hashes for the current image, palette and rendered output to the optional hash
 * stream. Borrows and updates the cinematic; returns 1 when disabled or successful, or 0 and
 * marks failure on output errors.
 */
static int cine_write_present_hashes(Cinematic *cinematic, const char *file_stem)
{
    if (cinematic->hashes) {
        XeenSha256 hash;
        XeenSha256 raw;
        uint8_t digest[32];
        uint8_t row[960];
        uint8_t dac[XEEN_PALETTE_BYTES];
        char pp[65];
        char idx[65];
        char pal[65];
        char pal8[65];
        char raw_rgb[65];
        static const char header[] = "P6\n320 200\n255\n";
        xeen_sha256_init(&hash);
        xeen_sha256_init(&raw);
        xeen_sha256_update(&hash, header, sizeof(header) - 1);
        for (unsigned y = 0; y < XEEN_HEIGHT_PIXELS; y++) {
            for (unsigned x = 0; x < XEEN_WIDTH_PIXELS; x++) {
                memcpy(row + x * 3,
                       cinematic->rgb + 3 * cinematic->pixels[y * XEEN_WIDTH_PIXELS + x], 3);
            }
            xeen_sha256_update(&hash, row, sizeof(row));
            xeen_sha256_update(&raw, row, sizeof(row));
        }
        xeen_sha256_finish(&hash, digest);
        cine_digest_hex(digest, pp);
        xeen_sha256_finish(&raw, digest);
        cine_digest_hex(digest, raw_rgb);
        xeen_sha256(cinematic->pixels, XEEN_PIXELS, digest);
        cine_digest_hex(digest, idx);
        for (unsigned i = 0; i < XEEN_PALETTE_BYTES; i++) {
            dac[i] = cinematic->rgb[i] >> 2;
        }
        xeen_sha256(dac, XEEN_PALETTE_BYTES, digest);
        cine_digest_hex(digest, pal);
        xeen_sha256(cinematic->rgb, XEEN_PALETTE_BYTES, digest);
        cine_digest_hex(digest, pal8);
        if (fprintf(cinematic->hashes, "%" PRIu64 ",%s,%" PRIu64 ",%s,%s,%s,%s,%s\n",
                    cinematic->presents, file_stem ? file_stem : "", cinematic->now, pp, idx, pal,
                    pal8, raw_rgb) < 0) {
            cinematic->result = XEEN_FAILED;
            return 0;
        }
    }
    return 1;
}

/* Hash and present the current image, writing requested headless dumps and timing records or
 * submitting live pixels. Borrows and updates the cinematic. Returns 1 on success or 0 on
 * failure; the present counter and partial outputs may already have advanced.
 */
int cine_present(Cinematic *cinematic, const char *file_stem, const char *csv_label)
{
    if (!cine_write_present_hashes(cinematic, file_stem)) {
        return 0;
    }
    cinematic->presents++;
    if (cinematic->dump) {
        char path[CINE_PATH_BYTES];
        if (!cinematic->hash_only && (!file_stem ||
                                      snprintf(path, sizeof(path), "%s/%s.ppm", cinematic->dump,
                                               file_stem) >= (int)sizeof(path) ||
                                      !ppm_write(path, cinematic->pixels, cinematic->rgb) ||
                                      (getenv("XEEN_DUMP_INDEXED") &&
                                       (!cine_dump_sidecar(cinematic->dump, file_stem, "idx",
                                                           cinematic->pixels, XEEN_PIXELS) ||
                                        !cine_dump_sidecar(cinematic->dump, file_stem, "pal",
                                                           cinematic->rgb, XEEN_PALETTE_BYTES))))) {
            cinematic->result = XEEN_FAILED;
            return 0;
        }
        if (csv_label) {
            cine_record(cinematic, csv_label);
        }
        return 1;
    }
    if (xeen_trace_enabled()) {
        xeen_trace_main("source-present", platform_cycles(),
                        cinematic->now +
                            (cinematic->approximate ? cinematic->pc_clock.host_origin : 0),
                        cinematic->presents, 0, file_stem);
    }
    if (!platform_present(cinematic->pixels, cinematic->rgb)) {
        cinematic->result = XEEN_FAILED;
        return 0;
    }
    return 1;
}

/* Present palette steps from the requested fade level through the endpoint, servicing retrace
 * between steps. Borrows and updates the cinematic; returns 0 for a zero increment or failed
 * present, otherwise 1. Updates the transient palette-present marker around each present.
 */
int cine_fade(Cinematic *cinematic, int in, unsigned increment, const char *prefix)
{
    unsigned remaining = 128;
    unsigned index = 0;
    if (!increment) {
        return 0;
    }
    for (;;) {
        char label[96];
        unsigned level = in ? 128 - remaining : remaining;
        palette_rgb(cinematic->palette, level, cinematic->rgb);
        if (!cine_retrace(cinematic)) {
            return 0;
        }
        snprintf(label, sizeof(label), "%s%03u", prefix, index++);
        cinematic->palette_present = 1;
        {
            int ok = cine_present(cinematic, label, label);
            cinematic->palette_present = 0;
            if (!ok) {
                return 0;
            }
        }
        if (!remaining) {
            break;
        }
        remaining = remaining > increment ? remaining - increment : 0;
    }
    return 1;
}

/* Start a borrowed decoded voice and record its cue. Tracks busy duration from the full
 * decoded sample count; caption-only approximate mode ends it immediately. Live output copies
 * the samples; an output error emits a diagnostic without changing the playback result.
 */
void cine_voice(Cinematic *cinematic, const Voice *v, const char *name)
{
    cine_sync(cinematic);
    cinematic->voice_end =
        cinematic->now + (v->rate ? ((uint64_t)v->count * CINE_PIT_HZ + v->rate - 1) / v->rate : 0);
    if (cinematic->events) {
        fprintf(cinematic->events, "voice,%s,%" PRIu64 ",%zu,%u\n", name, cinematic->now, v->count,
                v->rate);
    }
    if (cinematic->approximate && cinematic->subtitles) {
        cinematic->voice_end = cinematic->now;
        return;
    }
    if (!cinematic->dump) {
        if (xeen_trace_enabled()) {
            xeen_trace_cue(platform_cycles(),
                           cinematic->now +
                               (cinematic->approximate ? cinematic->pc_clock.host_origin : 0),
                           cinematic->presents, name);
        }
        /* Phase 308: the encoded VOC goes to the game's sink (decoded there as the configured
         * digital driver plays it); the busy model above stays this player's. */
        if (cinematic->audio && cinematic->audio->voice && v->encoded) {
            cinematic->audio->voice(cinematic->audio->context, v->encoded, v->encoded_size);
        }
    }
}

/* Synchronize the clock and report whether the modeled voice deadline is still ahead. Borrows
 * and updates the cinematic; this does not query the platform audio device.
 */
int cine_sound_busy(Cinematic *cinematic)
{
    cine_sync(cinematic);
    return cinematic->now < cinematic->voice_end;
}

/* Stop live voice output when attached and clear the modeled busy interval at the cached time.
 * Borrows and updates the cinematic without a fresh clock read.
 */
void cine_stop_voice(Cinematic *cinematic)
{
    if (cinematic->opened && cinematic->audio && cinematic->audio->voice_stop) {
        cinematic->audio->voice_stop(cinematic->audio->context);
    }
    cinematic->voice_end = cinematic->now;
}

/* Phase 308: the music services go to the game's audio sink, which runs the configured original
 * music driver (xeen_music_resolve) on its own clock. The calls keep the original's service
 * entries: entry 9 (1ed30, a driver cue) is the sink's effect, entry 6 (1ee18) its song, music
 * and volume. Without a sink they do nothing (a silent movie, as the original's nullmus). */

/* Entry 9: the driver's built-in cue COMMAND. */
void cine_music(Cinematic *cinematic, unsigned command)
{
    if (cinematic->events) {
        fprintf(cinematic->events, "music,%u,%" PRIu64 "\n", command, cinematic->now);
    }
    if (!cinematic->dump && cinematic->audio && cinematic->audio->effect) {
        cinematic->audio->effect(cinematic->audio->context, (uint16_t)command);
    }
}

/* Entry 6 with a song segment (1ee18): the archive's song NAME loaded and started. A read
 * failure does nothing. */
void cine_music_load(Cinematic *cinematic, CC *archive, const char *name)
{
    Blob b = {0};
    if (!cinematic->dump && cinematic->audio && cinematic->audio->song && cc_read(archive, name, &b)) {
        cinematic->audio->song(cinematic->audio->context, b.data, b.size);
        free(b.data);
    }
}

/* Entry 6 with a control word: 0 stop, 1 restart, 2..FFh a fade. */
void cine_music_control(Cinematic *cinematic, unsigned control)
{
    if (!cinematic->dump && cinematic->audio && cinematic->audio->music) {
        cinematic->audio->music(cinematic->audio->context, control);
    }
}

/* Poll entry 6's fade word (FFE0h) until zero, once per music-service interval (1cee4, 1e9ad).
 * No music driver: never fading. Ignores ordinary keys and Escape; returns 0 on the global
 * abort or a failed wait, otherwise 1. */
int cine_music_wait_fade(Cinematic *cinematic)
{
    while (!cinematic->dump && cinematic->music_fading &&
           cinematic->music_fading(cinematic->music_user)) {
        if (platform_pending_key() == 2) {
            cinematic->aborted = 1;
            cinematic->result = XEEN_FAILED;
            return 0;
        }
        if (!cine_wait_cycles(cinematic, cinematic->now + CINE_SERVICE_PIT_CYCLES, 0)) {
            return 0;
        }
    }
    return 1;
}

/* Entry 6 with 100h: the song volume (CC7). */
void cine_music_volume(Cinematic *cinematic, unsigned volume)
{
    if (!cinematic->dump && cinematic->audio && cinematic->audio->volume) {
        cinematic->audio->volume(cinematic->audio->context, volume);
    }
}

/* 1A515..1A544: DS 4298 (the configuration's byte 26) = 1 (a word write: byte 27 = 0), then the
 * 30 bytes from DS 427E (the 29 read, 1ef7f, and DS 429B, 0 in the initialized data and written
 * by no instruction) to xeen.cfg through the host. The original does not test the result. */
void darkside_intro_write_cfg(Cinematic *cinematic)
{
    cinematic->cfg[26] = 1;
    cinematic->cfg[27] = 0;
    if (cinematic->write_cfg) {
        cinematic->cfg_written = cinematic->write_cfg(cinematic->cfg_user, cinematic->cfg, 30);
    }
}
