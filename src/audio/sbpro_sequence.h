/* SPDX-License-Identifier: GPL-3.0-or-later
 */
/* Private plugin entry points. Only sbpro_get_api is exported from the shared library. */
#ifndef XEEN_SBPRO_SEQUENCE_H
#define XEEN_SBPRO_SEQUENCE_H
#include "sbpro.h"
#ifdef __cplusplus
extern "C" {
#endif
SBProSequence *sbpro_sequence_open(const uint8_t *, size_t, SBProEmit, void *, uint64_t);
SBProSequence *sbpro_sequence_open_flags(const uint8_t *, size_t, unsigned, SBProEmit, void *, uint64_t);
void sbpro_advance(SBProSequence *, uint64_t);
void sbpro_song(SBProSequence *, const uint8_t *, size_t, uint64_t);
void sbpro_command(SBProSequence *, unsigned, uint64_t);
void sbpro_control(SBProSequence *, unsigned, uint64_t);
void sbpro_volume(SBProSequence *, unsigned, uint64_t);
void sbpro_voice(SBProSequence *, uint64_t, uint64_t);
unsigned sbpro_fading(const SBProSequence *);
const char *sbpro_error(const SBProSequence *);
void sbpro_sequence_close(SBProSequence *, uint64_t);
#ifdef __cplusplus
}
#endif
#endif
