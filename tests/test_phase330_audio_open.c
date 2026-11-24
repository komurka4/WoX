/* Phase 330: xeen_audio_engine_open's failure and cleanup paths under fault injection (host code, not original).
 *
 * Linked with -Wl,--wrap for calloc and the SDL calls the open and close make (GNU ld; the wrappers count each call
 * and fail the Nth one on request), and run with SDL's dummy audio driver (no device, no sound). Each case prints one
 * transcript line: the result, the diagnostic, the music and CD errors and the calls' balance after the close; the
 * test checks the balances (every SDL init quit, every device closed, every mutex destroyed, every worker joined)
 * and the failure results, and `--transcript` prints the lines only (a base-vs-new comparison).
 *   usage: xeen_phase330_audio_open_tests [--transcript] */
#include "xeen/audio_engine.h"
#include "xeen/wox_config.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, transcript_only;
#define CHECK(c, what)                                                                                                 \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "FAIL %s: %s\n", what, #c);                                                                \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* ---------------------------------------------------------------- the wrappers */

enum { F_CALLOC, F_INIT, F_OPEN, F_MUTEX, F_THREAD, F_COUNT };
static const char *const fault_names[F_COUNT] = {"calloc", "SDL_InitSubSystem", "SDL_OpenAudioDevice",
                                                 "SDL_CreateMutex", "SDL_CreateThread"};
static int armed = -1, armed_at, seen[F_COUNT];
static int inits, quits, opens, closes, mutexes, mutexes_destroyed, threads, joins;

static int fail_now(int kind)
{
    ++seen[kind];
    return armed == kind && seen[kind] == armed_at;
}
void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t a, size_t b)
{
    return fail_now(F_CALLOC) ? NULL : __real_calloc(a, b);
}
int __real_SDL_InitSubSystem(Uint32);
int __wrap_SDL_InitSubSystem(Uint32 f)
{
    int r;
    if (fail_now(F_INIT)) {
        SDL_SetError("injected InitSubSystem failure");
        return -1;
    }
    r = __real_SDL_InitSubSystem(f);
    inits += r == 0 && (f & SDL_INIT_AUDIO);
    return r;
}
void __real_SDL_QuitSubSystem(Uint32);
void __wrap_SDL_QuitSubSystem(Uint32 f)
{
    quits += (f & SDL_INIT_AUDIO) != 0;
    __real_SDL_QuitSubSystem(f);
}
SDL_AudioDeviceID __real_SDL_OpenAudioDevice(const char *, int, const SDL_AudioSpec *, SDL_AudioSpec *, int);
SDL_AudioDeviceID __wrap_SDL_OpenAudioDevice(const char *d, int c, const SDL_AudioSpec *w, SDL_AudioSpec *g, int a)
{
    SDL_AudioDeviceID r;
    if (fail_now(F_OPEN)) {
        SDL_SetError("injected OpenAudioDevice failure");
        return 0;
    }
    r = __real_SDL_OpenAudioDevice(d, c, w, g, a);
    opens += r != 0;
    return r;
}
void __real_SDL_CloseAudioDevice(SDL_AudioDeviceID);
void __wrap_SDL_CloseAudioDevice(SDL_AudioDeviceID d)
{
    ++closes;
    __real_SDL_CloseAudioDevice(d);
}
SDL_mutex *__real_SDL_CreateMutex(void);
SDL_mutex *__wrap_SDL_CreateMutex(void)
{
    SDL_mutex *r;
    if (fail_now(F_MUTEX)) {
        return NULL;
    }
    r = __real_SDL_CreateMutex();
    mutexes += r != NULL;
    return r;
}
void __real_SDL_DestroyMutex(SDL_mutex *);
void __wrap_SDL_DestroyMutex(SDL_mutex *m)
{
    mutexes_destroyed += m != NULL;
    __real_SDL_DestroyMutex(m);
}
SDL_Thread *__real_SDL_CreateThread(SDL_ThreadFunction, const char *, void *);
SDL_Thread *__wrap_SDL_CreateThread(SDL_ThreadFunction f, const char *n, void *u)
{
    SDL_Thread *r;
    if (fail_now(F_THREAD)) {
        return NULL;
    }
    r = __real_SDL_CreateThread(f, n, u);
    threads += r != NULL;
    return r;
}
void __real_SDL_WaitThread(SDL_Thread *, int *);
void __wrap_SDL_WaitThread(SDL_Thread *t, int *s)
{
    joins += t != NULL;
    __real_SDL_WaitThread(t, s);
}

/* ---------------------------------------------------------------- the cases */

static xeen_wox_config config;

/* One open (with the fault KIND armed at its Nth call, -1 none) and its close; prints the transcript line. */
static xeen_audio_engine *open_case(const char *label, xeen_audio_mode mode, const char *cd_dir, int kind, int nth,
                                    int keep)
{
    xeen_audio_engine_options o;
    xeen_audio_engine *e;
    char diag[1024];
    unsigned i;
    memset(&o, 0, sizeof o);
    o.config = &config;
    o.mode = mode;
    o.cd_dir = cd_dir;
    memset(seen, 0, sizeof seen);
    inits = quits = opens = closes = mutexes = mutexes_destroyed = threads = joins = 0;
    armed = kind;
    armed_at = nth;
    e = xeen_audio_engine_open(&o, diag, sizeof diag);
    armed = -1;
    for (i = 0; diag[i]; ++i) {
        if (diag[i] == '\n') {
            diag[i] = '|';
        }
    }
    printf("%-28s %s diag=[%s]", label, e ? "OPEN" : "NULL", diag);
    if (e) {
        printf(" music_ready=%d music_error=[%s] cd_error=[%s] sink=%d", xeen_audio_engine_music_ready(e),
               xeen_audio_engine_music_error(e) ? xeen_audio_engine_music_error(e) : "",
               xeen_audio_engine_cd_error(e) ? xeen_audio_engine_cd_error(e) : "", xeen_audio_engine_sink(e) != NULL);
        if (!keep) {
            xeen_audio_engine_close(e);
            e = NULL;
        }
    }
    printf(" init=%d quit=%d open=%d close=%d mutex=%d/%d thread=%d/%d audio_up=%d\n", inits, quits, opens, closes,
           mutexes, mutexes_destroyed, threads, joins, SDL_WasInit(SDL_INIT_AUDIO) != 0);
    if (!keep) {
        CHECK(inits == quits, label);
        CHECK(opens == closes, label);
        CHECK(mutexes == mutexes_destroyed, label);
        CHECK(threads == joins, label);
        CHECK(!SDL_WasInit(SDL_INIT_AUDIO), label);
    }
    return e;
}

int main(int argc, char **argv)
{
    char label[64], longdir[1100];
    int kind, n;
    xeen_audio_engine *e[3];
    transcript_only = argc > 1 && !strcmp(argv[1], "--transcript");
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    xeen_wox_config_defaults(&config, ".");
    config.system = XEEN_MUSIC_OFF;
    {
        char diag[64];
        CHECK(!xeen_audio_engine_open(NULL, diag, sizeof diag) && strstr(diag, "no configuration"), "no options");
        printf("%-28s NULL diag=[%s]\n", "no options", diag);
    }
    /* The fault-free paths. */
    (void)open_case("offline", XEEN_AUDIO_OFFLINE, NULL, -1, 0, 0);
    (void)open_case("offline cd missing", XEEN_AUDIO_OFFLINE, "/nonexistent/cd", -1, 0, 0);
    (void)open_case("offline cd is a file", XEEN_AUDIO_OFFLINE, "CMakeCache.txt", -1, 0, 0);
    memset(longdir, 'a', sizeof longdir - 1);
    longdir[sizeof longdir - 1] = 0;
    longdir[0] = '/';
    (void)open_case("offline cd too long", XEEN_AUDIO_OFFLINE, longdir, -1, 0, 0);
    (void)open_case("offline cd ok", XEEN_AUDIO_OFFLINE, ".", -1, 0, 0);
    (void)open_case("realtime", XEEN_AUDIO_REALTIME, ".", -1, 0, 0);
    config.system = XEEN_MUSIC_SBPRO; /* no INTRO.CC: the music error, the engine still opens */
    (void)open_case("offline music no driver", XEEN_AUDIO_OFFLINE, NULL, -1, 0, 0);
    (void)open_case("realtime music no driver", XEEN_AUDIO_REALTIME, NULL, -1, 0, 0);
    config.system = XEEN_MUSIC_OFF;
    /* Each fault at each of its first calls in the realtime open (the calls past the open's own are the worker's). */
    for (kind = 0; kind < F_COUNT; ++kind) {
        for (n = 1; n <= 2; ++n) {
            snprintf(label, sizeof label, "rt %s #%d", fault_names[kind], n);
            e[0] = open_case(label, XEEN_AUDIO_REALTIME, ".", kind, n, 0);
            (void)e[0];
        }
    }
    snprintf(label, sizeof label, "offline calloc #1");
    (void)open_case(label, XEEN_AUDIO_OFFLINE, NULL, F_CALLOC, 1, 0);
    /* Repeated opens and closes, and two engines open at once (SDL's audio subsystem is reference counted). */
    for (n = 0; n < 5; ++n) {
        snprintf(label, sizeof label, "repeat %d", n);
        (void)open_case(label, XEEN_AUDIO_REALTIME, ".", -1, 0, 0);
    }
    /* A second engine while one is open: the dummy driver has one device, so its open fails after its own
     * SDL_InitSubSystem, which it quits again (SDL keeps the first engine's reference). */
    e[0] = open_case("two engines: first", XEEN_AUDIO_REALTIME, NULL, -1, 0, 1);
    e[1] = open_case("two engines: second", XEEN_AUDIO_REALTIME, NULL, -1, 0, 1);
    CHECK(e[0] && !e[1] && inits == quits && SDL_WasInit(SDL_INIT_AUDIO), "a second device refused, cleaned up");
    xeen_audio_engine_close(e[0]);
    CHECK(!SDL_WasInit(SDL_INIT_AUDIO), "the first closed: audio down");
    xeen_audio_engine_close(NULL);
    printf("two engines closed audio_up=%d\n", SDL_WasInit(SDL_INIT_AUDIO) != 0);
    if (!transcript_only) {
        fprintf(stderr, failures ? "FAIL (%d)\n" : "PASS\n", failures);
    }
    SDL_Quit();
    return failures ? 1 : 0;
}
