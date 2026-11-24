#ifndef XEEN_MAIN_MENU_H
#define XEEN_MAIN_MENU_H
#include "xeen/movement.h"
#include <stddef.h>
/* The DOS date and time (INT 21h 2Ah/2Ch) the C runtime's time() reads. */
typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second, hundredth;
} xeen_dos_clock;
/* Host save-file service, the platform side of the original's DOS file
 * access in the game directory (13BFA/13C2D find, 14033/14781/13168 open,
 * read, close). list() writes up to cap names matching a DOS pattern, upper
 * case, in the host's directory order (wox and the menu replay:
 * ascending), and returns their count. read() returns a malloc'd copy of a
 * named file (case-insensitive) or NULL. The 1978C copies are kept in memory
 * by the game. write() replaces or creates a named file with the
 * given bytes (195C5, the CUR -> slot copy of a save) and returns 0 when the
 * host refuses (no writable save directory: the original's fatal 2C80D);
 * clock() supplies the DOS date and time. Both are optional (NULL).
 * remove() is 10A42's INT 21h 41h: 1 when the named file is gone,
 * 0 when the host refuses (DOS error 5: a read-only file, or one the
 * read-only resources hold); optional (NULL refuses). */
typedef struct {
    unsigned (*list)(void *, const char *pattern, char names[][13], unsigned cap);
    uint8_t *(*read)(void *, const char *name, size_t *size);
    void *user;
    int (*write)(void *, const char *name, const uint8_t *bytes, size_t size);
    int (*clock)(void *, xeen_dos_clock *);
    int (*remove)(void *, const char *name);
} xeen_save_files;
/* The C runtime's time() (1160C: 1068D getdate, 106A0 gettime, 14DEC
 * dostounix): 1511A tzset without a TZ variable writes timezone 18000
 * (8C14), daylight 1 (8C18) and the tzname buffers ("EST" at FD74, "EDT" at
 * FD70) into the DS; then seconds since 1970 for that local time, minus an
 * hour when 15308 (__isDST, the US rules of 1987) says daylight time. The
 * month tables are the DS's (8BE6, 8BF2): M must hold the program data. */
uint32_t xeen_dos_time(xeen_movement *, const xeen_dos_clock *);
/* The C runtime's ctime(T) as the original's Borland runtime computes it: tzset as above, the local time T minus the
 * time zone broken down into the static struct tm (XEEN_DS_CRT_TM; a time before 1970 is 1970's start, and daylight
 * time by the 1987 US rules moves the hour on), then the text "Www Mmm dd hh:mm:ss yyyy\n" into asctime's static
 * buffer. Returns that buffer's DS offset (XEEN_DS_CRT_ASCTIME_TEXT). Original: 13472 = 13447 asctime(13411
 * localtime), comtime 131AE, the format DS:8BAF. */
uint16_t xeen_dos_ctime(xeen_movement *, uint32_t t);
/* DOS FCB-style 8.3 match: '?' matches any character or the padding, '*'
 * fills its part with '?'. Case-insensitive. */
int xeen_dos_match(const char *pattern, const char *name);
/* 199B7 over a save file's bytes: the archive count word, then 31 bytes at
 * count*8+2 (maze.nam, the first member's data: the game name and its id
 * byte at +1Eh). The full read continues with 297Ch roster bytes and 5FAh
 * party bytes in file order and yields the party flag (party +1Bh, 'A'/'W')
 * and the highest 2C909 level of the members whose roster name is set.
 * Returns 0 when the file is shorter than the reads (the original would
 * continue with a short read; the native refuses). */
typedef struct {
    uint8_t name[31], preference, level; /* the game name (maze.nam); XEEN_DS_GAME_PREFERENCE */
} xeen_save_header;
int xeen_save_header_read(const uint8_t *file, size_t size, int full, xeen_save_header *out);
/* The resident tables' limits: spells.xen's 77 entries (278B4's CMP 4Dh) and MAE.xen's 131 (27924's CMP 83h); 15FE9
 * passes the same. */
#define XEEN_SPELL_NAMES_LIMIT    0x4du
#define XEEN_MATERIAL_NAMES_LIMIT 0x83u
/* The 15FE9/43C37 resident pointer table over a loaded resource: base word
 * pair (offset 0, segment) at DS base/base+2, then for each NUL byte at i of
 * the resource (limit = DS 8582, the size 4A877 stored) while the counter is
 * below limit, entry n (n from 1) = offset i+1, segment. Returns the final
 * counter (15FE9 keeps it in F606; 43C37 in SI). */
unsigned xeen_resident_table(xeen_movement *, uint16_t base, uint16_t segment,
                             const uint8_t *resource, size_t size, unsigned limit);
#endif
