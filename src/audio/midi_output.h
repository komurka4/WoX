/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Send complete MIDI messages to a Windows or Linux system destination. midi_output_open, send
 * and close own the platform device; music_output serializes these calls without changing
 * instruments or event bytes.
 */
#ifndef XEEN_MIDI_OUTPUT_H
#define XEEN_MIDI_OUTPUT_H
#include <stddef.h>
#include <stdint.h>
/* Single owner, complete messages (including F0/F7). No instrument mapping. */
typedef struct MidiOutput MidiOutput;
/* Open an owned system MIDI destination, using device on Windows or port on Linux. Borrow both
 * selection strings and return state for midi_output_close. Return NULL with a diagnostic when
 * the device, optional service or native setup is unavailable.
 */
MidiOutput *midi_output_open(const char *device, const char *port);
/* Send one complete borrowed MIDI message without instrument remapping. Windows copies
 * asynchronous SysEx bytes before returning. Return one when accepted, zero for invalid length
 * or native delivery failure; earlier messages are not rolled back.
 */
int midi_output_send(MidiOutput *, const uint8_t *, size_t);
/* Close owned system MIDI state; null is ignored. A Windows driver that cannot release
 * asynchronous buffers retains the bounded allocation to avoid dangling pointers. The caller
 * must stop message delivery before closing; no status is returned.
 */
void midi_output_close(MidiOutput *);
#endif
