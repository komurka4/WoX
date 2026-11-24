/* SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef XEEN_HOST_MOVIE_H
#define XEEN_HOST_MOVIE_H
/*
 * Phase 308: the host side of the integrated XEEN.EXE movie player (include/xeen/movie.h). One process, one window:
 * a movie plays in the game's window (xeen_platform), takes its keys from the game's keyboard map, plays through the
 * game's audio sink and reads the one XEEN.CFG image the game reads. The program flow it serves is the original's
 * (xeen_dat_evidence/phase308/A_original_execution_contract.md): XEEN.EXE first (XEENCOMMAND absent: the Darkside
 * intro), then XEEN.DAT's program start (15CDB); XEEN.DAT's exec of xeen.exe (123A1) plays the movie its XEENCOMMAND
 * names, then XEEN.DAT starts again at 15CDB.
 */
#include "xeen/movie.h"
#include "xeen/platform.h"
#include "xeen/main_menu.h"
#include "xeen/wox_config.h"

/* Developer and test controls (labelled; not original behaviour): --movie-* on the command line, the player's former
 * [playback] sequence/command, [endings] and [timing]/[diagnostics] keys in wox.cfg. */
typedef struct {
    const char *command;  /* the first movie's XEENCOMMAND instead of an absent one */
    const char *sequence; /* or a sequence name */
    const char *dump;     /* hash dumps on the virtual clock */
    const char *dos_date, *dos_time;
    int original_timer_quirks, legacy_timing, timing_only;
    int score_given, badge_given;
    uint32_t score;
    unsigned badge;
    const char *payload;
} host_movie_developer;

typedef struct {
    xeen_platform *platform;
    xeen_input_provider provider;
    /* INTRO.CC, XEEN.CC (NULL without Clouds), DARK.CC: the host's bytes */
    const uint8_t *intro, *xeen, *dark;
    size_t intro_size, xeen_size, dark_size;
    const xeen_audio_sink *audio;
    int (*music_fading)(void *);
    void *music_user;
    const xeen_wox_config *wox;
    const uint8_t *xeen_cfg; /* the current XEEN.CFG image (NULL: none) */
    size_t xeen_cfg_size;
    const xeen_save_files *save_files; /* its write writes xeen.cfg where the game writes it */
    const char *data_dir;
    const char *cue;        /* [files] cue (NULL: auto) */
    unsigned cd_last_track; /* [files] cd_num_tracks (0: auto) */
    int headless;
    const host_movie_developer *developer;
} host_movie_context;

/* Plays the movie COMMAND names (NULL: XEENCOMMAND absent) with CTX; the developer's sequence applies only to
 * FIRST (the program's start). Logs the request and its outcome (wox.log); fills REPORT. */
xeen_movie_outcome host_movie_play(const host_movie_context *ctx, const char *command, int first,
                                   xeen_movie_report *report);
#endif
