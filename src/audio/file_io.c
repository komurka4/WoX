/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Translate native paths to the player's UTF-8 boundary. xeen_fopen supports archive, config
 * and synth modules with read-only case-tolerant asset lookup (Windows: through xeen_wide's wide paths).
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "file_io.h"
#ifndef _WIN32
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>

/* Convert a borrowed UTF-8 string to an allocated NUL-terminated Windows wide string. The
 * caller frees the result. Return NULL for null input, invalid encoding or
 * allocation/conversion failure.
 */
static wchar_t *xeen_wide(const char *s)
{
    int n;
    wchar_t *w;
    if (!s) {
        return NULL;
    }
    n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (!n) {
        return NULL;
    }
    w = malloc((size_t)n * sizeof(*w));
    if (w && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n)) {
        free(w);
        return NULL;
    }
    return w;
}

/** Compares two wide-character names ignoring ASCII case; returns nonzero when they are equal. */
static int file_io_equal_wide_case(const wchar_t *a, const wchar_t *b)
{
    while (*a && *b) {
        wchar_t x = *a++;
        wchar_t y = *b++;
        if (x >= L'A' && x <= L'Z') {
            x += L'a' - L'A';
        }
        if (y >= L'A' && y <= L'Z') {
            y += L'a' - L'A';
        }
        if (x != y) {
            return 0;
        }
    }
    return *a == *b;
}

/* Retry a read-mode open by matching ASCII filename case component by component, including
 * case-sensitive WSL shares. Borrow wide path and mode; return a caller-owned FILE or NULL on
 * lookup, allocation or open failure. Lookup never renames or creates files.
 */
static FILE *file_io_read_wide_case(const wchar_t *path, const wchar_t *mode)
{
    DWORD capacity = GetFullPathNameW(path, 0, NULL, NULL);
    DWORD length;
    wchar_t *copy;
    wchar_t *query;
    size_t start = 0;
    size_t n;
    FILE *f;
    if (!capacity) {
        return NULL;
    }
    copy = malloc(((size_t)capacity + 2) * sizeof(*copy));
    query = malloc(((size_t)capacity + 2) * sizeof(*query));
    if (!copy || !query) {
        free(copy);
        free(query);
        return NULL;
    }
    length = GetFullPathNameW(path, capacity, copy, NULL);
    if (!length || length >= capacity) {
        free(copy);
        free(query);
        return NULL;
    }
    n = length;
    for (size_t i = 0; i < n; i++) {
        if (copy[i] == L'/') {
            copy[i] = L'\\';
        }
    }
    if (n >= 3 && copy[1] == L':') {
        start = 3;
    } else if (n >= 2 && copy[0] == L'\\' && copy[1] == L'\\') {
        start = 2;
        for (unsigned component = 0; component < 2; component++) {
            while (start < n && copy[start] != L'\\') {
                start++;
            }
            if (start < n) {
                start++;
            }
        }
    }
    while (start < n) {
        size_t end = start;
        wchar_t saved;
        WIN32_FIND_DATAW entry;
        HANDLE search;
        while (end < n && copy[end] != L'\\') {
            end++;
        }
        memcpy(query, copy, start * sizeof(*query));
        query[start] = L'*';
        query[start + 1] = 0;
        search = FindFirstFileW(query, &entry);
        if (search == INVALID_HANDLE_VALUE) {
            break;
        }
        saved = copy[end];
        copy[end] = 0;
        do {
            if (wcslen(entry.cFileName) == end - start &&
                file_io_equal_wide_case(copy + start, entry.cFileName)) {
                memcpy(copy + start, entry.cFileName, (end - start) * sizeof(*copy));
                break;
            }
        } while (FindNextFileW(search, &entry));
        FindClose(search);
        copy[end] = saved;
        start = end + 1;
    }
    f = _wfopen(copy, mode);
    free(copy);
    free(query);
    return f;
}

/* Open the borrowed UTF-8 path and mode through the native file API, normalizing separators
 * and retrying read-mode asset lookup with ASCII case matching. Return a caller-owned FILE to
 * close, or NULL on conversion, allocation or open failure. Component lookup never renames
 * files.
 */
FILE *xeen_fopen(const char *path, const char *mode)
{
    wchar_t *p = xeen_wide(path);
    wchar_t *m = xeen_wide(mode);
    FILE *f = NULL;
    if (p && m) {
        f = _wfopen(p, m);
        if (!f && mode[0] == 'r') {
            f = file_io_read_wide_case(p, m);
        }
    }
    free(p);
    free(m);
    return f;
}
#else
#include <dirent.h>

/** Compares two names ignoring ASCII case; returns nonzero when they are equal. */
static int file_io_equal_ascii_case(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned x = (unsigned char)*a++;
        unsigned y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') {
            x += 32;
        }
        if (y >= 'A' && y <= 'Z') {
            y += 32;
        }
        if (x != y) {
            return 0;
        }
    }
    return *a == *b;
}

/* Open the borrowed UTF-8 path and mode through the native file API, normalizing separators
 * and retrying read-mode asset lookup with ASCII case matching. Return a caller-owned FILE to
 * close, or NULL on conversion, allocation or open failure. Component lookup never renames
 * files.
 */
FILE *xeen_fopen(const char *path, const char *mode)
{
    char *copy;
    size_t i;
    size_t start;
    size_t n;
    FILE *f;
    if (!path || !mode) {
        return NULL;
    }
    n = strlen(path);
    copy = malloc(n + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, path, n + 1);
    for (i = 0; i < n; i++) {
        if (copy[i] == '\\') {
            copy[i] = '/';
        }
    }
    f = fopen(copy, mode);
    if (!f && mode[0] == 'r') {
        /* Resolve each component independently; never change or create a file. */
        for (start = copy[0] == '/' ? 1 : 0; start < n;) {
            size_t end = start;
            char saved;
            DIR *dir;
            struct dirent *entry;
            while (end < n && copy[end] != '/') {
                end++;
            }
            saved = copy[start];
            copy[start] = 0;
            dir = opendir(start ? copy : ".");
            copy[start] = saved;
            if (!dir) {
                break;
            }
            saved = copy[end];
            copy[end] = 0;
            while ((entry = readdir(dir))) {
                if (strlen(entry->d_name) == end - start &&
                    file_io_equal_ascii_case(copy + start, entry->d_name)) {
                    memcpy(copy + start, entry->d_name, end - start);
                    break;
                }
            }
            closedir(dir);
            copy[end] = saved;
            start = end + 1;
        }
        f = fopen(copy, mode);
    }
    free(copy);
    return f;
}
#endif


#define APP_PATH_CAPACITY 32768

/* Allocate a UTF-8 path beside the running executable for a borrowed filename without path
 * separators. The caller frees the result. Returns NULL on invalid input, executable lookup,
 * conversion or allocation failure; never searches the working directory. */
char *xeen_application_path(const char *filename)
{
    size_t name_bytes;
    char *path;
    char *slash;
    size_t directory_bytes;
    if (!filename || strchr(filename, '/') || strchr(filename, '\\')) {
        return NULL;
    }
    name_bytes = strlen(filename) + 1;
    if (name_bytes > 4096) {
        return NULL;
    }
#ifdef _WIN32
    wchar_t *wide = malloc(APP_PATH_CAPACITY * sizeof(*wide));
    DWORD path_characters;
    int utf8_bytes;
    if (!wide) {
        return NULL;
    }
    path_characters = GetModuleFileNameW(NULL, wide, APP_PATH_CAPACITY);
    if (!path_characters || path_characters >= APP_PATH_CAPACITY) {
        free(wide);
        return NULL;
    }
    utf8_bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    path = utf8_bytes ? malloc((size_t)utf8_bytes + name_bytes) : NULL;
    if (path && !WideCharToMultiByte(CP_UTF8, 0, wide, -1, path, utf8_bytes, NULL, NULL)) {
        free(path);
        path = NULL;
    }
    free(wide);
    if (!path) {
        return NULL;
    }
#else
    ssize_t path_bytes;
    path = malloc(APP_PATH_CAPACITY + name_bytes);
    if (!path) {
        return NULL;
    }
    path_bytes = readlink("/proc/self/exe", path, APP_PATH_CAPACITY);
    if (path_bytes <= 0 || path_bytes >= APP_PATH_CAPACITY) {
        free(path);
        return NULL;
    }
    path[path_bytes] = 0;
#endif
    slash = strrchr(path, '/');
#ifdef _WIN32
    {
        char *back = strrchr(path, '\\');
        if (back && (!slash || back > slash)) {
            slash = back;
        }
    }
#endif
    if (!slash) {
        free(path);
        return NULL;
    }
    directory_bytes = (size_t)(slash - path) + 1;
    memcpy(path + directory_bytes, filename, name_bytes);
    return path;
}
/* Phase 308 (from the XEEN.EXE player's file_io.c, for the movie player in src/player/): read environment value
 * NAME as UTF-8 into BUFFER when the platform needs a conversion (Windows); *VALUE NULL when it is not set. Return
 * zero when it does not fit or cannot be converted. */
int xeen_environment(const char *name, char *buffer, size_t capacity, const char **value)
{
#ifdef _WIN32
    wchar_t *key = xeen_wide(name);
    const wchar_t *wide;
    int n;
    if (!key) {
        return 0;
    }
    wide = _wgetenv(key);
    free(key);
    *value = NULL;
    if (!wide) {
        return 1;
    }
    n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (n <= 0 || (size_t)n > capacity) {
        return 0;
    }
    if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer, n, NULL, NULL)) {
        return 0;
    }
    *value = buffer;
    return 1;
#else
    (void)buffer;
    (void)capacity;
    *value = getenv(name);
    return 1;
#endif
}
