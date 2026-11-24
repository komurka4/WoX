/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Resolve the requested movie, set up one Cinematic over the game's services and dispatch the
 * sequence. xeen_movie_play (include/xeen/movie.h) owns one synchronous invocation. Phase 308:
 * the standalone player's argument parser, configuration readers, music backends and handoff
 * are gone; the game's host supplies the window, input, audio sink, archives and XEEN.CFG.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "audio/file_io.h"
#include "runtime.h"
#include "player.h"
#include "player_host.h"
#include <stdlib.h>
#include <string.h>
#include "disc.h"
#include "clouds_ending.h"
#include "clouds_closing.h"
#include "darkside_ending.h"
#include "world_ending.h"

#define PLAYER_PATH_BYTES 4096
#define PLAYER_LOGO_FRAME_COUNT 80
#define PLAYER_LOGO_SECOND_BANK_FRAME 65
#define PLAYER_LEGACY_RETRACE_PIT_CYCLES 17026
#define PLAYER_DOS_GET_TIME_AX 0x2c00

/* Play the Clouds publisher logo with music, palette fades and input handling. Borrows the
 * cinematic and archive and releases temporary art before returning the cinematic result:
 * completed, failed or skipped.
 */
static int clouds_logo_play(Cinematic *cinematic, CC *archive)
{
    Blob pal = {0};
    Blob back = {0};
    Blob logo[2] = {{0}};
    Blob voc[2] = {{0}};
    Voice voice[2] = {{0}};
    unsigned frame;
    int index;
    if (!cine_load(archive, "intro1.pal", &pal) || !cine_load(archive, "logobak.raw", &back) ||
        !cine_load(archive, "logo.vga", &logo[0]) || !cine_load(archive, "logo1.vga", &logo[1]) ||
        !cine_load(archive, "fire.voc", &voc[0]) || !cine_load(archive, "meangro&.voc", &voc[1])) {
        goto done;
    }
    if (pal.size != XEEN_PALETTE_BYTES || back.size != XEEN_PIXELS ||
        !player_voc_decode(&voc[0], &voice[0]) || !player_voc_decode(&voc[1], &voice[1])) {
        player_log("Invalid palette/background/VOC");
        goto done;
    }
    memcpy(cinematic->palette, pal.data, XEEN_PALETTE_BYTES);
    memcpy(cinematic->background[0], back.data, XEEN_PIXELS);
    /* Approximate presents the background before its black and full-brightness
     * palette updates. Legacy keeps its diagnostic presentation stream.
     */
    cine_restore(cinematic, 0);
    palette_rgb(cinematic->palette, 0, cinematic->rgb);
    if (!cine_present(cinematic, "initial-black", "initial-black")) {
        goto done;
    }
    if (cinematic->approximate) {
        if (!cine_fade(cinematic, 1, 128, "initial-dac-")) {
            goto done;
        }
    } else {
        palette_rgb(cinematic->palette, 128, cinematic->rgb);
        if (!cine_wait_cycles(cinematic,
                              cinematic->dump
                                  ? PLAYER_LEGACY_RETRACE_PIT_CYCLES
                                  : platform_cycles() + PLAYER_LEGACY_RETRACE_PIT_CYCLES,
                              0) ||
            !cine_present(cinematic, "initial-full", "initial-full")) {
            goto done;
        }
    }
    cine_music(cinematic, 1);
    for (frame = 0; frame < PLAYER_LOGO_FRAME_COUNT; frame++) {
        char label[32];
        char stem[32];
        cine_reset(cinematic);
        cine_restore(cinematic, 0);
        if (!cine_draw(cinematic, &logo[frame >= PLAYER_LOGO_SECOND_BANK_FRAME],
                       frame >= PLAYER_LOGO_SECOND_BANK_FRAME
                           ? frame - PLAYER_LOGO_SECOND_BANK_FRAME
                           : frame)) {
            goto done;
        }
        snprintf(label, sizeof(label), "%u", frame);
        snprintf(stem, sizeof(stem), "frame%03u", frame);
        if (!cine_present(cinematic, stem, label) || !cine_poll(cinematic)) {
            goto done;
        }
        if (frame == 37) {
            cine_music(cinematic, 0);
            cine_music(cinematic, 53);
            cine_voice(cinematic, &voice[0], "fire.voc");
        }
        if (frame == 52 || frame == 60) {
            cine_music(cinematic, 3);
        }
        if (frame == 64) {
            cine_music(cinematic, 2);
        }
        if (frame == 66) {
            cine_music(cinematic, 52);
            cine_voice(cinematic, &voice[1], "meangro&.voc");
        }
        if (!cine_wait_ticks(cinematic, 2, 0)) {
            goto done;
        }
    }
    /* Present the background and reset before the final hold. The game's
     * intervening wrapper does nothing, so a voice may continue across it.
     */
    cine_restore(cinematic, 0);
    if (!cine_present(cinematic, "transition", "transition")) {
        goto done;
    }
    cine_reset(cinematic);
    if (!cine_wait_ticks(cinematic, 30, 0) || !cine_poll(cinematic)) {
        goto done;
    }
    cine_record(cinematic, "completed");
    cinematic->result = XEEN_COMPLETED;
done:
    free(pal.data);
    free(back.data);
    for (index = 0; index < 2; index++) {
        free(logo[index].data);
        free(voc[index].data);
        voice_free(&voice[index]);
    }
    return cinematic->result;
}

/* One movie's state: its start, its archive and its cinematic. */
typedef struct {
    XeenStart start;
    const char *sequence;
    EndingPayload payload;
    unsigned badge;
    char path[PLAYER_PATH_BYTES];
    CC archive;
    Cinematic cinematic;
    int result;
} PlayerRun;

static xeen_movie_outcome movie_refused(xeen_movie_report *r, xeen_movie_outcome o, const char *why)
{
    r->outcome = o;
    snprintf(r->diagnostic, sizeof r->diagnostic, "%s", why);
    return o;
}

/* Validate the developer ending controls (labelled, not original) against the sequence and
 * apply them: a score, or a ten-digit payload, for an ending; a badge for the World ending.
 * Returns 0 and a diagnostic when they do not apply. */
static int player_ending_controls(PlayerRun *run, const xeen_movie_services *s, xeen_movie_report *r)
{
    int ending = run->start.mode == XEEN_MODE_CLOUDS_ENDING ||
                 run->start.mode == XEEN_MODE_DARKSIDE_ENDING || run->start.mode == XEEN_MODE_WORLD_ENDING;
    run->payload = run->start.payload;
    run->badge = run->start.badge;
    if ((s->score_given || s->payload) && !ending) {
        movie_refused(r, XEEN_MOVIE_INVALID, "score/payload require an ending sequence");
        return 0;
    }
    if (s->badge_given && run->start.mode != XEEN_MODE_WORLD_ENDING) {
        movie_refused(r, XEEN_MOVIE_INVALID, "badge requires the World ending");
        return 0;
    }
    if (s->score_given && s->payload) {
        movie_refused(r, XEEN_MOVIE_INVALID, "use either score or payload");
        return 0;
    }
    if (s->score_given) {
        ending_payload_from_score(s->score, &run->payload);
    }
    if (s->payload && !ending_payload_parse(s->payload, &run->payload)) {
        movie_refused(r, XEEN_MOVIE_INVALID, "payload must be exactly ten ASCII digits");
        return 0;
    }
    if (s->badge_given) {
        run->badge = s->badge;
    }
    return 1;
}

/* The cinematic's clock and diagnostic streams (player_start_clock of the standalone player). */
static int player_start_clock(PlayerRun *run, const xeen_movie_services *s)
{
    Cinematic *c = &run->cinematic;
    if (!s->legacy_timing) {
        int virt = s->virtual_clock || s->dump != NULL;
        if (!virt && !c->opened) {
            if (!platform_open()) {
                return 0;
            }
            c->opened = 1;
        }
        if (!cine_clock_start(c, virt, s->dos_date, s->dos_time)) {
            return 0;
        }
        if (s->dump) {
            if (!s->timing_only) {
                if (snprintf(run->path, sizeof run->path, "%s/presents.csv", s->dump) >= (int)sizeof run->path ||
                    !(c->hashes = xeen_fopen(run->path, "wb"))) {
                    return 0;
                }
                fputs("ordinal,state,pit_cycles,ppm_sha256,index_sha256,dac_sha256,palette_sha256,"
                      "rgb_sha256\n",
                      c->hashes);
            }
            if (snprintf(run->path, sizeof run->path, "%s/source-states.csv", s->dump) >= (int)sizeof run->path ||
                !(c->coverage = xeen_fopen(run->path, "wb"))) {
                return 0;
            }
            fputs("ordinal,site,kind,a,b\n", c->coverage);
        }
    }
    c->subtitles = s->subtitles;
    c->data = s->data_dir;
    return 1;
}

/* The disc-dependent selector (1f164) and the endings' service clock (player_initialize_selector of
 * the standalone player). An explicit track count wins over CUE discovery; 31 without either. */
static int player_initialize_selector(PlayerRun *run, const xeen_movie_services *s)
{
    Cinematic *c = &run->cinematic;
    /* Startup 14464 sets DS 4188 = 1 after the configuration (not a CFG word). */
    c->fire_enabled = 1;
    if (c->approximate || run->start.mode == XEEN_MODE_DARKSIDE) {
        DiscInfo disc;
        uint16_t high, low;
        if (s->cd_last_track) {
            c->last_track = s->cd_last_track;
        } else {
            int found = disc_discover_cue(s->cue, s->data_dir ? s->data_dir : ".", &disc);
            if (found < 0) {
                return 0;
            }
            c->last_track = found ? disc.last_track : 31;
        }
        c->disc_known = 1;
        if (c->approximate) {
            if (!cine_dos_time(c, (uint16_t)(PLAYER_DOS_GET_TIME_AX | c->last_track), &high, &low)) {
                return 0;
            }
        } else {
            queen_clock(c->clock_origin, 0, &high, &low);
        }
        {
            uint16_t selected = queen_select_range(&c->selector, (uint16_t)(PLAYER_DOS_GET_TIME_AX | c->last_track),
                                                   high, low, 2, 29);
            if (c->approximate && c->events) {
                fprintf(c->events, "selector,1f164,%llu,%u\n", (unsigned long long)c->now, (unsigned)selected);
            }
        }
    }
    if ((c->approximate || run->start.mode == XEEN_MODE_DARKSIDE_ENDING ||
         run->start.mode == XEEN_MODE_WORLD_ENDING) &&
        !cine_ending_timeline_start(c)) {
        return 0;
    }
    return 1;
}

/* Plays the selected sequence (player_dispatch of the standalone player). */
static void player_dispatch(PlayerRun *run)
{
    Cinematic *c = &run->cinematic;
    switch (run->start.mode) {
        case XEEN_MODE_DARKSIDE:
            run->result = darkside_play(c, &run->archive);
            break;
        case XEEN_MODE_CLOUDS_ENDING:
            run->result = clouds_ending_play(c, &run->archive, &run->payload);
            break;
        case XEEN_MODE_DARKSIDE_ENDING:
            run->result = darkside_ending_play(c, &run->archive, &run->payload);
            break;
        case XEEN_MODE_WORLD_ENDING:
            run->result = world_ending_play(c, &run->archive, &run->payload, run->badge);
            break;
        default:
            run->result = clouds_logo_play(c, &run->archive);
            if (run->start.mode == XEEN_MODE_CLOUDS) {
                if (run->result == XEEN_COMPLETED) {
                    c->result = XEEN_FAILED;
                    run->result = clouds_opening(c, &run->archive);
                } else if (run->result == XEEN_SKIPPED) {
                    cine_reset(c);
                    cine_music(c, 0);
                    cine_music_control(c, 50);
                    if (!clouds_closing(c, &run->archive, 0)) {
                        run->result = XEEN_FAILED;
                    }
                }
            }
            break;
    }
    c->result = run->result;
    if (run->result == XEEN_COMPLETED && c->approximate) {
        cine_stop_voice(c);
    }
    if (run->result == XEEN_COMPLETED &&
        (run->start.mode == XEEN_MODE_DARKSIDE_ENDING || run->start.mode == XEEN_MODE_WORLD_ENDING)) {
        cine_record(c, "ending-barrier-14d6c-safe-stub");
    }
}

xeen_movie_outcome xeen_movie_play(const char *command, const char *sequence, const xeen_movie_services *s,
                                   xeen_movie_report *report)
{
    static PlayerRun run; /* one movie at a time (not reentrant); about 200 KB */
    xeen_movie_report local;
    Cinematic *c = &run.cinematic;
    xeen_movie_outcome o;
    int set_up = 0;
    if (!report) {
        report = &local;
    }
    memset(report, 0, sizeof *report);
    memset(&run, 0, sizeof run);
    run.result = XEEN_FAILED;
    if (!s || !s->intro) {
        return movie_refused(report, XEEN_MOVIE_ASSET_FAILURE, "INTRO.CC unavailable");
    }
    if (command && sequence) {
        return movie_refused(report, XEEN_MOVIE_INVALID, "use a command or a sequence");
    }
    if (!(sequence ? xeen_resolve_sequence(sequence, &run.start) : xeen_resolve_start(command, &run.start))) {
        return movie_refused(report, XEEN_MOVIE_INVALID, "invalid command or sequence (canonical ending payload required)");
    }
    run.sequence = xeen_mode_name(run.start.mode);
    report->sequence = run.sequence;
    if (!player_ending_controls(&run, s, report)) {
        return report->outcome;
    }
    player_host_bind(s);
    /* 14a35..14bb9 select the archive by family: the Darkside intro and the Darkside and World
     * endings read INTRO.CC, the Clouds logo, intro and ending XEEN.CC (absent without Clouds). */
    if (!cc_open(&run.archive, run.start.mode == XEEN_MODE_DARKSIDE || run.start.mode == XEEN_MODE_DARKSIDE_ENDING ||
                                       run.start.mode == XEEN_MODE_WORLD_ENDING
                                   ? "INTRO.CC"
                                   : "XEEN.CC")) {
        player_host_bind(NULL);
        return movie_refused(report, XEEN_MOVIE_ASSET_FAILURE, "the sequence's archive is unavailable");
    }
    if (cine_open(c, s->dump)) {
        /* 1ef7f: 29 bytes zero-filled, then read; DS 429B (byte 29) stays 0. */
        memset(c->cfg, 0, sizeof c->cfg);
        if (s->xeen_cfg) {
            memcpy(c->cfg, s->xeen_cfg, s->xeen_cfg_size < 29u ? s->xeen_cfg_size : 29u);
        }
        c->short_intro = (c->cfg[26] | c->cfg[27] << 8) != 0; /* DS 4298 */
        c->write_cfg = s->write_cfg;
        c->cfg_user = s->cfg_user;
        c->audio = s->audio;
        c->music_fading = s->music_fading;
        c->music_user = s->music_user;
        c->music_selector = s->music_selector;
        c->hash_only = s->hash_only;
        c->original_timer_quirks = s->original_timer_quirks;
        if (s->dump && (!s->legacy_timing || run.start.mode == XEEN_MODE_DARKSIDE_ENDING)) {
            if (snprintf(run.path, sizeof run.path, "%s/events.csv", s->dump) < (int)sizeof run.path) {
                c->events = xeen_fopen(run.path, "wb");
            }
        }
        set_up = (!s->dump || c->events) && player_start_clock(&run, s) && player_initialize_selector(&run, s);
    }
    if (set_up) {
        player_dispatch(&run);
    }
    if (c->opened || c->times || c->events) {
        run.result = cine_close(c);
    }
    cc_close(&run.archive);
    player_host_bind(NULL);
    o = !set_up                         ? XEEN_MOVIE_ASSET_FAILURE
        : run.result == XEEN_COMPLETED  ? XEEN_MOVIE_COMPLETED
        : run.result == XEEN_SKIPPED    ? XEEN_MOVIE_SKIPPED
        : c->evidence_stop              ? XEEN_MOVIE_EVIDENCE_STOP
        : c->aborted                    ? XEEN_MOVIE_ABORTED
                                        : XEEN_MOVIE_ASSET_FAILURE;
    report->outcome = o;
    report->short_intro = run.start.mode == XEEN_MODE_DARKSIDE && c->short_intro;
    report->cfg_written = c->cfg_written;
    report->presents = c->presents;
    if (!set_up) {
        snprintf(report->diagnostic, sizeof report->diagnostic, "movie setup failed (clock, dump or CUE)");
    } else if (c->evidence_stop) {
        snprintf(report->diagnostic, sizeof report->diagnostic, "evidence stop: %s", c->evidence_stop);
    }
    return o;
}

const char *xeen_movie_outcome_name(xeen_movie_outcome o)
{
    static const char *const names[] = {"completed",     "skipped",      "aborted",
                                        "invalid-input", "asset-failure", "evidence-stop"};
    return (unsigned)o < sizeof names / sizeof *names ? names[o] : "invalid-input";
}

int xeen_movie_hands_off(xeen_movie_outcome o)
{
    return o == XEEN_MOVIE_COMPLETED || o == XEEN_MOVIE_SKIPPED;
}
