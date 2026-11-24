/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Palette, image-file and simple tick helpers, and player_voc_decode (the game's VOC decoder plus the encoded copy
 * the audio sink plays). Phase 308: the former VOC decoder here is the game's src/audio/voc.c.
 */
#include "audio/file_io.h"
#include "xeen.h"
#include "telemetry.h"
#include <stdlib.h>
#include <string.h>

#define MEDIA_BIOS_TICK_PIT_CYCLES 65536

/* Return the BIOS tick containing a PIT-cycle timestamp, using the fixed BIOS reload divisor.
 * This arithmetic helper allocates nothing and has no failure result.
 */
uint64_t tick_at(uint64_t cycles)
{
    return cycles / MEDIA_BIOS_TICK_PIT_CYCLES;
}

/* Return the BIOS tick boundary ticks after the tick containing reset. Inputs and result use
 * PIT cycles; zero ticks can precede reset within its current tick. Arithmetic is unsigned and
 * has no failure result.
 */
uint64_t tick_deadline(uint64_t reset, unsigned ticks)
{
    return (tick_at(reset) + ticks) * MEDIA_BIOS_TICK_PIT_CYCLES;
}

/* Convert the borrowed 768-byte DAC palette to caller-owned RGB bytes at the supplied
 * brightness, where 128 means full level. Inputs must have palette-sized storage; no
 * validation or failure status is provided.
 */
void palette_rgb(const uint8_t *palette, unsigned level, uint8_t *rgb)
{
    size_t component;
    for (component = 0; component < XEEN_PALETTE_BYTES; component++) {
        unsigned scaled = (palette[component] * level) / XEEN_PALETTE_FULL_LEVEL;
        rgb[component] = (uint8_t)((scaled << 2) | (scaled >> 4));
    }
}

/* Write one indexed cinematic frame and its borrowed RGB palette as a P6 image at path. Own
 * and close the opened output stream. Return one on success, zero on open or write failure; a
 * partial output file can remain.
 */
int ppm_write(const char *path, const uint8_t *pixels, const uint8_t *rgb)
{
    FILE *f = xeen_fopen(path, "wb");
    size_t i;
    int ok;
    if (!f) {
        return 0;
    }
    if (fprintf(f, "P6\n320 200\n255\n") < 0) {
        fclose(f);
        return 0;
    }
    for (i = 0; i < XEEN_PIXELS; i++) {
        if (fwrite(rgb + pixels[i] * 3, 1, 3, f) != 3) {
            fclose(f);
            return 0;
        }
    }
    ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

/* Phase 308: decode a borrowed VOC with the game's decoder (src/audio/voc.c, the same code as this player's former
 * media.c decoder) and keep an owned copy of its encoded bytes for the audio sink (cine_voice). Return one on
 * success; zero on malformed data or allocation failure, VOICE cleared. voice_free releases both.
 */
int player_voc_decode(const Blob *encoded, Voice *voice)
{
    if (!voc_decode(encoded, voice)) {
        return 0;
    }
    voice->encoded = malloc(encoded->size ? encoded->size : 1u);
    if (!voice->encoded) {
        voice_free(voice);
        return 0;
    }
    memcpy(voice->encoded, encoded->data, encoded->size);
    voice->encoded_size = encoded->size;
    return 1;
}
