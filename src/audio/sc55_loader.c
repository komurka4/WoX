/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Resolve the optional SC-55 shared library and validate its C ABI. synth_open_sc55 constructs
 * the common synth wrapper; synth owns teardown after the streaming worker has stopped.
 */
#include "synth_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef XEEN_HAVE_SC55
#include "sc55.h"
#include <SDL.h>

typedef struct {
    void *library;
    XeenSC55 *engine;
    unsigned (*version)(void);
    XeenSC55 *(*open)(const char *);
    XeenSC55 *(*open_progress)(const char *, XeenSC55Progress, void *); /* optional; NULL without */
    void (*message)(XeenSC55 *, const uint8_t *, size_t);
    void (*render)(XeenSC55 *, int16_t *, size_t);
    void (*close)(XeenSC55 *);
} SC55Library;

/* Load the optional SC-55 library from an authoritative environment override, the executable
 * directory or the platform search path. Return an owned SDL loader handle, or NULL on
 * failure; the caller unloads a successful handle.
 */
static void *sc55_open_library(void)
{
    const char *override = getenv("XEEN_SC55_LIBRARY");
    void *library = NULL;
#ifdef _WIN32
    const char *name = "xeen_sc55.dll";
#else
    const char *name = "libxeen_sc55.so.1";
#endif
    if (override && *override) {
        return SDL_LoadObject(override);
    }
    /* Look beside the executable first, independent of working directory. */
    {
        char *base = SDL_GetBasePath();
        if (base) {
            size_t n = strlen(base) + strlen(name) + 1;
            char *path = malloc(n);
            if (path) {
                snprintf(path, n, "%s%s", base, name);
                library = SDL_LoadObject(path);
                free(path);
            }
            SDL_free(base);
        }
    }
    if (!library) {
        library = SDL_LoadObject(name);
    }
    return library;
}

/* Resolve and validate every required function of the borrowed SC-55 library state. Return one
 * for the supported ABI, zero after a missing-symbol or version diagnostic. Outer setup
 * retains ownership of the loader handle on failure.
 */
static int sc55_load_api(SC55Library *library)
{
    void *symbol;
#define LOAD(member, name)                                                                         \
    do {                                                                                           \
        symbol = SDL_LoadFunction(library->library, name);                                         \
        _Static_assert(sizeof(symbol) == sizeof(library->member), "loader function-pointer size"); \
        if (!symbol) {                                                                             \
            fprintf(stderr, "SC-55: library missing %s; no SC-55 music (no other driver stands in)\n", name);     \
            return 0;                                                                              \
        }                                                                                          \
        memcpy(&library->member, &symbol, sizeof(symbol));                                         \
    } while (0)
    LOAD(version, "sc55_abi_version");
    if (library->version() != XEEN_SC55_ABI_VERSION) {
        fputs("SC-55: incompatible library ABI; no SC-55 music (no other driver stands in)\n", stderr);
        return 0;
    }
    LOAD(open, "sc55_open");
    LOAD(message, "sc55_message");
    LOAD(render, "sc55_render");
    LOAD(close, "sc55_close");
#undef LOAD
    /* Optional (a library built before the start-up bar lacks it): the open then reports no progress. */
    symbol = SDL_LoadFunction(library->library, "sc55_open_progress");
    _Static_assert(sizeof(symbol) == sizeof(library->open_progress), "loader function-pointer size");
    if (symbol) {
        memcpy(&library->open_progress, &symbol, sizeof(symbol));
    }
    return 1;
}

/* Forward borrowed MIDI bytes to the library-owned SC-55 engine through its resolved C ABI.
 * The wrapper retains no message storage; caller serialization and the engine's diagnostics
 * apply.
 */
static void sc55_send_message(void *context, const uint8_t *bytes, size_t length_bytes)
{
    SC55Library *library = context;
    library->message(library->engine, bytes, length_bytes);
}

/* Render the requested frame count through the loaded C ABI into borrowed stereo S16 storage.
 * Engine ownership stays in the library, and calls must be serialized with message delivery
 * and close.
 */
static void sc55_render_frames(void *context, int16_t *samples, size_t frames)
{
    SC55Library *library = context;
    library->render(library->engine, samples, frames);
}

/* Close the library-owned engine before unloading its function pointers and freeing the owned
 * loader state. The streaming worker must already be stopped. No status is returned.
 */
static void sc55_close_library(void *context)
{
    SC55Library *library = context;
    if (library->engine) {
        library->close(library->engine);
    }
    SDL_UnloadObject(library->library);
    free(library);
}
#endif
/* Load the optional SC-55 C ABI and open its engine with the borrowed ROM directory. Return
 * owned common synth state for synth_close, or NULL on unavailable support, library, ABI, ROM
 * or allocation failure. The streaming worker must stop before teardown unloads the library.
 */
XeenSynth *synth_open_sc55(const char *directory)
{
    return synth_open_sc55_progress(directory, NULL, NULL);
}
XeenSynth *synth_open_sc55_progress(const char *directory, void (*progress)(void *user, unsigned permille),
                                    void *user)
{
#ifdef XEEN_HAVE_SC55
    XeenSynth *synth = calloc(1, sizeof(*synth));
    SC55Library *state = calloc(1, sizeof(*state));
    if (!synth || !state) {
        free(synth);
        free(state);
        return NULL;
    }
    state->library = sc55_open_library();
    if (!state->library) {
        fprintf(stderr, "SC-55: library unavailable: %s; no SC-55 music (no other driver stands in)\n",
                SDL_GetError());
        goto fail;
    }
    if (!sc55_load_api(state)) {
        goto fail;
    }
    if (progress && state->open_progress) {
        progress(user, 0);
        state->engine = state->open_progress(directory, progress, user);
    } else {
        state->engine = state->open(directory);
    }
    if (!state->engine) {
        goto fail;
    }
    synth->context = state;
    synth->message = sc55_send_message;
    synth->render = sc55_render_frames;
    synth->close = sc55_close_library;
    return synth;
fail:
    if (state->library) {
        SDL_UnloadObject(state->library);
    }
    free(state);
    free(synth);
    return NULL;
#else
    (void)directory;
    (void)progress;
    (void)user;
    fputs("SC-55: support was not built (enable XEEN_SC55)\n", stderr);
    return NULL;
#endif
}
