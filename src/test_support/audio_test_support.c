/*
 * Original audio driver entries used only by the tests.
 *
 * Test support (Phase 284): functions only the tests call, moved out of src/audio/audio.c
 * so that the game's executable does not carry them. They are built into the xeen_test_support library,
 * which the test programs link and the shipped programs do not; their declarations stay where they were.
 */
#include "xeen/audio.h"
#include "xeen/ds_globals.h"
#include "xeen/byte_order.h"
#include <string.h>
#include <stdio.h>

/**
 * Loads the BLASTMUS.DRV music driver image into S at SEGMENT, after checking its entry jump and
 * effect-routine signatures and its size; returns XEEN_BAD_INPUT for any other file.
 */
xeen_status xeen_audio_load_blastmus(xeen_audio *s, const void *data, size_t size, uint16_t segment)
{
    const uint8_t *p = data;
    /* Unique bounded code signatures and table extent, not filename trust. */
    static const uint8_t entry[] = {0xe9, 0xbd, 0x02};
    static const uint8_t fx[] = {0x55, 0x8b, 0xec, 0x1e, 0x8c, 0xc8, 0x8e, 0xd8};
    if (!s || !p || size < 0xa89 || size > 65536 || memcmp(p + 9, entry, 3) ||
        memcmp(p + 0x2c9, fx, sizeof fx)) {
        return XEEN_BAD_INPUT;
    }
    memset(s, 0, sizeof *s);
    memcpy(s->segment, p, size);
    s->dos_segment = segment;
    s->loaded = 1;
    return XEEN_OK;
}
/**
 * Records one OPL register write (index to PORT, value to PORT+1) and the 23 timer reads the
 * driver's write delay makes.
 */
static void opl_write(xeen_audio_result *r, uint16_t port, uint8_t reg, uint8_t value)
{
    r->writes[r->write_count++] = (xeen_audio_output){port, reg};
    r->writes[r->write_count++] = (xeen_audio_output){(uint16_t)(port + 1u), value};
    r->pit_reads += 23;
}
/* The blastmus driver's segment as its effect entry (driver:0009, from 1880E) reads it: the word per cue (0..180) of its cue table at 91Fh,
 * the chip's port word at 18h, the byte at 1Fh set while an effect plays (cues 7..10 are then refused, as the music
 * driver's music_command refuses them: xeen/audio.h's XEEN_MUSIC_EFFECT_*), and per channel its carrier's operator slot (18Fh + channel) and saved
 * level byte (EBh + channel, whose key-scale bits the 3Fh attenuation keeps); the OPL2 registers it writes. The
 * other offsets keep their numbers. */
#define FX_DRIVER_CUES           0x91fu
#define FX_DRIVER_PORT           0x18u
#define FX_DRIVER_EFFECT_ACTIVE  0x1fu
#define FX_DRIVER_CARRIER_SLOTS  0x18fu
#define FX_DRIVER_CARRIER_LEVELS 0xebu
#define FX_OPL_LEVEL             0x40u
#define FX_OPL_FNUMBER_LOW       0xa0u
#define FX_OPL_KEY_BLOCK         0xb0u
xeen_status xeen_audio_play_fx(xeen_audio *s, const xeen_movement *m, uint16_t fx, uint16_t cx,
                               uint8_t pit, xeen_audio_result *r)
{
    uint8_t *p;
    uint16_t ptr, port;
    unsigned ch;
    if (!m || !r) {
        return XEEN_BAD_INPUT;
    }
    memset(r, 0, sizeof *r);
    if (!m->ds[XEEN_DS_SOUND_ON]) {
        return XEEN_OK;
    }
    if (!s || !s->loaded) {
        r->boundary = XEEN_AUDIO_EFFECT_CALL;
        return XEEN_SERVICE_REQUIRED;
    }
    p = s->segment;
    r->ax = s->dos_segment;
    r->bx = fx;
    r->cx = fx;
    if ((p[FX_DRIVER_EFFECT_ACTIVE] && fx >= XEEN_MUSIC_EFFECT_FIRST && fx < XEEN_MUSIC_EFFECT_END) ||
        fx > XEEN_MUSIC_CUE_LAST) {
        return XEEN_OK;
    }
    ptr = xeen_get_le16(p + (FX_DRIVER_CUES + fx * 2u));
    xeen_put_le16(p + 0xe9, ptr);
    xeen_put_le16(p + 0x11a, ptr);
    p[0x119] = p[0x117] = p[0x116] = 0;
    port = xeen_get_le16(p + FX_DRIVER_PORT);
    for (ch = xeen_get_le16(p + 0xf) ? 8u : 7u; ch <= 8; ch++) {
        xeen_put_le16(p + (ch == 7 ? 0x10b : 0x10d), 0);
        opl_write(r, port, (uint8_t)(FX_OPL_FNUMBER_LOW + ch), (uint8_t)cx);
        opl_write(r, port, (uint8_t)(FX_OPL_KEY_BLOCK + ch), (uint8_t)(cx >> 8));
        p[0xf4 + ch] = 0x3f;
        opl_write(r, port, (uint8_t)(p[FX_DRIVER_CARRIER_SLOTS + ch] + FX_OPL_LEVEL),
                  (uint8_t)(0x3f | (p[FX_DRIVER_CARRIER_LEVELS + ch] & 0xc0u)));
        cx = 0;
    }
    p[FX_DRIVER_EFFECT_ACTIVE] = 1;
    r->ax = (uint16_t)(((uint16_t)(0x3f | (p[0xf3] & 0xc0u)) << 8) | pit);
    r->bx = 8;
    r->cx = fx;
    r->dx = (uint16_t)(port + 1u);
    return XEEN_OK;
}
/**
 * Reads XEEN.CFG's 29 bytes into SELECTION: the sound and music driver names chosen by its words 0
 * and 4 (out-of-range choices give nullsnd and nullmus) and the ports in words 2 and 6; returns
 * XEEN_BAD_INPUT for a file of any other size.
 */
xeen_status xeen_audio_select_configuration(const void *data, size_t size,
                                            xeen_audio_selection *selection)
{
    const uint8_t *p = data;
    static const char *const sound[] = {"ibmsnd",   "adsnd",  "nullsnd", "nullsnd", "source",
                                        "blastsnd", "prosnd", "specsnd", "covxsnd", "prosnd"};
    static const char *const music[] = {"admus",   "blastmus", "promus",  "rolmus", "canmus",
                                        "specmus", "ibmmus",   "wavemus", "tapmus"};
    uint16_t s, m;
    if (!p || !selection || size != 29) {
        return XEEN_BAD_INPUT;
    }
    s = xeen_get_le16(p + 0);
    m = xeen_get_le16(p + 4);
    selection->sound_name = (s >= 1 && s <= 10) ? sound[s - 1] : "nullsnd";
    selection->music_name = (m >= 1 && m <= 9) ? music[m - 1] : "nullmus";
    selection->sound_port = xeen_get_le16(p + 2);
    selection->music_port = xeen_get_le16(p + 6);
    return XEEN_OK;
}
