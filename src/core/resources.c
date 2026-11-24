#include "xeen/resources.h"
#include "xeen/ds_globals.h"
#include "xeen/byte_order.h"
#include <stdio.h>
#include <string.h>
/**
 * Decrypts byte I of a .CC archive directory (rotate left by 2, then add ACh + 67h per position).
 */
uint8_t xeen_directory_decode(uint8_t b, size_t i)
{
    return (uint8_t)(((unsigned)b << 2 | (unsigned)b >> 6) + 0xacu + (i % 256u) * 0x67u);
}
uint16_t xeen_resource_name_id(const uint8_t *p, size_t n)
{
    uint16_t a = 0;
    size_t i;
    if (!p) {
        return 0;
    }
    for (i = 0; i < n && p[i]; i++) {
        unsigned c = p[i] & 127u;
        if (c >= 96u) {
            c -= 32u;
        }
        a = (uint16_t)((unsigned)a << 8 | (unsigned)a >> 8);
        a = (uint16_t)(((unsigned)a << 1 | (unsigned)a >> 15) + c);
    }
    return a;
}
/* A CC archive's directory: its entry count word, then 8 bytes per member (id, offset, size: xeen_archive_record).
 * Returns the bytes of a directory of COUNT members, where the first member's data may begin. */
static size_t directory_end(size_t count)
{
    return 2u + 8u * count;
}
xeen_resource_status xeen_archive_record(const xeen_archive *a, size_t i, xeen_resource_info *r)
{
    uint8_t b[8];
    size_t j;
    if (!a || !a->bytes || !r || i >= a->count) {
        return XR_BAD_INPUT;
    }
    if (a->size < directory_end(i + 1u)) {
        return XR_TRUNCATED;
    }
    for (j = 0; j < 8; j++) {
        b[j] = xeen_directory_decode(a->bytes[2 + 8 * i + j], 8 * i + j);
    }
    r->id = xeen_get_le16(b);
    r->offset = (uint32_t)b[2] | ((uint32_t)b[3] << 8) | ((uint32_t)b[4] << 16);
    r->size = xeen_get_le16(b + 5);
    r->opaque7 = b[7];
    return XR_OK;
}
xeen_resource_status xeen_archive_open(xeen_archive *out, const uint8_t *p, size_t n)
{
    xeen_archive a;
    size_t i;
    uint8_t seen[8192] = {0};
    if (!out || !p) {
        return XR_BAD_INPUT;
    }
    if (n < 2) {
        return XR_TRUNCATED;
    }
    a.bytes = p;
    a.size = n;
    a.count = xeen_get_le16(p);
    a.duplicate_ids = 0;
    if (directory_end(a.count) > n) {
        return XR_TRUNCATED;
    }
    for (i = 0; i < a.count; i++) {
        xeen_resource_info r;
        xeen_resource_status status = xeen_archive_record(&a, i, &r);
        if (status != XR_OK) {
            return status;
        }
        if (r.offset < directory_end(a.count)) {
            return XR_BAD_INDEX;
        }
        if (r.offset > n || r.size > n - r.offset) {
            return XR_TRUNCATED;
        }
        if (seen[r.id / 8u] & (1u << (r.id % 8u))) {
            a.duplicate_ids++;
        }
        seen[r.id / 8u] |= (uint8_t)(1u << (r.id % 8u));
    }
    *out = a;
    return XR_OK;
}
/* The last lookup miss (diagnostics only; xeen_archive_last_miss). */
static xeen_archive_miss last_miss;
static int miss_recorded;
void xeen_archive_miss_clear(void)
{
    miss_recorded = 0;
}
int xeen_archive_last_miss(xeen_archive_miss *out)
{
    if (miss_recorded && out) {
        *out = last_miss;
    }
    return miss_recorded;
}
static xeen_archive_miss kept_miss;
static uint32_t kept_tag;
static int miss_kept;
void xeen_archive_miss_keep(uint32_t tag)
{
    miss_kept = miss_recorded;
    if (miss_recorded) {
        kept_miss = last_miss;
        kept_tag = tag;
    }
}
int xeen_archive_kept_miss(uint32_t tag, xeen_archive_miss *out)
{
    if (!miss_kept || kept_tag != tag) {
        return 0;
    }
    if (out) {
        *out = kept_miss;
    }
    return 1;
}
xeen_resource_status xeen_archive_find(const xeen_archive *a, uint16_t id, xeen_resource_info *r)
{
    size_t i;
    if (!a || !r) {
        return XR_BAD_INPUT;
    }
    for (i = 0; i < a->count; i++) {
        xeen_resource_status s = xeen_archive_record(a, i, r);
        if (s != XR_OK) {
            return s;
        }
        if (r->id == id) {
            return XR_OK;
        }
    }
    last_miss.bytes = a->bytes;
    last_miss.size = a->size;
    last_miss.id = id;
    last_miss.name[0] = 0;
    miss_recorded = 1;
    return XR_NOT_FOUND;
}
xeen_resource_status xeen_archive_find_named(const xeen_archive *a, const char *name, size_t length,
                                             xeen_resource_info *r)
{
    xeen_resource_status s;
    if (!name) {
        return XR_BAD_INPUT;
    }
    s = xeen_archive_find(a, xeen_resource_name_id((const uint8_t *)name, length), r);
    if (s == XR_NOT_FOUND) {
        size_t n = 0;
        while (n < length && n + 1u < sizeof last_miss.name && name[n]) {
            last_miss.name[n] = name[n];
            ++n;
        }
        last_miss.name[n] = 0;
    }
    return s;
}
xeen_resource_status xeen_archive_extract(const xeen_archive *a, xeen_bank bank,
                                          const xeen_resource_info *r, uint8_t *dst, size_t cap,
                                          size_t *written)
{
    size_t i;
    if (written) {
        *written = 0;
    }
    if (!a || !r || (!dst && r->size) || (bank != XR_BANK_STATE && bank != XR_BANK_ASSETS_XOR35)) {
        return XR_BAD_INPUT;
    }
    if (r->offset < directory_end(a->count)) {
        return XR_BAD_INDEX;
    }
    if (r->offset > a->size || r->size > a->size - r->offset) {
        return XR_TRUNCATED;
    }
    if (cap < r->size) {
        return XR_CAPACITY;
    }
    for (i = 0; i < r->size; i++) {
        dst[i] = (uint8_t)(a->bytes[r->offset + i] ^ (bank == XR_BANK_STATE ? 0 : 0x35));
    }
    if (written) {
        *written = r->size;
    }
    return XR_OK;
}
static uint16_t xeen_map_neighbor(const xeen_map_record *r, unsigned i)
{
    return i < 4 ? xeen_get_le16(r->bytes + XEEN_MAP_RECORD_NORTH + 2 * i) : 0;
}
static uint16_t xeen_map_mode(const xeen_map_record *r)
{
    return xeen_get_le16(r->bytes + XEEN_MAP_RECORD_MODE);
}
uint8_t xeen_map_classification(const xeen_map_record *r, unsigned i)
{
    return i < 16 ? r->bytes[XEEN_MAP_RECORD_CLASSIFICATION + i] : 0;
}
/* Loads map ID ("maze0NNN.dat", or "mazexNNN.dat" above map 99, the id compared as a signed word) into slot I of the
 * cache C, then applies the two patches: Darkside's map 50 sets 32 seen bytes of slot 0's record (slot 0's, even
 * when map 50 is a neighbour's: the original's quirk, it writes the fixed address of slot 0's seen bytes), and with
 * Clouds flag 19h maps 42..44 lose the low three bits of their metadata (the loaded slot's own).
 * Returns the lookup's or the read's status, XR_BAD_MAP for a record of another size.
 * Original: 1A134..1A148 (world DS 94 nonzero and the id 32h: 3FE7 memset of DS:C128, = BDCC + 35Ch, FFh x 20h,
 * against 1A175's BFCC + 37Ch x slot); 1A151..1A199 (the flag test 1F847(19h), maps 2Ah..2Ch). */
static xeen_resource_status cache_slot_load(const xeen_archive *a, xeen_bank bank, uint16_t id, xeen_patch_inputs patch,
                                            xeen_navigation_cache *c, unsigned i)
{
    char name[32];
    xeen_resource_info info;
    xeen_resource_status s;
    size_t j;
    /* Original compares signed 16-bit argument with 99. */
    snprintf(name, sizeof(name), "maze%c%03u.dat", (id <= 99 || id >= 32768) ? '0' : 'x', (unsigned)id);
    s = xeen_archive_find_name(a, name, &info);
    if (s != XR_OK) {
        return s;
    }
    if (info.size != XEEN_MAP_RECORD_BYTES) {
        return XR_BAD_MAP;
    }
    s = xeen_archive_extract(a, bank, &info, c->records[i].bytes, sizeof(c->records[i].bytes), NULL);
    if (s != XR_OK) {
        return s;
    }
    if (patch.world && id == 50) {
        memset(c->records[0].bytes + XEEN_MAP_RECORD_SEEN, 255, 32);
    }
    if (!patch.world && patch.flag19 && id >= 42 && id <= 44) {
        for (j = XEEN_MAP_RECORD_METADATA; j < XEEN_MAP_RECORD_ID; j++) {
            c->records[i].bytes[j] &= 0xf8;
        }
    }
    return XR_OK;
}
xeen_resource_status xeen_cache_prepare(const xeen_archive *a, xeen_bank bank, uint16_t id,
                                        xeen_patch_inputs patch, xeen_navigation_cache *out)
{
    /* The 3 x 3 map cache's slots: 0 the map, 1..4 its north, east, south and west neighbours, 5..8 the corners NE,
     * SE, SW and NW. The map of slot i + 1 is link pl[i] of slot ps[i]'s map, or when that is 0 link fl[i] of slot
     * fs[i]'s (round the corner the other way: NE is the north map's east link, else the east map's north link);
     * entry 8 is computed and unused. The program data's map_cache_links (4C03, 4C0C, 4C15, 4C1E; 1A608, 1A62C). */
    enum { N, E, S, W };
    static const unsigned ps[9] = {0, 0, 0, 0, 1, 2, 3, 4, 0}, pl[9] = {N, E, S, W, E, S, W, N, N};
    static const unsigned fs[9] = {0, 0, 0, 0, 2, 3, 4, 1, 0}, fl[9] = {N, E, S, W, N, E, S, W, N};
    xeen_navigation_cache c;
    unsigned i;
    if (!a || !out || (bank != XR_BANK_STATE && bank != XR_BANK_ASSETS_XOR35)) {
        return XR_BAD_INPUT;
    }
    memset(&c, 0, sizeof(c));
    for (i = 0; i < 9; i++) {
        xeen_resource_status s;
        c.ids[i] = id;
        if (id && (s = cache_slot_load(a, bank, id, patch, &c, i)) != XR_OK) {
            return s;
        }
        id = xeen_map_neighbor(&c.records[ps[i]], pl[i]);
        if (!id) {
            id = xeen_map_neighbor(&c.records[fs[i]], fl[i]);
        }
    }
    c.mode = xeen_map_mode(&c.records[0]) & 0x8000u;
    *out = c;
    return XR_OK;
}
xeen_resource_status xeen_cache_commit(const xeen_navigation_cache *c, xeen_movement *m)
{
    unsigned i;
    if (!c || !m) {
        return XR_BAD_INPUT;
    }
    for (i = 0; i < 9; i++) {
        memcpy(m->ds + XEEN_DS_MAP_DATA + i * XEEN_MAP_RECORD_BYTES, c->records[i].bytes, XEEN_MAP_RECORD_BYTES);
        xeen_write16(m, (uint16_t)(XEEN_DS_MAP_SLOT_IDS + 2 * i), c->ids[i]);
    }
    xeen_write16(m, XEEN_DS_OUTDOORS, c->mode);
    m->ds[XEEN_DS_PARTY_MAP] = (uint8_t)c->ids[0];
    return XR_OK;
}
