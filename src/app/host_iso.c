/* Phase 287: CD-image (ISO 9660) archives and their selection (src/app/host_iso.h). */
#if !defined(_WIN32)
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#endif
#include "host_iso.h"
#include "host_files.h"
#include "xeen/resources.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#define iso_seek(f, o) _fseeki64((f), (__int64)(o), SEEK_SET)
#define iso_end(f)     _fseeki64((f), 0, SEEK_END)
#define iso_tell(f)    ((int64_t)_ftelli64(f))
#else
#include <sys/types.h>
#define iso_seek(f, o) fseeko((f), (off_t)(o), SEEK_SET)
#define iso_end(f)     fseeko((f), 0, SEEK_END)
#define iso_tell(f)    ((int64_t)ftello(f))
#endif

#define ISO_BLOCK 2048u
/* ECMA-119's fields as the reader uses them. A directory record: its length, the extended attribute record's length,
 * the extent's block and its byte size (each recorded little-endian, then big-endian), the flags, the interleave unit
 * and gap, the name's length and the name; the smallest record has a one-byte name. */
enum {
    DR_LENGTH = 0, DR_XAR = 1, DR_EXTENT = 2, DR_SIZE = 10, DR_FLAGS = 25, DR_UNIT = 26, DR_GAP = 27,
    DR_NAME_LENGTH = 32, DR_NAME = 33, DR_MIN = 34
};
enum { DR_FLAG_DIRECTORY = 2u, DR_FLAG_MULTI_EXTENT = 0x80u };
/* A volume descriptor: its type (1 the primary, 255 the set's terminator), "CD001" and version 1; the primary's
 * volume label, block count (both-endian 32-bit), logical block size (both-endian 16-bit) and root directory record. */
enum {
    VD_TYPE = 0, VD_ID = 1, VD_VERSION = 6, VD_LABEL = 40, VD_BLOCKS = 80, VD_BLOCK_SIZE = 128, VD_ROOT = 156
};
enum { VD_PRIMARY = 1u, VD_TERMINATOR = 255u, VD_LABEL_BYTES = 32 };
#define RAW_SECTOR 2352u
static const uint8_t raw_sync[12] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[3] | (uint32_t)p[2] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[0] << 24;
}
static unsigned le16(const uint8_t *p)
{
    return p[0] | (unsigned)p[1] << 8;
}
static unsigned be16(const uint8_t *p)
{
    return p[1] | (unsigned)p[0] << 8;
}
/* The both-endian 32-bit field at P (little-endian, then big-endian) agrees with itself. */
static int both_endian32(const uint8_t *p)
{
    return le32(p) == be32(p + 4);
}

/* Reads the 2048 user bytes of sector N into OUT; for a raw image the sector's sync and mode are checked. */
static int read_block(host_iso *iso, uint32_t n, uint8_t out[ISO_BLOCK], char *why, size_t why_size)
{
    uint8_t raw[RAW_SECTOR];
    if (n >= iso->sectors) {
        snprintf(why, why_size, "sector %lu is past the end of the file (%lu sectors)", (unsigned long)n,
                 (unsigned long)iso->sectors);
        return 0;
    }
    if ((iso->pos != (uint64_t)n * iso->sector && iso_seek(iso->f, (uint64_t)n * iso->sector) != 0) ||
        fread(iso->sector == RAW_SECTOR ? raw : out, 1, iso->sector, iso->f) != iso->sector) {
        iso->pos = UINT64_MAX;
        snprintf(why, why_size, "short read at sector %lu", (unsigned long)n);
        return 0;
    }
    iso->pos = ((uint64_t)n + 1u) * iso->sector;
    if (iso->sector == RAW_SECTOR) {
        if (memcmp(raw, raw_sync, sizeof raw_sync) || raw[15] != 1u) {
            snprintf(why, why_size, "sector %lu is not a mode 1 CD sector", (unsigned long)n);
            return 0;
        }
        memcpy(out, raw + iso->data_offset, ISO_BLOCK);
    }
    return 1;
}

/* The layout: raw mode-1 2352-byte sectors (a sync at sector 16), else 2048-byte sectors; the descriptor at sector 16
 * must then be a volume descriptor ("CD001"). */
static int detect_layout(host_iso *iso, uint64_t size, char *why, size_t why_size)
{
    uint8_t b[RAW_SECTOR];
    if (size >= 17u * (uint64_t)RAW_SECTOR && !iso_seek(iso->f, 16u * (uint64_t)RAW_SECTOR) &&
        fread(b, 1, RAW_SECTOR, iso->f) == RAW_SECTOR && !memcmp(b, raw_sync, sizeof raw_sync)) {
        if (b[15] != 1u) {
            snprintf(why, why_size, "raw CD sectors of mode %u (only mode 1 is supported)", b[15]);
            return 0;
        }
        if (size % RAW_SECTOR) {
            snprintf(why, why_size, "raw 2352-byte sectors, but the file ends inside a sector (truncated)");
            return 0;
        }
        iso->sector = RAW_SECTOR;
        iso->data_offset = 16u;
        iso->sectors = size / RAW_SECTOR > UINT32_MAX ? UINT32_MAX : (uint32_t)(size / RAW_SECTOR);
        return 1;
    }
    if (size >= 17u * (uint64_t)ISO_BLOCK && !iso_seek(iso->f, 16u * (uint64_t)ISO_BLOCK) &&
        fread(b, 1, ISO_BLOCK, iso->f) == ISO_BLOCK && !memcmp(b + 1, "CD001", 5)) {
        if (size % ISO_BLOCK) {
            snprintf(why, why_size, "2048-byte sectors, but the file ends inside a sector (truncated)");
            return 0;
        }
        iso->sector = ISO_BLOCK;
        iso->data_offset = 0;
        iso->sectors = size / ISO_BLOCK > UINT32_MAX ? UINT32_MAX : (uint32_t)(size / ISO_BLOCK);
        return 1;
    }
    snprintf(why, why_size, "not an ISO 9660 image (no volume descriptor at sector 16 in 2048-byte or raw "
                            "2352-byte mode-1 sectors)");
    return 0;
}

int host_iso_open(host_iso *iso, const char *path, char *why, size_t why_size)
{
    uint8_t b[ISO_BLOCK];
    int64_t size;
    memset(iso, 0, sizeof *iso);
    if (!(iso->f = fopen(path, "rb"))) {
        snprintf(why, why_size, "cannot open it");
        return 0;
    }
    /* One fixed stream buffer (glibc ignores a size given with a NULL buffer): few, large reads of the image. */
    if ((iso->buffer = malloc(HOST_ISO_STREAM_BUFFER)) != NULL &&
        setvbuf(iso->f, iso->buffer, _IOFBF, HOST_ISO_STREAM_BUFFER) != 0) {
        free(iso->buffer);
        iso->buffer = NULL;
    }
    if (iso_end(iso->f) != 0 || (size = iso_tell(iso->f)) < 0) {
        snprintf(why, why_size, "cannot read its size");
        host_iso_close(iso);
        return 0;
    }
    iso->pos = UINT64_MAX; /* unknown until the first read_block seeks */
    if (!detect_layout(iso, (uint64_t)size, why, why_size)) {
        host_iso_close(iso);
        return 0;
    }
    /* The volume descriptor set from sector 16 up to its terminator (at most 32 descriptors): the first primary. */
    for (uint32_t n = 16;; ++n) {
        if (n >= 48u) {
            snprintf(why, why_size, "no primary volume descriptor among the first 32");
            host_iso_close(iso);
            return 0;
        }
        if (!read_block(iso, n, b, why, why_size)) {
            host_iso_close(iso);
            return 0;
        }
        if (memcmp(b + VD_ID, "CD001", 5) || b[VD_VERSION] != 1u) {
            snprintf(why, why_size, "malformed volume descriptor set (sector %lu)", (unsigned long)n);
            host_iso_close(iso);
            return 0;
        }
        if (b[VD_TYPE] == VD_PRIMARY) {
            break;
        }
        if (b[VD_TYPE] == VD_TERMINATOR) {
            snprintf(why, why_size, "no primary volume descriptor");
            host_iso_close(iso);
            return 0;
        }
    }
    if (le16(b + VD_BLOCK_SIZE) != ISO_BLOCK || be16(b + VD_BLOCK_SIZE + 2) != ISO_BLOCK) {
        snprintf(why, why_size, "logical block size %u (only 2048 is supported)", le16(b + VD_BLOCK_SIZE));
        host_iso_close(iso);
        return 0;
    }
    iso->volume_blocks = le32(b + VD_BLOCKS);
    if (!both_endian32(b + VD_BLOCKS) || b[VD_ROOT + DR_LENGTH] < DR_MIN ||
        !(b[VD_ROOT + DR_FLAGS] & DR_FLAG_DIRECTORY) || !both_endian32(b + VD_ROOT + DR_EXTENT) ||
        !both_endian32(b + VD_ROOT + DR_SIZE)) {
        snprintf(why, why_size, "malformed primary volume descriptor");
        host_iso_close(iso);
        return 0;
    }
    if (iso->volume_blocks > iso->sectors) {
        snprintf(why, why_size, "truncated: the volume has %lu blocks, the file %lu", (unsigned long)iso->volume_blocks,
                 (unsigned long)iso->sectors);
        host_iso_close(iso);
        return 0;
    }
    iso->root_lba = le32(b + VD_ROOT + DR_EXTENT);
    iso->root_length = le32(b + VD_ROOT + DR_SIZE);
    memcpy(iso->label, b + VD_LABEL, VD_LABEL_BYTES);
    iso->label[VD_LABEL_BYTES] = 0;
    for (int i = VD_LABEL_BYTES - 1; i >= 0 && iso->label[i] == ' '; --i) {
        iso->label[i] = 0;
    }
    return 1;
}

void host_iso_close(host_iso *iso)
{
    if (iso && iso->f) {
        fclose(iso->f); /* before its buffer is freed */
        iso->f = NULL;
    }
    if (iso) {
        free(iso->buffer);
        iso->buffer = NULL;
    }
}

/* An extent of LENGTH bytes at LBA lies inside the volume and the file. */
static int extent_ok(const host_iso *iso, uint32_t lba, uint32_t length)
{
    uint64_t blocks = ((uint64_t)length + ISO_BLOCK - 1u) / ISO_BLOCK;
    return lba + blocks <= iso->volume_blocks && lba + blocks <= iso->sectors;
}

/* A directory's records, read whole (bounded). */
typedef struct {
    uint8_t *bytes;
    uint32_t length;
} iso_directory;
static int read_directory(host_iso *iso, uint32_t lba, uint32_t length, iso_directory *d, char *why, size_t n)
{
    d->bytes = NULL;
    if (!length || length > HOST_ISO_MAX_DIRECTORY || length % ISO_BLOCK || !extent_ok(iso, lba, length)) {
        snprintf(why, n, "malformed directory extent (block %lu, %lu bytes)", (unsigned long)lba, (unsigned long)length);
        return 0;
    }
    if (!(d->bytes = malloc(length))) {
        snprintf(why, n, "out of memory");
        return 0;
    }
    d->length = length;
    for (uint32_t i = 0; i < length / ISO_BLOCK; ++i) {
        if (!read_block(iso, lba + i, d->bytes + (size_t)i * ISO_BLOCK, why, n)) {
            free(d->bytes);
            d->bytes = NULL;
            return 0;
        }
    }
    return 1;
}

/* One directory record. */
typedef struct {
    uint32_t lba, length;
    unsigned flags, xar, unit, gap;
    char name[32]; /* upper case, without ";N" or a trailing '.' */
    char raw[32];  /* as recorded */
} iso_record;
/* The next record at *POS of D: 1 with R, 0 at the end, -1 for a malformed record. */
static int next_record(const iso_directory *d, uint32_t *pos, iso_record *r)
{
    for (;;) {
        uint32_t at = *pos, block_end = (at / ISO_BLOCK + 1u) * ISO_BLOCK;
        unsigned len, name_len;
        if (at >= d->length) {
            return 0;
        }
        len = d->bytes[at + DR_LENGTH];
        if (!len) {
            *pos = block_end; /* the rest of the block is padding */
            continue;
        }
        if (len < DR_MIN || at + len > block_end) {
            return -1;
        }
        const uint8_t *b = d->bytes + at;
        name_len = b[DR_NAME_LENGTH];
        if (!name_len || DR_NAME + name_len > len || name_len >= sizeof r->raw) {
            return -1;
        }
        if (!both_endian32(b + DR_EXTENT) || !both_endian32(b + DR_SIZE)) {
            return -1;
        }
        r->lba = le32(b + DR_EXTENT);
        r->length = le32(b + DR_SIZE);
        r->flags = b[DR_FLAGS];
        r->xar = b[DR_XAR];
        r->unit = b[DR_UNIT];
        r->gap = b[DR_GAP];
        memcpy(r->raw, b + DR_NAME, name_len);
        r->raw[name_len] = 0;
        {
            unsigned k = 0;
            for (unsigned i = 0; i < name_len && b[DR_NAME + i] != ';'; ++i) {
                r->name[k++] = (char)toupper(b[DR_NAME + i]);
            }
            while (k && r->name[k - 1u] == '.') {
                --k;
            }
            r->name[k] = 0;
        }
        if (name_len == 1u && (b[DR_NAME] == 0u || b[DR_NAME] == 1u)) {
            r->name[0] = 0; /* the directory itself or its parent */
        }
        *pos = at + len;
        return 1;
    }
}

/* One directory's lookup of NAMES (COUNT, at most 8): PREFIX begins each record text ("/" or "/GAME/"), OUT[i]
 * receives name i's file, FOUND[i] counts its entries, and WHY (N bytes) the reason a lookup fails. */
typedef struct {
    const char *prefix;
    const char *const *names;
    unsigned count;
    host_iso_file *out;
    unsigned found[8];
    char *why;
    size_t n;
} iso_lookup;

/* Whether record R is the file NAME: the record's name (upper case) against NAME in upper case; a NAME of 16
 * characters or more is never found. */
static int record_named(const iso_record *r, const char *name)
{
    char want[16];
    size_t w = strlen(name);
    if (w >= sizeof want) {
        return 0;
    }
    for (size_t j = 0; j <= w; ++j) {
        want[j] = (char)toupper((unsigned char)name[j]);
    }
    return !strcmp(r->name, want);
}
/* Record R found as name I: 1 with OUT[I] set (its extent and record text) when it is that name's first entry and a
 * file the reader supports (one extent, not interleaved, no extended attributes, 1..HOST_ISO_MAX_FILE bytes inside
 * the image), else 0 with the reason. */
static int take_file(host_iso *iso, iso_lookup *l, unsigned i, const iso_record *r)
{
    if (l->found[i]++) {
        snprintf(l->why, l->n, "%s%s: more than one entry", l->prefix, l->names[i]);
        return 0;
    }
    if ((r->flags & DR_FLAG_MULTI_EXTENT) || r->unit || r->gap || r->xar) {
        snprintf(l->why, l->n, "%s%s: a multi-extent, interleaved or extended-attribute file (not supported)",
                 l->prefix, r->raw);
        return 0;
    }
    if (!r->length || r->length > HOST_ISO_MAX_FILE || !extent_ok(iso, r->lba, r->length)) {
        snprintf(l->why, l->n, "%s%s: its extent (block %lu, %lu bytes) is empty, too large or outside the image",
                 l->prefix, r->raw, (unsigned long)r->lba, (unsigned long)r->length);
        return 0;
    }
    l->out[i].lba = r->lba;
    l->out[i].length = r->length;
    snprintf(l->out[i].record, sizeof l->out[i].record, "%s%s", l->prefix, r->raw);
    return 1;
}
/* A directory record R: the GAME subdirectory's extent into GAME_DIR when wanted (a second GAME fails), any other
 * directory ignored. Returns 0 with the reason when it fails. */
static int note_directory(const iso_record *r, iso_record *game_dir, iso_lookup *l)
{
    if (game_dir && !strcmp(r->name, "GAME")) {
        if (game_dir->lba) {
            snprintf(l->why, l->n, "two GAME directories");
            return 0;
        }
        *game_dir = *r;
    }
    return 1;
}
/* A file record R against every wanted name, in order (take_file for each it is). Returns 0 when one fails. */
static int match_file(host_iso *iso, iso_lookup *l, const iso_record *r)
{
    for (unsigned i = 0; i < l->count; ++i) {
        if (record_named(r, l->names[i]) && !take_file(iso, l, i, r)) {
            return 0;
        }
    }
    return 1;
}
/* Looks L's names up in the directory at LBA/LENGTH, record by record: the directory itself and its parent skipped,
 * directories (note_directory), files (match_file). 1 when every name is found once (L's OUT set), 0 with the reason:
 * the walk's first failure, a malformed record, then the first missing name. GAME_DIR, when given, gets the GAME
 * subdirectory's extent (lba 0 when there is none). */
static int search(host_iso *iso, uint32_t lba, uint32_t length, iso_lookup *l, iso_record *game_dir)
{
    iso_directory d;
    iso_record r;
    uint32_t pos = 0;
    int k = 0, ok = 1;
    if (l->count > sizeof l->found / sizeof l->found[0] || !read_directory(iso, lba, length, &d, l->why, l->n)) {
        return 0;
    }
    memset(l->found, 0, sizeof l->found);
    if (game_dir) {
        memset(game_dir, 0, sizeof *game_dir);
    }
    while (ok && (k = next_record(&d, &pos, &r)) == 1) {
        if (r.name[0]) {
            ok = r.flags & DR_FLAG_DIRECTORY ? note_directory(&r, game_dir, l) : match_file(iso, l, &r);
        }
    }
    free(d.bytes);
    if (!ok) {
        return 0;
    }
    if (k < 0) {
        snprintf(l->why, l->n, "malformed directory record in %s", *l->prefix ? l->prefix : "/");
        return 0;
    }
    for (unsigned i = 0; i < l->count; ++i) {
        if (!l->found[i]) {
            snprintf(l->why, l->n, "no %s in %s", l->names[i], *l->prefix ? l->prefix : "/");
            return 0;
        }
    }
    return 1;
}

int host_iso_find(host_iso *iso, const char *const *names, unsigned count, host_iso_file *out, char *why,
                  size_t why_size)
{
    iso_record game = {0}; /* lba 0 (no GAME directory) when the root search fails before it looks */
    char root_why[300] = "";
    iso_lookup root = {.prefix = "/", .names = names, .count = count, .out = out, .why = root_why,
                       .n = sizeof root_why};
    iso_lookup game_lookup = {.prefix = "/GAME/", .names = names, .count = count, .out = out, .why = why,
                              .n = why_size};
    if (search(iso, iso->root_lba, iso->root_length, &root, &game)) {
        return 1;
    }
    if (!game.lba || strncmp(root_why, "no ", 3)) {
        snprintf(why, why_size, "%s", root_why); /* a malformed root, or nothing to search beside it */
        return 0;
    }
    return search(iso, game.lba, game.length, &game_lookup, NULL);
}

uint8_t *host_iso_read(host_iso *iso, const host_iso_file *file, char *why, size_t why_size)
{
    uint8_t block[ISO_BLOCK], *b;
    uint32_t done = 0;
    if (!file->length || file->length > HOST_ISO_MAX_FILE || !(b = malloc(file->length))) {
        snprintf(why, why_size, "%s: cannot hold %lu bytes", file->record, (unsigned long)file->length);
        return NULL;
    }
    for (uint32_t i = 0; done < file->length; ++i) {
        uint32_t take = file->length - done < ISO_BLOCK ? file->length - done : ISO_BLOCK;
        char sector_why[200];
        if (!read_block(iso, file->lba + i, block, sector_why, sizeof sector_why)) {
            snprintf(why, why_size, "%s: %s", file->record, sector_why);
            free(b);
            return NULL;
        }
        memcpy(b + done, block, take);
        done += take;
    }
    return b;
}

/* ---------------------------------------------------------------- the selection */
static const char *const cc_names[HOST_CC_COUNT] = {"XEEN.CC", "DARK.CC", "INTRO.CC"};

/* Tries image PATH: all three archives in one directory, each read and accepted by the CC reader (an index of at least
 * one entry). 1 with SET filled, or 0 with the reason (nothing kept). */
static int try_image(const char *path, host_cc_set *set, char *why, size_t n)
{
    host_iso iso;
    host_iso_file files[HOST_CC_COUNT];
    if (!host_iso_open(&iso, path, why, n)) {
        return 0;
    }
    if (!host_iso_find(&iso, cc_names, HOST_CC_COUNT, files, why, n)) {
        host_iso_close(&iso);
        return 0;
    }
    for (unsigned i = 0; i < HOST_CC_COUNT; ++i) {
        xeen_archive a;
        if (!(set->bytes[i] = host_iso_read(&iso, &files[i], why, n))) {
            break;
        }
        set->sizes[i] = files[i].length;
        if (xeen_archive_open(&a, set->bytes[i], set->sizes[i]) != XR_OK || !a.count) {
            snprintf(why, n, "%s: not a CC archive the game can read", files[i].record);
            free(set->bytes[i]);
            set->bytes[i] = NULL;
            break;
        }
        snprintf(set->where[i], sizeof set->where[i], "%s:%s", path, files[i].record);
    }
    host_iso_close(&iso);
    if (!set->bytes[HOST_CC_COUNT - 1u] || !set->bytes[0] || !set->bytes[1]) {
        host_cc_free(set);
        return 0;
    }
    return 1;
}

int host_cc_select(const char *dir, host_cc_set *set)
{
    static const char *const images[2] = {"GAME1.GOG", "GAME2.GOG"};
    memset(set, 0, sizeof *set);
    for (unsigned k = 0; k < 2u; ++k) {
        char path[1100], why[500];
        if (!host_dos_path(dir, images[k], path, sizeof path)) {
            continue; /* absent */
        }
        if (try_image(path, set, why, sizeof why)) {
            set->image = 1;
            snprintf(set->image_path, sizeof set->image_path, "%s", path);
            return 1;
        }
        snprintf(set->rejected[k], sizeof set->rejected[k], "%s: %s", path, why);
    }
    for (unsigned i = 0; i < HOST_CC_COUNT; ++i) {
        int written = snprintf(set->where[i], sizeof set->where[i], "%s/%s", dir, cc_names[i]);
        if (written > 0 && (size_t)written < sizeof set->where[i]) {
            set->bytes[i] = host_read_file(set->where[i], &set->sizes[i]);
        }
    }
    return 1;
}

void host_cc_free(host_cc_set *set)
{
    for (unsigned i = 0; i < HOST_CC_COUNT; ++i) {
        free(set->bytes[i]);
        set->bytes[i] = NULL;
        set->sizes[i] = 0;
    }
}
