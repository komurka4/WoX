/*
 * A map's resources (Phase 246, from game_lifecycle.c): the map's case (19F31's world, monster and picture
 * files), xeen_game_prepare_map_objects (the quest objects, 1A1B0's map text, 19F31's selectors),
 * xeen_game_prepare_monsters (1A3A9's aliased table), the scene resources, the program data, the intro and
 * other archives, the sprite hooks and archives, the world entry, the music requests and
 * xeen_game_prepare_map_sprites.
 */
#include "game_internal.h"
#include "xeen/monster_record.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* 19F31's resource case (all from archive 1, 191DE(1)): Clouds (96E2 0)
 * 1A071; Darkside maps 71h..80h by CS:1416: 71h..74h and 80h 19F89,
 * 75h..78h and 7Ch 19FC4, 7Dh..7Fh 1A001, 79h..7Bh and every other
 * Darkside map 1A03D. sel = 8C4C (wall items), 8C4E (objects), 8C50
 * (monsters): the archive 1A84B/1A929/1AB46 and 38FD0 select. */
typedef struct {
    uint16_t table, mon, pic; /* the files' names: DS texts (read with ds_text) */
    uint16_t sel[3];
} xeen_map_case;
/**
 * The current map's case of files: Clouds every map from clouds.dat, xeen.mon and xeenpic.dat; Darkside its own
 * set, but for the special maps 113..128 (71h..80h), which mix the worlds' files by the original's table. The names
 * are the program data's (DS 4E96..4ECB, read live: the xeen_game_prepare_* entries require it).
 * Original: the map load's cases 1A071, 1A03D, 19F89, 19FC4 and 1A001 (CS:1416).
 */
static const xeen_map_case *map_case(const xeen_game *g)
{
    static const xeen_map_case cases[5] = {
            {XEEN_TEXT_CLOUDS_ANIMATION_FILE, XEEN_TEXT_CLOUDS_MONSTER_FILE, XEEN_TEXT_CLOUDS_PICTURE_FILE,
             {0, 0, 0}}, /* 1A071 */
            {XEEN_TEXT_DARKSIDE_ANIMATION_FILE, XEEN_TEXT_DARKSIDE_MONSTER_FILE, XEEN_TEXT_DARKSIDE_PICTURE_FILE,
             {1, 1, 1}}, /* 1A03D */
            {XEEN_TEXT_CLOUDS_ANIMATION_FILE, XEEN_TEXT_DARKSIDE_MONSTER_FILE, XEEN_TEXT_DARKSIDE_PICTURE_FILE,
             {1, 0, 1}}, /* 19F89 */
            {XEEN_TEXT_CLOUDS_ANIMATION_FILE, XEEN_TEXT_CLOUDS_MONSTER_FILE, XEEN_TEXT_DARKSIDE_PICTURE_FILE,
             {1, 0, 0}}, /* 19FC4 */
            {XEEN_TEXT_CLOUDS_ANIMATION_FILE, XEEN_TEXT_DARKSIDE_MONSTER_FILE, XEEN_TEXT_CLOUDS_PICTURE_FILE,
             {0, 0, 1}}}; /* 1A001 */
    static const uint8_t special[16] = {2, 2, 2, 2, 3, 3, 3, 3,
                                        1, 1, 1, 3, 4, 4, 4, 2}; /* CS:1416, maps 71h..80h */
    if (!g || !g->world) {
        return &cases[0];
    }
    return &cases[g->map_id >= 113u && g->map_id <= 128u ? special[g->map_id - 113u] : 1];
}
/* The Clouds quest objects that a set game flag hides on its map (each moved to 80h, 80h): the rules are tested in
 * order and the first that applies ends the test, so on map 0Fh flag 42h hides object 11 and flag 45h's object 14
 * then stays (an original quirk). The objects of completed quests stay gone on every later load of the map (found
 * by a random differential: genuine slot 4, flag 44h, map 10h; xeen_dat_evidence/phase162).
 * Original: 19E82's tail 1A65F..1A7A6 ([BP-8] = DS 94 as 1A0C0 read it after 191DE(96E2); 2F847(n, F01F); 1A7A6). */
static void hide_quest_objects(xeen_movement *m)
{
    static const uint8_t rule[][3] = {/* flag, map, object */
                                      {0x3f, 0x07, 5},  {0x27, 0x04, 6},  {0x40, 0x0c, 11},
                                      {0x41, 0x0b, 10}, {0x42, 0x0f, 11}, {0x43, 0x0a, 9},
                                      {0x44, 0x10, 9},  {0x45, 0x0f, 14}, {0x31, 0x13, 14}};
    size_t i;
    if (m->ds[XEEN_DS_WORLD]) {
        return;
    }
    for (i = 0; i < sizeof rule / sizeof rule[0]; ++i) {
        unsigned f = rule[i][0];
        if (xeen_ds_get_bit(m->ds, XEEN_DS_GAME_FLAGS, f) &&
            m->ds[XEEN_DS_PARTY_MAP] == rule[i][1]) {
            uint16_t a = (uint16_t)(XEEN_DS_MAP_OBJECTS + rule[i][2] * XEEN_MAP_OBJECT_BYTES);
            m->ds[(uint16_t)(a + XEEN_MAP_OBJECT_Y)] = 0x80;
            m->ds[a + XEEN_MAP_OBJECT_X] = 0x80; /* y, then x */
            return;
        }
    }
}
/* Loads the map's text file ("darkNNN.txt" or "xeenNNN.txt" with x for a map above 99, compared signed, from the
 * runtime archive) into the map texts' DS area, its size as the loaded file's. Returns XR_BAD_INPUT when the DS format
 * does not take these arguments (ds_format), XR_CAPACITY when the file would pass the end of DS, else the lookup's or
 * the read's status.
 * Original: 1A1B0..1A20D (DS 4EE3, the 9A/9E names; into DS 8C53; 4A877's 8582). */
static xeen_resource_status map_text_load(xeen_game *g, const xeen_archive *runtime)
{
    xeen_resource_status s;
    xeen_resource_info ti;
    char tname[24];
    const char *format = ds_format(g, XEEN_TEXT_MAP_TEXT_FILE_FORMAT, "%s%c%03u");
    if (!format) {
        return XR_BAD_INPUT;
    }
    (void)snprintf(tname, sizeof tname, format,
                   ds_text(g, g->world ? XEEN_TEXT_DARKSIDE_PREFIX : XEEN_TEXT_CLOUDS_PREFIX),
                   (int16_t)g->map_id > 99 ? 'x' : '0', (unsigned)g->map_id);
    s = xeen_archive_find_name(runtime, tname, &ti);
    if (s == XR_OK && ti.size > 0x10000u - XEEN_DS_MAP_TEXTS) {
        s = XR_CAPACITY;
    }
    if (s == XR_OK) {
        s = xeen_archive_extract(runtime, XR_BANK_ASSETS_XOR35, &ti, g->movement.ds + XEEN_DS_MAP_TEXTS, ti.size, NULL);
    }
    if (s == XR_OK) {
        xeen_write16(&g->movement, XEEN_DS_LOADED_FILE_SIZE, (uint16_t)ti.size);
    }
    return s;
}
/* The map's case of archives: its wall-item, object and monster archive selectors, the Town Portal side (the map
 * side, but 0 for the special maps 113..127), the map side's archive selected, the quest objects hidden (Clouds) and
 * the effect tables set; the map's objects then count as prepared.
 * Original: 19F31..19F53's selectors and FB00 (map not 71h..7Fh); 1A0AE's select. */
static void map_selectors(xeen_game *g, const xeen_map_case *c)
{
    xeen_write16(&g->movement, XEEN_DS_MAP_WALL_ITEM_ARCHIVE, c->sel[0]);
    xeen_write16(&g->movement, XEEN_DS_MAP_OBJECT_ARCHIVE, c->sel[1]);
    xeen_write16(&g->movement, XEEN_DS_MAP_MONSTER_ARCHIVE, c->sel[2]);
    g->movement.ds[XEEN_DS_TOWN_PORTAL_SIDE] =
            g->map_id >= 113u && g->map_id <= 127u ? 0 : g->movement.ds[XEEN_DS_MAP_SIDE];
    xeen_select_archive(&g->movement, g->movement.ds[XEEN_DS_MAP_SIDE]);
    hide_quest_objects(&g->movement);
    (void)xeen_scene_init_effect_tables(&g->movement);
    g->objects_prepared = 1;
}
/* The current map's .mob file name in the state archive: "maze0NNN.mob", or "mazexNNN.mob" above map 99, from the
 * DS format. Returns 0 (NAME empty) when the format does not take these arguments (ds_format).
 * Original: 1A210..1A22A (DS 4EF0 into F5E8). */
static int map_mob_name(const xeen_game *g, char name[24])
{
    const char *format = ds_format(g, XEEN_TEXT_MAP_MOB_FILE, "%c%03u");
    name[0] = 0;
    if (!format) {
        return 0;
    }
    (void)snprintf(name, 24, format, g->map_id > 99u ? 'x' : '0', (unsigned)g->map_id);
    return 1;
}
/* A map's three object files: its .mob (the state archive's), the world's animation table (1,452 bytes) and the
 * map case's picture frames (53 bytes), the last two from the runtime archive. */
typedef struct {
    xeen_resource_info mob_info, animation_info, frames_info;
    uint8_t *mob;
    size_t mob_size, animation_size, frames_size;
    uint8_t animation[1452], frames[53];
} map_object_files;
/* Finds the map's object files and checks the fixed sizes, before anything is read: the picture frames, the .mob,
 * the animation table. Returns the first failing status. */
static xeen_resource_status find_map_object_files(xeen_game *g, const xeen_archive *runtime, const xeen_map_case *c,
                                                  map_object_files *f)
{
    xeen_resource_status s;
    char name[24];
    if (!map_mob_name(g, name)) {
        return XR_BAD_INPUT;
    }
    if ((s = xeen_archive_find_named(runtime, ds_text(g, c->pic), 11, &f->frames_info)) != XR_OK) {
        return s;
    }
    if (f->frames_info.size != sizeof f->frames) {
        return XR_BAD_INPUT;
    }
    if ((s = xeen_archive_find_name(&g->state, name, &f->mob_info)) != XR_OK) {
        return s;
    }
    if ((s = xeen_archive_find_name(runtime, ds_text(g, c->table), &f->animation_info)) != XR_OK) {
        return s;
    }
    return f->animation_info.size != sizeof f->animation ? XR_BAD_INPUT : XR_OK;
}
/* Reads the three files found (the .mob into F's buffer of its size). Returns the first failing status. */
static xeen_resource_status read_map_object_files(xeen_game *g, const xeen_archive *runtime, map_object_files *f)
{
    xeen_resource_status s;
    s = xeen_archive_extract(&g->state, XR_BANK_STATE, &f->mob_info, f->mob, f->mob_info.size, &f->mob_size);
    if (s == XR_OK) {
        s = xeen_archive_extract(runtime, XR_BANK_ASSETS_XOR35, &f->animation_info, f->animation,
                                 sizeof f->animation, &f->animation_size);
    }
    if (s == XR_OK) {
        s = xeen_archive_extract(runtime, XR_BANK_ASSETS_XOR35, &f->frames_info, f->frames, sizeof f->frames,
                                 &f->frames_size);
    }
    return s;
}
/* Installs the files into DS, in order, each step only after the last succeeded: the map's objects (the object helper
 * validates the whole input before any write), the animation table and the wall frames (their sizes exact), the
 * objects' orientation, and the map's text file. Returns the first failing status. */
static xeen_resource_status install_map_objects(xeen_game *g, const xeen_archive *runtime, const map_object_files *f)
{
    xeen_resource_status s = xeen_entities_objects(f->mob, f->mob_size, &g->movement, NULL);
    if (s == XR_OK) {
        s = xeen_entities_animation(f->animation, f->animation_size, &g->movement);
    }
    if (s == XR_OK) {
        s = xeen_entities_wall_frames(f->frames, f->frames_size, &g->movement);
    }
    if (s == XR_OK) {
        s = xeen_entities_orientation(&g->movement);
    }
    if (s == XR_OK) {
        s = map_text_load(g, runtime);
    }
    return s;
}
/*
 * Prepares the map's objects in a restored, unused navigation session, from RUNTIME_ASSETS (the archive holding the
 * world tables: the combined installation's DARK.CC holds both worlds'): the map's case of files (map_case), its
 * object files found and size-checked, read and installed (objects, animation, wall frames, orientation, the map
 * text), then the case's archive selectors, the quest objects hidden and the effect tables (map_selectors). Returns
 * XR_OK, or the first failing status (XR_BAD_INPUT for a session that cannot take it or a wrong size, XR_CAPACITY
 * without memory); the selectors are set only after every step succeeded.
 * Original: 19F31's case and selectors; the .mob, the animation and the picture files of 19E82's load; 1A1B0's
 * text (Clouds maps 41 and outdoor 12 take the world-zero 1A071 case as maps 28 and 49 do; CS:1416 sends Darkside
 * maps 79h..7Bh to the default 1A03D case, as every other Darkside map, with only FB00 = 0 (19F49) different).
 */
xeen_resource_status xeen_game_prepare_map_objects(xeen_game *g, const uint8_t *runtime_assets,
                                                   size_t runtime_size)
{
    map_object_files f = {0};
    xeen_resource_status s;
    xeen_archive runtime;
    const xeen_map_case *c = map_case(g);
    if (!g || !g->program_data || !g->restored || g->acted || g->objects_prepared || g->phase != XG_NAVIGATION) {
        return XR_BAD_INPUT;
    }
    if ((s = xeen_archive_open(&runtime, runtime_assets, runtime_size)) != XR_OK) {
        return s;
    }
    if ((s = find_map_object_files(g, &runtime, c, &f)) != XR_OK) {
        return s;
    }
    if (!(f.mob = malloc(f.mob_info.size ? f.mob_info.size : 1u))) {
        return XR_CAPACITY;
    }
    s = read_map_object_files(g, &runtime, &f);
    if (s == XR_OK) {
        s = install_map_objects(g, &runtime, &f);
    }
    free(f.mob);
    if (s == XR_OK) {
        map_selectors(g, c);
    }
    return s;
}
/*
 * The map's monsters and wall items installed from the monster database DATABASE (the file's *DN bytes, room for
 * 15,360) for the .mob MOB: every record reads its monster's row, inside the file or not. Past the file the original's
 * table shares memory with the event buffer, modelled here: the event index's entries leave an 80h fill from
 * 1C7Eh + 10 x the record count, so a row wholly past the entries reads 80h bytes; rows between the file's end and
 * there (the image's own bytes, an earlier world's table, the entries) are refused. On success *DN is the size the
 * session keeps: the whole table when a record reads past the file. Clouds maps 49, 71, 73 and 74 keep one off-grid
 * record of an FFh sprite slot (row FFh).
 * Original: 1A3A9 (row id x 60 of 50D4:0000); 2C88D (1A0C6) runs 2C6CE, filling 50D4:1C7E..5F7B with 80h and one
 * 10-byte entry per event record from 1C7E (measured at 50D4:3BC4 in original bytes, phase94/row255_probe.py).
 */
static xeen_resource_status monster_table(xeen_game *g, const uint8_t *mob, size_t mn, uint8_t database[15360],
                                          size_t *dn, xeen_entity_random_fn random, void *user)
{
    const size_t whole = 15360;
    size_t alias = 0x1c7eu + 10u * (size_t)g->index.record_count;
    xeen_resource_status status;
    memset(database + *dn, 0, whole - *dn);
    if (alias < whole) {
        memset(database + alias, 0x80, whole - alias);
    }
    status = xeen_entities_monsters_walls_aliased(mob, mn, database, *dn, alias, whole, &g->movement, random, user);
    if (status == XR_OK) {
        size_t size = *dn;
        for (unsigned k = 0; k < g->movement.ds[XEEN_DS_MAP_MONSTER_COUNT]; ++k) {
            size_t id = g->movement.ds[(uint16_t)(XEEN_MAP_MONSTER(k) + XEEN_MAP_MONSTER_ID)];
            if (id * XEEN_MONSTER_RECORD_BYTES + XEEN_MONSTER_RECORD_BYTES > *dn) {
                size = whole;
            }
        }
        *dn = size;
    }
    return status;
}
/*
 * Prepares the map's monsters once its objects are prepared (and the world entered, unless REFERENCES gives the
 * attack sprites' handles): the map's .mob from the state archive and the world's monster database (xeen.mon or
 * dark.mon by the map's case, at most 15,360 bytes) from RUNTIME_ASSETS; the map's monsters and wall items installed
 * from them (monster_table, with RANDOM for their draws); the session keeps the database (the whole aliased table
 * when a record reads past the file); REFERENCES' 32 handles become the attack sprites; the scene's monster tables
 * set. Returns XR_OK, or the first failing status (XR_BAD_INPUT for a session that cannot take it or an unopenable
 * archive, XR_CAPACITY for a database too large or no memory).
 * Original: 1A3A9's records and table; 1A344's monster initialization.
 */
xeen_resource_status xeen_game_prepare_monsters(xeen_game *g, const uint8_t *runtime_assets,
                                                size_t runtime_size, xeen_entity_random_fn random,
                                                void *user, const uint16_t references[32])
{
    xeen_archive archive;
    xeen_resource_info mi, di;
    xeen_resource_status status;
    uint8_t *mob;
    uint8_t database[15360];
    size_t mn = 0, dn = 0;
    unsigned i;
    char name[24];
    const char *db;
    if (!g || !g->program_data || !g->restored || !g->objects_prepared || g->acted || g->monsters_prepared ||
        !random) {
        return XR_BAD_INPUT;
    }
    if (!references && !g->world_entered) {
        return XR_BAD_INPUT;
    }
    if (xeen_archive_open(&archive, runtime_assets, runtime_size) != XR_OK) {
        return XR_BAD_INPUT;
    }
    db = ds_text(g, map_case(g)->mon);
    if (!map_mob_name(g, name)) {
        return XR_BAD_INPUT;
    }
    status = xeen_archive_find_name(&g->state, name, &mi);
    if (status != XR_OK) {
        return status;
    }
    status = xeen_archive_find_name(&archive, db, &di);
    if (status != XR_OK) {
        return status;
    }
    if (di.size > sizeof database) {
        return XR_CAPACITY;
    }
    mob = malloc(mi.size ? mi.size : 1u);
    if (!mob) {
        return XR_CAPACITY;
    }
    status = xeen_archive_extract(&g->state, XR_BANK_STATE, &mi, mob, mi.size, &mn);
    if (status == XR_OK) {
        status = xeen_archive_extract(&archive, XR_BANK_ASSETS_XOR35, &di, database,
                                      sizeof database, &dn);
    }
    if (status == XR_OK) {
        status = monster_table(g, mob, mn, database, &dn, random, user);
    }
    free(mob);
    if (status != XR_OK) {
        return status;
    }
    memcpy(g->monster_database, database, dn);
    g->monster_database_size = dn;
    if (references) {
        for (i = 0; i < 32; i++) {
            xeen_write16(&g->movement, (uint16_t)(XEEN_DS_MONSTER_ATTACK_SPRITES + i * 2u), references[i]);
        }
    }
    (void)xeen_scene_init_monster_tables(&g->movement);
    g->monsters_prepared = 1;
    return XR_OK;
}
int xeen_game_load_program_data(xeen_game *g)
{
    /* The compiled initialized DS image (src/data), below BSS 8C4C.
     * The native's own earlier writes there (the movement tables of
     * xeen_movement_init, the startup's) must agree with the image. */
    const uint8_t *image = xeen_program_ds_image;
    unsigned i;
    if (!g || g->restored || g->acted || g->program_data) {
        return 0;
    }
    for (i = 0; i < XEEN_PROGRAM_DS_SIZE; i++) {
        if (g->movement.ds[i] && g->movement.ds[i] != image[i]) {
            return 0;
        }
    }
    memcpy(g->movement.ds, image, XEEN_PROGRAM_DS_SIZE);
    g->program_data = 1;
    return 1;
}
int xeen_game_bind_intro_archive(xeen_game *g, const uint8_t *bytes, size_t size)
{
    if (!g || g->intro_bound || !bytes || xeen_archive_open(&g->intro, bytes, size) != XR_OK) {
        return 0;
    }
    g->intro_bound = 1;
    return 1;
}
int xeen_game_bind_other_archive(xeen_game *g, const uint8_t *bytes, size_t size)
{
    if (!g || g->restored || g->other_bound || !bytes ||
        xeen_archive_open(&g->other, bytes, size) != XR_OK) {
        return 0;
    }
    g->other_bound = 1;
    return 1;
}
/** Sprite-load hook before a load: the CD-audio preload step. */
static void sprite_pre(void *u)
{
    cd_preload(u);
}
/** Sprite-load hook after a load: the CD-audio postload step. */
static void sprite_post(void *u)
{
    cd_postload(u);
}
/* Entry K's offset (contract in game_internal.h). */
size_t nul_offset(const uint8_t *bytes, size_t size, unsigned k)
{
    size_t i;
    if (!k) {
        return 0;
    }
    for (i = 0; i < size; ++i) {
        if (!bytes[i] && !--k) {
            return i + 1u;
        }
    }
    return (size_t)-1;
}
/* Entry K's text (contract in game_internal.h): the original reads the entries through the offsets 2B632 builds
 * (the 50D4:165C words). */
const char *nul_entry(const uint8_t *bytes, size_t size, unsigned k)
{
    size_t at = nul_offset(bytes, size, k), i;
    if (at == (size_t)-1) {
        return NULL; /* an offset the file never reaches */
    }
    for (i = at; i < size; ++i) {
        if (!bytes[i]) {
            return (const char *)bytes + at;
        }
    }
    return NULL; /* the text runs past the file */
}
/*
 * The sprite loads and the sprite handle stack (ds_globals.h: XEEN_DS_SPRITE_HANDLE, _HANDLE_STACK, _HANDLE_DEPTH).
 * sprite_load is 18931 (18935 / 18964 through the archive set's hooks). handle_push is 190A3: the depth incremented,
 * the current handle saved in its slot, NAME loaded and made current; when the load fails the push stays (its
 * callers stop at their own sites, some after undoing the depth). handle_pop is 190CF: the current handle freed,
 * the saved one restored, the depth decremented.
 */
int sprite_load(xeen_game *g, const char *name, uint16_t *handle)
{
    xeen_sprite_archives archives = sprite_archives(g, NULL);
    int ok;
    /* 18935 / 18964 (through the archive set's hooks) */
    ok = xeen_sprite_load(&g->sprites, &archives, g->movement.ds[XEEN_DS_WORLD], name, 0, handle) == XR_OK;
    return ok;
}
int sprite_load_resident(xeen_game *g, const char *name)
{
    xeen_movement *m = &g->movement;
    xeen_sprite_archives archives = sprite_archives(g, NULL);
    uint16_t reload = xeen_read16(m, XEEN_DS_RESIDENT_SPRITE_SLOT), h = 0;
    if (xeen_sprite_load(&g->sprites, &archives, m->ds[XEEN_DS_WORLD], name, reload, &h) != XR_OK) {
        return 0;
    }
    xeen_write16(m, XEEN_DS_SPRITE_HANDLE, reload ? reload : h);
    return 1;
}
int handle_push(xeen_game *g, const char *name, uint16_t *handle)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(ds[XEEN_DS_HANDLE_DEPTH] + 1u);
    xeen_write16(m, XEEN_DS_HANDLE_SLOT(ds), xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
    if (!sprite_load(g, name, handle)) {
        return 0;
    }
    xeen_write16(m, XEEN_DS_SPRITE_HANDLE, *handle);
    return 1;
}
/* Pushes the sprites of the DS text NAME on the handle stack; when the load fails the stack depth is put back (the
 * slot's word stays written, as the original leaves it). Returns 0 then. Original: 190A3. */
int handle_push_or_undo(xeen_game *g, uint16_t name)
{
    uint16_t h = 0;
    if (!handle_push(g, ds_text(g, name), &h)) {
        g->movement.ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(g->movement.ds[XEEN_DS_HANDLE_DEPTH] - 1u);
        return 0;
    }
    return 1;
}
void handle_pop(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    (void)xeen_sprite_free_from(&g->sprites, xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
    xeen_write16(m, XEEN_DS_SPRITE_HANDLE, xeen_read16(m, XEEN_DS_HANDLE_SLOT(ds)));
    ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(ds[XEEN_DS_HANDLE_DEPTH] - 1u);
}
/**
 * Returns the archive set sprite loads use: the current world's .CC, the other world's (or the
 * runtime one), and the state archive, with the CD hooks attached.
 */
xeen_sprite_archives sprite_archives(const xeen_game *g, const xeen_archive *runtime)
{
    xeen_sprite_archives a;
    a.cc[g->world] = &g->assets;
    a.cc[!g->world] = g->other_bound ? &g->other : (!g->world ? runtime : NULL);
    a.cur = &g->state;
    { /* Every sprite load is an 18931 (or 190A3's): 47B7D before it */
        xeen_sprite_cache *c = (xeen_sprite_cache *)(uintptr_t)&g->sprites;
        c->pre = sprite_pre;
        c->post = sprite_post;
        c->user = (void *)(uintptr_t)g;
    }
    return a;
}
xeen_resource_status xeen_game_enter_world(xeen_game *g)
{
    xeen_sprite_archives a;
    xeen_resource_status s;
    if (!g || !g->program_data || !g->restored || g->acted || g->world_entered || g->objects_prepared) {
        return XR_BAD_INPUT;
    }
    a = sprite_archives(g, NULL);
    s = xeen_sprites_enter_world(&g->sprites, &a, &g->movement);
    if (s == XR_OK) {
        g->world_entered = 1;
    }
    return s;
}
/**
 * Music hook of the map load: records the requested song name (truncated to the buffer) and its
 * flag.
 */
void record_music(void *user, const char *name, uint16_t flag)
{
    xeen_game *g = user;
    size_t n = strlen(name);
    if (n >= sizeof g->music_name) {
        n = sizeof g->music_name - 1u;
    }
    memcpy(g->music_name, name, n);
    g->music_name[n] = 0;
    g->music_flag = flag;
    ++g->music_requests;
    xeen_input_poll_request_music(&g->input_poll, flag);
}
/** RNG stand-in for a transition probe that must not draw: always returns 0. */
uint16_t transition_probe_random(void *user, uint16_t incoming)
{
    (void)user;
    (void)incoming;
    return 0;
}
/*
 * Loads the map's sprites once its monsters are prepared (and the world entered): the map's .mob read again (at least
 * 48 bytes; on Clouds map 85 its bytes 16..31 are set to FFh before the load) and the map's
 * object, monster, wall-item and sky sprites loaded (xeen_sprites_map_load, its music request recorded); the session
 * then keeps RANDOM as its event random source, and with speech on the audio sink is told the drive's side. Returns
 * XR_OK, or the first failing status (XR_BAD_INPUT for a session that cannot take it, XR_TRUNCATED for a short .mob,
 * XR_CAPACITY without memory).
 * Original: the map load's sprite part (1A84B / 1A929 / 1AB46); 1B0B2's 19CC3 for the sink's drive only (no DS).
 */
xeen_resource_status xeen_game_prepare_map_sprites(xeen_game *g, const uint8_t *runtime_assets,
                                                   size_t runtime_size,
                                                   xeen_entity_random_fn random, void *user)
{
    xeen_archive runtime;
    xeen_resource_info mi;
    xeen_sprite_archives a;
    xeen_resource_status s;
    uint8_t *mob;
    size_t mn = 0;
    char name[24];
    if (!g || !g->program_data || !g->world_entered || !g->monsters_prepared || g->map_sprites_prepared ||
        g->scene_resources_prepared || g->acted || !random ||
        xeen_archive_open(&runtime, runtime_assets, runtime_size) != XR_OK) {
        return XR_BAD_INPUT;
    }
    if (!map_mob_name(g, name)) {
        return XR_BAD_INPUT;
    }
    s = xeen_archive_find_name(&g->state, name, &mi);
    if (s != XR_OK) {
        return s;
    }
    if (mi.size < 48u) {
        return XR_TRUNCATED;
    }
    mob = malloc(mi.size);
    if (!mob) {
        return XR_CAPACITY;
    }
    s = xeen_archive_extract(&g->state, XR_BANK_STATE, &mi, mob, mi.size, &mn);
    a = sprite_archives(g, &runtime);
    if (s == XR_OK && !g->movement.ds[XEEN_DS_WORLD] && g->map_id == 85u) {
        memset(mob + 16, 0xff, 16);
    }
    if (s == XR_OK) {
        s = xeen_sprites_map_load(&g->sprites, &a, &g->movement, mob, g->monster_database,
                                  g->monster_database_size, random, user, record_music, g);
    }
    free(mob);
    if (s == XR_OK) {
        g->map_sprites_prepared = 1;
        g->scene_resources_prepared = 1;
    }
    if (s == XR_OK) {
        g->event_random = random;
        g->event_random_user = user;
    }
    /* 1B0B2's 19CC3, for the audio sink's drive only (no DS). */
    xeen_native_audio_speech_hold(&g->audio, g->movement.ds); /* Phase 286: speech = off */
    if (s == XR_OK && g->movement.ds[XEEN_DS_SPEECH_ON]) {
        xeen_native_audio_cd_side(
                &g->audio, xeen_cd_side(g->movement.ds[XEEN_DS_MAP_SIDE], g->movement.ds[XEEN_DS_PARTY_MAP]));
    }
    return s;
}
const xeen_sprite_cache *xeen_game_sprites(const xeen_game *g)
{
    return g && g->world_entered ? &g->sprites : NULL;
}
unsigned long xeen_game_music(const xeen_game *g, const char **name)
{
    if (name) {
        *name = g ? g->music_name : NULL;
    }
    return g ? g->music_requests : 0;
}
