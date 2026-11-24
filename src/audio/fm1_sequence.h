/* SPDX-License-Identifier: GPL-3.0-or-later
 */
/* Phase 286: the one-chip FM music drivers (INTRO.CC blastmus = specmus, admus); fm1_sequence.c. Private to the
 * SB Pro library (sbpro_sequence.c dispatches to it). Events are sbpro.h's four-byte OPL, mixer and mode events. */
#ifndef XEEN_FM1_SEQUENCE_H
#define XEEN_FM1_SEQUENCE_H
#include "sbpro.h"
#define FM1_BLASTMUS_BYTES 6202u /* blastmus and specmus (byte-identical in INTRO.CC) */
#define FM1_ADMUS_BYTES    6300u
#define FM1_EVENT_OPL      SBPRO_EVENT_OPL
#define FM1_EVENT_MIXER    SBPRO_EVENT_MIXER
#define FM1_EVENT_MODE     SBPRO_EVENT_MODE
#define FM1_MODE_MONO      SBPRO_MODE_MONO
#define FM1_FLAG_MIXER     SBPRO_FLAG_CONFIG_8D4C
typedef struct Fm1Sequence Fm1Sequence;
typedef SBProEmit Fm1Emit;
Fm1Sequence *fm1_open(const uint8_t *, size_t, unsigned flags, Fm1Emit, void *, uint64_t);
void fm1_advance(Fm1Sequence *, uint64_t);
void fm1_song(Fm1Sequence *, const uint8_t *, size_t, uint64_t);
void fm1_command(Fm1Sequence *, unsigned, uint64_t);
void fm1_control(Fm1Sequence *, unsigned, uint64_t);
void fm1_volume(Fm1Sequence *, unsigned, uint64_t);
/* Phase 287: a digital voice's window on the music word 0Fh (FROM, TO in PIT cycles; TO <= FROM ends it). */
void fm1_voice(Fm1Sequence *, uint64_t from, uint64_t to);
unsigned fm1_fading(const Fm1Sequence *);
const char *fm1_error(const Fm1Sequence *);
void fm1_close(Fm1Sequence *, uint64_t);
#endif
