#ifndef XEEN_SETUP_MODEL_H
#define XEEN_SETUP_MODEL_H
/*
 * Phase 331: the setup tool's toolkit-independent core (owner-approved host tool, not original behaviour and not the
 * original INSTALL.EXE). It edits wox.cfg in place (comments, unknown keys and line endings kept; only the managed
 * values change), maps the game's XEEN.CFG to the setup's Speech and Music choices and back, and holds the first-run
 * presets. The Win32 (setup_win32.c) and GTK (setup_gtk.c) windows only show and collect these values.
 *
 * XEEN.CFG's fields (29 bytes as INSTALL.EXE writes them; 30 as the game writes them back), from the installer's
 * code (Phase 331 evidence install/FINDINGS.md) and XEEN.DAT's readers: word 00 the digital sound driver, 02 its port,
 * 04 the music driver, 06 its port, 08 the speech IRQ, 0A..11 the Clouds ending's mark, 12 an installer flag of
 * unestablished role, 14 the /V "adjust Sound Blaster volumes" flag (XEEN.DAT's promus mixer flag 8D4C), 16 and 17
 * the Darkside and World endings, 18 the speech DMA channel, 1A the intro-played word, 1C the CD drive (letter - 'A').
 */
#include <stddef.h>
#include <stdint.h>

#define SETUP_PATH_MAX 1024u
#define SETUP_XEEN_CFG_SIZE 29u     /* INSTALL.EXE's write (06F54: 1Dh bytes) */
#define SETUP_XEEN_CFG_MAX 30u      /* the game's own writes (16ECF, 33536, 1A52F) */
/* Phase 335: the game's start-up (47C1F) zero-fills its 29-byte image (47C33) and reads at most 29 bytes into it
 * without checking how many it got (47C83), so a shorter file is valid (GOG's is 28 bytes). Setup accepts 1..30
 * bytes and writes a changed file back at its own length, but at least through the music port (04..07), the last
 * field it writes. */
#define SETUP_XEEN_CFG_WRITE_MIN 8u
#define SETUP_WIZARD_STYLES 3u
#define SETUP_STYLE_BLACK 2u /* wizard_eye_plus styles: index of "black" (the default) in setup_wizard_style_names */

/* XEEN.CFG's fields by their offset (see the comment above; INSTALL.EXE's image DS:2720 + offset). */
enum {
    SETUP_CFG_SOUND = 0x00,         /* word: the digital sound (speech) driver */
    SETUP_CFG_SOUND_PORT = 0x02,    /* word */
    SETUP_CFG_MUSIC = 0x04,         /* word: the music driver */
    SETUP_CFG_MUSIC_PORT = 0x06,    /* word */
    SETUP_CFG_IRQ = 0x08,           /* word: the speech IRQ (/SI#) */
    SETUP_CFG_CLOUDS_ENDING = 0x0a, /* 8 bytes: the Clouds ending's mark */
    SETUP_CFG_VOLUME_ADJUST = 0x14, /* word: /V1 (adjust Sound Blaster volumes) = XEEN.DAT's 8D4C */
    SETUP_CFG_DARKSIDE_ENDING = 0x16,
    SETUP_CFG_WORLD_ENDING = 0x17,
    SETUP_CFG_DMA = 0x18,           /* word: the speech DMA channel (/SD#) */
    SETUP_CFG_INTRO_PLAYED = 0x1a,  /* word */
    SETUP_CFG_CD_DRIVE = 0x1c       /* byte: the CD drive letter - 'A' (/CDx) */
};
/* The driver words and ports the setup writes itself (INSTALL.EXE's menu values). */
#define SETUP_SOUND_NONE 0u
#define SETUP_SOUND_SBPRO 7u /* prosnd */
#define SETUP_MUSIC_WORD_SBPRO 3u /* promus */
#define SETUP_PORT_SBPRO 0x220u

/* The directories the settings window shows, in its order. */
typedef enum {
    SETUP_DIR_XEEN_CFG, /* [files] xeen_cfg_dir */
    SETUP_DIR_DATA,     /* [files] data_dir */
    SETUP_DIR_SAVE,     /* [files] save_dir */
    SETUP_DIR_MT32,     /* [files] mt32_roms_dir */
    SETUP_DIR_SC55,     /* [files] sc55_roms_dir */
    SETUP_DIR_CD,       /* [files] cd_tracks_dir */
    SETUP_DIRS
} setup_dir;

/* The Music list: "No music", the emulated cards XEEN.CFG's music word selects, and wox.cfg's system MIDI. */
typedef enum {
    SETUP_MUSIC_NONE,   /* XEEN.CFG music 0 (None), 7 (PC speaker: not available natively) or an unknown value */
    SETUP_MUSIC_ADLIB,  /* 1 */
    SETUP_MUSIC_SB,     /* 2 */
    SETUP_MUSIC_SBPRO,  /* 3 */
    SETUP_MUSIC_MT32,   /* 4 */
    SETUP_MUSIC_CANVAS, /* 5 */
    SETUP_MUSIC_PAS,    /* 6 */
    SETUP_MUSIC_WAVE,   /* 8 */
    SETUP_MUSIC_TAP10,  /* 9 */
    SETUP_MUSIC_SYSTEM, /* wox.cfg [audio] music_override = system (not stored in XEEN.CFG) */
    SETUP_MUSIC_CHOICES
} setup_music;

/* One wox.cfg flag of [addons] or [bugfix]: its key as the generated wox.cfg spells it, a short name for the window
 * and the index of the flag it belongs under (-1: none). */
typedef struct {
    const char *key;
    const char *label;
    int parent;
} setup_flag;

extern const setup_flag setup_addons[];
extern const unsigned setup_addon_count;
extern const setup_flag setup_bugfixes[];
extern const unsigned setup_bugfix_count;
/* The three wizard_eye_plus style keys (indoor, outdoor, sky) and the style values in the list's order. */
extern const char *const setup_wizard_style_keys[SETUP_WIZARD_STYLES];
extern const char *const setup_wizard_style_names[SETUP_WIZARD_STYLES]; /* "1px", "2px", "black" */
/* The index of add-on KEY in setup_addons (-1 when absent). */
int setup_addon_index(const char *key);
const char *setup_music_label(setup_music);
/* The first run's texts shared by both windows (Phase 335, owner requests). WELCOME_NEW opens a first run without
 * wox.cfg, WELCOME_REPLACE one that replaces it (the settings window's Initial setup... or a damaged wox.cfg). */
#define SETUP_TEXT_WELCOME_NEW "Welcome! No wox.cfg was found, so a new one will be created."
#define SETUP_TEXT_WELCOME_REPLACE "Welcome! Your current wox.cfg will be replaced by a new one."
#define SETUP_TEXT_RERUN_QUESTION                                                                                    \
    "Initial setup creates a new wox.cfg: your current wox.cfg will be overwritten.\n\nDo you want to continue?"
#define SETUP_TEXT_OTHER_NOTE "You will have to pick the data, save and XEEN.CFG locations yourself."
#define SETUP_TEXT_ENHANCED_NOTE                                                                                     \
    "The original game with modern comforts: Identify shows the statistics of identified items, World map shows "   \
    "the whole world, and more add-ons and bug fixes."
#define SETUP_WIDE_(s) L##s
#define SETUP_WIDE(s) SETUP_WIDE_(s) /* a text above as a wide string (the Win32 window) */

/* The note the Music list shows right-aligned in bold beside a card ("recommended" for the Sound Canvas, Phase 335
 * owner request), or NULL. */
const char *setup_music_badge(setup_music);

/* Everything the settings window edits. Directories hold the text shown (auto already mapped to its default). */
typedef struct {
    char dir[SETUP_DIRS][SETUP_PATH_MAX];
    int fullscreen, aspect;
    int locked_scale; /* [display] locked_scale (Phase 332, an owner-approved host option) */
    unsigned scale; /* 1..XEEN_WOX_SCALE_MAX (10) */
    int addon[32];
    unsigned wizard_style[SETUP_WIZARD_STYLES]; /* index into setup_wizard_style_names */
    int bugfix[16];
    int system_midi; /* wox.cfg music_override = system: the Music list shows System MIDI */
} setup_settings;

/* ---- wox.cfg as text, edited in place ---- */
typedef struct {
    char **lines;  /* each line without its ending */
    char **ends;   /* each line's own ending: "\n", "\r\n" or "" (the last line without one) */
    size_t count, cap;
    const char *newline; /* the ending added lines take: the file's first ending, else the platform's */
} setup_doc;

void setup_doc_init(setup_doc *);
void setup_doc_free(setup_doc *);
/* Reads PATH (any line endings, an optional UTF-8 BOM kept in the first line). 1, or 0 with the reason in ERR. */
int setup_doc_load(setup_doc *, const char *path, char *err, size_t n);
/* Replaces the document's text with TEXT (for tests and the generated defaults). 1, or 0 out of memory. */
int setup_doc_parse(setup_doc *, const char *text);
/* The document as one string (malloc'd; NULL out of memory). */
char *setup_doc_text(const setup_doc *);
/* VALUE of KEY in [SECTION] ('_' and '-' alike, one pair of quotes removed, as wox's parser reads it): 1 when present
 * (VALUE gets "" when it does not fit). */
int setup_doc_get(const setup_doc *, const char *section, const char *key, char *value, size_t n);
/* Sets KEY in [SECTION] to VALUE: the existing line keeps its key, spacing and ending and only its value changes; a
 * missing key is added after the section's last setting, a missing section at the end. VALUE is quoted when wox's
 * parser would otherwise read it differently. 1, or 0 when VALUE cannot be written so that it reads back exactly (a
 * line break, or both quote characters with outer white space) or out of memory. */
int setup_doc_set(setup_doc *, const char *section, const char *key, const char *value);
/* Writes the document to PATH through a temporary file beside it renamed over PATH (a failed write leaves PATH as it
 * was). 1, or 0 with the reason in ERR. */
int setup_doc_save(const setup_doc *, const char *path, char *err, size_t n);
/* The text of the default wox.cfg (xeen_wox_config_create's), malloc'd; NULL out of memory. */
char *setup_default_wox_cfg(void);

/* ---- settings <-> wox.cfg ---- */
/* The text shown for auto in each directory (xeen_cfg_dir's auto is the data directory's text). */
const char *setup_dir_default(setup_dir);
const char *setup_dir_key(setup_dir);
/* Phase 335 (owner request): whether A and B hold the same settings (the folder texts, the display, the add-ons with
 * their styles, the bug fixes, System MIDI): Cancel asks only after a change. */
int setup_settings_same(const setup_settings *a, const setup_settings *b);
/* Reads the settings from DOC (missing or auto keys at their defaults). */
void setup_settings_from_doc(const setup_doc *, setup_settings *);
/* Writes S into DOC: every managed key whose meaning changed (an equal value keeps its line untouched); a directory
 * equal to its default text is written as auto; [audio] music_override = auto (system with System MIDI) and speech = auto.
 * 1, or 0 when a value cannot be written (setup_doc_set). */
int setup_settings_to_doc(const setup_settings *, setup_doc *);
/* The first-run choices: GOG (save_dir and xeen_cfg_dir = cloud_saves/WORLD with the platform's separator) or
 * not, then Vanilla (every add-on and bug fix off) or Enhanced (all on but damage_numbers.stack; the three
 * wizard_eye_plus styles black). */
void setup_apply_version(setup_settings *, int gog);
void setup_apply_style(setup_settings *, int enhanced);
/* "cloud_saves\WORLD" on Windows, "cloud_saves/WORLD" elsewhere. */
const char *setup_gog_dir(void);

/* ---- XEEN.CFG ---- */
typedef struct {
    uint8_t bytes[SETUP_XEEN_CFG_MAX];
    size_t size;               /* the file's length (a proper file has 29 or 30 bytes) */
    char path[SETUP_PATH_MAX]; /* the file read (its own spelling) */
} setup_xeen_cfg;

/* The game's XEEN.CFG in DIR: XEEN.CFG, else xeen.cfg (wox's two spellings). 1 when found (PATH set). */
int setup_xeen_cfg_find(const char *dir, char *path, size_t n);
/* Reads DIR's XEEN.CFG. 1: found and proper (29 or 30 bytes); 0: none (ERR "") or not proper (ERR says why). */
int setup_xeen_cfg_read(const char *dir, setup_xeen_cfg *, char *err, size_t n);
/* The approved default: the fresh genuine file (emulator/XEENGAME/XEEN.CFG) with speech and music Sound Blaster Pro
 * (7 and 3, both ports 220h), IRQ 7, DMA 1, no ending seen, the intro not played. Writes 29 bytes. */
void setup_xeen_cfg_default(uint8_t out[SETUP_XEEN_CFG_SIZE]);
/* Creates DIR/XEEN.CFG with the default bytes (never over an existing file). 1, or 0 with the reason in ERR. */
int setup_xeen_cfg_create(const char *dir, char *err, size_t n);
/* Speech: on unless the sound word is 0 (None), 1 (PC speaker), 3 or 4 (nullsnd) or unknown (past 10). */
int setup_xeen_cfg_speech(const setup_xeen_cfg *);
/* Music: the card of the music word (None for 0, 7 and unknown values). */
setup_music setup_xeen_cfg_music(const setup_xeen_cfg *);
/* The music word and port a card writes (INSTALL.EXE's autodetection defaults; TAP-10 0x340, the owner's choice).
 * Returns 0 for System MIDI (nothing written). PORT 0: the port is left as it is (None). */
int setup_music_word(setup_music, unsigned *word, unsigned *port);
/* Applies the window's Speech and Music to CFG: Speech off writes sound word 0 (the port kept); Speech on over an
 * off file writes Sound Blaster Pro (7) at port 220h, on over an on file changes nothing; a changed Music writes its
 * word and port (System MIDI and an unchanged Music: nothing). Every other byte is kept. */
void setup_xeen_cfg_apply(setup_xeen_cfg *, int speech_on, setup_music music);
/* Writes CFG back to its file (its length kept). 1, or 0 with the reason in ERR. */
int setup_xeen_cfg_write(const setup_xeen_cfg *, char *err, size_t n);

/* ---- the two whole operations the windows run ---- */
/* The first run's end: wox.cfg at WOX_PATH from the defaults with the two choices (GOG or not, Enhanced or Vanilla)
 * and music_override = auto. GOG also creates cloud_saves/WORLD beside wox.cfg and the default XEEN.CFG there when it has
 * none. Replaces an existing (invalid) wox.cfg. 1, or 0 with the reason in ERR. */
int setup_first_run(const char *wox_path, int gog, int enhanced, char *err, size_t n);
/* Save: S into DOC and DOC to WOX_PATH (checked with wox's own parser before it replaces the file); then, when CFG is
 * a proper XEEN.CFG, the window's SPEECH_ON and MUSIC into it (setup_xeen_cfg_apply) and back to its file. 1, or 0
 * with the reason in ERR (nothing of a failed step is written). */
int setup_save(const char *wox_path, setup_doc *doc, const setup_settings *s, setup_xeen_cfg *cfg, int speech_on,
               setup_music music, char *err, size_t n);

/* ---- the windows' decisions (one rule for both toolkits) ---- */
typedef enum {
    SETUP_CFG_PROPER,    /* a proper XEEN.CFG was read */
    SETUP_CFG_ABSENT,    /* the folder exists without one: the default can be created there */
    SETUP_CFG_NO_FOLDER, /* the folder does not exist */
    SETUP_CFG_INVALID    /* a file the game cannot use (empty or longer than 30 bytes): the default can replace it */
} setup_cfg_state;
/* Reads the XEEN.CFG of location TEXT (resolved against BASE into DIR) into CFG. For every state but PROPER, NOTE
 * gets the line the window shows under the location. */
setup_cfg_state setup_xeen_cfg_locate(const char *base, const char *text, setup_xeen_cfg *cfg, char *dir, size_t dn,
                                      char *note, size_t nn);
/* The question offering the default XEEN.CFG in DIR (headline, a blank line, the details). */
void setup_create_question(const char *dir, char *q, size_t n);
/* Phase 335: the question before the default replaces the invalid XEEN.CFG in DIR (REASON: its note), and the
 * replacement itself (the file keeps its name's case). 1 on success, else 0 with the reason in ERR. */
void setup_replace_question(const char *dir, const char *reason, char *q, size_t n);
int setup_xeen_cfg_replace(const char *dir, char *err, size_t n);
/* Save's folders: the first of the save folder and XEEN.CFG's that does not exist (the game will not start without
 * it): 1 with WHICH and the question asking to create it. */
int setup_missing_folder(const setup_settings *, const char *base, setup_dir *which, char *q, size_t n);
/* A data folder that does not exist: 1 with the question asking whether to save anyway. */
int setup_missing_data(const setup_settings *, const char *base, char *q, size_t n);

/* ---- paths ---- */
/* The directory holding PATH ("." when none). */
void setup_dirname(const char *path, char *out, size_t n);
/* DIR resolved against the wox.cfg directory BASE (an absolute DIR as given). */
void setup_resolve(const char *base, const char *dir, char *out, size_t n);
/* A directory chosen by the folder dialog (absolute) as wox.cfg should hold it: relative to BASE when inside it ("."
 * for BASE itself), else as given. */
void setup_relative_to(const char *base, const char *chosen, char *out, size_t n);
/* 1 when DIR (resolved against BASE) is an existing directory. */
int setup_dir_exists(const char *base, const char *dir);
/* Creates DIR (resolved against BASE) and its missing parents. 1 on success or when it exists. */
int setup_make_dirs(const char *base, const char *dir);
/* GAME1.GOG beside the setup executable (any case). */
int setup_has_gog_image(const char *exe_dir);
/* A warning when MUSIC needs ROMs (MT-32: mt32_roms_dir; Canvas, Wave Blaster, TAP-10: sc55_roms_dir) that wox's check
 * (xeen_wox_config_check_roms) does not find; 0 when none. */
int setup_rom_warning(const setup_settings *, setup_music, const char *base, char *msg, size_t n);

/* ROM downloads (Phase 335, owner request: the Download button beside the ROM warning; setup_roms.c). */
typedef enum {
    SETUP_ROMS_NONE,
    SETUP_ROMS_MT32, /* Munt's MT32_CONTROL.ROM and MT32_PCM.ROM (MT-32 v1.07) into mt32_roms_dir */
    SETUP_ROMS_SC55, /* Nuked-SC55's SC-55 v1.00 set (two control ROMs, three wave ROMs) into sc55_roms_dir */
} setup_rom_set;
/* The set MUSIC needs (NONE for a card without ROMs). */
setup_rom_set setup_music_rom_set(setup_music);
/* The owner's archive URL of SET, and its short name for messages ("MT-32", "SC-55"); NULL / "" for NONE. */
const char *setup_rom_set_url(setup_rom_set);
const char *setup_rom_set_name(setup_rom_set);
/* SET's directory from the window's settings, resolved against BASE, into OUT. */
void setup_rom_set_dir(const setup_settings *, setup_rom_set, const char *base, char *out, size_t n);
/* Extracts SET's ROM files from the ZIP archive in memory (ZIP, N bytes) into directory DIR (absolute), creating it
 * and its parents. Every member is checked (CRC-32, size) before anything is written; a file already in DIR is kept.
 * 1 on success; 0 with the reason in ERR. */
int setup_rom_install(setup_rom_set, const unsigned char *zip, size_t n, const char *dir, char *err, size_t en);

/* The setup executable's directory (from ARGV0 or the platform). */
void setup_exe_dir(const char *argv0, char *out, size_t n);

#endif
