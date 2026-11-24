#include "xeen/input_poll.h"
#include "xeen/byte_order.h"
#include "xeen/ds_globals.h"
#include <stdlib.h>
#include <string.h>
static const char punctuation[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
int xeen_input_punctuation_key(unsigned c, xeen_native_key *key)
{
    const char *at = c ? strchr(punctuation, (int)c) : NULL;
    if (!at || !key) {
        return 0;
    }
    *key = (xeen_native_key)((unsigned)XKEY_PUNCT_FIRST + (unsigned)(at - punctuation));
    return 1;
}
/* The original keyboard driver's tables (code segment 4853h, XEEN.DAT): CS:7AF8 the character of each scan code
 * 00h..7Fh that 4FEFF queues (0: none), CS:7AC8 the 22 scan codes that have a shifted character and CS:7AE0 those
 * characters (entry 0 of both unused). */
static const uint8_t driver_scan_character[0x80] = {
        0x00, 0x1b, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x2d, 0x3d, 0x08, 0x09,
        0x51, 0x57, 0x45, 0x52, 0x54, 0x59, 0x55, 0x49, 0x4f, 0x50, 0x5b, 0x5d, 0x0d, 0x00, 0x41, 0x53,
        0x44, 0x46, 0x47, 0x48, 0x4a, 0x4b, 0x4c, 0x3b, 0x27, 0x60, 0x00, 0x5c, 0x5a, 0x58, 0x43, 0x56,
        0x42, 0x4e, 0x4d, 0x2c, 0x2e, 0x2f, 0x00, 0x2a, 0x00, 0x20, 0xfc, 0xc9, 0xca, 0xcb, 0xcc, 0xcd,
        0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xfb, 0xfa, 0xdc, 0xf2, 0xde, 0x2d, 0xf0, 0x35, 0xf1, 0x2b, 0xdd,
        0xf3, 0xdf, 0xe0};
static const uint8_t driver_shift_scan[0x17] = {0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
                                                0x0d, 0x29, 0x2b, 0x1a, 0x1b, 0x27, 0x28, 0x33, 0x34, 0x35, 0x4c};
static const uint8_t driver_shifted[0x17] = {0x00, 0x21, 0x40, 0x23, 0x24, 0x25, 0x5e, 0x26, 0x2a, 0x28, 0x29, 0x5f,
                                             0x2b, 0x7e, 0x7c, 0x7b, 0x7d, 0x3a, 0x22, 0x3c, 0x3e, 0x3f, 0x25};
unsigned xeen_input_shift_character(unsigned c)
{
    unsigned scan, i;
    if (!c || c > 0xffu) {
        return c;
    }
    /* 4FE4E: the scan code whose character it is, searched from 7Fh down (the highest one wins: '-' is keypad 4Ah,
     * which has no shifted character, so Shift+'-' stays '-'; '5' is keypad 4Ch, shifted '%' like scan 06h) */
    for (scan = 0x7fu; scan >= 1u; --scan) {
        if (driver_scan_character[scan] == c) {
            break;
        }
    }
    if (!scan) {
        return c; /* 4FE5E: none (the original then zeroes it: no such character reaches here) */
    }
    for (i = 0x16u; i >= 1u; --i) { /* 4FE68 */
        if (driver_shift_scan[i] == scan) {
            return driver_shifted[i];
        }
    }
    return c;
}
int xeen_input_reader_character(unsigned c)
{
    unsigned scan;
    if (c < 0x20u || c > 0x7eu) {
        return 0;
    }
    for (scan = 1u; scan < 0x80u; ++scan) {
        unsigned plain = driver_scan_character[scan];
        if (plain == c || (plain >= 0x20u && plain <= 0x7eu && xeen_input_shift_character(plain) == c)) {
            return 1;
        }
    }
    return 0;
}
int xeen_input_character_key(unsigned c, xeen_native_key *key)
{
    static const xeen_native_key letters[26] = {
            XKEY_A, XKEY_B, XKEY_C, XKEY_D, XKEY_E, XKEY_F, XKEY_G, XKEY_H, XKEY_I,
            XKEY_J, XKEY_K, XKEY_L, XKEY_M, XKEY_N, XKEY_O, XKEY_P, XKEY_Q, XKEY_R,
            XKEY_S, XKEY_T, XKEY_U, XKEY_V, XKEY_W, XKEY_X, XKEY_Y, XKEY_Z};
    static const xeen_native_key digits[10] = {XKEY_0, XKEY_1, XKEY_2, XKEY_3, XKEY_4,
                                               XKEY_5, XKEY_6, XKEY_7, XKEY_8, XKEY_9};
    if (!key) {
        return 0;
    }
    if (c >= 'a' && c <= 'z') {
        c -= 32u;
    }
    if (c >= 'A' && c <= 'Z') {
        *key = letters[c - 'A'];
        return 1;
    }
    if (c >= '0' && c <= '9') {
        *key = digits[c - '0'];
        return 1;
    }
    if (c == ' ') {
        *key = XKEY_SPACE;
        return 1;
    }
    return xeen_input_punctuation_key(c, key);
}
int xeen_input_text_key(unsigned unshifted, unsigned produced, int shift, xeen_native_key *key)
{
    unsigned c = produced;
    if (!key) {
        return 0;
    }
    if (!shift) {
        c = unshifted;
    }
    if (!xeen_input_reader_character(c)) {
        /* Not a character the original's reader returns: with Shift, its own rule on the key's plain character */
        c = shift && xeen_input_reader_character(unshifted) ? xeen_input_shift_character(unshifted) : 0u;
    }
    return xeen_input_character_key(c, key);
}
/**
 * Translates a native key and its modifiers to the key code the original's keyboard poll returns (0
 * for a key it has no code for).
 */
uint16_t xeen_input_translate_key(xeen_native_key key, uint8_t modifiers)
{
    /* The original's code for each native key up to F8 (the order of xeen_native_key). */
    static const uint8_t commands[] = {
            [XKEY_UP] = 0xf2, /* the arrows: F2h up, F1h right, F3h down, F0h left */
            [XKEY_RIGHT] = 0xf1,
            [XKEY_DOWN] = 0xf3,
            [XKEY_LEFT] = 0xf0,
            [XKEY_ENTER] = 13, /* Enter, Esc, Space */
            [XKEY_ESCAPE] = 27,
            [XKEY_SPACE] = 32,
            [XKEY_F1] = 0xc9, /* F1..F6: C9h..CEh */
            [XKEY_F2] = 0xca,
            [XKEY_F3] = 0xcb,
            [XKEY_F4] = 0xcc,
            [XKEY_F5] = 0xcd,
            [XKEY_F6] = 0xce,
            [XKEY_1] = '1',
            [XKEY_2] = '2',
            [XKEY_3] = '3',
            [XKEY_A] = 'A',
            [XKEY_B] = 'B',
            [XKEY_S] = 'S',
            [XKEY_R] = 'R',
            [XKEY_Y] = 'Y',
            [XKEY_N] = 'N',
            [XKEY_GAME_ESCAPE] = 27, /* the game's Esc (1Bh) */
            [XKEY_L] = 'L',
            [XKEY_C] = 'C',
            [XKEY_O] = 'O',
            [XKEY_V] = 'V',
            [XKEY_0] = '0',
            [XKEY_4] = '4',
            [XKEY_5] = '5',
            [XKEY_6] = '6',
            [XKEY_7] = '7',
            [XKEY_8] = '8',
            [XKEY_9] = '9',
            [XKEY_TAB] = 9, /* Tab, Backspace */
            [XKEY_BACKSPACE] = 8,
            [XKEY_D] = 'D',
            [XKEY_E] = 'E',
            [XKEY_F] = 'F',
            [XKEY_G] = 'G',
            [XKEY_H] = 'H',
            [XKEY_I] = 'I',
            [XKEY_J] = 'J',
            [XKEY_K] = 'K',
            [XKEY_M] = 'M',
            [XKEY_P] = 'P',
            [XKEY_Q] = 'Q',
            [XKEY_T] = 'T',
            [XKEY_U] = 'U',
            [XKEY_W] = 'W',
            [XKEY_X] = 'X',
            [XKEY_Z] = 'Z',
            [XKEY_F7] = 0xcf, /* F7, F8: CFh, D0h (4FEFF's CS:7AF8) */
            [XKEY_F8] = 0xd0,
    };
    _Static_assert(sizeof commands == (size_t)XKEY_F8 + 1u, "one code per key up to F8");
    static const uint8_t extended[] = {0xdc, 0xdd, 0xde,
                                       0xdf, 0xe0, 0xd1}; /* Home End PgUp PgDn Ins F9 */
    unsigned base = (unsigned)key & ~(unsigned)XKEY_CTRL, command;
    if (base >= (unsigned)XKEY_PUNCT_FIRST && base <= (unsigned)XKEY_PUNCT_LAST) {
        command = (unsigned char)punctuation[base - (unsigned)XKEY_PUNCT_FIRST];
    } else if (base >= (unsigned)XKEY_LOWER_FIRST && base <= (unsigned)XKEY_LOWER_LAST) {
        command = 'a' + (base - (unsigned)XKEY_LOWER_FIRST);
    } else if (base >= (unsigned)XKEY_EXTENDED_FIRST && base <= (unsigned)XKEY_EXTENDED_LAST) {
        command = extended[base - (unsigned)XKEY_EXTENDED_FIRST]; /* CS:7AF8 */
    } else if (base >= sizeof commands) {
        return 0;
    } else {
        command = commands[base];
    }
    if (((unsigned)key & XKEY_CTRL) && command >= 0x40u) {
        command -= 0x40u;
    }
    return (uint16_t)(command | ((uint16_t)modifiers << 8));
}
/** Initializes an empty input queue. */
void xeen_input_queue_init(xeen_input_queue *q)
{
    if (q) {
        memset(q, 0, sizeof *q);
    }
}
/** Appends KEY to the queue; returns 0 when the queue is full. */
int xeen_input_queue_key(xeen_input_queue *q, uint16_t key)
{
    if (!q || q->key_count == XEEN_INPUT_QUEUE_MAX) {
        return 0;
    }
    q->keys[(q->key_head + q->key_count) % XEEN_INPUT_QUEUE_MAX] = key;
    ++q->key_count;
    return 1;
}
/* The INT 33h callback 4E172 (mask 0Ah: a left or right press)
 * pushes the press position while cs:5C40 < 13h; 4E18F pops the newest
 * (LIFO). A sample without a click only moves the pointer. */
int xeen_input_queue_mouse(xeen_input_queue *q, xeen_mouse_sample sample)
{
    if (!q) {
        return 0;
    }
    q->current_mouse.x = sample.x;
    q->current_mouse.y = sample.y;
    q->current_mouse.enabled = sample.enabled;
    if (!sample.click) {
        return 1;
    }
    if (q->mouse_count >= XEEN_INPUT_MOUSE_STACK) {
        return 0;
    }
    q->mice[q->mouse_count++] = sample;
    return 1;
}
/** Keyboard callback of a queue provider: takes the next queued key, or 0 when none is queued. */
static int queue_keyboard(void *u, uint16_t *key)
{
    xeen_input_queue *q = u;
    *key = 0;
    if (q->key_count) {
        *key = q->keys[q->key_head];
        q->key_head = (q->key_head + 1u) % XEEN_INPUT_QUEUE_MAX;
        --q->key_count;
    }
    return 1;
}
/**
 * Mouse callback of a queue provider: takes the last queued sample, or the current position without
 * a click.
 */
static int queue_mouse(void *u, xeen_mouse_sample *sample)
{
    xeen_input_queue *q = u;
    if (q->mouse_count) {
        *sample = q->mice[--q->mouse_count];
        return 1;
    }
    *sample = q->current_mouse;
    sample->click = 0;
    return 1;
}
/** Returns an input provider that reads keys and mouse samples from queue Q. */
xeen_input_provider xeen_input_queue_provider(xeen_input_queue *q)
{
    xeen_input_provider p = {queue_keyboard, queue_mouse, q};
    return p;
}
/** Initializes the input poll state for a DS at SEGMENT. */
void xeen_input_poll_init(xeen_input_poll *p, uint16_t segment)
{
    if (p) {
        memset(p, 0, sizeof *p);
        p->ds_segment = segment;
    }
}
/** Frees the poll's loaded song. */
void xeen_input_poll_destroy(xeen_input_poll *p)
{
    if (p) {
        free(p->song);
        p->song = NULL;
        p->song_size = 0;
    }
}
/** Requests the music change FLAG for the next poll. */
void xeen_input_poll_request_music(xeen_input_poll *p, uint16_t flag)
{
    if (p) {
        p->pending_music = (uint8_t)flag;
    }
}
void xeen_input_poll_stack_keys(xeen_input_poll *p, uint16_t segment, uint16_t offset,
                                const uint8_t *keys, unsigned count)
{
    if (!p) {
        return;
    }
    if (count > sizeof p->stack_keys) {
        count = 0;
    }
    p->stack_segment = segment;
    p->stack_offset = offset;
    p->stack_count = (uint8_t)count;
    if (count) {
        memcpy(p->stack_keys, keys, count);
    }
}
void xeen_input_poll_frame(xeen_input_poll *p, uint16_t segment, uint16_t base,
                           const uint8_t *bytes, unsigned size)
{
    if (!p) {
        return;
    }
    if (size > sizeof p->frame || (size && !bytes) || (unsigned)base + size > 65536u) {
        size = 0;
    }
    p->frame_segment = segment;
    p->frame_base = base;
    p->frame_size = (uint8_t)size;
    if (size) {
        memcpy(p->frame, bytes, size);
    }
}
/**
 * Performs a requested music change at the poll: loads the map's song from the archives and starts
 * it on the audio sink, as the original's poll does.
 */
static xeen_status load_pending_music(xeen_input_poll *p, xeen_movement *m,
                                      xeen_native_audio *audio,
                                      const xeen_sprite_archives *archives,
                                      xeen_input_poll_result *r)
{
    const xeen_archive *a;
    xeen_resource_info info;
    uint8_t *song;
    size_t n = 0, actual;
    uint8_t cc, saved_world;
    if (!p->pending_music) {
        return XEEN_OK;
    }
    if (!audio) {
        r->boundary = XEEN_POLL_MUSIC_AUDIO;
        return XEEN_SERVICE_REQUIRED;
    }
    /* 189EF..189FF: 50AB1(E0h) (the song playing), then 18ED0: 47F3D (
     * a busy drive defers the song; 1658 stays set, so the next poll retries),
     * then FC0F. cd_now is the game's BIOS clock, set before the poll. */
    if (audio->music_playing || xeen_native_audio_cd_status(audio, XEEN_CD_STATUS_MUSIC_CALL) ||
        xeen_read16(m, 0xfc0f)) { /* FC0F: 18ED0's third deferral test (numeric: its role is not established) */
        return XEEN_OK;
    }
    cc = m->ds[XEEN_DS_CLOUDS_INSTALLED] ? m->ds[XEEN_DS_MAP_ARCHIVE] : 1;
    if (cc > 1 || !archives || !(a = archives->cc[cc])) {
        r->music_unavailable = 1;
        r->boundary = XEEN_POLL_MUSIC_ARCHIVE;
        return XEEN_SERVICE_REQUIRED;
    }
    while (n < 16u && m->ds[XEEN_DS_SONG_NAME + n]) {
        ++n;
    }
    if (!n || n == 16u) {
        r->boundary = XEEN_POLL_MUSIC_LOAD;
        return XEEN_BAD_INPUT;
    }
    if (xeen_archive_find_named(a, (const char *)(m->ds + XEEN_DS_SONG_NAME), n, &info) != XR_OK) {
        r->music_unavailable = 1;
        r->boundary = XEEN_POLL_MUSIC_LOAD;
        return XEEN_SERVICE_REQUIRED;
    }
    song = malloc(info.size ? info.size : 1u);
    if (!song) {
        r->boundary = XEEN_POLL_MUSIC_LOAD;
        return XEEN_SERVICE_REQUIRED;
    }
    if (xeen_archive_extract(a, XR_BANK_ASSETS_XOR35, &info, song, info.size, &actual) != XR_OK) {
        free(song);
        r->music_unavailable = 1;
        r->boundary = XEEN_POLL_MUSIC_LOAD;
        return XEEN_SERVICE_REQUIRED;
    }
    saved_world = m->ds[XEEN_DS_WORLD]; /* the selected archive follows the world byte; restored after the load */
    xeen_select_archive(m, m->ds[XEEN_DS_MAP_ARCHIVE]);
    free(p->song);
    p->song = song;
    p->song_size = actual;
    ++p->music_loads;
    xeen_write16(m, XEEN_DS_LOADED_FILE_SIZE, (uint16_t)actual);
    ++audio->music_loads;
    audio->music_size = actual;
    xeen_select_archive(m, saved_world);
    if (m->ds[XEEN_DS_MUSIC_ON] && xeen_read16(m, 0xf430)) { /* F430 numeric (DS_GLOBALS.md: the song volume's word) */
        ++audio->music_starts;
    }
    xeen_native_audio_song(audio, m, song, actual); /* sink (ED9B); state unchanged */
    p->pending_music = 0;
    return XEEN_OK;
}
/* The installed gameplay button tables point into logical DS. Arbitrary far
 * UI tables (modal screens) require another owner and stop explicitly.
 * or into the installed frame image (xeen_input_poll_frame); a
 * table is then read as the frame's bytes (tb/tw), bounded by the frame. */
typedef struct {
    const uint8_t *bytes;
    uint32_t base, limit;
} far_table;
/**
 * Resolves the far pointer stored at DS SLOT to a bounded table: in DS itself, or in the host's
 * button frame; returns 0 for any other segment.
 */
static int table_of(const xeen_input_poll *p, const xeen_movement *m, uint16_t slot, far_table *t)
{
    uint16_t seg = xeen_read16(m, (uint16_t)(slot + 2u)), off = xeen_read16(m, slot);
    if (seg == p->ds_segment) {
        t->bytes = m->ds;
        t->base = off;
        t->limit = 65536u;
        return 1;
    }
    if (p->frame_size && seg == p->frame_segment && off >= p->frame_base &&
        off < p->frame_base + p->frame_size) {
        t->bytes = p->frame;
        t->base = (uint32_t)(off - p->frame_base);
        t->limit = p->frame_size;
        return 1;
    }
    return 0;
}
/** Reads byte I of table T into *V; returns 0 past the table's end. */
static int table_byte(const far_table *t, unsigned i, uint8_t *v)
{
    if (t->base + i >= t->limit) {
        return 0;
    }
    *v = t->bytes[t->base + i];
    return 1;
}
/** Reads word I of table T into *V; returns 0 past the table's end. */
static int table_word(const far_table *t, unsigned i, uint16_t *v)
{
    if (t->base + 2u * i + 1u >= t->limit) {
        return 0;
    }
    *v = xeen_get_le16(t->bytes + t->base + 2u * i);
    return 1;
}
/* The 989C keys: a DS table, or the installed stack table, whose
 * bytes past its count were never written (stack contents): no key;
 * or a table in the installed frame image. */
typedef struct {
    const xeen_input_poll *p;
    far_table t;
    int stack;
} key_table;
/**
 * Resolves the button key table (DS 989C): the host's stacked keys when the far pointer names them,
 * else the table it points to.
 */
static int keys_table(const xeen_input_poll *p, const xeen_movement *m, key_table *k)
{
    k->p = p;
    k->stack = 0;
    if (p->stack_count && xeen_read16(m, XEEN_DS_BUTTON_KEY_TABLE + 2u) == p->stack_segment &&
        xeen_read16(m, XEEN_DS_BUTTON_KEY_TABLE) == p->stack_offset) {
        k->stack = 1;
        return 1;
    }
    return table_of(p, m, XEEN_DS_BUTTON_KEY_TABLE, &k->t);
}
/** Returns button I's key from the key table; 0 past its end. */
static int key_at(const key_table *k, unsigned i, uint16_t *key)
{
    uint8_t v;
    if (k->stack) {
        if (i >= k->p->stack_count) {
            return 0;
        }
        *key = k->p->stack_keys[i];
        return 1;
    }
    if (!table_byte(&k->t, i, &v)) {
        return 0;
    }
    *key = v;
    return 1;
}
/* 18D5E (a click only): 4E18F until it returns 0 drains the queued presses, each sample that has
 * the mouse moving the pointer and presenting the page (4E18F's present), before the released
 * frame. The 64-sample bound is native; the original has none. Returns 0 when DRAIN has no
 * mouse sample (the caller's 18D66 stop). */
static int drain_clicks(const xeen_input_provider *drain, xeen_presentation *present,
                        xeen_movement *m)
{
    xeen_mouse_sample sample;
    unsigned samples = 0;
    do {
        if (!drain->mouse(drain->user, &sample)) {
            return 0;
        }
        if (sample.enabled) {
            xeen_write16(m, XEEN_DS_MOUSE_Y, sample.y);
            xeen_write16(m, XEEN_DS_MOUSE_X, sample.x);
            present->sampled_x = sample.x;
            present->sampled_y = sample.y;
            (void)xeen_presentation_present(present);
            xeen_presentation_display(present, XEEN_DISPLAY_RETRACE);
        }
    } while (sample.click && ++samples < 64u);
    return 1;
}
/**
 * Shows the press of BUTTON the way the original's poll does (18B0A.., 18D5E): its pressed
 * frame (2 x BUTTON + 1 of the button sprites) at the button's x/y, window 39 over its rectangle
 * copied to the screen with a retrace, then, for a click (DRAIN set), the queued mouse samples
 * drained until the button is released, and the released frame (2 x BUTTON) shown the same way.
 * Stops at 18B0A when the button tables cannot be read, and at the draw (18B33), window (18B76)
 * or mouse (18D66) service the host does not provide.
 */
static xeen_status button_feedback(xeen_input_poll *p, xeen_movement *m, xeen_presentation *present,
                                   unsigned button, const xeen_input_provider *drain,
                                   xeen_input_poll_result *r)
{
    far_table x_table, y_table, height_table, width_table;
    uint16_t x;
    uint8_t y, height, width;
    xeen_sprite_draw draw;
    unsigned released; /* 0: the pressed frame, 1: the released frame */
    if (!table_of(p, m, XEEN_DS_BUTTON_X_TABLE, &x_table) ||
        !table_of(p, m, XEEN_DS_BUTTON_Y_TABLE, &y_table) ||
        !table_of(p, m, XEEN_DS_BUTTON_HEIGHT_TABLE, &height_table) ||
        !table_of(p, m, XEEN_DS_BUTTON_WIDTH_TABLE, &width_table)) {
        r->boundary = XEEN_POLL_FEEDBACK_TABLES;
        return XEEN_SERVICE_REQUIRED;
    }
    if (!table_word(&x_table, button, &x) || !table_byte(&y_table, button, &y) ||
        !table_byte(&height_table, button, &height) || !table_byte(&width_table, button, &width)) {
        r->boundary = XEEN_POLL_FEEDBACK_TABLES;
        return XEEN_BAD_INPUT;
    }
    if (!present) {
        r->boundary = XEEN_POLL_FEEDBACK_DRAW;
        return XEEN_SERVICE_REQUIRED;
    }
    draw = (xeen_sprite_draw){xeen_read16(m, XEEN_DS_BUTTON_SPRITES), 0, x, y, 0, 0, 0};
    /* Evidence: the original-byte replay answers 189E3 from its key
     * queue, so this button feedback is not in its trace (drawcompare --npc). */
    xeen_presentation_trace_extra("npcsub begin");
    for (released = 0; released < 2; released++) {
        draw.frame = (uint16_t)(2u * button + (released ? 0u : 1u));
        if (xeen_presentation_draw_sprites(present, &draw, 1) != XEEN_OK) {
            r->boundary = XEEN_POLL_FEEDBACK_DRAW;
            return XEEN_SERVICE_REQUIRED;
        }
        if (xeen_presentation_define_window(present, XEEN_WINDOW_BUTTON_FEEDBACK, draw.x, draw.y, height, width) !=
                    XEEN_OK ||
            xeen_presentation_update_window(present, XEEN_WINDOW_BUTTON_FEEDBACK) != XEEN_OK) {
            r->boundary = XEEN_POLL_FEEDBACK_WINDOW;
            return XEEN_SERVICE_REQUIRED;
        }
        xeen_presentation_display(present, XEEN_DISPLAY_RETRACE); /* 50BF6's 4EF71 retrace */
        if (!released && drain && !drain_clicks(drain, present, m)) {
            r->boundary = XEEN_POLL_FEEDBACK_DRAIN;
            return XEEN_SERVICE_REQUIRED;
        }
    }
    xeen_presentation_trace_extra("npcsub end");
    return XEEN_OK;
}
/* Owner-approved add-ons (NOT original): button_feedback() for button i of the
 * installed native set (the same press, window 39 copy, retrace, drained
 * clicks and release). */
static xeen_status addon_set_feedback(const xeen_poll_buttons *b, xeen_presentation *present,
                                      unsigned i, const xeen_input_provider *drain,
                                      xeen_movement *m, xeen_input_poll_result *r)
{
    unsigned phase;
    if (!present) {
        r->boundary = XEEN_POLL_FEEDBACK_DRAW;
        return XEEN_SERVICE_REQUIRED;
    }
    xeen_presentation_trace_extra("npcsub begin");
    for (phase = 0; phase < 2; phase++) {
        b->draw(b->user, i, !phase);
        if (xeen_presentation_define_window(present, XEEN_WINDOW_BUTTON_FEEDBACK, b->x[i], b->y[i], b->h[i], b->w[i]) !=
                    XEEN_OK ||
            xeen_presentation_update_window(present, XEEN_WINDOW_BUTTON_FEEDBACK) != XEEN_OK) {
            r->boundary = XEEN_POLL_FEEDBACK_WINDOW;
            return XEEN_SERVICE_REQUIRED;
        }
        xeen_presentation_display(present, XEEN_DISPLAY_RETRACE);
        if (!phase && drain && !drain_clicks(drain, present, m)) {
            r->boundary = XEEN_POLL_FEEDBACK_DRAIN;
            return XEEN_SERVICE_REQUIRED;
        }
    }
    xeen_presentation_trace_extra("npcsub end");
    return XEEN_OK;
}
/** Pressed-button feedback for an add-on's buttons (owner-approved, not original). */
static xeen_status addon_feedback(xeen_input_poll *p, xeen_presentation *present, unsigned i,
                                  const xeen_input_provider *drain, xeen_movement *m,
                                  xeen_input_poll_result *r)
{
    return addon_set_feedback(p->addon_buttons, present, i, drain, m, r);
}
/** Sets *I to the first of add-on buttons B whose key is KEY and returns 1; 0 when none has it. */
static int poll_button_for_key(const xeen_poll_buttons *b, uint16_t key, unsigned *i)
{
    for (*i = 0; *i < b->count; ++*i) {
        if (key == b->key[*i]) {
            return 1;
        }
    }
    return 0;
}
/* Where the key scan of the live tables ended: at the row with the key, at the tables' end, or at a bad row. */
typedef enum { KEY_ROW_FOUND, KEY_ROW_END, KEY_ROW_BAD } key_row;
/**
 * Finds the first live row whose key (KEYS) is KEY: KEY_ROW_FOUND with its row in *I and its enabled byte (ENABLED)
 * in *IS_ENABLED. The rows are read in order, each its x word (X), key and enabled byte, until the next row's x is
 * FFFFh (KEY_ROW_END). KEY_ROW_BAD when a row's entry is past its table, or after 128 rows without an end. Unlike
 * the click's scan (live_button_at), a next row past the x table does not end the scan: that row is read next, and
 * it is bad. Original: 18AC5..18BF1.
 */
static key_row key_button_row(const far_table *x, const key_table *keys, const far_table *enabled, uint16_t key,
                              unsigned *i, uint8_t *is_enabled)
{
    for (*i = 0; *i < 128; ++*i) {
        uint16_t button_key, next;
        if (!table_word(x, *i, &next) || !key_at(keys, *i, &button_key) || !table_byte(enabled, *i, is_enabled)) {
            return KEY_ROW_BAD;
        }
        if (key == button_key) {
            return KEY_ROW_FOUND;
        }
        if (table_word(x, *i + 1u, &next) && next == 0xffffu) {
            return KEY_ROW_END;
        }
    }
    return KEY_ROW_BAD;
}
/*
 * A key pressed while the key buttons' feedback is on: the button with that key shows its press and release (no
 * queued clicks to drain), when it is enabled; a disabled one shows nothing. Nothing either while no button tables
 * are installed (98AC:98AE 0). In order:
 * 1. the add-on buttons beside the tables (owner-approved, not original): the first with the key;
 * 2. the add-on set replacing the tables: the first with the key; no other test;
 * 3. the live tables' first row with the key (key_button_row), until the tables' end.
 * An enabled button of 2 or 3 leaves the key feedback's residue (8722h) for the poll.
 * Stops: the tables unreadable (18AE7), a row unreadable or the table without an end in 128 rows (18BFE), and the
 * feedback's own stops. Original: 18AC5..18BF1, the keys 989C.
 */
static xeen_status key_buttons(xeen_input_poll *p, xeen_movement *m, xeen_presentation *present,
                               uint16_t key, xeen_input_poll_result *r)
{
    far_table x_table, enabled_table;
    uint8_t enabled;
    key_table keys;
    unsigned i;
    if (!(xeen_read16(m, XEEN_DS_BUTTON_X_TABLE) | xeen_read16(m, XEEN_DS_BUTTON_X_TABLE + 2u))) {
        return XEEN_OK;
    }
    if (p->addon_extra && poll_button_for_key(p->addon_extra, key, &i)) { /* owner-approved add-on */
        return p->addon_extra->enabled[i] ? addon_set_feedback(p->addon_extra, present, i, NULL, m, r) : XEEN_OK;
    }
    if (p->addon_buttons) { /* owner-approved add-on: the native set */
        if (!poll_button_for_key(p->addon_buttons, key, &i) || !p->addon_buttons->enabled[i]) {
            return XEEN_OK;
        }
        p->last_residue = XEEN_POLL_RESIDUE_BUTTON_KEY;
        return addon_feedback(p, present, i, NULL, m, r);
    }
    if (!table_of(p, m, XEEN_DS_BUTTON_X_TABLE, &x_table) || !keys_table(p, m, &keys) ||
        !table_of(p, m, XEEN_DS_BUTTON_ENABLED_TABLE, &enabled_table)) {
        r->boundary = XEEN_POLL_KEY_TABLES;
        return XEEN_SERVICE_REQUIRED;
    }
    switch (key_button_row(&x_table, &keys, &enabled_table, key, &i, &enabled)) {
        case KEY_ROW_BAD:
            r->boundary = XEEN_POLL_BUTTON_ROW;
            return XEEN_BAD_INPUT;
        case KEY_ROW_END:
            return XEEN_OK;
        case KEY_ROW_FOUND:
            break;
    }
    if (!enabled) {
        return XEEN_OK;
    }
    p->last_residue = XEEN_POLL_RESIDUE_BUTTON_KEY;
    return button_feedback(p, m, present, i, NULL, r);
}
/* The 18E34 far tables (not DS): the five combat monster columns' x
 * (57AB0) and widths (57AC0), image bytes 72,8,161,116,26 / 88,62,62,88,88. */
static const uint8_t monster_x[5] = {72, 8, 161, 116, 26}, monster_w[5] = {88, 62, 62, 88, 88};
/** Sets *I to the first of add-on buttons B containing (X, Y) and returns 1; 0 when none does. */
static int poll_button_at(const xeen_poll_buttons *b, int x, int y, unsigned *i)
{
    for (*i = 0; *i < b->count; ++*i) {
        if ((int)b->y[*i] <= y && (int)(b->y[*i] + b->h[*i]) > y && (int)b->x[*i] <= x &&
            (int)(b->x[*i] + b->w[*i]) > x) {
            return 1;
        }
    }
    return 0;
}
/**
 * Sets *I to the party member whose portrait contains (X, Y) and returns 1; 0 when none does. Portrait I is the
 * 32 x 32 square at x 17B2[I], y 150..181, tested for the members 0..count-1 in order.
 * Original: 18C0C's first loop (y 96h..B5h).
 */
static int portrait_at(const xeen_movement *m, int x, int y, unsigned *i)
{
    for (*i = 0; *i < m->ds[XEEN_DS_PARTY_COUNT]; ++*i) {
        if (y >= 150 && y < 182 && m->ds[XEEN_DS_PORTRAIT_X + *i] <= x && m->ds[XEEN_DS_PORTRAIT_X + *i] + 32 > x) {
            return 1;
        }
    }
    return 0;
}
/** The live button tables (98AC..9898) resolved: each row's x word, y, width, height and enabled bytes, its key. */
typedef struct {
    far_table x, y, width, height, enabled;
    key_table keys;
} live_buttons;
/** Resolves the six live tables into *B; returns 0 when one of them cannot be read. */
static int live_buttons_of(const xeen_input_poll *p, const xeen_movement *m, live_buttons *b)
{
    return table_of(p, m, XEEN_DS_BUTTON_X_TABLE, &b->x) && table_of(p, m, XEEN_DS_BUTTON_Y_TABLE, &b->y) &&
           table_of(p, m, XEEN_DS_BUTTON_HEIGHT_TABLE, &b->height) &&
           table_of(p, m, XEEN_DS_BUTTON_WIDTH_TABLE, &b->width) && keys_table(p, m, &b->keys) &&
           table_of(p, m, XEEN_DS_BUTTON_ENABLED_TABLE, &b->enabled);
}
typedef enum { LIVE_NONE, LIVE_FOUND, LIVE_BAD_ROW } live_hit;
/**
 * Finds the first live button whose rectangle contains (X, Y): LIVE_FOUND with its row in *I and its enabled byte
 * in *ENABLED. The rows are read in order until a hit, until the next row's x is FFFFh or past its table (the end),
 * or 128 rows (LIVE_NONE); LIVE_BAD_ROW when a row's x, y, width, height or enabled entry is past its table.
 * The rectangle test is the original's: y + height in int, x and x + width as signed words (x + width wraps).
 */
static live_hit live_button_at(const live_buttons *b, int x, int y, unsigned *i, uint8_t *enabled)
{
    for (*i = 0; *i < 128; ++*i) {
        uint16_t bx, next;
        uint8_t by, height, width;
        if (!table_word(&b->x, *i, &bx) || !table_byte(&b->y, *i, &by) || !table_byte(&b->width, *i, &width) ||
            !table_byte(&b->height, *i, &height) || !table_byte(&b->enabled, *i, enabled)) {
            return LIVE_BAD_ROW;
        }
        if (by <= y && by + height > y && (int16_t)bx <= x && (int16_t)(bx + width) > x) {
            return LIVE_FOUND;
        }
        if (!table_word(&b->x, *i + 1u, &next) || next == 0xffffu) {
            return LIVE_NONE;
        }
    }
    return LIVE_NONE;
}
/**
 * Sets *COLUMN to the combat monster column containing (X, Y) and returns 1; 0 when none does. The columns span
 * y 42..127: entries 0..2 (centre, left, right) for three monsters, 3..4 (the halves) when view slot 1 holds a
 * monster and slot 2 none.
 * Original: 18E0E with the 18E34 tables. The two-column choice, like the hit effects' (1BC46), compares the slots
 * with 0, not FFh (the draw offsets' "none", scene.c monster_offsets): an original quirk, kept (the intent is
 * inferred, Phase 257).
 */
static int monster_column_at(const xeen_movement *m, int x, int y, unsigned *column)
{
    *column = (m->ds[XEEN_DS_VIEW_MONSTER(1)] && !m->ds[XEEN_DS_VIEW_MONSTER(2)]) ? 3u : 0u;
    for (; *column < 5u; ++*column) {
        if (y >= 42 && y < 128 && monster_x[*column] <= x && monster_x[*column] + monster_w[*column] > x) {
            return 1;
        }
    }
    return 0;
}
/**
 * A click on add-on button I of B (owner-approved, not original): when enabled it is pressed, the queued clicks
 * drained and released (a feedback stop ends the click with no key); its key in any case.
 */
static xeen_status addon_click(const xeen_poll_buttons *b, unsigned i, const xeen_input_provider *provider,
                               xeen_presentation *present, xeen_movement *m, uint16_t *key,
                               xeen_input_poll_result *r)
{
    xeen_status s;
    if (b->enabled[i] && (s = addon_set_feedback(b, present, i, provider, m, r)) != XEEN_OK) {
        return s;
    }
    *key = b->key[i];
    return XEEN_OK;
}
/**
 * Finds the control under the click at (F604, F602) as signed words, and sets *KEY to its command (0: none). In
 * order, the first that contains the point wins:
 * 1. with the portrait clicks on (98D6), a party member's portrait: F1 + the member;
 * 2. an add-on button (owner-approved, not original): the extra set beside the tables, then the native set, which
 *    replaces the tables (no further test);
 * 3. with the default tables installed (98AC = DS:1BC4), command 1 wherever the click is;
 * 4. a live button: its key; an enabled one is first pressed, the queued clicks drained and released (feedback,
 *    which can stop the poll), a disabled one gives its key without feedback;
 * 5. in combat (9A1C 2), a monster column: 0Eh + the column.
 * Stops: the live tables unreadable (18C6B), a row unreadable or its key past the key table (18BFE), and the
 * feedback's own stops.
 * Original: 18C0C.
 */
static xeen_status clicked_control(xeen_input_poll *p, xeen_movement *m,
                                   const xeen_input_provider *provider, xeen_presentation *present,
                                   uint16_t *key, xeen_input_poll_result *r)
{
    /* 18C0C compares the position as signed words (jle/jg). */
    int mouse_x = (int16_t)xeen_read16(m, XEEN_DS_MOUSE_X),
        mouse_y = (int16_t)xeen_read16(m, XEEN_DS_MOUSE_Y);
    unsigned i;
    uint8_t enabled;
    uint16_t button_key;
    live_buttons live;
    xeen_status s;
    *key = 0;
    if (m->ds[XEEN_DS_PORTRAIT_CLICKS] && portrait_at(m, mouse_x, mouse_y, &i)) {
        *key = (uint16_t)(XEEN_KEY_F1 + i);
        return XEEN_OK;
    }
    if (p->addon_extra && poll_button_at(p->addon_extra, mouse_x, mouse_y, &i)) {
        return addon_click(p->addon_extra, i, provider, present, m, key, r);
    }
    if (p->addon_buttons) {
        if (!poll_button_at(p->addon_buttons, mouse_x, mouse_y, &i)) {
            return XEEN_OK;
        }
        if (p->addon_buttons->enabled[i]) {
            p->last_residue = XEEN_POLL_RESIDUE_CLICK;
        }
        return addon_click(p->addon_buttons, i, provider, present, m, key, r);
    }
    if (xeen_read16(m, XEEN_DS_BUTTON_X_TABLE + 2u) == p->ds_segment &&
        xeen_read16(m, XEEN_DS_BUTTON_X_TABLE) == 0x1bc4u) {
        *key = 1;
        return XEEN_OK;
    }
    if (!live_buttons_of(p, m, &live)) {
        r->boundary = XEEN_POLL_CLICK_TABLES;
        return XEEN_SERVICE_REQUIRED;
    }
    switch (live_button_at(&live, mouse_x, mouse_y, &i, &enabled)) {
        case LIVE_BAD_ROW:
            r->boundary = XEEN_POLL_BUTTON_ROW;
            return XEEN_BAD_INPUT;
        case LIVE_FOUND:
            if (enabled) {
                p->last_residue = XEEN_POLL_RESIDUE_CLICK; /* 50BF6's push ds under 18C0C */
                if ((s = button_feedback(p, m, present, i, provider, r)) != XEEN_OK) {
                    return s;
                }
            }
            if (!key_at(&live.keys, i, &button_key)) {
                r->boundary = XEEN_POLL_BUTTON_ROW;
                return XEEN_BAD_INPUT;
            }
            *key = button_key;
            return XEEN_OK;
        case LIVE_NONE:
            break;
    }
    if (m->ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_COMBAT && monster_column_at(m, mouse_x, mouse_y, &i)) {
        *key = (uint16_t)(0x0eu + i);
    }
    return XEEN_OK;
}
/* Answers the poll with COMMAND (counting the poll). */
static xeen_status poll_answer(xeen_input_poll *p, xeen_input_poll_result *r, uint16_t command)
{
    r->command = command;
    ++p->polls;
    return XEEN_OK;
}
/*
 * Reads the pointer (4E18F) into *MOUSE. A shown pointer stores its position in F604/F602 and in the presentation's
 * sampled position, then presents the frame (the moved pointer). A hidden one (the sample disabled, or a
 * presentation that is off: CS:16BE 0, 4E231 until the next 4E06B) stores 0, 0 and gives no click (4E225).
 * Stops when the host gives no sample, and when a shown pointer has no presentation or its frame is refused.
 */
static xeen_status sample_pointer(xeen_movement *m, const xeen_input_provider *provider, xeen_presentation *present,
                                  xeen_mouse_sample *mouse, xeen_input_poll_result *r)
{
    memset(mouse, 0, sizeof *mouse);
    if (!provider->mouse(provider->user, mouse)) {
        r->boundary = XEEN_POLL_MOUSE_SAMPLE;
        return XEEN_SERVICE_REQUIRED;
    }
    if (mouse->enabled && present && !present->enabled) {
        mouse->enabled = 0;
    }
    if (!mouse->enabled) {
        mouse->click = 0;
        xeen_write16(m, XEEN_DS_MOUSE_Y, 0);
        xeen_write16(m, XEEN_DS_MOUSE_X, 0);
        return XEEN_OK;
    }
    xeen_write16(m, XEEN_DS_MOUSE_Y, mouse->y);
    xeen_write16(m, XEEN_DS_MOUSE_X, mouse->x);
    if (present) {
        present->sampled_x = mouse->x;
        present->sampled_y = mouse->y;
    }
    if (!present || xeen_presentation_present(present) != XEEN_OK) {
        r->boundary = XEEN_POLL_MOUSE_PRESENT;
        return XEEN_SERVICE_REQUIRED;
    }
    return XEEN_OK;
}
/* Nonzero when the exploration buttons are installed (98AC = DS:197C) and the stored click position is over the 3D
 * view (x 8..224, y 8..135 as signed words: the original's bounds). Original: 18A48. */
static int click_over_view(const xeen_input_poll *p, const xeen_movement *m)
{
    int16_t x = (int16_t)xeen_read16(m, XEEN_DS_MOUSE_X), y = (int16_t)xeen_read16(m, XEEN_DS_MOUSE_Y);
    return xeen_read16(m, XEEN_DS_BUTTON_X_TABLE + 2u) == p->ds_segment &&
           xeen_read16(m, XEEN_DS_BUTTON_X_TABLE) == XEEN_DS_EXPLORATION_BUTTON_X && x >= 8 && x <= 224 && y >= 8 &&
           y <= 135;
}
/*
 * A left click: *KEY is the control under it (clicked_control; its feedback may stop the poll), stored in F5FE,
 * except over the 3D view with the exploration buttons, where it is command 1 whatever the control. 0 means the
 * click hit nothing (the keyboard is read next). Stops without a presentation (unreachable: the shown pointer
 * needed one).
 */
static xeen_status click_command(xeen_input_poll *p, xeen_movement *m, const xeen_input_provider *provider,
                                 xeen_presentation *present, uint16_t *key, xeen_input_poll_result *r)
{
    xeen_status s;
    if (!present) {
        r->boundary = XEEN_POLL_CLICK_PRESENTATION;
        return XEEN_SERVICE_REQUIRED;
    }
    if ((s = clicked_control(p, m, provider, present, key, r)) != XEEN_OK) {
        return s;
    }
    xeen_write16(m, XEEN_DS_INPUT_CODE, *key);
    if (click_over_view(p, m)) {
        *key = 1;
        xeen_write16(m, XEEN_DS_INPUT_CODE, 1);
    }
    return XEEN_OK;
}
/*
 * The keyboard (18A89): one reading, its low byte stored in F5FE and returned, its high byte (the modifiers) in
 * F5FC. A key leaves the clock call's frame residue. F10, and Ctrl+Q inside a menu, end the poll with the
 * original's own handlers (the DOS shell, the program's exit). With the key buttons' feedback on (8C76), a key
 * matching a live button presses it (key_buttons, which can stop the poll). Stops when the host gives no reading.
 */
static xeen_status key_command(xeen_input_poll *p, xeen_movement *m, const xeen_input_provider *provider,
                               xeen_presentation *present, xeen_input_poll_result *r)
{
    uint16_t key = 0;
    xeen_status s;
    if (!provider->keyboard(provider->user, &key)) {
        r->boundary = XEEN_POLL_KEYBOARD;
        return XEEN_SERVICE_REQUIRED;
    }
    xeen_write16(m, XEEN_DS_INPUT_CODE, key & 255u);
    xeen_write16(m, XEEN_DS_INPUT_MODIFIERS, key & 0xff00u);
    key &= 255u;
    if (key) {
        p->last_residue = XEEN_POLL_RESIDUE_KEY; /* 4FD2A's INT 1Ah frame */
    }
    if ((key == XEEN_KEY_CTRL_Q && m->ds[XEEN_DS_IN_MENU]) || key == XEEN_KEY_F10) {
        r->boundary = key == XEEN_KEY_F10 ? XEEN_POLL_DOS_SHELL : XEEN_POLL_QUIT;
        return XEEN_SERVICE_REQUIRED;
    }
    if (m->ds[XEEN_DS_KEY_BUTTON_FEEDBACK] && key && (s = key_buttons(p, m, present, key, r)) != XEEN_OK) {
        return s;
    }
    return poll_answer(p, r, key);
}
/*
 * The command poll (18A22): one input, as the original reads them.
 * 1. The result is cleared, the residue reset, and a "poll" trace line written.
 * 2. A pending song is started (load_pending_music), except while a release is awaited.
 * 3. The pointer is read (sample_pointer).
 * 4. While a release is awaited (after a click above 1), the poll ends with the code stored then (Esc) once the
 *    button is up, and waits again while it is still down.
 * 5. The click is stored in F600. A click above 1 is Esc, answered on its release (unreachable from the native
 *    mouse ABI, input_poll.h). A left click gives its control's command (click_command) unless it hit nothing.
 * 6. Otherwise the keyboard answers (key_command).
 * Every stop leaves its boundary in R; a missing argument is XEEN_BAD_INPUT before anything is touched.
 */
xeen_status xeen_input_poll_run(xeen_input_poll *p, xeen_movement *m,
                                const xeen_input_provider *provider, xeen_native_audio *audio,
                                const xeen_sprite_archives *archives, xeen_presentation *present,
                                xeen_input_poll_result *r)
{
    xeen_mouse_sample mouse;
    uint16_t key = 0;
    xeen_status s;
    if (!p || !m || !provider || !provider->keyboard || !provider->mouse || !r) {
        return XEEN_BAD_INPUT;
    }
    memset(r, 0, sizeof *r);
    p->last_residue = 0;
    xeen_presentation_trace("poll");
    if (!p->awaiting_mouse_release && (s = load_pending_music(p, m, audio, archives, r)) != XEEN_OK) {
        return s;
    }
    if ((s = sample_pointer(m, provider, present, &mouse, r)) != XEEN_OK) {
        return s;
    }
    if (p->awaiting_mouse_release) {
        if (mouse.click) {
            r->boundary = XEEN_POLL_MOUSE_RELEASE;
            return XEEN_SERVICE_REQUIRED;
        }
        p->awaiting_mouse_release = 0;
        return poll_answer(p, r, xeen_read16(m, XEEN_DS_INPUT_CODE));
    }
    xeen_write16(m, XEEN_DS_MOUSE_CLICK, mouse.click);
    if (mouse.click > 1) {
        xeen_write16(m, XEEN_DS_INPUT_CODE, XEEN_KEY_ESCAPE);
        p->awaiting_mouse_release = 1;
        r->boundary = XEEN_POLL_MOUSE_RELEASE;
        return XEEN_SERVICE_REQUIRED;
    }
    if (mouse.click == 1) {
        if ((s = click_command(p, m, provider, present, &key, r)) != XEEN_OK) {
            return s;
        }
        if (key) {
            return poll_answer(p, r, key);
        }
    }
    return key_command(p, m, provider, present, r);
}
