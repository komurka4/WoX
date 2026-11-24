/* SPDX-License-Identifier: GPL-3.0-or-later
 */
/* Independently implement INTRO.CC/promus game commands from original CS-relative assembly.
 * See ghidra/phase50 and SBPRO_FORMAT_SPEC.md. This is a bounded data interpreter, not x86/DOS
 * execution. Hardware ports, IRQ/PIT programming and DOS services are replaced by callbacks. */
#include "sbpro_sequence.h"
#include "fm1_sequence.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define FM_TICK               UINT64_C(16390)
#define FM_OVERLAY_BYTES      9381u
#define FM_XEEN_OVERLAY_BYTES 9137u
/* promus's two entry jumps (JMP 226h, JMP 344h), the same in both builds; the effect cue table of each build (INTRO.CC
 * the FM_OVERLAY_BYTES one, XEEN.CC the FM_XEEN_OVERLAY_BYTES one). */
#define FM_ENTRY_JUMPS        "\xe9\x23\x02\xe9\x3e\x03"
#define FM_INTRO_CUE_TABLE    0xadcu
#define FM_XEEN_CUE_TABLE     0xab9u
#define FM_BATCH_LIMIT        100000u
#define FM_TICK_LIMIT         1000000u

/* OPL2 register bases (one per channel, or per operator slot) and the fields of a channel's
 * B0h:A0h frequency word: F-number in bits 0..9, block in bits 10..12 (2..4 of B0h), key-on in
 * bit 13 (bit 5 of B0h). 40h's top two bits are the key-scale level, its low six the
 * attenuation (63, the maximum, silences the operator). */
enum {
    OPL_CHARACTER = 0x20,
    OPL_LEVEL = 0x40,
    OPL_ATTACK_DECAY = 0x60,
    OPL_SUSTAIN_RELEASE = 0x80,
    OPL_FNUMBER_LOW = 0xa0,
    OPL_KEY_BLOCK = 0xb0,
    OPL_FEEDBACK = 0xc0,
    OPL_WAVEFORM = 0xe0
};
/* The registers the driver's initialization writes (2A1..343): the test register's waveform-select enable (bit 5),
 * the keyboard split (08h), the rhythm and depth (BDh), and on the second bank an OPL3's mode (05h, which a YM3812
 * ignores) and, after setting it, the OPL3's four-operator select (04h; on a YM3812 the timer control, written 0). */
enum {
    OPL_TEST = 0x01,
    OPL3_FOUR_OPERATOR = 0x04,
    OPL3_MODE = 0x05,
    OPL_KEYBOARD_SPLIT = 0x08,
    OPL_RHYTHM = 0xbd
};
#define OPL_WAVEFORM_SELECT 0x20u
#define OPL_FNUMBER        1023u
#define OPL_BLOCK_HIGH     0x1cu
#define OPL_KEY_ON         0x2000u
#define OPL_KEY_SCALE      0xc0u
#define OPL_SILENT         63u

/* The overlay's per-channel operator slots (modulator, carrier) and its note table. */
#define FM_OVERLAY_MODULATOR_SLOTS 0x1a4
#define FM_OVERLAY_CARRIER_SLOTS   0x1ad
#define FM_OVERLAY_NOTE_TABLE      0x1b6

/* An event's opcode byte: the kind in the high nibble, the channel (or a wait) in the low one.
 * Songs and effects share the decoder; where a kind differs, both readings are given. Song
 * events for channels above 6 (the effect channels 7 and 8) are skipped. */
enum {
    FM_EVENT_CALL = 0,        /* song: call the word target (FFh returns) */
    FM_EVENT_WAIT = 1,        /* the channel nibble, or when it is 0 the operand byte */
    FM_EVENT_INSTRUMENT = 2,  /* the channel's instrument record (22 effect, 26 song bytes) */
    FM_EVENT_LEVEL = 3,       /* effect: (3 or 4 = bank, level), as the song's kind 10 */
    FM_EVENT_FREQUENCY = 6,   /* effect: the note's frequency, the key unchanged */
    FM_EVENT_KEY_OFF = 7,     /* effect: clears the key-on bit */
    FM_EVENT_NOTE_OFF = 8,    /* song: key off at the remembered frequency; effect: note, no key */
    FM_EVENT_NOTE_ON = 9,
    FM_EVENT_CONTROL = 10,    /* song: (3 or 4 = bank, level); other selectors ignored */
    FM_EVENT_SYSEX = 11,      /* song: skipped to F7h */
    FM_EVENT_PROGRAM = 12,    /* load the instrument record of the operand's slot */
    FM_EVENT_BEND_OFF = 13,
    FM_EVENT_BEND_ON = 14,    /* rate, delta high, delta low */
    FM_EVENT_END = 15         /* FFh: return from a call, or restart; any other: the stream ends */
};

struct SBProSequence {
    uint8_t *overlay;
    uint8_t *song;
    size_t song_size;
    SBProEmit emit;
    void *context;
    uint64_t now, next;
    unsigned pos[2], start, wait[2], active[2];
    unsigned instruments[2][16], returns[16], depth;
    uint8_t frequency[18];
    uint8_t carrier[2][9], level[2][9];
    uint8_t bend_on[9], bend_rate[9], bend_phase[9];
    uint16_t bend_delta[9];
    uint8_t fade, fade_phase, fade_left;
    /* XEEN.DAT XEEN.CC/promus (9137 bytes) has the same decoder and service modulo
     * relocation, its cue table at AB9 and no FM mixer (26h) writes: its init writes CT1335
     * registers 2/6/4 and its volume command only stores the level (PREFLIGHT.md section 3). */
    size_t overlay_size;
    unsigned cue_table;
    int mixer_writes;
    char error[128];
    /* Phase 286: a one-chip FM driver (admus, blastmus/specmus) runs its own sequencer; every entry point below
     * dispatches to it. */
    Fm1Sequence *fm1;
    /* Phase 287: promus's word 0Fh = 2 while a voice of ibmsnd, adsnd or source plays (sbpro_voice; on until the
     * clock reaches voice_to). */
    uint8_t voice;
    uint64_t voice_to;
};
/* Phase 287: whether channel CHANNEL's event is left out for a voice (promus's `cmp bx, 7` and `cmp word [0Fh], 2`;
 * operands consumed as on the normal path): the effect note on 7D1, the level change 812 (both streams' level events,
 * banks 3 and 4: 827 / 84B), bend on 863 (both streams), the effect program 8D1, and the service's channel-7 bend
 * (AA9..AB9). promus's effect stop (489), frequency, key off and note off have no test.
 * (xeen_dat_evidence/phase287/a/lin_promus.asm) */
static int fm_voice_skips(const SBProSequence *s, unsigned channel)
{
    return s->voice && channel == 7u;
}

/** Reads the little-endian word at P of the FM driver overlay. */
static unsigned fm_word(const uint8_t *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

/** Records the sequencer's first error TEXT and stops it. */
static void fm_fail(SBProSequence *s, const char *text)
{
    if (!s->error[0]) {
        snprintf(s->error, sizeof(s->error), "%s", text);
    }
    s->active[0] = s->active[1] = s->fade = 0;
}

/** Emits one OPL register write (BANK, REG, VALUE) to the output at the current time. */
static void fm_emit(SBProSequence *s, unsigned bank, unsigned reg, unsigned value)
{
    uint8_t message[4] = {SBPRO_EVENT_OPL, (uint8_t)bank, (uint8_t)reg, (uint8_t)value};
    if (s->emit) {
        s->emit(s->context, s->now, message, 4);
    }
}

/** Returns CHANNEL's saved frequency word. */
static unsigned fm_frequency(SBProSequence *s, unsigned channel)
{
    return fm_word(s->frequency + channel * 2);
}

/** Saves a channel's frequency word VALUE at byte OFFSET of the frequency table. */
static void fm_save_frequency(SBProSequence *s, unsigned offset, unsigned value)
{
    s->frequency[offset] = (uint8_t)value;
    s->frequency[offset + 1] = (uint8_t)(value >> 8);
}

/* promus 525/538 writes low then high frequency to one physical bank. */
static void fm_write_frequency(SBProSequence *s, unsigned channel, unsigned value)
{
    for (unsigned bank = 0; bank < 2; bank++) {
        fm_emit(s, bank, OPL_FNUMBER_LOW + channel, value & 255);
        fm_emit(s, bank, OPL_KEY_BLOCK + channel, value >> 8);
    }
}

/* promus 503/514 preserves the instrument carrier's key-scale-level bits. */
static void fm_level(SBProSequence *s, unsigned bank, unsigned channel, unsigned value)
{
    unsigned op = s->overlay[FM_OVERLAY_CARRIER_SLOTS + channel];
    fm_emit(s, bank, OPL_LEVEL + op, value | (s->carrier[bank][channel] & OPL_KEY_SCALE));
}

/** Silences the song channels in the driver's order (45A..488: bank zero first, then bank one). */
static void fm_stop_song(SBProSequence *s)
{
    /* 45a..488: all channels on bank zero first, then bank one. */
    for (unsigned bank = 0; bank < 2; bank++) {
        for (int channel = 6; channel >= 0; channel--) {
            fm_save_frequency(s, (unsigned)channel * 2, 0);
            fm_emit(s, bank, OPL_FNUMBER_LOW + (unsigned)channel, 0);
            fm_emit(s, bank, OPL_KEY_BLOCK + (unsigned)channel, 0);
        }
    }
}

/** Silences the effect channels in the driver's exact bank and channel order (489..4CF). */
static void fm_stop_effect(SBProSequence *s)
{
    /* 489..4cf, including exact bank/channel order and reset carrier attenuation. */
    for (unsigned bank = 0; bank < 2; bank++) {
        for (int channel = 8; channel >= 7; channel--) {
            fm_save_frequency(s, (unsigned)channel * 2, 0);
            fm_emit(s, bank, OPL_FNUMBER_LOW + (unsigned)channel, 0);
            fm_emit(s, bank, OPL_KEY_BLOCK + (unsigned)channel, 0);
            s->level[bank][channel] = OPL_SILENT;
            fm_level(s, bank, (unsigned)channel, OPL_SILENT);
        }
    }
}

/* 67e..692: the source note's high bits select block; low five index its own table. The note's
 * bits 5..7 shifted by 5 land on the frequency word's block bits 10..12. */
static unsigned fm_note(SBProSequence *s, unsigned note)
{
    return (fm_word(s->overlay + FM_OVERLAY_NOTE_TABLE + (note & 31) * 2) +
            ((note & 0xe0) << 5)) &
           65535;
}

/* One operator's five registers on BANK from the five bytes at DATA: 20h, 40h, 60h, 80h, E0h at the overlay's slot of
 * CHANNEL's modulator or CARRIER. The carrier's level byte is remembered raw (fm_level keeps its key-scale bits); on
 * bank zero during a fade it is written with the fade's level instead of its own attenuation. */
static void fm_operator(SBProSequence *s, unsigned bank, unsigned channel, int carrier, const uint8_t *data)
{
    static const unsigned registers[5] = {OPL_CHARACTER, OPL_LEVEL, OPL_ATTACK_DECAY, OPL_SUSTAIN_RELEASE,
                                          OPL_WAVEFORM};
    unsigned op = s->overlay[(carrier ? FM_OVERLAY_CARRIER_SLOTS : FM_OVERLAY_MODULATOR_SLOTS) + channel];
    for (unsigned i = 0; i < 5; i++) {
        unsigned value = data[i];
        if (carrier && registers[i] == OPL_LEVEL) {
            s->carrier[bank][channel] = (uint8_t)value;
            if (!bank && s->fade) {
                value = (value & OPL_KEY_SCALE) | s->fade_left;
            }
        }
        fm_emit(s, bank, registers[i] + op, value);
    }
}

/* 54b/5ed: an instrument record of 22 bytes, eleven per bank (bank zero first): the modulator's five, the carrier's
 * five (fm_operator), then the channel's C0h byte with the bank's output bit (zero left 20h, one right 10h). A song's
 * record has four more bytes, for other devices, which this driver does not read. */
static void fm_instrument(SBProSequence *s, unsigned channel, const uint8_t *data)
{
    for (unsigned bank = 0; bank < 2; bank++, data += 11) {
        fm_operator(s, bank, channel, 0, data);
        fm_operator(s, bank, channel, 1, data + 5);
        fm_emit(s, bank, OPL_FEEDBACK + channel, data[10] | (bank ? 0x10u : 0x20u));
    }
}

/* The operand bytes of one song or effect event, so that a truncated event is rejected before
 * any of its arguments is read. Song kinds 4 and 5 carry two bytes the driver does not use. */
static unsigned fm_operand_bytes(unsigned kind, unsigned channel, unsigned effect)
{
    switch (kind) {
        case FM_EVENT_WAIT:
            return channel ? 0 : 1;
        case FM_EVENT_INSTRUMENT:
            return effect ? 22 : 26;
        case FM_EVENT_LEVEL:
            return effect ? 2 : 0;
        case FM_EVENT_CALL:
        case 4:
        case 5:
        case FM_EVENT_CONTROL:
            return effect ? 0 : 2;
        case FM_EVENT_NOTE_ON:
            return effect ? 1 : 2;
        case FM_EVENT_FREQUENCY:
        case FM_EVENT_NOTE_OFF:
        case FM_EVENT_PROGRAM:
            return 1;
        case FM_EVENT_BEND_ON:
            return 3;
        default:
            return 0;
    }
}

/* The song's kind 10 and the effect's kind 3 set a channel's carrier level: (3 or 4, level). */
static int fm_level_event(unsigned kind, unsigned effect)
{
    return (!effect && kind == FM_EVENT_CONTROL) || (effect && kind == FM_EVENT_LEVEL);
}

/* One fetched event: its opcode byte, kind (high nibble) and channel (low nibble), its operand count N, the position
 * AT of its operands in the stream and the first two operand bytes (0 when absent). */
typedef struct {
    unsigned op, kind, channel, n, at, a, b;
} FmEvent;

/* Fetches the event at *P of DATA (SIZE bytes) into *E and moves *P past the opcode. Returns 0 with the error set
 * when *P is outside the resource or the operands are cut short. */
static int fm_fetch(SBProSequence *s, const uint8_t *data, size_t size, unsigned *p, unsigned effect, FmEvent *e)
{
    if (*p >= size) {
        fm_fail(s, "FM sequence position outside resource");
        return 0;
    }
    e->op = data[(*p)++];
    e->kind = e->op >> 4;
    e->channel = e->op & 15;
    e->n = fm_operand_bytes(e->kind, e->channel, effect);
    if (e->n > size - *p) {
        fm_fail(s, "Truncated FM command");
        return 0;
    }
    e->at = *p;
    e->a = e->n ? data[*p] : 0;
    e->b = e->n > 1 ? data[*p + 1] : 0;
    return 1;
}

/* A song's note and program events for the effect channels (above 6) are skipped. */
static int fm_song_skips(const FmEvent *e, unsigned effect)
{
    return !effect && e->channel > 6 &&
           (e->kind == FM_EVENT_NOTE_OFF || e->kind == FM_EVENT_NOTE_ON || e->kind == FM_EVENT_PROGRAM);
}

/* An event that would address a channel past 8: a program or bend, an effect's frequency, key or note event, or a
 * level change that would apply (no fade, bank 3 or 4). The driver has no registers for it. */
static int fm_channel_refused(const SBProSequence *s, const FmEvent *e, unsigned effect)
{
    unsigned kind = e->kind;
    return e->channel > 8 &&
           (kind == FM_EVENT_PROGRAM || kind == FM_EVENT_BEND_OFF || kind == FM_EVENT_BEND_ON ||
            (effect && kind >= FM_EVENT_FREQUENCY && kind <= FM_EVENT_NOTE_ON) ||
            (fm_level_event(kind, effect) && !s->fade && (e->a == 3 || e->a == 4)));
}

/* A song's call: the return position (after the word) pushed, the word the new position; 0 when the 16 levels are
 * full. */
static int fm_call(SBProSequence *s, const uint8_t *data, unsigned *p)
{
    if (s->depth == 16) {
        fm_fail(s, "FM call stack overflow");
        return 0;
    }
    s->returns[s->depth++] = *p + 2;
    *p = fm_word(data + *p);
    return 1;
}

/* A program change: the channel takes the instrument record kept for slot A (song events of channels above 6 were
 * skipped before). 0 when the slot is past 15 or its record is missing or past the resource. */
static int fm_program(SBProSequence *s, const FmEvent *e, unsigned effect, const uint8_t *data, size_t size)
{
    if (e->a >= 16) {
        fm_fail(s, "FM instrument slot outside range");
        return 0;
    }
    if (effect || e->channel <= 6) {
        unsigned at = s->instruments[effect][e->a];
        if (!at || at > size || size - at < 22) {
            fm_fail(s, "FM instrument outside resource");
            return 0;
        }
        fm_instrument(s, e->channel, data + at);
    }
    return 1;
}

/* A level change (bank 3 or 4) remembered and written, unless a fade owns the levels. */
static void fm_level_change(SBProSequence *s, const FmEvent *e)
{
    if (!s->fade && (e->a == 3 || e->a == 4)) {
        unsigned bank = e->a == 4;
        s->level[bank][e->channel] = (uint8_t)e->b;
        fm_level(s, bank, e->channel, e->b);
    }
}

/* An effect's frequency: the note's frequency written with the key unchanged. */
static void fm_effect_frequency(SBProSequence *s, const FmEvent *e)
{
    unsigned value = fm_note(s, e->a);
    /* 722 intentionally stores at byte offset channel, not channel*2. Preserve the
     * original unaligned-frequency bookkeeping bug; do not substitute a corrected map. */
    fm_save_frequency(s, e->channel, value);
    fm_write_frequency(s, e->channel, value);
}

/* An effect's key off: the remembered frequency without its key-on bit, the block register of both chips. */
static void fm_effect_key_off(SBProSequence *s, const FmEvent *e)
{
    unsigned value = fm_frequency(s, e->channel) & (0xffffu ^ OPL_KEY_ON);
    fm_save_frequency(s, e->channel * 2, value);
    fm_emit(s, 0, OPL_KEY_BLOCK + e->channel, value >> 8);
    fm_emit(s, 1, OPL_KEY_BLOCK + e->channel, value >> 8);
}

/* A note off: an effect's note frequency (remembered) with no key; a song's remembered frequency without its key,
 * not remembered. */
static void fm_note_off(SBProSequence *s, const FmEvent *e, unsigned effect)
{
    unsigned value;
    if (effect) {
        value = fm_note(s, e->a);
        fm_save_frequency(s, e->channel * 2, value);
    } else {
        value = fm_frequency(s, e->channel) & (OPL_BLOCK_HIGH << 8 | OPL_FNUMBER);
        /* 762..774 does not update remembered frequency during song release. */
    }
    if (effect || e->channel <= 6) {
        fm_write_frequency(s, e->channel, value);
    }
}

/* A note on: the frequency written without the key, then remembered and written with it (a re-key). */
static void fm_note_on(SBProSequence *s, const FmEvent *e, unsigned effect)
{
    if (effect || e->channel <= 6) {
        unsigned value = fm_note(s, e->a);
        fm_write_frequency(s, e->channel, value);
        value |= OPL_KEY_ON;
        fm_save_frequency(s, e->channel * 2, value);
        fm_write_frequency(s, e->channel, value);
    }
}

/* A song's shared-format message: *P moves past its F7h; 0 when the resource ends first. */
static int fm_skip_sysex(SBProSequence *s, const uint8_t *data, size_t size, unsigned *p)
{
    while (*p < size && data[(*p)++] != 0xf7) {
    }
    if (*p == size && data[*p - 1] != 0xf7) {
        fm_fail(s, "Unterminated FM shared-format message");
        return 0;
    }
    return 1;
}

/* A bend on (863): rate, delta high, delta low; the phase starts at FFh. Not on the voice's channel (868..872, both
 * streams). */
static void fm_bend_on(SBProSequence *s, const FmEvent *e, const uint8_t *data)
{
    if (fm_voice_skips(s, e->channel)) {
        return;
    }
    s->bend_rate[e->channel] = (uint8_t)e->a;
    s->bend_delta[e->channel] = (uint16_t)((e->b << 8) | data[e->at + 2]);
    s->bend_phase[e->channel] = 255;
    s->bend_on[e->channel] = 1;
}

/* The position after FFh (the end event): an effect restarts at its cue; a song returns from its last call, else
 * restarts at 0. */
static unsigned fm_restart(SBProSequence *s, unsigned effect)
{
    return effect ? s->start : s->depth ? s->returns[--s->depth] : 0;
}

/* Runs a channel event of stream EFFECT. 0 when it failed (the error set). A kind the stream does not use (a song's
 * frequency or key off) and kinds 4 and 5 pass their operands over. */
static int fm_channel_event(SBProSequence *s, const FmEvent *e, unsigned effect, const uint8_t *data, size_t size)
{
    switch (e->kind) {
        case FM_EVENT_INSTRUMENT:
            s->instruments[effect][e->channel] = e->at;
            return 1;
        case FM_EVENT_LEVEL:
        case FM_EVENT_CONTROL:
            if (fm_level_event(e->kind, effect) && !fm_voice_skips(s, e->channel)) {
                fm_level_change(s, e);
            }
            return 1;
        case FM_EVENT_PROGRAM:
            if (effect && fm_voice_skips(s, e->channel)) {
                return 1; /* 8D7..8E4 */
            }
            return fm_program(s, e, effect, data, size);
        case FM_EVENT_BEND_ON:
            fm_bend_on(s, e, data);
            return 1;
        case FM_EVENT_BEND_OFF:
            s->bend_on[e->channel] = 0;
            return 1;
        case FM_EVENT_FREQUENCY:
            if (effect) {
                fm_effect_frequency(s, e);
            }
            return 1;
        case FM_EVENT_KEY_OFF:
            if (effect) {
                fm_effect_key_off(s, e);
            }
            return 1;
        case FM_EVENT_NOTE_OFF:
            fm_note_off(s, e, effect);
            return 1;
        case FM_EVENT_NOTE_ON:
            if (!(effect && fm_voice_skips(s, e->channel))) { /* 7D6..7E0 */
                fm_note_on(s, e, effect);
            }
            return 1;
        default:
            return 1;
    }
}

/* Execute one due song/effect batch. Return 1 for wait/postprocessing, 0 for immediate end.
 * Every variable-length read, branch and stack operation is bounded against owned bytes.
 * Each event is fetched (fm_fetch), a song's effect-channel note skipped and a channel past 8 refused. The flow events
 * move the position here: a song's call and message and FFh continue at their new position (an effect's call or
 * message does nothing); a wait ends the batch; another end stops the stream. Every other event acts on its channel
 * (fm_channel_event) and the position moves past its operands. */
static int fm_batch(SBProSequence *s, unsigned effect)
{
    const uint8_t *data = effect ? s->overlay : s->song;
    size_t size = effect ? s->overlay_size : s->song_size;
    unsigned p = s->pos[effect];
    for (unsigned step = 0; step < FM_BATCH_LIMIT; step++) {
        FmEvent e;
        if (!fm_fetch(s, data, size, &p, effect, &e)) {
            return 0;
        }
        if (fm_song_skips(&e, effect)) {
            p += e.n;
            continue;
        }
        if (fm_channel_refused(s, &e, effect)) {
            fm_fail(s, "FM channel outside supported range");
            return 0;
        }
        switch (e.kind) {
            case FM_EVENT_CALL:
                if (effect) {
                    break;
                }
                if (!fm_call(s, data, &p)) {
                    return 0;
                }
                continue;
            case FM_EVENT_WAIT:
                s->wait[effect] = e.channel ? e.channel : e.a;
                s->pos[effect] = p + e.n;
                return 1;
            case FM_EVENT_SYSEX:
                if (effect) {
                    break;
                }
                if (!fm_skip_sysex(s, data, size, &p)) {
                    return 0;
                }
                continue;
            case FM_EVENT_END:
                if (e.op == 255) {
                    p = fm_restart(s, effect);
                    continue;
                }
                s->active[effect] = 0;
                return 0;
            default:
                if (!fm_channel_event(s, &e, effect, data, size)) {
                    return 0;
                }
                break;
        }
        p += e.n;
    }
    fm_fail(s, "FM event budget exhausted");
    return 0;
}

/* One tick of a song fade (9cc..): the fade rate accumulates in an 8-bit phase; each carry is one step. A step past
 * the last stops the song; any other step makes every sounding song channel one level quieter on both banks
 * (channels 6..0). The original increments its remembered attenuation but writes the previous byte: preserve its
 * initial redundant write. */
static void fm_fade_tick(SBProSequence *s)
{
    unsigned sum = s->fade_phase + s->fade;
    s->fade_phase = (uint8_t)sum;
    if (sum <= 255) {
        return;
    }
    s->fade_left--;
    if ((int8_t)s->fade_left < 0) {
        s->active[0] = s->fade = 0;
        fm_stop_song(s);
        return;
    }
    for (int channel = 6; channel >= 0; channel--) {
        for (unsigned bank = 0; bank < 2; bank++) {
            if (s->level[bank][channel] != OPL_SILENT) {
                unsigned old = s->level[bank][channel]++;
                fm_level(s, bank, (unsigned)channel, old);
            }
        }
    }
}

/* One tick of effect CHANNEL's pitch bend (..abe): the rate accumulates in an 8-bit phase; each carry adds the
 * signed delta to the remembered F-number. Rising to 2DEh or more halves it and raises the block (4 in the block
 * bits); falling to 184h or less doubles it and lowers the block. An F-number of 0 after the halving becomes 1, and
 * after the doubling one less (all ten bits set). The key bit is kept; the word is remembered and written. */
static void fm_bend_tick(SBProSequence *s, unsigned channel)
{
    unsigned sum = s->bend_phase[channel] + s->bend_rate[channel];
    s->bend_phase[channel] = (uint8_t)sum;
    if (sum <= 255) {
        return;
    }
    unsigned value = fm_frequency(s, channel), block = (value >> 8) & OPL_BLOCK_HIGH;
    unsigned key = value & OPL_KEY_ON;
    int delta = (int16_t)s->bend_delta[channel];
    unsigned frequency = ((value & OPL_FNUMBER) + delta) & 65535;
    if (delta >= 0 && (int16_t)frequency >= 0x2de) {
        frequency >>= 1;
        if (!(frequency & OPL_FNUMBER)) {
            frequency++;
        }
        block = (block + 4) & OPL_BLOCK_HIGH;
    } else if (delta < 0 && (int16_t)frequency <= 0x184) {
        frequency = frequency << 1 & 65535;
        if (!(frequency & OPL_FNUMBER)) {
            frequency--;
        }
        block = (block - 4) & OPL_BLOCK_HIGH;
    }
    value = (frequency & OPL_FNUMBER) | block << 8 | key;
    fm_save_frequency(s, channel * 2, value);
    fm_write_frequency(s, channel, value);
}

/* Source 9cc..abe: after a tick's batches, the song's fade (fm_fade_tick), then the bends of effect channels 8 and 7
 * (fm_bend_tick), each only while it is on. */
static void fm_post(SBProSequence *s)
{
    if (s->fade) {
        fm_fade_tick(s);
    }
    for (int channel = 8; channel >= 7; channel--) {
        if (s->bend_on[channel] && !fm_voice_skips(s, (unsigned)channel)) {
            fm_bend_tick(s, (unsigned)channel);
        }
    }
}

/* The stream a tick runs: the song when its wait runs out, else the effect when its does (so the effect's wait counts
 * down only on a tick the song does not take); -1 when neither is due. */
static int fm_due_stream(SBProSequence *s)
{
    for (unsigned i = 0; i < 2; i++) {
        if (s->active[i] && (!s->wait[i] || !--s->wait[i])) {
            return (int)i;
        }
    }
    return -1;
}

/* One timer tick, at the next tick's time: a voice whose window has closed ends first (its last interrupt stored 0);
 * then the due stream's batch, and the service (fm_post) after a batch that ended in a wait or when no stream was
 * due. */
static void fm_tick(SBProSequence *s)
{
    if (s->voice && s->next >= s->voice_to) {
        s->voice = 0;
    }
    s->now = s->next;
    s->next += FM_TICK;
    int effect = fm_due_stream(s);
    if (effect < 0 || fm_batch(s, (unsigned)effect)) {
        fm_post(s);
    }
}

/** Runs the FM driver's timer ticks up to PIT, emitting their register writes. */
void sbpro_advance(SBProSequence *s, uint64_t pit)
{
    if (s && s->fm1) {
        fm1_advance(s->fm1, pit);
        return;
    }
    if (!s || s->error[0]) {
        return;
    }
    if (pit < s->now || pit > UINT64_MAX - FM_TICK ||
        (pit >= s->next && (pit - s->next) / FM_TICK >= FM_TICK_LIMIT)) {
        fm_fail(s, "Invalid or excessive FM clock advance");
        return;
    }
    while (s->next <= pit && !s->error[0]) {
        fm_tick(s);
    }
    s->now = pit;
    if (s->voice && pit >= s->voice_to) {
        s->voice = 0;
    }
}

/* Phase 287: a voice of ibmsnd, adsnd or source from FROM (the word 0Fh = 2) to TO (0); TO <= FROM ends it. */
void sbpro_voice(SBProSequence *s, uint64_t from, uint64_t to)
{
    if (s && s->fm1) {
        fm1_voice(s->fm1, from, to);
        return;
    }
    if (!s) {
        return;
    }
    sbpro_advance(s, from);
    if (s->error[0]) {
        return;
    }
    s->voice = to > from;
    s->voice_to = to;
}

/**
 * Creates an FM sequencer from the music driver OVERLAY (checked by size and entry jumps); its
 * register writes go to EMIT from time ORIGIN. Returns NULL for any other file.
 */
SBProSequence *sbpro_sequence_open_flags(const uint8_t *overlay, size_t size, unsigned flags, SBProEmit emit,
                                         void *ctx, uint64_t origin)
{
    if (overlay && (size == FM1_BLASTMUS_BYTES || size == FM1_ADMUS_BYTES)) {
        SBProSequence *s = calloc(1, sizeof(*s));
        if (s && !(s->fm1 = fm1_open(overlay, size, flags, emit, ctx, origin))) {
            free(s);
            s = NULL;
        }
        return s;
    }
    if (!overlay || (size != FM_OVERLAY_BYTES && size != FM_XEEN_OVERLAY_BYTES) ||
        memcmp(overlay, FM_ENTRY_JUMPS, sizeof FM_ENTRY_JUMPS - 1u) || origin > UINT64_MAX - FM_TICK) {
        return NULL;
    }
    SBProSequence *s = calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->overlay = malloc(size);
    if (!s->overlay) {
        free(s);
        return NULL;
    }
    memcpy(s->overlay, overlay, size);
    s->overlay_size = size;
    s->cue_table = size == FM_OVERLAY_BYTES ? FM_INTRO_CUE_TABLE : FM_XEEN_CUE_TABLE;
    s->mixer_writes = size == FM_OVERLAY_BYTES; /* the song volume's 26h write (XEEN.CC's build stores it only) */
    s->emit = emit;
    s->context = ctx;
    s->now = origin;
    s->next = origin + FM_TICK;
    /* INTRO.CC promus's init (0239h..0265h), when its second argument (XEEN.DAT's XEEN.CFG word 8D4C, 1 in the
     * genuine file) is nonzero: mixer line 2Eh, master 22h and FM 26h = FFh, voice 04h = 99h, before the OPL set-up
     * (Phase 285A, the driver in Unicorn). The renderer acts on the FM volume only. */
    static const uint8_t init_mixer[4][2] = {{0x2e, 0xff}, {0x22, 0xff}, {SBPRO_MIXER_FM_VOLUME, 0xff}, {0x04, 0x99}};
    for (unsigned i = 0; s->emit && s->mixer_writes && (flags & SBPRO_FLAG_CONFIG_8D4C) && i < 4u; ++i) {
        const uint8_t mixer[4] = {SBPRO_EVENT_MIXER, 0, init_mixer[i][0], init_mixer[i][1]};
        s->emit(s->context, s->now, mixer, 4);
    }
    /* 2a1..343: raw register requests. Two actual YM3812 engines ignore OPL3-only 05/C0 bits. */
    fm_emit(s, 1, OPL3_MODE, 0);
    fm_emit(s, 0, OPL_TEST, OPL_WAVEFORM_SELECT);
    fm_emit(s, 1, OPL_TEST, OPL_WAVEFORM_SELECT);
    fm_emit(s, 0, OPL_KEYBOARD_SPLIT, 0);
    fm_emit(s, 1, OPL_KEYBOARD_SPLIT, 0);
    fm_emit(s, 0, OPL_RHYTHM, 0);
    fm_emit(s, 1, OPL_RHYTHM, 0);
    fm_emit(s, 1, OPL3_MODE, 1);
    for (unsigned bank = 0; bank < 2; bank++) {
        for (unsigned ch = 0; ch < 9; ch++) {
            fm_emit(s, bank, OPL_FEEDBACK + ch, bank ? 0x10 : 0x20);
        }
    }
    fm_emit(s, 1, OPL3_FOUR_OPERATOR, 0);
    return s;
}

/** sequence_open_flags with XEEN.CFG's 8D4C nonzero (the genuine file's 1). */
SBProSequence *sbpro_sequence_open(const uint8_t *overlay, size_t size, SBProEmit emit, void *ctx, uint64_t origin)
{
    return sbpro_sequence_open_flags(overlay, size, SBPRO_FLAG_CONFIG_8D4C, emit, ctx, origin);
}

/* The effect entry's refusals, as xeen/audio.h's XEEN_MUSIC_CUE_LAST and XEEN_MUSIC_EFFECT_FIRST/_END (this backend
 * builds without include/): cues above 180, and 7..10 while an effect plays. */
#define SBPRO_CUE_LAST     180u
#define SBPRO_EFFECT_FIRST 7u
#define SBPRO_EFFECT_END   11u
/**
 * Runs driver COMMAND (an effect cue) at time PIT: through the FM1 sequence when one is loaded; else, after the
 * stream advances to PIT and unless refused (a latched error, a cue past 180, or 7..10 while an effect plays), the
 * cue table's word for COMMAND becomes the effect channel's start (an entry outside the overlay latches an error):
 * its wait cleared, the bends of channels 7 and 8 off, the playing effect stopped, the effect active.
 */
void sbpro_command(SBProSequence *s, unsigned command, uint64_t pit)
{
    if (s && s->fm1) {
        fm1_command(s->fm1, command, pit);
        return;
    }
    if (!s) {
        return;
    }
    sbpro_advance(s, pit);
    if (s->error[0] || command > SBPRO_CUE_LAST ||
        (s->active[1] && command >= SBPRO_EFFECT_FIRST && command < SBPRO_EFFECT_END)) {
        return;
    }
    unsigned at = fm_word(s->overlay + s->cue_table + command * 2);
    if (at >= s->overlay_size) {
        fm_fail(s, "FM effect outside overlay");
        return;
    }
    s->pos[1] = s->start = at;
    s->wait[1] = 0;
    s->bend_on[7] = s->bend_on[8] = 0;
    fm_stop_effect(s);
    s->active[1] = 1;
}

/* A song fade's steps: one attenuation level each, from the loudest to OPL_SILENT; the step after the last stops the
 * song (fm_fade_tick). */
#define FM_FADE_STEPS 63u
/**
 * Runs the driver's song CONTROL at time PIT, after the driver has run up to it: 0 stops the song; 1 stops it and
 * rewinds it to its start, playing again; 2..255 starts a fade at that rate (fm_fade_tick: the rate accumulates in an
 * 8-bit phase per tick, each carry one step of FM_FADE_STEPS). A larger CONTROL, or a driver that has failed, changes
 * nothing. The XEEN.DAT build (fm1) has its own controls (fm1_control).
 */
void sbpro_control(SBProSequence *s, unsigned control, uint64_t pit)
{
    if (s && s->fm1) {
        fm1_control(s->fm1, control, pit);
        return;
    }
    if (!s) {
        return;
    }
    sbpro_advance(s, pit);
    if (s->error[0]) {
        return;
    }
    if (!control) {
        s->active[0] = s->fade = 0;
        fm_stop_song(s);
    } else if (control == 1) {
        s->pos[0] = s->depth = s->wait[0] = s->fade = 0;
        s->active[0] = 1;
        fm_stop_song(s);
    } else if (control < 256) {
        s->fade = (uint8_t)control;
        s->fade_left = FM_FADE_STEPS;
    }
}

/** Loads and starts SONG DATA at time PIT. */
void sbpro_song(SBProSequence *s, const uint8_t *data, size_t size, uint64_t pit)
{
    if (s && s->fm1) {
        fm1_song(s->fm1, data, size, pit);
        return;
    }
    if (!s) {
        return;
    }
    sbpro_advance(s, pit);
    if (s->error[0]) {
        return;
    }
    if (!size) {
        sbpro_control(s, 0, pit);
        return;
    }
    if (!data || size > 65535) {
        fm_fail(s, "Invalid FM song size");
        return;
    }
    uint8_t *copy = malloc(size);
    if (!copy) {
        fm_fail(s, "FM song allocation failed");
        return;
    }
    memcpy(copy, data, size);
    free(s->song);
    s->song = copy;
    s->song_size = size;
    sbpro_control(s, 1, pit);
}

/**
 * Sets the music VOLUME (0..127) at time PIT, after the driver has run up to it. The INTRO.CC build (mixer_writes)
 * writes it to the SB Pro mixer's FM volume register: VOLUME's high nibble plus 60h (modulo 100h) is the left level
 * and is copied into the low nibble for the right; the XEEN.CC build keeps no mixer write. A VOLUME past 127 latches
 * an error. The one-chip builds have their own volume (fm1_volume).
 */
void sbpro_volume(SBProSequence *s, unsigned volume, uint64_t pit)
{
    if (s && s->fm1) {
        fm1_volume(s->fm1, volume, pit);
        return;
    }
    if (!s) {
        return;
    }
    sbpro_advance(s, pit);
    if (s->error[0]) {
        return;
    }
    if (volume > 127) {
        fm_fail(s, "Invalid FM song volume");
        return;
    }
    unsigned high = ((volume & 0xf0) + 0x60) & 255; /* the left level in the high nibble */
    uint8_t event[4] = {SBPRO_EVENT_MIXER, 0, SBPRO_MIXER_FM_VOLUME, (uint8_t)(high + (high >> 4))};
    if (s->emit && s->mixer_writes) {
        s->emit(s->context, s->now, event, 4);
    }
}

/** Returns the driver's fade state (0 when not fading). */
unsigned sbpro_fading(const SBProSequence *s)
{
    if (s && s->fm1) {
        return fm1_fading(s->fm1);
    }
    return s ? s->fade : 0;
}

/** Returns the sequencer's error text (empty when it has none). */
const char *sbpro_error(const SBProSequence *s)
{
    if (s && s->fm1) {
        return fm1_error(s->fm1);
    }
    return s ? s->error : "FM sequencer unavailable";
}

/** Silences the driver at time PIT (the song and the effect stopped, OPL3 mode cleared: register set 1's 05h; a
 * one-chip build closes through fm1_close) and frees the sequencer. */
void sbpro_sequence_close(SBProSequence *s, uint64_t pit)
{
    if (!s) {
        return;
    }
    if (s->fm1) {
        fm1_close(s->fm1, pit);
        free(s);
        return;
    }
    sbpro_advance(s, pit);
    fm_stop_song(s);
    fm_stop_effect(s);
    fm_emit(s, 1, OPL3_MODE, 0);
    free(s->song);
    free(s->overlay);
    free(s);
}
