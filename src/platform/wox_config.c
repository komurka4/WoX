#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "xeen/wox_config.h"
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif
/* The settings of wox.cfg, each spelled exactly as its name below (Phase 336, owner decision: '_' is the one
 * separator, no other spelling, alias or retired key is accepted). */
enum {
    K_MUSIC,
    K_VOICEVOL,
    K_MUSICVOL,
    K_MIDIDEV,
    K_MIDIPORT,
    K_MT32,
    K_SC55,
    K_FULLSCREEN,
    K_CDVOL,
    K_CDTRACKS,
    K_IDENTIFY,
    K_MAPMOVE,
    K_TRAINPREV,
    K_DMGNUM,
    K_DMGSTACK,
    K_MONDIFF,
    K_SPELLDESC,
    K_SPELLSHORT,
    K_MOUSESCROLL,
    K_TOWNICONS,
    K_CHARDETAILS,
    K_SPELLSORT,
    K_WORLDMAP,
    K_LEFTCLIP,
    K_WIDTHTRIM,
    K_QUESTJOURNAL,
    K_QUESTUPDOWN,
    K_SCALE,
    K_ASPECT,
    K_AUTHCURSOR,
    K_LOCKEDSCALE,
    K_DATA,
    K_SPEECH, /* Phase 286 */
    K_SAVEDIR, /* Phase 287 */
    K_XEENCFG, /* Phase 287 */
    K_PLEASEWAIT, /* Phase 310 */
    K_WIZEYE,     /* Phase 318 */
    K_WIZEYE_INDOOR,
    K_WIZEYE_OUTDOOR,
    K_WIZEYE_SKY,
    K_COUNT
};
/*
 * One xeen_wox_config_load: the settings read so far and the first error. A helper that rejects a line
 * sets WHAT (and ITEM, or NULL) and returns 0; the loader closes the file and formats
 * "path:line: what[: item]" (fail).
 */
typedef struct {
    xeen_wox_config *c;
    const char *base; /* the file's directory (relative paths resolve against it) */
    char section[64]; /* the current [section], "" before the first */
    unsigned seen[K_COUNT];
    unsigned movie_seen[32]; /* one per player_keys entry (asserted at the table) */
    const char *what, *item;
    char where[96]; /* WHAT's storage for "KEY belongs in [SECTION]" */
} wox_parse;
/* How apply_setting reads a setting's value:
 * - SETTING_SWITCH: on/off (Phase 336: every boolean setting) into an int; SETTING_PERCENT: a whole percent into an
 *   unsigned volume;
 * - SETTING_TEXT: a name (auto: empty) into a char array (the MIDI device and port);
 * - SETTING_PATH: a directory relative to the file's (empty or auto: the default, xeen_wox_config_defaults) into a
 *   char array; SETTING_AUTO_PATH: the same, and an explicit one clears the setting's int auto flag (the host
 *   resolves auto);
 * - SETTING_OWN: the music system, the speech and the scale, each in its own function. */
enum setting_kind {
    SETTING_OWN,
    SETTING_SWITCH,
    SETTING_PERCENT,
    SETTING_TEXT,
    SETTING_PATH,
    SETTING_AUTO_PATH
};
#define CONFIG_SIZE(field) sizeof(((xeen_wox_config *)0)->field)
#define CONFIG_SWITCH(section, name, field) {section, name, SETTING_SWITCH, offsetof(xeen_wox_config, field), 0, 0}
#define CONFIG_PERCENT(section, name, field) {section, name, SETTING_PERCENT, offsetof(xeen_wox_config, field), 0, 0}
#define CONFIG_TEXT(section, name, field)                                                                            \
    {section, name, SETTING_TEXT, offsetof(xeen_wox_config, field), CONFIG_SIZE(field), 0}
#define CONFIG_PATH(section, name, field)                                                                            \
    {section, name, SETTING_PATH, offsetof(xeen_wox_config, field), CONFIG_SIZE(field), 0}
#define CONFIG_AUTO_PATH(section, name, field, flag)                                                                 \
    {section, name, SETTING_AUTO_PATH, offsetof(xeen_wox_config, field), CONFIG_SIZE(field),                         \
     offsetof(xeen_wox_config, flag)}
#define OWN_SETTING(section, name) {section, name, SETTING_OWN, 0, 0, 0}
/* Each setting K: its section, its name in the file, how its value is read and (but for SETTING_OWN) the offset of
 * the field it sets, the field's size (a text or path) and the offset of a path's auto flag. */
static const struct {
    const char *section, *name;
    enum setting_kind kind;
    size_t offset, size, auto_flag;
} own[K_COUNT] = {
        [K_MUSIC] = OWN_SETTING("audio", "music_override"),
        [K_VOICEVOL] = CONFIG_PERCENT("audio", "voice_volume", voice_volume),
        [K_MUSICVOL] = CONFIG_PERCENT("audio", "music_volume", music_volume),
        [K_MIDIDEV] = CONFIG_TEXT("audio", "midi_device", midi_device),
        [K_MIDIPORT] = CONFIG_TEXT("audio", "midi_port", midi_port),
        [K_MT32] = CONFIG_PATH("files", "mt32_roms_dir", mt32_roms),
        [K_SC55] = CONFIG_PATH("files", "sc55_roms_dir", sc55_roms),
        [K_FULLSCREEN] = CONFIG_SWITCH("display", "fullscreen", fullscreen),
        [K_CDVOL] = CONFIG_PERCENT("audio", "cd_volume", cd_volume),
        [K_CDTRACKS] = CONFIG_AUTO_PATH("files", "cd_tracks_dir", cd_tracks, cd_tracks_auto),
        [K_IDENTIFY] = CONFIG_SWITCH("addons", "identify", addon_identify),
        [K_MAPMOVE] = CONFIG_SWITCH("addons", "map_movement", addon_map_movement),
        [K_TRAINPREV] = CONFIG_SWITCH("addons", "training_preview", addon_training_preview),
        [K_DMGNUM] = CONFIG_SWITCH("addons", "damage_numbers", addon_damage_numbers),
        [K_DMGSTACK] = CONFIG_SWITCH("addons", "damage_numbers.stack", addon_damage_numbers_stack),
        [K_MONDIFF] = CONFIG_SWITCH("addons", "monster_difficulty", addon_monster_difficulty),
        [K_SPELLDESC] = CONFIG_SWITCH("bugfix", "spell_description", bugfix_spell_description),
        [K_SPELLSHORT] = CONFIG_SWITCH("addons", "spell_short_description", addon_spell_short_description),
        [K_MOUSESCROLL] = CONFIG_SWITCH("addons", "mouse_scroll", addon_mouse_scroll),
        [K_TOWNICONS] = CONFIG_SWITCH("addons", "town_icons", addon_town_icons),
        [K_CHARDETAILS] = CONFIG_SWITCH("addons", "character_details", addon_character_details),
        [K_SPELLSORT] = CONFIG_SWITCH("addons", "spell_list_sort", addon_spell_list_sort),
        [K_WORLDMAP] = CONFIG_SWITCH("addons", "world_map", addon_world_map),
        [K_LEFTCLIP] = CONFIG_SWITCH("bugfix", "left_clip_colors", bugfix_left_clip_colors),
        [K_WIDTHTRIM] = CONFIG_SWITCH("bugfix", "sprite_width_trim", bugfix_sprite_width_trim),
        [K_QUESTJOURNAL] = CONFIG_SWITCH("addons", "quest_journal", addon_quest_journal),
        [K_QUESTUPDOWN] = CONFIG_SWITCH("bugfix", "quest_up_down", bugfix_quest_up_down),
        [K_SCALE] = OWN_SETTING("display", "scale"),
        [K_ASPECT] = CONFIG_SWITCH("display", "aspect", aspect),
        [K_AUTHCURSOR] = CONFIG_SWITCH("display", "authentic_cursor", authentic_cursor),
        [K_LOCKEDSCALE] = CONFIG_SWITCH("display", "locked_scale", locked_scale), /* Phase 332: a host option */
        /* auto (or empty): the process's working directory, resolved by the host; an explicit path is relative to
         * this file's directory (./auto names a directory "auto"). Phase 287: save_dir the same (a window's working
         * directory); xeen_cfg_dir's auto is the data directory (after --data-dir). */
        [K_DATA] = CONFIG_AUTO_PATH("files", "data_dir", data, data_auto),
        [K_SPEECH] = OWN_SETTING("audio", "speech"), /* Phase 286 */
        [K_SAVEDIR] = CONFIG_AUTO_PATH("files", "save_dir", save_dir, save_dir_auto),
        [K_XEENCFG] = CONFIG_AUTO_PATH("files", "xeen_cfg_dir", xeen_cfg_dir, xeen_cfg_auto),
        [K_PLEASEWAIT] = CONFIG_SWITCH("addons", "please_wait_skip", addon_please_wait_skip),
        [K_WIZEYE] = CONFIG_SWITCH("addons", "wizard_eye_plus", addon_wizard_eye_plus),
        [K_WIZEYE_INDOOR] = OWN_SETTING("addons", "wizard_eye_plus.indoor"),
        [K_WIZEYE_OUTDOOR] = OWN_SETTING("addons", "wizard_eye_plus.outdoor"),
        [K_WIZEYE_SKY] = OWN_SETTING("addons", "wizard_eye_plus.sky"),
};
#undef CONFIG_SWITCH
#undef CONFIG_PERCENT
#undef CONFIG_TEXT
#undef CONFIG_PATH
#undef CONFIG_AUTO_PATH
#undef OWN_SETTING
int xeen_wox_config_setting(size_t i, const char **section, const char **name)
{
    if (i >= (size_t)K_COUNT) {
        return 0;
    }
    *section = own[i].section;
    *name = own[i].name;
    return 1;
}
/* Phase 308: how apply_movie_setting reads a movie-player key's value:
 * - MOVIE_CUE: the cue sheet (a path; empty or auto: none); MOVIE_TRACKS: auto or 1..99 (the CD's last track);
 * - MOVIE_TEXT: a name (auto: empty) into its char array; MOVIE_SCORE: auto or 0..4294967295; MOVIE_PAYLOAD: auto
 *   or ten digits; MOVIE_BADGE: auto, 0, 1 or 2;
 * - MOVIE_TIMING: approximate or legacy; MOVIE_QUIRKS: on/off; MOVIE_CLOCK: auto, real or virtual;
 * - MOVIE_DUMP: one dump directory (empty or auto: none), of the key's kind. */
enum movie_value {
    MOVIE_CUE,
    MOVIE_TRACKS,
    MOVIE_TEXT,
    MOVIE_SCORE,
    MOVIE_PAYLOAD,
    MOVIE_BADGE,
    MOVIE_TIMING,
    MOVIE_QUIRKS,
    MOVIE_CLOCK,
    MOVIE_DUMP
};
#define MOVIE_KEY(name, section, value) {name, section, value, 0, 0, 0}
#define MOVIE_TEXT_KEY(name, section, field)                                                                         \
    {name, section, MOVIE_TEXT, offsetof(xeen_wox_config, movie.field), CONFIG_SIZE(movie.field), 0}
#define MOVIE_DUMP_KEY(name, kind) {name, "diagnostics", MOVIE_DUMP, 0, 0, kind}
/* The movie player's keys (xeen_wox_config.movie) and the section each belongs in; a text key's field (offset,
 * size) and a dump key's kind (movie.dump_kind: 1 frames, 2 hashes, 3 timings). */
static const struct {
    const char *name, *section;
    enum movie_value value;
    size_t offset, size;
    int dump_kind;
} player_keys[] = {
        MOVIE_KEY("cue", "files", MOVIE_CUE),
        MOVIE_KEY("cd_num_tracks", "files", MOVIE_TRACKS),
        MOVIE_TEXT_KEY("sequence", "playback", sequence),
        MOVIE_TEXT_KEY("command", "playback", command),
        MOVIE_KEY("score", "endings", MOVIE_SCORE),
        MOVIE_KEY("payload", "endings", MOVIE_PAYLOAD),
        MOVIE_KEY("badge", "endings", MOVIE_BADGE),
        MOVIE_KEY("timing", "timing", MOVIE_TIMING),
        MOVIE_KEY("original_timer_quirks", "timing", MOVIE_QUIRKS),
        MOVIE_KEY("clock", "timing", MOVIE_CLOCK),
        MOVIE_TEXT_KEY("dos_date", "timing", dos_date),
        MOVIE_TEXT_KEY("dos_time", "timing", dos_time),
        MOVIE_DUMP_KEY("dump_frames", 1),
        MOVIE_DUMP_KEY("dump_hashes", 2),
        MOVIE_DUMP_KEY("dump_timings", 3)};
#define PLAYER_KEYS (sizeof player_keys / sizeof player_keys[0])
_Static_assert(PLAYER_KEYS <= sizeof ((wox_parse *)0)->movie_seen / sizeof ((wox_parse *)0)->movie_seen[0],
               "one seen counter per movie player key");
#undef MOVIE_KEY
#undef MOVIE_TEXT_KEY
#undef MOVIE_DUMP_KEY
#undef CONFIG_SIZE
static const char *const sections[] = {"audio",  "files",       "display", "playback", "endings",
                                       "timing", "diagnostics", "addons",  "bugfix"};
const char *xeen_music_system_name(xeen_music_system s)
{
    static const char *const names[XEEN_MUSIC_SYSTEMS] = {"system", "mt32", "sc55",  "sbpro", "off",
                                                          "adlib",  "sb",   "pas",   "wave",  "tap10"};
    return (unsigned)s < XEEN_MUSIC_SYSTEMS ? names[s] : "?";
}
/* XEEN.CFG's words the drivers are chosen by (XEEN.DAT copies the file to DS 8D38: the sound word at offset 0, DS
 * 8D38; the music word at offset 4, DS 8D3C). */
enum { XEEN_CFG_SOUND_WORD = 0, XEEN_CFG_MUSIC_WORD = 4 };
/** The little-endian word at offset AT of XEEN.CFG's N bytes CFG; 0 without the file or when it is too short. */
static unsigned xeen_cfg_word(const uint8_t *cfg, size_t n, size_t at)
{
    return cfg && n >= at + 2u ? (unsigned)(cfg[at] | cfg[at + 1u] << 8) : 0u;
}
/* 15EC9's table (XEEN.DAT): XEEN.CFG music 1..9 -> the driver; XEEN.EXE's 14b02 has the same order (Phase 308). */
static const char *const music_drivers[9] = {"admus",  "blastmus", "promus",  "rolmus", "canmus",
                                             "specmus", "ibmmus",  "wavemus", "tapmus"};
void xeen_music_resolve(const xeen_wox_config *c, const uint8_t *cfg, size_t n, xeen_music_resolution *out, char *why,
                        size_t why_size)
{
    /* 15EC9: the driver (music_drivers) and its native system (ibmmus: none). */
    const char *const *drivers = music_drivers;
    static const xeen_music_system systems[9] = {XEEN_MUSIC_ADLIB, XEEN_MUSIC_SB,  XEEN_MUSIC_SBPRO,
                                                 XEEN_MUSIC_MT32,  XEEN_MUSIC_SC55, XEEN_MUSIC_PAS,
                                                 XEEN_MUSIC_OFF,   XEEN_MUSIC_WAVE, XEEN_MUSIC_TAP10};
    memset(out, 0, sizeof *out);
    out->xeen_cfg_present = cfg != NULL;
    out->word = xeen_cfg_word(cfg, n, XEEN_CFG_MUSIC_WORD);
    out->driver = out->word >= 1u && out->word <= 9u ? drivers[out->word - 1u] : "nullmus";
    if (!c->music_auto) {
        out->system = c->system;
        if (why && why_size) {
            snprintf(why, why_size, "music_override = %s (wox.cfg; XEEN.CFG's selection not used)", xeen_music_system_name(c->system));
        }
        return;
    }
    out->from_xeen_cfg = 1;
    out->system = out->word >= 1u && out->word <= 9u ? systems[out->word - 1u] : XEEN_MUSIC_OFF;
    out->unsupported = out->word == 7u;
    if (!why || !why_size) {
        return;
    }
    if (!cfg) {
        snprintf(why, why_size, "music_override = auto: no XEEN.CFG (the original asks for install.exe): no music; set an "
                                "explicit [audio] music_override to choose one");
    } else if (out->unsupported) {
        snprintf(why, why_size, "music_override = auto: XEEN.CFG music %u selects ibmmus (PC speaker), which is not "
                                "available: no music (set an explicit [audio] music_override to choose another)", out->word);
    } else {
        snprintf(why, why_size, "music_override = auto: XEEN.CFG music %u selects %s: %s", out->word, out->driver,
                 out->system == XEEN_MUSIC_OFF ? "no music" : xeen_music_system_name(out->system));
    }
}
void xeen_speech_resolve(const xeen_wox_config *c, const uint8_t *cfg, size_t n, xeen_speech_resolution *out,
                         char *why, size_t why_size)
{
    /* 15E52's table: XEEN.CFG sound 1..10 -> the digital driver (phase286/a/switch_tables.txt). */
    static const char *const drivers[10] = {"ibmsnd", "adsnd",    "nullsnd", "nullsnd", "source",
                                            "blastsnd", "prosnd", "specsnd", "covxsnd", "prosnd"};
    static const char *const modes[3] = {"auto", "on", "off"};
    unsigned w;
    memset(out, 0, sizeof *out);
    out->xeen_cfg_present = cfg != NULL;
    w = out->word = xeen_cfg_word(cfg, n, XEEN_CFG_SOUND_WORD);
    out->driver = w >= 1u && w <= 10u ? drivers[w - 1u] : "nullsnd";
    out->driver_voice = strcmp(out->driver, "nullsnd") != 0; /* nullsnd's entry 3: XOR AX, AX; RETF */
    out->pc_speaker = w == 1u;
    switch (c->speech) {
        case XEEN_SPEECH_ON:
            out->audible = 1;
            out->modelled = 1;
            out->fallback = !out->driver_voice || out->pc_speaker;
            break;
        case XEEN_SPEECH_OFF:
            out->forced_off = 1;
            break;
        case XEEN_SPEECH_AUTO:
        default: /* a value past the three is auto (the loader accepts only the three) */
            out->audible = out->driver_voice && !out->pc_speaker;
            out->modelled = out->driver_voice;
            break;
    }
    if (why && why_size) {
        snprintf(why, why_size, "speech = %s: XEEN.CFG %s%u (%s): %s", modes[c->speech < 3u ? c->speech : 0],
                 cfg ? "sound " : "absent, sound ", w, out->driver,
                 out->forced_off ? "no voice, subtitles and text shown, the panel's Speech held off"
                 : out->fallback && out->audible ? "voices audible (the SB driver's model stands for the driver)"
                 : out->audible                  ? "voices audible"
                 : out->pc_speaker               ? "the PC speaker's voices are not played (its blocking start kept)"
                                                 : "no voice (the driver plays none)");
    }
}
/* Phase 308: the movie player's speech and song selector from the shared settings (wox_config.h). */
int xeen_movie_subtitles(const xeen_wox_config *wox, const uint8_t *cfg, size_t n)
{
    unsigned w = xeen_cfg_word(cfg, n, XEEN_CFG_SOUND_WORD);
    if (wox && wox->speech == XEEN_SPEECH_ON) {
        return 0;
    }
    if (wox && wox->speech == XEEN_SPEECH_OFF) {
        return 1;
    }
    return w < 2u; /* 1eaee: CMP word [427E], 2 */
}

unsigned xeen_movie_music_selector(const xeen_wox_config *wox, const uint8_t *cfg, size_t n)
{
    /* 14b02's table (XEEN.CFG word 4, 1..9; music_drivers' order); anything else nullmus. */
    const char *const *drivers = music_drivers;
    const char *d;
    if (!wox || wox->music_auto) {
        return xeen_cfg_word(cfg, n, XEEN_CFG_MUSIC_WORD);
    }
    d = xeen_music_system_driver(wox->system);
    for (unsigned i = 0; i < 9u; ++i) {
        if (!strcmp(d, drivers[i])) {
            return i + 1u;
        }
    }
    return 0;
}

const char *xeen_music_system_driver(xeen_music_system s)
{
    static const char *const drivers[XEEN_MUSIC_SYSTEMS] = {"rolmus", "rolmus",   "canmus",  "promus",  "nullmus",
                                                            "admus",  "blastmus", "specmus", "wavemus", "tapmus"};
    return (unsigned)s < XEEN_MUSIC_SYSTEMS ? drivers[s] : "?";
}
/* Phase 335 (owner request): the separator of the paths wox builds (wox.cfg beside the executable, the default and
 * relative directories, the ROM files), so that a Windows path shown to the player has backslashes only. */
#if defined(_WIN32)
#define WOX_SEPARATOR "\\"
#define WOX_DEFAULT_MT32 "roms\\mt32"
#define WOX_DEFAULT_SC55 "roms\\sc55"
#else
#define WOX_SEPARATOR "/"
#define WOX_DEFAULT_MT32 "roms/mt32"
#define WOX_DEFAULT_SC55 "roms/sc55"
#endif
/** Returns nonzero when path P is absolute (on Windows also a drive or backslash path). */
static int is_absolute(const char *p)
{
#if defined(_WIN32)
    return p[0] == '\\' || p[0] == '/' || (p[0] && p[1] == ':');
#else
    return p[0] == '/';
#endif
}
/**
 * Resolves VALUE against the configuration's directory BASE (an absolute VALUE is kept) into OUT;
 * returns 0 when it does not fit.
 */
static int set_path(char *out, size_t n, const char *base, const char *value)
{
    int w;
    if (is_absolute(value) || !base || !*base) {
        w = snprintf(out, n, "%s", value);
    } else {
        w = snprintf(out, n, "%s" WOX_SEPARATOR "%s", base, value);
    }
    return w >= 0 && (size_t)w < n;
}
/**
 * Sets OUT to the default directory FALLBACK resolved against BASE (empty when it does not fit). Phase 331: the
 * former environment overrides of the ROM and CD-rip directories are gone (owner decision): auto is roms/mt32,
 * roms/sc55 and music beside wox.cfg.
 */
static void default_dir(char *out, size_t n, const char *base, const char *fallback)
{
    if (!set_path(out, n, base, fallback)) {
        out[0] = 0;
    }
}
void xeen_wox_config_defaults(xeen_wox_config *c, const char *base)
{
    memset(c, 0, sizeof *c);
    c->system = XEEN_MUSIC_EXTERNAL;
    c->voice_volume = c->music_volume = 100u;
    default_dir(c->mt32_roms, sizeof c->mt32_roms, base, WOX_DEFAULT_MT32);
    default_dir(c->sc55_roms, sizeof c->sc55_roms, base, WOX_DEFAULT_SC55);
    c->cd_volume = 100u;
    c->cd_tracks_auto = 1;
    default_dir(c->cd_tracks, sizeof c->cd_tracks, base, "music");
    c->scale = 3u;
    c->data_auto = 1;
    snprintf(c->data, sizeof c->data, ".");
    c->save_dir_auto = 1;
    snprintf(c->save_dir, sizeof c->save_dir, ".");
    c->xeen_cfg_auto = 1;
}
/** Strips leading and trailing white space from S in place and returns its new start. */
static char *trim(char *s)
{
    char *e;
    while (*s && isspace((unsigned char)*s)) {
        ++s;
    }
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) {
        *--e = 0;
    }
    return s;
}
/** Formats a configuration error "PATH:LINE: WHAT: ITEM" into ERROR and returns 0. */
static int fail(char *error, size_t n, const char *path, unsigned line, const char *what,
                const char *item)
{
    if (error && n) {
        snprintf(error, n, "%s:%u: %s%s%s", path, line, what, item ? ": " : "", item ? item : "");
    }
    return 0;
}
/**
 * Parses a switch (Phase 336: every boolean setting): on, or off with empty and auto meaning off (exactly these
 * spellings); returns 0 for anything else.
 */
static int parse_switch(const char *v, int *out)
{
    if (!strcmp(v, "on")) {
        *out = 1;
        return 1;
    }
    if (!*v || !strcmp(v, "auto") || !strcmp(v, "off")) {
        *out = 0;
        return 1;
    }
    return 0;
}
/** Parses the window scale 1..XEEN_WOX_SCALE_MAX, digits only without a leading zero (empty or auto meaning 3);
 * returns 0 for anything else. */
static int parse_scale(const char *v, unsigned *out)
{
    unsigned n = 0u;
    if (!*v || !strcmp(v, "auto")) {
        *out = 3u;
        return 1;
    }
    if (v[0] < '1' || v[0] > '9') {
        return 0;
    }
    for (const char *c = v; *c; ++c) {
        if (*c < '0' || *c > '9' || n > XEEN_WOX_SCALE_MAX) {
            return 0;
        }
        n = n * 10u + (unsigned)(*c - '0');
    }
    if (n > XEEN_WOX_SCALE_MAX) {
        return 0;
    }
    *out = n;
    return 1;
}
/** Parses a volume percentage 0..200 (100 = unscaled; empty or auto meaning 100); returns 0 for anything else. */
static int parse_percent(const char *v, unsigned *out)
{
    char *end;
    unsigned long x;
    if (!*v || !strcmp(v, "auto")) {
        *out = 100u;
        return 1;
    }
    if (!isdigit((unsigned char)*v)) {
        return 0;
    }
    errno = 0;
    x = strtoul(v, &end, 10);
    if (errno || *end || x > 200ul) {
        return 0;
    }
    *out = (unsigned)x;
    return 1;
}
/**
 * Parses the music system setting (system is the external MIDI device); returns 0 for an unknown value.
 */
static int parse_system(const char *v, xeen_music_system *out)
{
    if (!*v || !strcmp(v, "system")) {
        *out = XEEN_MUSIC_EXTERNAL;
    } else if (!strcmp(v, "mt32")) {
        *out = XEEN_MUSIC_MT32;
    } else if (!strcmp(v, "sc55")) {
        *out = XEEN_MUSIC_SC55;
    } else if (!strcmp(v, "sbpro")) {
        *out = XEEN_MUSIC_SBPRO;
    } else if (!strcmp(v, "off")) {
        *out = XEEN_MUSIC_OFF;
    } else {
        /* Phase 286: the remaining original drivers but the PC speaker's. */
        for (unsigned k = XEEN_MUSIC_ADLIB; k < XEEN_MUSIC_SYSTEMS; k++) {
            if (!strcmp(v, xeen_music_system_name((xeen_music_system)k))) {
                *out = (xeen_music_system)k;
                return 1;
            }
        }
        return 0;
    }
    return 1;
}
/** Stores the directory part of PATH in OUT ("." when it has none). */
static void directory_of(const char *path, char *out, size_t n)
{
    const char *s = strrchr(path, '/');
#if defined(_WIN32)
    const char *b = strrchr(path, '\\');
    if (b && (!s || b > s)) {
        s = b;
    }
#endif
    if (!s) {
        snprintf(out, n, ".");
    } else if (s == path) {
        snprintf(out, n, "/");
    } else {
        snprintf(out, n, "%.*s", (int)(s - path), path);
    }
}
/** Records a rejected line's reason and item; returns 0. */
static int reject(wox_parse *p, const char *what, const char *item)
{
    p->what = what;
    p->item = item;
    return 0;
}
/** Reads the line "[NAME]" (KEY trimmed) into the current section; returns 0 for a malformed or unknown one. */
static int parse_section(wox_parse *p, const char *key)
{
    size_t len = strlen(key);
    int k;
    if (len < 3u || key[len - 1u] != ']' || len - 2u >= sizeof p->section) {
        return reject(p, "malformed section", key);
    }
    memcpy(p->section, key + 1, len - 2u);
    p->section[len - 2u] = 0;
    for (k = 0; k < (int)(sizeof sections / sizeof *sections) && strcmp(p->section, sections[k]);
         ++k) {
    }
    if (k == (int)(sizeof sections / sizeof *sections)) {
        return reject(p, "unknown section", p->section);
    }
    return 1;
}
/**
 * Splits the line KEY ("key = value", trimmed) in place into *NAME (trimmed, compared exactly: Phase 336 reads no
 * '-' as '_') and *VALUE (trimmed, one pair of matching quotes removed); returns 0 without '=' or for an
 * unterminated quote.
 */
static int split_setting(wox_parse *p, char *key, char **name, char **value)
{
    char *eq, *v;
    size_t len;
    if (!(eq = strchr(key, '='))) {
        return reject(p, "expected key = value", key);
    }
    *eq = 0;
    key = trim(key);
    v = trim(eq + 1);
    len = strlen(v);
    if (*v == '"' || *v == '\'') {
        if (len < 2u || v[len - 1u] != *v) {
            return reject(p, "unterminated quoted value", key);
        }
        v[len - 1u] = 0;
        ++v;
    }
    *name = key;
    *value = v;
    return 1;
}
/**
 * Returns the K_* index of setting NAME, K_COUNT + 1 + its index for one of the movie player's keys
 * (player_keys, Phase 308), or -1 for an unknown setting.
 */
static int find_setting(const char *name)
{
    int k;
    for (k = 0; k < K_COUNT && strcmp(name, own[k].name); ++k) {
    }
    if (k < K_COUNT) {
        return k;
    }
    for (size_t i = 0; i < PLAYER_KEYS; ++i) {
        if (!strcmp(name, player_keys[i].name)) {
            return K_COUNT + 1 + (int)i;
        }
    }
    return -1;
}
/** The field setting K sets (own[K].offset into the configuration); not for a SETTING_OWN setting. */
static void *setting_field(wox_parse *p, int k)
{
    return (char *)p->c + own[k].offset;
}
/**
 * A setting's placement, checked before its value: a second occurrence (SEEN counts them) and one outside its
 * SECTION are rejected, in that order. Returns 1 when NAME may be applied.
 */
static int placed(wox_parse *p, unsigned *seen, const char *name, const char *section)
{
    if ((*seen)++) {
        return reject(p, "duplicate setting", name);
    }
    if (strcmp(p->section, section)) {
        snprintf(p->where, sizeof p->where, "%s belongs in [%s]", name, section);
        return reject(p, p->where, NULL);
    }
    return 1;
}
/** Sets a ROM or CD-rip directory OUT (N bytes) from VALUE; empty or auto keeps the default. */
static int set_directory(wox_parse *p, const char *name, const char *value, char *out, size_t n)
{
    if (!*value || !strcmp(value, "auto")) {
        return 1; /* the defaults (xeen_wox_config_defaults) */
    }
    if (!set_path(out, n, p->base, value)) {
        return reject(p, "path too long", name);
    }
    return 1;
}
/* Copies VALUE ("auto" or empty: "") into OUT (CAP bytes); 0 when it does not fit. */
static int set_text(wox_parse *p, const char *name, const char *value, char *out, size_t cap)
{
    if (!strcmp(value, "auto")) {
        value = "";
    }
    if (strlen(value) >= cap) {
        return reject(p, "value too long", name);
    }
    strcpy(out, value);
    return 1;
}
/** A SETTING_AUTO_PATH setting K: an explicit directory replaces the default and clears K's auto flag. */
static int set_auto_directory(wox_parse *p, int k, const char *name, const char *value)
{
    xeen_wox_config *c = p->c;
    if (!*value || !strcmp(value, "auto")) {
        return 1; /* auto: the host resolves it (the flag stays set) */
    }
    if (!set_directory(p, name, value, (char *)c + own[k].offset, own[k].size)) {
        return 0;
    }
    *(int *)(void *)((char *)c + own[k].auto_flag) = 0;
    return 1;
}
/**
 * [audio] music_override (Phase 336; music before): auto (Phase 286: XEEN.CFG's selection, resolved by
 * xeen_music_resolve; the system keeps its default) or a native system, which overrides XEEN.CFG's.
 */
static int apply_music(wox_parse *p, const char *value)
{
    xeen_wox_config *c = p->c;
    c->music_auto = !strcmp(value, "auto");
    if (c->music_auto) {
        return 1;
    }
    if (!parse_system(value, &c->system)) {
        return reject(p,
                      "invalid music_override (auto, system, mt32, sc55, sbpro, adlib, sb, pas, wave, tap10, off)",
                      value);
    }
    return 1;
}
/** [audio] speech (Phase 286): auto (XEEN.CFG's digital sound driver), on or off. */
static int apply_speech(wox_parse *p, const char *value)
{
    xeen_wox_config *c = p->c;
    if (!strcmp(value, "auto")) {
        c->speech = XEEN_SPEECH_AUTO;
    } else if (!strcmp(value, "on")) {
        c->speech = XEEN_SPEECH_ON;
    } else if (!strcmp(value, "off")) {
        c->speech = XEEN_SPEECH_OFF;
    } else {
        return reject(p, "invalid speech (auto, on, off)", value);
    }
    return 1;
}
/** [addons] wizard_eye_plus.indoor, .outdoor and .sky (Phase 318): the style of that map kind, 1px, 2px or black. */
static int apply_wizard_eye_style(wox_parse *p, int k, const char *name, const char *value)
{
    static const char *const styles[3] = {"black", "1px", "2px"}; /* the add-on's XEEN_WEP_* order */
    for (unsigned i = 0; i < 3u; ++i) {
        if (!strcmp(value, styles[i])) {
            p->c->addon_wizard_eye_plus_style[k - K_WIZEYE_INDOOR] = i;
            return 1;
        }
    }
    snprintf(p->where, sizeof p->where, "%s must be 1px, 2px or black", name);
    return reject(p, p->where, value);
}
/**
 * Applies setting K (named NAME in the file) with VALUE: its placement first (placed), then its value by its kind
 * (own[K]); the music system, the speech, the wizard_eye_plus styles and the scale by their own rules.
 */
static int apply_setting(wox_parse *p, int k, const char *name, const char *value)
{
    if (!placed(p, &p->seen[k], name, own[k].section)) {
        return 0;
    }
    switch (own[k].kind) {
        case SETTING_SWITCH:
            return parse_switch(value, setting_field(p, k)) || reject(p, "must be on or off", name);
        case SETTING_PERCENT:
            return parse_percent(value, setting_field(p, k)) ||
                   reject(p, "volume must be a whole percent 0..200", name);
        case SETTING_TEXT:
            return set_text(p, name, value, setting_field(p, k), own[k].size);
        case SETTING_PATH:
            return set_directory(p, name, value, setting_field(p, k), own[k].size);
        case SETTING_AUTO_PATH:
            return set_auto_directory(p, k, name, value);
        case SETTING_OWN:
            break;
    }
    if (k == K_MUSIC) {
        return apply_music(p, value);
    }
    if (k == K_SPEECH) {
        return apply_speech(p, value);
    }
    if (k >= K_WIZEYE_INDOOR && k <= K_WIZEYE_SKY) {
        return apply_wizard_eye_style(p, k, name, value);
    }
    return parse_scale(value, &p->c->scale) || reject(p, "scale must be a whole number 1..10", value); /* K_SCALE */
}
/* (wox_config.h) strtoul alone is not the test: it skips white space, takes a sign (negating the result) and answers
 * ULONG_MAX with ERANGE on overflow, which is 4294967295 itself where unsigned long has 32 bits. */
int xeen_wox_whole_number(const char *value, unsigned long low, unsigned long high, unsigned long *out)
{
    char *end;
    unsigned long n;
    if (!isdigit((unsigned char)*value)) {
        return 0;
    }
    errno = 0;
    n = strtoul(value, &end, 10);
    if (errno || *end || n < low || n > high) {
        return 0;
    }
    *out = n;
    return 1;
}
/** [files] cd_num_tracks: auto (the default 0) or the CD's last track 1..99. */
static int movie_tracks(wox_parse *p, const char *value)
{
    unsigned long n;
    if (!strcmp(value, "auto")) {
        return 1;
    }
    if (!xeen_wox_whole_number(value, 1u, 99u, &n)) {
        return reject(p, "cd_num_tracks must be auto or 1..99", value);
    }
    p->c->movie.cd_num_tracks = (unsigned)n;
    return 1;
}
/** [endings] score: auto (none given) or 0..4294967295. */
static int movie_score(wox_parse *p, const char *value)
{
    unsigned long n;
    if (!strcmp(value, "auto")) {
        return 1;
    }
    if (!xeen_wox_whole_number(value, 0u, 4294967295ul, &n)) {
        return reject(p, "score must be auto or 0..4294967295", value);
    }
    p->c->movie.score = (uint32_t)n;
    p->c->movie.score_given = 1;
    return 1;
}
/** [endings] payload: auto (none) or exactly ten ASCII digits. */
static int movie_payload(wox_parse *p, const char *value)
{
    if (!strcmp(value, "auto")) {
        return 1;
    }
    if (strlen(value) != 10u || strspn(value, "0123456789") != 10u) {
        return reject(p, "payload must be auto or exactly ten ASCII digits", value);
    }
    strcpy(p->c->movie.payload, value);
    return 1;
}
/** [endings] badge: auto (none given), 0, 1 or 2. */
static int movie_badge(wox_parse *p, const char *value)
{
    if (!strcmp(value, "auto")) {
        return 1;
    }
    if (strcmp(value, "0") && strcmp(value, "1") && strcmp(value, "2")) {
        return reject(p, "badge must be auto, 0, 1 or 2", value);
    }
    p->c->movie.badge = (unsigned)(value[0] - '0');
    p->c->movie.badge_given = 1;
    return 1;
}
/** [diagnostics] dump_frames, dump_hashes or dump_timings (key I): one dump directory; empty or auto: none. */
static int movie_dump(wox_parse *p, int i, const char *name, const char *value)
{
    xeen_wox_config *c = p->c;
    if (!*value || !strcmp(value, "auto")) {
        return 1;
    }
    if (c->movie.dump_kind) {
        return reject(p, "use one of dump_frames, dump_hashes and dump_timings", name);
    }
    c->movie.dump_kind = player_keys[i].dump_kind;
    return set_directory(p, name, value, c->movie.dump, sizeof c->movie.dump);
}
/**
 * Phase 308: applies movie-player key I (player_keys) with VALUE: its placement first, as the other settings'
 * (placed); then its value by its kind (enum movie_value).
 */
static int apply_movie_setting(wox_parse *p, int i, const char *name, const char *value)
{
    xeen_wox_config *c = p->c;
    if (!placed(p, &p->movie_seen[i], name, player_keys[i].section)) {
        return 0;
    }
    switch (player_keys[i].value) {
        case MOVIE_CUE:
            if (!*value || !strcmp(value, "auto")) {
                c->movie.cue[0] = 0;
                return 1;
            }
            return set_directory(p, name, value, c->movie.cue, sizeof c->movie.cue);
        case MOVIE_TRACKS:
            return movie_tracks(p, value);
        case MOVIE_TEXT:
            return set_text(p, name, value, (char *)c + player_keys[i].offset, player_keys[i].size);
        case MOVIE_SCORE:
            return movie_score(p, value);
        case MOVIE_PAYLOAD:
            return movie_payload(p, value);
        case MOVIE_BADGE:
            return movie_badge(p, value);
        case MOVIE_TIMING:
            if (strcmp(value, "approximate") && strcmp(value, "legacy")) {
                return reject(p, "timing must be approximate or legacy", value);
            }
            c->movie.legacy_timing = !strcmp(value, "legacy");
            return 1;
        case MOVIE_QUIRKS:
            return parse_switch(value, &c->movie.original_timer_quirks) || reject(p, "must be on or off", name);
        case MOVIE_CLOCK:
            if (strcmp(value, "auto") && strcmp(value, "real") && strcmp(value, "virtual")) {
                return reject(p, "clock must be auto, real or virtual", value);
            }
            c->movie.virtual_clock = !strcmp(value, "virtual");
            return 1;
        case MOVIE_DUMP:
            return movie_dump(p, i, name, value);
    }
    return 1;
}
/**
 * Reads one line (BOM, comments and blank lines skipped, a section, or a setting); returns 0 with the
 * reason recorded for a line the configuration rejects.
 */
static int parse_line(wox_parse *p, char *line, unsigned number)
{
    char *key = line, *name, *value;
    int k;
    if (number == 1u && !memcmp(key, "\xef\xbb\xbf", 3)) {
        key += 3;
    }
    key = trim(key);
    if (!*key || *key == '#' || *key == ';') {
        return 1;
    }
    if (*key == '[') {
        return parse_section(p, key);
    }
    if (!split_setting(p, key, &name, &value)) {
        return 0;
    }
    k = find_setting(name);
    if (k < 0) {
        return reject(p, "unknown setting", name);
    }
    return k > K_COUNT ? apply_movie_setting(p, k - K_COUNT - 1, name, value) : apply_setting(p, k, name, value);
}
int xeen_wox_config_load(const char *path, xeen_wox_config *c, char *error, size_t n)
{
    FILE *f;
    char line[2048], base[1024];
    unsigned number = 0;
    wox_parse p;
    if (!path || !c) {
        return fail(error, n, "wox.cfg", 0, "no configuration path", NULL);
    }
    directory_of(path, base, sizeof base);
    xeen_wox_config_defaults(c, base);
    if (snprintf(c->path, sizeof c->path, "%s", path) >= (int)sizeof c->path) {
        return fail(error, n, path, 0, "path too long", NULL);
    }
    f = fopen(path, "rb");
    if (!f) {
        return fail(error, n, path, 0, "cannot open", strerror(errno));
    }
    memset(&p, 0, sizeof p);
    p.c = c;
    p.base = base;
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        ++number;
        if (len == sizeof line - 1u && line[len - 1u] != '\n' && !feof(f)) {
            fclose(f);
            return fail(error, n, path, number, "line too long", NULL);
        }
        if (!parse_line(&p, line, number)) {
            fclose(f);
            return fail(error, n, path, number, p.what, p.item);
        }
    }
    if (ferror(f)) {
        fclose(f);
        return fail(error, n, path, number, "read error", NULL);
    }
    fclose(f);
    return 1;
}
/** Returns nonzero when P is an existing directory. */
static int readable_dir(const char *p)
{
    struct stat st;
    return !stat(p, &st) && S_ISDIR(st.st_mode);
}
/** Returns nonzero when DIR/NAME can be opened for reading. */
static int readable_file(const char *dir, const char *name)
{
    char p[1100];
    FILE *f;
    if (snprintf(p, sizeof p, "%s" WOX_SEPARATOR "%s", dir, name) >= (int)sizeof p) {
        return 0;
    }
    if (!(f = fopen(p, "rb"))) {
        return 0;
    }
    fclose(f);
    return 1;
}
/** The ROM check's failure: its message into ERROR (when there is one with room, truncated to N); returns 0. */
static int rom_error(char *error, size_t n, const char *fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 3, 4)))
#endif
        ;
static int rom_error(char *error, size_t n, const char *fmt, ...)
{
    if (error && n) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(error, n, fmt, ap);
        va_end(ap);
    }
    return 0;
}
/** Munt's set in DIR: the directory, then MT32_CONTROL.ROM, then MT32_PCM.ROM; the first missing one fails. */
static int mt32_roms_present(const char *dir, char *error, size_t n)
{
    static const char *const files[] = {"MT32_CONTROL.ROM", "MT32_PCM.ROM"};
    if (!readable_dir(dir)) {
        return rom_error(error, n, "MT-32 ROM directory not found: %s (set [files] mt32_roms_dir)", dir);
    }
    for (unsigned i = 0; i < 2u; ++i) {
        if (!readable_file(dir, files[i])) {
            return rom_error(error, n, "MT-32 ROM missing: %s" WOX_SEPARATOR "%s (set [files] mt32_roms_dir)", dir, files[i]);
        }
    }
    return 1;
}
/** The systems Nuked-SC55 plays: sc55 (canmus) and, since Phase 286, wave (wavemus) and tap10 (tapmus). */
static int plays_on_sc55(xeen_music_system s)
{
    return s == XEEN_MUSIC_SC55 || s == XEEN_MUSIC_WAVE || s == XEEN_MUSIC_TAP10;
}
/* Only the selected system's ROMs are checked: Munt's two files, or Nuked-SC55's directory (the synth recognizes its
 * set by the hashes); the other systems need none and touch no file. */
int xeen_wox_config_check_roms(const xeen_wox_config *c, char *error, size_t n)
{
    if (c->system == XEEN_MUSIC_MT32) {
        return mt32_roms_present(c->mt32_roms, error, n);
    }
    if (plays_on_sc55(c->system) && !readable_dir(c->sc55_roms)) {
        return rom_error(error, n, "SC-55 ROM directory not found: %s (set [files] sc55_roms_dir; %s plays on Nuked-SC55)",
                         c->sc55_roms, xeen_music_system_name(c->system));
    }
    return 1;
}
int xeen_wox_config_create(const char *path)
{
    FILE *f;
    if ((f = fopen(path, "rb")) != NULL) {
        fclose(f);
        return 1;
    }
    if (!(f = fopen(path, "wb"))) {
        return 0;
    }
    fputs(xeen_wox_config_default_text(), f);
    return fclose(f) == 0;
}
const char *xeen_wox_config_default_text(void)
{
    return "# WoX native configuration (wox.cfg). Not the game's XEEN.CFG.\n"
          "# Relative paths are relative to this file's directory.\n\n"
          "[audio]\n"
          "# music_override: auto plays the music card chosen in the game's XEEN.CFG (the\n"
          "# DOS installer's choice; PC speaker, None or no XEEN.CFG: no music). Any other\n"
          "# value overrides XEEN.CFG's choice and plays that original DOS music driver\n"
          "# (all from INTRO.CC) instead:\n"
          "#   system  the MT-32 arrangement (rolmus) to the system MIDI output\n"
          "#   mt32    Roland MT-32 / LAPC-1 (rolmus) on Munt (mt32_roms_dir)\n"
          "#   sc55    Roland Sound Canvas / GS (canmus) on Nuked-SC55 (sc55_roms_dir)\n"
          "#   sbpro   Sound Blaster Pro (promus), two OPL2, stereo, no ROMs\n"
          "#   sb      Sound Blaster (blastmus), one OPL2, mono, no ROMs\n"
          "#   pas     Pro-Audio Spectrum (specmus: the Sound Blaster driver, byte for byte)\n"
          "#   adlib   AdLib (admus), one OPL2, mono, no ROMs\n"
          "#   wave    Wave Blaster (wavemus) on Nuked-SC55 (sc55_roms_dir)\n"
          "#   tap10   Roland TAP-10 (tapmus) on Nuked-SC55 (sc55_roms_dir)\n"
          "#   off     no music\n"
          "# The PC speaker's music driver is not available. Read at startup.\n"
          "music_override = system\n"
          "# Spoken voices: auto (the game's XEEN.CFG sound selection: None plays no voice\n"
          "# and shows the subtitles; the PC speaker's voices are not played), on (voices\n"
          "# play whatever XEEN.CFG says) or off (no voice: the scenes show their subtitles\n"
          "# and the dialogue its text; the control panel's Speech stays off).\n"
          "speech = auto\n"
          "# Voice and software-synth reference levels in whole percent, 0..200.\n"
          "voice_volume = 100\n"
          "music_volume = 100\n"
          "# CD voice tracks (the OGG rips) in whole percent, 0..200; 100 = unscaled.\n"
          "cd_volume = 100\n"
          "# Windows system MIDI device number or name (auto = default).\n"
          "midi_device = auto\n"
          "# Linux ALSA sequencer destination CLIENT:PORT (auto = default).\n"
          "midi_port = auto\n\n"
          "[files]\n"
          "# The game's data directory (XEEN.CC, DARK.CC, the optional INTRO.CC).\n"
          "# auto = the current working directory; a relative path is relative to this\n"
          "# file's directory. The command line's --data-dir DIR overrides it.\n"
          "data_dir = auto\n"
          "# Where the game writes its save slots (auto = the current working directory).\n"
          "# An explicit directory must exist and be writable (it is not created); a\n"
          "# relative path is relative to this file's directory. --save-dir DIR overrides it.\n"
          "save_dir = auto\n"
          "# The directory holding the game's XEEN.CFG (the DOS installer's sound and music\n"
          "# choices, read by music_override = auto and speech = auto; the game writes it\n"
          "# back there; a directory, not the file itself).\n"
          "# auto = the data directory above (not the save directory); a relative path is\n"
          "# relative to this file's directory.\n"
          "xeen_cfg_dir = auto\n"
          "# ROM directories (auto = roms/mt32, roms/sc55 beside this file).\n"
          "mt32_roms_dir = auto\n"
          "sc55_roms_dir = auto\n"
          "# CD voice rips xeen02.ogg .. xeen60.ogg (auto = music beside this file).\n"
          "cd_tracks_dir = auto\n\n"
          "[display]\n"
          "# on starts fullscreen; Alt+Enter still toggles. --fullscreen overrides.\n"
          "fullscreen = off\n"
          "# Window scale 1..10: the 320x200 picture times this (without this line: 3).\n"
          "scale = 6\n"
          "# on shows the picture at 4:3 as on a CRT (taller pixels); off = square pixels.\n"
          "aspect = off\n"
          "# on moves the mouse cursor only when the game shows a frame, as the original;\n"
          "# off = the system pointer follows the mouse at once.\n"
          "authentic_cursor = off\n"
          "# on keeps the picture at the scale above (with aspect) in a larger or fullscreen\n"
          "# window, and the window cannot be made smaller; off = the largest scale that fits.\n"
          "locked_scale = off\n\n"
          "[addons]\n"
          "# Optional additions that are NOT part of the original game (all off by default).\n"
          "# identify: identified item types show their statistics in the items list\n"
          "# (kept per save slot in ADDONnn.WOX).\n"
          "identify = off\n"
          "# map_movement: the map (M) pans with the arrow keys and a mouse drag; a click\n"
          "# inside it does not close it.\n"
          "map_movement = off\n"
          "# training_preview: at the training grounds a Prev button (P) shows the levels\n"
          "# a member can train to now, their costs and what stops the row.\n"
          "training_preview = off\n"
          "# damage_numbers: damage (red), healing (green) and misses (\"Miss\") shown over\n"
          "# the portraits and the monsters, in exploration and in combat.\n"
          "damage_numbers = off\n"
          "# damage_numbers.stack: with damage_numbers, one running total per target\n"
          "# instead of single rising values.\n"
          "damage_numbers.stack = off\n"
          "# monster_difficulty: an icon over each monster in the 3D view shows how\n"
          "# dangerous it is for the party (green sword, gold crossed swords, orange\n"
          "# shield, red skull; nothing for a trivial one); far monsters get a dot.\n"
          "monster_difficulty = off\n"
          "# spell_short_description: while the Cast Spell dialog is open, a small box\n"
          "# over the movement arrows describes the ready spell (or the spell highlighted\n"
          "# in the New list).\n"
          "spell_short_description = off\n"
          "# mouse_scroll: the mouse wheel scrolls the spell lists (Cast New, the guild),\n"
          "# the Quests panel, the Awards and the inn's roster, one line per notch, while\n"
          "# the pointer is over the list.\n"
          "mouse_scroll = off\n"
          "# town_icons: icons for the town places (blacksmith, tavern, temple, guild,\n"
          "# training, bank, mirror, tents, Castleview's pyramid and Ellinger's Tower)\n"
          "# and the Skyroad's tents on the map (M) and the minimap, once the map shows\n"
          "# their cell (or under Wizard Eye).\n"
          "town_icons = off\n"
          "# character_details: in the character view, the stat popups explain what a\n"
          "# value does; a click on a skill or a condition in its popup shows what it\n"
          "# does in a window beside it.\n"
          "character_details = off\n"
          "# spell_list_sort: a button (O) above the spell lists (Cast New, the guild)\n"
          "# switches a character's list between A-Z and grouped by type (kept per save\n"
          "# slot in ADDONnn.WOX).\n"
          "spell_list_sort = off\n"
          "# world_map: in the map (M), W or the World Map button shows the whole\n"
          "# world's surface on one screen (only what the map has seen); arrows or a\n"
          "# drag pan it, the wheel or PgUp/PgDn zoom (100%, 50%, the whole world);\n"
          "# Esc closes it.\n"
          "world_map = off\n"
          "# quest_journal: in the Quests dialog (V), the quest items are numbered: a\n"
          "# number or a click shows a description of the item (Space closes it); with\n"
          "# world_map on as well, a quest's or a note's number or a click shows its\n"
          "# place on the world map (Esc goes back).\n"
          "quest_journal = off\n"
          "# please_wait_skip: the \"Please Wait...\" box the original shows while a map\n"
          "# loads (leaving a shop or a town, stairs, a map edge, Town Portal) is not shown.\n"
          "# off (the default) shows the original box; on hides it (the load itself\n"
          "# is unchanged and its 200 ms showing is skipped; a fall's \"Oops...\" is\n"
          "# always shown).\n"
          "please_wait_skip = off\n"
          "# wizard_eye_plus: while Wizard Eye is on, a click on the minimap (the =\n"
          "# automap) hides again the cells the party has not seen; with this add-on they\n"
          "# show what is there through a checkerboard instead. Its style for indoor maps,\n"
          "# outdoor maps and sky maps (where the party can fall: the Skyroad, the cloud\n"
          "# maps, the Elemental Planes, ...): 1px (every other pixel, the rest the\n"
          "# original's fog), 2px (the same in 2x2 blocks) or black (every other pixel,\n"
          "# the rest black).\n"
          "wizard_eye_plus = off\n"
          "wizard_eye_plus.indoor = black\n"
          "wizard_eye_plus.outdoor = black\n"
          "wizard_eye_plus.sky = black\n"
          "# Also on the command line: --addon identify map_movement training_preview\n"
          "# damage_numbers damage_numbers.stack monster_difficulty\n"
          "# spell_short_description mouse_scroll town_icons character_details\n"
          "# spell_list_sort world_map quest_journal please_wait_skip wizard_eye_plus\n"
          "# (adds to these).\n\n"
          "[bugfix]\n"
          "# Corrections of original game errors that are NOT original behavior (all off\n"
          "# by default).\n"
          "# spell_description: corrected spell descriptions in the guild's Spell Info and\n"
          "# Buy Spells (wrong damage, targets and effects; permanent downsides in red).\n"
          "spell_description = off\n"
          "# left_clip_colors: monsters and objects cut by the 3D view's left edge show their\n"
          "# own colours (the original shows a few stray pixels from elsewhere in the sprite).\n"
          "left_clip_colors = off\n"
          "# sprite_width_trim: shrunk pictures are trimmed to their stated width (the original\n"
          "# draws one extra column where a picture's rows are longer, e.g. the castle zoom).\n"
          "sprite_width_trim = off\n"
          "# quest_up_down: U and D scroll the Quests dialog (V) as its Up and Down buttons\n"
          "# say (the original takes only the arrow keys and a click there).\n"
          "quest_up_down = off\n"
          "# Also on the command line: --bugfix spell_description left_clip_colors\n"
          "# sprite_width_trim quest_up_down (adds to these).\n";
}
/*
 * The platform primitives of the save and configuration locations, one per rule below: whether the process may
 * create files in a directory, its working directory, a directory's creation and the executable's own path. Each
 * keeps its platform's call and its success test exactly (errno is the failing call's).
 */
/** Nonzero when the process may create files in the existing directory DIR (POSIX also needs search, X_OK). */
static int dir_writable(const char *dir)
{
#if defined(_WIN32)
    return _access(dir, 2) == 0;
#else
    return access(dir, W_OK | X_OK) == 0;
#endif
}
/** The process's working directory into OUT (N > 0); returns 0 when it cannot be read. */
static int current_directory(char *out, size_t n)
{
#if defined(_WIN32)
    return _getcwd(out, (int)n) != NULL;
#else
    return getcwd(out, n) != NULL;
#endif
}
/** Creates directory PATH (POSIX mode 0777, before the umask); returns 0 when the call fails. */
static int make_directory(const char *path)
{
#if defined(_WIN32)
    return _mkdir(path) == 0;
#else
    return mkdir(path, 0777) == 0;
#endif
}
/**
 * The running executable's path into OUT (N bytes); returns 0 when it is not known. Windows asks for the module's
 * file name (0 or a truncated name fails); elsewhere /proc/self/exe is read, and when that link cannot be read ARGV0
 * is used if it fits.
 */
static int executable_path(const char *argv0, char *out, size_t n)
{
#if defined(_WIN32)
    DWORD k = GetModuleFileNameA(NULL, out, (DWORD)n);
    (void)argv0;
    return k && k < n;
#else
    ssize_t k = readlink("/proc/self/exe", out, n - 1u);
    if (k > 0) {
        out[k] = 0;
        return 1;
    }
    return argv0 && snprintf(out, n, "%s", argv0) < (int)n;
#endif
}
int xeen_wox_save_dir_check(const char *dir, char *why, size_t why_size)
{
    if (!dir || !*dir) {
        snprintf(why, why_size, "no directory named");
        return 0;
    }
    if (!readable_dir(dir)) {
        snprintf(why, why_size, "not an existing directory (it is not created)");
        return 0;
    }
    if (!dir_writable(dir)) {
        snprintf(why, why_size, "not writable (%s)", strerror(errno));
        return 0;
    }
    return 1;
}
void xeen_wox_working_directory(char *out, size_t n)
{
    if (!n || !current_directory(out, n)) {
        snprintf(out, n, ".");
    }
}
int xeen_wox_save_directory(const char *explicit_dir, char *out, size_t n)
{
    if (!explicit_dir || !*explicit_dir || snprintf(out, n, "%s", explicit_dir) >= (int)n) {
        return 0;
    }
    if (readable_dir(out)) {
        return 1;
    }
    return make_directory(out) && readable_dir(out);
}
int xeen_wox_config_locate(const char *explicit_path, const char *argv0, char *out, size_t n)
{
    char exe[1024], base[1024];
    if (explicit_path && *explicit_path) {
        return snprintf(out, n, "%s", explicit_path) < (int)n;
    }
    if (!executable_path(argv0, exe, sizeof exe)) {
        return 0;
    }
    directory_of(exe, base, sizeof base);
    return snprintf(out, n, "%s" WOX_SEPARATOR "wox.cfg", base) < (int)n;
}
