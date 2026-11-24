/*
 * The native host's command line (src/app/explore.c): the options record and its parser. The parser
 * prints its own diagnostics and the usage text; it touches no host state except the add-on and bug-fix
 * switches the --addon and --bugfix options enable.
 */
#ifndef XEEN_APP_EXPLORE_OPTIONS_H
#define XEEN_APP_EXPLORE_OPTIONS_H
#include "xeen/addons.h"
#include "xeen/entities.h"
#include "host_movie.h"

/**
 * The host's command-line settings, as explore_parse_options() leaves them. Each field's default is
 * the value the host uses without its option; the fields marked "set by the host" are resolved from
 * the options and wox.cfg after parsing (explore.c resolve_startup_config). The host-wide switches (the last group) are copied into
 * the host's state by explore.c after parsing; --addon and --bugfix enable their switches while parsing.
 */
/* The replay clock without --rng-time (12:34:56.78), also the host's random source before the options apply. */
#define EXPLORE_DEFAULT_RNG_TIME {12, 34, 56, 78}
typedef struct explore_options {
    const char *cloud_save, *dark_save; /* --cloud-save, --dark-save: the two worlds' saves (NULL:
                                               DIR/XEEN01.WOX, DIR/DARK01.WOX, set by the host) */
    const char *data_override;          /* --data-dir: the game data directory, over wox.cfg's */
    const char *dir;                    /* the game's DOS directory in effect: --data-dir, else
                                               wox.cfg [files] data_dir, else "." (set by the host) */
    int help;                           /* --help printed the usage */
    const char *commands;               /* --commands: the headless key script */
    const char *evidence;               /* --evidence: the evidence file prefix */
    const char *save_output;            /* --save-output: commit the final pair here */
    const char *save_dir;               /* --save-dir: the writable save overlay */
    /* Phase 287: the directory holding XEEN.CFG ([files] xeen_cfg_dir; auto: the data directory `dir`) and
     * whether it was named explicitly (resolve_startup_config). */
    const char *xeen_cfg_dir;
    int xeen_cfg_explicit;
    unsigned dos_date[3];               /* --dos-date: year, month, day */
    int headless;                       /* --headless */
    int diagnostic_state;               /* --diagnostic-state: the state-summary records (Phase 272; off) */
    int explicit_rng_time;              /* --rng-time was given */
    int aspect;                         /* wox.cfg [display] aspect: 4:3 display */
    int locked_scale;                   /* wox.cfg [display] locked_scale (a host option) */
    unsigned scale;                     /* wox.cfg [display] scale 1..6 (default 3) */
    xeen_rng_time rng_time;             /* --rng-time: the replay clock (random.replay_time) */
    int menu_choice;                    /* --menu 1, --no-menu 0, otherwise -1 */
    int menu_start;                     /* start at the main menu: menu_choice, else a window's
                                               default (set by the host; headless: saves) */
    const char *cut_keys;               /* --cutscene-keys N:K,...: key K before the Nth cutscene
                                               poll of the session (a key pressed during the animation) */
    const char *wox_path;               /* --wox-config: wox.cfg (window runs only; headless
                                               replays never read it) */
    const char *audio_capture;          /* --audio-capture: the audio request log */
    int no_audio;                       /* --no-audio */
    int display_override;               /* --fullscreen 1, otherwise -1 */
    /* Host-wide switches. */
    int voice_status_mode,
            cd_status_mode;  /* --voice-status, --cd-status: model 1, none 0, unset -1 */
    int frontend_input;      /* --frontend-input (also --boss-each-key, --ctrl-q-keys) */
    int boss_each_key;       /* --boss-each-key */
    const char *ctrl_q_keys; /* --ctrl-q-keys */
    int authentic_cursor;    /* wox.cfg [display] authentic_cursor */
    int clicks[64][2];       /* --clicks X,Y;...: each '*' of the replay, in order */
    unsigned click_count;
    /* Phase 308: the integrated movie player. A window plays the movies (at the program's start unless --menu or
     * --no-menu is given, and at every XEEN.EXE hand-off); a headless run only with --movies (developer/test
     * control). MOVIE holds the --movie-* developer controls (labelled, not original; wox.cfg's movie keys fill
     * what the command line leaves unset); MOVIE_KEYS (--movie-keys) the keys queued headless before each movie:
     * comma-separated, X the game's Esc, Q Ctrl+Q, K Space. */
    int movies;
    host_movie_developer movie;
    const char *movie_keys;
} explore_options;

int explore_parse_options(int argc, char **argv, explore_options *o, xeen_addons *addons);
/* Phase 338: nonzero when any argument is --version (main prints XEEN_WOX_VERSION and exits 0 before the log, wox.cfg,
 * the data or the window). */
int explore_version_requested(int argc, char **argv);
#endif
