/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Interpret game songs and effect cues as timed MIDI messages. music_open and the sequencing
 * entry points borrow an emission callback used by runtime or music_output; backend-specific
 * synthesis stays outside this decoder.
 */
#ifndef XEEN_MUSIC_H
#define XEEN_MUSIC_H
#include "audio_archive.h"
typedef struct Music Music;
typedef void (*MusicEmit)(void *, uint64_t, const uint8_t *, size_t);
/* Create a Roland event sequencer from the borrowed overlay archive and emit its
 * initialization messages at the supplied PIT-cycle origin. The callback must consume or copy
 * borrowed message bytes immediately. Return a caller-owned Music for music_close, or NULL for
 * allocation, overlay or origin failure; retain neither archive nor callback-context
 * ownership.
 */
Music *music_open(CC *overlay_archive, MusicEmit emit, void *context, uint64_t origin);
/* Create the canmus (Sound Canvas, GS) sequencer from the borrowed overlay archive's INTRO.CC build (4443 bytes;
 * XEEN.CFG's music index 5; XEEN.DAT loads its drivers from INTRO.CC) and emit the driver's initial volumes at the PIT-cycle origin. Return a caller-owned
 * Music for music_close, or NULL when canmus is missing, of another build, or allocation fails. */
Music *music_open_sc55(CC *overlay_archive, MusicEmit emit, void *context, uint64_t origin);
/* Load the optional SB Pro sequencer, emitting four-byte register/mixer events. It owns copied
 * promus data and its library reference until music_close. No ROM directory is needed. */
Music *music_open_sbpro(CC *overlay_archive, MusicEmit emit, void *context, uint64_t origin);
/* Phase 286: open INTRO.CC's music driver DRIVER of the borrowed archive: rolmus (MT-32), wavemus (Wave Blaster),
 * canmus (Sound Canvas), tapmus (TAP-10), promus (SB Pro), blastmus (Sound Blaster), specmus (Pro-Audio Spectrum,
 * byte-identical to blastmus), admus (AdLib). CONFIG_8D4C is XEEN.CFG's word 8D4C (the drivers' init writes the SB
 * Pro / SB16 mixer only when it is nonzero; the genuine file has 1, the other open functions assume it). NULL when
 * the driver is missing, of an unknown build, or the SB Pro library (the FM drivers) is unavailable. */
Music *music_open_driver(CC *overlay_archive, const char *driver, unsigned config_8d4c, MusicEmit emit, void *context,
                         uint64_t origin);
/* Phase 286: transport events (four bytes, never MIDI): F6, 0, byte, 0 an MPU-401 command-port byte (the UART
 * drivers' reset FFh and UART command 3Fh); F5, 0, register, value an SB mixer write (8D3A + 4/5; sbpro.h's
 * SBPRO_EVENT_MIXER). The FM drivers' four-byte OPL/mixer/mode events are sbpro.h's. */
#define MUSIC_EVENT_MPU_COMMAND 0xf6u
#define MUSIC_EVENT_MIXER       0xf5u
/* 1 when the driver writes MIDI bytes to a UART (wavemus, tapmus): its messages are the bytes as written (possibly not
 * valid MIDI), for a receiver to parse; 0 for the intelligent-mode MPU drivers, whose messages the MPU delivers. */
int music_uart(const Music *);
/* Advance the borrowed sequencer to PIT-cycle time and emit every due event in order. Invalid,
 * backwards or excessively large advances latch music_error and stop decoding. Null or
 * already-failed sequencers are ignored; no storage is returned.
 */
void music_advance(Music *, uint64_t pit);
/* Advance to the supplied PIT time and select an effect cue in the borrowed sequencer.
 * Unsupported cue numbers and protected overlapping cues are ignored. Message bytes are
 * borrowed during callback delivery; latched decoder errors prevent further selection.
 */
void music_command(Music *, unsigned command, uint64_t pit);
/* Copy and start a song in the borrowed sequencer at the supplied PIT time, replacing its
 * owned prior song storage. A null or empty song stops playback. Invalid data or allocation
 * failure latches music_error; the caller always retains its input Blob.
 */
void music_load(Music *, const Blob *song, uint64_t pit);
/* music_load with the driver's entry-6 mode word: 1 suppresses the release of the channel's remembered note before a
 * song note on (XEEN.DAT always passes 0: music_load). */
void music_load_mode(Music *, const Blob *song, unsigned mode, uint64_t pit);
/* Advance the borrowed sequencer, then stop with zero, restart with one or set a fade rate
 * from two through 255. Other control values are ignored. Null or failed sequencers do
 * nothing; errors remain available through music_error.
 */
void music_control(Music *, unsigned control, uint64_t pit);
/* Advance the borrowed sequencer and emit the song-volume controller in its defined channel
 * order. Values above the MIDI range latch music_error; null or failed sequencers are ignored.
 * The operation returns no memory or status.
 */
void music_volume(Music *, unsigned volume, uint64_t pit);
/* Phase 287: a voice of the digital drivers ibmsnd, adsnd or source from FROM to TO (PIT cycles; TO <= FROM ends it):
 * the FM drivers (admus, blastmus, specmus, promus) leave channel 7 alone meanwhile, as their word 0Fh = 2 makes them;
 * the MIDI drivers never read that word (no change). */
void music_voice(Music *, uint64_t from, uint64_t to);
/* Return the borrowed latched error text, empty while healthy; a null sequencer returns static
 * unavailable text. Do not free the result or retain a sequencer-owned pointer after
 * music_close.
 */
const char *music_error(const Music *);
/* Advance and stop the owned sequencer, emit its shutdown messages and release copied overlay,
 * song and sequencer storage. A null pointer is ignored. Callback context remains caller-owned
 * and must stay valid through this call.
 */
void music_close(Music *, uint64_t pit);
/* Phase 308: the fade word (entry 6 FFE0h): nonzero while a fade runs; 0 for NULL or a failed sequencer. */
unsigned music_fading(const Music *);
#endif
