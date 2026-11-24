/*
 * Phase 287: the game's three CC archives read directly from a CD image (ISO 9660), read-only, without extracting it
 * or holding it in memory, and their selection among GAME1.GOG, GAME2.GOG and the loose files of the data directory.
 *
 * Supported layouts (both seen on real images, xeen_dat_evidence/phase287/PHASE287_PROGRESS.md): 2048-byte ISO sectors
 * ("cooked"), and raw 2352-byte CD sectors of mode 1 (sync 00 FF x10 00, a 4-byte header with mode 01, 2048 user bytes
 * at +16; the GOG images GAME1.GOG / GAME2.GOG and the CD's WOX1.bin / WOX2.bin). Anything else (mode 2, 2336-byte
 * sectors, other block sizes, multi-extent or interleaved files) is refused with its reason.
 */
#ifndef XEEN_APP_HOST_ISO_H
#define XEEN_APP_HOST_ISO_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* The largest member read (each CC archive is about 13 MB on the CD). */
#define HOST_ISO_MAX_FILE (64u * 1024u * 1024u)
/* The largest directory read (the images' directories are one 2048-byte block). */
#define HOST_ISO_MAX_DIRECTORY (256u * 1024u)

typedef struct {
    FILE *f;
    unsigned sector;       /* bytes per sector in the file: 2048 or 2352 */
    unsigned data_offset;  /* user data within a sector: 0 or 16 */
    uint32_t sectors;      /* whole sectors in the file */
    uint32_t volume_blocks; /* the primary volume descriptor's volume space size */
    uint32_t root_lba, root_length;
    char label[33];        /* the volume identifier, trimmed */
    uint64_t pos;          /* the stream's position after the last read (a sequential read needs no seek) */
    char *buffer;          /* the stream's fixed buffer (HOST_ISO_STREAM_BUFFER bytes), freed at close */
} host_iso;
/* The image stream's buffer: one allocation per open image, so its member reads are few large reads. */
#define HOST_ISO_STREAM_BUFFER (256u * 1024u)

typedef struct {
    uint32_t lba, length;
    char record[40]; /* the directory and identifier as recorded, e.g. "/GAME/XEEN.CC;1" */
} host_iso_file;

/* Opens PATH read-only and validates its layout and primary volume descriptor. 1, or 0 with the reason in WHY (the
 * file is closed again). */
int host_iso_open(host_iso *, const char *path, char *why, size_t why_size);
/* Finds NAMES[0..COUNT) (8.3 names, any case; a ";N" version and a trailing '.' are ignored) all in one directory: the
 * root, else its GAME directory. 1 with OUT[i] set, or 0 with the reason. */
int host_iso_find(host_iso *, const char *const *names, unsigned count, host_iso_file *out, char *why,
                  size_t why_size);
/* Reads FILE whole into a new buffer (owned by the caller, FILE->length bytes) through a fixed sector buffer, each raw
 * sector's sync and mode checked. NULL with the reason on a short read or a bad sector. */
uint8_t *host_iso_read(host_iso *, const host_iso_file *, char *why, size_t why_size);
void host_iso_close(host_iso *);

/* The game's archives XEEN.CC, DARK.CC and INTRO.CC chosen as a unit: the first of GAME1.GOG and GAME2.GOG in the data
 * directory (as named, upper or lower case) that is a supported image holding all three in one directory, each an
 * archive the CC reader accepts with at least one entry; else the loose XEEN.CC, DARK.CC and the optional INTRO.CC of
 * the data directory, read as before. Never a mixture. */
enum { HOST_CC_XEEN, HOST_CC_DARK, HOST_CC_INTRO, HOST_CC_COUNT };
typedef struct {
    uint8_t *bytes[HOST_CC_COUNT];
    size_t sizes[HOST_CC_COUNT];
    int image;                       /* 1: from an image; 0: the loose files */
    char where[HOST_CC_COUNT][1200]; /* each archive's source for messages ("DIR/GAME1.GOG:/GAME/XEEN.CC;1") */
    char image_path[1100];           /* the image chosen, "" for the loose files */
    char rejected[2][1700];          /* why GAME1.GOG / GAME2.GOG was not used ("" when absent) */
} host_cc_set;
/* Fills SET from the data directory DIR. Returns 1 when an image was chosen or the loose files were read (a loose
 * archive missing is left NULL for the caller's existing report); the image candidates' reasons are kept. */
int host_cc_select(const char *dir, host_cc_set *set);
void host_cc_free(host_cc_set *);
#endif
