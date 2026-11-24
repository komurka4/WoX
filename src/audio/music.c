/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Interpret game songs and effect cues as timed MIDI messages. music_open and the sequencing
 * entry points borrow an emission callback used by runtime or music_output; backend-specific
 * synthesis stays outside this decoder.
 */
#include "music.h"
#include "xeen/audio.h"
#include "sbpro_loader.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Decode the songs and effect cues stored with the Roland music resources.
 * Music output modules consume complete MIDI messages through MusicEmit.
 */
struct Music {
    SBProLibrary *fm_library;
    SBProSequence *fm_sequence;
    Blob overlay;
    Blob song;
    MusicEmit emit;
    void *context;
    /* The driver family: 1 for the GS family (canmus, tapmus: `canmus` is the profile), 0 for the MT-32 family (rolmus,
     * wavemus: `roland`). The families' decoders share the stream format and differ event by event (music_decode_*). */
    unsigned gs;
    const struct MusicRolandProfile *roland;
    const struct MusicCanmusProfile *canmus;
    unsigned uart; /* Phase 286: wavemus, tapmus: the driver writes MIDI bytes to the MPU data port in UART mode */
    uint64_t next;
    uint64_t now;
    unsigned song_pos;
    unsigned effect_pos;
    unsigned effect_start;
    unsigned song_wait;
    unsigned effect_wait;
    unsigned depth;
    unsigned returns[16];
    unsigned song_active;
    unsigned effect_active;
    unsigned fade;
    unsigned fade_acc;
    unsigned fade_left;
    unsigned bend_ticks;
    unsigned suppress_note_off;
    uint16_t bend_delta[16];
    uint16_t bend_value[16];
    uint8_t song_program[16];
    uint8_t effect_program[16];
    uint8_t note[16];
    uint8_t expression[16];
    char error[128];
};

#define MUSIC_TICK UINT64_C(16390)
/* A single call cannot ask for unbounded work after a corrupt clock jump.
 * One million ticks exceeds three hours of uninterrupted cinematic time.
 */
#define MUSIC_ADVANCE_LIMIT        UINT64_C(1000000)
#define MUSIC_BATCH_EVENT_LIMIT    100000
#define MUSIC_CHANNEL_COUNT        16
#define MUSIC_RETURN_STACK_DEPTH   16
#define MUSIC_ROLAND_OVERLAY_BYTES 7600

/* XEEN.DAT the Roland driver build differs per archive. INTRO.CC's rolmus is the
 * reference's (7600 bytes); XEEN.CC's (7354 bytes, the one XEEN.DAT loads) has the same decoder,
 * effect entry, control entry and helpers modulo relocation, a cue table at 100C, a note table
 * at 164, the same 13 patch blocks at moved offsets, no channel-8 fade pre-step and no 400-tick
 * sweep limit (xeen_dat_evidence/phase132/PREFLIGHT.md section 3). */
struct MusicRolandProfile {
    size_t size;
    const char *signature;
    unsigned note_table;
    unsigned cue_table;
    unsigned patches[13];
    int fade_rhythm_step;
    int sweep_limit;
    /* Phase 286, the MT-32-family profile of wavemus (Wave Blaster, XEEN.CFG music 8; INTRO.CC 7202 bytes,
     * xeen_dat_evidence/phase286/a/wavemus.asm), the same decoder as rolmus's but for these fields. */
    const char *member;
    unsigned patch_count;    /* the init's patch exclusive messages (rolmus 13, wavemus none) */
    unsigned volume_byte;    /* 0: the init's literal 79 (rolmus); else the offset of the driver's initial volume
                              * byte (wavemus 15Fh = 9Fh: not a MIDI data byte, sent as written) */
    int uart;                /* wavemus: MPU reset (FFh, FFh), UART mode (3Fh); raw bytes; the SB16 mixer at init when
                              * 8D4C; the close resets the MPU twice */
    int release_remembered;  /* wavemus (521h): every release is the channel's remembered note, and an effect note on
                              * releases it first; rolmus (5FAh): channel 7 releases the mapped note, an effect note on
                              * nothing */
    int send_sysex;          /* rolmus sends the streams' exclusive messages; wavemus reads past them, sends none */
    int effect_wait_byte;    /* wavemus (081Eh): an effect's wait always takes the next byte as its delay, the short
                              * form 1n too (an original defect: the byte after 1n, another event, is consumed) */
};
static const struct MusicRolandProfile MUSIC_ROLAND_PROFILES[] = {
        {MUSIC_ROLAND_OVERLAY_BYTES,
         "\xe9\xbd\x01\xe9\x2f\x03",
         0x168,
         0x10eb,
         {0xa86, 0xabc, 0x10a3, 0xbc1, 0x10af, 0xcbb, 0x10bb, 0xdb5, 0x10c7, 0xeaf, 0x10d3, 0xfa9,
          0x10df},
         1,
         1,
         "rolmus",
         13,
         0,
         0,
         0,
         1,
         0},
        {7354,
         "\xe9\xb9\x01\xe9\xaa\x02",
         0x164,
         0x100c,
         {0x9a7, 0x9dd, 0xfc4, 0xae2, 0xfd0, 0xbdc, 0xfdc, 0xcd6, 0xfe8, 0xdd0, 0xff4, 0xeca,
          0x1000},
         0,
         0,
         "rolmus",
         13,
         0,
         0,
         0,
         1,
         0},
        /* wavemus: note table 16Ah, cue table F65h; no fade pre-step, no sweep limit. */
        {7202, "\xe9\xbf\x01\xe9\x76\x02", 0x16a, 0xf65, {0}, 0, 0, "wavemus", 0, 0x15f, 1, 1, 0, 1},
};
/* INTRO.CC's canmus (4443 bytes): XEEN.DAT loads its drivers from INTRO.CC (15E12: 17FFE("intro.cc", 4) before the
 * driver loads, 15F4C back) and selects canmus for XEEN.CFG music index 5 (the switch at 15EC9). XEEN.CC's 4380-byte
 * build is not the one the game plays and is not accepted. Its signature (the jumps to entries 0 and 3), the note
 * table at 163h, the cue table at 9EDh (181 words) and the initial song volume byte at 159h (63). Its decoder and
 * service are read from the driver's code (xeen_dat_evidence/phase285/a/CANMUS.md) and compared against the
 * unmodified driver in Unicorn (canmus_oracle.py). */
#define MUSIC_CANMUS_OVERLAY_BYTES 4443
#define MUSIC_CANMUS_SIGNATURE     "\xe9\xb8\x01\xe9\x06\x02"
#define MUSIC_CANMUS_NOTE_TABLE    0x163
#define MUSIC_CANMUS_CUE_TABLE     0x9ed
#define MUSIC_CANMUS_SONG_VOLUME   0x159
/* canmus's fade (0929h): channels 10..0 but 8 at or above 2 (a signed byte) are lowered by one, sent as volume. */
#define MUSIC_CANMUS_FADE_FLOOR    2
/* canmus's fade covers channels 10..0 but the rhythm (8); the MT-32 fade the expression of channels 6..0 at or above
 * 40 (and the rhythm channel on the profile with that pre-step). */
#define MUSIC_CANMUS_FADE_TOP      10
#define MUSIC_ROLAND_FADE_TOP      6
#define MUSIC_ROLAND_FADE_FLOOR    40
/* canmus's master volume message (033Dh): channel 7's volume 111 after a stop, a restart and a fade's end. */
#define MUSIC_CANMUS_MASTER_VOLUME 111
#define MUSIC_SYSEX_CAPACITY_BYTES 65536

/* MIDI 1.0 channel-voice status kinds and the controller numbers the drivers send. */
enum {
    MIDI_NOTE_OFF = 0x80,
    MIDI_NOTE_ON = 0x90,
    MIDI_CONTROL = 0xb0,
    MIDI_PROGRAM = 0xc0,
    MIDI_PITCH_BEND = 0xe0
};
enum {
    MIDI_CC_BANK_SELECT = 0,
    MIDI_CC_VOLUME = 7,
    MIDI_CC_PAN = 10,
    MIDI_CC_EXPRESSION = 11,
    MIDI_CC_RESET_ALL = 121,
    MIDI_CC_ALL_NOTES_OFF = 123
};
/* The status byte of sequencer channel C. The drivers send channel C on MIDI channel C + 1
 * (0-based), so channel 0 is never used and sequencer channel 8, whose notes bypass the pitch
 * table, is the GM/GS rhythm channel. */
#define MUSIC_STATUS(kind, c) ((kind) + 1 + (c))
#define MUSIC_RHYTHM_CHANNEL 8

/* Roland exclusive message header: manufacturer 41h, device 10h, then the model (16h MT-32,
 * 42h GS) and command 12h (data set 1); the address and data follow, then the checksum, F7h. */
enum {
    MIDI_SYSEX_START = 0xf0,
    MIDI_SYSEX_END = 0xf7,
    ROLAND_ID = 0x41,
    ROLAND_DEVICE = 0x10,
    ROLAND_MODEL_MT32 = 0x16,
    ROLAND_MODEL_GS = 0x42,
    ROLAND_DATA_SET = 0x12
};
/* Phase 286: the GS-family profiles: canmus (Roland Canvas, XEEN.CFG music 5) and tapmus (Roland TAP-10, music 9;
 * INTRO.CC 4306 bytes, xeen_dat_evidence/phase286/a/tapmus.asm): the same decoder and data layout. tapmus drives a
 * plain UART (the MPU reset FFh once, never the UART command), writes the SB16 mixer at init when 8D4C, sends its
 * song reset with the model 56h instead of 16h, and writes DFh (canmus's MPU "send system message" command) to the
 * DATA port before every exclusive message: a stray channel-pressure status byte on the wire. */
struct MusicCanmusProfile {
    const char *member;
    size_t size;
    const char *signature;
    unsigned note_table, cue_table, song_volume;
    unsigned reset_model;
    int uart;
};
static const struct MusicCanmusProfile MUSIC_CANMUS_PROFILES[] = {
        {"canmus", MUSIC_CANMUS_OVERLAY_BYTES, MUSIC_CANMUS_SIGNATURE, MUSIC_CANMUS_NOTE_TABLE, MUSIC_CANMUS_CUE_TABLE,
         MUSIC_CANMUS_SONG_VOLUME, ROLAND_MODEL_MT32, 0},
        {"tapmus", 4306, "\xe9\xb8\x01\xe9\x45\x02", 0x163, 0x973, 0x159, 0x56, 1},
};
/* The stray byte tapmus writes to the data port before each exclusive message. */
#define MUSIC_TAPMUS_STRAY 0xdfu

/* An event's opcode byte: the kind in the high nibble, the channel (or a short operand) in the
 * low one. Songs and effects share one decoder; where a kind differs between the two streams,
 * both readings are given (MUSIC_OPERANDS has their operand bytes). */
enum {
    /* song: call the word target (FFh returns); effect: nothing */
    MUSIC_EVENT_CALL = 0,
    /* the channel nibble, or when it is 0 the operand byte, is the wait */
    MUSIC_EVENT_WAIT = 1,
    /* the channel's program-table entry; song: byte 24 (MT-32 family) or 25 (GS family) of 26 */
    MUSIC_EVENT_PROGRAM_SET = 2,
    /* effect: the channel's expression (volume on the GS family); song: nothing */
    MUSIC_EVENT_EXPRESSION = 3,
    /* song: the bend word; effect: ends the channel's bend sweep */
    MUSIC_EVENT_PITCH_BEND = 4,
    /* effect: the sweep's delta and start value; song: a GS-family controller, else skipped */
    MUSIC_EVENT_BEND_SWEEP = 5,
    MUSIC_EVENT_PAN = 6,
    /* no operands, nothing sent (either family, either stream) */
    MUSIC_EVENT_NOTHING_7 = 7,
    MUSIC_EVENT_NOTE_OFF = 8,
    MUSIC_EVENT_NOTE_ON = 9,
    /* a (controller or selector, value) pair; see the decoder */
    MUSIC_EVENT_CONTROL = 10,
    MUSIC_EVENT_SYSEX = 11,
    /* program change from the stream's program table */
    MUSIC_EVENT_PROGRAM = 12,
    MUSIC_EVENT_NOTHING_13 = 13,
    /* song: three operand bytes skipped; effect: nothing */
    MUSIC_EVENT_SKIP = 14,
    /* FFh: return from a call, or restart; any other: the stream ends */
    MUSIC_EVENT_END = 15
};

/** Reads the little-endian word at B of a song. */
static unsigned music_read_u16_le(const uint8_t *b)
{
    return b[0] | ((unsigned)b[1] << 8);
}

/* Latch the first decoder error in borrowed state and stop both song and effect decoding. Copy
 * the borrowed error text into fixed storage; no allocation or status is returned.
 */
static void music_fail_sequence(Music *music, const char *s)
{
    if (!music->error[0]) {
        snprintf(music->error, sizeof(music->error), "%s", s);
    }
    music->song_active = music->effect_active = 0;
}

/* Deliver a short MIDI message through the optional callback. The callback borrows stack bytes
 * only until it returns, so asynchronous outputs must copy them. The sequencer owns no
 * callback context and receives no delivery status.
 */
static void music_emit_message(Music *music, unsigned status, unsigned first_data,
                               unsigned second_data, unsigned message_bytes)
{
    uint8_t message[3] = {(uint8_t)status, (uint8_t)first_data, (uint8_t)second_data};
    if (music->emit) {
        music->emit(music->context, music->now, message, message_bytes);
    }
}

/* Phase 286: a transport event, not MIDI (music.h): an MPU command-port byte or an SB mixer write. */
static void music_emit_transport(Music *music, unsigned kind, unsigned a, unsigned b)
{
    uint8_t event[4] = {(uint8_t)kind, 0, (uint8_t)a, (uint8_t)b};
    if (music->emit) {
        music->emit(music->context, music->now, event, 4);
    }
}
/* The UART drivers' init (wavemus 01E1h, tapmus): with XEEN.CFG's 8D4C nonzero, the SB16 mixer at 8D3A + 4: line 2Eh,
 * master 22h, FM 26h = FFh, voice 04h = CCh, input gains 41h/42h = 40h; then the MPU reset (wavemus: FFh direct and
 * FFh again through its command routine, then the UART command 3Fh; tapmus: FFh only). */
static void music_uart_init(Music *music, unsigned config_8d4c, int wavemus)
{
    static const uint8_t mixer[6][2] = {{0x2e, 0xff}, {0x22, 0xff}, {0x26, 0xff}, {0x04, 0xcc}, {0x41, 0x40}, {0x42, 0x40}};
    for (unsigned i = 0; config_8d4c && i < 6u; i++) {
        music_emit_transport(music, MUSIC_EVENT_MIXER, mixer[i][0], mixer[i][1]);
    }
    music_emit_transport(music, MUSIC_EVENT_MPU_COMMAND, 0xff, 0);
    if (wavemus) {
        music_emit_transport(music, MUSIC_EVENT_MPU_COMMAND, 0xff, 0);
        music_emit_transport(music, MUSIC_EVENT_MPU_COMMAND, 0x3f, 0);
    }
}

/** Silences the MIDI channels: rolmus channels 8..0; canmus (03CBh) channels 9 and 10, then 6..0 (not 7 or 8). */
static void music_stop_all_notes(Music *music)
{
    int c;
    if (music->gs) {
        music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, 9), MIDI_CC_ALL_NOTES_OFF, 0, 3);
        music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, 10), MIDI_CC_ALL_NOTES_OFF, 0, 3);
        for (c = 6; c >= 0; c--) {
            music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, c), MIDI_CC_ALL_NOTES_OFF, 0, 3);
        }
    } else {
        for (c = 8; c >= 0; c--) {
            music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, c), MIDI_CC_ALL_NOTES_OFF, 0, 3);
        }
    }
}

/** Resets the pitch bend of every music channel to its centre. */
static void music_center_pitch_bends(Music *music)
{
    int c;
    for (c = 8; c >= 0; c--) {
        music_emit_message(music, MUSIC_STATUS(MIDI_PITCH_BEND, c), 0, 64, 3);
    }
}

/* Read an overlay SysEx payload, append its Roland checksum and deliver one complete borrowed
 * message to the callback. Advance the caller-owned cursor; malformed data latches the
 * sequencer error. The callback must copy stack bytes before returning.
 */
static void music_emit_sysex(Music *music, const uint8_t *bytes, size_t size_bytes,
                             unsigned *position, unsigned model)
{
    uint8_t message[MUSIC_SYSEX_CAPACITY_BYTES];
    size_t written = 5;
    unsigned checksum_sum = 0;
    message[0] = MIDI_SYSEX_START;
    message[1] = ROLAND_ID;
    message[2] = ROLAND_DEVICE;
    message[3] = (uint8_t)model;
    message[4] = ROLAND_DATA_SET;
    /* The first payload byte is included with every subsequent address/data byte. */
    while (*position < size_bytes && bytes[*position] != MIDI_SYSEX_END &&
           written < sizeof(message) - 2) {
        unsigned value = bytes[(*position)++];
        if (value > 127) {
            music_fail_sequence(music, "invalid SysEx data");
            return;
        }
        checksum_sum += value;
        message[written++] = (uint8_t)value;
    }
    if (*position >= size_bytes || bytes[*position] != MIDI_SYSEX_END) {
        music_fail_sequence(music, "unterminated SysEx");
        return;
    }
    ++*position;
    message[written++] = (uint8_t)((0 - checksum_sum) & 127);
    message[written++] = MIDI_SYSEX_END;
    if (music->emit) {
        music->emit(music->context, music->now, message, written);
    }
}

/* canmus's exclusive message (0785h), song and effect alike: the header always names the GS model (the driver tests
 * the opcode byte, never zero, for the MT-32 header); the first payload byte is taken unconditionally, the following
 * ones up to F7h; no byte is checked; the checksum is the 8-bit sum's negation, 7 bits. Reading past the resource
 * latches the sequencer error (the driver would read the memory that follows). */
static void music_emit_canmus_sysex(Music *music, const uint8_t *bytes, size_t size_bytes, unsigned *position)
{
    uint8_t message[MUSIC_SYSEX_CAPACITY_BYTES];
    size_t written = 5;
    uint8_t checksum_sum = 0;
    if (music->uart) {
        music_emit_message(music, MUSIC_TAPMUS_STRAY, 0, 0, 1);
    }
    message[0] = MIDI_SYSEX_START;
    message[1] = ROLAND_ID;
    message[2] = ROLAND_DEVICE;
    message[3] = ROLAND_MODEL_GS;
    message[4] = ROLAND_DATA_SET;
    do {
        if (*position >= size_bytes || written >= sizeof(message) - 2) {
            music_fail_sequence(music, "unterminated SysEx");
            return;
        }
        checksum_sum = (uint8_t)(checksum_sum + bytes[*position]);
        message[written++] = bytes[(*position)++];
    } while (*position >= size_bytes || bytes[*position] != MIDI_SYSEX_END);
    ++*position;
    message[written++] = (uint8_t)((0u - checksum_sum) & 127u);
    message[written++] = MIDI_SYSEX_END;
    if (music->emit) {
        music->emit(music->context, music->now, message, written);
    }
}

/* canmus's song reset (0352h, a restart's first message): the MT-32 model's exclusive 7F 00 03 = 7Fh, sent as
 * immediates by the driver's code (not from its data). */
static void music_emit_canmus_reset(Music *music)
{
    uint8_t reset[] = {MIDI_SYSEX_START, ROLAND_ID, ROLAND_DEVICE, ROLAND_MODEL_MT32,
                       ROLAND_DATA_SET, 0x7f, 0x00, 0x03, 0x7f, 0x7f, MIDI_SYSEX_END};
    reset[3] = (uint8_t)music->canmus->reset_model; /* tapmus: 56h */
    if (music->uart) {
        music_emit_message(music, MUSIC_TAPMUS_STRAY, 0, 0, 1);
    }
    if (music->emit) {
        music->emit(music->context, music->now, reset, sizeof reset);
    }
}

/* Map an encoded song note: bits 5..7 are the octave (12 semitones each), bits 0..4 index
 * the borrowed overlay's pitch table. Return the wrapped MIDI note number; callers must have
 * validated overlay storage.
 */
static unsigned music_map_note(Music *music, unsigned b)
{
    unsigned octave = (b & 224) >> 5;
    return (octave * 12 +
            music->overlay.data[(music->gs ? music->canmus->note_table : music->roland->note_table) + (b & 31)]) &
           127;
}

/* Emit the channel's remembered note release (velocity 127; rolmus's channel 7 releases the mapped note) and, on
 * rolmus, mark that note silent. canmus's release (0580h) keeps the remembered note: a second effect note off on the
 * channel releases it again. */
static void music_stop_remembered_note(Music *music, unsigned ch, unsigned mapped)
{
    music_emit_message(music, MUSIC_STATUS(MIDI_NOTE_OFF, ch),
                       !music->gs && !music->roland->release_remembered && ch == 7 ? mapped : music->note[ch] & 127,
                       127, 3);
    if (!music->gs) {
        music->note[ch] = 255;
    }
}

/* Each kind's operand bytes in a song and in an effect cue: the bytes after the opcode that the event consumes, checked
 * against the resource's end before any is read. Two kinds depend on more than the stream (music_operand_bytes): a
 * wait with a nonzero channel nibble carries its delay there, and an MT-32-family effect's controller has none. */
static const struct {
    uint8_t song, effect;
} MUSIC_OPERANDS[16] = {
        [MUSIC_EVENT_CALL] = {2, 0},     [MUSIC_EVENT_WAIT] = {1, 1},       [MUSIC_EVENT_PROGRAM_SET] = {26, 1},
        [MUSIC_EVENT_EXPRESSION] = {0, 1}, [MUSIC_EVENT_PITCH_BEND] = {2, 0}, [MUSIC_EVENT_BEND_SWEEP] = {2, 4},
        [MUSIC_EVENT_PAN] = {1, 1},      [MUSIC_EVENT_NOTHING_7] = {0, 0},  [MUSIC_EVENT_NOTE_OFF] = {1, 1},
        [MUSIC_EVENT_NOTE_ON] = {2, 2},  [MUSIC_EVENT_CONTROL] = {2, 2},    [MUSIC_EVENT_SYSEX] = {0, 0},
        [MUSIC_EVENT_PROGRAM] = {1, 1},  [MUSIC_EVENT_NOTHING_13] = {0, 0}, [MUSIC_EVENT_SKIP] = {3, 0},
        [MUSIC_EVENT_END] = {0, 0}};
/* The operand bytes of an event of KIND on CHANNEL (MUSIC_OPERANDS and its two exceptions). wavemus (081Eh) takes an
 * effect wait's delay byte even after the short form 1n: an original defect that consumes the next event's byte. */
static unsigned music_operand_bytes(const Music *music, unsigned kind, unsigned channel, int effect)
{
    if (kind == MUSIC_EVENT_WAIT && channel && !(effect && !music->gs && music->roland->effect_wait_byte)) {
        return 0;
    }
    if (kind == MUSIC_EVENT_CONTROL && effect && !music->gs) {
        return 0;
    }
    return effect ? MUSIC_OPERANDS[kind].effect : MUSIC_OPERANDS[kind].song;
}

/* The stream a batch decodes: the song (its own copied resource) or the effect cue (inside the driver overlay), its
 * bounds, and the read position, which the batch's flow events move and a wait parks (music_park_stream). */
typedef struct {
    const uint8_t *bytes;
    size_t size;
    unsigned position;
    int effect;
} MusicStream;
/* The song's or (EFFECT) the effect cue's stream at its parked position. */
static MusicStream music_stream(const Music *music, int effect)
{
    MusicStream s = {effect ? music->overlay.data : music->song.data, effect ? music->overlay.size : music->song.size,
                     effect ? music->effect_pos : music->song_pos, effect};
    return s;
}
/* Parks stream S for the next batch: its position and the ticks to wait before it. */
static void music_park_stream(Music *music, const MusicStream *s, unsigned wait)
{
    if (s->effect) {
        music->effect_wait = wait;
        music->effect_pos = s->position;
    } else {
        music->song_wait = wait;
        music->song_pos = s->position;
    }
}
/* One decoded event: its opcode (kind in the high nibble, channel in the low), its operand bytes and the first two
 * of them (0 when absent), as music_read_event has validated them. */
typedef struct {
    unsigned opcode, kind, channel, operand_bytes, first, second;
} MusicEvent;
/* Reads the event at S's position into *E, the position left after the opcode (the operands are still to be
 * consumed). 0, the sequencer error latched, when the opcode or its operands lie outside the resource. */
static int music_read_event(Music *music, MusicStream *s, MusicEvent *e)
{
    if (s->position >= s->size) {
        music_fail_sequence(music, "sequence position outside resource");
        return 0;
    }
    e->opcode = s->bytes[s->position++];
    e->channel = e->opcode & 15;
    e->kind = e->opcode >> 4;
    e->operand_bytes = music_operand_bytes(music, e->kind, e->channel, s->effect);
    if (e->operand_bytes > s->size - s->position) {
        music_fail_sequence(music, "truncated music event");
        return 0;
    }
    e->first = e->operand_bytes ? s->bytes[s->position] : 0;
    e->second = e->operand_bytes > 1 ? s->bytes[s->position + 1] : 0;
    return 1;
}
/* The little-endian operand word at byte OFFSET of the event's operands. */
static unsigned music_operand_word(const MusicStream *s, unsigned offset)
{
    return music_read_u16_le(s->bytes + s->position + offset);
}

/* ---- The data events: each is translated to its MIDI messages (or a state change), and then exactly its operand
 * bytes are consumed (music_decode_batch). */

/* A note off: an effect stops the note it remembers on the channel; a song sends the note off (velocity 64) and
 * forgets the channel's note. A song's channel 8 (the rhythm) takes its note unmapped. */
static void music_decode_note_off(Music *music, const MusicEvent *e, int effect)
{
    unsigned note = e->channel == MUSIC_RHYTHM_CHANNEL && !effect ? e->first : music_map_note(music, e->first);
    if (effect) {
        music_stop_remembered_note(music, e->channel, note);
    } else {
        music_emit_message(music, MUSIC_STATUS(MIDI_NOTE_OFF, e->channel), note, 64, 3);
        music->note[e->channel] = 255;
    }
}
/* A note on (channel 8 unmapped): the channel's remembered note stops first, an effect's on the GS family (and
 * wavemus) only, a song's unless it is the MT-32 family's rhythm channel or note offs are suppressed (the load's mode
 * 1); then the note sounds and is remembered. */
static void music_decode_note_on(Music *music, const MusicEvent *e, int effect)
{
    unsigned note = e->channel == MUSIC_RHYTHM_CHANNEL ? e->first : music_map_note(music, e->first);
    if ((effect ? music->gs || music->roland->release_remembered
                : (music->gs || e->channel != 8) && !music->suppress_note_off)) {
        music_stop_remembered_note(music, e->channel, note);
    }
    music_emit_message(music, MUSIC_STATUS(MIDI_NOTE_ON, e->channel), note, e->second, 3);
    music->note[e->channel] = (uint8_t)note;
}
/* The channel's expression VALUE, unless a fade runs (the fade owns the expressions while it lowers them): kept and
 * sent as the volume controller (GS family) or the expression controller (MT-32 family). */
static void music_set_expression(Music *music, unsigned channel, unsigned value)
{
    if (music->fade) {
        return;
    }
    music->expression[channel] = (uint8_t)value;
    music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, channel), music->gs ? MIDI_CC_VOLUME : MIDI_CC_EXPRESSION,
                       value, 3);
}
/* A controller: a GS-family effect sends it as it is; a song's volume controller (1 in the GS family, 0 in the MT-32
 * family) sets the channel's expression (music_set_expression), any other song controller is skipped. An MT-32-family
 * effect's controller has no operands and does nothing. */
static void music_decode_control(Music *music, const MusicEvent *e, int effect)
{
    if (effect) {
        if (music->gs) {
            music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, e->channel), e->first, e->second, 3);
        }
    } else if (e->first == (music->gs ? 1u : 0u)) {
        music_set_expression(music, e->channel, e->second);
    }
}
/* A song's pitch bend word: its high byte, then its low byte, each masked to 7 bits in the MT-32 family; canmus
 * (03B7h) sends both unmasked. */
static void music_decode_pitch_bend(Music *music, const MusicStream *s, const MusicEvent *e)
{
    unsigned v = music_operand_word(s, 0), mask = music->gs ? 255u : 127u;
    music_emit_message(music, MUSIC_STATUS(MIDI_PITCH_BEND, e->channel), (v >> 8) & mask, v & mask, 3);
}
/* The program-table entry of a song is byte 24 (MT-32 family) or 25 (GS family) of its 26 operand bytes. */
#define MUSIC_SONG_PROGRAM_BYTE(gs) ((gs) ? 25u : 24u)
/* Translates data event E of stream S (the position at its operands, which the caller consumes). */
static void music_decode_data(Music *music, const MusicStream *s, const MusicEvent *e)
{
    int effect = s->effect;
    switch (e->kind) {
        case MUSIC_EVENT_PROGRAM_SET:
            if (effect) {
                music->effect_program[e->channel] = (uint8_t)e->first;
            } else {
                music->song_program[e->channel] = s->bytes[s->position + MUSIC_SONG_PROGRAM_BYTE(music->gs)];
            }
            break;
        case MUSIC_EVENT_EXPRESSION:
            if (effect) {
                music_set_expression(music, e->channel, e->first);
            }
            break;
        case MUSIC_EVENT_PITCH_BEND:
            if (effect) {
                music->bend_delta[e->channel] = 0; /* the channel's sweep ends */
            } else {
                music_decode_pitch_bend(music, s, e);
            }
            break;
        case MUSIC_EVENT_BEND_SWEEP:
            if (effect) {
                music->bend_delta[e->channel] = (uint16_t)music_operand_word(s, 0);
                music->bend_value[e->channel] = (uint16_t)music_operand_word(s, 2);
                music->bend_ticks = 0;
            } else if (music->gs) {
                music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, e->channel), e->first, e->second, 3);
            }
            break;
        case MUSIC_EVENT_PAN:
            music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, e->channel), MIDI_CC_PAN, e->first, 3);
            break;
        case MUSIC_EVENT_NOTE_OFF:
            music_decode_note_off(music, e, effect);
            break;
        case MUSIC_EVENT_NOTE_ON:
            music_decode_note_on(music, e, effect);
            break;
        case MUSIC_EVENT_CONTROL:
            music_decode_control(music, e, effect);
            break;
        case MUSIC_EVENT_PROGRAM:
            if (e->first >= MUSIC_CHANNEL_COUNT) {
                music_fail_sequence(music, "program table index outside supported range");
                break;
            }
            music_emit_message(music, MUSIC_STATUS(MIDI_PROGRAM, e->channel),
                               effect ? music->effect_program[e->first] : music->song_program[e->first], 0, 2);
            break;
        default: /* MUSIC_EVENT_NOTHING_7, MUSIC_EVENT_NOTHING_13, MUSIC_EVENT_SKIP: nothing but the operands */
            break;
    }
}

/* ---- The flow events: each moves the stream itself (a call, an exclusive message's variable length, an end) or
 * ends the batch (a wait, a stream's end). */

/* Whether the batch goes on after an event: the next event, or the batch is over (a wait or the stream's end). */
enum { MUSIC_BATCH_NEXT, MUSIC_BATCH_OVER };
/* A song's call (effects ignore it): the return position pushed (16 deep), then the jump to the operand word. A
 * seventeenth nested call is an error on rolmus; canmus (0872h) ignores it without consuming its operand word, so
 * the word's bytes are decoded as the next events. */
static void music_decode_call(Music *music, MusicStream *s)
{
    if (s->effect) {
        return;
    }
    if (music->gs && music->depth >= MUSIC_RETURN_STACK_DEPTH) {
        return;
    }
    if (music->depth >= MUSIC_RETURN_STACK_DEPTH) {
        music_fail_sequence(music, "music call stack overflow");
        return;
    }
    music->returns[music->depth++] = s->position + 2;
    s->position = music_operand_word(s, 0);
}
/* A wait ends the batch: its operand byte when it has one (nibble 0, or wavemus's effect: music_operand_bytes), else
 * the channel nibble, in ticks; the stream is parked past the operands for the next batch. */
static void music_decode_wait(Music *music, MusicStream *s, const MusicEvent *e)
{
    unsigned delay = e->operand_bytes ? e->first : e->channel;
    s->position += e->operand_bytes;
    music_park_stream(music, s, delay);
}
/* wavemus's exclusive message (06EFh): the first byte, then every byte up to F7h is read (the checksum computed)
 * and nothing is sent. Reading past the resource latches the sequencer error. */
static void music_skip_sysex(Music *music, MusicStream *s)
{
    unsigned first = 1;
    for (;;) {
        if (s->position >= s->size) {
            music_fail_sequence(music, "unterminated SysEx");
            return;
        }
        if (s->bytes[s->position++] == MIDI_SYSEX_END && !first) {
            return;
        }
        first = 0;
    }
}
/* An exclusive message, read to its F7h by the family's own rule: the GS family's (always the GS header, no byte
 * checked), rolmus's (the MT-32 header, every byte checked) or wavemus's (read, not sent). */
static void music_decode_sysex(Music *music, MusicStream *s)
{
    if (music->gs) {
        music_emit_canmus_sysex(music, s->bytes, s->size, &s->position);
    } else if (music->roland->send_sysex) {
        music_emit_sysex(music, s->bytes, s->size, &s->position, ROLAND_MODEL_MT32);
    } else {
        music_skip_sysex(music, s);
    }
}
/* The end of a stream. FFh loops: a GS-family song at its top level first resets the bank select of channels 8..0
 * and stops every note; an effect restarts at its start, a called song returns, a song restarts at 0. Any other end
 * opcode stops the stream (the batch is over; its position is not parked). */
static int music_decode_end(Music *music, MusicStream *s, const MusicEvent *e)
{
    if (e->opcode != 255) {
        if (s->effect) {
            music->effect_active = 0;
        } else {
            music->song_active = 0;
        }
        return MUSIC_BATCH_OVER;
    }
    if (music->gs && !s->effect && !music->depth) {
        for (int c = 8; c >= 0; c--) {
            music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, c), MIDI_CC_BANK_SELECT, 0, 3);
        }
        music_stop_all_notes(music);
    }
    s->position = s->effect ? music->effect_start : music->depth ? music->returns[--music->depth] : 0;
    return MUSIC_BATCH_NEXT;
}
/* Decodes one validated event E of S: a flow event moves the stream itself; a data event is translated and its
 * operands consumed. Whether the batch goes on. */
static int music_decode_event(Music *music, MusicStream *s, const MusicEvent *e)
{
    switch (e->kind) {
        case MUSIC_EVENT_CALL:
            music_decode_call(music, s);
            return MUSIC_BATCH_NEXT;
        case MUSIC_EVENT_WAIT:
            music_decode_wait(music, s, e);
            return MUSIC_BATCH_OVER;
        case MUSIC_EVENT_SYSEX:
            music_decode_sysex(music, s);
            return MUSIC_BATCH_NEXT;
        case MUSIC_EVENT_END:
            return music_decode_end(music, s, e);
        default:
            music_decode_data(music, s, e);
            s->position += e->operand_bytes;
            return MUSIC_BATCH_NEXT;
    }
}
/* One service tick's batch of the song or (EFFECT) the effect cue: events are decoded and their messages emitted
 * synchronously, in stream order, until a wait parks the stream or its end stops it. A malformed stream (an event or
 * its operands outside the resource, an unterminated exclusive message, an overflowing call on rolmus, a bad program
 * index) or more than MUSIC_BATCH_EVENT_LIMIT events without a wait (a loop without one) latch music_error, which
 * stops all decoding; the stream is then not parked. */
static void music_decode_batch(Music *music, int effect)
{
    MusicStream s = music_stream(music, effect);
    for (unsigned events = 0; events < MUSIC_BATCH_EVENT_LIMIT && !music->error[0]; events++) {
        MusicEvent e;
        if (!music_read_event(music, &s, &e)) {
            break;
        }
        if (music_decode_event(music, &s, &e) == MUSIC_BATCH_OVER) {
            return;
        }
    }
    if (!music->error[0]) {
        music_fail_sequence(music, "music event budget exhausted");
    }
}

/* One fade step of channel C: an expression at or above FLOOR (as a signed byte) is lowered by one and sent as
 * CONTROLLER. Returns 1 when it changed. */
static int music_fade_channel(Music *music, int c, unsigned controller, int floor)
{
    if ((int8_t)music->expression[c] < floor) {
        return 0;
    }
    --music->expression[c];
    music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, c), controller, music->expression[c], 3);
    return 1;
}
/* canmus's master volume (033Dh): channel 7's volume 111. */
static void music_emit_canmus_master_volume(Music *music)
{
    music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, 7), MIDI_CC_VOLUME, MUSIC_CANMUS_MASTER_VOLUME, 3);
}
/* The fade's end: the song stops with every note; canmus (0942h) then sends its master volume. */
static void music_fade_end(Music *music)
{
    music->song_active = 0;
    music->fade = 0;
    music_stop_all_notes(music);
    if (music->gs) {
        music_emit_canmus_master_volume(music);
    }
}
/* One fade step over the song's channels, emitting their controllers or note stops: on canmus (0954h) the volume of
 * channels 10..0 but 8 at or above 2; on the MT-32 the expression of channels 6..0 at or above 40, after the rhythm
 * channel's on the profile that has that pre-step. Returns whether any channel changed. */
static int music_fade_channels(Music *music)
{
    int changed = 0;
    if (music->gs) {
        for (int c = MUSIC_CANMUS_FADE_TOP; c >= 0; c--) {
            if (c != MUSIC_RHYTHM_CHANNEL && music_fade_channel(music, c, MIDI_CC_VOLUME, MUSIC_CANMUS_FADE_FLOOR)) {
                changed = 1;
            }
        }
        return changed;
    }
    /* The original fade can inherit an interrupted channel index whose exact
     * value is unknown. Use the expression slot belonging to the rhythm
     * status byte. This choice affects the first decrement and fade end.
     */
    if (music->roland->fade_rhythm_step && music_fade_channel(music, MUSIC_RHYTHM_CHANNEL, MIDI_CC_EXPRESSION, MUSIC_ROLAND_FADE_FLOOR)) {
        changed = 1;
    }
    for (int c = MUSIC_ROLAND_FADE_TOP; c >= 0; c--) {
        if (music_fade_channel(music, c, MIDI_CC_EXPRESSION, MUSIC_ROLAND_FADE_FLOOR)) {
            changed = 1;
        }
    }
    return changed;
}
/* One tick of a pending fade, in borrowed channel state: the fade rate accumulates in an 8-bit phase, and each carry
 * is one step (music_fade_channels). The fade ends (music_fade_end) past its last step or when a step changes no
 * channel. This operation uses no allocation; decoding failure remains latched in the sequencer.
 */
static void music_service_fade(Music *music)
{
    unsigned sum;
    if (!music->fade) {
        return;
    }
    sum = music->fade_acc + music->fade;
    music->fade_acc = sum & 255;
    if (sum <= 255) {
        return;
    }
    if (!music->fade_left-- || !music_fade_channels(music)) {
        music_fade_end(music);
    }
}

/* Service one music tick in borrowed state, including pitch changes, pending waits,
 * song/effect batches and fades. Emit due messages in their existing order; malformed data is
 * retained as music_error.
 */
static void music_service_tick(Music *music)
{
    if (music->song_active) {
        if (!music->song_wait || !--music->song_wait) {
            music_decode_batch(music, 0);
            return;
        }
    }
    if (music->effect_active) {
        if (!music->effect_wait || !--music->effect_wait) {
            music_decode_batch(music, 1);
            return;
        }
    }
    music_service_fade(music);
    if (music->bend_delta[7]) {
        if (!music->gs && music->roland->sweep_limit && ++music->bend_ticks == 400) {
            music->bend_delta[7] = 0;
            music->bend_ticks = 0;
        } else {
            music->bend_value[7] = (uint16_t)(music->bend_value[7] + music->bend_delta[7]);
            music_emit_message(music, MUSIC_STATUS(MIDI_PITCH_BEND, 7), music->bend_value[7] & 127,
                               (music->bend_value[7] >> 8) & 127, 3);
        }
    }
}

/* The Roland and SC-55 sequencers' allocation: a zeroed Music, or NULL when the allocation fails or CC is missing or
 * ORIGIN leaves no room for the first tick. */
static Music *music_alloc(const CC *cc, uint64_t origin)
{
    Music *music = calloc(1, sizeof(*music));
    if (!music) {
        return NULL;
    }
    if (!cc || origin > UINT64_MAX - MUSIC_TICK) {
        free(music);
        return NULL;
    }
    return music;
}

/* Create a Roland event sequencer from the borrowed overlay archive and emit its
 * initialization messages at the supplied PIT-cycle origin. The callback must consume or copy
 * borrowed message bytes immediately. Return a caller-owned Music for music_close, or NULL for
 * allocation, overlay or origin failure; retain neither archive nor callback-context
 * ownership.
 */
static Music *music_open_fm(CC *, const char *, unsigned, MusicEmit, void *, uint64_t);
/* Phase 286: the MT-32-family driver MEMBER (rolmus, or wavemus) of the borrowed archive, its profile chosen by size
 * and signature, and its initialization (entry 0): rolmus volume 79 on channels 8, 6..0, 127 on 7, its 13 patch
 * exclusive messages; wavemus the UART set-up (music_uart_init), the same volumes but its 9Fh byte, no exclusive
 * message. NULL for any other file. */
static Music *music_open_roland(CC *cc, const char *member, unsigned config_8d4c, MusicEmit emit, void *context,
                                uint64_t origin)
{
    Music *music = music_alloc(cc, origin);
    unsigned i, volume;
    if (!music) {
        return NULL;
    }
    if (cc_read(cc, member, &music->overlay)) {
        for (i = 0; i < sizeof(MUSIC_ROLAND_PROFILES) / sizeof(*MUSIC_ROLAND_PROFILES); i++) {
            if (!strcmp(member, MUSIC_ROLAND_PROFILES[i].member) &&
                music->overlay.size == MUSIC_ROLAND_PROFILES[i].size &&
                !memcmp(music->overlay.data, MUSIC_ROLAND_PROFILES[i].signature, 6)) {
                music->roland = &MUSIC_ROLAND_PROFILES[i];
            }
        }
    }
    if (!music->roland) {
        free(music->overlay.data);
        free(music);
        return NULL;
    }
    music->uart = (unsigned)music->roland->uart;
    music->emit = emit;
    music->context = context;
    music->now = origin;
    music->next = origin + MUSIC_TICK;
    memset(music->note, 255, sizeof(music->note));
    if (music->uart) {
        music_uart_init(music, config_8d4c, 1);
    }
    volume = music->roland->volume_byte ? music->overlay.data[music->roland->volume_byte] : 79u;
    music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, MUSIC_RHYTHM_CHANNEL), MIDI_CC_VOLUME, volume, 3);
    for (i = 7; i > 0; i--) {
        music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, i - 1), MIDI_CC_VOLUME, volume, 3);
    }
    music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, 7), MIDI_CC_VOLUME, 127, 3);
    for (i = 0; i < music->roland->patch_count; i++) {
        unsigned p = music->roland->patches[i];
        music_emit_sysex(music, music->overlay.data, music->overlay.size, &p, ROLAND_MODEL_MT32);
    }
    return music;
}
Music *music_open(CC *cc, MusicEmit emit, void *context, uint64_t origin)
{
    return music_open_roland(cc, "rolmus", 1, emit, context, origin);
}

/* Load the isolated SB Pro interpreter with the original promus bytes. Other modes never
 * load this library; each sequencer owns an independent dynamic-loader reference. */
Music *music_open_sbpro(CC *cc, MusicEmit emit, void *context, uint64_t origin)
{
    return music_open_fm(cc, "promus", 1, emit, context, origin);
}

/* Create the canmus (Sound Canvas, GS) sequencer from the borrowed overlay archive's INTRO.CC build and emit its
 * initialization at the supplied PIT-cycle origin, as the driver's entry 0 (01BBh) does: the volume byte at 159h on
 * channels 10..0 (045Eh), then channel 6's once more (047Dh adds the loop's exhausted index to B8h). The MPU-401 reset (FFh) and
 * the PIT divisor 4006h are hardware set-up, not MIDI (the tick is MUSIC_TICK). Return a caller-owned Music for
 * music_close, or NULL when canmus is absent or of another build; retain neither archive nor callback context. */
static Music *music_open_gs(CC *cc, const struct MusicCanmusProfile *profile, unsigned config_8d4c, MusicEmit emit,
                            void *context, uint64_t origin)
{
    Music *music = music_alloc(cc, origin);
    int c;
    if (!music) {
        return NULL;
    }
    if (!cc_read(cc, profile->member, &music->overlay) || music->overlay.size != profile->size ||
        memcmp(music->overlay.data, profile->signature, 6)) {
        free(music->overlay.data);
        free(music);
        return NULL;
    }
    music->gs = 1;
    music->canmus = profile;
    music->uart = (unsigned)profile->uart;
    music->emit = emit;
    music->context = context;
    music->now = origin;
    music->next = origin + MUSIC_TICK;
    memset(music->note, 255, sizeof(music->note));
    if (music->uart) {
        music_uart_init(music, config_8d4c, 0);
    }
    for (c = 10; c >= 0; c--) {
        music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, c), MIDI_CC_VOLUME,
                           music->overlay.data[profile->song_volume], 3);
    }
    music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, 6), MIDI_CC_VOLUME,
                       music->overlay.data[profile->song_volume], 3);
    return music;
}
Music *music_open_sc55(CC *cc, MusicEmit emit, void *context, uint64_t origin)
{
    return music_open_gs(cc, &MUSIC_CANMUS_PROFILES[0], 1, emit, context, origin);
}

/* Load the SB Pro library's sequencer for MEMBER (promus, or a one-chip FM driver) with XEEN.CFG's 8D4C. */
static Music *music_open_fm(CC *cc, const char *member, unsigned config_8d4c, MusicEmit emit, void *context,
                            uint64_t origin)
{
    Blob overlay = {0};
    Music *music;
    if (!cc || !cc_read(cc, member, &overlay)) {
        return NULL;
    }
    music = calloc(1, sizeof(*music));
    if (music) {
        music->fm_library = sbpro_library_open();
        if (music->fm_library) {
            music->fm_sequence = music->fm_library->api->sequence_open_flags(
                    overlay.data, overlay.size, config_8d4c ? SBPRO_FLAG_CONFIG_8D4C : 0u, emit, context, origin);
        }
        if (!music->fm_sequence) {
            sbpro_library_close(music->fm_library);
            free(music);
            music = NULL;
        }
    }
    free(overlay.data);
    return music;
}

Music *music_open_driver(CC *cc, const char *driver, unsigned config_8d4c, MusicEmit emit, void *context,
                         uint64_t origin)
{
    if (!driver) {
        return NULL;
    }
    if (!strcmp(driver, "rolmus") || !strcmp(driver, "wavemus")) {
        return music_open_roland(cc, driver, config_8d4c, emit, context, origin);
    }
    for (size_t i = 0; i < sizeof MUSIC_CANMUS_PROFILES / sizeof *MUSIC_CANMUS_PROFILES; i++) {
        if (!strcmp(driver, MUSIC_CANMUS_PROFILES[i].member)) {
            return music_open_gs(cc, &MUSIC_CANMUS_PROFILES[i], config_8d4c, emit, context, origin);
        }
    }
    if (!strcmp(driver, "promus") || !strcmp(driver, "admus") || !strcmp(driver, "blastmus") ||
        !strcmp(driver, "specmus")) {
        return music_open_fm(cc, driver, config_8d4c, emit, context, origin);
    }
    return NULL;
}

int music_uart(const Music *music)
{
    return music && music->uart;
}

/* Advance the borrowed sequencer to PIT-cycle time and emit every due event in order. Invalid,
 * backwards or excessively large advances latch music_error and stop decoding. Null or
 * already-failed sequencers are ignored; no storage is returned.
 */
void music_advance(Music *music, uint64_t pit)
{
    if (music && music->fm_sequence) {
        music->fm_library->api->advance(music->fm_sequence, pit);
        return;
    }
    if (!music || music->error[0]) {
        return;
    }
    if (pit < music->now) {
        music_fail_sequence(music, "music clock moved backwards");
        return;
    }
    if (pit > UINT64_MAX - MUSIC_TICK) {
        music_fail_sequence(music, "music clock exceeds supported range");
        return;
    }
    if (pit >= music->next && (pit - music->next) / MUSIC_TICK >= MUSIC_ADVANCE_LIMIT) {
        music_fail_sequence(music, "music clock advance budget exceeded");
        return;
    }
    while (music->next <= pit && !music->error[0]) {
        music->now = music->next;
        music->next += MUSIC_TICK;
        music_service_tick(music);
    }
    music->now = pit;
}

/* Advance to the supplied PIT time and select an effect cue in the borrowed sequencer.
 * Unsupported cue numbers and protected overlapping cues are ignored. Message bytes are
 * borrowed during callback delivery; latched decoder errors prevent further selection.
 */
void music_command(Music *music, unsigned command, uint64_t pit)
{
    if (music && music->fm_sequence) {
        music->fm_library->api->command(music->fm_sequence, command, pit);
        return;
    }
    if (!music) {
        return;
    }
    music_advance(music, pit);
    if (music->error[0] || command > XEEN_MUSIC_CUE_LAST) {
        return;
    }
    if (music->effect_active && command >= XEEN_MUSIC_EFFECT_FIRST && command < XEEN_MUSIC_EFFECT_END) {
        return;
    }
    if (music->gs) {
        /* canmus (027Fh): the sweeps of channels 7, 9 and 10 cleared; all notes off on channels 8 and 7 (0412h);
         * reset all controllers on 8, then 6..0 (043Bh); the pitch bends centred (038Fh). */
        music->bend_delta[7] = music->bend_delta[9] = music->bend_delta[10] = 0;
        music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, MUSIC_RHYTHM_CHANNEL),
                           MIDI_CC_ALL_NOTES_OFF, 0, 3);
        music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, 7), MIDI_CC_ALL_NOTES_OFF, 0, 3);
        for (int c = 8; c >= 0; c--) {
            if (c == 7) {
                continue;
            }
            music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, c), MIDI_CC_RESET_ALL, 0, 3);
        }
        music_center_pitch_bends(music);
    } else {
        music->bend_delta[7] = music->bend_delta[8] = 0;
        music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, 7), MIDI_CC_ALL_NOTES_OFF, 0, 3);
        music_center_pitch_bends(music);
    }
    music->effect_start = music->effect_pos = music_read_u16_le(
            music->overlay.data + (music->gs ? music->canmus->cue_table : music->roland->cue_table) + command * 2);
    music->effect_active = 1;
    music->effect_wait = 0;
}

/* canmus's entry 6 for V <= 255 (02B4h). 0 (02DFh): the song and the fade stop, all notes off, the master volume.
 * 1 (030Bh, also a song load's tail): the mode word (MODE; XEEN.DAT's 187A5 passes 0), the position, the call depth
 * and the wait cleared; the song reset (0352h), the pitch bends centred (038Fh), pan 63 on channels 8, 7 and 5..0
 * (0496h: channel 6 is skipped), bank select 0 on 8..0 (04B9h), the song started, all notes off (03CBh), the master
 * volume (033Dh); a running fade is NOT stopped (it goes on over the new song). 2..255 (02D4h): the fade rate, 127
 * steps, no message. */
static void music_control_canmus(Music *music, unsigned control, unsigned mode)
{
    int c;
    if (control == 0) {
        music->song_active = 0;
        music->fade = 0;
        music_stop_all_notes(music);
        music_emit_canmus_master_volume(music);
    } else if (control == 1) {
        music->suppress_note_off = mode == 1u;
        music->song_pos = 0;
        music->depth = 0;
        music->song_wait = 0;
        music_emit_canmus_reset(music);
        music_center_pitch_bends(music);
        for (c = 8; c >= 0; c--) {
            if (c != 6) {
                music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, c), MIDI_CC_PAN, 63, 3);
            }
        }
        for (c = 8; c >= 0; c--) {
            music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, c), MIDI_CC_BANK_SELECT, 0, 3);
        }
        music->song_active = music->song.data != NULL;
        music_stop_all_notes(music);
        music_emit_canmus_master_volume(music);
    } else if (control <= 255) {
        music->fade = control;
        music->fade_left = 127;
    }
}

/* Advance the borrowed sequencer, then stop with zero, restart with one or set a fade rate
 * from two through 255. Other control values are ignored. Null or failed sequencers do
 * nothing; errors remain available through music_error.
 */
void music_control(Music *music, unsigned control, uint64_t pit)
{
    if (music && music->fm_sequence) {
        music->fm_library->api->control(music->fm_sequence, control, pit);
        return;
    }
    if (!music) {
        return;
    }
    music_advance(music, pit);
    if (music->error[0]) {
        return;
    }
    if (music->gs) {
        music_control_canmus(music, control, 0);
        return;
    }
    if (control == 0) {
        music->song_active = 0;
        music->fade = 0;
        music_stop_all_notes(music);
    } else if (control == 1) {
        /* rolmus's restart (03A9h) takes the mode word again: XEEN.DAT's 0 (music_load_mode applies its own after). */
        music->suppress_note_off = 0;
        music->song_pos = 0;
        music->depth = 0;
        music->song_wait = 0;
        music_center_pitch_bends(music);
        for (int c = 8; c >= 0; c--) {
            music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, c), MIDI_CC_PAN, 63, 3);
        }
        music->song_active = music->song.data != NULL;
        music->fade = 0;
        music_stop_all_notes(music);
    } else if (control <= 255) {
        music->fade = control;
        music->fade_left = 127;
    }
}

/* Copy and start a song in the borrowed sequencer at the supplied PIT time with the driver's entry-6 mode word
 * MODE (1: a song note on does not first release the channel's remembered note; any other value: it does),
 * replacing its owned prior song storage. A null or empty song stops playback (the mode is not applied). Invalid
 * data or allocation failure latches music_error; the caller always retains its input Blob. The SB Pro sequencer
 * ignores the mode. */
void music_load_mode(Music *music, const Blob *song, unsigned mode, uint64_t pit)
{
    if (music && music->fm_sequence) {
        music->fm_library->api->song(music->fm_sequence, song ? song->data : NULL,
                                     song ? song->size : 0, pit);
        return;
    }
    uint8_t *b;
    if (!music) {
        return;
    }
    music_advance(music, pit);
    if (music->error[0]) {
        return;
    }
    if (!song || !song->size) {
        music_control(music, 0, pit);
        return;
    }
    if (!song->data || song->size > 65535) {
        music_fail_sequence(music, "invalid song storage or size");
        return;
    }
    b = malloc(song->size);
    if (!b) {
        music_fail_sequence(music, "song allocation failed");
        return;
    }
    memcpy(b, song->data, song->size);
    free(music->song.data);
    music->song.data = b;
    music->song.size = song->size;
    if (music->gs) {
        music_control_canmus(music, 1, mode);
    } else {
        music_control(music, 1, pit);
        music->suppress_note_off = mode == 1u;
    }
}

/* Phase 287: a digital voice's window on the FM drivers' word 0Fh (sbpro.h's voice). The MIDI drivers (rolmus, canmus,
 * wavemus, tapmus) never read that word (xeen_dat_evidence/phase287/a): nothing changes for them. */
void music_voice(Music *music, uint64_t from, uint64_t to)
{
    if (music && music->fm_sequence) {
        music->fm_library->api->voice(music->fm_sequence, from, to);
    }
}

/* XEEN.DAT's song start, 187A5(segment): 50AB1(segment, 0, 0), the mode word 0 (see music_load_mode). */
void music_load(Music *music, const Blob *song, uint64_t pit)
{
    music_load_mode(music, song, 0, pit);
}

/* Advance the borrowed sequencer and emit the song-volume controller in its defined channel
 * order. Values above the MIDI range latch music_error; null or failed sequencers are ignored.
 * The operation returns no memory or status.
 */
void music_volume(Music *music, unsigned volume, uint64_t pit)
{
    if (music && music->fm_sequence) {
        music->fm_library->api->volume(music->fm_sequence, volume, pit);
        return;
    }
    int c;
    if (!music) {
        return;
    }
    music_advance(music, pit);
    if (music->error[0]) {
        return;
    }
    /* Roland updates rhythm before the melodic channels and leaves the
     * effect channel volume separate. Preserve that message order.
     */
    if (volume > 127) {
        music_fail_sequence(music, "invalid song volume");
        return;
    }
    if (music->gs) {
        /* canmus (02F6h, 045Eh): the volume byte, then channels 10..0. */
        for (c = 10; c >= 0; c--) {
            music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, c), MIDI_CC_VOLUME, volume, 3);
        }
    } else {
        music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, MUSIC_RHYTHM_CHANNEL),
                           MIDI_CC_VOLUME, volume, 3);
        for (c = 6; c >= 0; c--) {
            music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, c), MIDI_CC_VOLUME, volume, 3);
        }
    }
}


/* Return the borrowed latched error text, empty while healthy; a null sequencer returns static
 * unavailable text. Do not free the result or retain a sequencer-owned pointer after
 * music_close.
 */
const char *music_error(const Music *music)
{
    if (music && music->fm_sequence) {
        return music->fm_library->api->error(music->fm_sequence);
    }
    return music ? music->error : "music unavailable";
}


/* Advance and stop the owned sequencer, emit its shutdown messages and release copied overlay,
 * song and sequencer storage. A null pointer is ignored. Callback context remains caller-owned
 * and must stay valid through this call.
 */
void music_close(Music *music, uint64_t pit)
{
    if (music && music->fm_sequence) {
        music->fm_library->api->sequence_close(music->fm_sequence, pit);
        sbpro_library_close(music->fm_library);
        free(music);
        return;
    }
    if (!music) {
        return;
    }
    music_advance(music, pit);
    music_stop_all_notes(music);
    if (music->gs) {
        music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, MUSIC_RHYTHM_CHANNEL),
                           MIDI_CC_ALL_NOTES_OFF, 0, 3);
    }
    music_emit_message(music, MUSIC_STATUS(MIDI_CONTROL, 7), MIDI_CC_ALL_NOTES_OFF, 0, 3);
    if (music->uart && !music->gs) {
        /* wavemus's entry 3: the MPU reset twice (049Ah). */
        music_emit_transport(music, MUSIC_EVENT_MPU_COMMAND, 0xff, 0);
        music_emit_transport(music, MUSIC_EVENT_MPU_COMMAND, 0xff, 0);
    }
    free(music->song.data);
    free(music->overlay.data);
    free(music);
}
/* Phase 308: the driver's fade word (entry 6 with FFE0h: the fade increment while a fade runs, 0 otherwise), for
 * the movie player's World ending waits (1cee4, 1e9ad); from the XEEN.EXE player's music.c. A failed sequencer
 * reports 0. */
unsigned music_fading(const Music *music)
{
    if (music && music->fm_sequence) {
        return music->fm_library->api->fading(music->fm_sequence);
    }
    return music && !music->error[0] ? music->fade : 0;
}
