/* Phase 331: the setup tool's core (src/setup/setup_model.c): wox.cfg edited in place, the first-run presets, the
 * XEEN.CFG mapping and default, paths and ROM warnings. Headless; writes only below its own scratch directory. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "setup/setup_model.h"
#include "xeen/wox_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#define make_dir(p) _mkdir(p)
#define process_id() _getpid()
#else
#include <unistd.h>
#define make_dir(p) mkdir(p, 0700)
#define process_id() getpid()
#endif

static int failures;
#define CHECK(c)                                                                                                     \
    do {                                                                                                             \
        if (!(c)) {                                                                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);                                    \
            ++failures;                                                                                              \
        }                                                                                                            \
    } while (0)
static char root[512];

static void path_in(char *out, size_t n, const char *name)
{
    snprintf(out, n, "%s/%s", root, name);
}
static int write_file(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f) {
        return 0;
    }
    ok = fwrite(data, 1, n, f) == n;
    return (fclose(f) == 0) && ok;
}
static size_t read_file(const char *path, void *data, size_t cap)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) {
        return 0;
    }
    n = fread(data, 1, cap, f);
    fclose(f);
    return n;
}
/** The number of lines that differ between texts A and B (same line count expected), or -1. */
static int changed_lines(const char *a, const char *b)
{
    int changed = 0;
    while (*a || *b) {
        const char *ea = strchr(a, '\n'), *eb = strchr(b, '\n');
        size_t la = ea ? (size_t)(ea - a) + 1u : strlen(a), lb = eb ? (size_t)(eb - b) + 1u : strlen(b);
        if (!*a || !*b) {
            return -1;
        }
        changed += la != lb || memcmp(a, b, la) != 0;
        a += la;
        b += lb;
    }
    return changed;
}
/** Loads TEXT with wox's parser (through a file in the scratch directory). */
static int wox_load(const char *text, xeen_wox_config *c)
{
    char p[600], err[1400];
    path_in(p, sizeof p, "probe.cfg");
    return write_file(p, text, strlen(text)) && xeen_wox_config_load(p, c, err, sizeof err);
}

/* 1. The document: in place, comments and endings kept, keys added, quoting. */
static void test_document(void)
{
    setup_doc d;
    char v[256], *text, *base = NULL;
    setup_doc_init(&d);
    CHECK(setup_doc_parse(&d, xeen_wox_config_default_text()));
    text = setup_doc_text(&d);
    CHECK(text && !strcmp(text, xeen_wox_config_default_text())); /* byte for byte */
    free(text);
    CHECK(setup_doc_get(&d, "display", "scale", v, sizeof v) && !strcmp(v, "6")); /* Phase 335 (owner): was 3 */
    CHECK(setup_doc_get(&d, "files", "xeen_cfg_dir", v, sizeof v) && !strcmp(v, "auto"));
    CHECK(setup_doc_get(&d, "addons", "wizard_eye_plus.sky", v, sizeof v) && !strcmp(v, "black"));
    /* Phase 336: keys are compared exactly, as wox reads them ('-' no longer stands for '_'). */
    CHECK(!setup_doc_get(&d, "files", "xeen-cfg-dir", v, sizeof v));
    CHECK(!setup_doc_get(&d, "addons", "wizard-eye-plus.sky", v, sizeof v));
    CHECK(!setup_doc_get(&d, "display", "nothing", v, sizeof v));
    CHECK(!setup_doc_get(&d, "files", "scale", v, sizeof v)); /* another section's key */
    CHECK(setup_doc_set(&d, "display", "scale", "5"));
    CHECK(setup_doc_set(&d, "files", "data_dir", "D:\\Might and Magic 4-5"));
    text = setup_doc_text(&d);
    base = (char *)xeen_wox_config_default_text();
    CHECK(text && changed_lines(base, text) == 2);
    CHECK(text && strstr(text, "\nscale = 5\n") && strstr(text, "\ndata_dir = \"D:\\Might and Magic 4-5\"\n"));
    CHECK(text && strstr(text, "# Window scale 1..10")); /* the comments stay */
    {
        xeen_wox_config c;
        CHECK(text && wox_load(text, &c) && c.scale == 5u && !c.data_auto && strstr(c.data, "Might and Magic 4-5"));
    }
    free(text);
    /* A missing key goes after its section's last setting; a missing section at the end. */
    CHECK(setup_doc_parse(&d, "# top\r\n[display]\r\nscale = 2\r\n\r\n[files]\r\ndata_dir = auto"));
    CHECK(setup_doc_set(&d, "display", "fullscreen", "on"));
    CHECK(setup_doc_set(&d, "bugfix", "quest_up_down", "on"));
    CHECK(setup_doc_set(&d, "files", "data_dir", "auto"));
    text = setup_doc_text(&d);
    CHECK(text && !strcmp(text, "# top\r\n[display]\r\nscale = 2\r\nfullscreen = on\r\n\r\n[files]\r\ndata_dir = auto\r\n"
                                "\r\n[bugfix]\r\nquest_up_down = on\r\n"));
    free(text);
    /* Spacing and the key's own spelling are kept; a BOM stays; quotes only when needed. */
    CHECK(setup_doc_parse(&d, "\xef\xbb\xbf[files]\nmt32_roms_dir=x\nsc55_roms_dir   =   'y z'\n"));
    CHECK(setup_doc_get(&d, "files", "mt32_roms_dir", v, sizeof v) && !strcmp(v, "x"));
    CHECK(setup_doc_get(&d, "files", "sc55_roms_dir", v, sizeof v) && !strcmp(v, "y z"));
    CHECK(setup_doc_set(&d, "files", "mt32_roms_dir", "my roms"));
    CHECK(setup_doc_set(&d, "files", "sc55_roms_dir", "\"odd"));
    CHECK(setup_doc_set(&d, "files", "cd_tracks_dir", "rips"));
    text = setup_doc_text(&d);
    CHECK(text && !strcmp(text, "\xef\xbb\xbf[files]\nmt32_roms_dir=\"my roms\"\nsc55_roms_dir   =   \"\"odd\"\ncd_tracks_dir = rips\n"));
    {
        xeen_wox_config c;
        CHECK(text && wox_load(text, &c) && strstr(c.mt32_roms, "/my roms") && strstr(c.sc55_roms, "/\"odd") &&
              strstr(c.cd_tracks, "/rips"));
    }
    free(text);
    CHECK(!setup_doc_set(&d, "files", "data_dir", "two\nlines")); /* cannot read back */
    {
        /* wox reads lines of up to 2047 bytes: a value that would make a longer line is refused */
        static char longer[2100];
        memset(longer, 'x', 1989u); /* "data_dir = " + 1989 = 2000 bytes, SETUP_LINE_LIMIT (Phase 336: the key grew) */
        CHECK(setup_doc_set(&d, "files", "data_dir", longer));
        longer[1989] = 'x';
        CHECK(!setup_doc_set(&d, "files", "data_dir", longer)); /* 2001 */
        memset(longer, 'x', 2050u);
        CHECK(!setup_doc_set(&d, "files", "data_dir", longer));
    }
    setup_doc_free(&d);
}

/* 2. Settings <-> wox.cfg: auto shown as its default and written back as auto; only changed values are written. */
static void test_settings(void)
{
    setup_doc d;
    setup_settings s;
    char *text, v[64];
    xeen_wox_config c;
    setup_doc_init(&d);
    CHECK(setup_doc_parse(&d, xeen_wox_config_default_text()));
    setup_settings_from_doc(&d, &s);
    CHECK(!strcmp(s.dir[SETUP_DIR_DATA], ".") && !strcmp(s.dir[SETUP_DIR_SAVE], "."));
    CHECK(!strcmp(s.dir[SETUP_DIR_XEEN_CFG], ".")); /* auto: the data directory's text */
#if defined(_WIN32)
    CHECK(!strcmp(s.dir[SETUP_DIR_MT32], "roms\\mt32") && !strcmp(s.dir[SETUP_DIR_SC55], "roms\\sc55"));
#else
    CHECK(!strcmp(s.dir[SETUP_DIR_MT32], "roms/mt32") && !strcmp(s.dir[SETUP_DIR_SC55], "roms/sc55"));
#endif
    CHECK(!strcmp(s.dir[SETUP_DIR_CD], "music"));
    CHECK(s.scale == 6u && !s.aspect && !s.fullscreen && !s.locked_scale && s.system_midi); /* Phase 335: was 3 */
    for (unsigned i = 0; i < SETUP_WIZARD_STYLES; ++i) {
        CHECK(!strcmp(setup_wizard_style_names[s.wizard_style[i]], "black"));
    }
    /* Unchanged settings (System MIDI kept) leave the file byte for byte. */
    CHECK(setup_settings_to_doc(&s, &d));
    text = setup_doc_text(&d);
    CHECK(text && !strcmp(text, xeen_wox_config_default_text()));
    free(text);
    /* The defaults' texts typed back are auto; a changed path is written; the data text drives XEEN.CFG's auto. */
    s.system_midi = 0;
    snprintf(s.dir[SETUP_DIR_SAVE], sizeof s.dir[0], "saves here");
    snprintf(s.dir[SETUP_DIR_CD], sizeof s.dir[0], "auto"); /* a folder named auto */
    s.aspect = 1;
    s.addon[0] = 1;
    s.wizard_style[1] = 0;
    s.bugfix[3] = 1;
    CHECK(setup_settings_to_doc(&s, &d));
    text = setup_doc_text(&d);
    CHECK(text && changed_lines(xeen_wox_config_default_text(), text) == 7);
    CHECK(setup_doc_get(&d, "audio", "music_override", v, sizeof v) && !strcmp(v, "auto"));
    CHECK(setup_doc_get(&d, "files", "save_dir", v, sizeof v) && !strcmp(v, "saves here"));
    CHECK(setup_doc_get(&d, "files", "xeen_cfg_dir", v, sizeof v) && !strcmp(v, "auto"));
    CHECK(text && wox_load(text, &c) && c.music_auto && c.aspect && c.addon_identify && c.bugfix_quest_up_down &&
          c.addon_wizard_eye_plus_style[1] == 1u /* 1px */ && !c.save_dir_auto && !c.cd_tracks_auto &&
          strstr(c.cd_tracks, "auto") && c.xeen_cfg_auto);
    free(text);
    /* Phase 332: the Lock scale checkbox writes [display] locked_scale = on and the game reads it. */
    setup_settings_from_doc(&d, &s);
    s.locked_scale = 1;
    CHECK(setup_settings_to_doc(&s, &d));
    CHECK(setup_doc_get(&d, "display", "locked_scale", v, sizeof v) && !strcmp(v, "on"));
    text = setup_doc_text(&d);
    CHECK(text && wox_load(text, &c) && c.locked_scale);
    free(text);
    setup_settings_from_doc(&d, &s);
    CHECK(s.locked_scale);
    s.locked_scale = 0;
    CHECK(setup_settings_to_doc(&s, &d) && setup_doc_get(&d, "display", "locked_scale", v, sizeof v) && !strcmp(v, "off"));
    /* XEEN.CFG's own folder: explicit unless it equals the data folder's text. */
    setup_settings_from_doc(&d, &s);
    snprintf(s.dir[SETUP_DIR_DATA], sizeof s.dir[0], "/games/xeen");
    snprintf(s.dir[SETUP_DIR_XEEN_CFG], sizeof s.dir[0], "/games/xeen");
    CHECK(setup_settings_to_doc(&s, &d));
    CHECK(setup_doc_get(&d, "files", "xeen_cfg_dir", v, sizeof v) && !strcmp(v, "auto"));
    snprintf(s.dir[SETUP_DIR_XEEN_CFG], sizeof s.dir[0], "cfg");
    CHECK(setup_settings_to_doc(&s, &d));
    CHECK(setup_doc_get(&d, "files", "xeen_cfg_dir", v, sizeof v) && !strcmp(v, "cfg"));
    snprintf(s.dir[SETUP_DIR_MT32], sizeof s.dir[0], "roms/mt32");
    snprintf(s.dir[SETUP_DIR_DATA], sizeof s.dir[0], ".");
    snprintf(s.dir[SETUP_DIR_XEEN_CFG], sizeof s.dir[0], ".");
    CHECK(setup_settings_to_doc(&s, &d));
    CHECK(setup_doc_get(&d, "files", "data_dir", v, sizeof v) && !strcmp(v, "auto"));
    CHECK(setup_doc_get(&d, "files", "xeen_cfg_dir", v, sizeof v) && !strcmp(v, "auto"));
    /* An explicit default-equal value the user did not change keeps its line (only changed values are written). */
    CHECK(setup_doc_parse(&d, "[files]\nmt32_roms_dir = roms/mt32\n[audio]\nmusic_override = auto\n"));
    setup_settings_from_doc(&d, &s);
    CHECK(setup_settings_to_doc(&s, &d));
    CHECK(setup_doc_get(&d, "files", "mt32_roms_dir", v, sizeof v) && !strcmp(v, "roms/mt32"));
    /* A file without a music_override key is wox's system: System MIDI, written explicitly only when it changes. */
    CHECK(setup_doc_parse(&d, "[display]\nscale = 4\n"));
    setup_settings_from_doc(&d, &s);
    CHECK(s.system_midi && s.scale == 4u);
    /* Phase 333: scales up to 10 are read and written back; anything wox rejects reads as the default 3. */
    for (unsigned k = 0; k < 2u; ++k) {
        CHECK(setup_doc_parse(&d, k ? "[display]\nscale = 10\n" : "[display]\nscale = 7\n"));
        setup_settings_from_doc(&d, &s);
        CHECK(s.scale == (k ? 10u : 7u));
    }
    s.scale = 9u;
    CHECK(setup_settings_to_doc(&s, &d) && setup_doc_get(&d, "display", "scale", v, sizeof v) && !strcmp(v, "9"));
    text = setup_doc_text(&d);
    CHECK(text && wox_load(text, &c) && c.scale == 9u);
    free(text);
    s.scale = 10u;
    CHECK(setup_settings_to_doc(&s, &d) && setup_doc_get(&d, "display", "scale", v, sizeof v) && !strcmp(v, "10"));
    text = setup_doc_text(&d);
    CHECK(text && wox_load(text, &c) && c.scale == 10u);
    free(text);
    for (unsigned k = 0; k < 4u; ++k) {
        static const char *const bad[4] = {"[display]\nscale = 11\n", "[display]\nscale = 0\n",
                                           "[display]\nscale = 05\n", "[display]\nscale = 1x\n"};
        CHECK(setup_doc_parse(&d, bad[k]));
        setup_settings_from_doc(&d, &s);
        CHECK(s.scale == 3u);
    }
    s.system_midi = 0;
    CHECK(setup_settings_to_doc(&s, &d));
    CHECK(setup_doc_get(&d, "audio", "music_override", v, sizeof v) && !strcmp(v, "auto"));
    CHECK(!setup_doc_get(&d, "audio", "speech", v, sizeof v)); /* missing speech already reads auto */
    /* Phase 336: on is the one spelling of a set switch (true, which wox refuses, reads as off). */
    CHECK(setup_doc_parse(&d, "[display]\naspect = on\nfullscreen = on\n[addons]\nidentify = on\n"));
    setup_settings_from_doc(&d, &s);
    CHECK(s.aspect && s.fullscreen && s.addon[0]);
    CHECK(setup_doc_parse(&d, "[display]\naspect = true\nfullscreen = true\n[addons]\nidentify = true\n"));
    setup_settings_from_doc(&d, &s);
    CHECK(!s.aspect && !s.fullscreen && !s.addon[0]);
    /* Values the user did not change keep their own spelling (auto, on, an omitted scale), flags and styles too. */
    {
        static const char spelled[] = "[audio]\nmusic_override = auto\nspeech = auto\n[display]\nfullscreen = auto\naspect = "
                                      "on\n[addons]\nidentify = auto\nwizard_eye_plus.sky = 2px\n[bugfix]\n"
                                      "quest_up_down = auto\n";
        CHECK(setup_doc_parse(&d, spelled));
        setup_settings_from_doc(&d, &s);
        CHECK(!s.fullscreen && s.aspect && !s.addon[0] && s.wizard_style[2] == 1u && !s.bugfix[3] && s.scale == 3u);
        CHECK(setup_settings_to_doc(&s, &d));
        text = setup_doc_text(&d);
        CHECK(text && !strcmp(text, spelled));
        free(text);
    }
    setup_doc_free(&d);
}

/* 3. The first-run presets and the flag lists against wox's own key table. */
static void test_presets(void)
{
    setup_settings s;
    const char *sec, *name;
    unsigned wox_flags = 0;
    memset(&s, 0, sizeof s);
    s.system_midi = 1;
    setup_apply_version(&s, 0);
    CHECK(!s.system_midi && !s.dir[SETUP_DIR_SAVE][0]);
    setup_apply_version(&s, 1);
#if defined(_WIN32)
    CHECK(!strcmp(s.dir[SETUP_DIR_SAVE], "cloud_saves\\WORLD") && !strcmp(s.dir[SETUP_DIR_XEEN_CFG], "cloud_saves\\WORLD"));
#else
    CHECK(!strcmp(s.dir[SETUP_DIR_SAVE], "cloud_saves/WORLD") && !strcmp(s.dir[SETUP_DIR_XEEN_CFG], "cloud_saves/WORLD"));
#endif
    setup_apply_style(&s, 1);
    for (unsigned i = 0; i < setup_addon_count; ++i) {
        CHECK(s.addon[i] == (strcmp(setup_addons[i].key, "damage_numbers.stack") != 0));
    }
    for (unsigned i = 0; i < setup_bugfix_count; ++i) {
        CHECK(s.bugfix[i]);
    }
    for (unsigned i = 0; i < SETUP_WIZARD_STYLES; ++i) {
        CHECK(!strcmp(setup_wizard_style_names[s.wizard_style[i]], "black"));
    }
    setup_apply_style(&s, 0);
    for (unsigned i = 0; i < setup_addon_count; ++i) {
        CHECK(!s.addon[i]);
    }
    for (unsigned i = 0; i < setup_bugfix_count; ++i) {
        CHECK(!s.bugfix[i]);
    }
    /* Every [addons]/[bugfix] flag wox knows is in the setup's lists and nothing else is. */
    for (size_t i = 0; xeen_wox_config_setting(i, &sec, &name); ++i) {
        int addons = !strcmp(sec, "addons"), found = 0;
        char want[64];
        if ((!addons && strcmp(sec, "bugfix")) || strstr(name, "wizard_eye_plus.")) {
            continue;
        }
        ++wox_flags;
        snprintf(want, sizeof want, "%s", name); /* Phase 336: the same spelling on both sides */
        for (unsigned k = 0; k < (addons ? setup_addon_count : setup_bugfix_count); ++k) {
            found += !strcmp((addons ? setup_addons : setup_bugfixes)[k].key, want);
        }
        if (found != 1) {
            fprintf(stderr, "wox setting %s/%s is not in the setup's list\n", sec, name);
        }
        CHECK(found == 1);
    }
    CHECK(wox_flags == setup_addon_count + setup_bugfix_count);
    CHECK(setup_addons[4].parent == 3 && !strcmp(setup_addons[setup_addons[4].parent].key, "damage_numbers"));
    /* The wizard-eye style keys are wox's. */
    for (unsigned i = 0; i < SETUP_WIZARD_STYLES; ++i) {
        int found = 0;
        for (size_t k = 0; xeen_wox_config_setting(k, &sec, &name); ++k) {
            found += !strcmp(name, setup_wizard_style_keys[i]) && !strcmp(sec, "addons");
        }
        CHECK(found == 1);
    }
}

/* 4. XEEN.CFG: the default, the mapping of every word, the Save rules, the files. */
static void test_xeen_cfg(void)
{
    static const uint8_t expected_default[SETUP_XEEN_CFG_SIZE] = {
            0x07, 0x00, 0x20, 0x02, 0x03, 0x00, 0x20, 0x02, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03};
    static const int speech_on[12] = {0, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0};
    static const setup_music music_of[12] = {
            SETUP_MUSIC_NONE,  SETUP_MUSIC_ADLIB,  SETUP_MUSIC_SB,   SETUP_MUSIC_SBPRO, SETUP_MUSIC_MT32,  SETUP_MUSIC_CANVAS,
            SETUP_MUSIC_PAS,   SETUP_MUSIC_NONE,   SETUP_MUSIC_WAVE, SETUP_MUSIC_TAP10, SETUP_MUSIC_NONE,  SETUP_MUSIC_NONE};
    static const unsigned ports[SETUP_MUSIC_SYSTEM] = {0, 0x388, 0x388, 0x220, 0x330, 0x330, 0x330, 0x330, 0x340};
    static const unsigned words[SETUP_MUSIC_SYSTEM] = {0, 1, 2, 3, 4, 5, 6, 8, 9};
    uint8_t def[SETUP_XEEN_CFG_SIZE], bytes[40];
    setup_xeen_cfg c;
    char dir[600], path[700], err[800];
    setup_xeen_cfg_default(def);
    CHECK(!memcmp(def, expected_default, sizeof def));
    memset(&c, 0, sizeof c);
    c.size = SETUP_XEEN_CFG_SIZE;
    for (unsigned w = 0; w < 12u; ++w) {
        c.bytes[0] = (uint8_t)w;
        c.bytes[4] = (uint8_t)w;
        CHECK(setup_xeen_cfg_speech(&c) == speech_on[w]);
        CHECK(setup_xeen_cfg_music(&c) == music_of[w]);
    }
    c.bytes[1] = 1; /* 0x102: unknown */
    CHECK(!setup_xeen_cfg_speech(&c));
    c.bytes[5] = 1;
    CHECK(setup_xeen_cfg_music(&c) == SETUP_MUSIC_NONE);
    for (unsigned m = 0; m < SETUP_MUSIC_SYSTEM; ++m) {
        unsigned w, p;
        CHECK(setup_music_word((setup_music)m, &w, &p) && w == words[m] && p == ports[m]);
    }
    {
        unsigned w = 77, p = 77;
        CHECK(!setup_music_word(SETUP_MUSIC_SYSTEM, &w, &p) && w == 77u && p == 77u);
    }
    /* The Save rules on the genuine fresh file's bytes: SB16 speech (10) on, MT-32 music at 330h. */
    {
        static const uint8_t fresh[SETUP_XEEN_CFG_SIZE] = {0x0a, 0x00, 0x20, 0x02, 0x04, 0x00, 0x30, 0x03, 0x07, 0x00,
                                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                                                           0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03};
        setup_xeen_cfg f;
        memset(&f, 0, sizeof f);
        memcpy(f.bytes, fresh, sizeof fresh);
        f.size = sizeof fresh;
        setup_xeen_cfg_apply(&f, 1, SETUP_MUSIC_MT32); /* unchanged: nothing */
        CHECK(!memcmp(f.bytes, fresh, sizeof fresh));
        setup_xeen_cfg_apply(&f, 1, SETUP_MUSIC_SYSTEM); /* System MIDI: XEEN.CFG's music untouched */
        CHECK(!memcmp(f.bytes, fresh, sizeof fresh));
        setup_xeen_cfg_apply(&f, 0, SETUP_MUSIC_TAP10); /* speech off: word 0, port kept; TAP-10 at 340h */
        CHECK(f.bytes[0] == 0 && f.bytes[1] == 0 && f.bytes[2] == 0x20 && f.bytes[3] == 0x02);
        CHECK(f.bytes[4] == 9 && f.bytes[5] == 0 && f.bytes[6] == 0x40 && f.bytes[7] == 0x03);
        CHECK(!memcmp(f.bytes + 8, fresh + 8, sizeof fresh - 8u));
        f.bytes[2] = 0x88; /* an off file's port changes to 220h when speech goes on */
        setup_xeen_cfg_apply(&f, 1, SETUP_MUSIC_NONE); /* speech on: SB Pro 220h; No music: word 0, port kept */
        CHECK(f.bytes[0] == 7 && f.bytes[2] == 0x20 && f.bytes[3] == 0x02);
        CHECK(f.bytes[4] == 0 && f.bytes[6] == 0x40 && f.bytes[7] == 0x03);
        f.bytes[0] = 1; /* the PC speaker: off; unchanged off keeps the value */
        setup_xeen_cfg_apply(&f, 0, SETUP_MUSIC_NONE);
        CHECK(f.bytes[0] == 1);
        f.bytes[4] = 7; /* the PC speaker's music shows No music; kept unless Music changes */
        setup_xeen_cfg_apply(&f, 0, SETUP_MUSIC_NONE);
        CHECK(f.bytes[4] == 7);
        f.bytes[4] = 0x63; /* unknown */
        setup_xeen_cfg_apply(&f, 0, SETUP_MUSIC_NONE);
        CHECK(f.bytes[4] == 0x63);
        setup_xeen_cfg_apply(&f, 0, SETUP_MUSIC_SBPRO);
        CHECK(f.bytes[4] == 3 && f.bytes[6] == 0x20 && f.bytes[7] == 0x02);
        f.bytes[0] = 3; /* nullsnd: off */
        setup_xeen_cfg_apply(&f, 1, SETUP_MUSIC_SBPRO);
        CHECK(f.bytes[0] == 7);
    }
    /* Files: XEEN.CFG or xeen.cfg, 29 or 30 bytes, length kept; create never overwrites. */
    path_in(dir, sizeof dir, "cfgdir");
    CHECK(make_dir(dir) == 0);
    CHECK(!setup_xeen_cfg_read(dir, &c, err, sizeof err) && !err[0]);
    CHECK(setup_xeen_cfg_create(dir, err, sizeof err));
    snprintf(path, sizeof path, "%s/XEEN.CFG", dir);
    CHECK(read_file(path, bytes, sizeof bytes) == SETUP_XEEN_CFG_SIZE && !memcmp(bytes, expected_default, 29));
    CHECK(!setup_xeen_cfg_create(dir, err, sizeof err) && strstr(err, "already exists"));
    CHECK(setup_xeen_cfg_read(dir, &c, err, sizeof err) && c.size == 29u && setup_xeen_cfg_speech(&c) &&
          setup_xeen_cfg_music(&c) == SETUP_MUSIC_SBPRO);
    remove(path);
    snprintf(path, sizeof path, "%s/xeen.cfg", dir);
    memcpy(bytes, expected_default, 29);
    bytes[29] = 0x5a;
    CHECK(write_file(path, bytes, 30));
    CHECK(setup_xeen_cfg_read(dir, &c, err, sizeof err) && c.size == 30u && strstr(c.path, "xeen.cfg"));
    setup_xeen_cfg_apply(&c, 0, SETUP_MUSIC_CANVAS);
    CHECK(setup_xeen_cfg_write(&c, err, sizeof err));
    memset(bytes, 0, sizeof bytes);
    CHECK(read_file(path, bytes, sizeof bytes) == 30u && bytes[0] == 0 && bytes[4] == 5 && bytes[6] == 0x30 &&
          bytes[7] == 3 && bytes[29] == 0x5a && bytes[0x1c] == 3);
    CHECK(write_file(path, bytes, 31));
    CHECK(!setup_xeen_cfg_read(dir, &c, err, sizeof err) && !strcmp(err, "This XEEN.CFG is too long (more than 30 bytes)."));
    CHECK(write_file(path, bytes, 0));
    CHECK(!setup_xeen_cfg_read(dir, &c, err, sizeof err) && !strcmp(err, "This XEEN.CFG is empty."));
    {
        /* Phase 335: GOG's 28-byte XEEN.CFG (D:\MM\Might and Magic 4-5, byte for byte) is valid, as for the game's
         * 47C1F (at most 29 bytes read, the count unchecked, the rest 0); a change is written back at 28 bytes */
        static const uint8_t gog[28] = {0x06, 0x00, 0x20, 0x02, 0x02, 0x00, 0x88, 0x03, 0x07, 0x00, 0, 0, 0, 0,
                                        0,    0,    0,    0,    0x01, 0x00, 0,    0,    0,    0,    0x01, 0, 0x01, 0};
        CHECK(write_file(path, gog, sizeof gog));
        CHECK(setup_xeen_cfg_read(dir, &c, err, sizeof err) && c.size == 28u && c.bytes[28] == 0 && c.bytes[29] == 0);
        CHECK(setup_xeen_cfg_speech(&c) && setup_xeen_cfg_music(&c) == SETUP_MUSIC_SB);
        setup_xeen_cfg_apply(&c, 1, SETUP_MUSIC_CANVAS);
        CHECK(setup_xeen_cfg_write(&c, err, sizeof err));
        memset(bytes, 0xee, sizeof bytes);
        CHECK(read_file(path, bytes, sizeof bytes) == 28u && bytes[4] == 5 && bytes[6] == 0x30 && bytes[7] == 3 &&
              !memcmp(bytes + 8, gog + 8, 20));
        /* a 6-byte file: valid; written back through the music port (8 bytes) */
        CHECK(write_file(path, gog, 6));
        CHECK(setup_xeen_cfg_read(dir, &c, err, sizeof err) && c.size == 6u);
        setup_xeen_cfg_apply(&c, 1, SETUP_MUSIC_CANVAS);
        CHECK(setup_xeen_cfg_write(&c, err, sizeof err) && read_file(path, bytes, sizeof bytes) == 8u &&
              bytes[4] == 5 && bytes[6] == 0x30);
    }
    CHECK(!setup_xeen_cfg_create(dir, err, sizeof err)); /* any spelling present: never overwritten */
    remove(path);
    remove(dir);
}

/* 5. Paths, the GOG image, the ROM warnings. */
static void test_paths(void)
{
    char out[1200], sub[700], other[700], cwd_base[700];
    setup_settings s;
    path_in(sub, sizeof sub, "a/b c/d");
    CHECK(setup_make_dirs("", sub) && setup_dir_exists("", sub));
    CHECK(setup_dir_exists(root, "a/b c") && !setup_dir_exists(root, "a/none"));
    setup_relative_to(root, sub, out, sizeof out);
    CHECK(!strcmp(out, "a/b c/d"));
    setup_relative_to(root, root, out, sizeof out);
    CHECK(!strcmp(out, "."));
    snprintf(other, sizeof other, "%s-other", root);
    CHECK(make_dir(other) == 0);
    setup_relative_to(root, other, out, sizeof out); /* a sibling sharing the prefix: absolute */
    CHECK(out[0] == '/' && strstr(out, "-other"));
    remove(other);
    setup_resolve("/base", "rel/x", out, sizeof out);
    CHECK(!strcmp(out, "/base/rel/x"));
    setup_resolve("/base", "/abs", out, sizeof out);
    CHECK(!strcmp(out, "/abs"));
    setup_resolve("/base", ".", out, sizeof out);
    CHECK(!strcmp(out, "/base"));
    setup_dirname("/x/y/wox.cfg", out, sizeof out);
    CHECK(!strcmp(out, "/x/y"));
    setup_dirname("wox.cfg", out, sizeof out);
    CHECK(!strcmp(out, "."));
    setup_dirname("/wox.cfg", out, sizeof out);
    CHECK(!strcmp(out, "/"));
    /* GAME1.GOG in any case */
    CHECK(!setup_has_gog_image(root));
    path_in(cwd_base, sizeof cwd_base, "game1.Gog");
    CHECK(write_file(cwd_base, "x", 1));
    CHECK(setup_has_gog_image(root));
    remove(cwd_base);
    path_in(cwd_base, sizeof cwd_base, "GAME1.GOG.bak");
    CHECK(write_file(cwd_base, "x", 1) && !setup_has_gog_image(root));
    remove(cwd_base);
    /* ROM warnings: none for the OPL cards and System MIDI; MT-32 without its two ROMs; SC-55 without its folder. */
    memset(&s, 0, sizeof s);
    snprintf(s.dir[SETUP_DIR_MT32], sizeof s.dir[0], "roms/mt32");
    snprintf(s.dir[SETUP_DIR_SC55], sizeof s.dir[0], "roms/sc55");
    CHECK(!setup_rom_warning(&s, SETUP_MUSIC_SBPRO, root, out, sizeof out));
    CHECK(!setup_rom_warning(&s, SETUP_MUSIC_SYSTEM, root, out, sizeof out));
    CHECK(setup_rom_warning(&s, SETUP_MUSIC_MT32, root, out, sizeof out) && strstr(out, "MT-32"));
    CHECK(setup_rom_warning(&s, SETUP_MUSIC_TAP10, root, out, sizeof out) && strstr(out, "SC-55"));
    /* Phase 333: the owner's wording, the system named after the chosen card. */
    {
        static const struct {
            setup_music m;
            const char *dir, *rest;
        } want[] = {{SETUP_MUSIC_CANVAS, "roms/sc55", " not found - sc55 plays on Nuked-SC55. "},
                    {SETUP_MUSIC_WAVE, "roms/sc55", " not found - wave plays on Nuked-SC55. "},
                    {SETUP_MUSIC_TAP10, "roms/sc55", " not found - tap10 plays on Nuked-SC55. "},
                    {SETUP_MUSIC_MT32, "roms/mt32", " not found - mt32 plays on Munt. "}};
        for (unsigned i = 0; i < sizeof want / sizeof want[0]; ++i) {
            char expect[2 * SETUP_PATH_MAX];
            path_in(sub, sizeof sub, want[i].dir);
            snprintf(expect, sizeof expect, "%s ROM directory (%s)%sThe game will not start with this music until the ROMs are there.",
                     want[i].m == SETUP_MUSIC_MT32 ? "MT-32" : "SC-55", sub, want[i].rest);
            CHECK(setup_rom_warning(&s, want[i].m, root, out, sizeof out) && !strcmp(out, expect));
        }
    }
    CHECK(setup_make_dirs(root, "roms/sc55"));
    CHECK(!setup_rom_warning(&s, SETUP_MUSIC_CANVAS, root, out, sizeof out));
    CHECK(!setup_rom_warning(&s, SETUP_MUSIC_WAVE, root, out, sizeof out));
    path_in(sub, sizeof sub, "roms/mt32");
    CHECK(setup_make_dirs("", sub));
    path_in(sub, sizeof sub, "roms/mt32/MT32_CONTROL.ROM");
    CHECK(write_file(sub, "x", 1));
    path_in(sub, sizeof sub, "roms/mt32/MT32_PCM.ROM");
    CHECK(write_file(sub, "x", 1));
    CHECK(!setup_rom_warning(&s, SETUP_MUSIC_MT32, root, out, sizeof out));
    remove(sub);
    CHECK(setup_rom_warning(&s, SETUP_MUSIC_MT32, root, out, sizeof out) && strstr(out, "MT-32 ROM (") &&
          strstr(out, "MT32_PCM.ROM) not found - mt32 plays on Munt. The game will not start"));
    path_in(sub, sizeof sub, "roms/mt32/MT32_CONTROL.ROM");
    remove(sub);
    path_in(sub, sizeof sub, "roms/mt32");
    remove(sub);
    path_in(sub, sizeof sub, "roms/sc55");
    remove(sub);
    path_in(sub, sizeof sub, "roms");
    remove(sub);
    path_in(sub, sizeof sub, "a/b c/d");
    remove(sub);
    path_in(sub, sizeof sub, "a/b c");
    remove(sub);
    path_in(sub, sizeof sub, "a");
    remove(sub);
}

/* 6. The whole first run and a Save, read back with wox's parser. */
static void test_flows(void)
{
    char wox[700], err[1400], cfgdir[800], cfgpath[900], v[256];
    xeen_wox_config c;
    setup_doc d;
    setup_settings s;
    setup_xeen_cfg x;
    uint8_t bytes[40];
    path_in(wox, sizeof wox, "wox.cfg");
    /* Other + Vanilla */
    CHECK(setup_first_run(wox, 0, 0, err, sizeof err));
    CHECK(xeen_wox_config_load(wox, &c, err, sizeof err));
    CHECK(c.music_auto && c.speech == XEEN_SPEECH_AUTO && c.save_dir_auto && c.xeen_cfg_auto && c.data_auto);
    CHECK(!c.addon_identify && !c.addon_wizard_eye_plus && !c.bugfix_spell_description);
    path_in(cfgdir, sizeof cfgdir, "cloud_saves");
    CHECK(!setup_dir_exists("", cfgdir));
    /* GOG + Enhanced over it (as the recreation of an invalid wox.cfg does) */
    CHECK(write_file(wox, "[nonsense\n", 10) && !xeen_wox_config_load(wox, &c, err, sizeof err));
    CHECK(setup_first_run(wox, 1, 1, err, sizeof err));
    CHECK(xeen_wox_config_load(wox, &c, err, sizeof err));
    CHECK(!c.save_dir_auto && strstr(c.save_dir, "cloud_saves") && !c.xeen_cfg_auto && strstr(c.xeen_cfg_dir, "WORLD"));
    CHECK(c.addon_identify && c.addon_wizard_eye_plus && !c.addon_damage_numbers_stack && c.bugfix_quest_up_down &&
          c.addon_wizard_eye_plus_style[0] == 0u && c.addon_wizard_eye_plus_style[2] == 0u && c.music_auto);
    path_in(cfgdir, sizeof cfgdir, "cloud_saves/WORLD");
    snprintf(cfgpath, sizeof cfgpath, "%s/XEEN.CFG", cfgdir);
    CHECK(read_file(cfgpath, bytes, sizeof bytes) == 29u && bytes[0] == 7 && bytes[4] == 3);
    /* A second GOG run keeps an existing XEEN.CFG. */
    bytes[0] = 1;
    CHECK(write_file(cfgpath, bytes, 29));
    CHECK(setup_first_run(wox, 1, 0, err, sizeof err));
    CHECK(read_file(cfgpath, bytes, sizeof bytes) == 29u && bytes[0] == 1);
    /* Save: the settings window's values into wox.cfg and XEEN.CFG */
    setup_doc_init(&d);
    CHECK(setup_doc_load(&d, wox, err, sizeof err));
    setup_settings_from_doc(&d, &s);
    CHECK(!s.system_midi && !strcmp(s.dir[SETUP_DIR_XEEN_CFG], setup_gog_dir()));
    CHECK(setup_xeen_cfg_read(cfgdir, &x, err, sizeof err) && !setup_xeen_cfg_speech(&x));
    s.scale = 5;
    s.aspect = 1;
    s.fullscreen = 1;
    CHECK(setup_save(wox, &d, &s, &x, 1, SETUP_MUSIC_MT32, err, sizeof err));
    CHECK(xeen_wox_config_load(wox, &c, err, sizeof err) && c.scale == 5u && c.aspect && c.fullscreen);
    CHECK(read_file(cfgpath, bytes, sizeof bytes) == 29u && bytes[0] == 7 && bytes[2] == 0x20 && bytes[4] == 4 &&
          bytes[6] == 0x30 && bytes[7] == 3);
    /* A value wox would reject is never written: the file stays as it was. */
    {
        char before[16384], after[16384];
        size_t nb = read_file(wox, before, sizeof before), na;
        CHECK(setup_doc_set(&d, "display", "scale", "11"));
        CHECK(!setup_doc_save(&d, wox, err, sizeof err) && strstr(err, "scale"));
        na = read_file(wox, after, sizeof after);
        CHECK(nb == na && !memcmp(before, after, nb));
    }
    CHECK(setup_doc_get(&d, "display", "scale", v, sizeof v));
    setup_doc_free(&d);
    remove(cfgpath);
    remove(cfgdir);
    path_in(cfgdir, sizeof cfgdir, "cloud_saves");
    remove(cfgdir);
    remove(wox);
}

/* 7. The windows' decisions: XEEN.CFG's state and note, the questions, the folders Save needs. */
static void test_decisions(void)
{
    char dir[900], note[1600], q[2400], path[1000], err[600];
    setup_xeen_cfg c;
    setup_settings s;
    setup_dir which = SETUP_DIRS;
    uint8_t def[SETUP_XEEN_CFG_SIZE];
    CHECK(setup_xeen_cfg_locate(root, "nowhere", &c, dir, sizeof dir, note, sizeof note) == SETUP_CFG_NO_FOLDER &&
          !strcmp(note, "This folder does not exist.") && strstr(dir, "/nowhere"));
    CHECK(setup_make_dirs(root, "here"));
    CHECK(setup_xeen_cfg_locate(root, "here", &c, dir, sizeof dir, note, sizeof note) == SETUP_CFG_ABSENT &&
          !strcmp(note, "No XEEN.CFG in this folder."));
    setup_create_question(dir, q, sizeof q);
    CHECK(!strncmp(q, "Create a default XEEN.CFG?\n\n", 28) && strstr(q, dir) && strstr(q, "IRQ 7, DMA 1"));
    snprintf(path, sizeof path, "%s/XEEN.CFG", dir);
    setup_xeen_cfg_default(def);
    CHECK(write_file(path, def, 12)); /* Phase 335: short files are valid (the game reads them) */
    CHECK(setup_xeen_cfg_locate(root, "here", &c, dir, sizeof dir, note, sizeof note) == SETUP_CFG_PROPER &&
          c.size == 12u);
    CHECK(write_file(path, "", 0));
    CHECK(setup_xeen_cfg_locate(root, "here", &c, dir, sizeof dir, note, sizeof note) == SETUP_CFG_INVALID &&
          !strcmp(note, "This XEEN.CFG is empty."));
    /* Phase 335: Create default... replaces an invalid file after its question */
    setup_replace_question(dir, note, q, sizeof q);
    static const char replace_head[] = "Replace XEEN.CFG with the default?\n\nThis XEEN.CFG is empty. The game cannot use";
    CHECK(!strncmp(q, replace_head, sizeof replace_head - 1u) &&
          strstr(q, dir));
    CHECK(setup_xeen_cfg_replace(dir, err, sizeof err));
    CHECK(setup_xeen_cfg_locate(root, "here", &c, dir, sizeof dir, note, sizeof note) == SETUP_CFG_PROPER &&
          c.size == sizeof def && !memcmp(c.bytes, def, sizeof def));
    CHECK(write_file(path, def, sizeof def));
    CHECK(setup_xeen_cfg_locate(root, "here", &c, dir, sizeof dir, note, sizeof note) == SETUP_CFG_PROPER &&
          c.size == sizeof def);
    remove(path);
    /* Save's folders: the save folder first, then XEEN.CFG's; empty texts and existing folders ask nothing. */
    memset(&s, 0, sizeof s);
    CHECK(!setup_missing_folder(&s, root, &which, q, sizeof q) && !setup_missing_data(&s, root, q, sizeof q));
    snprintf(s.dir[SETUP_DIR_SAVE], sizeof s.dir[0], "saves");
    snprintf(s.dir[SETUP_DIR_XEEN_CFG], sizeof s.dir[0], "cfg");
    snprintf(s.dir[SETUP_DIR_DATA], sizeof s.dir[0], "data");
    CHECK(setup_missing_folder(&s, root, &which, q, sizeof q) && which == SETUP_DIR_SAVE &&
          !strncmp(q, "Create the save folder?\n\nsaves", 30));
    CHECK(setup_make_dirs(root, "saves"));
    CHECK(setup_missing_folder(&s, root, &which, q, sizeof q) && which == SETUP_DIR_XEEN_CFG &&
          strstr(q, "XEEN.CFG folder"));
    snprintf(s.dir[SETUP_DIR_XEEN_CFG], sizeof s.dir[0], "here");
    CHECK(!setup_missing_folder(&s, root, &which, q, sizeof q));
    CHECK(setup_missing_data(&s, root, q, sizeof q) && !strncmp(q, "Save anyway?\n\nThe data folder data", 34));
    snprintf(s.dir[SETUP_DIR_DATA], sizeof s.dir[0], ".");
    CHECK(!setup_missing_data(&s, root, q, sizeof q));
    path_in(path, sizeof path, "saves");
    remove(path);
    path_in(path, sizeof path, "here");
    remove(path);
}

/* ------------------------------------------------------------------ Phase 335: the Music note and ROM downloads */
#include "phase335_zip_vectors.h"

/** Byte I of test member K (tests/phase335_make_zip_vectors.py's content()). */
static unsigned char zip_content(unsigned k, size_t i)
{
    static const char text[] = "Sphinx of black quartz, judge my vow; the five boxing wizards jump quickly. ";
    return (unsigned char)text[(i + (i / 37u) * k) % (sizeof text - 1u)];
}
/** Whether file NAME in DIR holds exactly member K's first SIZE bytes. */
static int holds(const char *dir, const char *name, unsigned k, size_t size)
{
    static unsigned char buf[80000];
    char p[1200];
    size_t n;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    n = read_file(p, buf, sizeof buf);
    if (n != size) {
        return 0;
    }
    for (size_t i = 0; i < n; ++i) {
        if (buf[i] != zip_content(k, i)) {
            return 0;
        }
    }
    return 1;
}
static int exists(const char *dir, const char *name)
{
    char p[1200];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return stat(p, &st) == 0;
}
static void remove_in(const char *dir, const char *name)
{
    char p[1200];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    remove(p);
}
static void test_rom_downloads(void)
{
    static const char *const sc55_files[] = {"sc55_rom1.bin", "sc55_rom2.bin", "sc55_waverom1.bin",
                                             "sc55_waverom2.bin", "sc55_waverom3.bin"};
    static const unsigned sc55_k[] = {1, 2, 3, 4, 5};
    static const size_t sc55_size[] = {24, 300, 6000, 2500, 70000};
    static unsigned char copy[8000];
    char top[700], mid[720], dir[760], err[800];
    setup_settings s;
    /* the Music list's note, and which set each card needs */
    for (unsigned m = 0; m < SETUP_MUSIC_CHOICES; ++m) {
        const char *b = setup_music_badge((setup_music)m);
        CHECK(m == SETUP_MUSIC_CANVAS ? b && !strcmp(b, "recommended") : b == NULL);
        CHECK(setup_music_rom_set((setup_music)m) == (m == SETUP_MUSIC_MT32                               ? SETUP_ROMS_MT32
                                                      : m == SETUP_MUSIC_CANVAS || m == SETUP_MUSIC_WAVE ||
                                                                m == SETUP_MUSIC_TAP10 ? SETUP_ROMS_SC55
                                                                                       : SETUP_ROMS_NONE));
    }
    CHECK(!strcmp(setup_rom_set_url(SETUP_ROMS_SC55),
                  "https://dn710106.ca.archive.org/0/items/nuked-sc-55-clap-rom-files/Nuked-SC55-CLAP-ROM-files.zip"));
    CHECK(!strcmp(setup_rom_set_url(SETUP_ROMS_MT32), "https://archive.org/download/"
                                                      "mame-versioned-roland-mt-32-and-cm-32l-rom-files/"
                                                      "MT-32_v1.07_legacy_ROM_files.zip"));
    CHECK(setup_rom_set_url(SETUP_ROMS_NONE) == NULL && !strcmp(setup_rom_set_name(SETUP_ROMS_NONE), ""));
    CHECK(!strcmp(setup_rom_set_name(SETUP_ROMS_MT32), "MT-32") && !strcmp(setup_rom_set_name(SETUP_ROMS_SC55), "SC-55"));
    memset(&s, 0, sizeof s);
    snprintf(s.dir[SETUP_DIR_MT32], sizeof s.dir[0], "m32");
    snprintf(s.dir[SETUP_DIR_SC55], sizeof s.dir[0], "%s/abs", root);
    setup_rom_set_dir(&s, SETUP_ROMS_MT32, root, dir, sizeof dir);
    path_in(mid, sizeof mid, "m32");
    CHECK(!strcmp(dir, mid));
    setup_rom_set_dir(&s, SETUP_ROMS_SC55, "/elsewhere", dir, sizeof dir);
    path_in(mid, sizeof mid, "abs");
    CHECK(!strcmp(dir, mid));

    /* SC-55: the five .bin files of the v1.00 folder (stored, fixed and dynamic members) into a new nested folder;
     * the .DS_Store, notes.txt and the other folder's .bin are not taken */
    path_in(top, sizeof top, "dl");
    snprintf(mid, sizeof mid, "%s/roms", top);
    snprintf(dir, sizeof dir, "%s/sc55", mid);
    CHECK(setup_rom_install(SETUP_ROMS_SC55, sc55_zip, sizeof sc55_zip, dir, err, sizeof err));
    for (unsigned i = 0; i < 5u; ++i) {
        CHECK(holds(dir, sc55_files[i], sc55_k[i], sc55_size[i]));
    }
    CHECK(!exists(dir, ".DS_Store") && !exists(dir, "notes.txt") && !exists(dir, "sc55_rom1.bin.part"));
    /* a second install keeps the files already there */
    CHECK(write_file(strcat(strcpy(err, dir), "/sc55_rom1.bin"), "mine", 4));
    CHECK(setup_rom_install(SETUP_ROMS_SC55, sc55_zip, sizeof sc55_zip, dir, err, sizeof err));
    CHECK(read_file(strcat(strcpy(err, dir), "/sc55_rom1.bin"), copy, sizeof copy) == 4u && !memcmp(copy, "mine", 4));
    for (unsigned i = 0; i < 5u; ++i) {
        remove_in(dir, sc55_files[i]);
    }
    remove(dir);

    /* MT-32: the two .ROM files at the archive's top; a missing one is added beside the existing one */
    snprintf(dir, sizeof dir, "%s/mt32", mid);
    CHECK(make_dir(dir) == 0);
    CHECK(write_file(strcat(strcpy(err, dir), "/MT32_CONTROL.ROM"), "own", 3));
    CHECK(setup_rom_install(SETUP_ROMS_MT32, mt32_zip, sizeof mt32_zip, dir, err, sizeof err));
    CHECK(read_file(strcat(strcpy(err, dir), "/MT32_CONTROL.ROM"), copy, sizeof copy) == 3u);
    CHECK(holds(dir, "MT32_PCM.ROM", 8, 9000) && !exists(dir, "readme.txt"));
    remove_in(dir, "MT32_CONTROL.ROM");
    CHECK(setup_rom_install(SETUP_ROMS_MT32, mt32_zip, sizeof mt32_zip, dir, err, sizeof err));
    CHECK(holds(dir, "MT32_CONTROL.ROM", 7, 4000));
    remove_in(dir, "MT32_CONTROL.ROM");
    remove_in(dir, "MT32_PCM.ROM");
    remove(dir);

    /* refusals write nothing (the folder is not even created) */
    snprintf(dir, sizeof dir, "%s/never", mid);
    CHECK(!setup_rom_install(SETUP_ROMS_SC55, mt32_zip, sizeof mt32_zip, dir, err, sizeof err) &&
          !strcmp(err, "the archive does not hold the 5 SC-55 ROM files expected (found 0)"));
    CHECK(!setup_rom_install(SETUP_ROMS_MT32, sc55_zip, sizeof sc55_zip, dir, err, sizeof err) &&
          strstr(err, "found 0"));
    CHECK(!setup_rom_install(SETUP_ROMS_NONE, mt32_zip, sizeof mt32_zip, dir, err, sizeof err));
    CHECK(!setup_rom_install(SETUP_ROMS_MT32, mt32_zip, sizeof mt32_zip - 30u, dir, err, sizeof err) &&
          !strcmp(err, "the download is not a ZIP archive"));
    CHECK(!setup_rom_install(SETUP_ROMS_MT32, (const unsigned char *)"<html>404</html>", 16, dir, err, sizeof err) &&
          !strcmp(err, "the download is not a ZIP archive"));
    memcpy(copy, mt32_zip, sizeof mt32_zip);
    copy[30 + 16 + 100] ^= 0x40u; /* inside MT32_CONTROL.ROM's 169 bytes of compressed data */
    CHECK(!setup_rom_install(SETUP_ROMS_MT32, copy, sizeof mt32_zip, dir, err, sizeof err) &&
          !strncmp(err, "the archive is damaged", 22));
    CHECK(!setup_rom_install(SETUP_ROMS_MT32, mt32_one_zip, sizeof mt32_one_zip, dir, err, sizeof err) &&
          !strcmp(err, "the archive does not hold the 2 MT-32 ROM files expected (found 1)"));
    CHECK(!setup_rom_install(SETUP_ROMS_MT32, mt32_far_zip, sizeof mt32_far_zip, dir, err, sizeof err) &&
          !strcmp(err, "the archive is damaged"));
    /* Phase 337: a stored block whose NLEN is not ~LEN, with the right size and CRC-32: damaged */
    CHECK(!setup_rom_install(SETUP_ROMS_MT32, mt32_nlen_zip, sizeof mt32_nlen_zip, dir, err, sizeof err) &&
          !strcmp(err, "the archive is damaged"));
    {
        /* intact data under another CRC-32 in the central directory */
        size_t central = (size_t)mt32_zip[sizeof mt32_zip - 6u] | (size_t)mt32_zip[sizeof mt32_zip - 5u] << 8;
        memcpy(copy, mt32_zip, sizeof mt32_zip); /* CENTRAL: the directory's offset (EOCD + 16) */
        copy[central + 16u] ^= 1u;
        CHECK(!setup_rom_install(SETUP_ROMS_MT32, copy, sizeof mt32_zip, dir, err, sizeof err) &&
              !strcmp(err, "the archive is damaged (CRC mismatch)"));
    }
    CHECK(!exists(mid, "never"));
    remove(mid);
    /* every single-bit change of either archive is parsed and inflated safely (ASan/UBSan builds watch it); the
     * target folder lies below a file, so nothing is ever written */
    snprintf(dir, sizeof dir, "%s/file", top);
    CHECK(write_file(dir, "x", 1));
    snprintf(dir, sizeof dir, "%s/file/fuzz", top);
    for (int set = 0; set < 2; ++set) {
        const unsigned char *zip = set ? sc55_zip : mt32_zip;
        size_t n = set ? sizeof sc55_zip : sizeof mt32_zip;
        for (size_t at = 0; at < n; at += set ? 3u : 1u) {
            memcpy(copy, zip, n);
            copy[at] ^= (unsigned char)(1u << (at % 8u));
            (void)setup_rom_install(set ? SETUP_ROMS_SC55 : SETUP_ROMS_MT32, copy, n, dir, err, sizeof err);
        }
    }
    CHECK(!setup_rom_install(SETUP_ROMS_MT32, mt32_zip, sizeof mt32_zip, dir, err, sizeof err) &&
          strstr(err, "could not be created"));
    remove_in(top, "file");
    remove(top);
}

int main(void)
{
    snprintf(root, sizeof root, "phase331_setup_%ld", (long)process_id());
    if (make_dir(root) != 0) {
        fprintf(stderr, "cannot create %s\n", root);
        return 1;
    }
    {
        /* absolute, so the relative-path checks compare real directories */
        char cwd[400];
#if defined(_WIN32)
        if (!_getcwd(cwd, sizeof cwd)) {
#else
        if (!getcwd(cwd, sizeof cwd)) {
#endif
            return 1;
        }
        snprintf(root, sizeof root, "%s/phase331_setup_%ld", cwd, (long)process_id());
    }
    test_document();
    test_settings();
    test_presets();
    test_xeen_cfg();
    test_paths();
    test_flows();
    test_decisions();
    test_rom_downloads();
    {
        /* Phase 335: Cancel asks only after a change (setup_settings_same) */
        setup_settings a, b;
        memset(&a, 0, sizeof a);
        snprintf(a.dir[SETUP_DIR_DATA], sizeof a.dir[0], ".");
        a.scale = 6u;
        a.addon[0] = 1;
        b = a;
        memset(b.dir[SETUP_DIR_DATA] + 2, 'x', 8); /* bytes after the text do not count */
        b.addon[0] = 2;                             /* any nonzero is on */
        b.addon[31] = 1;                            /* past the add-on list: not a setting */
        CHECK(setup_settings_same(&a, &b));
        b = a, b.scale = 5u;
        CHECK(!setup_settings_same(&a, &b));
        b = a, b.addon[0] = 0;
        CHECK(!setup_settings_same(&a, &b));
        b = a, b.wizard_style[1] = 1u;
        CHECK(!setup_settings_same(&a, &b));
        b = a, b.bugfix[0] = 1;
        CHECK(!setup_settings_same(&a, &b));
        b = a, b.dir[SETUP_DIR_SAVE][0] = 's';
        CHECK(!setup_settings_same(&a, &b));
        b = a, b.system_midi = 1;
        CHECK(!setup_settings_same(&a, &b));
        b = a, b.locked_scale = 1;
        CHECK(!setup_settings_same(&a, &b));
    }
    {
        char p[600];
        path_in(p, sizeof p, "probe.cfg");
        remove(p);
    }
    remove(root);
    if (!failures) {
        printf("phase331 setup: all checks passed\n");
    }
    return failures != 0;
}
