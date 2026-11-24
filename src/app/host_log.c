/*
 * Phase 272: wox.log, the stream capture and the fatal sequence of a window session (host_log.h). The capture
 * follows the XEEN.EXE player's app_log ("Name player wox and add timestamped logging with Windows error
 * dialogs"): one pipe and one reader thread per standard stream, each line logged and forwarded to the stream's
 * original destination (none in a Windows GUI process without a redirection). The host's own messages are logged
 * directly, with their severity, and never pass through the pipe (no duplicate entry).
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "host_log.h"
#include "xeen_wox_version.h" /* XEEN_WOX_VERSION: generated from versions.cmake (Phase 338) */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601 /* the condition variable (Vista and later) */
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#define log_write _write
#define log_read  _read
#define log_close _close
#define log_dup2  _dup2
#define log_fileno _fileno
typedef unsigned log_size;
#else
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#define log_write write
#define log_read  read
#define log_close close
#define log_dup2  dup2
#define log_fileno fileno
typedef size_t log_size;
#endif

/* A session's first entry (Phase 338: with the game's version). */
#define SESSION_START "session start: WoX (" XEEN_WOX_VERSION ")"
/* The sync marker a reader recognizes as a complete line (never logged nor forwarded). */
#define SYNC_MARK     "\x1ewox-log-sync"
#define SYNC_MARK_LEN (sizeof SYNC_MARK - 1u)

typedef struct {
    FILE *stream;
    const char *name; /* "stdout", "stderr" */
    int reader;       /* the pipe's read end (-1: none) */
    int original;     /* the stream's former destination (-1: none) */
    int redirected;
    unsigned long synced; /* the last sync token the reader saw */
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
    int has_thread;
#endif
} capture;

static FILE *log_file;
static int log_failed;
static char log_where[1100];
static char errors[1024];
/* The two captured streams, by slot. */
enum { CAPTURE_STDOUT, CAPTURE_STDERR };
static capture streams[2];
static unsigned long sync_token;
static int fatal_shown;
#if defined(_WIN32)
static CRITICAL_SECTION log_lock;
static CONDITION_VARIABLE log_changed;
#define LOCK()   EnterCriticalSection(&log_lock)
#define UNLOCK() LeaveCriticalSection(&log_lock)
#else
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t log_changed = PTHREAD_COND_INITIALIZER;
#define LOCK()   pthread_mutex_lock(&log_lock)
#define UNLOCK() pthread_mutex_unlock(&log_lock)
#endif
static int lock_ready;

static const char *level_name(host_level level)
{
    return level == HOST_ERROR ? "ERROR" : level == HOST_WARN ? "WARN" : "INFO";
}
/* The local time as "YYYY-MM-DD HH:MM:SS.mmm". */
static void timestamp(char *out, size_t n)
{
#if defined(_WIN32)
    SYSTEMTIME t;
    GetLocalTime(&t);
    snprintf(out, n, "%04u-%02u-%02u %02u:%02u:%02u.%03u", (unsigned)t.wYear, (unsigned)t.wMonth,
             (unsigned)t.wDay, (unsigned)t.wHour, (unsigned)t.wMinute, (unsigned)t.wSecond,
             (unsigned)t.wMilliseconds);
#else
    struct timespec ts;
    struct tm tm;
    char day[32];
    if (clock_gettime(CLOCK_REALTIME, &ts) || !localtime_r(&ts.tv_sec, &tm) ||
        !strftime(day, sizeof day, "%Y-%m-%d %H:%M:%S", &tm)) {
        snprintf(out, n, "time-unavailable");
        return;
    }
    snprintf(out, n, "%.24s.%03u", day, (unsigned)(ts.tv_nsec / 1000000L) % 1000u);
#endif
}
/* One entry, under the lock when the capture runs; a failed write latches the log off. */
static void record_locked(host_level level, const char *text, size_t length)
{
    char stamp[40];
    if (!log_file || log_failed) {
        return;
    }
    timestamp(stamp, sizeof stamp);
    if (fprintf(log_file, "%s %-5s %.*s\n", stamp, level_name(level), (int)length, text) < 0 ||
        fflush(log_file)) {
        log_failed = 1;
    }
}
static void record(host_level level, const char *text, size_t length)
{
    if (lock_ready) {
        LOCK();
    }
    record_locked(level, text, length);
    if (level == HOST_ERROR) {
        size_t used = strlen(errors);
        if (used + length + 2u < sizeof errors) {
            if (used) {
                errors[used++] = '\n';
            }
            memcpy(errors + used, text, length);
            errors[used + length] = 0;
        }
    }
    if (lock_ready) {
        UNLOCK();
    }
}
/* A captured stderr line's severity: WARN when it reports a failure or a missing part (a keyword in its first 255
 * bytes, case folded), else INFO; a stdout line is INFO. */
static host_level captured_level(const capture *c, const char *line, size_t n)
{
    static const char *const words[] = {"unavailable", "cannot", "failed", "missing", "mismatch",
                                        "stopped",     "overflow", "error",  "incompatible", "not built"};
    char lower[256];
    size_t i, k = n < sizeof lower - 1u ? n : sizeof lower - 1u;
    if (c != &streams[CAPTURE_STDERR]) {
        return HOST_INFO;
    }
    for (i = 0; i < k; ++i) {
        lower[i] = (char)(line[i] >= 'A' && line[i] <= 'Z' ? line[i] - 'A' + 'a' : line[i]);
    }
    lower[k] = 0;
    for (i = 0; i < sizeof words / sizeof *words; ++i) {
        if (strstr(lower, words[i])) {
            return HOST_WARN;
        }
    }
    return HOST_INFO;
}
static void write_all(int fd, const char *b, size_t n)
{
    while (fd >= 0 && n) {
        int k = (int)log_write(fd, b, (log_size)n);
        if (k < 0 && errno == EINTR) {
            continue;
        }
        if (k <= 0) {
            return;
        }
        b += k;
        n -= (size_t)k;
    }
}
/* A captured line's longest piece (a longer line is split, reader_main), and the log record's room for the stream's
 * "[name] " head before it. */
#define CAPTURED_LINE_BYTES 4096u
#define CAPTURED_HEAD_BYTES 104u
/* One complete captured line (without its newline): a sync marker wakes host_log_sync; any other line is logged
 * and forwarded. */
static void captured_line(capture *c, const char *line, size_t n, int newline)
{
    if (n >= SYNC_MARK_LEN && !memcmp(line, SYNC_MARK, SYNC_MARK_LEN)) {
        unsigned long token = strtoul(line + SYNC_MARK_LEN, NULL, 10);
        LOCK();
        if (token > c->synced) {
            c->synced = token;
        }
#if defined(_WIN32)
        WakeAllConditionVariable(&log_changed);
#else
        pthread_cond_broadcast(&log_changed);
#endif
        UNLOCK();
        return;
    }
    write_all(c->original, line, n);
    if (newline) {
        write_all(c->original, "\n", 1);
    }
    {
        char text[CAPTURED_LINE_BYTES + CAPTURED_HEAD_BYTES];
        size_t k = n && line[n - 1] == '\r' ? n - 1u : n;
        int head = snprintf(text, sizeof text, "[%s] ", c->name);
        if (head > 0 && (size_t)head + k < sizeof text) {
            memcpy(text + head, line, k);
            LOCK();
            record_locked(captured_level(c, line, k), text, (size_t)head + k);
            UNLOCK();
        }
    }
}
/* The capture thread of C: reads the pipe in chunks until its end (or a read error), hands each line without its
 * newline to captured_line, a piece of CAPTURED_LINE_BYTES of an overlong line (and a last unterminated line) as a
 * line without a newline, then closes the reader (c->reader -1). */
#if defined(_WIN32)
static DWORD WINAPI reader_main(LPVOID u)
#else
static void *reader_main(void *u)
#endif
{
    capture *c = u;
    char input[512], line[CAPTURED_LINE_BYTES];
    size_t used = 0;
    for (;;) {
        int n = (int)log_read(c->reader, input, (log_size)sizeof input);
        int i;
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        for (i = 0; i < n; ++i) {
            if (input[i] == '\n') {
                captured_line(c, line, used, 1);
                used = 0;
            } else {
                line[used++] = input[i];
                if (used == sizeof line) {
                    captured_line(c, line, used, 0);
                    used = 0;
                }
            }
        }
    }
    if (used) {
        captured_line(c, line, used, 0);
    }
    log_close(c->reader);
    c->reader = -1;
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}
/* Redirects C's stream into a new pipe read by its own thread; 0 when a step fails (the stream may then be
 * partly redirected: host_log_close restores what was done). */
static int capture_start(capture *c)
{
    int fds[2];
#if defined(_WIN32)
    DWORD id = c->stream == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE;
    HANDLE original = GetStdHandle(id);
    DWORD type = original && original != INVALID_HANDLE_VALUE ? GetFileType(original) : FILE_TYPE_UNKNOWN;
    /* A redirection (a file, a pipe) or a console a console process inherited stays a destination; a GUI process
     * started from Explorer has none. */
    if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK || type == FILE_TYPE_CHAR) {
        HANDLE copy;
        if (DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &copy, 0, FALSE,
                            DUPLICATE_SAME_ACCESS)) {
            c->original = _open_osfhandle((intptr_t)copy, _O_BINARY | _O_WRONLY);
            if (c->original < 0) {
                CloseHandle(copy);
            }
        }
    }
    fflush(c->stream);
    if (_pipe(fds, 4096, _O_BINARY | _O_NOINHERIT)) {
        return 0;
    }
    /* The stream gets a valid descriptor first (a GUI process's has none), then the pipe's write end. */
    if (!freopen("NUL", "wb", c->stream)) {
        log_close(fds[0]);
        log_close(fds[1]);
        return 0;
    }
#else
    fflush(c->stream);
    c->original = dup(log_fileno(c->stream));
    if (c->original < 0 || pipe(fds)) {
        return 0;
    }
    (void)fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    (void)fcntl(c->original, F_SETFD, FD_CLOEXEC);
#endif
    c->reader = fds[0];
    if (log_dup2(fds[1], log_fileno(c->stream)) < 0) {
        log_close(fds[1]);
        return 0;
    }
    log_close(fds[1]);
    c->redirected = 1;
#if defined(_WIN32)
    (void)SetStdHandle(id, (HANDLE)_get_osfhandle(_fileno(c->stream)));
    setvbuf(c->stream, NULL, _IONBF, 0);
    c->thread = CreateThread(NULL, 0, reader_main, c, 0, NULL);
    return c->thread != NULL;
#else
    setvbuf(c->stream, NULL, c->stream == stdout ? _IOLBF : _IONBF, 0);
    c->has_thread = pthread_create(&c->thread, NULL, reader_main, c) == 0;
    return c->has_thread;
#endif
}
/* Restores C's stream (its original destination, else the null device), which closes the pipe's write end, and
 * joins its reader. */
static void capture_stop(capture *c)
{
    if (c->redirected) {
        fflush(c->stream);
        if (c->original >= 0) {
            (void)log_dup2(c->original, log_fileno(c->stream));
        } else {
#if defined(_WIN32)
            if (!freopen("NUL", "wb", c->stream)) {
                /* nothing left to restore it to */
            }
#else
            if (!freopen("/dev/null", "wb", c->stream)) {
                /* nothing left to restore it to */
            }
#endif
        }
#if defined(_WIN32)
        (void)SetStdHandle(c->stream == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE,
                           (HANDLE)_get_osfhandle(_fileno(c->stream)));
#endif
        c->redirected = 0;
    }
#if defined(_WIN32)
    if (c->thread) {
        WaitForSingleObject(c->thread, INFINITE);
        CloseHandle(c->thread);
        c->thread = NULL;
    }
#else
    if (c->has_thread) {
        pthread_join(c->thread, NULL);
        c->has_thread = 0;
    }
#endif
    if (c->reader >= 0) {
        log_close(c->reader);
        c->reader = -1;
    }
    if (c->original >= 0) {
        log_close(c->original);
        c->original = -1;
    }
}
static int make_dir(const char *path)
{
#if defined(_WIN32)
    return _mkdir(path) == 0 || errno == EEXIST;
#else
    return mkdir(path, 0777) == 0 || errno == EEXIST;
#endif
}
/* The per-user fallback directory (created when missing); 0 when there is none. */
static int fallback_dir(char *out, size_t n)
{
#if defined(_WIN32)
    const char *base = getenv("LOCALAPPDATA");
    if (!base || !*base || snprintf(out, n, "%s\\WoX", base) >= (int)n) {
        return 0;
    }
    return make_dir(out);
#else
    const char *state = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
    char base[1024];
    if (state && *state == '/') {
        if (snprintf(base, sizeof base, "%s", state) >= (int)sizeof base) {
            return 0;
        }
    } else if (home && *home) {
        char local[1024];
        if (snprintf(local, sizeof local, "%s/.local", home) >= (int)sizeof local ||
            snprintf(base, sizeof base, "%s/state", local) >= (int)sizeof base) {
            return 0;
        }
        (void)make_dir(local);
    } else {
        return 0;
    }
    (void)make_dir(base);
    if (snprintf(out, n, "%s/wox", base) >= (int)n) {
        return 0;
    }
    return make_dir(out);
#endif
}
static FILE *open_in(const char *dir)
{
    FILE *f;
    if (snprintf(log_where, sizeof log_where, "%s/wox.log", dir) >= (int)sizeof log_where) {
        log_where[0] = 0;
        return NULL;
    }
#if defined(_WIN32)
    {
        char *s;
        for (s = log_where; *s; ++s) {
            if (*s == '/') {
                *s = '\\';
            }
        }
    }
#endif
    f = fopen(log_where, "a");
    if (!f) {
        log_where[0] = 0;
    }
    return f;
}
int host_log_open(const char *exe_path, int argc, char **argv)
{
    char dir[1024], line[1200];
    const char *primary_fail = NULL;
    int i;
    size_t used;
    if (log_file) {
        return 1;
    }
    log_failed = 0;
    errors[0] = 0;
    streams[CAPTURE_STDOUT] = (capture){.stream = stdout, .name = "stdout", .reader = -1, .original = -1};
    streams[CAPTURE_STDERR] = (capture){.stream = stderr, .name = "stderr", .reader = -1, .original = -1};
    if (exe_path && *exe_path) {
        const char *slash = strrchr(exe_path, '/');
#if defined(_WIN32)
        const char *back = strrchr(exe_path, '\\');
        if (!slash || (back && back > slash)) {
            slash = back;
        }
#endif
        if (slash && (size_t)(slash - exe_path) < sizeof dir) {
            memcpy(dir, exe_path, (size_t)(slash - exe_path));
            dir[slash - exe_path] = 0;
            if (!dir[0]) {
                dir[0] = '/', dir[1] = 0;
            }
            log_file = open_in(dir);
            if (!log_file) {
                primary_fail = "not writable";
            }
        }
    }
    if (!log_file && fallback_dir(dir, sizeof dir)) {
        log_file = open_in(dir);
    }
    if (!log_file) {
        return 0;
    }
#if defined(_WIN32)
    InitializeCriticalSection(&log_lock);
    InitializeConditionVariable(&log_changed);
#endif
    lock_ready = 1;
    record(HOST_INFO, SESSION_START, strlen(SESSION_START));
    snprintf(line, sizeof line, "log file: %s%s", log_where,
             primary_fail ? " (the executable's directory is not writable)" : "");
    record(HOST_INFO, line, strlen(line));
    used = (size_t)snprintf(line, sizeof line, "command line:");
    for (i = 0; i < argc && used < sizeof line; ++i) {
        used += (size_t)snprintf(line + used, sizeof line - used, " %s", argv[i]);
    }
    record(HOST_INFO, line, strlen(line));
    if (!capture_start(&streams[CAPTURE_STDOUT]) || !capture_start(&streams[CAPTURE_STDERR])) {
        const char *why = "stream capture unavailable: library messages are not logged";
        capture_stop(&streams[CAPTURE_STDOUT]);
        capture_stop(&streams[CAPTURE_STDERR]);
        record(HOST_WARN, why, strlen(why));
    }
    return 1;
}
const char *host_log_path(void)
{
    return log_file ? log_where : "";
}
static void vreport(host_level level, int echo, const char *fmt, va_list ap)
{
    char text[2048];
    int n = vsnprintf(text, sizeof text, fmt, ap);
    size_t k = n < 0 ? 0u : (size_t)n < sizeof text ? (size_t)n : sizeof text - 1u;
    if (n < 0) {
        text[0] = 0;
    }
    if (echo) {
        if (streams[CAPTURE_STDERR].redirected) {
            /* The original stream directly (the pipe would log it a second time). */
            write_all(streams[CAPTURE_STDERR].original, text, k);
            write_all(streams[CAPTURE_STDERR].original, "\n", 1);
        } else {
            fputs(text, stderr);
            fputc('\n', stderr);
        }
    }
    record(level, text, k);
}
void host_report(host_level level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vreport(level, 1, fmt, ap);
    va_end(ap);
}
void host_note(host_level level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vreport(level, 0, fmt, ap);
    va_end(ap);
}
void host_log_sync(void)
{
    unsigned i;
    fflush(stdout);
    fflush(stderr);
    if (!lock_ready) {
        return;
    }
    for (i = 0; i < 2u; ++i) {
        capture *c = &streams[i];
        char mark[64];
        unsigned long token;
        int n;
        if (!c->redirected) {
            continue;
        }
        LOCK();
        token = ++sync_token;
        UNLOCK();
        n = snprintf(mark, sizeof mark, "%s%lu\n", SYNC_MARK, token);
        fflush(c->stream);
        write_all(log_fileno(c->stream), mark, (size_t)n);
        LOCK();
#if defined(_WIN32)
        {
            DWORD start = GetTickCount();
            while (c->synced < token && GetTickCount() - start < 1000u) {
                (void)SleepConditionVariableCS(&log_changed, &log_lock, 100);
            }
        }
#else
        {
            struct timespec until;
            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_sec += 1;
            while (c->synced < token) {
                if (pthread_cond_timedwait(&log_changed, &log_lock, &until) == ETIMEDOUT) {
                    break;
                }
            }
        }
#endif
        UNLOCK();
    }
}
void host_log_close(int status)
{
    char line[64];
    if (!log_file) {
        return;
    }
    capture_stop(&streams[CAPTURE_STDOUT]);
    capture_stop(&streams[CAPTURE_STDERR]);
    snprintf(line, sizeof line, "session end: exit status %d", status);
    record(HOST_INFO, line, strlen(line));
    lock_ready = 0;
#if defined(_WIN32)
    DeleteCriticalSection(&log_lock);
#endif
    fclose(log_file);
    log_file = NULL;
    log_where[0] = 0;
}
int host_fatal_present(const host_fatal_hooks *hooks, int status)
{
    char text[1600];
    const char *where;
    size_t used;
    int shown = 0;
    if (fatal_shown) {
        return 0;
    }
    fatal_shown = 1;
    /* Everything already written to the streams is logged first. */
    host_log_sync();
    /* The summary of the errors reported so far, then the fatal entry itself. */
    if (errors[0]) {
        used = (size_t)snprintf(text, sizeof text, "WoX stopped because of an error.\n\n%s\n\n", errors);
    } else {
        used = (size_t)snprintf(text, sizeof text, "WoX stopped because of an error (exit status %d).\n\n",
                                status);
    }
    host_note(HOST_ERROR, "fatal error: exit status %d", status);
    where = host_log_path();
    if (used < sizeof text) {
        snprintf(text + used, sizeof text - used, *where ? "Details are in %s" : "%s",
                 *where ? where : "wox.log could not be written.");
    }
    if (hooks && hooks->pause_audio) {
        hooks->pause_audio(hooks->user);
    }
    if (hooks && hooks->dialog) {
        shown = hooks->dialog(hooks->user, "WoX - Error", text);
    }
    if (shown) {
        host_note(HOST_INFO, "the error dialog was dismissed; shutting down");
    }
    return 1;
}
