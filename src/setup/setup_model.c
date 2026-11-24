/* Phase 331: the setup tool's core (setup_model.h). Host tooling, not original behaviour. */
#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif
#include "setup_model.h"
#include "xeen/wox_config.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#endif

/* The longest setting line written: wox's parser reads lines into 2048 bytes (xeen_wox_config_load) and rejects a
 * longer one; the margin leaves room for the line ending. */
#define SETUP_LINE_LIMIT 2000u
#if defined(_WIN32)
#define SETUP_SEPARATOR '\\'
#define SETUP_NEWLINE "\r\n"
#else
#define SETUP_SEPARATOR '/'
#define SETUP_NEWLINE "\n"
#endif

/* The add-ons and bug fixes in the generated wox.cfg's order (xeen_wox_config_default_text). */
const setup_flag setup_addons[] = {
        {"identify", "Identify: identified items show their statistics", -1},
        {"map_movement", "Map movement: pan the map (M) with the arrows or a drag", -1},
        {"training_preview", "Training preview: a Prev button at the training grounds", -1},
        {"damage_numbers", "Damage numbers over the portraits and the monsters", -1},
        {"damage_numbers.stack", "Stack: one running total per target", 3},
        {"monster_difficulty", "Monster difficulty icons in the 3D view", -1},
        {"spell_short_description", "Spell short description in Cast Spell", -1},
        {"mouse_scroll", "Mouse wheel scrolls the lists", -1},
        {"town_icons", "Town icons on the map and the minimap", -1},
        {"character_details", "Character details: what the character's values do", -1},
        {"spell_list_sort", "Spell list sort (A-Z or by type)", -1},
        {"world_map", "World map (W in the map)", -1},
        {"quest_journal", "Quest journal: numbered quest items", -1},
        {"please_wait_skip", "Skip the \"Please Wait...\" box", -1},
        {"wizard_eye_plus", "Wizard Eye+: unseen cells through a checkerboard", -1},
};
const unsigned setup_addon_count = sizeof setup_addons / sizeof *setup_addons;
const setup_flag setup_bugfixes[] = {
        {"spell_description", "Corrected spell descriptions (the guild's Spell Info and Buy)", -1},
        {"left_clip_colors", "Own colours for pictures cut by the 3D view's left edge", -1},
        {"sprite_width_trim", "Shrunk pictures trimmed to their stated width", -1},
        {"quest_up_down", "U and D scroll the Quests dialog", -1},
};
const unsigned setup_bugfix_count = sizeof setup_bugfixes / sizeof *setup_bugfixes;
const char *const setup_wizard_style_keys[SETUP_WIZARD_STYLES] = {
        "wizard_eye_plus.indoor", "wizard_eye_plus.outdoor", "wizard_eye_plus.sky"};
const char *const setup_wizard_style_names[SETUP_WIZARD_STYLES] = {"1px", "2px", "black"};
int setup_addon_index(const char *key)
{
    for (unsigned i = 0; i < setup_addon_count; ++i) {
        if (!strcmp(setup_addons[i].key, key)) {
            return (int)i;
        }
    }
    return -1;
}

const char *setup_music_label(setup_music m)
{
    static const char *const labels[SETUP_MUSIC_CHOICES] = {
            "No music",           "AdLib",
            "Sound Blaster",      "Sound Blaster Pro",
            "Roland MT-32 / LAPC-1", "Roland Sound Canvas / GS (SC-55)",
            "Pro-Audio Spectrum", "Wave Blaster",
            "Roland TAP-10",      "System MIDI"};
    return (unsigned)m < SETUP_MUSIC_CHOICES ? labels[m] : "";
}
int setup_settings_same(const setup_settings *a, const setup_settings *b)
{
    for (unsigned k = 0; k < SETUP_DIRS; ++k) {
        if (strcmp(a->dir[k], b->dir[k]) != 0) {
            return 0;
        }
    }
    for (unsigned i = 0; i < setup_addon_count; ++i) {
        if (!a->addon[i] != !b->addon[i]) {
            return 0;
        }
    }
    for (unsigned i = 0; i < setup_bugfix_count; ++i) {
        if (!a->bugfix[i] != !b->bugfix[i]) {
            return 0;
        }
    }
    for (unsigned i = 0; i < SETUP_WIZARD_STYLES; ++i) {
        if (a->wizard_style[i] != b->wizard_style[i]) {
            return 0;
        }
    }
    return !a->fullscreen == !b->fullscreen && !a->aspect == !b->aspect && !a->locked_scale == !b->locked_scale &&
           a->scale == b->scale && !a->system_midi == !b->system_midi;
}
const char *setup_music_badge(setup_music m)
{
    return m == SETUP_MUSIC_CANVAS ? "recommended" : NULL;
}

/* ------------------------------------------------------------------ small helpers */
/** Copies SRC into OUT (N > 0 bytes), truncating (a truncated copy keeps its NUL; callers size OUT for paths). */
static void copy_text(char *out, size_t n, const char *src)
{
    size_t len = src ? strlen(src) : 0u;
    if (len >= n) {
        len = n - 1u;
    }
    memcpy(out, src ? src : "", len);
    out[len] = 0;
}
/** A message (FMT ...) into OUT (N > 0 bytes); a long one is cut short (messages only, never paths). */
static void say(char *out, size_t n, const char *fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 3, 4)))
#endif
        ;
static void say(char *out, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (vsnprintf(out, n, fmt, ap) < 0) {
        out[0] = 0;
    }
    va_end(ap);
}
/** DIR, the platform's separator and NAME into OUT; 0 (OUT empty) when the path does not fit. */
static int join(char *out, size_t n, const char *dir, const char *name)
{
    int w = snprintf(out, n, "%s%c%s", dir, SETUP_SEPARATOR, name);
    if (w < 0 || (size_t)w >= n) {
        out[0] = 0;
        return 0;
    }
    return 1;
}
/** Formats an error message into ERR when there is room. */
static int set_error(char *err, size_t n, const char *what, const char *item)
{
    if (err && n) {
        say(err, n, "%s%s%s", what, item && *item ? ": " : "", item ? item : "");
    }
    return 0;
}
/** strdup in C11. */
static char *duplicate(const char *s, size_t len)
{
    char *d = malloc(len + 1u);
    if (d) {
        memcpy(d, s, len);
        d[len] = 0;
    }
    return d;
}
/** S without leading and trailing white space, as [*START, *START + *LEN). */
static void trimmed(const char *s, const char **start, size_t *len)
{
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1u])) {
        --n;
    }
    while (n && isspace((unsigned char)*s)) {
        ++s;
        --n;
    }
    *start = s;
    *len = n;
}
/** Whether key text A (LEN bytes) names KEY, exactly (Phase 336: wox's parser compares keys exactly). */
static int same_key(const char *a, size_t len, const char *key)
{
    return strlen(key) == len && !memcmp(a, key, len);
}

/* ------------------------------------------------------------------ the document */
void setup_doc_init(setup_doc *d)
{
    memset(d, 0, sizeof *d);
    d->newline = SETUP_NEWLINE;
}
void setup_doc_free(setup_doc *d)
{
    for (size_t i = 0; i < d->count; ++i) {
        free(d->lines[i]);
        free(d->ends[i]);
    }
    free(d->lines);
    free(d->ends);
    setup_doc_init(d);
}
/** Inserts the line TEXT (LEN bytes) with ending END at index AT; 1, or 0 out of memory. */
static int insert_line(setup_doc *d, size_t at, const char *text, size_t len, const char *end)
{
    char *line, *ending;
    if (d->count == d->cap) {
        size_t cap = d->cap ? d->cap * 2u : 64u;
        char **l = realloc(d->lines, cap * sizeof *l), **e;
        if (!l) {
            return 0;
        }
        d->lines = l;
        if (!(e = realloc(d->ends, cap * sizeof *e))) {
            return 0;
        }
        d->ends = e;
        d->cap = cap;
    }
    line = duplicate(text, len);
    ending = duplicate(end, strlen(end));
    if (!line || !ending) {
        free(line);
        free(ending);
        return 0;
    }
    memmove(d->lines + at + 1, d->lines + at, (d->count - at) * sizeof *d->lines);
    memmove(d->ends + at + 1, d->ends + at, (d->count - at) * sizeof *d->ends);
    d->lines[at] = line;
    d->ends[at] = ending;
    ++d->count;
    return 1;
}
int setup_doc_parse(setup_doc *d, const char *text)
{
    setup_doc_free(d);
    while (*text) {
        const char *nl = strchr(text, '\n');
        size_t len = nl ? (size_t)(nl - text) : strlen(text);
        const char *end = "";
        if (nl) {
            end = len && text[len - 1u] == '\r' ? "\r\n" : "\n";
            if (*end == '\r') {
                --len;
            }
        }
        if (!insert_line(d, d->count, text, len, end)) {
            return 0;
        }
        if (d->count == 1u && *end) {
            d->newline = *end == '\r' ? "\r\n" : "\n";
        }
        text += len + strlen(end);
    }
    return 1;
}
int setup_doc_load(setup_doc *d, const char *path, char *err, size_t n)
{
    FILE *f = fopen(path, "rb");
    char *text;
    long size;
    int ok;
    if (!f) {
        return set_error(err, n, strerror(errno), path);
    }
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) || size > 4l * 1024 * 1024) {
        fclose(f);
        return set_error(err, n, "cannot read", path);
    }
    if (!(text = malloc((size_t)size + 1u))) {
        fclose(f);
        return set_error(err, n, "out of memory", NULL);
    }
    ok = fread(text, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    text[size] = 0;
    if (!ok || memchr(text, 0, (size_t)size)) {
        free(text);
        return set_error(err, n, "cannot read", path);
    }
    ok = setup_doc_parse(d, text);
    free(text);
    return ok || set_error(err, n, "out of memory", NULL);
}
char *setup_doc_text(const setup_doc *d)
{
    size_t total = 1u, at = 0u;
    char *s;
    for (size_t i = 0; i < d->count; ++i) {
        total += strlen(d->lines[i]) + strlen(d->ends[i]);
    }
    if (!(s = malloc(total))) {
        return NULL;
    }
    for (size_t i = 0; i < d->count; ++i) {
        size_t a = strlen(d->lines[i]), b = strlen(d->ends[i]);
        memcpy(s + at, d->lines[i], a);
        memcpy(s + at + a, d->ends[i], b);
        at += a + b;
    }
    s[at] = 0;
    return s;
}
/** Line I's section header name into SECTION (1), or 0 when line I is not a section header. The first line's BOM is
 * skipped as wox's parser does. */
static int section_of(const setup_doc *d, size_t i, char *section, size_t n)
{
    const char *s;
    size_t len;
    trimmed(d->lines[i] + (i == 0u && !strncmp(d->lines[0], "\xef\xbb\xbf", 3) ? 3 : 0), &s, &len);
    if (len < 3u || *s != '[' || s[len - 1u] != ']') {
        return 0;
    }
    say(section, n, "%.*s", (int)(len - 2u), s + 1);
    return 1;
}
/** Whether line I is a setting (key = value): 1 with its key (*KEY, *KEY_LEN) and the offset of its '='. */
static int setting_of(const setup_doc *d, size_t i, const char **key, size_t *key_len, size_t *equals)
{
    const char *line = d->lines[i] + (i == 0u && !strncmp(d->lines[0], "\xef\xbb\xbf", 3) ? 3 : 0);
    const char *s, *eq;
    size_t len;
    trimmed(line, &s, &len);
    if (!len || *s == '#' || *s == ';' || *s == '[' || !(eq = memchr(s, '=', len))) {
        return 0;
    }
    *equals = (size_t)(eq - d->lines[i]);
    len = (size_t)(eq - s);
    while (len && isspace((unsigned char)s[len - 1u])) {
        --len;
    }
    *key = s;
    *key_len = len;
    return 1;
}
/** The index of KEY's line in [SECTION] (SIZE_MAX when absent); *SECTION_LINE gets the section's header (SIZE_MAX:
 * no such section) and *LAST its last setting line (the header when it has none). */
static size_t find_key(const setup_doc *d, const char *section, const char *key, size_t *section_line, size_t *last)
{
    char current[64] = "";
    size_t found = (size_t)-1;
    *section_line = *last = (size_t)-1;
    for (size_t i = 0; i < d->count; ++i) {
        const char *k;
        size_t klen, eq;
        if (section_of(d, i, current, sizeof current)) {
            if (!strcmp(current, section) && *section_line == (size_t)-1) {
                *section_line = *last = i;
            }
            continue;
        }
        if (strcmp(current, section) || !setting_of(d, i, &k, &klen, &eq)) {
            continue;
        }
        if (*section_line != (size_t)-1) {
            *last = i;
        }
        if (found == (size_t)-1 && same_key(k, klen, key)) {
            found = i;
        }
    }
    return found;
}
int setup_doc_get(const setup_doc *d, const char *section, const char *key, char *value, size_t n)
{
    size_t header, last, i = find_key(d, section, key, &header, &last), eq, klen, len;
    const char *k, *v;
    if (i == (size_t)-1 || !setting_of(d, i, &k, &klen, &eq)) {
        if (n) {
            *value = 0;
        }
        return 0;
    }
    trimmed(d->lines[i] + eq + 1, &v, &len);
    if (len >= 2u && (*v == '"' || *v == '\'') && v[len - 1u] == *v) {
        ++v;
        len -= 2u;
    }
    if (len >= n) {
        len = 0;
    }
    if (n) {
        memcpy(value, v, len);
        value[len] = 0;
    }
    return 1;
}
/** VALUE as wox's parser must read it back: quoted when it has white space or starts with a quote. 0 when it cannot
 * be written (a line break) or does not fit. */
static int quoted_value(const char *value, char *out, size_t n)
{
    int quote = 0;
    if (strchr(value, '\n') || strchr(value, '\r')) {
        return 0;
    }
    for (const char *p = value; *p; ++p) {
        quote |= isspace((unsigned char)*p) != 0;
    }
    quote |= *value == '"' || *value == '\'';
    return snprintf(out, n, quote ? "\"%s\"" : "%s", value) < (int)n;
}
int setup_doc_set(setup_doc *d, const char *section, const char *key, const char *value)
{
    char text[2100], line[2200];
    size_t header, last, i = find_key(d, section, key, &header, &last);
    if (!quoted_value(value, text, sizeof text) || strlen(text) + strlen(key) + 3u > SETUP_LINE_LIMIT) {
        return 0;
    }
    if (i != (size_t)-1) {
        size_t eq, klen, keep;
        const char *k;
        char *copy;
        (void)setting_of(d, i, &k, &klen, &eq);
        keep = eq + 1u;
        while (d->lines[i][keep] == ' ' || d->lines[i][keep] == '\t') {
            ++keep;
        }
        if (snprintf(line, sizeof line, "%.*s%s", (int)keep, d->lines[i], text) >= (int)sizeof line) {
            return 0; /* not reached: the length was checked above */
        }
        if (!(copy = duplicate(line, strlen(line)))) {
            return 0;
        }
        free(d->lines[i]);
        d->lines[i] = copy;
        return 1;
    }
    if (snprintf(line, sizeof line, "%s = %s", key, text) >= (int)sizeof line) {
        return 0; /* not reached: the length was checked above */
    }
    if (d->count && !*d->ends[d->count - 1u]) {
        char *end = duplicate(d->newline, strlen(d->newline));
        if (!end) {
            return 0;
        }
        free(d->ends[d->count - 1u]);
        d->ends[d->count - 1u] = end;
    }
    if (header == (size_t)-1) {
        char head[80];
        say(head, sizeof head, "[%s]", section);
        if (d->count && *d->lines[d->count - 1u] && !insert_line(d, d->count, "", 0, d->newline)) {
            return 0;
        }
        return insert_line(d, d->count, head, strlen(head), d->newline) &&
               insert_line(d, d->count, line, strlen(line), d->newline);
    }
    return insert_line(d, last + 1u, line, strlen(line), d->newline);
}
/** Writes LEN bytes of DATA to PATH through PATH.setup-tmp renamed over it. 1, or 0 with the reason in ERR. */
static int write_replacing(const char *path, const void *data, size_t len, char *err, size_t n)
{
    char tmp[SETUP_PATH_MAX + 16];
    FILE *f;
    int ok;
    if (snprintf(tmp, sizeof tmp, "%s.setup-tmp", path) >= (int)sizeof tmp) {
        return set_error(err, n, "path too long", path);
    }
    if (!(f = fopen(tmp, "wb"))) {
        return set_error(err, n, strerror(errno), tmp);
    }
    ok = fwrite(data, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    if (!ok) {
        remove(tmp);
        return set_error(err, n, "cannot write", tmp);
    }
#if defined(_WIN32)
    ok = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    ok = rename(tmp, path) == 0;
#endif
    if (!ok) {
        remove(tmp);
        return set_error(err, n, "cannot replace", path);
    }
    return 1;
}
/** Whether TEXT, written beside PATH, is a wox.cfg wox's parser accepts; the parser's message into ERR otherwise. */
static int wox_accepts(const char *path, const char *text, char *err, size_t n)
{
    char probe[SETUP_PATH_MAX + 16];
    xeen_wox_config *c = malloc(sizeof *c);
    FILE *f;
    int ok;
    if (!c) {
        return set_error(err, n, "out of memory", NULL);
    }
    if (snprintf(probe, sizeof probe, "%s.setup-check", path) >= (int)sizeof probe) {
        free(c);
        return set_error(err, n, "path too long", path);
    }
    if (!(f = fopen(probe, "wb"))) {
        free(c);
        return set_error(err, n, strerror(errno), probe);
    }
    ok = fputs(text, f) >= 0;
    ok = (fclose(f) == 0) && ok;
    ok = ok && xeen_wox_config_load(probe, c, err, n);
    remove(probe);
    free(c);
    return ok;
}
int setup_doc_save(const setup_doc *d, const char *path, char *err, size_t n)
{
    char *text = setup_doc_text(d);
    int ok;
    if (!text) {
        return set_error(err, n, "out of memory", NULL);
    }
    ok = wox_accepts(path, text, err, n) && write_replacing(path, text, strlen(text), err, n);
    free(text);
    return ok;
}
char *setup_default_wox_cfg(void)
{
    const char *t = xeen_wox_config_default_text();
    return duplicate(t, strlen(t));
}

/* ------------------------------------------------------------------ settings <-> document */
const char *setup_dir_key(setup_dir k)
{
    static const char *const keys[SETUP_DIRS] = {"xeen_cfg_dir",  "data_dir",      "save_dir",
                                                 "mt32_roms_dir", "sc55_roms_dir", "cd_tracks_dir"};
    return (unsigned)k < SETUP_DIRS ? keys[k] : "";
}
const char *setup_dir_default(setup_dir k)
{
#if defined(_WIN32)
    static const char *const shown[SETUP_DIRS] = {".", ".", ".", "roms\\mt32", "roms\\sc55", "music"};
#else
    static const char *const shown[SETUP_DIRS] = {".", ".", ".", "roms/mt32", "roms/sc55", "music"};
#endif
    return (unsigned)k < SETUP_DIRS ? shown[k] : "";
}
const char *setup_gog_dir(void)
{
#if defined(_WIN32)
    return "cloud_saves\\WORLD";
#else
    return "cloud_saves/WORLD";
#endif
}
/** wox's switch reading (Phase 336: every boolean is on/off): on is 1; anything else 0 (off, auto and empty as wox
 * reads them; any other value wox refuses, and the file is checked with its parser before it is saved); a missing
 * key: FALLBACK. */
static int read_flag(const setup_doc *d, const char *section, const char *key, int fallback)
{
    char v[32];
    if (!setup_doc_get(d, section, key, v, sizeof v)) {
        return fallback;
    }
    if (!strcmp(v, "on")) {
        return 1;
    }
    return 0;
}
void setup_settings_from_doc(const setup_doc *d, setup_settings *s)
{
    char v[SETUP_PATH_MAX];
    memset(s, 0, sizeof *s);
    for (unsigned k = SETUP_DIR_DATA; k < SETUP_DIRS; ++k) {
        if (!setup_doc_get(d, "files", setup_dir_key((setup_dir)k), v, sizeof v) || !*v || !strcmp(v, "auto")) {
            copy_text(s->dir[k], sizeof s->dir[k], setup_dir_default((setup_dir)k));
        } else {
            copy_text(s->dir[k], sizeof s->dir[k], v);
        }
    }
    if (!setup_doc_get(d, "files", "xeen_cfg_dir", v, sizeof v) || !*v || !strcmp(v, "auto")) {
        copy_text(s->dir[SETUP_DIR_XEEN_CFG], sizeof s->dir[0], s->dir[SETUP_DIR_DATA]);
    } else {
        copy_text(s->dir[SETUP_DIR_XEEN_CFG], sizeof s->dir[0], v);
    }
    s->fullscreen = read_flag(d, "display", "fullscreen", 0);
    s->aspect = read_flag(d, "display", "aspect", 0);
    s->locked_scale = read_flag(d, "display", "locked_scale", 0);
    s->scale = 3u;
    if (setup_doc_get(d, "display", "scale", v, sizeof v)) {
        char *end;
        unsigned long n = v[0] >= '1' && v[0] <= '9' ? strtoul(v, &end, 10) : 0ul;
        if (n && !*end && n <= XEEN_WOX_SCALE_MAX) {
            s->scale = (unsigned)n;
        }
    }
    for (unsigned i = 0; i < setup_addon_count; ++i) {
        s->addon[i] = read_flag(d, "addons", setup_addons[i].key, 0);
    }
    for (unsigned i = 0; i < SETUP_WIZARD_STYLES; ++i) {
        s->wizard_style[i] = SETUP_STYLE_BLACK;
        if (setup_doc_get(d, "addons", setup_wizard_style_keys[i], v, sizeof v)) {
            for (unsigned k = 0; k < SETUP_WIZARD_STYLES; ++k) {
                if (!strcmp(v, setup_wizard_style_names[k])) {
                    s->wizard_style[i] = k;
                }
            }
        }
    }
    for (unsigned i = 0; i < setup_bugfix_count; ++i) {
        s->bugfix[i] = read_flag(d, "bugfix", setup_bugfixes[i].key, 0);
    }
    /* A missing music_override key is wox's default, system. */
    s->system_midi = !setup_doc_get(d, "audio", "music_override", v, sizeof v) || !*v || !strcmp(v, "system");
}
/** Sets KEY in [SECTION] to VALUE unless the document's value already reads the same. MISSING_IS_AUTO: a missing or
 * empty value means auto for this key (every [files] directory and speech; not music_override, whose default is
 * system). */
static int set_if_changed(setup_doc *d, const char *section, const char *key, const char *value, int missing_is_auto)
{
    char v[SETUP_PATH_MAX];
    int present = setup_doc_get(d, section, key, v, sizeof v);
    if (present && !strcmp(v, value)) {
        return 1;
    }
    if (missing_is_auto && !strcmp(value, "auto") && (!present || !*v)) {
        return 1;
    }
    return setup_doc_set(d, section, key, value);
}
/** Whether directory texts A and B are equal, on Windows a slash and a backslash alike (roms/mt32 = roms\mt32). */
static int same_dir_text(const char *a, const char *b)
{
#if defined(_WIN32)
    for (; *a && *b; ++a, ++b) {
        if (*a != *b && !((*a == '/' || *a == '\\') && (*b == '/' || *b == '\\'))) {
            return 0;
        }
    }
    return *a == *b;
#else
    return strcmp(a, b) == 0;
#endif
}
/** Directory K's value for wox.cfg: auto for its default text (XEEN.CFG: the data directory's text), "./auto" for a
 * directory literally named auto, else the text. */
static const char *dir_value(const setup_settings *s, setup_dir k, char *buf, size_t n)
{
    const char *text = s->dir[k];
    const char *deflt = k == SETUP_DIR_XEEN_CFG ? s->dir[SETUP_DIR_DATA] : setup_dir_default(k);
    if (!*text || same_dir_text(text, deflt)) {
        return "auto";
    }
    if (!strcmp(text, "auto")) {
        say(buf, n, ".%cauto", SETUP_SEPARATOR);
        return buf;
    }
    return text;
}
int setup_settings_to_doc(const setup_settings *s, setup_doc *d)
{
    setup_settings old;
    char buf[64];
    int ok = 1;
    setup_settings_from_doc(d, &old);
    for (unsigned k = 0; k < SETUP_DIRS; ++k) {
        int changed = strcmp(s->dir[k], old.dir[k]) != 0;
        if (k == SETUP_DIR_XEEN_CFG) {
            /* auto follows the data directory: a changed data text changes what an unchanged auto means */
            changed |= strcmp(s->dir[SETUP_DIR_DATA], old.dir[SETUP_DIR_DATA]) != 0;
        }
        if (changed) {
            char lit[16];
            ok &= set_if_changed(d, "files", setup_dir_key((setup_dir)k), dir_value(s, (setup_dir)k, lit, sizeof lit),
                                  1);
        }
    }
    if (s->fullscreen != old.fullscreen) {
        ok &= setup_doc_set(d, "display", "fullscreen", s->fullscreen ? "on" : "off");
    }
    if (s->scale != old.scale) {
        say(buf, sizeof buf, "%u", s->scale);
        ok &= setup_doc_set(d, "display", "scale", buf);
    }
    if (s->aspect != old.aspect) {
        ok &= setup_doc_set(d, "display", "aspect", s->aspect ? "on" : "off");
    }
    if (s->locked_scale != old.locked_scale) {
        ok &= setup_doc_set(d, "display", "locked_scale", s->locked_scale ? "on" : "off");
    }
    for (unsigned i = 0; i < setup_addon_count; ++i) {
        if (s->addon[i] != old.addon[i]) {
            ok &= setup_doc_set(d, "addons", setup_addons[i].key, s->addon[i] ? "on" : "off");
        }
    }
    for (unsigned i = 0; i < SETUP_WIZARD_STYLES; ++i) {
        if (s->wizard_style[i] != old.wizard_style[i]) {
            ok &= setup_doc_set(d, "addons", setup_wizard_style_keys[i], setup_wizard_style_names[s->wizard_style[i]]);
        }
    }
    for (unsigned i = 0; i < setup_bugfix_count; ++i) {
        if (s->bugfix[i] != old.bugfix[i]) {
            ok &= setup_doc_set(d, "bugfix", setup_bugfixes[i].key, s->bugfix[i] ? "on" : "off");
        }
    }
    ok &= set_if_changed(d, "audio", "music_override", s->system_midi ? "system" : "auto", 0);
    ok &= set_if_changed(d, "audio", "speech", "auto", 1);
    return ok;
}
void setup_apply_version(setup_settings *s, int gog)
{
    if (gog) {
        copy_text(s->dir[SETUP_DIR_SAVE], sizeof s->dir[0], setup_gog_dir());
        copy_text(s->dir[SETUP_DIR_XEEN_CFG], sizeof s->dir[0], setup_gog_dir());
    }
    s->system_midi = 0; /* the Music list follows XEEN.CFG (music_override = auto) */
}
void setup_apply_style(setup_settings *s, int enhanced)
{
    for (unsigned i = 0; i < setup_addon_count; ++i) {
        s->addon[i] = enhanced && (int)i != setup_addon_index("damage_numbers.stack");
    }
    for (unsigned i = 0; i < setup_bugfix_count; ++i) {
        s->bugfix[i] = enhanced;
    }
    if (enhanced) {
        for (unsigned i = 0; i < SETUP_WIZARD_STYLES; ++i) {
            s->wizard_style[i] = SETUP_STYLE_BLACK;
        }
    }
}

/* ------------------------------------------------------------------ paths */
/** Whether P is absolute (on Windows also a drive or a backslash path). */
static int is_absolute(const char *p)
{
#if defined(_WIN32)
    return p[0] == '\\' || p[0] == '/' || (p[0] && p[1] == ':');
#else
    return p[0] == '/';
#endif
}
static int is_separator(char c)
{
#if defined(_WIN32)
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}
void setup_dirname(const char *path, char *out, size_t n)
{
    size_t len = strlen(path);
    while (len && !is_separator(path[len - 1u])) {
        --len;
    }
    if (!len) {
        copy_text(out, n, ".");
        return;
    }
    if (len > 1u && !(len == 3u && path[1] == ':')) {
        --len; /* the separator, but not the root's */
    }
    say(out, n, "%.*s", (int)len, path);
}
void setup_resolve(const char *base, const char *dir, char *out, size_t n)
{
    if (is_absolute(dir) || !*base) {
        copy_text(out, n, dir);
    } else if (!strcmp(dir, ".")) {
        copy_text(out, n, base);
    } else {
        (void)join(out, n, base, dir); /* a path that does not fit resolves to "": no such directory */
    }
}
/** The canonical absolute form of existing directory P into OUT; 0 when it cannot be resolved. */
static int canonical(const char *p, char *out, size_t n)
{
#if defined(_WIN32)
    DWORD r = GetFullPathNameA(p, (DWORD)n, out, NULL);
    if (!r || r >= n) {
        return 0;
    }
    for (char *c = out; *c; ++c) {
        if (*c == '/') {
            *c = '\\';
        }
    }
    r = (DWORD)strlen(out);
    while (r > 3u && out[r - 1u] == '\\') {
        out[--r] = 0;
    }
    return 1;
#else
    char buf[PATH_MAX];
    if (!realpath(p, buf)) {
        return 0;
    }
    copy_text(out, n, buf);
    return strlen(buf) < n;
#endif
}
/** Whether the N-byte prefixes of A and B name the same path (case-insensitively on Windows). */
static int same_prefix(const char *a, const char *b, size_t n)
{
#if defined(_WIN32)
    return _strnicmp(a, b, n) == 0;
#else
    return strncmp(a, b, n) == 0;
#endif
}
void setup_relative_to(const char *base, const char *chosen, char *out, size_t n)
{
    char b[SETUP_PATH_MAX], c[SETUP_PATH_MAX];
    size_t len;
    if (!canonical(base, b, sizeof b) || !canonical(chosen, c, sizeof c)) {
        copy_text(out, n, chosen);
        return;
    }
    len = strlen(b);
    if (strlen(c) == len && same_prefix(b, c, len)) {
        copy_text(out, n, ".");
    } else if (strlen(c) > len && same_prefix(b, c, len) && (is_separator(c[len]) || is_separator(b[len - 1u]))) {
        copy_text(out, n, c + len + (is_separator(c[len]) ? 1u : 0u));
    } else {
        copy_text(out, n, c);
    }
}
/** Whether P is an existing directory. */
static int is_directory(const char *p)
{
    struct stat st;
    return !stat(p, &st) && (st.st_mode & S_IFMT) == S_IFDIR;
}
int setup_dir_exists(const char *base, const char *dir)
{
    char p[SETUP_PATH_MAX];
    setup_resolve(base, dir, p, sizeof p);
    return is_directory(p);
}
/** Creates the one directory P; 1 when it now exists. */
static int make_one(const char *p)
{
#if defined(_WIN32)
    return _mkdir(p) == 0 || is_directory(p);
#else
    return mkdir(p, 0777) == 0 || is_directory(p);
#endif
}
int setup_make_dirs(const char *base, const char *dir)
{
    char p[SETUP_PATH_MAX];
    size_t len, start = 1u;
    setup_resolve(base, dir, p, sizeof p);
    len = strlen(p);
#if defined(_WIN32)
    if (len > 2u && p[1] == ':') {
        start = 3u;
    }
#endif
    for (size_t i = start; i < len; ++i) {
        if (is_separator(p[i])) {
            char keep = p[i];
            p[i] = 0;
            if (!make_one(p)) {
                return 0;
            }
            p[i] = keep;
        }
    }
    return make_one(p);
}
int setup_has_gog_image(const char *exe_dir)
{
#if defined(_WIN32)
    char p[SETUP_PATH_MAX];
    return join(p, sizeof p, exe_dir, "GAME1.GOG") && GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
#else
    DIR *dir = opendir(exe_dir);
    struct dirent *e;
    int found = 0;
    if (!dir) {
        return 0;
    }
    while (!found && (e = readdir(dir)) != NULL) {
        const char *want = "GAME1.GOG";
        size_t i;
        for (i = 0; want[i] && toupper((unsigned char)e->d_name[i]) == want[i]; ++i) {
        }
        found = !want[i] && !e->d_name[i];
    }
    closedir(dir);
    return found;
#endif
}
void setup_exe_dir(const char *argv0, char *out, size_t n)
{
    char cfg[SETUP_PATH_MAX];
    if (!xeen_wox_config_locate(NULL, argv0, cfg, sizeof cfg)) {
        copy_text(out, n, ".");
        return;
    }
    setup_dirname(cfg, out, n);
}

/* ------------------------------------------------------------------ XEEN.CFG */
/** The little-endian word at OFFSET of CFG. */
static unsigned word_at(const setup_xeen_cfg *c, size_t offset)
{
    return (unsigned)c->bytes[offset] | (unsigned)c->bytes[offset + 1u] << 8;
}
static void put_word(uint8_t *b, size_t offset, unsigned v)
{
    b[offset] = (uint8_t)(v & 0xffu);
    b[offset + 1u] = (uint8_t)(v >> 8 & 0xffu);
}
int setup_xeen_cfg_find(const char *dir, char *path, size_t n)
{
    static const char *const names[2] = {"XEEN.CFG", "xeen.cfg"};
    for (unsigned i = 0; i < 2u; ++i) {
        FILE *f;
        if (join(path, n, dir, names[i]) && (f = fopen(path, "rb")) != NULL) {
            fclose(f);
            return 1;
        }
    }
    return 0;
}
int setup_xeen_cfg_read(const char *dir, setup_xeen_cfg *c, char *err, size_t n)
{
    FILE *f;
    uint8_t extra;
    memset(c, 0, sizeof *c);
    if (n) {
        *err = 0;
    }
    if (!setup_xeen_cfg_find(dir, c->path, sizeof c->path) || !(f = fopen(c->path, "rb"))) {
        return 0;
    }
    c->size = fread(c->bytes, 1, sizeof c->bytes, f);
    if (c->size == sizeof c->bytes && fread(&extra, 1, 1, f) == 1u) {
        c->size = sizeof c->bytes + 1u;
    }
    fclose(f);
    /* Phase 335: 1..30 bytes, as the game accepts (SETUP_XEEN_CFG_WRITE_MIN); the note names no path, since the
     * windows show its folder beside it */
    if (!c->size) {
        return set_error(err, n, "This XEEN.CFG is empty.", NULL);
    }
    if (c->size > SETUP_XEEN_CFG_MAX) {
        return set_error(err, n, "This XEEN.CFG is too long (more than 30 bytes).", NULL);
    }
    return 1;
}
void setup_xeen_cfg_default(uint8_t out[SETUP_XEEN_CFG_SIZE])
{
    /* emulator/XEENGAME/XEEN.CFG (a fresh installation), byte for byte, then the approved changes below. */
    static const uint8_t fresh[SETUP_XEEN_CFG_SIZE] = {0x0a, 0x00, 0x20, 0x02, 0x04, 0x00, 0x30, 0x03, 0x07, 0x00,
                                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                                                       0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03};
    memcpy(out, fresh, sizeof fresh);
    put_word(out, SETUP_CFG_SOUND, SETUP_SOUND_SBPRO);
    put_word(out, SETUP_CFG_SOUND_PORT, SETUP_PORT_SBPRO);
    put_word(out, SETUP_CFG_MUSIC, SETUP_MUSIC_WORD_SBPRO);
    put_word(out, SETUP_CFG_MUSIC_PORT, SETUP_PORT_SBPRO); /* the installer's SB Pro music default */
    put_word(out, SETUP_CFG_IRQ, 7u);
    memset(out + SETUP_CFG_CLOUDS_ENDING, 0, 8u); /* no ending seen */
    out[SETUP_CFG_DARKSIDE_ENDING] = out[SETUP_CFG_WORLD_ENDING] = 0;
    put_word(out, SETUP_CFG_DMA, 1u);
    put_word(out, SETUP_CFG_INTRO_PLAYED, 0u);
}
int setup_xeen_cfg_create(const char *dir, char *err, size_t n)
{
    char path[SETUP_PATH_MAX];
    uint8_t bytes[SETUP_XEEN_CFG_SIZE];
    FILE *f;
    if (setup_xeen_cfg_find(dir, path, sizeof path)) {
        return set_error(err, n, "already exists", path);
    }
    if (!join(path, sizeof path, dir, "XEEN.CFG")) {
        return set_error(err, n, "path too long", dir);
    }
    setup_xeen_cfg_default(bytes);
    if (!(f = fopen(path, "wb"))) {
        return set_error(err, n, strerror(errno), path);
    }
    if (fwrite(bytes, 1, sizeof bytes, f) != sizeof bytes) {
        fclose(f);
        remove(path);
        return set_error(err, n, "cannot write", path);
    }
    if (fclose(f) != 0) {
        remove(path);
        return set_error(err, n, "cannot write", path);
    }
    return 1;
}
int setup_xeen_cfg_speech(const setup_xeen_cfg *c)
{
    /* XEEN.DAT's driver table (15E52): 0 and past 10 nullsnd, 1 the PC speaker, 3 and 4 nullsnd; the rest speak. */
    unsigned w = word_at(c, SETUP_CFG_SOUND);
    return w >= 2u && w <= 10u && w != 3u && w != 4u;
}
setup_music setup_xeen_cfg_music(const setup_xeen_cfg *c)
{
    switch (word_at(c, SETUP_CFG_MUSIC)) { /* XEEN.DAT 15EC9; 7 ibmmus (no native driver), 0 and past 9 nullmus */
        case 1: return SETUP_MUSIC_ADLIB;
        case 2: return SETUP_MUSIC_SB;
        case 3: return SETUP_MUSIC_SBPRO;
        case 4: return SETUP_MUSIC_MT32;
        case 5: return SETUP_MUSIC_CANVAS;
        case 6: return SETUP_MUSIC_PAS;
        case 8: return SETUP_MUSIC_WAVE;
        case 9: return SETUP_MUSIC_TAP10;
        default: return SETUP_MUSIC_NONE;
    }
}
int setup_music_word(setup_music m, unsigned *word, unsigned *port)
{
    /* The installer's autodetection defaults (Phase 331 install/FINDINGS.md): AdLib 388h (095A3), Sound Blaster 388h
     * and Sound Blaster Pro 220h and Wave Blaster 330h (09080..09092), PAS 330h (09471), Canvas and MT-32 330h
     * (093F8/093FD); TAP-10 has none (the owner chose 340h, the installer's help example). */
    static const unsigned table[SETUP_MUSIC_CHOICES][2] = {
            {0u, 0u},    {1u, 0x388u}, {2u, 0x388u}, {3u, 0x220u}, {4u, 0x330u},
            {5u, 0x330u}, {6u, 0x330u}, {8u, 0x330u}, {9u, 0x340u}, {0u, 0u}};
    if ((unsigned)m >= SETUP_MUSIC_SYSTEM) {
        return 0;
    }
    *word = table[m][0];
    *port = table[m][1];
    return 1;
}
void setup_xeen_cfg_apply(setup_xeen_cfg *c, int speech_on, setup_music music)
{
    unsigned word, port;
    int was_on = setup_xeen_cfg_speech(c);
    if (!speech_on && was_on) {
        put_word(c->bytes, SETUP_CFG_SOUND, SETUP_SOUND_NONE);
    } else if (speech_on && !was_on) {
        put_word(c->bytes, SETUP_CFG_SOUND, SETUP_SOUND_SBPRO);
        put_word(c->bytes, SETUP_CFG_SOUND_PORT, SETUP_PORT_SBPRO);
    }
    if (music != setup_xeen_cfg_music(c) && setup_music_word(music, &word, &port)) {
        put_word(c->bytes, SETUP_CFG_MUSIC, word);
        if (port) {
            put_word(c->bytes, SETUP_CFG_MUSIC_PORT, port);
        }
    }
}
int setup_xeen_cfg_write(const setup_xeen_cfg *c, char *err, size_t n)
{
    if (!c->size || c->size > SETUP_XEEN_CFG_MAX) {
        return set_error(err, n, "not a valid XEEN.CFG", c->path);
    }
    /* its own length, but at least through the music port (the bytes after the file's end were 0 in the image) */
    return write_replacing(c->path, c->bytes, c->size < SETUP_XEEN_CFG_WRITE_MIN ? SETUP_XEEN_CFG_WRITE_MIN : c->size,
                           err, n);
}
/** Whether file NAME in directory DIR can be opened for reading. */
static int readable_in(const char *dir, const char *name)
{
    char p[SETUP_PATH_MAX];
    FILE *f;
    if (!join(p, sizeof p, dir, name) || (f = fopen(p, "rb")) == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}
/** Setup's own wording of the ROMs C's music system lacks (Phase 333, the owner's text; wox's check decided that they
 * are missing): the missing directory or MT-32 file in parentheses, then which synth plays the system. */
static void rom_message(const xeen_wox_config *c, char *msg, size_t n)
{
    static const char tail[] = "The game will not start with this music until the ROMs are there.";
    static const char *const mt32_files[] = {"MT32_CONTROL.ROM", "MT32_PCM.ROM"};
    const char *name = xeen_music_system_name(c->system);
    if (c->system != XEEN_MUSIC_MT32) {
        say(msg, n, "SC-55 ROM directory (%s) not found - %s plays on Nuked-SC55. %s", c->sc55_roms, name, tail);
        return;
    }
    if (!is_directory(c->mt32_roms)) {
        say(msg, n, "MT-32 ROM directory (%s) not found - %s plays on Munt. %s", c->mt32_roms, name, tail);
        return;
    }
    for (unsigned i = 0; i < 2u; ++i) {
        char p[SETUP_PATH_MAX];
        if (!readable_in(c->mt32_roms, mt32_files[i])) {
            if (!join(p, sizeof p, c->mt32_roms, mt32_files[i])) {
                copy_text(p, sizeof p, mt32_files[i]);
            }
            say(msg, n, "MT-32 ROM (%s) not found - %s plays on Munt. %s", p, name, tail);
            return;
        }
    }
    say(msg, n, "MT-32 ROMs (%s) not readable - %s plays on Munt. %s", c->mt32_roms, name, tail);
}
int setup_rom_warning(const setup_settings *s, setup_music m, const char *base, char *msg, size_t n)
{
    xeen_wox_config c;
    char why[1400];
    xeen_wox_config_defaults(&c, base);
    switch (m) {
        case SETUP_MUSIC_MT32: c.system = XEEN_MUSIC_MT32; break;
        case SETUP_MUSIC_CANVAS: c.system = XEEN_MUSIC_SC55; break;
        case SETUP_MUSIC_WAVE: c.system = XEEN_MUSIC_WAVE; break;
        case SETUP_MUSIC_TAP10: c.system = XEEN_MUSIC_TAP10; break;
        default: return 0;
    }
    setup_resolve(base, s->dir[SETUP_DIR_MT32], c.mt32_roms, sizeof c.mt32_roms);
    setup_resolve(base, s->dir[SETUP_DIR_SC55], c.sc55_roms, sizeof c.sc55_roms);
    if (xeen_wox_config_check_roms(&c, why, sizeof why)) {
        return 0;
    }
    rom_message(&c, msg, n);
    return 1;
}

/* ------------------------------------------------------------------ the windows' two operations */
int setup_first_run(const char *wox_path, int gog, int enhanced, char *err, size_t n)
{
    setup_doc doc;
    setup_settings s;
    char *text = setup_default_wox_cfg(), base[SETUP_PATH_MAX], cfg_dir[SETUP_PATH_MAX], path[SETUP_PATH_MAX];
    int ok;
    setup_doc_init(&doc);
    if (!text || !setup_doc_parse(&doc, text)) {
        free(text);
        return set_error(err, n, "out of memory", NULL);
    }
    free(text);
    setup_dirname(wox_path, base, sizeof base);
    setup_settings_from_doc(&doc, &s);
    setup_apply_version(&s, gog);
    setup_apply_style(&s, enhanced);
    if (gog) {
        setup_resolve(base, setup_gog_dir(), cfg_dir, sizeof cfg_dir);
        if (!setup_make_dirs(base, setup_gog_dir())) {
            setup_doc_free(&doc);
            return set_error(err, n, "cannot create the folder", cfg_dir);
        }
        if (!setup_xeen_cfg_find(cfg_dir, path, sizeof path) && !setup_xeen_cfg_create(cfg_dir, err, n)) {
            setup_doc_free(&doc);
            return 0;
        }
    }
    ok = setup_settings_to_doc(&s, &doc) || set_error(err, n, "cannot write the settings", NULL);
    ok = ok && setup_doc_save(&doc, wox_path, err, n);
    setup_doc_free(&doc);
    return ok;
}
int setup_save(const char *wox_path, setup_doc *doc, const setup_settings *s, setup_xeen_cfg *cfg, int speech_on,
               setup_music music, char *err, size_t n)
{
    if (!setup_settings_to_doc(s, doc)) {
        return set_error(err, n, "a value cannot be written to wox.cfg", NULL);
    }
    if (!setup_doc_save(doc, wox_path, err, n)) {
        return 0;
    }
    if (cfg && cfg->size && cfg->size <= SETUP_XEEN_CFG_MAX) {
        setup_xeen_cfg tmp = *cfg;
        setup_xeen_cfg_apply(&tmp, speech_on, music);
        if (memcmp(tmp.bytes, cfg->bytes, sizeof tmp.bytes) != 0) {
            if (!setup_xeen_cfg_write(&tmp, err, n)) {
                return 0;
            }
            *cfg = tmp;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ the windows' decisions */
setup_cfg_state setup_xeen_cfg_locate(const char *base, const char *text, setup_xeen_cfg *cfg, char *dir, size_t dn,
                                      char *note, size_t nn)
{
    setup_resolve(base, text, dir, dn);
    if (setup_xeen_cfg_read(dir, cfg, note, nn)) {
        return SETUP_CFG_PROPER;
    }
    if (*note) {
        return SETUP_CFG_INVALID;
    }
    if (!is_directory(dir)) {
        copy_text(note, nn, "This folder does not exist.");
        return SETUP_CFG_NO_FOLDER;
    }
    copy_text(note, nn, "No XEEN.CFG in this folder.");
    return SETUP_CFG_ABSENT;
}
void setup_create_question(const char *dir, char *q, size_t n)
{
    say(q, n,
        "Create a default XEEN.CFG?\n\nThe folder %s has no XEEN.CFG. The default: speech and music Sound "
             "Blaster Pro, port 220, IRQ 7, DMA 1; no ending seen, intro not played.",
             dir);
}
void setup_replace_question(const char *dir, const char *reason, char *q, size_t n)
{
    say(q, n,
        "Replace XEEN.CFG with the default?\n\n%s The game cannot use the XEEN.CFG in %s. The default: speech "
        "and music Sound Blaster Pro, port 220, IRQ 7, DMA 1; no ending seen, intro not played.",
        reason, dir);
}
int setup_xeen_cfg_replace(const char *dir, char *err, size_t n)
{
    char path[SETUP_PATH_MAX];
    uint8_t bytes[SETUP_XEEN_CFG_SIZE];
    if (!setup_xeen_cfg_find(dir, path, sizeof path)) {
        return setup_xeen_cfg_create(dir, err, n);
    }
    setup_xeen_cfg_default(bytes);
    return write_replacing(path, bytes, sizeof bytes, err, n);
}
int setup_missing_folder(const setup_settings *s, const char *base, setup_dir *which, char *q, size_t n)
{
    static const setup_dir needed[2] = {SETUP_DIR_SAVE, SETUP_DIR_XEEN_CFG};
    for (unsigned i = 0; i < 2u; ++i) {
        const char *text = s->dir[needed[i]];
        if (*text && !setup_dir_exists(base, text)) {
            *which = needed[i];
            say(q, n, "Create the %s folder?\n\n%s does not exist; the game will not start without it.",
                     needed[i] == SETUP_DIR_SAVE ? "save" : "XEEN.CFG", text);
            return 1;
        }
    }
    return 0;
}
int setup_missing_data(const setup_settings *s, const char *base, char *q, size_t n)
{
    const char *text = s->dir[SETUP_DIR_DATA];
    if (!*text || setup_dir_exists(base, text)) {
        return 0;
    }
    say(q, n, "Save anyway?\n\nThe data folder %s does not exist.", text);
    return 1;
}
