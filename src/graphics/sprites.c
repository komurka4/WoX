#include "xeen/sprites.h"
#include "xeen/monster_record.h"
#include "xeen/ds_globals.h"
#include "xeen/byte_order.h"
#include "xeen/map_objects.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xeen/ds_texts.h"

/* The sprite and song names of the map load come from the live DS (src/data; Phase 293): a text at its offset, or
 * string K of a far-pointer table (its offset word, the raw ds:XXXX pointers), as the original reads them. */
static const char *ds_string(const xeen_movement *m, uint16_t offset)
{
    return (const char *)m->ds + offset;
}
/** Returns string K of the DS table of far pointers at TABLE. */
static const char *far_string(const xeen_movement *m, uint16_t table, unsigned k)
{
    return ds_string(m, xeen_read16(m, (uint16_t)(table + 4u * k)));
}

/** Initializes an empty sprite cache. */
void xeen_sprite_cache_init(xeen_sprite_cache *c)
{
    if (c) {
        memset(c, 0, sizeof *c);
    }
}

int xeen_sprite_free_from(xeen_sprite_cache *c, uint16_t handle)
{
    unsigned i;
    if (!c || !handle) {
        return 0;
    }
    if (handle > c->last) {
        return 1;
    }
    c->last = (uint16_t)(handle - 1u);
    for (i = (unsigned)handle - 1u; i < XEEN_SPRITE_SLOTS; ++i) {
        memset(&c->slots[i], 0, sizeof c->slots[i]);
    }
    return 1;
}

/** Returns the index of the first free sprite slot, or -1 when the cache is full. */
static int first_free(const xeen_sprite_cache *c)
{
    unsigned i;
    for (i = 0; i < XEEN_SPRITE_SLOTS; ++i) {
        if (c->slots[i].state == XSP_FREE) {
            return (int)i;
        }
    }
    return -1;
}

/* Startup resource whose identity is not reconstructed: occupies the next
 * slot so later numbering is authentic; it can never be resolved or drawn. */
static xeen_resource_status xeen_sprite_reserve(xeen_sprite_cache *c, uint16_t *handle)
{
    int slot;
    if (!c || !handle) {
        return XR_BAD_INPUT;
    }
    if ((slot = first_free(c)) < 0) {
        return XR_CAPACITY;
    }
    c->slots[slot].state = XSP_RESERVED;
    *handle = c->last = (uint16_t)(slot + 1);
    return XR_OK;
}

xeen_resource_status xeen_sprite_reserve_startup(xeen_sprite_cache *c, uint16_t *first)
{
    static const uint16_t sizes[7] = {20795u, 64000u, 47104u, 34816u, 34816u, 34816u, 34816u};
    unsigned i;
    uint16_t h = 0;
    xeen_resource_status s;
    if (!c || !first) {
        return XR_BAD_INPUT;
    }
    for (i = 0; i < 7u; ++i) {
        if ((s = xeen_sprite_reserve(c, &h)) != XR_OK) {
            return s;
        }
        c->slots[h - 1u].size = sizes[i];
        if (!i) {
            *first = h;
        }
    }
    return XR_OK;
}

/** Returns nonzero when NAME starts with "MAZE" (in any case): the map's own sprite files. */
static int is_maze(const char *name)
{
    static const char maze[4] = {'M', 'A', 'Z', 'E'};
    unsigned i;
    for (i = 0; i < 4; ++i) {
        char ch = name[i];
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 32);
        }
        if (ch != maze[i]) {
            return 0;
        }
    }
    return 1;
}

/*
 * The slot (0-based) a load of a SIZE-byte resource goes to: with RELOAD (a 1-based handle) that slot, which must
 * hold a loaded sprite or be reserved with a size (else XR_BAD_INDEX) and be at least SIZE bytes (else XR_CAPACITY);
 * without, the first free slot (an empty resource XR_BAD_INPUT, a full cache XR_CAPACITY).
 */
static xeen_resource_status target_slot(const xeen_sprite_cache *c, uint16_t reload, uint16_t size, int *slot)
{
    if (reload) {
        if (reload > XEEN_SPRITE_SLOTS ||
            (c->slots[reload - 1u].state != XSP_LOADED &&
             !(c->slots[reload - 1u].state == XSP_RESERVED && c->slots[reload - 1u].size))) {
            return XR_BAD_INDEX;
        }
        if (size > c->slots[reload - 1u].size) {
            return XR_CAPACITY;
        }
        *slot = reload - 1;
        return XR_OK;
    }
    if (!size || (*slot = first_free(c)) < 0) {
        return size ? XR_CAPACITY : XR_BAD_INPUT;
    }
    return XR_OK;
}
/*
 * Records resource NAME (LENGTH bytes, from SOURCE) in slot E: loaded, its size (a new slot's allocation and the
 * cache's watermark; a reload keeps the slot's allocation), the frame count (the resource's first word, 0 for a
 * shorter one) and the frame table's second-cell bits: frame f's bit is set when its word at 4 + 4f is nonzero, for
 * the frames whose second word lies inside the GOT bytes (at most 256).
 */
static void record_sprite(xeen_sprite_cache *c, int slot, int reload, uint8_t source, const char *name, size_t length,
                          uint16_t size, const uint8_t *bytes, size_t got)
{
    xeen_sprite *e = &c->slots[slot];
    if (!reload) {
        e->size = size;
        c->last = (uint16_t)(slot + 1);
    }
    e->state = XSP_LOADED;
    e->archive = source;
    e->resource_size = size;
    memcpy(e->name, name, length + 1u);
    e->frames = (uint16_t)(got >= 2u ? ((unsigned)bytes[0] | (unsigned)bytes[1] << 8) : 0u);
    memset(e->second, 0, sizeof e->second);
    for (e->table_frames = 0; e->table_frames < 256u && 6u + 4u * e->table_frames <= got; ++e->table_frames) {
        if (bytes[4u + 4u * e->table_frames] | bytes[5u + 4u * e->table_frames]) {
            e->second[e->table_frames >> 3] |= (uint8_t)(1u << (e->table_frames & 7u));
        }
    }
}
xeen_resource_status xeen_sprite_load(xeen_sprite_cache *c, const xeen_sprite_archives *a,
                                      uint8_t current_cc, const char *name, uint16_t reload,
                                      uint16_t *handle)
{
    const xeen_archive *archive;
    xeen_resource_info info;
    xeen_resource_status s;
    uint8_t *bytes;
    size_t got = 0, length;
    uint8_t source;
    int slot = 0;
    if (!c || !a || !name || !handle || current_cc > 1u) {
        return XR_BAD_INPUT;
    }
    if (c->pre) {
        c->pre(c->user); /* 18931's 47B7D */
    }
    length = strlen(name);
    if (!length || length >= sizeof c->slots[0].name) {
        return XR_BAD_INPUT;
    }
    source = is_maze(name) ? (uint8_t)XSP_CUR : current_cc;
    archive = source == XSP_CUR ? a->cur : a->cc[current_cc];
    if (!archive) {
        return XR_NOT_FOUND;
    }
    s = xeen_archive_find_named(archive, name, length, &info);
    if (s != XR_OK) {
        return s;
    }
    if ((s = target_slot(c, reload, info.size, &slot)) != XR_OK) {
        return s;
    }
    bytes = malloc(info.size);
    if (!bytes) {
        return XR_CAPACITY;
    }
    s = xeen_archive_extract(archive, source == XSP_CUR ? XR_BANK_STATE : XR_BANK_ASSETS_XOR35,
                             &info, bytes, info.size, &got);
    if (s == XR_OK) {
        record_sprite(c, slot, reload != 0, source, name, length, info.size, bytes, got);
        *handle = (uint16_t)(slot + 1);
    }
    free(bytes);
    if (s == XR_OK && c->post) {
        c->post(c->user); /* 18964: FB06 -> 483C7 */
    }
    return s;
}

const xeen_sprite *xeen_sprite_get(const xeen_sprite_cache *c, uint16_t handle)
{
    if (!c || !handle || handle > XEEN_SPRITE_SLOTS) {
        return NULL;
    }
    return c->slots[handle - 1u].state == XSP_LOADED ? &c->slots[handle - 1u] : NULL;
}

int xeen_sprite_driver_check(const xeen_sprite_cache *c, uint16_t handle, uint16_t frame)
{
    const xeen_sprite *e;
    if (!c || !handle || handle == 0xffffu || frame == 0xffffu) {
        return 0;
    }
    if (handle > c->last) {
        return 1;
    }
    e = xeen_sprite_get(c, handle);
    return e && e->frames && frame >= e->frames ? 2 : 0;
}

unsigned xeen_sprite_cells(const xeen_sprite_cache *c, uint16_t handle, uint16_t frame)
{
    const xeen_sprite *e = xeen_sprite_get(c, handle);
    if (!e || frame >= e->table_frames) {
        return 0;
    }
    return e->second[frame >> 3] >> (frame & 7u) & 1u ? 2u : 1u;
}

void xeen_select_archive(xeen_movement *m, uint16_t n)
{
    if (!m->ds[XEEN_DS_CLOUDS_INSTALLED]) {
        n = 1;
    }
    if (n == 0) {
        m->ds[XEEN_DS_WORLD] = 0;
    } else if (n == 1) {
        m->ds[XEEN_DS_WORLD] = 1;
    }
}

/* Allocation context: in check mode everything runs on copies and neither
 * the RNG nor the music callback is invoked. */
typedef struct {
    xeen_sprite_cache *cache;
    const xeen_sprite_archives *archives;
    xeen_movement *m;
    xeen_resource_status status;
} alloc_ctx;

/**
 * Loads sprite NAME into the cache unless an earlier step failed; returns its handle (0 on failure,
 * which is kept in the context's status).
 */
static uint16_t load_sprite(alloc_ctx *x, const char *name, uint16_t reload)
{
    uint16_t h = 0;
    if (x->status != XR_OK) {
        return 0;
    }
    x->status = xeen_sprite_load(x->cache, x->archives, x->m->ds[XEEN_DS_WORLD], name, reload, &h);
    return h;
}
/* TEXT copied with its NUL into the original DS name buffer F5E8 (the rest kept; unbounded: every caller passes a
 * name that fits the buffer, as the original's sprintf does), returned from there. */
static const char *ds_name(alloc_ctx *x, const char *text)
{
    size_t n = strlen(text);
    memcpy(x->m->ds + XEEN_DS_FILE_NAME, text, n + 1u);
    return (const char *)x->m->ds + XEEN_DS_FILE_NAME;
}
/** Stores sprite HANDLE in the DS word at ADDRESS unless an earlier step failed. */
static void store_handle(alloc_ctx *x, uint16_t address, uint16_t handle)
{
    if (x->status == XR_OK) {
        xeen_write16(x->m, address, handle);
    }
}

/**
 * Loads the world's global sprites (global.icn, border.icn, spellfx.icn, ...) and stores their
 * handles in the DS words the original keeps them in. The names are the program data's texts in M's DS (the
 * caller's DS must hold the program data: xeen_game_enter_world requires it).
 * Original: mouse.icn 15F8E; the eight resident sets 43C4B..43CC2 and inn.icn 43CE3; dse.fac 2A0A1; main.icn 28D71.
 */
static void enter_world(alloc_ctx *x)
{
    static const uint16_t globals[8] = {XEEN_TEXT_RESIDENT_GLOBAL,  XEEN_TEXT_RESIDENT_BORDER,
                                        XEEN_TEXT_RESIDENT_SPELL_EFFECTS, XEEN_TEXT_RESIDENT_FECP,
                                        XEEN_TEXT_RESIDENT_BLESS,   XEEN_TEXT_RESIDENT_HP_BARS,
                                        XEEN_TEXT_RESIDENT_RESTORE, XEEN_TEXT_RESIDENT_CHARACTER_POWER};
    static const uint16_t words[8] = {XEEN_DS_GLOBAL_SPRITES, XEEN_DS_BORDER_SPRITES, XEEN_DS_SPELLFX_SPRITES,
                                      XEEN_DS_FECP_SPRITES,   XEEN_DS_BLESS_SPRITES,  XEEN_DS_HPBARS_SPRITES,
                                      XEEN_DS_RESTOREX_SPRITES, XEEN_DS_CHARPOW_SPRITES};
    xeen_movement *m = x->m;
    unsigned i, count;
    uint16_t h;
    char name[16];
    x->status = xeen_sprite_reserve_startup(x->cache, &h);
    store_handle(x, XEEN_DS_MOUSE_SPRITES, load_sprite(x, ds_string(m, XEEN_TEXT_MOUSE_SPRITES), 0));
    xeen_sprite_free_from(x->cache, (uint16_t)(xeen_read16(m, XEEN_DS_MOUSE_SPRITES) + 1u));
    for (i = 0; i < 8; ++i) {
        store_handle(x, words[i], load_sprite(x, ds_string(m, globals[i]), 0));
    }
    store_handle(x, XEEN_DS_INN_SPRITES, load_sprite(x, ds_string(m, XEEN_TEXT_INN_SPRITES), 0));
    xeen_sprite_free_from(x->cache, (uint16_t)(xeen_read16(m, XEEN_DS_CHARPOW_SPRITES) + 1u));
    count = m->ds[XEEN_DS_PARTY_COUNT];
    for (i = 0; i < count; ++i) {
        unsigned id = count > 8u ? i : m->ds[(uint16_t)(XEEN_DS_PARTY_MEMBERS + i)];
        snprintf(name, sizeof name, "char%02u.fac", (id + 1u) % 1000u);
        store_handle(x, XEEN_DS_MEMBER_FACE_SPRITE(i), load_sprite(x, name, 0));
    }
    if (count <= 8u) {
        store_handle(x, XEEN_DS_DARKSIDE_FACE_SPRITES, load_sprite(x, ds_string(m, XEEN_TEXT_DARKSIDE_FACES), 0));
    }
    store_handle(x, XEEN_DS_SPRITE_HANDLE, load_sprite(x, ds_string(m, XEEN_TEXT_MAIN_SPRITES), 0));
    if (x->status == XR_OK) {
        xeen_write16(m, XEEN_DS_MAP_SPRITE_BASE, (uint16_t)(xeen_read16(m, XEEN_DS_SPRITE_HANDLE) + 1u));
    }
}

/**
 * Runs a sprite-loading BODY on copies of the cache and the game state first and, only when it
 * succeeds, again on the real ones through COMMIT, so a failing load changes nothing.
 */
static xeen_resource_status run_checked(xeen_sprite_cache *c, const xeen_sprite_archives *a,
                                        xeen_movement *m, void (*body)(alloc_ctx *, void *),
                                        void *arg, void (*commit)(alloc_ctx *, void *))
{
    xeen_sprite_cache *cc = malloc(sizeof *cc);
    xeen_movement *mm = malloc(sizeof *mm);
    alloc_ctx x;
    if (!cc || !mm) {
        free(cc);
        free(mm);
        return XR_CAPACITY;
    }
    *cc = *c;
    *mm = *m;
    x.cache = cc;
    x.archives = a;
    x.m = mm;
    x.status = XR_OK;
    body(&x, arg); /* check-only pass on copies */
    if (x.status == XR_OK) {
        x.cache = c;
        x.m = m;
        commit(&x, arg); /* identical pass with side effects */
    }
    free(cc);
    free(mm);
    return x.status;
}
/** run_checked body that loads the world's global sprites. */
static void enter_body(alloc_ctx *x, void *arg)
{
    (void)arg;
    enter_world(x);
}

xeen_resource_status xeen_sprites_enter_world(xeen_sprite_cache *c, const xeen_sprite_archives *a,
                                              xeen_movement *m)
{
    if (!c || !a || !m || c->last) {
        return XR_BAD_INPUT;
    }
    return run_checked(c, a, m, enter_body, NULL, enter_body);
}

typedef struct {
    const uint8_t *mob;
    const uint8_t *db;
    size_t dbn;
    xeen_entity_random_fn random;
    void *random_user;
    xeen_music_fn music;
    void *music_user;
    int commit;
} map_args;

/** Returns quest flag BIT of the party's flag bytes (DS F01F, most significant bit first). */
static int quest_bit(const xeen_movement *m, unsigned bit)
{ /* 2F847 on F01F */
    return xeen_ds_get_bit(m->ds, XEEN_DS_GAME_FLAGS, bit);
}
/**
 * Records the map's song name in DS 9766 for the music service; refuses a name that does not fit.
 */
static void play_music(alloc_ctx *x, map_args *p, const char *song)
{
    xeen_movement *m = x->m;
    size_t old = 0, n = strlen(song);
    while (old < 16u && m->ds[(uint16_t)(XEEN_DS_SONG_NAME + old)]) {
        ++old;
    }
    if (old > 14u || n > 15u) {
        x->status = XR_BAD_INPUT;
        return;
    }
    memmove(m->ds + XEEN_DS_PREVIOUS_SONG_NAME, m->ds + XEEN_DS_SONG_NAME, old + 1u);
    memcpy(m->ds + XEEN_DS_SONG_NAME, song, n + 1u);
    if (p->commit && p->music) {
        p->music(p->music_user, song, 0xcf);
    }
}
/* An empty sprite handle (FFFFh): a handle word no file fills (the wall and surface tables, record 28). */
#define NO_HANDLE 0xffffu
/*
 * The Darkside's song for a map load: one of its seven songs, 504DA(0, 6) in both branches (the outdoor 1ABF1 into
 * F45C, the indoor 1ADCD into F45E). Drawn on the committing pass only (P->commit); the trial pass draws nothing and
 * takes song 0.
 */
static uint16_t darkside_song_draw(const map_args *p, const xeen_movement *m)
{
    return p->commit ? (uint16_t)(((uint16_t)p->random(p->random_user, m->ds[XEEN_DS_WORLD]) & 0x7fffu) % 7u) : 0u;
}
/*
 * Maps 89..112 and 128..129 (59h..70h, 80h..81h): the time-of-day sky reload 2A0FB always takes the day sky there,
 * in either world (2A119..2A133); the indoor branch's Darkside tail clears record 28 there (1B084..1B0A6).
 */
static int always_day_sky_map(uint8_t map)
{
    return (map >= 89u && map <= 112u) || (map >= 128u && map <= 129u);
}
/* The indoor view records (XEEN_DS_INDOOR_VIEW_RECORD numbers) whose handle the style's wall files fill: the .swl
 * file's 24, the 4.fwl file's 9, the 3.fwl file's 8 and the 1.fwl file's 4 (record 28 later cleared on some maps). */
#define RECORD(n) XEEN_DS_INDOOR_VIEW_RECORD(n)
static const uint16_t swl_records[24] = {RECORD(146), RECORD(144), RECORD(134), RECORD(133), RECORD(110), RECORD(109),
                                         RECORD(108), RECORD(107), RECORD(78),  RECORD(77),  RECORD(76),  RECORD(75),
                                         RECORD(74),  RECORD(73),  RECORD(72),  RECORD(71),  RECORD(33),  RECORD(34),
                                         RECORD(35),  RECORD(36),  RECORD(32),  RECORD(31),  RECORD(30),  RECORD(29)},
                      fwl4_records[9] = {RECORD(45), RECORD(44), RECORD(43), RECORD(42), RECORD(41),
                                         RECORD(40), RECORD(39), RECORD(38), RECORD(37)},
                      fwl3_records[8] = {RECORD(121), RECORD(120), RECORD(119), RECORD(91),
                                         RECORD(90),  RECORD(89),  RECORD(88),  RECORD(87)},
                      fwl1_records[4] = {RECORD(147), RECORD(145), RECORD(143), RECORD(28)};
/* Which view records draw each wall file of the style, in the order the original fills them: the .swl file's, then
 * the 4.fwl, 3.fwl and 1.fwl files' (the 2.fwl handle is drawn through its own slot). */
static const struct {
    const uint16_t *records;
    unsigned count;
    uint16_t handle; /* the DS word holding the file's sprite handle */
} wall_record_sets[4] = {{swl_records, 24, XEEN_DS_INDOOR_SWL_SPRITE},
                         {fwl4_records, 9, XEEN_DS_INDOOR_FWL_SPRITE(4u)},
                         {fwl3_records, 8, XEEN_DS_INDOOR_FWL_SPRITE(3u)},
                         {fwl1_records, 4, XEEN_DS_INDOOR_FWL_SPRITE(1u)}};
/* Loads the map style's sprite file FORMAT ("%s.sky", "%s.gnd", ...: the style's name filled in), named through the
 * DS file-name buffer (F5E8); returns its handle. */
static uint16_t load_style_file(alloc_ctx *x, uint16_t format, uint8_t style)
{
    char name[24];
    snprintf(name, sizeof name, ds_string(x->m, format), far_string(x->m, XEEN_DS_STYLE_NAMES, style));
    return load_sprite(x, ds_name(x, name), 0);
}
/* The indoor load's first stage: the world's sky into F45C (the Darkside's maps 125..127 then read the Clouds
 * archive), the map archive byte, and the style's song (the Clouds': by the style's song group; the Darkside's: one
 * of the group's seven, drawn) unless it is already playing. */
static void load_indoor_sky_and_song(alloc_ctx *x, map_args *p, uint8_t style, uint8_t map)
{
    xeen_movement *m = x->m;
    unsigned group;
    const char *song;
    store_handle(x, 0xf45c, load_sprite(x, far_string(m, XEEN_DS_SKY_NAMES, m->ds[XEEN_DS_WORLD]), 0));
    if (m->ds[XEEN_DS_WORLD] && map >= 125 && map <= 127) {
        xeen_select_archive(m, 0);
    }
    m->ds[XEEN_DS_MAP_ARCHIVE] = m->ds[XEEN_DS_WORLD];
    group = m->ds[(uint16_t)(XEEN_DS_STYLE_SONG_GROUP + style)]; /* style 0..5 (xeen_sprites_map_load) */
    if (!m->ds[XEEN_DS_WORLD]) {
        song = far_string(m, XEEN_DS_CLOUDS_SONGS, group);
    } else {
        uint16_t r = darkside_song_draw(p, m);
        xeen_write16(m, 0xf45e, r); /* F45E holds the draw until the style's .sky replaces it */
        song = far_string(m, XEEN_DS_DARKSIDE_SONGS, 7u * group + r);
    }
    if (strncmp((const char *)m->ds + XEEN_DS_SONG_NAME, song, 16)) {
        play_music(x, p, song);
    }
}
/* The surface sprites of the map's 16 surface kinds into XEEN_DS_SURFACE_SPRITES (kind 0 only for slot 4, else
 * NO_HANDLE); a kind past 15 sets XR_BAD_INDEX and stops. */
static void load_indoor_surfaces(alloc_ctx *x)
{
    xeen_movement *m = x->m;
    unsigned k;
    for (k = 0; k < 16; ++k) {
        uint8_t type = m->ds[(uint16_t)(XEEN_DS_MAP_FIELD(CLASSIFICATION) + k)];
        if (type > 15u) {
            x->status = XR_BAD_INDEX;
            return;
        }
        store_handle(x, (uint16_t)(XEEN_DS_SURFACE_SPRITES + 2u * k),
                     (type || k == 4u) ? load_sprite(x, far_string(m, XEEN_DS_SURFACE_NAMES, type), 0) : NO_HANDLE);
    }
}
/* The style's wall files: slot 0 empty, the four front-wall files 1..4.fwl (the name's digit stepped in place in the
 * file-name buffer) and the side-wall .swl into XEEN_DS_INDOOR_FWL_SPRITE / _SWL_SPRITE. */
static void load_indoor_walls(alloc_ctx *x, uint8_t style)
{
    xeen_movement *m = x->m;
    unsigned k;
    xeen_write16(m, XEEN_DS_WALL_SPRITES, NO_HANDLE);
    store_handle(x, XEEN_DS_INDOOR_FWL_SPRITE(1u), load_style_file(x, XEEN_TEXT_STYLE_FRONT_WALL_FORMAT, style));
    for (k = 0; k < 3; ++k) { /* fixed byte F5ED, whatever the name length */
        ++m->ds[XEEN_DS_FILE_NAME + 5u];
        store_handle(x, (uint16_t)(XEEN_DS_INDOOR_FWL_SPRITE(2u) + 2u * k),
                     load_sprite(x, (const char *)m->ds + XEEN_DS_FILE_NAME, 0));
    }
    store_handle(x, XEEN_DS_INDOOR_SWL_SPRITE, load_style_file(x, XEEN_TEXT_STYLE_SIDE_WALL_FORMAT, style));
}
/* Each wall file's handle into the view records that draw it (wall_record_sets), the ground (F45A) into record 2,
 * and record 28 emptied on Clouds maps 25..27 and on the Darkside's always_day_sky_map maps. */
static void fill_indoor_records(xeen_movement *m, uint8_t map)
{
    unsigned k;
    for (k = 0; k < 4u; ++k) {
        unsigned r;
        for (r = 0; r < wall_record_sets[k].count; ++r) {
            xeen_write16(m, wall_record_sets[k].records[r], xeen_read16(m, wall_record_sets[k].handle));
        }
    }
    xeen_write16(m, RECORD(2), xeen_read16(m, 0xf45a));
    if (!m->ds[XEEN_DS_WORLD]) {
        if (map >= 25 && map <= 27) {
            xeen_write16(m, RECORD(28), NO_HANDLE);
        }
    } else if (always_day_sky_map(map)) {
        xeen_write16(m, RECORD(28), NO_HANDLE);
    }
}
/*
 * Loads an indoor map's sprites and stores their handles where the 3D view reads them: the world's sky (in the
 * Darkside's maps 125..127 from the Clouds archive), the map's song (a Darkside style draws one of its seven songs
 * when committing; the draw is kept in F45E until the style's .sky replaces it) unless already playing, the style's
 * .sky, .gnd and .til, the surface sprites of the map's 16 surface kinds (kind 0 only for slot 4; a kind past 15 is
 * refused), the four .fwl files (the name's digit stepped in place) and the .swl file; then each wall file's handle
 * into the view records that draw it (wall_record_sets), the ground into record 2, and record 28 cleared on Clouds
 * maps 25..27 and Darkside maps 89..112 and 128..129. A failed load leaves x->status set and stops early.
 * Original: the map load's indoor branch 1AD29..1B0AC (F608 clear; the outdoor branch is 1ABC0..1AD26): F45C, F45E,
 * F45A (numeric: the menu and the scene builder reuse these words), the minimap tiles, the surface handles and the
 * wall handles.
 */
static void load_indoor_sprites(alloc_ctx *x, map_args *p)
{
    xeen_movement *m = x->m;
    uint8_t style = m->ds[XEEN_DS_MAP_FIELD(STYLE)], map = m->ds[XEEN_DS_PARTY_MAP];
    load_indoor_sky_and_song(x, p, style, map);
    store_handle(x, 0xf45e, load_style_file(x, XEEN_TEXT_STYLE_SKY_FORMAT, style));
    store_handle(x, 0xf45a, load_style_file(x, XEEN_TEXT_STYLE_GROUND_FORMAT, style));
    store_handle(x, XEEN_DS_MINIMAP_TILES, load_style_file(x, XEEN_TEXT_STYLE_TILES_FORMAT, style));
    load_indoor_surfaces(x);
    if (x->status != XR_OK) {
        return;
    }
    load_indoor_walls(x, style);
    if (x->status != XR_OK) {
        return;
    }
    fill_indoor_records(m, map);
}
#undef RECORD
/*
 * Loads an outdoor map's sprites (the twin of load_indoor_sprites): the song (Clouds: outdoors.m; Darkside: one of
 * its seven songs, darkside_song_draw, kept in F45C until the sky replaces it) unless already playing, the world's
 * sky into F45C, the ground and the minimap tiles; then per wall kind k its "%s.wal" set (XEEN_DS_OUTDOOR_WALL_NAMES
 * through the file-name buffer) into XEEN_DS_WALL_SPRITES[k] and per surface kind k > 0 its sprite into
 * XEEN_DS_SURFACE_SPRITES[k] (kind 0 and absent kinds NO_HANDLE); the outdoor view's records 0 and 1 take the sky,
 * record 2 the ground. A kind byte past the 16-entry tables is refused (the original reads past them).
 * Original: 1ABC0..1AD26 (F608 != 0): DS 2A3C (the skies by world), C0DA (the wall kinds), C0EA (the surfaces).
 */
static void load_outdoor_sprites(alloc_ctx *x, map_args *p)
{
    xeen_movement *m = x->m;
    unsigned k;
    char name[24];
    m->ds[XEEN_DS_MAP_ARCHIVE] = m->ds[XEEN_DS_WORLD];
    if (!m->ds[XEEN_DS_WORLD]) {
        if (strncmp((const char *)m->ds + XEEN_DS_SONG_NAME, ds_string(m, XEEN_TEXT_OUTDOOR_SONG), 16)) {
            play_music(x, p, ds_string(m, XEEN_TEXT_OUTDOOR_SONG));
        }
    } else {
        uint16_t r = darkside_song_draw(p, m);
        xeen_write16(m, 0xf45c, r);
        if (strncmp((const char *)m->ds + XEEN_DS_SONG_NAME, far_string(m, XEEN_DS_DARKSIDE_SONGS, r), 16)) {
            play_music(x, p, far_string(m, XEEN_DS_DARKSIDE_SONGS, r));
        }
    }
    if (x->status != XR_OK) {
        return;
    }
    store_handle(x, 0xf45c, load_sprite(x, far_string(m, XEEN_DS_SKY_NAMES, m->ds[XEEN_DS_WORLD]), 0));
    store_handle(x, 0xf45a, load_sprite(x, ds_string(m, XEEN_TEXT_OUTDOOR_GROUND), 0));
    store_handle(x, XEEN_DS_MINIMAP_TILES, load_sprite(x, ds_string(m, XEEN_TEXT_OUTDOOR_TILES), 0));
    if (x->status != XR_OK) {
        return;
    }
    /* the outdoor view's records 0 and 1 the sky, 2 the ground */
    xeen_write16(m, XEEN_DS_OUTDOOR_VIEW_RECORD(1u), xeen_read16(m, 0xf45c));
    xeen_write16(m, XEEN_DS_OUTDOOR_VIEW_RECORD(0u), xeen_read16(m, 0xf45c));
    xeen_write16(m, XEEN_DS_OUTDOOR_VIEW_RECORD(2u), xeen_read16(m, 0xf45a));
    for (k = 0; k < 16; ++k) {
        uint8_t wall = m->ds[(uint16_t)(XEEN_DS_MAP_FIELD(WALL_CLASSIFICATION) + k)],
                surface = m->ds[(uint16_t)(XEEN_DS_MAP_FIELD(CLASSIFICATION) + k)];
        if (wall > 15u || (k && surface > 15u)) {
            x->status = XR_BAD_INDEX;
            return;
        }
        if (wall) {
            snprintf(name, sizeof name, ds_string(m, XEEN_TEXT_OUTDOOR_WALL_FORMAT),
                     far_string(m, XEEN_DS_OUTDOOR_WALL_NAMES, wall));
            store_handle(x, (uint16_t)(XEEN_DS_WALL_SPRITES + 2u * k), load_sprite(x, ds_name(x, name), 0));
        } else {
            xeen_write16(m, (uint16_t)(XEEN_DS_WALL_SPRITES + 2u * k), NO_HANDLE);
        }
        if (k && surface) {
            store_handle(x, (uint16_t)(XEEN_DS_SURFACE_SPRITES + 2u * k),
                         load_sprite(x, far_string(m, XEEN_DS_SURFACE_NAMES, surface), 0));
        } else {
            xeen_write16(m, (uint16_t)(XEEN_DS_SURFACE_SPRITES + 2u * k), NO_HANDLE);
        }
        if (x->status != XR_OK) {
            return;
        }
    }
}
/*
 * The object sprite of MOB slot I (its id not FFh) from the map's object archive into the object handles: "%03u.obj"
 * ("%03u.0bj" for ids 100 and above). Two Clouds exceptions: after the endgame, object 85 on map 27 gets no handle
 * (FFFFh, then overwritten by a load of the file-name buffer as it stands: the previous resource's name) and the
 * two DS bytes the original sets there; on map 12 with quest flag 43, object 118 is shown as 085 (and DS E004 notes
 * 85). Original: 1A84B (the endgame case 1A87E..1A88E, then JMP 1A909; ds-write: F3F8 FFFFh at 1A888, then 24h at
 * 1A917).
 */
static void load_object_sprite(alloc_ctx *x, const map_args *p, unsigned i, uint8_t world)
{
    xeen_movement *m = x->m;
    uint8_t id = p->mob[XEEN_MOB_OBJECT_SPRITES + i];
    char name[24];
    xeen_select_archive(m, xeen_read16(m, XEEN_DS_MAP_OBJECT_ARCHIVE));
    if (xeen_read16(m, XEEN_DS_ENDGAME_REACHED) && id == 85u && m->ds[XEEN_DS_PARTY_MAP] == 27 && !world) {
        m->ds[0xe0ec] = 0;
        m->ds[0xe0ea] = 8;
        xeen_write16(m, (uint16_t)(XEEN_DS_OBJECT_SPRITES + 2u * i), 0xffff);
    } else if (m->ds[XEEN_DS_PARTY_MAP] == 12 && quest_bit(m, 43) && id == 118u && !world) {
        ds_name(x, ds_string(m, XEEN_TEXT_OBJECT_085_FILE));
        m->ds[0xe004] = 85; /* object sprite 118 shown as 085 */
    } else {
        snprintf(name, sizeof name, ds_string(m, XEEN_TEXT_OBJECT_SPRITE_FORMAT), id, id < 100u ? 'o' : '0');
        ds_name(x, name);
    }
    store_handle(x, (uint16_t)(XEEN_DS_OBJECT_SPRITES + 2u * i),
                 load_sprite(x, (const char *)m->ds + XEEN_DS_FILE_NAME, 0));
}
/* The original's table of each monster sprite's first MOB slot is 94 bytes on its stack: a sprite id past it would
 * alias other stack data, so the native load refuses it (XR_BAD_INDEX). */
#define SHARED_SPRITE_IDS 94u
/*
 * Stores the handle of MOB slot I in TABLE (the monster or the attack sprites): when an earlier slot, SHARED
 * (1-based), uses the same sprite, that slot's handle, else NAME loaded. NAME is written to the file-name buffer
 * either way (before the load, or after the shared handle is stored).
 */
static void store_monster_sprite(alloc_ctx *x, uint16_t table, unsigned i, uint8_t shared, const char *name)
{
    xeen_movement *m = x->m;
    store_handle(x, (uint16_t)(table + 2u * i),
                 shared ? xeen_read16(m, (uint16_t)(table - 2u + 2u * shared)) : load_sprite(x, ds_name(x, name), 0));
    if (shared) {
        ds_name(x, name);
    }
}
/*
 * The monster of MOB slot I (its id not FFh): its record in the monster database gives its sprite number, whose body
 * "%03u.mon" and attack "%03u.att" are loaded from the map's monster archive (the Clouds archive for monster 91 when
 * the Clouds are installed), or shared with the first slot using that sprite (FIRST_SLOT, 1-based, by sprite id).
 * A record past the database is XR_NOT_FOUND; a sprite id of SHARED_SPRITE_IDS or more XR_BAD_INDEX.
 * Original: 1A929.
 */
static void load_monster_sprites(alloc_ctx *x, const map_args *p, unsigned i, uint8_t first_slot[SHARED_SPRITE_IDS])
{
    xeen_movement *m = x->m;
    uint8_t id = p->mob[XEEN_MOB_MONSTER_SPRITES + i], sprite;
    char name[24];
    xeen_select_archive(m, (id == 91u && m->ds[XEEN_DS_CLOUDS_INSTALLED])
                                   ? 0u
                                   : xeen_read16(m, XEEN_DS_MAP_MONSTER_ARCHIVE));
    if ((size_t)id * XEEN_MONSTER_RECORD_BYTES + XEEN_MONSTER_BOUNCE > p->dbn) {
        x->status = XR_NOT_FOUND;
        return;
    }
    sprite = p->db[(size_t)id * XEEN_MONSTER_RECORD_BYTES + XEEN_MONSTER_SPRITE];
    if (sprite >= SHARED_SPRITE_IDS) {
        x->status = XR_BAD_INDEX;
        return;
    }
    snprintf(name, sizeof name, ds_string(m, XEEN_TEXT_MONSTER_SPRITE_FORMAT), sprite);
    store_monster_sprite(x, XEEN_DS_MONSTER_SPRITES, i, first_slot[sprite], name);
    snprintf(name, sizeof name, ds_string(m, XEEN_TEXT_MONSTER_ATTACK_FORMAT), sprite);
    store_monster_sprite(x, XEEN_DS_MONSTER_ATTACK_SPRITES, i, first_slot[sprite], name);
    first_slot[sprite] = (uint8_t)(i + 1u);
}
/* The wall item of MOB slot I (its id not FFh): "%03u.pic" from the map's wall-item archive. Original: 1AB46. */
static void load_wall_item_sprite(alloc_ctx *x, const map_args *p, unsigned i)
{
    xeen_movement *m = x->m;
    char name[24];
    xeen_select_archive(m, xeen_read16(m, XEEN_DS_MAP_WALL_ITEM_ARCHIVE));
    snprintf(name, sizeof name, ds_string(m, XEEN_TEXT_WALL_ITEM_SPRITE_FORMAT), p->mob[XEEN_MOB_WALL_SPRITES + i]);
    store_handle(x, (uint16_t)(XEEN_DS_WALL_ITEM_SPRITES + 2u * i), load_sprite(x, ds_name(x, name), 0));
}
/*
 * Reloads the sky sprite (its handle in DS F45C) for the time of day: the night sky before 300 or from 1260 minutes,
 * recorded in DS 8C92 (the night flag), except on maps 89..112 and 128..129, which always take the day sky.
 */
static void load_time_of_day_sky(alloc_ctx *x)
{
    xeen_movement *m = x->m;
    uint16_t clock = xeen_read16(m, XEEN_DS_MINUTE_OF_DAY);
    uint16_t night = (clock < XEEN_NIGHT_ENDS || clock >= XEEN_NIGHT_BEGINS) ? 1u : 0u;
    uint8_t map = m->ds[XEEN_DS_PARTY_MAP];
    xeen_write16(m, XEEN_DS_NIGHT, night);
    if (always_day_sky_map(map)) {
        night = 0;
    }
    (void)load_sprite(x, far_string(m, XEEN_DS_SKY_NAMES, night), xeen_read16(m, 0xf45c));
}
/**
 * Loads the sprites of a newly entered map (the 1A7AA path), in the original's order: frees the previous map's
 * sprites (from the map's sprite base) and marks its four handle tables empty (FFFFh); names the map's MOB in the
 * file-name buffer; for each of the 16 MOB slots, its object, its monster's body and attack, its wall item; then the
 * world's archive and the outdoor or indoor sprite sets; the time of day's sky; the world's archive again. The first
 * failure ends the load (its status kept); P->commit selects the trial or the committing pass (run_checked).
 */
static void load_map_sprites(alloc_ctx *x, void *arg)
{
    map_args *p = arg;
    xeen_movement *m = x->m;
    uint8_t world = m->ds[XEEN_DS_WORLD], first_slot[SHARED_SPRITE_IDS];
    unsigned i;
    char name[24];
    xeen_sprite_free_from(x->cache, xeen_read16(m, XEEN_DS_MAP_SPRITE_BASE));
    memset(m->ds + XEEN_DS_MONSTER_SPRITES, 0xff, 0x20);
    memset(m->ds + XEEN_DS_MONSTER_ATTACK_SPRITES, 0xff, 0x20);
    memset(m->ds + XEEN_DS_OBJECT_SPRITES, 0xff, 0x20);
    memset(m->ds + XEEN_DS_WALL_ITEM_SPRITES, 0xff, 0x20);
    /* 1A809..1A827: the map's MOB is reloaded through F5E8 ("maze%c%03u.mob";
     * the later sprite names are shorter and leave its tail) and 4A877
     * stores its size in 8582. */
    snprintf(name, sizeof name, ds_string(m, XEEN_TEXT_MAP_MOB_FILE), m->ds[XEEN_DS_PARTY_MAP] > 99u ? 'x' : '0',
             (unsigned)m->ds[XEEN_DS_PARTY_MAP]);
    memcpy(m->ds + XEEN_DS_FILE_NAME, name, strlen(name) + 1u);
    memset(first_slot, 0, sizeof first_slot);
    for (i = 0; i < 16 && x->status == XR_OK; ++i) {
        if (p->mob[XEEN_MOB_OBJECT_SPRITES + i] != 0xff) {
            load_object_sprite(x, p, i, world);
        }
        if (p->mob[XEEN_MOB_MONSTER_SPRITES + i] != 0xff && x->status == XR_OK) {
            load_monster_sprites(x, p, i, first_slot);
        }
        if (p->mob[XEEN_MOB_WALL_SPRITES + i] != 0xff && x->status == XR_OK) {
            load_wall_item_sprite(x, p, i);
        }
    }
    if (x->status != XR_OK) {
        return;
    }
    xeen_select_archive(m, world);
    if (xeen_read16(m, XEEN_DS_OUTDOORS)) {
        load_outdoor_sprites(x, p);
    } else {
        load_indoor_sprites(x, p);
    }
    if (x->status != XR_OK) {
        return;
    }
    load_time_of_day_sky(x);
    if (x->status == XR_OK) {
        xeen_select_archive(m, world);
    }
}
/** run_checked trial pass of load_map_sprites. */
static void map_check(alloc_ctx *x, void *arg)
{
    map_args *p = arg;
    p->commit = 0;
    load_map_sprites(x, p);
}
/** run_checked committing pass of load_map_sprites. */
static void map_commit(alloc_ctx *x, void *arg)
{
    map_args *p = arg;
    p->commit = 1;
    load_map_sprites(x, p);
}

xeen_resource_status xeen_sprites_map_load(xeen_sprite_cache *c, const xeen_sprite_archives *a,
                                           xeen_movement *m, const uint8_t mob_header[48],
                                           const uint8_t *database, size_t database_size,
                                           xeen_entity_random_fn random, void *random_user,
                                           xeen_music_fn music, void *music_user)
{
    map_args p;
    if (!c || !a || !m || !mob_header || !database || !random) {
        return XR_BAD_INPUT;
    }
    if ((!xeen_read16(m, XEEN_DS_OUTDOORS) && m->ds[XEEN_DS_MAP_FIELD(STYLE)] > XEEN_MAP_STYLE_SCIFI) ||
        !xeen_read16(m, XEEN_DS_MAP_SPRITE_BASE)) {
        return XR_BAD_INPUT;
    }
    p.mob = mob_header;
    p.db = database;
    p.dbn = database_size;
    p.random = random;
    p.random_user = random_user;
    p.music = music;
    p.music_user = music_user;
    p.commit = 0;
    return run_checked(c, a, m, map_check, &p, map_commit);
}

/** run_checked body of the sky reload (load_time_of_day_sky). */
static void reload_sky_body(alloc_ctx *x, void *arg)
{
    (void)arg;
    load_time_of_day_sky(x);
}

xeen_resource_status xeen_sprites_reload_sky(xeen_sprite_cache *c, const xeen_sprite_archives *a,
                                             xeen_movement *m)
{
    if (!c || !a || !m || !xeen_read16(m, 0xf45c)) {
        return XR_BAD_INPUT;
    }
    return run_checked(c, a, m, reload_sky_body, NULL, reload_sky_body);
}
