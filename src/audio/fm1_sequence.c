/* SPDX-License-Identifier: GPL-3.0-or-later
 */
/* Phase 286: the one-chip FM music drivers of INTRO.CC, implemented from their code
 * (xeen_dat_evidence/phase286/a/blastmus_full.asm, admus_full.asm): blastmus (XEEN.CFG music 2, Sound Blaster),
 * specmus (6, Pro-Audio Spectrum: byte-identical to blastmus in INTRO.CC) and admus (1, AdLib). One YM3812, nine
 * channels: the song plays 0..6, the effects 7 and 8. A bounded data interpreter, not x86 execution: the OPL and mixer
 * port writes become four-byte events (sbpro.h), the PIT and IRQ0 set-up become the 16390-cycle tick.
 *
 * The two builds share their data layout (the song volume at 187h, the operator slots at 188h/191h, the note table at
 * 19Ah) and decoder; they differ in the cue table (blastmus 94Fh, admus 975h), the song's level selector (5, 2),
 * the OPL port (admus takes XEEN.CFG's
 * music port, blastmus always 388h: one chip either way) and admus's pitch-sweep counter (below). Both write the SB Pro
 * mixer at init when XEEN.CFG's 8D4C is nonzero (flags bit 0), as promus does.
 *
 * Their word at 0Fh is 0 in both images (another value is refused at open). Phase 287: it is not the drivers' own: the
 * digital drivers ibmsnd, adsnd and source store 2 in it at a voice's start and 0 at its end or stop
 * (xeen_dat_evidence/phase287/a/lin_{ibmsnd,adsnd,source}.asm), and the effect paths then leave channel 7 alone
 * (fm1_voice; the sites at fm1_voice_skips). admus tests it against 0 at every site, blastmus against 0 at three and 2 at
 * five; the voice drivers write only 0 and 2, so one switch stands for both. */
#include "fm1_sequence.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FM1_TICK       UINT64_C(16390)
#define FM1_BATCH      100000u
#define FM1_TICK_LIMIT 1000000u
#define FM1_STACK      16u /* the call stacks hold 20h bytes: sixteen return words */
/* The driver's data: the song volume byte, the modulator and carrier operator slots per channel, the note table. */
#define FM1_VOLUME    0x187u
#define FM1_MODULATOR 0x188u
#define FM1_CARRIER   0x191u
#define FM1_NOTES     0x19au
#define FM1_VOICE_WORD 0x0fu /* the voice drivers' word (the file header): 0 in both images */
/* admus's pitch-sweep counter (its word 15Eh): cleared by every bend start, incremented by every tick that reaches the
 * service; past 400 the service clears the words 11Dh and 11Fh and skips the bends. Those words are not the bends'
 * switches (10Fh + channel): they lie in the song's instrument-record table (11Ch + 2 x channel), so the driver zeroes
 * the high byte of channel 0's record pointer, channel 1's whole pointer and the low byte of channel 2's, every tick,
 * until the next bend start. A later program change on those channels reads its instrument from the corrupted offset.
 * An original defect (the record table is kept as bytes to reproduce it); the bends themselves are not stopped. */
#define FM1_SWEEP_TICKS 400u

enum { OPL_LEVEL = 0x40, OPL_FNUMBER_LOW = 0xa0, OPL_KEY_BLOCK = 0xb0, OPL_FEEDBACK = 0xc0 };
#define FM1_KEY_ON 0x2000u
/* The word's block and F-number (its bits 0..12): the frequency without the key-on bit. */
#define FM1_BLOCK_FNUM 0x1fffu

struct Fm1Sequence {
    uint8_t *overlay;
    size_t overlay_size;
    uint8_t *song;
    size_t song_size;
    Fm1Emit emit;
    void *context;
    uint64_t now, next;
    unsigned cue_table;
    unsigned level_selector; /* the song control's level selector: blastmus 5 (67Ah), admus 2 (67Ch) */
    int sweep_bug; /* admus */
    /* Phase 287: admus's effect program tests the word 0Fh (734..743); blastmus's test is dead (below). */
    int program_guard;
    /* Streams: 0 the song, 1 the effect. */
    unsigned active[2], pos[2], wait[2], depth[2], start;
    unsigned returns[2][FM1_STACK];
    uint8_t song_records[32]; /* admus 11Ch / blastmus 11Eh: sixteen little-endian words, kept as bytes */
    unsigned effect_records[16];
    unsigned freq[9];
    uint8_t level[9], carrier[9];
    uint8_t fade, fade_phase, fade_left, volume;
    uint8_t bend_on[9], bend_rate[9], bend_phase[9];
    uint16_t bend_delta[9];
    unsigned sweep;
    /* Phase 287: the music word 0Fh = 2 while a voice of ibmsnd, adsnd or source plays (fm1_voice): on until the
     * sequencer's clock reaches voice_to. */
    uint8_t voice;
    uint64_t voice_to;
    char error[128];
};
/* A little-endian word at P (this unit is built standalone into the synth library, without xeen/byte_order.h). */
static unsigned fm1_le16(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}
/* Phase 287: whether channel CH's write at this point is left out for the voice (the original's
 * `cmp word [0Fh], 0/2` with `cmp bx, 7`; every site's operands are consumed as on its normal path). The sites
 * (blastmus/admus): the effect stop 3D1 (channel 7's frequency and level), the effect frequency 588/58A, effect note
 * off 5EB/5ED, effect note on 643/645, effect level 69D/69F, bend on 6C3/6C5 (both streams; admus clears its sweep
 * counter before the test), effect program (admus 734 only), and the service's channel-7 bend 91C/942. The effect
 * key off (5AF/5B1) has no test: it writes channel 7's block register during a voice too.
 * blastmus's program test is an original defect: 732 `cmp word [0Fh], 2`, then 737 `and ax, 0Fh` before 73A `jne`,
 * so the branch reads the AND's result (channel not 0) and channel 7's program is always loaded (reproduced). */
static int fm1_voice_skips(const Fm1Sequence *s, unsigned ch)
{
    return s->voice && ch == 7u;
}

static void fm1_fail(Fm1Sequence *s, const char *text)
{
    if (!s->error[0]) {
        snprintf(s->error, sizeof s->error, "%s", text);
    }
    s->active[0] = s->active[1] = s->fade = 0;
}
static void fm1_out(Fm1Sequence *s, uint8_t kind, unsigned reg, unsigned value)
{
    uint8_t m[4] = {kind, 0, (uint8_t)reg, (uint8_t)value};
    if (s->emit) {
        s->emit(s->context, s->now, m, 4);
    }
}
/* 405: one OPL register write. */
static void fm1_opl(Fm1Sequence *s, unsigned reg, unsigned value)
{
    fm1_out(s, FM1_EVENT_OPL, reg & 255u, value & 255u);
}
/* 42E: the channel's frequency word, low byte then the key/block byte. */
static void fm1_write_frequency(Fm1Sequence *s, unsigned ch, unsigned value)
{
    fm1_opl(s, OPL_FNUMBER_LOW + ch, value & 255u);
    fm1_opl(s, OPL_KEY_BLOCK + ch, (value >> 8) & 255u);
}
/* 41D: the carrier's level byte with the instrument carrier's key-scale bits. */
static void fm1_write_level(Fm1Sequence *s, unsigned ch, unsigned value)
{
    fm1_opl(s, OPL_LEVEL + s->overlay[FM1_CARRIER + ch], (value | (s->carrier[ch] & 0xc0u)) & 255u);
}
/* 3BA: song channels 6..0 silenced (frequency 0). */
static void fm1_stop_song(Fm1Sequence *s)
{
    for (int ch = 6; ch >= 0; ch--) {
        s->freq[ch] = 0;
        fm1_write_frequency(s, (unsigned)ch, 0);
    }
}
/* 3D1: the effect channels 7 then 8: frequency 0, level 3Fh. */
static void fm1_stop_effect(Fm1Sequence *s)
{
    for (unsigned ch = 7; ch <= 8; ch++) {
        if (fm1_voice_skips(s, ch)) {
            continue; /* 3D3..3D8: channel 7 left alone during a voice */
        }
        s->freq[ch] = 0;
        fm1_write_frequency(s, ch, 0);
        s->level[ch] = 0x3f;
        fm1_write_level(s, ch, 0x3f);
    }
}
/* 4F2: the note byte's low five bits index the note table; its top three are added at bit 10 (the block). */
static unsigned fm1_note(const Fm1Sequence *s, unsigned note)
{
    const uint8_t *t = s->overlay + FM1_NOTES + (note & 31u) * 2u;
    return (fm1_le16(t) + ((note & 0xe0u) << 5)) & 0xffffu;
}
/* 441: an instrument record (11 bytes at AT of DATA): modulator 20h/40h/60h/80h/E0h, carrier 20h, the carrier's
 * level (kept raw for 41D; written raised by (7Fh - the song volume) / 2, then above 3Fh clamped to 3Fh, or during a
 * fade to the fade's level, the key-scale bits dropped), 60h/80h/E0h, then the channel's C0h byte. 8-bit arithmetic
 * as the driver's. */
static int fm1_instrument(Fm1Sequence *s, unsigned ch, const uint8_t *data, size_t size, unsigned at)
{
    static const uint8_t regs[5] = {0x20, 0x40, 0x60, 0x80, 0xe0};
    unsigned mod = s->overlay[FM1_MODULATOR + ch], car = s->overlay[FM1_CARRIER + ch];
    if (at > size || size - at < 11u) {
        fm1_fail(s, "FM instrument outside resource");
        return 0;
    }
    data += at;
    for (unsigned i = 0; i < 5u; i++) {
        fm1_opl(s, regs[i] + mod, data[i]);
    }
    fm1_opl(s, 0x20u + car, data[5]);
    {
        uint8_t raw = data[6], add = (uint8_t)((uint8_t)(0x7fu - s->volume) >> 1), level = (uint8_t)(raw + add);
        s->carrier[ch] = raw;
        if (level > 0x3fu) {
            level = s->fade ? s->fade_left : 0x3fu;
        }
        fm1_opl(s, 0x40u + car, level);
    }
    for (unsigned i = 2; i < 5u; i++) {
        fm1_opl(s, regs[i] + car, data[5 + i]);
    }
    fm1_opl(s, OPL_FEEDBACK + ch, data[10]);
    return 1;
}
/* Song record SLOT (0..15): the stream offset the driver keeps for it (admus 11Ch / blastmus 11Eh, set by the
 * record command), a little-endian word. */
static unsigned fm1_song_record(const Fm1Sequence *s, unsigned slot)
{
    return fm1_le16(s->song_records + slot * 2u);
}
/* An event's opcode byte: the kind in the high nibble, the channel (or a wait) in the low one. Both streams share the
 * decoder (the song's handlers at 77E.., the effect's at 7A1..); where a kind differs, both readings are given.
 * The flow events (call, wait, message, end) move the stream; the others act on a channel. */
enum {
    FM1_EVENT_CALL = 0,        /* call the word target; ignored, the word NOT consumed, when sixteen returns are held */
    FM1_EVENT_WAIT = 1,        /* the channel nibble, or when it is 0 the operand byte */
    FM1_EVENT_INSTRUMENT = 2,  /* the channel's instrument record (26 song bytes, 11 effect bytes) */
    FM1_EVENT_LEVEL = 3,       /* effect: the channel's level; song: nothing */
    FM1_EVENT_FREQUENCY = 6,   /* effect: the note's frequency, the key unchanged; song: one byte passed over */
    FM1_EVENT_KEY_OFF = 7,     /* effect: the key-on bit cleared; song: nothing */
    FM1_EVENT_NOTE_OFF = 8,
    FM1_EVENT_NOTE_ON = 9,
    FM1_EVENT_CONTROL = 10,    /* song: (selector, level); effect: nothing */
    FM1_EVENT_MESSAGE = 11,    /* song: passed over to F7h; effect: nothing */
    FM1_EVENT_PROGRAM = 12,    /* the instrument record of the operand's slot */
    FM1_EVENT_BEND_OFF = 13,
    FM1_EVENT_BEND_ON = 14,    /* rate, delta high, delta low */
    FM1_EVENT_END = 15         /* FFh: return from a call, else restart; any other: the stream ends */
};
/* One fetched event: its opcode byte, kind and channel, its operand count N, the position AT of its operands in the
 * stream and the first operand byte A (0 when it has none). */
typedef struct {
    unsigned op, kind, ch, n, at, a;
} Fm1Event;
/* Operand bytes of kind K on stream EFFECT (the handlers' own reads; the program-set records are passed over). */
static unsigned fm1_operands(unsigned kind, unsigned ch, unsigned effect)
{
    static const uint8_t song[16] = {2, 0, 26, 0, 2, 2, 1, 0, 1, 2, 2, 0, 1, 0, 3, 0};
    static const uint8_t effect_n[16] = {2, 0, 11, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 0, 3, 0};
    if (kind == FM1_EVENT_WAIT) {
        return ch ? 0u : 1u;
    }
    return effect ? effect_n[kind] : song[kind];
}
/* A channel past 8 would address the driver's other variables: refused (no shipped stream does it). */
static int fm1_channel_ok(Fm1Sequence *s, unsigned ch)
{
    if (ch > 8u) {
        fm1_fail(s, "FM channel outside supported range");
        return 0;
    }
    return 1;
}
/* Fade (856..891): the rate accumulates in an 8-bit phase; on each carry the remaining steps fall; past the last the
 * song stops (no channel written but the stop's), else every song channel 6..0 whose level is at most 3Eh is raised
 * by one and its PREVIOUS level written (the driver passes the byte read before the increment). */
static void fm1_fade(Fm1Sequence *s)
{
    unsigned sum;
    if (!s->fade) {
        return;
    }
    sum = s->fade_phase + s->fade;
    s->fade_phase = (uint8_t)sum;
    if (sum <= 255u) {
        return;
    }
    s->fade_left--;
    if ((int8_t)s->fade_left < 0) {
        s->active[0] = 0;
        s->fade = 0;
        fm1_stop_song(s);
        return;
    }
    for (int ch = 6; ch >= 0; ch--) {
        if (s->level[ch] <= 0x3eu) {
            unsigned old = s->level[ch]++;
            fm1_write_level(s, (unsigned)ch, old);
        }
    }
}
/* One bend step of CH (89F..917), as promus's: the signed delta added to the F-number; at or above 2DEh halved and
 * the block raised, at or below 184h doubled and the block lowered (an F-number of 0 after the step: +1 / -1); the
 * key bit kept; remembered and written. */
static void fm1_bend(Fm1Sequence *s, unsigned ch)
{
    unsigned sum = s->bend_phase[ch] + s->bend_rate[ch];
    s->bend_phase[ch] = (uint8_t)sum;
    if (sum <= 255u) {
        return;
    }
    unsigned value = s->freq[ch], block = (value >> 8) & 0x1cu, key = (value >> 8) & 0x20u;
    int16_t delta = (int16_t)s->bend_delta[ch];
    unsigned f = ((value & 0x3ffu) + (unsigned)(uint16_t)delta) & 0xffffu;
    if (delta >= 0) {
        if ((int16_t)f >= 0x2de) {
            f >>= 1;
            if (!(f & 0x3ffu)) {
                f++;
            }
            block = (block + 4u) & 0x1cu;
        }
    } else if ((int16_t)f <= 0x184) {
        f = (f << 1) & 0xffffu;
        if (!(f & 0x3ffu)) {
            f = (f - 1u) & 0xffffu;
        }
        block = (block - 4u) & 0x1cu;
    }
    value = (f & 0x3ffu) | (block | key) << 8;
    s->freq[ch] = value;
    fm1_write_frequency(s, ch, value);
}
/* The tick's service after a batch that ended in a wait, or when no stream was due (856..): the fade, admus's sweep
 * counter, then the bends of channels 8 and 7. */
static void fm1_post(Fm1Sequence *s)
{
    fm1_fade(s);
    if (s->sweep_bug) {
        s->sweep = (s->sweep + 1u) & 0xffffu;
        if (s->sweep > FM1_SWEEP_TICKS) {
            memset(s->song_records + 1, 0, 4); /* the words 11Dh and 11Fh */
            return;
        }
    }
    for (int ch = 8; ch >= 7; ch--) {
        if (s->bend_on[ch] && !fm1_voice_skips(s, (unsigned)ch)) {
            fm1_bend(s, (unsigned)ch);
        }
    }
}
/* Fetches the event at *P of DATA (SIZE bytes) into *E and moves *P past the opcode. Returns 0 with the error set when
 * *P is outside the resource or the operands are cut short (the message's bytes are bounded by its own scan). */
static int fm1_fetch(Fm1Sequence *s, const uint8_t *data, size_t size, unsigned *p, unsigned effect, Fm1Event *e)
{
    if (*p >= size) {
        fm1_fail(s, "FM sequence position outside resource");
        return 0;
    }
    e->op = data[(*p)++];
    e->kind = e->op >> 4;
    e->ch = e->op & 15u;
    e->n = fm1_operands(e->kind, e->ch, effect);
    if (e->n > size - *p) {
        fm1_fail(s, "Truncated FM command");
        return 0;
    }
    e->at = *p;
    e->a = e->n ? data[*p] : 0u;
    return 1;
}
/* A call (77E / 7C4), on both streams: the return position (after the word) pushed and the word the new position;
 * with sixteen returns held it is ignored and *P stays on the word, whose bytes are then read as events. */
static void fm1_call(Fm1Sequence *s, unsigned effect, const uint8_t *data, unsigned *p)
{
    if (s->depth[effect] < FM1_STACK) {
        s->returns[effect][s->depth[effect]++] = *p + 2u;
        *p = fm1_le16(data + *p);
    }
}
/* The position after FFh (the end event): the last return of a call, else the restart (the song at 0, the effect at
 * its cue). */
static unsigned fm1_restart(Fm1Sequence *s, unsigned effect)
{
    return s->depth[effect] ? s->returns[effect][--s->depth[effect]] : effect ? s->start : 0u;
}
/* A song's shared-format message (560): *P moves past its F7h; 0 when the resource ends first. */
static int fm1_skip_message(Fm1Sequence *s, const uint8_t *data, size_t size, unsigned *p)
{
    while (*p < size && data[(*p)++] != 0xf7u) {
    }
    if (data[*p - 1u] != 0xf7u) {
        fm1_fail(s, "Unterminated FM shared-format message");
        return 0;
    }
    return 1;
}
/* The channel's instrument record kept at the operands' position (7F9 / 7B3); a program change reads it. The song's
 * table is kept as bytes (admus's sweep defect overwrites part of it: FM1_SWEEP_TICKS). */
static void fm1_keep_record(Fm1Sequence *s, const Fm1Event *e, unsigned effect)
{
    if (effect) {
        s->effect_records[e->ch] = e->at;
    } else {
        s->song_records[e->ch * 2u] = (uint8_t)e->at;
        s->song_records[e->ch * 2u + 1u] = (uint8_t)(e->at >> 8);
    }
}
/* Effect level (69D): the channel's level remembered and written, unless a fade owns the levels or the channel is the
 * voice's. A song's kind 3 does nothing. */
static int fm1_effect_level(Fm1Sequence *s, const Fm1Event *e, unsigned effect)
{
    if (!effect || s->fade || fm1_voice_skips(s, e->ch)) {
        return 1;
    }
    if (!fm1_channel_ok(s, e->ch)) {
        return 0;
    }
    s->level[e->ch] = (uint8_t)e->a;
    fm1_write_level(s, e->ch, e->a);
    return 1;
}
/* Effect frequency (588): the note's frequency remembered and written, the key unchanged; not on the voice's channel.
 * A song's kind 6 passes its byte over. */
static int fm1_effect_frequency(Fm1Sequence *s, const Fm1Event *e, unsigned effect)
{
    if (!effect || fm1_voice_skips(s, e->ch)) {
        return 1;
    }
    if (!fm1_channel_ok(s, e->ch)) {
        return 0;
    }
    s->freq[e->ch] = fm1_note(s, e->a);
    fm1_write_frequency(s, e->ch, s->freq[e->ch]);
    return 1;
}
/* Effect key off (5AF): the remembered frequency's key-on bit cleared, the block register only. No voice test: it
 * writes channel 7 during a voice too. A song's kind 7 does nothing. */
static int fm1_effect_key_off(Fm1Sequence *s, const Fm1Event *e, unsigned effect)
{
    if (!effect) {
        return 1;
    }
    if (!fm1_channel_ok(s, e->ch)) {
        return 0;
    }
    s->freq[e->ch] &= 0xffffu & ~FM1_KEY_ON; /* the key-on bit cleared, the word kept to 16 bits */
    fm1_opl(s, OPL_KEY_BLOCK + e->ch, s->freq[e->ch] >> 8);
    return 1;
}
/* Note off: an effect's note frequency remembered and written without the key (5EB; not on the voice's channel); a
 * song's remembered frequency written without its key, not remembered (5CF; song channels 0..6 only). */
static int fm1_note_off(Fm1Sequence *s, const Fm1Event *e, unsigned effect)
{
    if (!effect) {
        if (e->ch <= 6u) {
            fm1_write_frequency(s, e->ch, s->freq[e->ch] & FM1_BLOCK_FNUM);
        }
        return 1;
    }
    if (fm1_voice_skips(s, e->ch)) {
        return 1; /* 5F7..5FC */
    }
    if (!fm1_channel_ok(s, e->ch)) {
        return 0;
    }
    s->freq[e->ch] = fm1_note(s, e->a);
    fm1_write_frequency(s, e->ch, s->freq[e->ch]);
    return 1;
}
/* Note on (611 / 643): the note's frequency written without the key, then remembered and written with it (a re-key);
 * an effect's not on the voice's channel, a song's on channels 0..6 only. */
static int fm1_note_on(Fm1Sequence *s, const Fm1Event *e, unsigned effect)
{
    unsigned v;
    if (effect ? fm1_voice_skips(s, e->ch) : e->ch > 6u) {
        return 1;
    }
    if (!fm1_channel_ok(s, e->ch)) {
        return 0;
    }
    v = fm1_note(s, e->a);
    fm1_write_frequency(s, e->ch, v);
    v |= FM1_KEY_ON;
    s->freq[e->ch] = v;
    fm1_write_frequency(s, e->ch, v);
    return 1;
}
/* Song control (selector, level): the channel's level remembered and written when the selector is this build's
 * (blastmus 5 at 67Ah, admus 2 at 67Ch: a song carries one per device) and no fade runs. An effect's kind 10 does
 * nothing. */
static int fm1_song_control(Fm1Sequence *s, const Fm1Event *e, unsigned effect, const uint8_t *data)
{
    if (effect || e->a != s->level_selector || s->fade) {
        return 1;
    }
    if (!fm1_channel_ok(s, e->ch)) {
        return 0;
    }
    s->level[e->ch] = data[e->at + 1u];
    fm1_write_level(s, e->ch, data[e->at + 1u]);
    return 1;
}
/* Program change (70B / 72C): the record kept for slot A on the channel; a song's on channels 0..6 only, an effect's
 * (admus only: program_guard) not on the voice's channel. 0 when the slot is past 15, the channel past 8 or the
 * record outside the resource. */
static int fm1_program(Fm1Sequence *s, const Fm1Event *e, unsigned effect, const uint8_t *data, size_t size)
{
    if (effect ? s->program_guard && fm1_voice_skips(s, e->ch) : e->ch > 6u) {
        return 1;
    }
    if (e->a >= 16u) {
        fm1_fail(s, "FM instrument slot outside range");
        return 0;
    }
    return fm1_channel_ok(s, e->ch) &&
           fm1_instrument(s, e->ch, data, size, effect ? s->effect_records[e->a] : fm1_song_record(s, e->a));
}
/* Bend on (6C3), on both streams: rate, delta high, delta low; the phase starts at FFh. admus clears its sweep counter
 * first, even when the channel is the voice's, whose bend is then left off (6CD..6D2, admus 6D5..6DA). */
static int fm1_bend_on(Fm1Sequence *s, const Fm1Event *e, const uint8_t *data)
{
    if (!fm1_channel_ok(s, e->ch)) {
        return 0;
    }
    if (s->sweep_bug) {
        s->sweep = 0;
    }
    if (fm1_voice_skips(s, e->ch)) {
        return 1;
    }
    s->bend_rate[e->ch] = (uint8_t)e->a;
    s->bend_delta[e->ch] = (uint16_t)(data[e->at + 1u] << 8 | data[e->at + 2u]);
    s->bend_phase[e->ch] = 0xff;
    s->bend_on[e->ch] = 1;
    return 1;
}
/* Runs a channel event of stream EFFECT. 0 when it failed (the error set). Kinds 4 and 5 (the song's two bytes) and
 * an unused kind of the stream pass their operands over. */
static int fm1_channel_event(Fm1Sequence *s, const Fm1Event *e, unsigned effect, const uint8_t *data, size_t size)
{
    switch (e->kind) {
        case FM1_EVENT_INSTRUMENT:
            fm1_keep_record(s, e, effect);
            return 1;
        case FM1_EVENT_LEVEL:
            return fm1_effect_level(s, e, effect);
        case FM1_EVENT_FREQUENCY:
            return fm1_effect_frequency(s, e, effect);
        case FM1_EVENT_KEY_OFF:
            return fm1_effect_key_off(s, e, effect);
        case FM1_EVENT_NOTE_OFF:
            return fm1_note_off(s, e, effect);
        case FM1_EVENT_NOTE_ON:
            return fm1_note_on(s, e, effect);
        case FM1_EVENT_CONTROL:
            return fm1_song_control(s, e, effect, data);
        case FM1_EVENT_PROGRAM:
            return fm1_program(s, e, effect, data, size);
        case FM1_EVENT_BEND_OFF: /* 6FD, both streams */
            if (!fm1_channel_ok(s, e->ch)) {
                return 0;
            }
            s->bend_on[e->ch] = 0;
            return 1;
        case FM1_EVENT_BEND_ON:
            return fm1_bend_on(s, e, data);
        default:
            return 1;
    }
}
/* One batch of stream EFFECT. Returns 1 when it ended in a wait (the service follows), 0 when the stream ended or
 * failed (the tick ends). Each event is fetched (fm1_fetch); the flow events move the position here (a call, the
 * song's message and FFh continue at their new position; a wait ends the batch; another end stops the stream);
 * every other event acts on its channel (fm1_channel_event) and the position moves past its operands. */
static int fm1_batch(Fm1Sequence *s, unsigned effect)
{
    const uint8_t *data = effect ? s->overlay : s->song;
    size_t size = effect ? s->overlay_size : s->song_size;
    unsigned p = s->pos[effect];
    for (unsigned step = 0; step < FM1_BATCH; step++) {
        Fm1Event e;
        if (!fm1_fetch(s, data, size, &p, effect, &e)) {
            return 0;
        }
        switch (e.kind) {
            case FM1_EVENT_CALL:
                fm1_call(s, effect, data, &p);
                continue;
            case FM1_EVENT_WAIT: /* 7E7 / 7A1 */
                s->wait[effect] = e.ch ? e.ch : e.a;
                s->pos[effect] = p + e.n;
                return 1;
            case FM1_EVENT_MESSAGE:
                if (effect) {
                    break;
                }
                if (!fm1_skip_message(s, data, size, &p)) {
                    return 0;
                }
                continue;
            case FM1_EVENT_END:
                if (e.op == 0xffu) {
                    p = fm1_restart(s, effect);
                    continue;
                }
                s->active[effect] = 0;
                return 0;
            default:
                if (!fm1_channel_event(s, &e, effect, data, size)) {
                    return 0;
                }
                break;
        }
        p += e.n;
    }
    fm1_fail(s, "FM event budget exhausted");
    return 0;
}

/* The stream a tick runs (507..55D): the song when its wait runs out, else the effect when its does (so the effect's
 * wait counts down only on a tick the song does not take); -1 when neither is due. */
static int fm1_due_stream(Fm1Sequence *s)
{
    for (unsigned i = 0; i < 2u; i++) {
        if (s->active[i] && (!s->wait[i] || !--s->wait[i])) {
            return (int)i;
        }
    }
    return -1;
}
/* One timer tick, at the next tick's time: a voice whose window has closed ends first (its last interrupt stored 0);
 * then the due stream's batch, and the service (fm1_post) after a batch that ended in a wait or when no stream was
 * due. */
static void fm1_tick(Fm1Sequence *s)
{
    int stream;
    if (s->voice && s->next >= s->voice_to) {
        s->voice = 0;
    }
    s->now = s->next;
    s->next += FM1_TICK;
    stream = fm1_due_stream(s);
    if (stream < 0 || fm1_batch(s, (unsigned)stream)) {
        fm1_post(s);
    }
}
/* Runs the ticks up to PIT (each 16390 PIT cycles), then stands at PIT; a voice window that closes by PIT ends. */
void fm1_advance(Fm1Sequence *s, uint64_t pit)
{
    if (!s || s->error[0]) {
        return;
    }
    if (pit < s->now || pit > UINT64_MAX - FM1_TICK ||
        (pit >= s->next && (pit - s->next) / FM1_TICK >= FM1_TICK_LIMIT)) {
        fm1_fail(s, "Invalid or excessive FM clock advance");
        return;
    }
    while (s->next <= pit && !s->error[0]) {
        fm1_tick(s);
    }
    s->now = pit;
    if (s->voice && pit >= s->voice_to) {
        s->voice = 0;
    }
}
/* Phase 287: a voice of ibmsnd, adsnd or source from FROM (its start: the word 0Fh = 2) to TO (its end: 0); TO <= FROM
 * is a stop (or no voice). A new voice replaces the window, as the drivers' start stops the playing voice first. */
void fm1_voice(Fm1Sequence *s, uint64_t from, uint64_t to)
{
    if (!s) {
        return;
    }
    fm1_advance(s, from);
    if (s->error[0]) {
        return;
    }
    s->voice = to > from;
    s->voice_to = to;
}

/* Both builds start with the same two entry jumps (JMP 20Ah, the initialization; JMP 2C6h); their size tells them
 * apart. */
#define FM1_ENTRY_JUMPS "\xe9\x07\x02\xe9\xc0\x02"
/* The cue words' tables (a word per cue 0..FM1_CUE_LAST, entry 9's index) inside each build's image. */
#define FM1_CUE_LAST       0xb4u
#define FM1_BLASTMUS_CUES  0x94fu
#define FM1_ADMUS_CUES     0x975u
_Static_assert(FM1_BLASTMUS_CUES + 2u * FM1_CUE_LAST + 2u <= FM1_BLASTMUS_BYTES, "blastmus's cue words inside its image");
_Static_assert(FM1_ADMUS_CUES + 2u * FM1_CUE_LAST + 2u <= FM1_ADMUS_BYTES, "admus's cue words inside its image");
static const struct fm1_build {
    size_t bytes;
    unsigned cue_table, level_selector;
    int admus; /* the pitch-sweep defect and the effect program's voice test */
} fm1_builds[] = {
    {FM1_BLASTMUS_BYTES, FM1_BLASTMUS_CUES, 5u, 0}, /* blastmus (= specmus) */
    {FM1_ADMUS_BYTES, FM1_ADMUS_CUES, 2u, 1},
};
/**
 * Opens one of the one-chip FM drivers from its INTRO.CC image OVERLAY (SIZE bytes), its clock at ORIGIN (PIT
 * cycles); FLAGS bit FM1_FLAG_MIXER writes the SB Pro mixer first. EMIT receives the OPL, mixer and mode events.
 * Returns NULL for an image that is not blastmus/specmus or admus (the entry jumps or the size differ), for a voice
 * word at 0Fh that is not 0, for an ORIGIN with no room for a tick, or when memory runs out.
 */
Fm1Sequence *fm1_open(const uint8_t *overlay, size_t size, unsigned flags, Fm1Emit emit, void *context,
                      uint64_t origin)
{
    Fm1Sequence *s;
    const struct fm1_build *build = NULL;
    if (!overlay || memcmp(overlay, FM1_ENTRY_JUMPS, sizeof FM1_ENTRY_JUMPS - 1u) || origin > UINT64_MAX - FM1_TICK) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof fm1_builds / sizeof *fm1_builds; i++) {
        if (size == fm1_builds[i].bytes) {
            build = &fm1_builds[i];
        }
    }
    if (!build || overlay[FM1_VOICE_WORD] || overlay[FM1_VOICE_WORD + 1]) {
        return NULL;
    }
    if (!(s = calloc(1, sizeof *s)) || !(s->overlay = malloc(size))) {
        free(s);
        return NULL;
    }
    memcpy(s->overlay, overlay, size);
    s->overlay_size = size;
    s->cue_table = build->cue_table;
    s->sweep_bug = build->admus;
    s->program_guard = build->admus;
    s->level_selector = build->level_selector;
    s->volume = overlay[FM1_VOLUME];
    s->emit = emit;
    s->context = context;
    s->now = origin;
    s->next = origin + FM1_TICK;
    /* One chip: the renderer plays it on both outputs. */
    fm1_out(s, FM1_EVENT_MODE, FM1_MODE_MONO, 0);
    /* 20A..24F: with XEEN.CFG's 8D4C nonzero, the SB Pro mixer at 8D3A + 4: line 2Eh, master 22h, FM 26h = FFh,
     * voice 04h = 99h (the same four writes as promus). */
    if (flags & FM1_FLAG_MIXER) {
        static const uint8_t mixer[4][2] = {{0x2e, 0xff}, {0x22, 0xff}, {0x26, 0xff}, {0x04, 0x99}};
        for (unsigned i = 0; i < 4u; i++) {
            fm1_out(s, FM1_EVENT_MIXER, mixer[i][0], mixer[i][1]);
        }
    }
    /* 27B..295: the test register's waveform select, the keyboard split, the rhythm/depth; then the stops. */
    fm1_opl(s, 0x01, 0x20);
    fm1_opl(s, 0x08, 0x00);
    fm1_opl(s, 0xbd, 0x00);
    fm1_stop_song(s);
    fm1_stop_effect(s);
    return s;
}

/* Entry 9 (2FE): cues 7..0Ah are refused while an effect plays, cues past B4h always; else the cue starts: its wait
 * cleared, the bends of 7 and 8 off, the effect channels stopped. Its call depth is NOT reset (a cue interrupted in a
 * call leaves its returns to the next). */
void fm1_command(Fm1Sequence *s, unsigned command, uint64_t pit)
{
    unsigned at;
    if (!s) {
        return;
    }
    fm1_advance(s, pit);
    if (s->error[0] || (s->active[1] && command >= 7u && command < 11u) || command > FM1_CUE_LAST) {
        return;
    }
    at = fm1_le16(s->overlay + s->cue_table + command * 2u);
    s->pos[1] = s->start = at;
    s->wait[1] = 0;
    s->bend_on[7] = s->bend_on[8] = 0;
    fm1_stop_effect(s);
    s->active[1] = 1;
}

/* Entry 6 (346) for V <= FFh: 0 stops the song and its fade; 1 restarts it (position, call depth, wait and fade
 * cleared, the song channels stopped); 2..FFh set a fade of 3Fh steps when a song plays (the phase is kept). */
void fm1_control(Fm1Sequence *s, unsigned control, uint64_t pit)
{
    if (!s) {
        return;
    }
    fm1_advance(s, pit);
    if (s->error[0]) {
        return;
    }
    if (control == 0u) {
        s->active[0] = 0;
        s->fade = 0;
        fm1_stop_song(s);
    } else if (control == 1u) {
        s->pos[0] = s->depth[0] = s->wait[0] = 0;
        s->fade = 0;
        s->active[0] = 1;
        fm1_stop_song(s);
    } else if (control <= 255u && s->active[0]) {
        s->fade = (uint8_t)control;
        s->fade_left = 0x3f;
    }
}

/* Entry 6 with a song segment (39A): the song taken and restarted. An empty song stops (as promus's native). */
void fm1_song(Fm1Sequence *s, const uint8_t *data, size_t size, uint64_t pit)
{
    uint8_t *copy;
    if (!s) {
        return;
    }
    fm1_advance(s, pit);
    if (s->error[0]) {
        return;
    }
    if (!size) {
        fm1_control(s, 0, pit);
        return;
    }
    if (!data || size > 65535u || !(copy = malloc(size))) {
        fm1_fail(s, "Invalid FM song");
        return;
    }
    memcpy(copy, data, size);
    free(s->song);
    s->song = copy;
    s->song_size = size;
    fm1_control(s, 1, pit);
}

/* Entry 6 (100h, V): the song volume byte is stored; it raises the carrier level of instruments loaded later
 * (441). Nothing is written now. */
void fm1_volume(Fm1Sequence *s, unsigned volume, uint64_t pit)
{
    if (!s) {
        return;
    }
    fm1_advance(s, pit);
    if (!s->error[0]) {
        s->volume = (uint8_t)volume;
    }
}

unsigned fm1_fading(const Fm1Sequence *s)
{
    return s ? s->fade : 0u;
}
const char *fm1_error(const Fm1Sequence *s)
{
    return s ? s->error : "FM sequencer unavailable";
}
/* Entry 3 (2C6): the song and effect channels stopped. */
void fm1_close(Fm1Sequence *s, uint64_t pit)
{
    if (!s) {
        return;
    }
    fm1_advance(s, pit);
    fm1_stop_song(s);
    fm1_stop_effect(s);
    free(s->song);
    free(s->overlay);
    free(s);
}
