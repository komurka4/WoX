/* SPDX-License-Identifier: GPL-3.0-or-later
 */
/* Keep all third-party C++ and both real OPL2 chip instances behind the optional C ABI.
 * No OPL3 core or ROM is used. Rational resampling follows 3579545/72 Hz to 32000 Hz;
 * native FM mixer gain is three-bit linear, not a claim of calibrated CT1345 analogue gain.
 *
 * register writes take chip time. promus writes every register through 4AD/4C5: OUT index,
 * three IN 40h, OUT data, twenty IN 40h (about 14..25 us on the ISA bus, one YM3812 sample is 72 clocks
 * of 3.579545 MHz = 20.1 us). The renderer queues the chip writes in their order and applies one write per
 * chip sample. Before Phase170 all writes of a driver tick landed between two chip samples, so a key-off
 * followed by a key-on in the same tick (B0h bit 5 1->0->1: promus re-keys a channel) was never seen
 * by the envelope generator and the note was not re-attacked (the Diamond Rock cue 14h stayed in a
 * silent sustain, peak -52 dBFS; DOSBox and a timed hardware-accurate core both re-attack it). */
#include "sbpro_sequence.h"
#include "ymfm_opl.h"
#include <algorithm>
#include <new>

/* Pending chip writes (chip, register, value). 8192 writes are 165 ms of chip time; a song start writes
 * a few hundred. On overflow the oldest write is applied at once (never dropped). */
constexpr size_t WRITE_QUEUE = 8192;
struct ChipWrite {
    uint8_t chip, reg, value;
};

struct SBProRenderer {
    ymfm::ymfm_interface interface_left, interface_right;
    ymfm::ym3812 left, right;
    ChipWrite pending[WRITE_QUEUE];
    size_t head = 0, count = 0;
    int32_t previous[2] = {0, 0};
    int32_t current[2] = {0, 0};
    uint64_t phase = 0;
    unsigned gain[2] = {7, 7};
    int mono = 0; /* Phase 286: a one-chip driver (SBPRO_MODE_MONO): chip 0 on both outputs */

    SBProRenderer() : left(interface_left), right(interface_right)
    {
        left.reset();
        right.reset();
    }
};

/** Creates the SB Pro (dual OPL2) renderer; only 32000 Hz output is supported (NULL otherwise). */
static SBProRenderer *renderer_open(unsigned rate)
{
    if (rate != 32000) {
        return nullptr;
    }
    try {
        return new SBProRenderer();
    } catch (...) {
        return nullptr;
    }
}

/** Applies the next pending register write to the left or right OPL chip. */
static void apply_write(SBProRenderer *renderer)
{
    const ChipWrite &w = renderer->pending[renderer->head];
    auto &chip = w.chip ? renderer->right : renderer->left;
    chip.write_address(w.reg);
    chip.write_data(w.value);
    renderer->head = (renderer->head + 1) % WRITE_QUEUE;
    renderer->count--;
}

/** Queues one 4-byte register-write message (marker, chip, register, value) for the renderer. */
static void renderer_message(SBProRenderer *renderer, const uint8_t *bytes, size_t length)
{
    if (!renderer || !bytes || length != 4) {
        return;
    }
    if (bytes[0] == SBPRO_EVENT_OPL && bytes[1] <= 1) {
        if (renderer->count == WRITE_QUEUE) {
            apply_write(renderer);
        }
        renderer->pending[(renderer->head + renderer->count) % WRITE_QUEUE] = {bytes[1], bytes[2],
                                                                               bytes[3]};
        renderer->count++;
    } else if (bytes[0] == SBPRO_EVENT_MODE) {
        renderer->mono = bytes[2] == SBPRO_MODE_MONO;
    } else if (bytes[0] == SBPRO_EVENT_MIXER && bytes[2] == SBPRO_MIXER_FM_VOLUME) {
        renderer->gain[0] = bytes[3] >> 5 & 7;
        renderer->gain[1] = bytes[3] >> 1 & 7;
    }
}

/**
 * Renders COUNT stereo samples, applying each queued register write at its sample, one per chip
 * sample as the original's port timing does.
 */
static void renderer_render(SBProRenderer *renderer, int16_t *samples, size_t count)
{
    if (!samples) {
        return;
    }
    if (!renderer) {
        std::fill_n(samples, count * 2, 0);
        return;
    }
    constexpr uint64_t denominator = 72u * 32000u;
    for (size_t i = 0; i < count; i++) {
        renderer->phase += 3579545;
        while (renderer->phase >= denominator) {
            renderer->phase -= denominator;
            if (renderer->count) {
                apply_write(renderer);
            }
            ymfm::ym3812::output_data left, right;
            renderer->left.generate(&left);
            renderer->right.generate(&right);
            for (unsigned channel = 0; channel < 2; channel++) {
                renderer->previous[channel] = renderer->current[channel];
            }
            renderer->current[0] = left.data[0];
            renderer->current[1] = renderer->mono ? left.data[0] : right.data[0];
        }
        for (unsigned channel = 0; channel < 2; channel++) {
            int64_t value = (int64_t)renderer->previous[channel] * (denominator - renderer->phase) +
                            (int64_t)renderer->current[channel] * renderer->phase;
            value = value / (int64_t)denominator * renderer->gain[channel] / 7;
            samples[i * 2 + channel] = (int16_t)std::clamp<int64_t>(value, -32768, 32767);
        }
    }
}

/** Frees the renderer. */
static void renderer_close(SBProRenderer *renderer)
{
    delete renderer;
}

extern "C" SBPRO_EXPORT const SBProAPI *sbpro_get_api(unsigned version)
{
    static const SBProAPI api = {XEEN_SBPRO_ABI,   sizeof(SBProAPI),     sbpro_sequence_open,
                                 sbpro_advance,    sbpro_song,           sbpro_command,
                                 sbpro_control,    sbpro_volume,         sbpro_fading,
                                 sbpro_error,      sbpro_sequence_close, renderer_open,
                                 renderer_message, renderer_render,      renderer_close,
                                 sbpro_sequence_open_flags, sbpro_voice};
    return version == XEEN_SBPRO_ABI ? &api : nullptr;
}
