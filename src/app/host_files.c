/* The host's DOS file access (src/app/host_files.h). */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE /* struct dirent's d_type (glibc) */
#include "host_files.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#endif
/**
 * Reads the whole file at PATH into a new buffer (owned by the caller) and stores its size in *N;
 * returns NULL on failure.
 */
uint8_t *host_read_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    long len;
    uint8_t *b = NULL;
    if (!f) {
        return NULL;
    }
    if (!fseek(f, 0, SEEK_END) && (len = ftell(f)) > 0 && !fseek(f, 0, SEEK_SET)) {
        b = malloc((size_t)len);
        if (b && fread(b, 1, (size_t)len, f) == (size_t)len) {
            *n = (size_t)len;
        } else {
            free(b);
            b = NULL;
        }
    }
    fclose(f);
    return b;
}
/** qsort comparison of two 13-byte file names. */
static int compare_names(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}
/* Stores NAME upper-cased as the next of at most CAP names in OUT (count *N) when it can be a DOS name: shorter than
 * 13 characters and not hidden (a leading '.', which no DOS name has); returns whether it was stored. */
static int store_dos_name(char (*out)[13], unsigned *n, unsigned cap, const char *name)
{
    size_t len = strlen(name);
    if (len >= sizeof out[0] || name[0] == '.' || *n >= cap) {
        return 0;
    }
    for (size_t k = 0; k <= len; ++k) {
        out[*n][k] = (char)toupper((unsigned char)name[k]);
    }
    ++*n;
    return 1;
}
/**
 * Lists the DOS names of DIR's files into OUT: upper case, sorted, each once, at most CAP (entries past CAP are
 * dropped in the directory's enumeration order); returns how many, 0 when DIR cannot be read. A file is what the
 * original's find-first with attribute 0 answers (18E77 -> 13BFA, INT 21h AH=4Eh with CX = 0): not a directory and
 * not hidden or system (Win32 attributes; POSIX: a leading '.'). POSIX decides by the entry's own type (d_type),
 * never by a call on the file: directories, FIFOs, sockets and devices are left out; a symbolic link or an entry of
 * a file system without types stays listed (as before). No entry is opened or stat'ed: the game directory holds
 * XEEN.DAT, which the program never probes (Phase 184). On a case-sensitive file system two spellings of one name
 * are one DOS name. Phase 328: POSIX listed directories and devices, Win32 hidden and system files.
 */
unsigned host_directory_names(const char *dir, char (*out)[13], unsigned cap)
{
    unsigned n = 0, kept = 0;
#ifdef _WIN32
    char pattern[1024];
    struct _finddata_t f;
    intptr_t h;
    int len = snprintf(pattern, sizeof pattern, "%s/*", dir);
    if (len < 0 || (size_t)len >= sizeof pattern || (h = _findfirst(pattern, &f)) == -1) {
        return 0;
    }
    do {
        if (!(f.attrib & (_A_SUBDIR | _A_HIDDEN | _A_SYSTEM))) {
            store_dos_name(out, &n, cap, f.name);
        }
    } while (!_findnext(h, &f));
    _findclose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) {
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        if (e->d_type == DT_REG || e->d_type == DT_LNK || e->d_type == DT_UNKNOWN) {
            store_dos_name(out, &n, cap, e->d_name);
        }
    }
    closedir(d);
#endif
    qsort(out, n, sizeof out[0], compare_names);
    for (unsigned i = 0; i < n; ++i) {
        if (!kept || strcmp(out[i], out[kept - 1u])) {
            memmove(out[kept++], out[i], sizeof out[0]);
        }
    }
    return kept;
}
/* The listing's halves: the resource directory's names, then the save directory's. */
#define SAVE_LIST_HALF 512u
/**
 * Save-file listing service: the resource directory's names matching PATTERN, plus the save directory's names the
 * resource directory does not have, in sorted order; at most CAP are stored in NAMES. Returns how many were stored.
 * Each directory contributes at most SAVE_LIST_HALF names (one half of a static buffer: not reentrant).
 */
static unsigned save_list(void *u, const char *pattern, char names[][13], unsigned cap)
{
    static char all[2u * SAVE_LIST_HALF][13];
    const save_directory *d = u;
    unsigned n = host_directory_names(d->dir, all, SAVE_LIST_HALF), i, j, matched = 0;
    if (d->save_dir) {
        /* the save directory's names that the resources do not have, moved down after the resources' */
        unsigned extra = host_directory_names(d->save_dir, all + n, SAVE_LIST_HALF), added = 0;
        for (i = 0; i < extra; ++i) {
            for (j = 0; j < n && strcmp(all[j], all[n + i]); ++j) {
            }
            if (j == n) {
                memmove(all[n + added], all[n + i], 13);
                ++added;
            }
        }
        n += added;
        qsort(all, n, sizeof all[0], compare_names);
    }
    for (i = 0; i < n; ++i) {
        if (xeen_dos_match(pattern, all[i])) {
            if (matched < cap) {
                memcpy(names[matched], all[i], 13);
            }
            ++matched;
        }
    }
    return matched < cap ? matched : cap;
}
/* Writes into PATH (CAP bytes) the host path of DIR's file NAME, a DOS name of up to 12 characters matched against
 * the directory's DOS listing (host_directory_names: upper case, each name once): DIR/NAME upper-cased when that file
 * opens, else DIR/NAME lower-cased (a host file spelt in mixed case therefore never resolves). Returns 1 with PATH
 * written, 0 when NAME is too long or not listed, or PATH cannot be formatted. The listing is a static buffer: not
 * reentrant. */
int host_dos_path(const char *dir, const char *name, char *path, size_t cap)
{
    static char all[512][13];
    char want[13];
    unsigned n, i;
    size_t len = strlen(name);
    FILE *f;
    if (len >= sizeof want) {
        return 0;
    }
    for (i = 0; i <= len; ++i) {
        want[i] = (char)toupper((unsigned char)name[i]);
    }
    n = host_directory_names(dir, all, 512);
    for (i = 0; i < n; ++i) {
        if (!strcmp(all[i], want)) {
            if (snprintf(path, cap, "%s/%s", dir, want) < 0) {
                return 0;
            }
            if ((f = fopen(path, "rb")) != NULL) {
                fclose(f);
                return 1;
            }
            for (size_t k = 0; k <= len; ++k) {
                want[k] = (char)tolower((unsigned char)want[k]);
            }
            return snprintf(path, cap, "%s/%s", dir, want) > 0;
        }
    }
    return 0;
}
/* Phase 287: NAME is the game's configuration file (XEEN.CFG, any case). */
static int is_xeen_cfg(const char *name)
{
    static const char cfg[] = "XEEN.CFG";
    for (size_t i = 0; i < sizeof cfg; ++i) {
        if (toupper((unsigned char)name[i]) != cfg[i]) {
            return 0;
        }
    }
    return 1;
}
/**
 * Save-file read service: reads NAME from the save directory when it has it, else from the resource
 * directory; Phase 287: XEEN.CFG from its own directory only when one is set.
 */
uint8_t *host_save_read(void *u, const char *name, size_t *size)
{
    const save_directory *d = u;
    char path[1024];
    if (d->cfg_dir && is_xeen_cfg(name)) {
        return host_dos_path(d->cfg_dir, name, path, sizeof path) ? host_read_file(path, size) : NULL;
    }
    if (d->save_dir && host_dos_path(d->save_dir, name, path, sizeof path)) {
        return host_read_file(path, size);
    }
    return host_dos_path(d->dir, name, path, sizeof path) ? host_read_file(path, size) : NULL;
}
/* 195C5 writes a slot file: only into the writable save directory (an
 * existing file there in its own spelling, else the upper-case name). Phase 287: XEEN.CFG (33518, 16E85) into its
 * own directory when one is set (where it is read: one file for every reader and writer). */
static int save_write(void *u, const char *name, const uint8_t *bytes, size_t size)
{
    const save_directory *d = u;
    const char *dir = d->cfg_dir && is_xeen_cfg(name) ? d->cfg_dir : d->save_dir;
    char path[1024], upper[13];
    FILE *f;
    size_t len = strlen(name), k;
    int ok;
    if (!dir || len >= sizeof upper) {
        return 0;
    }
    if (!host_dos_path(dir, name, path, sizeof path)) {
        for (k = 0; k <= len; ++k) {
            upper[k] = (char)toupper((unsigned char)name[k]);
        }
        if (snprintf(path, sizeof path, "%s/%s", dir, upper) < 0) {
            return 0;
        }
    }
    if (!(f = fopen(path, "wb"))) {
        return 0;
    }
    ok = fwrite(bytes, 1, size, f) == size;
    ok = fclose(f) == 0 && ok;
    if (d->trace) {
        printf("file-op write %s %lu %s\n", name, (unsigned long)size,
               ok ? "ok" : "failed"); /* Evidence */
    }
    return ok;
}
/* 10A42 (INT 21h 41h) unlinks a slot file. Only the writable save
 * directory is ever changed: a name the read-only resources hold would stay
 * visible, and a file without write permission is read-only to DOS (and to
 * Windows' remove; POSIX unlink would delete it): both are refused, as DOS
 * answers error 5, and the original continues (18363 ignores the result). */
static int save_remove(void *u, const char *name)
{
    const save_directory *d = u;
    char path[1024];
    struct stat st;
    int ok = 0;
    if (!host_dos_path(d->dir, name, path, sizeof path) && d->save_dir &&
        host_dos_path(d->save_dir, name, path, sizeof path) && !stat(path, &st)) {
#ifdef _WIN32
        if (st.st_mode & _S_IWRITE) {
            ok = remove(path) == 0;
        }
#else
        if (st.st_mode & S_IWUSR) {
            ok = remove(path) == 0;
        }
#endif
    }
    if (d->trace) {
        printf("file-op remove %s %s\n", name, ok ? "ok" : "refused"); /* Evidence */
    }
    return ok;
}
/**
 * DOS clock service for saves: the session's fixed clock when deterministic, else the host's local
 * date and time.
 */
int host_save_clock(void *u, xeen_dos_clock *c)
{
    const save_directory *d = u;
    time_t seconds;
    struct tm *t;
    *c = d->clock;
    if (d->deterministic) {
        return 1;
    }
    seconds = time(NULL);
    t = localtime(&seconds);
    if (!t) {
        return 0;
    }
    *c = (xeen_dos_clock){(uint16_t)(t->tm_year + 1900),
                          (uint8_t)(t->tm_mon + 1),
                          (uint8_t)t->tm_mday,
                          (uint8_t)t->tm_hour,
                          (uint8_t)t->tm_min,
                          (uint8_t)t->tm_sec,
                          0};
    return 1;
}
xeen_save_files host_save_services(save_directory *d)
{
    return (xeen_save_files){save_list,  host_save_read,  d,
                             save_write, host_save_clock, save_remove};
}
