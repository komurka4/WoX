#ifndef XEEN_WOX_CONFIG_H
#define XEEN_WOX_CONFIG_H
#include <stddef.h>
#include <stdint.h>

/* [display] scale's largest value (Phase 333, owner request: 6 before), also xeen_platform_open's limit. */
#define XEEN_WOX_SCALE_MAX 10u
/* Wox.cfg, the native runtime configuration (audio backend, ROM
 * directories, initial display mode): "key = value" under [section], "auto",
 * '#'/';' comments, quoted values; unknown or duplicate keys and unknown values
 * rejected. Phase 336 (owner decision): every key is spelled with '_' and read
 * exactly (no '-' spelling, alias or retired key), every boolean is on/off,
 * [audio] music became music_override and the [files] directories end in _dir.
 * It is separate from the game's own XEEN.CFG, which this module never reads
 * or writes. */
/* The music driver XEEN.DAT would load (INTRO.CC; XEEN.CFG's music word 8D3C) and the native output that plays it.
 * Phase 286 adds the remaining original drivers but the PC speaker's (ibmmus, XEEN.CFG music 7: not implemented). */
typedef enum {
    XEEN_MUSIC_EXTERNAL, /* music_override = system: rolmus (the MT-32 arrangement) to system MIDI output (a native choice) */
    XEEN_MUSIC_MT32,     /* rolmus (XEEN.CFG music 4, Roland LAPC-1 / MT-32) on Munt, MT-32 ROMs */
    XEEN_MUSIC_SC55,     /* canmus (music 5, Roland Canvas / GS) on Nuked-SC55, SC-55 ROMs */
    XEEN_MUSIC_SBPRO,    /* promus (music 3, Sound Blaster Pro) on two YM3812 (dual OPL2), no ROMs */
    XEEN_MUSIC_OFF,      /* no music driver (nullmus: XEEN.CFG music 0 or past 9) */
    XEEN_MUSIC_ADLIB,    /* admus (music 1, AdLib) on one YM3812, mono */
    XEEN_MUSIC_SB,       /* blastmus (music 2, Sound Blaster) on one YM3812, mono */
    XEEN_MUSIC_PAS,      /* specmus (music 6, Pro-Audio Spectrum; byte-identical to blastmus) on one YM3812, mono */
    XEEN_MUSIC_WAVE,     /* wavemus (music 8, Wave Blaster) on Nuked-SC55, SC-55 ROMs */
    XEEN_MUSIC_TAP10,    /* tapmus (music 9, Roland TAP-10) on Nuked-SC55, SC-55 ROMs */
    XEEN_MUSIC_SYSTEMS
} xeen_music_system;
typedef struct {
    xeen_music_system system;
    /* Phase 286: [audio] music_override = auto (Phase 336; music before): the system is resolved from the game's
     * XEEN.CFG (xeen_music_resolve) when the host has read it; system then holds the resolution. Any other value
     * overrides XEEN.CFG's choice. An absent key keeps the default (system). */
    int music_auto;
    /* Phase 286: [audio] speech = auto (the default: XEEN.CFG's digital sound driver decides), on, off
     * (xeen_speech_resolve). */
    unsigned speech;
    unsigned voice_volume, music_volume;   /* whole percent 0..200; 100 = the player's reference */
    char midi_device[256];                 /* Windows device number or name; "" = default */
    char midi_port[32];                    /* Linux CLIENT:PORT; "" = default */
    char mt32_roms[1024], sc55_roms[1024]; /* resolved directories */
    /* The CD voice rips (xeen02.ogg .. xeen60.ogg) and their level:
     * whole percent 0..200, 100 = the unscaled CD PCM. cd_tracks_auto: "auto"
     * (Phase 331: music beside wox.cfg only, no other place is tried). */
    unsigned cd_volume;
    char cd_tracks[1024];
    int cd_tracks_auto;
    int fullscreen; /* initial display mode only */
    /* Phase 270: [display] scale (1..XEEN_WOX_SCALE_MAX, default 3), aspect and authentic_cursor (on/off, default
     * off), formerly the command line's --scale, --aspect and --authentic-cursor. */
    unsigned scale;
    int aspect, authentic_cursor;
    /* Phase 332 (owner-approved host option, not original): [display] locked_scale (on/off, default off): the window
     * keeps the picture at SCALE (with ASPECT) however large it is, and cannot be made smaller than that. */
    int locked_scale;
    /* Phase 270: [files] data_dir (Phase 336; data before), the game's data directory. data_auto: "auto" (the default; the host
     * uses the process's working directory, "."); otherwise data is the path, resolved against the
     * file's directory. */
    char data[1024];
    int data_auto;
    /* Phase 287: [files] save_dir, the writable directory of the game's save slots (the save directory overlay,
     * host_files.h). save_dir_auto: "auto" (the default; a window saves into the process's working directory, ".");
     * otherwise save_dir is the path, resolved against the file's directory, which must be an existing writable
     * directory (the host refuses to start otherwise: no fallback, nothing created). --save-dir overrides it. */
    char save_dir[1024];
    int save_dir_auto;
    /* Phase 287: [files] xeen_cfg_dir (Phase 336; XEEN_CFG_location before), the directory holding the game's
     * XEEN.CFG. xeen_cfg_auto: "auto" (the default: the directory [files] data_dir / --data-dir names, never the
     * save directory nor a GAME*.GOG image); otherwise xeen_cfg_dir is the path, resolved against the file's
     * directory: a directory, not the file itself. */
    char xeen_cfg_dir[1024];
    int xeen_cfg_auto;
    /* Owner-approved add-ons ([addons], on/off; NOT original behavior, all off by
     * default): identify = the identified item statistics (xeen/addons.h);
     * Phase188 map_movement = the pannable map (xeen/addon/map_movement.h);
     * Phase190 training_preview = the trainer's level ladder
     * (xeen/addon/training_preview.h); damage_numbers and
     * damage_numbers.stack = the damage numbers and their stack layout
     * (xeen/addon/damage_numbers.h); monster_difficulty = the
     * monsters' difficulty icons (xeen/addon/monster_difficulty.h);
     * Phase203 spell_short_description = the Cast dialog's spell box
     * (xeen/addon/spell_short_description.h); mouse_scroll = the
     * mouse wheel scrolls the lists (xeen/addon/mouse_scroll.h); Phase207
     * town_icons = the town places' icons on the M map and the minimap
     * (xeen/addon/town_icons.h); character_details = what the
     * character view's stats, skills and conditions do
     * (xeen/addon/character_details.h); spell_list_sort = the
     * spell lists sorted A-Z or by type (xeen/addon/spell_list_sort.h);
     * Phase226 world_map = the whole
     * world's map from the M map (xeen/addon/world_map.h); Phase247
     * quest_journal = the Quests dialog as a journal
     * (xeen/addon/quest_journal.h); Phase310 please_wait_skip = the map
     * load's "Please Wait..." box not shown (xeen/addon/please_wait_skip.h); Phase318 wizard_eye_plus = under
     * Wizard Eye with the = automap on, the minimap's unseen cells through a checkerboard
     * (xeen/addon/wizard_eye_plus.h), its style per map kind (indoor, outdoor, sky: 0 black, the default, 1 1px,
     * 2 2px; the add-on's XEEN_WEP_* values). */
    int addon_identify;
    int addon_map_movement;
    int addon_training_preview;
    int addon_damage_numbers;
    int addon_damage_numbers_stack;
    int addon_monster_difficulty;
    int addon_spell_short_description;
    int addon_mouse_scroll;
    int addon_town_icons;
    int addon_character_details;
    int addon_spell_list_sort;
    int addon_world_map;
    int addon_quest_journal;
    int addon_please_wait_skip;
    int addon_wizard_eye_plus;
    unsigned addon_wizard_eye_plus_style[3]; /* wizard_eye_plus.indoor, .outdoor, .sky */
    /* owner-approved bug fixes ([bugfix], on/off; NOT original behavior, all
     * off by default): spell_description = corrected spell descriptions
     * (xeen/bugfixes.h). */
    int bugfix_spell_description;
    int bugfix_left_clip_colors;
    int bugfix_sprite_width_trim;
    int bugfix_quest_up_down; /* Phase 269 */
    /* Phase 308: the movie player's keys (the former XEEN.EXE player's wox.cfg, now one file and one parser).
     * [files] cue, cd_num_tracks: the CD metadata the Darkside intro's selector reads (1f164: the CD's last track;
     * the standalone player's rule: an explicit count, else the CUE near the data, else 31). The rest are
     * developer/test controls (labelled, NOT original behaviour): [playback] sequence or command (the first
     * movie instead of an absent XEENCOMMAND), [endings] score or payload and badge, [timing] timing
     * (approximate, legacy), original_timer_quirks (on/off), clock (real, virtual), dos_date, dos_time,
     * [diagnostics] dump_frames, dump_hashes or dump_timings (a dump directory). Phase 336: the former player's
     * retired keys (speech-mode, subtitles, handoff-exec, dump-midi, dump-opl, version, experimental-music) are
     * unknown keys now (rejected). */
    struct {
        char cue[1024];
        unsigned cd_num_tracks; /* 0: auto */
        char sequence[32], command[256];
        int score_given, badge_given;
        uint32_t score;
        unsigned badge;
        char payload[16];
        int legacy_timing, original_timer_quirks, virtual_clock;
        char dos_date[16], dos_time[16];
        char dump[1024];
        int dump_kind; /* 0 none, 1 frames, 2 hashes, 3 timings */
    } movie;
    char path[1024]; /* the file read, "" for defaults */
} xeen_wox_config;
/* The defaults: music_override = system, volumes 100, MIDI default
 * device, ROM directories roms/mt32 and roms/sc55 relative to base_dir,
 * windowed; cd_volume 100, the CD rips in music relative to base_dir (Phase
 * 331: no environment override of these three); scale 3,
 * aspect and authentic_cursor off; data_dir auto ("."); save_dir auto ("."); xeen_cfg_dir auto (the data
 * directory). */
void xeen_wox_config_defaults(xeen_wox_config *, const char *base_dir);
/* Read path over the defaults. Relative paths in it are relative to the file's
 * directory. A missing file is an error here (the caller decides whether to
 * create one). Returns 1, or 0 with "path:line: reason" in error. */
int xeen_wox_config_load(const char *path, xeen_wox_config *, char *error, size_t n);
/* Check the selected backend's ROM directory: mt32 needs MT32_CONTROL.ROM and
 * MT32_PCM.ROM; sc55, wave and tap10 (Nuked-SC55) need a readable directory
 * (the backend recognizes the set by its hashes). The other systems need none.
 * 1 = ok; 0 with the reason in error (when error and n are nonzero). */
int xeen_wox_config_check_roms(const xeen_wox_config *, char *error, size_t n);
/* Create path with the defaults and comments only when it does not exist
 * (never overwrites). 1 = created or already present. */
int xeen_wox_config_create(const char *path);
/* Phase 331: the text xeen_wox_config_create writes (the defaults with their comments), for the setup tool. */
const char *xeen_wox_config_default_text(void);
/* Phase 331: the I-th setting this parser knows (not the movie player's keys): its section and its name as the parser
 * reads it (exactly; Phase 336). Returns 0 past the last one. Lets the setup tool check its add-on and bug-fix lists. */
int xeen_wox_config_setting(size_t i, const char **section, const char **name);
/* Lookup: an explicit path, else wox.cfg beside the executable (argv0's
 * directory or the platform's executable path). Writes the path; 1 = found
 * a location (the file need not exist). */
int xeen_wox_config_locate(const char *explicit_path, const char *argv0, char *out, size_t n);
/* The command line's --save-dir DIR: the directory, created when missing (the original writes its slot files into
 * its own directory, which DOS never refuses: 14033's failure is 19629's fatal 2C80D). Writes the path; 1 = it exists
 * as a directory. (Phase 287: the default when neither --save-dir nor [files] save_dir names one is the working
 * directory, xeen_wox_save_dir_check; "saves" beside the executable is no longer used.) */
int xeen_wox_save_directory(const char *explicit_dir, char *out, size_t n);
/* Phase 287: whether DIR is an existing directory the process may write into (an explicit [files] save_dir is never
 * created). Writes the reason into WHY on failure; 1 = usable. */
int xeen_wox_save_dir_check(const char *dir, char *why, size_t why_size);
/* Phase 287: the process's working directory (for messages) into OUT; "." when it cannot be read. */
void xeen_wox_working_directory(char *out, size_t n);
/* Phase 328: VALUE as a whole number LOW..HIGH into *OUT: accepted only as a non-empty run of ASCII digits (no sign,
 * no white space, nothing after it) whose value is representable and in range; returns 0 (nothing stored)
 * otherwise. The same answer whatever the width of unsigned long (a Windows long is 32 bits). Shared by wox.cfg's
 * numeric movie keys and the command line's --movie-score. Host input, not original behaviour. */
int xeen_wox_whole_number(const char *value, unsigned long low, unsigned long high, unsigned long *out);
const char *xeen_music_system_name(xeen_music_system);
/* Phase 286: music_override = auto (music before Phase 336). XEEN.DAT loads the music driver XEEN.CFG's word at offset 4 (DS 8D3C) selects (the switch
 * at 15EC9, table 1564:0CCC; an unsigned compare): 1 admus, 2 blastmus, 3 promus, 4 rolmus, 5 canmus, 6 specmus,
 * 7 ibmmus, 8 wavemus, 9 tapmus, anything else nullmus. The native system for each is the driver's own output:
 * adlib, sb, sbpro, mt32, sc55, pas, (none), wave, tap10, off. ibmmus (the PC speaker) is not implemented: no music,
 * nothing stands in. Without XEEN.CFG (the original refuses to start: 47C1F's "Use install.exe ...") there is no
 * original selection: no music (the native's zeroed configuration image selects nullmus as well). */
typedef struct {
    xeen_music_system system; /* the system to open */
    int from_xeen_cfg;        /* 1: music_override = auto (resolved here); 0: wox.cfg's value (or its default) */
    int xeen_cfg_present;     /* XEEN.CFG's bytes were given */
    unsigned word;            /* XEEN.CFG's music word (0 when absent or short) */
    const char *driver;       /* the original driver XEEN.DAT loads for it (nullmus, ibmmus, ...) */
    int unsupported;          /* 1: ibmmus (the PC speaker): not implemented, no music */
} xeen_music_resolution;
/* Phase 286: [audio] speech. XEEN.DAT loads the digital sound driver XEEN.CFG's word at offset 0 (DS 8D38) selects
 * (the switch at 15E52, table 1564:0CDE): 1 ibmsnd (the PC speaker), 2 adsnd, 3 and 4 nullsnd, 5 source, 6 blastsnd,
 * 7 and 10 prosnd, 8 specsnd, 9 covxsnd, anything else nullsnd; the cutscenes' voices speak instead of their
 * subtitles when the panel's sound (ED9A) is on and that word is 2 or more (185E3, 34276, 35FC4).
 *   auto: the driver decides: nullsnd plays no voice (and is never busy); the PC speaker's voices are not played (the
 *         owner's policy: no PC-speaker output), their timing kept; every other driver's voices play.
 *   on:   voices play whatever the driver; for nullsnd and the PC speaker the SB driver's model (prosnd's) stands in.
 *         The panel's Speech option (the CD speech) stays the player's.
 *   off:  no voice and none busy (nullsnd's behaviour); the cutscenes show their subtitles (the original's no-voice
 *         branch); the panel's Speech is held off (so the CD speech is silent and the dialogue text shown, as the
 *         original's Speech off: 2976D stores Text off = 0). XEEN.CFG and the game's configuration image are not
 *         changed. */
enum { XEEN_SPEECH_AUTO = 0, XEEN_SPEECH_ON = 1, XEEN_SPEECH_OFF = 2 };
typedef struct {
    int xeen_cfg_present;
    unsigned word;      /* XEEN.CFG's sound word (0 when absent) */
    const char *driver; /* the digital driver XEEN.DAT loads */
    int driver_voice;   /* the driver plays voices (all but nullsnd) */
    int pc_speaker;     /* ibmsnd */
    int audible;        /* voices are played */
    int modelled;       /* gameplay waits for a playing voice (a driver model); 0: none is ever busy */
    int fallback;       /* speech = on over nullsnd or the PC speaker: the SB model stands for the driver */
    int forced_off;     /* speech = off */
} xeen_speech_resolution;
void xeen_speech_resolve(const xeen_wox_config *c, const uint8_t *cfg, size_t n, xeen_speech_resolution *out,
                         char *why, size_t why_size);
/* Resolve C's music against XEEN.CFG's bytes (CFG, N; NULL when absent; a file shorter than the word reads 0, as
 * 47C1F's zeroed 29-byte image does). Writes a one-line explanation into WHY when given. */
void xeen_music_resolve(const xeen_wox_config *c, const uint8_t *cfg, size_t n, xeen_music_resolution *out, char *why,
                        size_t why_size);
/* Phase 286: the INTRO.CC music driver a system plays (rolmus, canmus, promus, admus, blastmus, specmus, wavemus,
 * tapmus), "nullmus" for off. */
const char *xeen_music_system_driver(xeen_music_system);
/* Phase 308: the movie player's speech and song selector from the shared settings: [audio] speech (auto: 1eaee's
 * gate, XEEN.CFG's word 0 below 2 shows captions; on: speech; off: captions) and the music selector its song choice
 * follows (14eb8; auto: XEEN.CFG's word 4; an explicit [audio] music_override: the index of the driver it resolves to in
 * 14b02's table, 0 for nullmus). */
int xeen_movie_subtitles(const xeen_wox_config *, const uint8_t *cfg, size_t n);
unsigned xeen_movie_music_selector(const xeen_wox_config *, const uint8_t *cfg, size_t n);
#endif
