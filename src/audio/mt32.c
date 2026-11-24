/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Load the optional Munt service, validate supplied ROMs and expose MT-32 synthesis through
 * XeenSynth. mt32_open creates state for synth and synth_stream; message delivery, rendering
 * and teardown share one serialized owner.
 */
#include "file_io.h"
#include "synth_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef XEEN_HAVE_MT32
#include <mt32emu/c_interface/c_types.h>
#ifdef _WIN32
#include <windows.h>
typedef HMODULE LibraryHandle;

/* Open the borrowed native library name and return its platform handle, or a null handle on
 * loader failure. The caller owns a successful handle until mt32_close_library.
 */
static LibraryHandle mt32_open_library(const char *name)
{
    return LoadLibraryA(name);
}

/* Release the owned platform library handle after all service contexts and calls have ended. A
 * null handle is ignored; loader-close status is not returned.
 */
static void mt32_close_library(LibraryHandle library)
{
    if (library) {
        FreeLibrary(library);
    }
}
#else
#include <dlfcn.h>
typedef void *LibraryHandle;

/* Open the borrowed native library name and return its platform handle, or a null handle on
 * loader failure. The caller owns a successful handle until mt32_close_library.
 */
static LibraryHandle mt32_open_library(const char *name)
{
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
}

/* Release the owned platform library handle after all service contexts and calls have ended. A
 * null handle is ignored; loader-close status is not returned.
 */
static void mt32_close_library(LibraryHandle library)
{
    if (library) {
        dlclose(library);
    }
}
#endif

typedef struct MT32State {
    mt32emu_context context;
    LibraryHandle library;
    const mt32emu_service_i_v4 *api;
} MT32State;

typedef mt32emu_service_i(MT32EMU_C_CALL *GetService)(void);

/* Load Munt and validate its versioned C service into caller-owned state. An explicit
 * environment override controls lookup even when it fails. Return one on success, zero with a
 * diagnostic on lookup or ABI failure; outer cleanup owns any opened handle.
 */
static int mt32_load_service(MT32State *state)
{
    const char *override = getenv("XEEN_MT32_LIBRARY");
    unsigned i;
    static const char *names[] = {
#ifdef _WIN32
            "xeen_mt32.dll", /* the package's Munt (src/audio/MT32.cmake) */
#elif defined(__APPLE__)
            "libmt32emu.2.dylib", "libmt32emu.dylib",
#else
            "libmt32emu.so.2", "libmt32emu.so",
#endif
            NULL};
    GetService get_service = NULL;
    mt32emu_service_i service;
    /* An explicit override is authoritative, including failure. This also makes
     * missing-library fallback testable without changing the installed library.
     */
    if (override && *override) {
        state->library = mt32_open_library(override);
    } else {
        for (i = 0; names[i] && !state->library; i++) {
            state->library = mt32_open_library(names[i]);
        }
#ifdef XEEN_MT32_LIBRARY_PATH
        /* CMake may find Munt in a local prefix outside the system loader paths.
         * Keep portable installed names first, then use that configured library.
         * The explicit environment override remains authoritative.
         */
        if (!state->library) {
            state->library = mt32_open_library(XEEN_MT32_LIBRARY_PATH);
        }
#endif
    }
    if (!state->library) {
        fprintf(stderr, "MT-32: library unavailable%s%s; music unavailable\n",
                override && *override ? ": " : "", override && *override ? override : "");
        return 0;
    }
    {
#ifdef _WIN32
        FARPROC symbol = GetProcAddress(state->library, "mt32emu_get_service_i");
#else
        void *symbol = dlsym(state->library, "mt32emu_get_service_i");
#endif
        /* Both supported loader ABIs provide callable addresses. memcpy avoids a
         * non-ISO object/function-pointer cast in this otherwise C11 source.
         */
        _Static_assert(sizeof(symbol) == sizeof(get_service), "loader function-pointer size");
        memcpy(&get_service, &symbol, sizeof(get_service));
    }
    if (!get_service) {
        fputs("MT-32: library has no C service interface; music unavailable\n", stderr);
        return 0;
    }
    service = get_service();
    if (!service.v0 || !service.v0->getVersionID ||
        service.v0->getVersionID(service) < MT32EMU_SERVICE_VERSION_4) {
        fputs("MT-32: library C service interface v4 required; music unavailable\n", stderr);
        return 0;
    }
    state->api = service.v4;
    return 1;
}

/* SHA-256 is used solely for the two approved user-supplied ROM files. */
typedef struct {
    uint32_t h[8];
    uint64_t size;
    size_t used;
    uint8_t block[64];
} Sha256;

/** Rotates the 32-bit X right by N bits (SHA-256's ROTR). */
static uint32_t mt32_sha256_rotate_right(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

/* Compress one complete borrowed ROM digest block into caller-owned hash state. Requires a
 * full block and initialized state, allocates nothing and has no failure path.
 */
static void mt32_sha256_process_block(Sha256 *state, const uint8_t *p)
{
    static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};
    uint32_t w[64];
    uint32_t a = state->h[0];
    uint32_t b = state->h[1];
    uint32_t c = state->h[2];
    uint32_t d = state->h[3];
    uint32_t e = state->h[4];
    uint32_t f = state->h[5];
    uint32_t g = state->h[6];
    uint32_t h = state->h[7];
    unsigned i;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | p[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t x = w[i - 15];
        uint32_t y = w[i - 2];
        w[i] = w[i - 16] +
               (mt32_sha256_rotate_right(x, 7) ^ mt32_sha256_rotate_right(x, 18) ^ (x >> 3)) +
               w[i - 7] +
               (mt32_sha256_rotate_right(y, 17) ^ mt32_sha256_rotate_right(y, 19) ^ (y >> 10));
    }
    for (i = 0; i < 64; i++) {
        uint32_t t1 = h +
                      (mt32_sha256_rotate_right(e, 6) ^ mt32_sha256_rotate_right(e, 11) ^
                       mt32_sha256_rotate_right(e, 25)) +
                      ((e & f) ^ ((~e) & g)) + k[i] + w[i];
        uint32_t t2 = (mt32_sha256_rotate_right(a, 2) ^ mt32_sha256_rotate_right(a, 13) ^
                       mt32_sha256_rotate_right(a, 22)) +
                      ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state->h[0] += a;
    state->h[1] += b;
    state->h[2] += c;
    state->h[3] += d;
    state->h[4] += e;
    state->h[5] += f;
    state->h[6] += g;
    state->h[7] += h;
}

/** Initializes a SHA-256 state (used to identify the MT-32 ROM images). */
static void mt32_sha256_init(Sha256 *state)
{
    static const uint32_t initial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memset(state, 0, sizeof(*state));
    memcpy(state->h, initial, sizeof(initial));
}

/** Adds the N bytes at P to a SHA-256 state. */
static void mt32_sha256_update(Sha256 *state, const uint8_t *p, size_t n)
{
    state->size += n;
    while (n) {
        size_t k = 64 - state->used;
        if (k > n) {
            k = n;
        }
        memcpy(state->block + state->used, p, k);
        state->used += k;
        p += k;
        n -= k;
        if (state->used == 64) {
            mt32_sha256_process_block(state, state->block);
            state->used = 0;
        }
    }
}

/** Completes a SHA-256 state and writes its digest as 64 hexadecimal characters to DIGEST. */
static void mt32_sha256_finish(Sha256 *state, char digest[65])
{
    uint64_t bits = state->size * 8;
    uint8_t pad[64] = {0x80};
    uint8_t length[8];
    unsigned i;
    size_t n = state->used < 56 ? 56 - state->used : 120 - state->used;
    for (i = 0; i < 8; i++) {
        length[7 - i] = (uint8_t)(bits >> (i * 8));
    }
    mt32_sha256_update(state, pad, n);
    mt32_sha256_update(state, length, 8);
    for (i = 0; i < 8; i++) {
        snprintf(digest + i * 8, 9, "%08x", (unsigned)state->h[i]);
    }
}

/* Read the borrowed ROM path, compute its SHA-256 and compare against the required digest
 * string. Own and close only the temporary read stream. Return one after reporting a match,
 * zero with a diagnostic on read or identity failure; file bytes remain untouched.
 */
static int mt32_hash_rom(const char *path, const char *expected)
{
    FILE *f = xeen_fopen(path, "rb");
    uint8_t b[4096];
    size_t n;
    Sha256 state;
    char digest[65];
    int failed;
    if (!f) {
        fprintf(stderr, "MT-32: cannot read ROM %s; music unavailable\n", path);
        return 0;
    }
    mt32_sha256_init(&state);
    while ((n = fread(b, 1, sizeof(b), f)) != 0) {
        mt32_sha256_update(&state, b, n);
    }
    failed = ferror(f);
    fclose(f);
    if (failed) {
        fprintf(stderr, "MT-32: ROM read failed: %s; music unavailable\n", path);
        return 0;
    }
    mt32_sha256_finish(&state, digest);
    if (strcmp(digest, expected)) {
        fprintf(stderr, "MT-32: ROM SHA-256 mismatch: %s (%s); music unavailable\n", path, digest);
        return 0;
    }
    fprintf(stderr, "MT-32 ROM SHA-256 %s %s\n", path, digest);
    return 1;
}

/* Create an owned Munt context, validate and load both ROMs, and configure immediate MIDI
 * delivery with native-rate stereo output. Borrow the directory string. Return state for
 * mt32_state_close or NULL after releasing partial setup on failure.
 */
static MT32State *mt32_state_open(const char *directory)
{
    static const char *names[] = {"MT32_CONTROL.ROM", "MT32_PCM.ROM"};
    static const char *hashes[] = {
            "a73a06c23ed38370e58a11fb1b86f7ea4c547061a60d7aa62bae446235fd2dff",
            "d9164063f293410cf33f2f64cdcea6893b44723fe41938e07ec9aef58b406238"};
    MT32State *state;
    mt32emu_report_handler_i handler = {0};
    unsigned i;
    mt32emu_rom_info info;
    if (!directory) {
        directory = "roms/mt32";
    }
    state = calloc(1, sizeof(*state));
    if (!state) {
        return NULL;
    }
    if (!mt32_load_service(state)) {
        goto fail;
    }
    state->context = state->api->createContext(handler, NULL);
    if (!state->context) {
        goto fail;
    }
    for (i = 0; i < 2; i++) {
        size_t n = strlen(directory) + strlen(names[i]) + 2;
        char *path = malloc(n);
        int valid;
        if (!path) {
            goto fail;
        }
        snprintf(path, n, "%s/%s", directory, names[i]);
        valid = mt32_hash_rom(path, hashes[i]);
        if (valid) {
            memset(&info, 0, sizeof(info));
            valid = state->api->identifyROMFile(&info, path, NULL) == MT32EMU_RC_OK;
            if (valid) {
                fprintf(stderr, "MT-32: %s\n",
                        i == 0 ? info.control_rom_description : info.pcm_rom_description);
                valid = state->api->addROMFile(state->context, path) > 0;
            }
        }
        free(path);
        if (!valid) {
            goto fail;
        }
    }
    /* Coarse analogue emulation keeps 32000 Hz output. Schedule messages
     * between render spans so a second serial-MIDI queue adds no latency.
     */
    state->api->setAnalogOutputMode(state->context, MT32EMU_AOM_COARSE);
    state->api->setMIDIDelayMode(state->context, MT32EMU_MDM_IMMEDIATE);
    if (state->api->openSynth(state->context) != MT32EMU_RC_OK) {
        goto fail;
    }
    fprintf(stderr, "MT-32: libmt32emu %s, stereo 32000 Hz\n",
            state->api->getLibraryVersionString());
    return state;
fail:
    if (state->context) {
        state->api->freeContext(state->context);
    }
    mt32_close_library(state->library);
    free(state);
    return NULL;
}

/* Deliver a validated channel message or complete SysEx directly to the borrowed Munt context.
 * Ignore invalid pointers, unsupported statuses and malformed lengths. Message storage is
 * borrowed only during the call; access must be serialized.
 */
static void mt32_state_message(void *context, const uint8_t *bytes, size_t n)
{
    MT32State *state = context;
    uint32_t packed;
    size_t wanted;
    size_t i;
    if (!state || !bytes || !n) {
        return;
    }
    if (bytes[0] == 0xf0) {
        if (n >= 2 && n <= UINT32_MAX && bytes[n - 1] == 0xf7) {
            state->api->playSysexNow(state->context, bytes, (mt32emu_bit32u)n);
        }
        return;
    }
    if (bytes[0] < 0x80 || bytes[0] >= 0xf0) {
        return;
    }
    wanted = (bytes[0] >> 4) == 0xc || (bytes[0] >> 4) == 0xd ? 2 : 3;
    if (n != wanted) {
        return;
    }
    packed = bytes[0];
    for (i = 1; i < n; i++) {
        if (bytes[i] & 0x80) {
            return;
        }
        packed |= (uint32_t)bytes[i] << (8 * i);
    }
    state->api->playMsgNow(state->context, packed);
}

/* Render requested stereo S16 frames into caller-owned output, splitting requests to fit the
 * service frame-count width. A null context produces silence and null output is ignored. Calls
 * must share the context owner with MIDI delivery.
 */
static void mt32_state_render(void *context, int16_t *stereo, size_t frames)
{
    MT32State *state = context;
    if (!stereo) {
        return;
    }
    if (!state) {
        memset(stereo, 0, frames * 2 * sizeof(*stereo));
        return;
    }
    while (frames) {
        mt32emu_bit32u n = frames > UINT32_MAX ? UINT32_MAX : (mt32emu_bit32u)frames;
        state->api->renderBit16s(state->context, stereo, n);
        frames -= n;
        stereo += (size_t)n * 2;
    }
}

/* Send final note and sound stops, close the owned Munt context, unload its service and free
 * backend state. Null is ignored; the worker must already be joined. No status is returned.
 */
static void mt32_state_close(void *context)
{
    MT32State *state = context;
    unsigned ch;
    if (!state) {
        return;
    }
    for (ch = 0; ch < 16; ch++) {
        state->api->playMsgNow(state->context, 0xb0u | ch | (123u << 8));
        state->api->playMsgNow(state->context, 0xb0u | ch | (120u << 8));
    }
    state->api->closeSynth(state->context);
    state->api->freeContext(state->context);
    mt32_close_library(state->library);
    free(state);
}

/* Open the optional Munt backend with the approved user-supplied ROM pair; a null directory
 * uses the default ROM folder. Return an owned common synth for synth_close or mt32_close, or
 * NULL on missing support, library, ROM or allocation failure. Serialize all later operations
 * and stop streaming before close.
 */
XeenSynth *mt32_open(const char *directory)
{
    XeenSynth *state = calloc(1, sizeof(*state));
    if (!state) {
        return NULL;
    }
    state->context = mt32_state_open(directory);
    if (!state->context) {
        free(state);
        return NULL;
    }
    state->message = mt32_state_message;
    state->render = mt32_state_render;
    state->close = mt32_state_close;
    return state;
}
#else
/* Open the optional Munt backend with the approved user-supplied ROM pair; a null directory
 * uses the default ROM folder. Return an owned common synth for synth_close or mt32_close, or
 * NULL on missing support, library, ROM or allocation failure. Serialize all later operations
 * and stop streaming before close.
 */
XeenSynth *mt32_open(const char *directory)
{
    (void)directory;
    fprintf(stderr, "MT-32: support was not built; music unavailable\n");
    return NULL;
}
#endif
