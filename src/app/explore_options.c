/* The native host's command line (src/app/explore_options.h). */
#include "explore_options.h"
#include "xeen/bugfixes.h" /* owner-approved bug fixes (not original; off by default) */
#include "xeen/wox_config.h" /* xeen_wox_whole_number */
#include "host_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * The parser reads the arguments in order. Each option belongs to one group (the session and window, the audio, the
 * movie player's developer controls, the replay and test controls, the files, the add-ons and bug fixes); a group
 * answers OPTION_TAKEN (its value read and stored), OPTION_REJECTED (its diagnostic printed), OPTION_HELP or, for
 * an argument that is not one of its options, OPTION_UNKNOWN. The option names are distinct, so the groups' order
 * decides nothing. An option that takes a value is only that option when a value follows it: as the last argument
 * it is not recognised (the usage text; --data-dir alone names itself).
 */
typedef enum { OPTION_UNKNOWN, OPTION_TAKEN, OPTION_REJECTED, OPTION_HELP } option_result;
/* The argument list under the parser: ARGV[I] is the argument being read; an option's value is the next one. */
typedef struct {
    int argc;
    char **argv;
    int i;
} option_cursor;
/* Nonzero when the argument being read is NAME (a switch, or --data-dir, which checks its own value). */
static int is_option(const option_cursor *a, const char *name)
{
    return !strcmp(a->argv[a->i], name);
}
/* When the argument being read is the option NAME with a value after it: consumes the value and returns it; NULL
 * otherwise. */
static const char *option_value(option_cursor *a, const char *name)
{
    if (a->i + 1 < a->argc && is_option(a, name)) {
        return a->argv[++a->i];
    }
    return NULL;
}
/* A value parser's answer (nonzero: stored; 0: rejected after its diagnostic) as the option's result. */
static option_result parsed(int ok)
{
    return ok ? OPTION_TAKEN : OPTION_REJECTED;
}
/* OPTION's (--voice-status, --cd-status) ARG into MODE: model 1, none 0; returns 0 after the
 * diagnostic for anything else. */
static int parse_status_mode(const char *option, const char *arg, int *mode)
{
    if (!strcmp(arg, "model")) {
        *mode = 1;
    } else if (!strcmp(arg, "none")) {
        *mode = 0;
    } else {
        host_report(HOST_ERROR, "%s takes model or none", option);
        return 0;
    }
    return 1;
}
/* --clicks X,Y[;X,Y...] into O's click list (at most 64); returns 0 after the diagnostic for a
 * malformed pair (the pairs before it are kept). */
static int parse_clicks(const char *c, explore_options *o)
{
    while (*c && o->click_count < 64u) {
        char *end;
        long x = strtol(c, &end, 10), y;
        if (end == c || *end != ',') {
            host_report(HOST_ERROR, "--clicks takes X,Y[;X,Y...] window pixels");
            return 0;
        }
        c = end + 1;
        y = strtol(c, &end, 10);
        if (end == c) {
            host_report(HOST_ERROR, "--clicks takes X,Y[;X,Y...] window pixels");
            return 0;
        }
        o->clicks[o->click_count][0] = (int)x;
        o->clicks[o->click_count][1] = (int)y;
        ++o->click_count;
        c = *end == ';' ? end + 1 : end;
    }
    return 1;
}
/* --rng-time HH:MM:SS.CS into O (and marks it explicit); returns 0 after the diagnostic. */
static int parse_rng_time(const char *arg, explore_options *o)
{
    unsigned h, m, s, cs;
    char extra;
    if (sscanf(arg, "%u:%u:%u.%u%c", &h, &m, &s, &cs, &extra) != 4 || h > 23u || m > 59u ||
        s > 59u || cs > 99u) {
        host_report(HOST_ERROR, "invalid RNG time");
        return 0;
    }
    o->rng_time = (xeen_rng_time){(uint8_t)h, (uint8_t)m, (uint8_t)s, (uint8_t)cs};
    o->explicit_rng_time = 1;
    return 1;
}
/* --dos-date YYYY-MM-DD (1980..2099) into O; returns 0 after the diagnostic. */
static int parse_dos_date(const char *arg, explore_options *o)
{
    unsigned y, mo, d;
    char extra;
    if (sscanf(arg, "%u-%u-%u%c", &y, &mo, &d, &extra) != 3 || y < 1980u || y > 2099u || !mo ||
        mo > 12u || !d || d > 31u) {
        host_report(HOST_ERROR, "invalid DOS date");
        return 0;
    }
    o->dos_date[0] = y;
    o->dos_date[1] = mo;
    o->dos_date[2] = d;
    return 1;
}
/* --movie-score N (0..4294967295, wox.cfg's [endings] score grammar: xeen_wox_whole_number) into MOVIE; returns 0
 * after the diagnostic (on stderr, not the host log's). */
static int parse_movie_score(const char *arg, host_movie_developer *movie)
{
    unsigned long n;
    if (!xeen_wox_whole_number(arg, 0u, 4294967295ul, &n)) {
        fprintf(stderr, "--movie-score: expected 0..4294967295\n");
        return 0;
    }
    movie->score = (uint32_t)n;
    movie->score_given = 1;
    return 1;
}
/* --movie-badge 0|1|2 into MOVIE; returns 0 after the diagnostic (on stderr, as --movie-score's). */
static int parse_movie_badge(const char *arg, host_movie_developer *movie)
{
    if (strcmp(arg, "0") && strcmp(arg, "1") && strcmp(arg, "2")) {
        fprintf(stderr, "--movie-badge: expected 0, 1 or 2\n");
        return 0;
    }
    movie->badge = (unsigned)(arg[0] - '0');
    movie->badge_given = 1;
    return 1;
}
/*
 * --addon NAME...: one or more names after the option, up to the next option ("--..."), each enabled in ADDONS as
 * wox.cfg's [addons] would (never turned off; a repeated name is harmless). Leaves the cursor on the last name;
 * returns 0 after the diagnostic for an unknown name (the names before it stay enabled) or none.
 */
static int parse_addons(option_cursor *a, xeen_addons *addons)
{
    int names = 0;
    while (a->i + 1 < a->argc && strncmp(a->argv[a->i + 1], "--", 2)) {
        if (!xeen_addons_enable(addons, a->argv[++a->i])) {
            host_report(HOST_ERROR, "unknown add-on %s (known: " XEEN_ADDON_NAMES ")", a->argv[a->i]);
            return 0;
        }
        ++names;
    }
    if (!names) {
        host_report(HOST_ERROR, "--addon needs one or more add-on names (known: " XEEN_ADDON_NAMES ")");
        return 0;
    }
    return 1;
}
/* --bugfix NAME...: as parse_addons, for the bug fixes (wox.cfg's [bugfix]). */
static int parse_bugfixes(option_cursor *a)
{
    int names = 0;
    while (a->i + 1 < a->argc && strncmp(a->argv[a->i + 1], "--", 2)) {
        if (!xeen_bugfixes_enable(a->argv[++a->i])) {
            host_report(HOST_ERROR, "unknown bug fix %s (known: " XEEN_BUGFIX_NAMES ")", a->argv[a->i]);
            return 0;
        }
        ++names;
    }
    if (!names) {
        host_report(HOST_ERROR,
                    "--bugfix needs one or more bug-fix names (known: " XEEN_BUGFIX_NAMES ")");
        return 0;
    }
    return 1;
}
/* The session and its window: --headless, --diagnostic-state, --wox-config, --fullscreen, --menu, --no-menu and
 * --help (-h). */
static option_result session_option(option_cursor *a, explore_options *o)
{
    const char *v;
    if (is_option(a, "--headless")) {
        o->headless = 1;
    } else if (is_option(a, "--diagnostic-state")) {
        o->diagnostic_state = 1;
    } else if ((v = option_value(a, "--wox-config"))) {
        o->wox_path = v;
    } else if (is_option(a, "--fullscreen")) {
        o->display_override = 1;
    } else if (is_option(a, "--menu")) {
        o->menu_choice = 1;
    } else if (is_option(a, "--no-menu")) {
        o->menu_choice = 0;
    } else if (is_option(a, "--help") || is_option(a, "-h")) {
        return OPTION_HELP;
    } else {
        return OPTION_UNKNOWN;
    }
    return OPTION_TAKEN;
}
/* The audio: --no-audio, --audio-capture and the two status models (--voice-status, --cd-status). */
static option_result audio_option(option_cursor *a, explore_options *o)
{
    const char *v;
    if (is_option(a, "--no-audio")) {
        o->no_audio = 1;
    } else if ((v = option_value(a, "--audio-capture"))) {
        o->audio_capture = v;
    } else if ((v = option_value(a, "--voice-status"))) {
        return parsed(parse_status_mode("--voice-status", v, &o->voice_status_mode));
    } else if ((v = option_value(a, "--cd-status"))) {
        return parsed(parse_status_mode("--cd-status", v, &o->cd_status_mode));
    } else {
        return OPTION_UNKNOWN;
    }
    return OPTION_TAKEN;
}
/* Phase 308: the integrated movie player's developer and test controls (--movies, --movie-*; labelled, not
 * original): stored as given; the host fills what they leave unset from wox.cfg (explore.c adopt_movie_config). */
static option_result movie_option(option_cursor *a, explore_options *o)
{
    host_movie_developer *m = &o->movie;
    const char *v;
    if (is_option(a, "--movies")) {
        o->movies = 1;
    } else if ((v = option_value(a, "--movie-command"))) {
        m->command = v;
    } else if ((v = option_value(a, "--movie-sequence"))) {
        m->sequence = v;
    } else if ((v = option_value(a, "--movie-dump"))) {
        m->dump = v;
    } else if ((v = option_value(a, "--movie-dump-timings"))) {
        m->dump = v;
        m->timing_only = 1;
    } else if ((v = option_value(a, "--movie-dos-date"))) {
        m->dos_date = v;
    } else if ((v = option_value(a, "--movie-dos-time"))) {
        m->dos_time = v;
    } else if (is_option(a, "--movie-original-timer-quirks")) {
        m->original_timer_quirks = 1;
    } else if (is_option(a, "--movie-legacy-timing")) {
        m->legacy_timing = 1;
    } else if ((v = option_value(a, "--movie-score"))) {
        return parsed(parse_movie_score(v, m));
    } else if ((v = option_value(a, "--movie-payload"))) {
        m->payload = v;
    } else if ((v = option_value(a, "--movie-badge"))) {
        return parsed(parse_movie_badge(v, m));
    } else if ((v = option_value(a, "--movie-keys"))) {
        o->movie_keys = v;
    } else {
        return OPTION_UNKNOWN;
    }
    return OPTION_TAKEN;
}
/* The replay and the host test controls: the input paths (--frontend-input; --boss-each-key and --ctrl-q-keys imply
 * it), --clicks, the key scripts (--commands, --cutscene-keys), --evidence and the fixed clocks (--rng-time,
 * --dos-date). */
static option_result replay_option(option_cursor *a, explore_options *o)
{
    const char *v;
    if (is_option(a, "--frontend-input")) {
        o->frontend_input = 1;
    } else if (is_option(a, "--boss-each-key")) {
        o->boss_each_key = o->frontend_input = 1;
    } else if ((v = option_value(a, "--ctrl-q-keys"))) {
        o->ctrl_q_keys = v;
        o->frontend_input = 1;
    } else if ((v = option_value(a, "--clicks"))) {
        return parsed(parse_clicks(v, o));
    } else if ((v = option_value(a, "--commands"))) {
        o->commands = v;
    } else if ((v = option_value(a, "--evidence"))) {
        o->evidence = v;
    } else if ((v = option_value(a, "--rng-time"))) {
        return parsed(parse_rng_time(v, o));
    } else if ((v = option_value(a, "--cutscene-keys"))) {
        o->cut_keys = v;
    } else if ((v = option_value(a, "--dos-date"))) {
        return parsed(parse_dos_date(v, o));
    } else {
        return OPTION_UNKNOWN;
    }
    return OPTION_TAKEN;
}
/* The files: --data-dir (its own diagnostic for a missing or empty directory), the two saves, --save-output and
 * --save-dir. */
static option_result file_option(option_cursor *a, explore_options *o)
{
    const char *v;
    if (is_option(a, "--data-dir")) {
        if (a->i + 1 >= a->argc || !*a->argv[a->i + 1]) {
            host_report(HOST_ERROR, "--data-dir needs a directory (the one holding XEEN.CC and DARK.CC)");
            return OPTION_REJECTED;
        }
        o->data_override = a->argv[++a->i];
    } else if ((v = option_value(a, "--cloud-save"))) {
        o->cloud_save = v;
    } else if ((v = option_value(a, "--dark-save"))) {
        o->dark_save = v;
    } else if ((v = option_value(a, "--save-output"))) {
        o->save_output = v;
    } else if ((v = option_value(a, "--save-dir"))) {
        o->save_dir = v;
    } else {
        return OPTION_UNKNOWN;
    }
    return OPTION_TAKEN;
}
/* The owner-approved add-ons and bug fixes (--addon, --bugfix): enabled while parsing. */
static option_result switch_option(option_cursor *a, xeen_addons *addons)
{
    if (is_option(a, "--addon")) {
        return parsed(parse_addons(a, addons));
    }
    if (is_option(a, "--bugfix")) {
        return parsed(parse_bugfixes(a));
    }
    return OPTION_UNKNOWN;
}
/* Reads the argument under the cursor (and its value) by the group it belongs to. */
static option_result parse_option(option_cursor *a, explore_options *o, xeen_addons *addons)
{
    option_result r = session_option(a, o);
    if (r == OPTION_UNKNOWN) {
        r = audio_option(a, o);
    }
    if (r == OPTION_UNKNOWN) {
        r = movie_option(a, o);
    }
    if (r == OPTION_UNKNOWN) {
        r = replay_option(a, o);
    }
    if (r == OPTION_UNKNOWN) {
        r = file_option(a, o);
    }
    if (r == OPTION_UNKNOWN) {
        r = switch_option(a, addons);
    }
    return r;
}
/*
 * Phase 270 (and 336): the retired options and where their settings went. Each is rejected with this hint (no
 * alias is kept).
 */
static const struct {
    const char *option, *hint;
} retired[] = {
        {"--windowed", "a window is the default; --fullscreen selects fullscreen"},
        {"--scale", "set [display] scale = 1..10 in wox.cfg"},
        {"--aspect", "set [display] aspect = on in wox.cfg"},
        {"--authentic-cursor", "set [display] authentic_cursor = on in wox.cfg"},
        {"--resources", "use --data-dir DIR or [files] data_dir in wox.cfg"},
        {"--cd-test", "removed"},
        /* Phase 336 (owner decision): --data became --data-dir, as wox.cfg's [files] data became data_dir. */
        {"--data", "renamed --data-dir"},
};
/* Phase 338: nonzero when an argument is --version, wherever it stands (as window_session finds --headless); the host
 * then prints its version before anything else runs. */
int explore_version_requested(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) {
            return 1;
        }
    }
    return 0;
}
/* The usage text on OUT, for --help (stdout) or an unknown argument or one without its value (stderr). */
static void print_usage(FILE *out, const char *argv0)
{
    fprintf(out,
            "usage: %s [--data-dir DIR (the game data directory: XEEN.CC, DARK.CC; default wox.cfg "
            "[files] data_dir, auto = the working directory)] [--fullscreen (overrides [display] "
            "fullscreen; a window otherwise)] [--menu | --no-menu (a window starts at the main "
            "menu, --no-menu loads the saves; headless loads the saves unless --menu)] "
            "[--cloud-save FILE --dark-save FILE (default DIR/XEEN01.WOX, DIR/DARK01.WOX)] "
            "[--wox-config FILE (default: wox.cfg beside the executable, created once; its "
            "[display] scale, aspect and authentic_cursor set the window; headless reads only "
            "an explicit one)] [--help] [--version (print the version number and exit; nothing else runs and "
            "no file is written)] ",
            argv0);
    /* Phase 272: the rest in further strings (one literal would pass C99's 4095-character limit). */
    fprintf(out, "[--movies (developer/test control: a headless run plays the XEEN.EXE movies as a window does: at "
                 "the start and at each hand-off)] [--movie-command TEXT | --movie-sequence NAME (developer: the "
                 "first movie's XEENCOMMAND, or darkside, clouds, clouds-logo, clouds-ending, darkside-ending, "
                 "world-ending)] [--movie-dump DIR | --movie-dump-timings DIR] [--movie-dos-date YYYY-MM-DD] "
                 "[--movie-dos-time HH:MM:SS.cc] [--movie-original-timer-quirks] [--movie-legacy-timing] "
                 "[--movie-score N | --movie-payload DIGITS] [--movie-badge 0|1|2] [--movie-keys X,Q,K (headless: "
                 "keys before each movie)] ");
    fprintf(out, "[--diagnostic-state (developer/test control, off by default: print the "
                 "state-summary records, e.g. program-start accepted=..., on stdout; a window also "
                 "logs them in wox.log)] ");
    fprintf(out,
            "[--headless "
            "--commands UDLR[]SAEBFZYN1..8=K ([ ] Ctrl+Left/Right: strafe, S Space, W "
            "Wait, F Shoot, Z Rest, Y/N answer a yes/no, X sends the game Esc (also at the "
            "exploration poll, Phase161), E closes the host, 1..8 F1..F8, = the minimap, K "
            "the D key; Phase153: a lower-case letter a..z is that letter's own key in "
            "exploration, combat and modal waits)] [--rng-time HH:MM:SS.CS] [--evidence "
            "PREFIX] [--save-output NEW_DIR] (at "
            "a main menu, input continues after '|': L S C O V and digits are its keys, A "
            "Enter, X Esc) [--save-dir WRITABLE_DIR (created when missing; overrides wox.cfg [files] save_dir, "
            "whose auto is a window's working directory)] [--dos-date YYYY-MM-DD] "
            "[--frontend-input (replay keys through the window "
            "key path)] [--boss-each-key (Phase161 host test control: an F10 boss-screen "
            "round trip before every replay key; implies --frontend-input)] (window: F10 "
            "shows a calculator; any key returns) [--clicks X,Y;... (each '*' of the "
            "replay is a left click at that window pixel, at [display] scale)] [--no-audio] "
            "[--audio-capture FILE (host test control: log the audio "
            "requests; offline engine, any wox.cfg backend)] [--cd-status model|none (the "
            "CD drive's busy status: modeled from the play's length on the game clock, or "
            "no device; default model in a window with the CD rips, none headless)] "
            "[--voice-status model|none (Phase169: the digital driver's VOC busy status "
            "187E9(1, 0): modeled from the voice's length on the game clock, or no driver; "
            "default model in a window with the audio engine, none headless)] "
            "[--addon NAME... "
            "(owner-approved add-ons, not original; one or more names, repeatable, added "
            "to wox.cfg [addons] NAME = on: identify = identified item statistics in the "
            "items list, map_movement = the M map pans with the arrow keys and a mouse "
            "drag, a click inside it does not close it, training_preview = a Prev button "
            "(P) at the training grounds shows the levels, costs and limits, "
            "damage_numbers = damage, healing and misses shown as numbers over the "
            "portraits and the monsters, damage_numbers.stack = with damage_numbers, one "
            "running total per target, monster_difficulty = a difficulty icon over each "
            "monster (sword, crossed swords, shield, skull; none for trivial ones), "
            "spell_short_description = a short description of the ready or highlighted "
            "spell in a box under the Cast Spell dialog, mouse_scroll = the mouse wheel "
            "scrolls the spell lists, the Quests panel, the Awards and the inn's roster "
            "while the pointer is over the list, town_icons = icons for the town places "
            "(blacksmith, tavern, temple, guild, training, bank, mirror, tents, "
            "Castleview's pyramid and Ellinger's Tower) and the Skyroad's tents on the map "
            "(M) and the minimap where the map shows "
            "their cell, character_details = the character view's stat popups explain the "
            "values, a click on a skill or a condition shows what it does, spell_list_sort "
            "= a button (O) above the spell lists switches a character's list between A-Z "
            "and grouped by type (kept per save slot in ADDONnn.WOX), world_map = in the map "
            "(M), W "
            "or the World Map button shows the whole world's surface on one screen (arrows "
            "or a drag pan, the wheel or PgUp/PgDn zoom 100%%/50%%/the whole world, Esc "
            "closes), quest_journal = in the Quests dialog (V) a number or a click on a quest "
            "item shows its description (Space closes it), and with world_map a quest's or a "
            "note's number shows its place on the world map, please_wait_skip = the "
            "\"Please Wait...\" box of a map load is not shown, ");
    /* Phase 318: a second literal (C99's 4095-character limit). */
    fprintf(out, "wizard_eye_plus = while Wizard Eye is on and the minimap is clicked on (= automap), the cells "
                 "not yet seen show what is there through a checkerboard (wox.cfg wizard_eye_plus.indoor, "
                 ".outdoor, .sky = 1px, 2px or black; default black))] ");
    /* The bug fixes' part is a second string: one literal would pass C99's 4095-character limit. */
    fprintf(out,
            "[--bugfix NAME... "
            "(owner-approved bug fixes, not original; one or more names, repeatable, added "
            "to wox.cfg [bugfix] NAME = on: spell_description = corrected spell "
            "descriptions in the guild's Spell Info and Buy Spells, left_clip_colors = "
            "monsters and objects cut by the 3D view's left edge keep their own colours, "
            "sprite_width_trim = shrunk pictures trimmed to their stated width, quest_up_down = U "
            "and D scroll the Quests dialog as its Up and Down buttons)] (T opens the "
            "control panel; at its polls letters, digits and space are keys, '.' Enter, '!' "
            "Esc, '<' Backspace)\n");
}
/* The diagnostic for ARG, an argument no group knows: a retired option's hint (Phase 270), else the usage text on
 * stderr. */
static void reject_unknown(const char *argv0, const char *arg)
{
    for (size_t r = 0; r < sizeof retired / sizeof *retired; ++r) {
        if (!strcmp(arg, retired[r].option)) {
            host_report(HOST_ERROR, "%s is no longer an option (%s)", arg, retired[r].hint);
            return;
        }
    }
    /* Phase 272: the option itself, for wox.log and the error dialog (stderr keeps the usage alone). */
    host_note(HOST_ERROR, "unknown option, or an option without its value: %s", arg);
    print_usage(stderr, argv0);
}
/**
 * Parses the command line into O (defaults first, then each argument in order); --addon names are
 * enabled in ADDONS. Returns 0 after printing the diagnostic for an invalid, incomplete or unknown
 * argument (the host then exits with status 1 through main's cleanup), or after --help printed the
 * usage on stdout (O->help set; status 0), 1 otherwise. An option that takes a value but is the last
 * argument falls through to the usage text (--data-dir names itself). A retired option (Phase 270) is
 * rejected with where its setting went. The display settings and the data directory are wox.cfg's
 * and are resolved by the host after parsing (explore.c resolve_startup_config).
 */
int explore_parse_options(int argc, char **argv, explore_options *o, xeen_addons *addons)
{
    option_cursor a = {argc, argv, 1};
    /* The defaults: the replay's DOS date and clock without --dos-date / --rng-time (fixed: a replay is repeatable),
     * wox.cfg's default scale 3, and every override unset (-1). */
    *o = (explore_options){.dos_date = {1995, 1, 1},
                           .scale = 3,
                           .rng_time = EXPLORE_DEFAULT_RNG_TIME,
                           .display_override = -1,
                           .menu_choice = -1,
                           .voice_status_mode = -1,
                           .cd_status_mode = -1};
    for (; a.i < argc; a.i++) {
        switch (parse_option(&a, o, addons)) {
            case OPTION_TAKEN:
                break;
            case OPTION_HELP:
                print_usage(stdout, argv[0]);
                o->help = 1;
                return 0;
            case OPTION_REJECTED:
                return 0;
            case OPTION_UNKNOWN:
                reject_unknown(argv[0], argv[a.i]);
                return 0;
        }
    }
    return 1;
}
