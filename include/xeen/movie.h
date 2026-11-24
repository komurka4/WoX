/* SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef XEEN_MOVIE_H
#define XEEN_MOVIE_H
/*
 * Phase 308: the XEEN.EXE movie player inside the game's executable (src/player/, migrated from the separate
 * reconstruction at xeen.exe 3262749). One call plays what one run of the original XEEN.EXE plays for one
 * XEENCOMMAND value: its 14c15 getenv and 13c24 dispatch (the Darkside intro for an absent command, MAINGAME or
 * DARKINTRO; the Clouds logo and intro for XEENINTRO; the three endings with their score), then its 14cf7 common
 * cleanup. The original then overlays XEEN.DAT (14e09, INT 21h 4B03) with XEENCOMMAND=MAINGAME, so XEEN.DAT starts
 * again at 15CDB. That is the caller's job: completed and skipped mean "start the game program"; aborted (Ctrl+Q,
 * the window closed: 1ec89's exit 1) and the failures mean "no hand-off".
 *
 * Synchronous and not reentrant. The services are borrowed for the call. The player draws through HOST, plays
 * through the game's audio sink and reads the shared XEEN.CFG image; it never reads wox.cfg or XEEN.CFG itself.
 * Evidence: xeen_dat_evidence/phase308/A_original_execution_contract.md.
 */
#include "xeen/audio.h"
#include "xeen/resources.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    XEEN_MOVIE_COMPLETED,     /* played to its end: hand off to the game */
    XEEN_MOVIE_SKIPPED,       /* Escape in an intro (source closing and cleanup): hand off to the game */
    XEEN_MOVIE_ABORTED,       /* Ctrl+Q or the window closed: the program ends (1ec89, exit 1) */
    XEEN_MOVIE_INVALID,       /* the command cannot be played (a noncanonical Clouds payload) */
    XEEN_MOVIE_ASSET_FAILURE, /* a resource is missing or malformed */
    XEEN_MOVIE_EVIDENCE_STOP  /* a named unverified boundary of the reconstruction */
} xeen_movie_outcome;

/* The window (or a headless stand-in). PIT cycles are 1193182 Hz. */
typedef struct {
    void *user;
    /* Shows the 320x200 indexed frame through the 6-bit DAC palette. 0: the display failed. */
    int (*present)(void *, const uint8_t *pixels, const uint8_t *dac);
    /* Drains the pending input. ANY_KEY 0: 1 for Escape; ANY_KEY 1: 1 for any fresh key press; 2 for the global
     * abort (Ctrl+Q or the window closed) in either case; 0 for nothing. */
    int (*poll)(void *, int any_key);
    /* Monotonic PIT cycles since an arbitrary origin, and a wait until such a time (input stays queued). */
    uint64_t (*now)(void *);
    void (*wait_until)(void *, uint64_t pit);
} xeen_movie_host;

typedef struct {
    xeen_movie_host host;
    /* The game's log (wox.log: host_report). The player's diagnostics go here; NULL: stderr. */
    void (*log)(void *, const char *text);
    void *log_user;
    const xeen_archive *intro; /* INTRO.CC: required */
    const xeen_archive *xeen;  /* XEEN.CC: the Clouds movies (NULL: Clouds not installed) */
    const xeen_archive *dark;  /* DARK.CC: the Darkside and World movies */
    /* The game's audio sink (NULL: silent). The voices go through its voice/voice_stop, the driver cues through
     * effect, the songs through song, the controls through music and volume. */
    const xeen_audio_sink *audio;
    /* The music driver's fade state (entry 6 with FFE0h: nonzero while a fade runs; World ending 1cee4, 1e9ad).
     * NULL: no music driver, never fading. */
    int (*music_fading)(void *);
    void *music_user;
    /* The shared XEEN.CFG image (1ef7f: 29 bytes zero-filled then read). NULL: none (an all-zero image). */
    const uint8_t *xeen_cfg;
    size_t xeen_cfg_size;
    /* 1A52F: the Darkside intro's 30-byte xeen.cfg write. NULL: not written. Returns 0 when it failed (the
     * original's result is not tested). */
    int (*write_cfg)(void *, const uint8_t *, size_t);
    void *cfg_user;
    /* The resolved speech: 1 captions (1eaee: XEEN.CFG's word 0 below 2, or [audio] speech = off), 0 speech. */
    int subtitles;
    /* The music selector the song choice follows (14eb8: 4 bigtheme.m, 5 cantheme.m, else mm4theme.m): XEEN.CFG's
     * word 4, or the index of the driver wox.cfg's [audio] music_override resolves to. */
    unsigned music_selector;
    /* The CD's last track for the selector at 1f164 (0: from CUE, else 31) and the CUE path (NULL: auto). */
    unsigned cd_last_track;
    const char *cue;
    const char *data_dir; /* for CUE discovery */
    /* Developer and test controls (labelled, not original): a dump directory with present hashes and timings
     * (virtual clock), a fixed DOS date and time, the original timer quirks, an ending's score/payload/badge. */
    const char *dump;
    int hash_only, timing_only;
    int virtual_clock;
    int legacy_timing; /* the standalone player's pre-Phase-25 timing model (diagnostic) */
    const char *dos_date, *dos_time;
    int original_timer_quirks;
    int score_given;
    uint32_t score;
    const char *payload; /* exactly ten digits, or NULL */
    int badge_given;
    unsigned badge;
} xeen_movie_services;

typedef struct {
    xeen_movie_outcome outcome;
    const char *sequence; /* darkside, clouds, clouds-logo, clouds-ending, darkside-ending, world-ending */
    int short_intro;      /* the Darkside intro took 17F0D's bypass (CFG byte 26 set) */
    int cfg_written;      /* 1A52F wrote xeen.cfg */
    uint64_t presents;
    char diagnostic[256];
} xeen_movie_report;

/* Plays the movie COMMAND selects (NULL: XEENCOMMAND absent). SEQUENCE, when not NULL, is the developer
 * shortcut (a sequence name instead of a command; both given is invalid). */
xeen_movie_outcome xeen_movie_play(const char *command, const char *sequence, const xeen_movie_services *,
                                   xeen_movie_report *);
const char *xeen_movie_outcome_name(xeen_movie_outcome);
/* 1 when the outcome hands off to the game (completed, skipped). */
int xeen_movie_hands_off(xeen_movie_outcome);
#endif
