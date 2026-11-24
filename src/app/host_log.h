/*
 * Phase 272: the window session's timestamped log (wox.log) and its fatal-error sequence.
 *
 * A window session (no --headless) opens wox.log beside the executable (append mode; when that directory is not
 * writable: %LOCALAPPDATA%\WoX\wox.log on Windows, $XDG_STATE_HOME/wox/wox.log or
 * ~/.local/state/wox/wox.log elsewhere). Every entry is one line "YYYY-MM-DD HH:MM:SS.mmm LEVEL text" (local time,
 * LEVEL INFO, WARN or ERROR padded to five characters). The host's own messages (host_report) keep their exact
 * stderr text and add one entry with their severity; everything else written to stdout or stderr while the log
 * is open (the audio libraries, the SC-55 and SB Pro modules, --diagnostic-state's records) is captured through a
 * pipe per stream and logged as "[stdout] LINE" / "[stderr] LINE", still forwarded to the original stream when one
 * exists (a terminal, a redirection). A headless run opens no log: its streams are untouched.
 *
 * Logging never fails the game: an unwritable log latches off (no recursive report), and the fatal sequence still
 * shows its message (the dialog, stderr).
 */
#ifndef XEEN_APP_HOST_LOG_H
#define XEEN_APP_HOST_LOG_H
#include <stddef.h>

typedef enum { HOST_INFO, HOST_WARN, HOST_ERROR } host_level;

/* Opens the session log for EXE_PATH's directory (the running executable; NULL: none) or the per-user
 * fallback, starts the stream capture and writes the session's first entries (the log path, the command line
 * ARGC/ARGV). Returns 1 when a log file is open, 0 otherwise (the session continues without one). */
int host_log_open(const char *exe_path, int argc, char **argv);
/* The open log's path ("" when none, also after host_log_close). */
const char *host_log_path(void);
/* One host message: FMT's text plus a newline on stderr, exactly as before (the original stream while the
 * capture runs), and one entry at LEVEL. An ERROR also joins the session's error summary. */
void host_report(host_level level, const char *fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;
/* An entry (and, for ERROR, the summary) without any stream output: context for the log and the dialog that the
 * existing stderr text does not carry. */
void host_note(host_level level, const char *fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;
/* Flushes stdout and stderr and waits (at most about one second per stream) until the capture has logged every
 * line written before the call. */
void host_log_sync(void);
/* Ends the capture (the original streams restored, the readers joined), writes "session end: exit status
 * STATUS" and closes the log. Safe without an open log, and twice. */
void host_log_close(int status);

/* The fatal sequence's services: pause the audio, then show the modal error and return 1 after its OK (0: no dialog
 * on this platform). Both may be NULL. */
typedef struct {
    void (*pause_audio)(void *user);
    int (*dialog)(void *user, const char *title, const char *text);
    void *user;
} host_fatal_hooks;
/* Once per process: logs "fatal error: exit status STATUS", composes the dialog's text (the error summary, else
 * the status, and where the details are), syncs the log, pauses the audio and shows the dialog, which returns
 * after its OK. Nothing else (no frame, no cleanup) happens in between. Returns 1 the first time, 0 after. */
int host_fatal_present(const host_fatal_hooks *hooks, int status);
#endif
