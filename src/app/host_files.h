/*
 * The host's DOS file access (the native adapter of the original's INT 21h file services in its one game
 * directory): whole-file reads, the 8.3 directory listing, the host spelling of a DOS name, and the
 * save-file services the game binds (xeen_save_files: list, read, write, remove and the DOS clock).
 */
#ifndef XEEN_APP_HOST_FILES_H
#define XEEN_APP_HOST_FILES_H
#include "xeen/main_menu.h"
#include <stddef.h>
#include <stdint.h>

/* The original's DOS file access in the game directory: the game data
 * directory (--data-dir, wox.cfg [files] data_dir; Phase 270), read-only, overlaid copy-on-write by an optional writable
 * --save-dir (its files shadow the resources' ones; new files are created
 * there, never in the resources). Names upper case, ascending. The DOS
 * clock: headless runs use --dos-date and the --rng-time clock. */
typedef struct {
    const char *dir, *save_dir;
    int deterministic;
    xeen_dos_clock clock;
    int trace;
    /* Phase 287: XEEN.CFG's directory ([files] xeen_cfg_dir): the game's reads and writes of XEEN.CFG through
     * these services go there only; NULL: XEEN.CFG is a file of the overlay like the slots (a headless developer run). */
    const char *cfg_dir;
} save_directory;

/* Reads the whole file at PATH into a new buffer (owned by the caller) and stores its size in *N;
 * returns NULL on failure. */
uint8_t *host_read_file(const char *path, size_t *n);
/* Lists the DOS names (upper case, at most 12 characters) of DIR's files (no directory, device or hidden entry; by the
 * entries' own types, no file probed) into OUT, sorted and each once, at most CAP; returns how many (0 when DIR cannot
 * be read). The original's find-first, attribute 0. */
unsigned host_directory_names(const char *dir, char (*out)[13], unsigned cap);
/* The host spelling of DOS name NAME in DIR (upper case, else lower case) into PATH; 0 when DIR has
 * no such file. */
int host_dos_path(const char *dir, const char *name, char *path, size_t cap);
/* The save-file read service (U a save_directory): NAME from the save directory when it has it, else
 * from the resource directory. */
uint8_t *host_save_read(void *u, const char *name, size_t *size);
/* The DOS clock service (U a save_directory): its fixed clock when deterministic, else the host's
 * local date and time; returns 0 when the local time is unavailable. */
int host_save_clock(void *u, xeen_dos_clock *c);
/* The save-file services of directory D (borrowed while the game holds them). */
xeen_save_files host_save_services(save_directory *d);
#endif
