/* SPDX-License-Identifier: GPL-3.0-or-later
 */
/* Phase 308: the integrated movie player's host (host_movie.h). */
#include "host_movie.h"
#include "host_log.h"
#include <stdio.h>
#include <string.h>

#define PIT_HZ 1193182ull
#define NS_PER_S 1000000000ull

typedef struct {
    const host_movie_context *ctx;
    int aborted;
} movie_host;

static uint64_t ns_to_pit(uint64_t ns)
{
    return ns / NS_PER_S * PIT_HZ + ns % NS_PER_S * PIT_HZ / NS_PER_S;
}

static uint64_t host_now(void *u)
{
    movie_host *h = u;
    return ns_to_pit(xeen_platform_clock_ns(h->ctx->platform));
}

/* Waits until PIT, its events queued (xeen_platform_wait pumps); a close request ends the wait early (the next
 * poll sees it). A headless platform has no autonomous time: the movie plays on its virtual clock there. */
static void host_wait_until(void *u, uint64_t pit)
{
    movie_host *h = u;
    for (;;) {
        uint64_t now = host_now(h);
        uint64_t ms;
        if (now >= pit || xeen_platform_should_close(h->ctx->platform) || h->ctx->headless) {
            return;
        }
        ms = (pit - now) * 1000u / PIT_HZ;
        xeen_platform_wait(h->ctx->platform, ms > 5u ? 5u : ms ? (unsigned)ms : 1u);
    }
}

/* The former SDL platform's two drains (platform_escape, platform_pending_key): every queued key is read; the
 * global abort (the window closed, or Ctrl+Q: the movie's menu-quit query answers yes) takes priority. Keys are
 * the game's keyboard map's (a key the game has no code for is not a key here either). */
static int host_poll(void *u, int any_key)
{
    movie_host *h = u;
    xeen_platform *p = h->ctx->platform;
    uint16_t key;
    int r = 0;
    xeen_platform_pump(p);
    /* The provider answers 1 with key 0 once the queue is empty (input_poll.c queue_keyboard). */
    for (unsigned n = 0; n < 64u && h->ctx->provider.keyboard && h->ctx->provider.keyboard(h->ctx->provider.user, &key) &&
                         key;
         ++n) {
        if (key == 0x1bu || any_key) {
            r = 1;
        }
    }
    if (xeen_platform_should_close(p)) {
        h->aborted = 1;
        return 2;
    }
    return r;
}

static int host_present(void *u, const uint8_t *pixels, const uint8_t *dac)
{
    movie_host *h = u;
    return xeen_platform_present(h->ctx->platform, pixels, dac);
}

static int host_write_cfg(void *u, const uint8_t *bytes, size_t n)
{
    const xeen_save_files *f = u;
    return f && f->write && f->write(f->user, "XEEN.CFG", bytes, n);
}

static void host_log(void *u, const char *text)
{
    (void)u;
    host_report(HOST_INFO, "%s", text);
}

/* Ctrl+Q during a movie is XEEN.EXE's global abort (1ec89: exit 1, no hand-off). */
static int movie_quit_query(void *u)
{
    (void)u;
    return 1;
}

xeen_movie_outcome host_movie_play(const host_movie_context *ctx, const char *command, int first,
                                   xeen_movie_report *report)
{
    static movie_host h;
    xeen_movie_services s;
    xeen_archive intro, xeen, dark;
    const host_movie_developer *dev = ctx->developer;
    const char *sequence = first && dev && dev->sequence ? dev->sequence : NULL;
    xeen_movie_outcome o;
    if (first && dev && dev->command) {
        command = dev->command;
    }
    memset(&s, 0, sizeof s);
    h.ctx = ctx;
    h.aborted = 0;
    s.host = (xeen_movie_host){&h, host_present, host_poll, host_now, host_wait_until};
    s.log = host_log;
    if (ctx->intro && xeen_archive_open(&intro, ctx->intro, ctx->intro_size) == XR_OK) {
        s.intro = &intro;
    }
    if (ctx->xeen && xeen_archive_open(&xeen, ctx->xeen, ctx->xeen_size) == XR_OK) {
        s.xeen = &xeen;
    }
    if (ctx->dark && xeen_archive_open(&dark, ctx->dark, ctx->dark_size) == XR_OK) {
        s.dark = &dark;
    }
    s.audio = ctx->audio;
    s.music_fading = ctx->music_fading;
    s.music_user = ctx->music_user;
    s.xeen_cfg = ctx->xeen_cfg;
    s.xeen_cfg_size = ctx->xeen_cfg_size;
    s.write_cfg = host_write_cfg;
    s.cfg_user = (void *)ctx->save_files;
    s.subtitles = xeen_movie_subtitles(ctx->wox, ctx->xeen_cfg, ctx->xeen_cfg_size);
    s.music_selector = xeen_movie_music_selector(ctx->wox, ctx->xeen_cfg, ctx->xeen_cfg_size);
    s.data_dir = ctx->data_dir;
    s.cue = ctx->cue;
    s.cd_last_track = ctx->cd_last_track;
    /* A headless platform has no autonomous time: the virtual clock (as the standalone player's dump runs). */
    s.virtual_clock = ctx->headless;
    if (dev) {
        s.dump = dev->dump;
        s.hash_only = dev->dump != NULL;
        s.timing_only = dev->timing_only;
        s.dos_date = dev->dos_date;
        s.dos_time = dev->dos_time;
        s.original_timer_quirks = dev->original_timer_quirks;
        s.legacy_timing = dev->legacy_timing;
        s.score_given = dev->score_given;
        s.score = dev->score;
        s.payload = dev->payload;
        s.badge_given = dev->badge_given;
        s.badge = dev->badge;
    }
    host_report(HOST_INFO, "movie: XEENCOMMAND %s%s%s (subtitles %d, music selector %u)",
                sequence ? "sequence " : command ? "=" : "absent", sequence ? sequence : "", sequence ? "" : command ? command : "",
                s.subtitles, s.music_selector);
    /* XEEN.EXE starts its own driver (1eddc: entry 0); after a hand-off the game's driver must not carry over (the
     * engine is one instance: its entry 3 notes-off precede the fresh start). The program's first movie meets a
     * freshly opened engine. */
    if (!first && ctx->audio && ctx->audio->music_restart) {
        ctx->audio->music_restart(ctx->audio->context);
    }
    xeen_platform_set_menu_quit_query(ctx->platform, movie_quit_query, NULL);
    o = xeen_movie_play(sequence ? NULL : command, sequence, &s, report);
    /* XEEN.EXE's common cleanup and XEEN.DAT's fresh driver (14cf7: 1ee18(0), 1ed49's entry 3; 15CDB's entry 0): a
     * held note or a running effect program of the movie never carries into the game. */
    if (ctx->audio) {
        if (ctx->audio->music) {
            ctx->audio->music(ctx->audio->context, 0);
        }
        if (ctx->audio->music_restart) {
            ctx->audio->music_restart(ctx->audio->context);
        }
    }
    host_report(o == XEEN_MOVIE_COMPLETED || o == XEEN_MOVIE_SKIPPED || o == XEEN_MOVIE_ABORTED ? HOST_INFO : HOST_ERROR,
                "movie: %s %s (%llu frames%s%s)%s%s", report->sequence ? report->sequence : "-",
                xeen_movie_outcome_name(o), (unsigned long long)report->presents,
                report->short_intro ? ", short intro" : "", report->cfg_written ? ", xeen.cfg written" : "",
                report->diagnostic[0] ? ": " : "", report->diagnostic);
    return o;
}
