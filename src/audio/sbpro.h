/* SPDX-License-Identifier: GPL-3.0-or-later
 */
/* Optional SB Pro 1 backend ABI. The library owns the FM sequencer and two YM3812 engines.
 * No game data or ROM is embedded; all input bytes are borrowed for the duration of a call.
 * Sequence calls and renderer calls each require serialization, with independent objects. */
#ifndef XEEN_SBPRO_H
#define XEEN_SBPRO_H
#include <stddef.h>
#include <stdint.h>
#define XEEN_SBPRO_ABI 1u
#if defined(_WIN32) && defined(XEEN_SBPRO_BUILD)
#define SBPRO_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#define SBPRO_EXPORT __attribute__((visibility("default")))
#else
#define SBPRO_EXPORT
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SBProSequence SBProSequence;
typedef struct SBProRenderer SBProRenderer;
/* Consume/copy four-byte events immediately: F4, chip(0=left/1=right), register, value;
 * F5,0,26,value is the source FM mixer setting. Time is in PIT cycles, never capture-fitted. */
typedef void (*SBProEmit)(void *, uint64_t, const uint8_t *, size_t);
#define SBPRO_EVENT_OPL       0xf4u /* F4, chip, register, value: one OPL register write */
#define SBPRO_EVENT_MIXER     0xf5u /* F5, 0, register, value: one SB Pro mixer write */
#define SBPRO_MIXER_FM_VOLUME 0x26u /* the mixer's FM volume register (left in bits 5..7, right in 1..3) */
/* Phase 286: F3, 0, mode, 0 sets the renderer's output: SBPRO_MODE_MONO plays chip 0 on both outputs (the one-chip
 * AdLib / Sound Blaster / Pro-Audio Spectrum drivers); the default plays chip 0 left and chip 1 right. */
#define SBPRO_EVENT_MODE      0xf3u
#define SBPRO_MODE_MONO       1u
/* Phase 286: sequence_open_flags's FLAGS bit 0: XEEN.CFG's word 8D4C is nonzero (the drivers' init writes the SB Pro
 * mixer only then; the genuine XEEN.CFG has 1, and sequence_open assumes it). */
#define SBPRO_FLAG_CONFIG_8D4C 1u

typedef struct {
    unsigned version;
    size_t size;
    SBProSequence *(*sequence_open)(const uint8_t *, size_t, SBProEmit, void *, uint64_t);
    void (*advance)(SBProSequence *, uint64_t);
    void (*song)(SBProSequence *, const uint8_t *, size_t, uint64_t);
    void (*command)(SBProSequence *, unsigned, uint64_t);
    void (*control)(SBProSequence *, unsigned, uint64_t);
    void (*volume)(SBProSequence *, unsigned, uint64_t);
    unsigned (*fading)(const SBProSequence *);
    const char *(*error)(const SBProSequence *);
    void (*sequence_close)(SBProSequence *, uint64_t);
    SBProRenderer *(*renderer_open)(unsigned);
    void (*message)(SBProRenderer *, const uint8_t *, size_t);
    void (*render)(SBProRenderer *, int16_t *, size_t);
    void (*renderer_close)(SBProRenderer *);
    /* Phase 286: sequence_open with FLAGS (SBPRO_FLAG_*), and for the one-chip FM drivers (INTRO.CC admus,
     * blastmus/specmus) as well as promus. Required, as every member (the loader refuses a table without it). */
    SBProSequence *(*sequence_open_flags)(const uint8_t *, size_t, unsigned, SBProEmit, void *, uint64_t);
    /* Phase 287: a digital voice of ibmsnd, adsnd or source from FROM to TO (PIT cycles): the music driver's word 0Fh
     * is 2 meanwhile (those drivers store it at the voice's start and clear it at its end or stop) and the effect paths
     * leave channel 7 alone. TO <= FROM ends the window (a stop). An executable expects this table's exact size. */
    void (*voice)(SBProSequence *, uint64_t from, uint64_t to);
} SBProAPI;

/* Return the immutable ABI table, or NULL for an unsupported ABI. No allocation or I/O. */
SBPRO_EXPORT const SBProAPI *sbpro_get_api(unsigned version);
#ifdef __cplusplus
}
#endif
#endif
