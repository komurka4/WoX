#ifndef XEEN_RESOURCES_H
#define XEEN_RESOURCES_H
#include "xeen/map_record.h"
#include <stddef.h>
#include <string.h>
#include "xeen/movement.h"
typedef enum {
    XR_OK,
    XR_NOT_FOUND,
    XR_TRUNCATED,
    XR_BAD_INDEX,
    XR_CAPACITY,
    XR_BAD_MAP,
    XR_BAD_INPUT
} xeen_resource_status;
typedef enum {
    XR_BANK_STATE,
    XR_BANK_ASSETS_XOR35
} xeen_bank;
/* Borrowed immutable storage must outlive archive use. Duplicate IDs are
 * diagnosed by duplicate_ids; lookup deliberately retains first-match order. */
typedef struct {
    const uint8_t *bytes;
    size_t size;
    uint16_t count;
    size_t duplicate_ids;
} xeen_archive;
typedef struct {
    uint16_t id, size;
    uint32_t offset;
    uint8_t opaque7;
} xeen_resource_info;
typedef struct {
    uint8_t bytes[XEEN_MAP_RECORD_BYTES];
} xeen_map_record;
typedef struct {
    uint16_t ids[9], mode;
    xeen_map_record records[9];
} xeen_navigation_cache;
typedef struct {
    uint8_t world;
    int flag19;
} xeen_patch_inputs;
uint8_t xeen_directory_decode(uint8_t, size_t);
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
uint8_t xeen_directory_encode(uint8_t, size_t);
/* Stops at original NUL byte, before normalization, or supplied length. */
uint16_t xeen_resource_name_id(const uint8_t *, size_t);
/* Opens the CC archive of N bytes at P (borrowed): its directory (the count word, 8 bytes per member) must fit, and each
 * member's data must lie past the directory (XR_BAD_INDEX) and inside the file (XR_TRUNCATED); members sharing an id
 * are counted in duplicate_ids. */
xeen_resource_status xeen_archive_open(xeen_archive *, const uint8_t *, size_t);
/* Member I's directory entry (id, offset, size), the entry read through the directory's decode; XR_TRUNCATED when
 * the file ends inside it. */
xeen_resource_status xeen_archive_record(const xeen_archive *, size_t, xeen_resource_info *);
xeen_resource_status xeen_archive_find(const xeen_archive *, uint16_t, xeen_resource_info *);
/* xeen_archive_find of the member whose file name is NAME (LENGTH bytes, its xeen_resource_name_id); a
 * miss also records NAME (xeen_archive_last_miss). */
xeen_resource_status xeen_archive_find_named(const xeen_archive *, const char *name, size_t length,
                                             xeen_resource_info *);
/*
 * Diagnostics only (Phase 271, owner-requested; the game never reads it): the last member a lookup did
 * not find since xeen_archive_miss_clear — the archive's storage (to tell which file it is), the member's
 * id and, when the lookup had it, its name ("" otherwise). The host names it when the game stops (a
 * floppy archive lacks members of the CD release).
 */
typedef struct {
    const uint8_t *bytes;
    size_t size;
    uint16_t id;
    char name[32];
} xeen_archive_miss;
void xeen_archive_miss_clear(void);
/* 1 with the last miss in *OUT when one was recorded since the clear, else 0. */
int xeen_archive_last_miss(xeen_archive_miss *out);
/* Keeps the last miss (if any since the clear) under TAG (the game's stop boundary: the host
 * reports the stop one step later, after its next clear); a later keep replaces it. */
void xeen_archive_miss_keep(uint32_t tag);
/* 1 with the miss kept under TAG in *OUT, else 0. */
int xeen_archive_kept_miss(uint32_t tag, xeen_archive_miss *out);
xeen_resource_status xeen_archive_extract(const xeen_archive *, xeen_bank,
                                          const xeen_resource_info *, uint8_t *, size_t, size_t *);
/* Test support (the xeen_test_support library, src/test_support/; not in the game): the map record's decoder and
 * accessors. Numeric indices only; invalid indices return zero. Unknown bytes stay opaque. */
xeen_resource_status xeen_map_decode(const uint8_t *, size_t, xeen_map_record *);
uint16_t xeen_map_tile(const xeen_map_record *, unsigned, unsigned);
uint8_t xeen_map_metadata(const xeen_map_record *, unsigned, unsigned);
uint8_t xeen_map_threshold(const xeen_map_record *);
/* Numeric indices only; an invalid one returns zero. */
uint8_t xeen_map_classification(const xeen_map_record *, unsigned);
xeen_resource_status xeen_cache_prepare(const xeen_archive *, xeen_bank, uint16_t,
                                        xeen_patch_inputs, xeen_navigation_cache *);
xeen_resource_status xeen_cache_commit(const xeen_navigation_cache *, xeen_movement *);
/* Install as dispatcher or delegate LOAD_MAP from a broader dispatcher.
 * flag19 is an explicitly supplied result of the original F01F bit19 query.
 * Other services are forwarded only if a fallback is supplied. */
typedef struct {
    const xeen_archive *archive;
    xeen_bank bank;
    int flag19;
    xeen_resource_status last_status;
    xeen_service_fn fallback;
    void *fallback_user;
} xeen_resource_loader;
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
int xeen_resource_service(void *, xeen_movement *, xeen_service, uint16_t, uint16_t, uint16_t *);
/** Finds the member of archive A whose file name is NAME (its xeen_resource_name_id) into *R. */
static inline xeen_resource_status xeen_archive_find_name(const xeen_archive *a, const char *name,
                                                          xeen_resource_info *r)
{
    return xeen_archive_find_named(a, name, strlen(name), r);
}
/**
 * Finds the member NAME of archive A and extracts it from the state bank into OUT (CAP bytes);
 * returns nonzero on success with its size in *N. The save reader (19953's maze.nam/maze.chr/maze.pty)
 * and the menu's Load share it.
 */
static inline int xeen_archive_extract_state_member(const xeen_archive *a, const char *name,
                                                    uint8_t *out, size_t cap, size_t *n)
{
    xeen_resource_info i;
    return xeen_archive_find_name(a, name, &i) == XR_OK &&
           xeen_archive_extract(a, XR_BANK_STATE, &i, out, cap, n) == XR_OK;
}
#endif
