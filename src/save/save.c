#define _POSIX_C_SOURCE 200809L
#include "xeen/map_objects.h"
#include "xeen/party.h"
#include "xeen/ds_globals.h"
#include "xeen/save.h"
#include "xeen/resources.h"
#include "xeen/byte_order.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#define xeen_mkdir(p)  _mkdir(p)
#define xeen_getpid()  _getpid()
#define xeen_sync(fd)  _commit(fd)
#define fileno(f)      _fileno(f)
#define xeen_rmdir(p)  _rmdir(p)
#define xeen_unlink(p) _unlink(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define xeen_mkdir(p)  mkdir((p), 0700)
#define xeen_getpid()  getpid()
#define xeen_sync(fd)  fsync(fd)
#define xeen_rmdir(p)  rmdir(p)
#define xeen_unlink(p) unlink(p)
#endif
/** Copies the message WHY into ERROR (N bytes) and returns 0. */
static int set_error(char *error, size_t n, const char *why)
{
    if (error && n) {
        (void)snprintf(error, n, "%s", why);
    }
    return 0;
}
/* Byte K of the maze.pty image (the DS from XEEN_DS_PARTY_FILE) and of the maze.nam one (from XEEN_DS_GAME_NAME):
 * PTY(XEEN_DS_PARTY_X) is the party's x in a snapshot's party bytes. */
#define PTY(ds_offset) XEEN_PARTY_FILE_OFFSET(ds_offset)
#define NAM(ds_offset) ((ds_offset) - XEEN_DS_GAME_NAME)
/**
 * Returns nonzero when a save snapshot is consistent: its world, map, position and direction agree
 * with the party record and the party size and member indices are in range.
 */
static int snapshot_valid(const xeen_save_snapshot *s)
{
    if (!s || s->world > 1 || s->header[NAM(XEEN_DS_MAP_SIDE)] != s->world ||
        s->map != s->party[PTY(XEEN_DS_PARTY_MAP)] || s->x != s->party[PTY(XEEN_DS_PARTY_X)] ||
        s->y != s->party[PTY(XEEN_DS_PARTY_Y)] ||
        s->direction != s->party[PTY(XEEN_DS_PARTY_DIRECTION)] || s->direction > 3 || !s->map ||
        (s->party_size != XEEN_CLOUDS_PARTY_BYTES && s->party_size != XEEN_DARK_PARTY_BYTES) ||
        s->party[PTY(XEEN_DS_PARTY_COUNT)] > 8 ||
        s->party[PTY(XEEN_DS_PARTY_COUNT)] != s->party[PTY(XEEN_DS_SAVED_PARTY_COUNT)]) {
        return 0;
    }
    for (unsigned i = 0; i < s->party[PTY(XEEN_DS_PARTY_COUNT)]; ++i) {
        if (s->party[PTY(XEEN_DS_PARTY_MEMBERS) + i] >= XEEN_ROSTER_MEMBERS) {
            return 0;
        }
        for (unsigned j = 0; j < i; ++j) {
            if (s->party[PTY(XEEN_DS_PARTY_MEMBERS) + i] == s->party[PTY(XEEN_DS_PARTY_MEMBERS) + j]) {
                return 0;
            }
        }
    }
    return 1;
}
int xeen_save_read(xeen_save_snapshot *out, const uint8_t *bytes, size_t n, char *error, size_t en)
{
    xeen_archive a;
    xeen_save_snapshot s;
    size_t hn = 0, rn = 0;
    if (!out || !bytes) {
        return set_error(error, en, "invalid save storage");
    }
    if (xeen_archive_open(&a, bytes, n) != XR_OK || a.duplicate_ids) {
        return set_error(error, en, "unsupported original save archive directory");
    }
    memset(&s, 0, sizeof s);
    if (!xeen_archive_extract_state_member(&a, "maze.nam", s.header, sizeof s.header, &hn) ||
        hn != sizeof s.header ||
        !xeen_archive_extract_state_member(&a, "maze.chr", s.roster, sizeof s.roster, &rn) ||
        rn != sizeof s.roster ||
        !xeen_archive_extract_state_member(&a, "maze.pty", s.party, sizeof s.party,
                                           &s.party_size)) {
        return set_error(error, en, "missing or invalid original maze.nam/chr/pty payload");
    }
    s.world = s.header[NAM(XEEN_DS_MAP_SIDE)];
    s.map = s.party[PTY(XEEN_DS_PARTY_MAP)];
    s.x = s.party[PTY(XEEN_DS_PARTY_X)];
    s.y = s.party[PTY(XEEN_DS_PARTY_Y)];
    s.direction = s.party[PTY(XEEN_DS_PARTY_DIRECTION)];
    if (!snapshot_valid(&s)) {
        return set_error(error, en,
                         "unsupported original party membership, world or navigation state");
    }
    *out = s;
    if (error && en) {
        error[0] = 0;
    }
    return 1;
}
int xeen_save_restore(const xeen_save_snapshot *s, uint8_t *ds, size_t n, char *error, size_t en)
{
    if (!ds || n < 65536 || !snapshot_valid(s)) {
        return set_error(error, en, "invalid save restoration target or snapshot");
    }
    /* Profile, not selected world, controls payload size. Reuse the original
     * active-copy implementation without changing its legacy CUR contract. */
    if (xeen_party_restore(s->roster, sizeof s->roster, s->party, s->party_size,
                           s->party_size == XEEN_DARK_PARTY_BYTES, ds, n) != XP_OK) {
        return set_error(error, en, "unsupported original save party");
    }
    memcpy(ds + XEEN_DS_GAME_NAME, s->header, sizeof s->header);
    if (error && en) {
        error[0] = 0;
    }
    return 1;
}
/** Finds member NAME of the archive BYTES (N bytes, no duplicate ids) lying wholly inside it into *I; 0 when it
 * is missing or out of bounds. */
static int member_find(const uint8_t *bytes, size_t n, const char *name, xeen_resource_info *i)
{
    xeen_archive a;
    return xeen_archive_open(&a, bytes, n) == XR_OK && !a.duplicate_ids &&
           xeen_archive_find_name(&a, name, i) == XR_OK && i->offset <= n && i->size <= n - i->offset;
}
/**
 * Overwrites member NAME of the archive BYTES with SRC, which must have the member's exact size.
 */
static int replace_member(uint8_t *bytes, size_t n, const char *name, const uint8_t *src,
                          size_t size)
{
    xeen_resource_info i;
    if (!member_find(bytes, n, name, &i) || i.size != size) {
        return 0;
    }
    memcpy(bytes + i.offset, src, size);
    return 1;
}
/**
 * Formats a map file name, "mazeNNNN.EXT": maze0 for maps up to 99 (a signed comparison, as in
 * 30E60/30EF6), mazex above.
 */
static void map_name(char *out, size_t n, uint16_t id, const char *ext)
{
    /* 30E60/30EF6: signed word compared with 99, unsigned %03u formatting. */
    (void)snprintf(out, n, "maze%c%03u.%s", (int16_t)id <= 99 ? '0' : 'x', (unsigned)id, ext);
}
/** Stores the size of member NAME of the archive BYTES in *SIZE; returns 0 when it is missing. */
static int member_size(const uint8_t *bytes, size_t n, const char *name, size_t *size)
{
    xeen_resource_info i;
    if (!member_find(bytes, n, name, &i)) {
        return 0;
    }
    *size = i.size;
    return 1;
}
/** Copies member NAME of the archive BYTES, which must be exactly SIZE bytes, to OUT. */
static int member_copy(const uint8_t *bytes, size_t n, const char *name, uint8_t *out, size_t size)
{
    xeen_archive a;
    size_t got = 0;
    return xeen_archive_open(&a, bytes, n) == XR_OK &&
           xeen_archive_extract_state_member(&a, name, out, size, &got) && got == size;
}
/* 30F53..: the map's event script (its loaded store is the member's size), replaced by EVENTS when given. */
static int save_map_events(uint8_t *active, size_t n, const uint8_t *ds, uint16_t maze,
                           const uint8_t *events, size_t event_size, char *error, size_t en)
{
    char name[32];
    size_t size;
    map_name(name, sizeof name, maze, "evt");
    if (!member_size(active, n, name, &size)) {
        return set_error(error, en, "active saved map event resource is unavailable");
    }
    if (size != xeen_get_le16(ds + XEEN_DS_EVENT_BYTES)) {
        return set_error(error, en,
                         "saved map event resource disagrees with the loaded event store");
    }
    if (events && (event_size != size || !replace_member(active, n, name, events, event_size))) {
        return set_error(error, en, "mutable event store changed size or layout");
    }
    return 1;
}
/* 30E49: the map record of cache slot 0, read back to check the write. */
static int save_map_record(uint8_t *active, size_t n, const uint8_t *ds, uint16_t maze, char *error,
                           size_t en)
{
    char name[32];
    uint8_t check[sizeof(xeen_map_record)];
    map_name(name, sizeof name, maze, "dat");
    if (!replace_member(active, n, name, ds + XEEN_DS_MAP_DATA, sizeof check) ||
        !member_copy(active, n, name, check, sizeof check) ||
        memcmp(check, ds + XEEN_DS_MAP_DATA, sizeof check)) {
        return set_error(error, en, "active saved map resource is unavailable");
    }
    return 1;
}
/* Indoors, the map records of the neighbouring cache slots 1..8 (an empty slot skipped). */
static int save_map_neighbours(uint8_t *active, size_t n, const uint8_t *ds, char *error, size_t en)
{
    char name[32];
    unsigned i;
    for (i = 1; i < 9u; ++i) {
        uint16_t id = xeen_get_le16(ds + (XEEN_DS_MAP_SLOT_IDS + 2u * i));
        if (!id) {
            continue;
        }
        map_name(name, sizeof name, id, "dat");
        if (!replace_member(active, n, name, ds + XEEN_DS_MAP_DATA + i * sizeof(xeen_map_record),
                            sizeof(xeen_map_record))) {
            return set_error(error, en, "neighbouring saved map resource is unavailable");
        }
    }
    return 1;
}
/* 30FC5..3109E: rebuild the loaded MOB image (this member, which is what
 * the native map load read) from runtime object, monster and wall-item
 * positions. Only these bytes change; headers, directions, terminators and
 * trailing bytes are retained. Entries beyond the member are unsupported. */
static int save_map_objects(uint8_t *active, size_t n, const uint8_t *ds, uint16_t maze, char *error,
                            size_t en)
{
    char name[32];
    size_t size;
    uint8_t *mob;
    unsigned objects = ds[XEEN_DS_MAP_OBJECT_COUNT], monsters = ds[XEEN_DS_MAP_MONSTER_COUNT],
             walls = ds[XEEN_DS_WALL_ITEM_COUNT], entry = 0, i;
    int replaced;
    map_name(name, sizeof name, maze, "mob");
    if (!member_size(active, n, name, &size) || size < XEEN_MOB_HEADER_BYTES) {
        return set_error(error, en, "active saved map object resource is unavailable");
    }
    if ((objects && XEEN_MOB_HEADER_BYTES + XEEN_MOB_ENTRY_BYTES * (objects - 1u) + 3u > size) ||
        (monsters && XEEN_MOB_HEADER_BYTES + XEEN_MOB_ENTRY_BYTES * (objects + monsters) + 2u > size) ||
        (walls && XEEN_MOB_HEADER_BYTES + XEEN_MOB_ENTRY_BYTES * (objects + monsters + walls + 1u) + 2u > size)) {
        return set_error(error, en, "runtime map objects exceed the saved object resource");
    }
    mob = malloc(size);
    if (!mob) {
        return set_error(error, en, "save output allocation failed");
    }
    if (!member_copy(active, n, name, mob, size)) {
        free(mob);
        return set_error(error, en, "active saved map object resource is unavailable");
    }
    for (i = 0; i < objects; ++i, ++entry) {
        uint8_t *e = mob + XEEN_MOB_HEADER_BYTES + entry * XEEN_MOB_ENTRY_BYTES;
        const uint8_t *r = ds + XEEN_DS_MAP_OBJECTS + i * XEEN_MAP_OBJECT_BYTES;
        e[0] = r[XEEN_MAP_OBJECT_X];
        e[1] = r[XEEN_MAP_OBJECT_Y];
        e[2] = r[XEEN_MAP_OBJECT_SLOT];
    }
    ++entry;
    for (i = 0; i < monsters; ++i, ++entry) {
        uint8_t *e = mob + XEEN_MOB_HEADER_BYTES + entry * XEEN_MOB_ENTRY_BYTES;
        const uint8_t *r = ds + XEEN_MAP_MONSTER(i);
        e[0] = r[XEEN_MAP_MONSTER_X];
        e[1] = r[XEEN_MAP_MONSTER_Y];
    }
    ++entry;
    for (i = 0; i < walls; ++i, ++entry) {
        uint8_t *e = mob + XEEN_MOB_HEADER_BYTES + entry * XEEN_MOB_ENTRY_BYTES;
        const uint8_t *r = ds + XEEN_DS_WALL_ITEMS + i * XEEN_WALL_ITEM_BYTES;
        e[0] = r[XEEN_WALL_ITEM_X];
        e[1] = r[XEEN_WALL_ITEM_Y];
    }
    replaced = replace_member(active, n, name, mob, size);
    free(mob);
    return replaced ? 1 : set_error(error, en, "active saved map object resource changed size or layout");
}
/* Original 30F34 current-map save into the active world's component. Every
 * member is rewritten in place at its recorded size, as low-level 50776 does
 * (a missing member is fatal there, a rejection here). There is no dirty
 * tracking: the original rewrites unconditionally, so unchanged state is a
 * byte-identical rewrite. The legacy entry preserves EVT. The event-aware
 * entry mirrors 30F34 by replacing it from the current handle-owned store.
 * The stages in the original's order: the events, the map record, Clouds map 15's flag (in PARTY), the
 * neighbours (indoors), the objects; the first failing stage ends the save, earlier members rewritten. */
static int save_map(uint8_t *active, size_t n, const uint8_t *ds, uint8_t *party,
                    const uint8_t *events, size_t event_size, char *error, size_t en)
{
    uint16_t maze = xeen_get_le16(ds + XEEN_DS_MAP_FIELD(ID));
    if (xeen_map_save_skipped(maze, ds[XEEN_DS_WORLD])) {
        return 1; /* 30F36/30F40 */
    }
    /* 30F53 names the members from C0CC alone; ED99 is not read
     * (the ending heads 334AF/334D8 move ED99 before 33518's save). */
    if (xeen_get_le16(ds + XEEN_DS_MAP_SLOT_IDS) != maze) {
        return set_error(error, en, "current map cache identity disagrees with the loaded map");
    }
    if (!save_map_events(active, n, ds, maze, events, event_size, error, en) ||
        !save_map_record(active, n, ds, maze, error, en)) {
        return 0;
    }
    /* 30E9D..30EC5 require all three records off the map (each
     * x or y above 1Fh), as 1A5BF's clear on loading map 15 does. */
    if (!ds[XEEN_DS_WORLD] && maze == 15u && xeen_map15_monsters_gone(ds)) {
        party[PTY(XEEN_DS_GAME_FLAGS) + 56u / 8u] |= (uint8_t)(0x80u >> (56u & 7u));
    }
    if (!xeen_get_le16(ds + XEEN_DS_OUTDOORS) && !save_map_neighbours(active, n, ds, error, en)) {
        return 0;
    }
    return save_map_objects(active, n, ds, maze, error, en);
}
int xeen_save_map_archive(uint8_t *active, size_t n, const uint8_t *ds, size_t dsn,
                          const uint8_t *events, size_t event_size, char *error, size_t en)
{
    uint8_t party[XEEN_DARK_PARTY_BYTES];
    if (!active || !ds || dsn < 65536u) {
        return set_error(error, en, "invalid current-map archive input");
    }
    memcpy(party, ds + XEEN_DS_PARTY_FILE, sizeof party);
    return save_map(active, n, ds, party, events, event_size, error, en);
}
int xeen_save_map_live(uint8_t *active, size_t n, uint8_t *ds, size_t dsn, const uint8_t *events,
                       size_t event_size, char *error, size_t en)
{
    if (!active || !ds || dsn < 65536u) {
        return set_error(error, en, "invalid current-map archive input");
    }
    return save_map(active, n, ds, ds + XEEN_DS_PARTY_FILE, events, event_size, error, en);
}
int xeen_save_member_replace(uint8_t *bytes, size_t n, const char *name, const uint8_t *src,
                             size_t size)
{
    return bytes && name && src && replace_member(bytes, n, name, src, size);
}
void xeen_save_pair_free(xeen_save_pair *p)
{
    if (!p) {
        return;
    }
    free(p->cloud);
    free(p->dark);
    memset(p, 0, sizeof *p);
}
/* 9A10..9A19: the pending-items byte (and the byte after it), the pending gems and gold dwords. */
#define PENDING_TREASURE_BYTES 10u
/* The live party's largest member list a paired save takes (snapshot_valid's bound). */
#define SAVE_PARTY_LIMIT 8u
/* Why the runtime party in DS cannot be written into a paired save, or NULL when it can: no treasure still waits to
 * be shared out, the world byte agrees with the map header's side, and the party holds at most eight distinct roster
 * places (0..29) with the saved party count equal to the live one. The tests run in this order; the first failure
 * names the error. */
static const char *unsavable_party(const uint8_t *ds)
{
    unsigned count = ds[XEEN_DS_PARTY_COUNT];
    for (size_t i = 0; i < PENDING_TREASURE_BYTES; ++i) {
        if (ds[XEEN_DS_TREASURE_ITEMS_PENDING + i]) {
            return "cannot save unsettled transient reward state";
        }
    }
    if (ds[XEEN_DS_WORLD] > 1u || ds[XEEN_DS_MAP_SIDE] != ds[XEEN_DS_WORLD]) {
        return "runtime world and persistent header disagree";
    }
    if (count > SAVE_PARTY_LIMIT || ds[XEEN_DS_SAVED_PARTY_COUNT] != count) {
        return "unsupported runtime party membership";
    }
    for (unsigned i = 0; i < count; ++i) {
        unsigned who = ds[XEEN_DS_PARTY_MEMBERS + i];
        if (who >= XEEN_ROSTER_MEMBERS) {
            return "unsupported runtime roster reference";
        }
        for (unsigned j = 0; j < i; ++j) {
            if (ds[XEEN_DS_PARTY_MEMBERS + j] == who) {
                return "duplicate runtime roster reference";
            }
        }
    }
    return NULL;
}
/* Frees the pair being built and fails with WHY, or with the message already in ERROR when WHY is NULL. */
static int pair_failed(xeen_save_pair *pair, const char *why, char *error, size_t en)
{
    xeen_save_pair_free(pair);
    return why ? set_error(error, en, why) : 0;
}
/**
 * Builds the pair of world save files from the Clouds and Darkside archives, the DS image and the event state, as the
 * game's save (29669) writes them: the current map is saved into its own world's archive, then the Darkside archive's
 * maze.nam, maze.chr (the roster, each party member's live record in its roster place) and maze.pty (the party file)
 * are replaced, and the written Darkside archive is read back and must place the party where DS does. The inputs are
 * never changed. On any failure the pair is freed, ERROR names the cause (a save read's own message when one of the
 * archives or the map save is refused) and 0 is returned; on success *OUT owns the two buffers and ERROR is empty.
 */
static int build_pair(xeen_save_pair *out, const uint8_t *cloud, size_t cn, const uint8_t *dark,
                      size_t dn, const uint8_t *ds, size_t dsn, const uint8_t *events,
                      size_t event_size, char *error, size_t en)
{
    xeen_save_pair pair = {0};
    xeen_save_snapshot original, cloud_check, check;
    uint8_t roster[XEEN_ROSTER_BYTES];
    uint8_t party[XEEN_DARK_PARTY_BYTES];
    const char *why;
    int dark_world;
    /* 1. the inputs: two readable archives, the Darkside one holding the combined party storage */
    if (!out || !cloud || !dark || !ds || dsn < 65536u) {
        return set_error(error, en, "invalid paired save input");
    }
    if (!xeen_save_read(&original, dark, dn, error, en)) {
        return 0;
    }
    if (original.party_size != XEEN_DARK_PARTY_BYTES) {
        return set_error(error, en, "paired save requires combined Darkside party storage");
    }
    if (!xeen_save_read(&cloud_check, cloud, cn, error, en)) {
        return set_error(error, en, "invalid Clouds save component");
    }
    /* 2. the live party */
    if ((why = unsavable_party(ds)) != NULL) {
        return set_error(error, en, why);
    }
    memcpy(roster, original.roster, sizeof roster);
    for (unsigned i = 0; i < ds[XEEN_DS_PARTY_COUNT]; ++i) {
        memcpy(roster + (size_t)ds[XEEN_DS_PARTY_MEMBERS + i] * XEEN_CHARACTER_BYTES,
               ds + XEEN_PARTY_RECORDS + (size_t)i * XEEN_CHARACTER_BYTES, XEEN_CHARACTER_BYTES);
    }
    memcpy(party, ds + XEEN_DS_PARTY_FILE, sizeof party);
    /* 3. the output: copies of both archives */
    pair.cloud = malloc(cn);
    pair.dark = malloc(dn);
    if (!pair.cloud || !pair.dark) {
        return pair_failed(&pair, "save output allocation failed", error, en);
    }
    memcpy(pair.cloud, cloud, cn);
    memcpy(pair.dark, dark, dn);
    pair.cloud_size = cn;
    pair.dark_size = dn;
    /* 4. the current map into its world's archive (29669), before the party members (2966E) */
    dark_world = ds[XEEN_DS_WORLD] != 0;
    if (!save_map(dark_world ? pair.dark : pair.cloud, dark_world ? dn : cn, ds, party, events, event_size, error,
                  en)) {
        return pair_failed(&pair, NULL, error, en);
    }
    /* 5. the party members into the Darkside archive, each in place (same size and layout) */
    if (!replace_member(pair.dark, dn, "maze.nam", ds + XEEN_DS_GAME_NAME, XEEN_GAME_NAME_BYTES) ||
        !replace_member(pair.dark, dn, "maze.chr", roster, sizeof roster) ||
        !replace_member(pair.dark, dn, "maze.pty", party, sizeof party)) {
        return pair_failed(&pair, "paired save members changed size or layout", error, en);
    }
    /* 6. the written archive read back: the party where DS has it */
    if (!xeen_save_read(&check, pair.dark, pair.dark_size, error, en) || check.world != ds[XEEN_DS_WORLD] ||
        check.map != ds[XEEN_DS_PARTY_MAP] || check.x != ds[XEEN_DS_PARTY_X] ||
        check.y != ds[XEEN_DS_PARTY_Y] || check.direction != ds[XEEN_DS_PARTY_DIRECTION]) {
        return pair_failed(&pair, "serialized save verification failed", error, en);
    }
    *out = pair;
    if (error && en) {
        error[0] = 0;
    }
    return 1;
}
int xeen_save_build_pair(xeen_save_pair *out, const uint8_t *cloud, size_t cn, const uint8_t *dark,
                         size_t dn, const uint8_t *ds, size_t dsn, char *error, size_t en)
{
    return build_pair(out, cloud, cn, dark, dn, ds, dsn, NULL, 0, error, en);
}
int xeen_save_build_pair_event(xeen_save_pair *out, const uint8_t *cloud, size_t cn,
                               const uint8_t *dark, size_t dn, const uint8_t *ds, size_t dsn,
                               const uint8_t *events, size_t event_size, char *error, size_t en)
{
    return build_pair(out, cloud, cn, dark, dn, ds, dsn, events, event_size, error, en);
}
/** Returns nonzero when the file at PATH holds exactly the N BYTES. */
static int same_file(const char *path, const uint8_t *bytes, size_t n)
{
    FILE *f = fopen(path, "rb");
    uint8_t block[4096];
    size_t at = 0, got;
    if (!f) {
        return 0;
    }
    while ((got = fread(block, 1, sizeof block, f)) != 0) {
        if (got > n - at || memcmp(block, bytes + at, got)) {
            fclose(f);
            return 0;
        }
        at += got;
    }
    if (ferror(f) || at != n) {
        fclose(f);
        return 0;
    }
    return fclose(f) == 0;
}
/** Writes the N BYTES to PATH and syncs them to disk; returns 0 on any failure. */
static int write_file(const char *path, const uint8_t *bytes, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f) {
        return 0;
    }
    ok = fwrite(bytes, 1, n, f) == n && fflush(f) == 0 && xeen_sync(fileno(f)) == 0;
    if (fclose(f) != 0) {
        ok = 0;
    }
    return ok;
}
int xeen_save_commit_pair(const char *destination, const uint8_t *cloud, size_t cn,
                          const uint8_t *dark, size_t dn, const uint8_t *ds, size_t dsn,
                          char *error, size_t en)
{
    return xeen_save_commit_pair_event(destination, cloud, cn, dark, dn, ds, dsn, NULL, 0, error,
                                       en);
}
/** Formats DIR/NAME into OUT (SIZE bytes); returns 0 when it does not fit with a byte to spare (a path of SIZE - 1
 * characters is refused as well: it may have been cut). */
static int path_in(char *out, size_t size, const char *dir, const char *name)
{
    return snprintf(out, size, "%s/%s", dir, name) >= 0 && strlen(out) < size - 1u;
}
/** Publishes the PAIR: its two archives written into the new directory STAGE (as CLOUD_PATH and DARK_PATH), then the
 * directory renamed to DESTINATION. On any failure the two files and the directory are removed again, so the
 * destination is never replaced in part; returns 0 then. */
static int publish_pair(const xeen_save_pair *pair, const char *stage, const char *cloud_path, const char *dark_path,
                        const char *destination)
{
    if (write_file(cloud_path, pair->cloud, pair->cloud_size) && write_file(dark_path, pair->dark, pair->dark_size) &&
        rename(stage, destination) == 0) {
        return 1;
    }
    (void)xeen_unlink(cloud_path);
    (void)xeen_unlink(dark_path);
    (void)xeen_rmdir(stage);
    return 0;
}
/*
 * Builds the pair (build_pair) and publishes it as DESTINATION/XEEN.WOX and DESTINATION/DARK.WOX. A destination that
 * already holds exactly this pair is a success with nothing written. Otherwise the pair is staged in the new directory
 * DESTINATION.tmp.<process id> and that directory renamed to DESTINATION (publish_pair), which replaces no existing
 * generation: a destination holding other files makes the rename, and so the commit, fail. Returns 1 with ERROR
 * empty, else 0 with ERROR naming the cause (the build's own message when the pair cannot be built).
 */
int xeen_save_commit_pair_event(const char *destination, const uint8_t *cloud, size_t cn,
                                const uint8_t *dark, size_t dn, const uint8_t *ds, size_t dsn,
                                const uint8_t *events, size_t event_size, char *error, size_t en)
{
    xeen_save_pair pair = {0};
    char stage[4096], staged_cloud[4096], staged_dark[4096], cloud_path[4096], dark_path[4096];
    if (!destination || !*destination) {
        return set_error(error, en, "invalid save destination");
    }
    if (!build_pair(&pair, cloud, cn, dark, dn, ds, dsn, events, event_size, error, en)) {
        return 0;
    }
    if (!path_in(cloud_path, sizeof cloud_path, destination, "XEEN.WOX") ||
        !path_in(dark_path, sizeof dark_path, destination, "DARK.WOX")) {
        return pair_failed(&pair, "save destination is too long", error, en);
    }
    if (same_file(cloud_path, pair.cloud, pair.cloud_size) && same_file(dark_path, pair.dark, pair.dark_size)) {
        xeen_save_pair_free(&pair);
        if (error && en) {
            error[0] = 0;
        }
        return 1;
    }
    if (snprintf(stage, sizeof stage, "%s.tmp.%ld", destination, (long)xeen_getpid()) < 0 ||
        strlen(stage) >= sizeof stage - 1u || !path_in(staged_cloud, sizeof staged_cloud, stage, "XEEN.WOX") ||
        !path_in(staged_dark, sizeof staged_dark, stage, "DARK.WOX")) {
        return pair_failed(&pair, "save destination is too long", error, en);
    }
    if (xeen_mkdir(stage) != 0) {
        return pair_failed(&pair, "cannot create staged save directory", error, en);
    }
    if (!publish_pair(&pair, stage, staged_cloud, staged_dark, destination)) {
        return pair_failed(&pair, "save pair publish failed without replacing destination", error, en);
    }
    xeen_save_pair_free(&pair);
    if (error && en) {
        error[0] = 0;
    }
    return 1;
}
