#include "xeen/audio.h"
#include "xeen/ds_globals.h"
#include "xeen/byte_order.h"
#include <string.h>
#include <stdio.h>
void xeen_native_audio_init(xeen_native_audio *s)
{
    if (s) {
        memset(s, 0, sizeof *s);
    }
}
xeen_status xeen_native_audio_effect(xeen_native_audio *s, const xeen_movement *m, uint16_t effect)
{
    if (!m) {
        return XEEN_BAD_INPUT;
    }
    if (!m->ds[XEEN_DS_SOUND_ON]) {
        return XEEN_OK;
    }
    if (!s) {
        return XEEN_SERVICE_REQUIRED;
    }
    ++s->effects;
    s->last_effect = effect;
    if (s->sink && s->sink->effect) {
        s->sink->effect(s->sink->context, effect);
    }
    return XEEN_OK;
}
/* ------------------------------------------------ the voice model */
uint32_t xeen_voc_driver_samples(const uint8_t *voc, size_t n, unsigned trim)
{
    if (!voc || n < XEEN_VOC_FIRST_LENGTH + 2u) {
        return 0;
    }
    return (uint32_t)(uint16_t)(xeen_get_le16(voc + XEEN_VOC_FIRST_LENGTH) - trim) + 1u; /* 15A: sub ax, trim */
}
int xeen_voc_busy_at(uint64_t start, uint64_t now, uint32_t samples)
{
    uint64_t d = now >= start ? now - start : 0u;
    /* d < 2^27 keeps d*65536*1000000 below 2^63 (2^27 ticks: 85 days); a longer gap is never busy. */
    return d < (UINT64_C(1) << 27) &&
           d * (uint64_t)XEEN_BIOS_TICK_PIT * UINT64_C(1000000) <
                   (uint64_t)samples * XEEN_VOC_DRIVER_SAMPLE_US * XEEN_PIT_HZ;
}
uint32_t xeen_voc_driver_count(const uint8_t *voc, size_t n, unsigned kind, unsigned trim)
{
    unsigned length;
    if (kind == XEEN_VOICE_MODEL_PCSPEAKER) {
        /* ibmsnd 007A..0090, 0107..0114: the position from 20h, the end when the incremented position equals L. */
        if (!voc || n < XEEN_VOC_FIRST_LENGTH + 2u) {
            return 0;
        }
        length = xeen_get_le16(voc + XEEN_VOC_FIRST_LENGTH);
        length = (length - XEEN_VOC_FIRST_SAMPLES) & 0xffffu;
        return length ? length : 0x10000u;
    }
    if (kind != XEEN_VOICE_MODEL_ADLIB && kind != XEEN_VOICE_MODEL_SOURCE) {
        return xeen_voc_driver_samples(voc, n, trim);
    }
    if (!voc || n < XEEN_VOC_FIRST_LENGTH + 2u) {
        return 0;
    }
    length = xeen_get_le16(voc + XEEN_VOC_FIRST_LENGTH);
    if (kind == XEEN_VOICE_MODEL_ADLIB) {
        return length > XEEN_VOC_FIRST_SAMPLES ? length - XEEN_VOC_FIRST_SAMPLES : 1u; /* adsnd 018B: jae */
    }
    /* source 00EB: ja. Its 16-bit position (20h, then 17 on) never passes a length of FFFDh or more before it wraps:
     * that voice never ends (an original quirk; no shipped VOC is that long). */
    if (length >= 0xfffdu) {
        return UINT32_MAX;
    }
    return length >= XEEN_VOC_FIRST_SAMPLES ? (length - XEEN_VOC_FIRST_SAMPLES) / XEEN_VOC_SOURCE_STEP + 2u : 1u;
}
uint64_t xeen_voc_driver_pit(const uint8_t *voc, size_t n, unsigned kind, unsigned trim)
{
    uint32_t count = kind ? xeen_voc_driver_count(voc, n, kind, trim) : 0u;
    switch (kind) {
        case XEEN_VOICE_MODEL_ADLIB:
            return (uint64_t)count * XEEN_VOC_ADLIB_SAMPLE_PIT;
        case XEEN_VOICE_MODEL_SOURCE:
            return count == UINT32_MAX ? UINT64_MAX : (uint64_t)count * XEEN_VOC_SOURCE_IRQ_PIT;
        case XEEN_VOICE_MODEL_PCSPEAKER:
            return (uint64_t)count * XEEN_VOC_PCSPEAKER_SAMPLE_PIT;
        case XEEN_VOICE_MODEL_PAS:
            return (uint64_t)count * XEEN_VOC_PAS_SAMPLE_PIT;
        case XEEN_VOICE_MODEL_SB: /* one sample per 68 us, rounded up to the PIT cycle */
            return ((uint64_t)count * XEEN_VOC_DRIVER_SAMPLE_US * XEEN_PIT_HZ + 999999u) / 1000000u;
        default:
            return 0;
    }
}
uint32_t xeen_voc_pcspeaker_music_calls(uint32_t irqs)
{
    /* 010F..0122: the interrupt that reaches L ends the voice before the accumulator's add (148h; a carry chains): the
     * first IRQS - 1 interrupts may chain. */
    return irqs ? (uint32_t)((uint64_t)(irqs - 1u) * XEEN_VOC_PCSPEAKER_CHAIN_STEP >> 16) : 0u;
}
uint32_t xeen_voc_pcspeaker_bios_ticks(uint32_t irqs, int clock_stands)
{
    /* the music drivers' 000Ch: add cs:[acc], 4006h; jb the old INT 8 (ibmmus 12BF: with 0Fh = 2, IRET) */
    return clock_stands ? 0u : (uint32_t)((uint64_t)xeen_voc_pcspeaker_music_calls(irqs) * 0x4006u >> 16);
}
int xeen_voc_driver_marks_music(unsigned kind)
{
    return kind == XEEN_VOICE_MODEL_ADLIB || kind == XEEN_VOICE_MODEL_SOURCE || kind == XEEN_VOICE_MODEL_PCSPEAKER;
}
int xeen_voc_busy_at_kind(uint64_t start, uint64_t now, uint32_t samples, unsigned kind)
{
    uint64_t d = now >= start ? now - start : 0u;
    if (kind == XEEN_VOICE_MODEL_PCSPEAKER) {
        return 0; /* ibmsnd's query (005A): 0 after its start returned */
    }
    if (kind == XEEN_VOICE_MODEL_ADLIB || kind == XEEN_VOICE_MODEL_SOURCE) {
        uint64_t period = kind == XEEN_VOICE_MODEL_ADLIB ? XEEN_VOC_ADLIB_SAMPLE_PIT : XEEN_VOC_SOURCE_IRQ_PIT;
        return d < (UINT64_C(1) << 27) && d * (uint64_t)XEEN_BIOS_TICK_PIT < (uint64_t)samples * period;
    }
    if (kind != XEEN_VOICE_MODEL_PAS) {
        return xeen_voc_busy_at(start, now, samples);
    }
    /* specsnd: a sample per 81 cycles of the PIT clock that also makes the BIOS tick (65536 cycles). */
    return d < (UINT64_C(1) << 27) && d * (uint64_t)XEEN_BIOS_TICK_PIT < (uint64_t)samples * XEEN_VOC_PAS_SAMPLE_PIT;
}
/* Gameplay's answer at voice_now without counting (0 without a model). */
static int xeen_native_audio_voice_busy(const xeen_native_audio *s)
{
    return s && s->voice_model && s->voice_active &&
           xeen_voc_busy_at_kind(s->voice_start, s->voice_now, s->voice_samples, s->voice_model);
}
void xeen_native_audio_voice_model(xeen_native_audio *s, int model, unsigned trim)
{
    if (!s) {
        return;
    }
    s->voice_model = (uint8_t)(model == XEEN_VOICE_MODEL_PAS         ? XEEN_VOICE_MODEL_PAS
                               : model == XEEN_VOICE_MODEL_ADLIB     ? XEEN_VOICE_MODEL_ADLIB
                               : model == XEEN_VOICE_MODEL_SOURCE    ? XEEN_VOICE_MODEL_SOURCE
                               : model == XEEN_VOICE_MODEL_PCSPEAKER ? XEEN_VOICE_MODEL_PCSPEAKER
                               : model                            ? XEEN_VOICE_MODEL_SB
                                                                  : XEEN_VOICE_MODEL_NONE);
    s->voice_trim = (uint8_t)trim;
    s->voice_active = 0;
}
/**
 * Reports one voice query, start or stop (KIND at SITE, with the gate and busy state) to the voice
 * observer, when one is attached.
 */
static void voice_report(xeen_native_audio *s, int kind, uint32_t site, int gate, int busy)
{
    xeen_voice_request r;
    if (!s->voice_observer) {
        return;
    }
    memset(&r, 0, sizeof r);
    r.n = s->voice_queries + s->voice_starts + s->voice_stops;
    r.kind = kind;
    r.site = site;
    r.now = s->voice_now;
    r.start = s->voice_start;
    r.samples = s->voice_samples;
    r.gate = gate;
    r.active = s->voice_active;
    r.busy = busy;
    s->voice_observer(s->voice_context, &r);
}
/* A start (187E9(F42C, 1) or (segment, 0)): the driver's entry 3 stops any
 * voice and plays the new one. */
static void voice_start(xeen_native_audio *s, const xeen_movement *m, uint32_t samples,
                        uint32_t site)
{
    int gate = m->ds[XEEN_DS_SOUND_ON] != 0;
    if (!s->voice_model) {
        return;
    }
    if (gate) {
        s->voice_active = samples != 0;
        s->voice_start = s->voice_now;
        s->voice_samples = samples;
    }
    ++s->voice_starts;
    voice_report(s, XEEN_VOICE_START, site, gate, 0);
    /* Phase 287: ibmsnd's start returns only after its last interrupt (0072..00F0), then nothing is busy. */
    if (gate && samples && s->voice_model == XEEN_VOICE_MODEL_PCSPEAKER) {
        s->voice_active = 0;
        ++s->voice_blocks;
        if (s->voice_block) {
            s->voice_block(s->voice_block_context, samples);
        }
    }
}
int xeen_native_audio_voice_query(xeen_native_audio *s, const xeen_movement *m, uint32_t site)
{
    int gate, busy;
    if (!s || !m || !s->voice_model) {
        return 0; /* F42E = 0: 187E9 answers 0 */
    }
    gate = m->ds[XEEN_DS_SOUND_ON] != 0;
    busy = gate && xeen_native_audio_voice_busy(s);
    if (gate && s->voice_active && !busy) {
        s->voice_active = 0; /* the IRQ has cleared [1B] */
    }
    ++s->voice_queries;
    if (busy) {
        ++s->voice_busy;
    }
    voice_report(s, XEEN_VOICE_QUERY, site, gate, busy);
    return busy;
}
void xeen_native_audio_voice_stop(xeen_native_audio *s, const xeen_movement *m, uint32_t site)
{
    int gate;
    if (!s || !m) {
        return;
    }
    gate = m->ds[XEEN_DS_SOUND_ON] != 0;
    /* 187E9's gate: with ED9A clear the driver is not called (the voice plays on). */
    if (gate && s->sink && s->sink->voice_stop) {
        s->sink->voice_stop(s->sink->context);
    }
    if (!s->voice_model) {
        return;
    }
    if (gate) {
        s->voice_active = 0;
    }
    ++s->voice_stops;
    voice_report(s, XEEN_VOICE_STOP, site, gate, 0);
}
void xeen_native_audio_voice_at(xeen_native_audio *s, const xeen_movement *m, const uint8_t *voc,
                                size_t n, uint32_t site)
{
    if (s && s->sink && s->sink->voice && m && m->ds[XEEN_DS_SOUND_ON] && voc && n) {
        s->sink->voice(s->sink->context, voc, n);
    }
    if (s && m && voc && n) {
        voice_start(s, m, s->voice_model ? xeen_voc_driver_count(voc, n, s->voice_model, s->voice_trim) : 0u,
                    site);
    }
}
void xeen_native_audio_voice(xeen_native_audio *s, const xeen_movement *m, const uint8_t *voc,
                             size_t n)
{
    xeen_native_audio_voice_at(s, m, voc, n, 0);
}
void xeen_native_audio_voice_name(xeen_native_audio *s, const xeen_movement *m)
{
    char name[24];
    size_t i;
    if (!s || !m) {
        return;
    }
    for (i = 0; i + 1u < sizeof name && m->ds[XEEN_DS_FILE_NAME + i]; ++i) {
        name[i] = (char)m->ds[XEEN_DS_FILE_NAME + i];
    }
    name[i] = 0;
    if (!i) {
        return;
    }
    if (s->sink && s->sink->voice_name && m->ds[XEEN_DS_SOUND_ON]) {
        s->sink->voice_name(s->sink->context, name, m->ds[XEEN_DS_WORLD]);
    }
    if (s->voice_model) {
        voice_start(s, m,
                    s->voice_lookup ? s->voice_lookup(s->voice_lookup_context, name, m->ds[XEEN_DS_WORLD])
                                    : 0u,
                    0);
    }
}
void xeen_native_audio_song(xeen_native_audio *s, const xeen_movement *m, const uint8_t *song,
                            size_t n)
{
    if (s && s->sink && s->sink->song && m && m->ds[XEEN_DS_MUSIC_ON]) {
        s->sink->song(s->sink->context, song, n);
    }
}
void xeen_native_audio_song_buffer(xeen_native_audio *s, const uint8_t *song, size_t n)
{
    if (s && s->sink && s->sink->song_buffer && song && n) {
        s->sink->song_buffer(s->sink->context, song, n);
    }
}
void xeen_native_audio_start_buffer(xeen_native_audio *s, const xeen_movement *m)
{
    if (s && s->sink && s->sink->song_buffer_start && m && m->ds[XEEN_DS_MUSIC_ON]) {
        s->sink->song_buffer_start(s->sink->context);
    }
}
/* 187CB tests only the driver word (F430), not ED9B. */
void xeen_native_audio_volume(xeen_native_audio *s, unsigned level)
{
    if (s && s->sink && s->sink->volume) {
        s->sink->volume(s->sink->context, level);
    }
}
void xeen_native_audio_music(xeen_native_audio *s, const xeen_movement *m, unsigned control)
{
    if (s && s->sink && s->sink->music && m && m->ds[XEEN_DS_MUSIC_ON]) {
        s->sink->music(s->sink->context, control);
    }
}
void xeen_native_audio_music_driver(xeen_native_audio *s, unsigned control)
{
    if (s && s->sink && s->sink->music) {
        s->sink->music(s->sink->context, control);
    }
}
unsigned xeen_cd_side(uint8_t world, uint8_t map)
{
    if (!world) {
        return 0;
    }
    return (map >= 53u && map <= 56u) || (map >= 73u && map <= 77u) ||
                           (map >= 83u && map <= 88u) || (map >= 125u && map <= 127u) ||
                           map == 129u
                   ? 0u
                   : 1u;
}
/** Returns the last audio track of CD DISC (31 for disc 0, 30 for disc 1, else 0). */
unsigned xeen_cd_last_track(unsigned disc)
{
    return disc == 0 ? 31u : disc == 1 ? 30u : 0u;
}
int xeen_cd_track_file(unsigned disc, unsigned track, char *name, size_t n)
{
    int w;
    if (!name || !n || track < XEEN_CD_FIRST_TRACK || track > xeen_cd_last_track(disc)) {
        return 0;
    }
    w = snprintf(name, n, "xeen%02u.ogg", disc ? track + 30u : track);
    return w > 0 && (size_t)w < n;
}
/* 483C7's arithmetic for one word: 1074B (/10000) of the 32-bit value; the
 * seconds and frames from the 16-bit word less 16-bit products (483FC..4846C);
 * 48167 sign-extends each of the three (CWD) before (m*60+s)*75+f. */
static int32_t cd_frames(uint16_t word, int over_99)
{
    uint32_t v = (uint32_t)word + (over_99 && word < 60000u ? 0xffffu : 0u);
    uint16_t m = (uint16_t)(v / 10000u), s, f;
    s = (uint16_t)((uint16_t)(word - (uint16_t)(m * 10000u)) / 100u);
    f = (uint16_t)(word - (uint16_t)((uint16_t)(m * 10000u) + (uint16_t)(s * 100u)));
    return ((int32_t)(int16_t)m * 60 + (int16_t)s) * 75 + (int16_t)f;
}
void xeen_cd_segment(uint16_t from, uint16_t to, int over_99, int32_t *start, int32_t *end)
{
    /* 4847D: - 0AFh, 484DA: - 96h; the track start's 150-frame lead-in cancels. */
    if (start) {
        *start = cd_frames(from, over_99) - 25;
    }
    if (end) {
        *end = cd_frames(to, over_99);
    }
}
/* A play (1) or stop (2) to the status observer. */
static void cd_request(xeen_native_audio *s, int request)
{
    xeen_cd_status_query q;
    if (!s->cd_status_observer) {
        return;
    }
    memset(&q, 0, sizeof q);
    q.n = s->cd_status_queries;
    q.now = s->cd_now;
    q.start = s->cd_play_tick;
    q.frames = s->cd_play_frames;
    q.active = s->cd_active;
    q.outstanding = s->cd_outstanding;
    q.request = request;
    s->cd_status_observer(s->cd_status_context, &q);
}
void xeen_native_audio_cd_start(xeen_native_audio *s, uint16_t track, uint16_t from, uint16_t to,
                                int over_99, unsigned disc)
{
    if (!s) {
        return;
    }
    ++s->cd_starts;
    s->cd_track = track;
    s->cd_from = from;
    s->cd_to = to;
    s->cd_disc = (uint8_t)disc;
    xeen_cd_segment(from, to, over_99, &s->cd_start_frame, &s->cd_end_frame);
    /* 482D8: 84h with the length end - start (483C7's -96h and -0AFh). */
    s->cd_active = s->cd_model != 0;
    s->cd_outstanding = 1;
    s->cd_play_tick = s->cd_now;
    s->cd_play_frames = s->cd_end_frame > s->cd_start_frame
                                ? (uint32_t)(s->cd_end_frame - s->cd_start_frame)
                                : 0u;
    if (s->sink && s->sink->cd_play) {
        s->sink->cd_play(s->sink->context, disc, track, s->cd_start_frame, s->cd_end_frame);
    }
    cd_request(s, 1);
}
/* 483B4, the play stopped: nothing active or outstanding, the sink's stop and the stop request (2). */
static void cd_halt(xeen_native_audio *s)
{
    s->cd_active = 0;
    s->cd_outstanding = 0;
    if (s->sink && s->sink->cd_stop) {
        s->sink->cd_stop(s->sink->context);
    }
    cd_request(s, 2);
}
void xeen_native_audio_cd_stop(xeen_native_audio *s)
{
    if (!s) {
        return;
    }
    ++s->cd_stops;
    cd_halt(s);
}
void xeen_native_audio_cd_side(xeen_native_audio *s, unsigned side)
{
    if (!s) {
        return;
    }
    if (s->cd_drive_known && s->cd_drive != side) {
        cd_halt(s); /* 47F7F's two 483B4 */
    }
    s->cd_drive = (uint8_t)side;
    s->cd_drive_known = 1;
}
/* [audio] speech = off (s->speech_off) holds the panel's Speech off: SPEECH_ON and TEXT_OFF are stored 0, as the
 * original's Speech off stores Text off 0 (2976D). */
void xeen_native_audio_speech_hold(const xeen_native_audio *s, uint8_t *ds)
{
    if (s && ds && s->speech_off) {
        ds[XEEN_DS_SPEECH_ON] = 0;
        ds[XEEN_DS_TEXT_OFF] = 0;
    }
}
/* 483C7, an NPC's CD voice: the PlayCD tuple (XEEN_DS_CD_PLAY_*). With speech on (2813E: F385 set; speech = off holds
 * it off first) and a tuple, a track past 99 drops 100 (483FC; its positions below 60000 count 65536 more: the
 * segment's over_99); the play goes to the sink with its disc and segment, and 48505 clears XEEN_DS_CD_REPLAY. The
 * disc is 19CC3's side (the map load and the speech toggle's close keep it so while speech is on); the from/to words
 * become CD frames (xeen_cd_segment). Speech off or an empty tuple returns before 48505: the replay flag keeps its
 * value. The caller sets cd_now (the play's tick).
 * 48477/484D4 call 48167 twice with the track; before its 482F4 request 48167 writes the track's entry in
 * XEEN_DS_CD_TRACK_TABLE itself (48170: 0Bh, 4817E: the track's low byte; the index 16-bit). Those two bytes are the
 * game's, not the driver's, and track 1Eh's FBE8 is compared, so they are written here; the entry's rest (+2..+6) is
 * the driver's answer and nothing reads FBE8 (phase131/CD_STATE_CONTRACT.md). The MSCDEX requests themselves (the
 * FB08..FC10 blocks, 50B26; 483B4, the stop, writes only those) are the CD platform's; the native drive answers no
 * error (FBF8 0, so 4850B never retries). */
void xeen_native_audio_cd_voice(xeen_native_audio *s, uint8_t *ds)
{
    uint16_t track, entry;
    int over_99 = 0;
    if (!s || !ds) {
        return;
    }
    xeen_native_audio_speech_hold(s, ds); /* speech = off takes 483C7's speech-off return */
    track = xeen_get_le16(ds + XEEN_DS_CD_PLAY_TRACK);
    if (!ds[XEEN_DS_SPEECH_ON] ||
        !(track | xeen_get_le16(ds + XEEN_DS_CD_PLAY_START) | xeen_get_le16(ds + XEEN_DS_CD_PLAY_END))) {
        return;
    }
    if (track > 99u) {
        xeen_put_le16(ds + XEEN_DS_CD_PLAY_TRACK, (uint16_t)(track - 100u));
        over_99 = 1;
    }
    track = xeen_get_le16(ds + XEEN_DS_CD_PLAY_TRACK);
    entry = (uint16_t)(XEEN_DS_CD_TRACK_TABLE + track * 7u);
    ds[entry] = 0x0b;
    ds[(uint16_t)(entry + 1u)] = (uint8_t)track;
    xeen_native_audio_cd_start(s, xeen_get_le16(ds + XEEN_DS_CD_PLAY_TRACK), xeen_get_le16(ds + XEEN_DS_CD_PLAY_START),
                               xeen_get_le16(ds + XEEN_DS_CD_PLAY_END), over_99,
                               xeen_cd_side(ds[XEEN_DS_MAP_SIDE], ds[XEEN_DS_PARTY_MAP]));
    xeen_put_le16(ds + XEEN_DS_CD_REPLAY, 0);
}
/* 47B7D. 47BBA's media-changed IOCTL: the native drive's disc is
 * changed only by 19CC3's side (xeen_native_audio_cd_side), so it answers
 * "unchanged" and 47F7F is not called here. Then 47F3D (return 47B96): busy
 * and the side's archive read from the CD (XEEN_DS_CC_ON_DISK[96E2] = 0, the index
 * sign-extended) -> 483B4 and XEEN_DS_CD_REPLAY = 1. FB08 belongs to the request blocks (not modelled). 47B7D
 * never clears the replay flag. No device: the query is idle and nothing changes. */
int xeen_native_audio_cd_preload(xeen_native_audio *s, uint8_t *ds)
{
    uint16_t side;
    if (!s || !ds) {
        return 0;
    }
    side = (uint16_t)(XEEN_DS_CC_ON_DISK + 2u * (unsigned)(int)(int8_t)ds[XEEN_DS_MAP_SIDE]);
    if (!xeen_native_audio_cd_status(s, XEEN_CD_STATUS_PRELOAD_CALL) || xeen_get_le16(ds + side)) {
        return 0;
    }
    xeen_native_audio_cd_stop(s);  /* 47BAB: 483B4 */
    xeen_put_le16(ds + XEEN_DS_CD_REPLAY, 1);
    return 1;
}
/* 188E8/18923/18964/189AB after the load returned (a failed
 * driver load ends in 505CF and never gets here): XEEN_DS_CD_REPLAY set -> 483C7
 * again with the tuple DS holds now. */
void xeen_native_audio_cd_postload(xeen_native_audio *s, uint8_t *ds)
{
    if (s && ds && xeen_get_le16(ds + XEEN_DS_CD_REPLAY)) {
        xeen_native_audio_cd_voice(s, ds);
    }
}
int xeen_cd_busy_at(uint64_t start, uint64_t now, uint32_t frames)
{
    uint64_t d = now >= start ? now - start : 0u;
    /* d < 2^38 keeps d*65536*75 below 2^61; a longer gap is never busy. */
    return d < (UINT64_C(1) << 38) &&
           d * (uint64_t)XEEN_BIOS_TICK_PIT * UINT64_C(75) < (uint64_t)frames * XEEN_PIT_HZ;
}
int xeen_native_audio_cd_busy(const xeen_native_audio *s)
{
    return s && s->cd_model && s->cd_active &&
           xeen_cd_busy_at(s->cd_play_tick, s->cd_now, s->cd_play_frames);
}
int xeen_native_audio_cd_status(xeen_native_audio *s, uint32_t site)
{
    xeen_cd_status_query q;
    if (!s) {
        return 0;
    }
    memset(&q, 0, sizeof q);
    q.n = ++s->cd_status_queries;
    q.site = site;
    q.now = s->cd_now;
    q.start = s->cd_play_tick;
    q.frames = s->cd_play_frames;
    q.active = s->cd_active;
    q.outstanding = s->cd_outstanding;
    q.busy = xeen_native_audio_cd_busy(s);
    if (q.busy) {
        ++s->cd_status_busy;
    } else if (s->cd_active && s->cd_model) {
        s->cd_active = 0; /* the play has ended */
    }
    if (s->cd_status_observer) {
        s->cd_status_observer(s->cd_status_context, &q);
    }
    return q.busy;
}
void xeen_native_audio_cd_model(xeen_native_audio *s, int model)
{
    if (s) {
        s->cd_model = (uint8_t)(model != 0);
        s->cd_active = 0;
        s->cd_outstanding = 0;
    }
}
void xeen_speech_voice_model(xeen_speech_resolution *s, int have_cfg, unsigned *kind, unsigned *trim)
{
    if (have_cfg) {
        unsigned sound = s->word;
        *kind = !s->modelled ? XEEN_VOICE_MODEL_NONE
                : s->fallback ? XEEN_VOICE_MODEL_SB
                : sound == 1u ? XEEN_VOICE_MODEL_PCSPEAKER
                : sound == 8u ? XEEN_VOICE_MODEL_PAS
                : sound == 2u ? XEEN_VOICE_MODEL_ADLIB
                : sound == 5u ? XEEN_VOICE_MODEL_SOURCE
                              : XEEN_VOICE_MODEL_SB;
        *trim = !s->fallback && sound == 6u ? 0x0eu : 0x0au;
    } else {
        s->audible = !s->forced_off;
        s->modelled = !s->forced_off;
        *kind = s->forced_off ? XEEN_VOICE_MODEL_NONE : XEEN_VOICE_MODEL_SB;
    }
}
