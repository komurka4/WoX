/* SPDX-License-Identifier: GPL-3.0-or-later
 */
/* Load the SB Pro shared backend only on explicit selection. All FM code and C++ dependencies
 * remain behind its versioned C ABI; other music modes never call this loader. */
#include "sbpro_loader.h"
#include "synth_internal.h"
#include "file_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef XEEN_HAVE_SBPRO
#include <SDL.h>
#endif
#ifdef XEEN_HAVE_SBPRO
/* Whether API is the table this executable was built against: its version, exactly sizeof(SBProAPI) bytes and every
 * member present (music.c calls sequence_open_flags and voice, synth_open_sbpro the renderer's, unconditionally). */
static int sbpro_api_complete(const SBProAPI *api)
{
    return api && api->version == XEEN_SBPRO_ABI && api->size == sizeof(SBProAPI) && api->sequence_open &&
           api->advance && api->song && api->command && api->control && api->volume && api->fading && api->error &&
           api->sequence_close && api->renderer_open && api->message && api->render && api->renderer_close &&
           api->sequence_open_flags && api->voice;
}
#endif
/* (sbpro_loader.h) XEEN_SBPRO_LIBRARY, else the library beside the executable, else the bare name on the loader's
 * search path; a table that is not complete for this ABI (sbpro_api_complete) is refused as a load failure is. */
SBProLibrary *sbpro_library_open(void)
{
#ifdef XEEN_HAVE_SBPRO
    const char *override = getenv("XEEN_SBPRO_LIBRARY");
    SBProLibrary *library = calloc(1, sizeof(*library));
    const SBProAPI *(*get_api)(unsigned);
    void *symbol;
    if (!library) {
        return NULL;
    }
#ifdef _WIN32
    const char *name = "xeen_sbpro.dll";
#else
    const char *name = "libxeen_sbpro.so.1";
#endif
    char *path = override && *override ? NULL : xeen_application_path(name);
    library->handle = SDL_LoadObject(override && *override ? override : path ? path : name);
    free(path);
    if (!library->handle) {
        fprintf(stderr, "SB Pro: cannot load optional backend: %s\n", SDL_GetError());
        free(library);
        return NULL;
    }
    symbol = SDL_LoadFunction(library->handle, "sbpro_get_api");
    _Static_assert(sizeof(symbol) == sizeof(get_api), "loader function-pointer size");
    if (!symbol) {
        fputs("SB Pro: backend ABI entry missing\n", stderr);
        sbpro_library_close(library);
        return NULL;
    }
    memcpy(&get_api, &symbol, sizeof(symbol));
    library->api = get_api(XEEN_SBPRO_ABI);
    if (!sbpro_api_complete(library->api)) {
        fputs("SB Pro: incompatible or incomplete backend ABI\n", stderr);
        sbpro_library_close(library);
        return NULL;
    }
    return library;
#else
    fputs("SB Pro: backend support was not built\n", stderr);
    return NULL;
#endif
}

void sbpro_library_close(SBProLibrary *library)
{
    if (!library) {
        return;
    }
#ifdef XEEN_HAVE_SBPRO
    if (library->handle) {
        SDL_UnloadObject(library->handle);
    }
#endif
    free(library);
}

typedef struct {
    SBProLibrary *library;
    SBProRenderer *renderer;
} SBProSynth;

/** Synthesizer interface: passes a register-write message to the loaded SB Pro renderer. */
static void sbpro_synth_message(void *context, const uint8_t *bytes, size_t count)
{
    SBProSynth *state = context;
    state->library->api->message(state->renderer, bytes, count);
}

/** Synthesizer interface: renders FRAMES stereo frames from the loaded SB Pro renderer. */
static void sbpro_synth_render(void *context, int16_t *samples, size_t frames)
{
    SBProSynth *state = context;
    state->library->api->render(state->renderer, samples, frames);
}

/** Synthesizer interface: closes the SB Pro renderer and unloads its library. */
static void sbpro_synth_close(void *context)
{
    SBProSynth *state = context;
    state->library->api->renderer_close(state->renderer);
    sbpro_library_close(state->library);
    free(state);
}

XeenSynth *synth_open_sbpro(void)
{
    SBProLibrary *library = sbpro_library_open();
    if (!library) {
        return NULL;
    }
    SBProRenderer *renderer = library->api->renderer_open(32000);
    SBProSynth *state = renderer ? calloc(1, sizeof(*state)) : NULL;
    XeenSynth *synth = state ? calloc(1, sizeof(*synth)) : NULL;
    if (!synth) {
        if (renderer) {
            library->api->renderer_close(renderer);
        }
        sbpro_library_close(library);
        free(state);
        return NULL;
    }
    state->library = library;
    state->renderer = renderer;
    synth->context = state;
    synth->message = sbpro_synth_message;
    synth->render = sbpro_synth_render;
    synth->close = sbpro_synth_close;
    return synth;
}
