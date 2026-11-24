/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Expose the versioned SC-55 C ABI around the optional Nuked-SC55 backend. sc55_open owns
 * firmware and ROM storage until sc55_close; sc55_loader connects it to the C11 synth
 * interface.
 */
#include "sc55.h"
#include "emu.h"
#include "file_hashing.h"
#include "audio.h"
#include "diagnostics.h"
#include <cstdio>
#include <cstring>
#include <array>
#include <exception>
#include <memory>
#include <string>

#define SC55_OUTPUT_RATE_HZ   32000
#define SC55_BOOT_STEPS       24000000
#define SC55_DAC_OFFSET       512 /* the 16-bit output's constant offset (sc55_receive_sample) */
#define SC55_PROGRESS_STEPS   (SC55_BOOT_STEPS / 1000) /* one per-mille of the warm-up */
#define SC55_STALL_STEP_LIMIT 65536

struct XeenSC55 {
    RomsetInfo roms; /* Must outlive the emulator per LoadRoms contract. */
    Emulator emu;
    static constexpr size_t MIDI_CAPACITY_BYTES = 131072;
    std::array<uint8_t, MIDI_CAPACITY_BYTES> midi{};
    size_t midi_read = 0;
    size_t midi_write = 0;
    size_t midi_count = 0;
    int16_t *output = nullptr;
    size_t remaining = 0;
    bool failed = false;
};

/* Copy one normalized backend stereo sample into the currently borrowed render span and
 * advance its remaining count. Discard samples while no span is attached, including boot
 * warm-up. The engine owns callback context and returns no status.
 */
static void sc55_receive_sample(void *context, const AudioFrame<int32_t> &frame)
{
    auto *synth = static_cast<XeenSC55 *>(context);
    if (!synth->remaining) {
        return;
    }
    /* The emulated DAC's constant offset (+512 at 16 bits, idle or playing) removed, as the SC-55's AC-coupled
     * analog output does (owner-approved): otherwise the device's start, pause and close step from 0 to it and back
     * (an audible click). An arithmetic shift makes this exactly the 16-bit value less 512 before the clamp. */
    AudioFrame<int32_t> centred = frame;
    centred.left -= SC55_DAC_OFFSET << 15;
    centred.right -= SC55_DAC_OFFSET << 15;
    AudioFrame<int16_t> pcm;
    Normalize(centred, pcm);
    *synth->output++ = pcm.left;
    *synth->output++ = pcm.right;
    --synth->remaining;
}

/* Return the shared library C ABI version so the native loader can reject incompatible
 * backends. This query allocates nothing and cannot fail.
 */
extern "C" unsigned sc55_abi_version(void)
{
    return XEEN_SC55_ABI_VERSION;
}

/* Select the only complete MK1 set among borrowed recognized ROM names. Write its name into
 * caller-owned selected storage. Return false with a diagnostic when none or multiple sets
 * qualify; no ROM bytes are copied here.
 */
static bool sc55_select_original_rom_set(const RomsetRegistry &registry, const StringVector &names,
                                         std::string &selected)
{
    for (const auto &name : names) {
        Romset family;
        if (registry.GetRomsetFamily(name, family) && family == Romset::MK1) {
            if (!selected.empty()) {
                std::fputs("SC-55: multiple original SC-55 ROM versions; use a directory with "
                           "one complete set\n",
                           stderr);
                return false;
            }
            selected = name;
        }
    }
    if (selected.empty()) {
        std::fputs("SC-55: no complete recognized original SC-55 ROM set (two control ROMs and "
                   "three wave ROMs)\n",
                   stderr);
        return false;
    }
    return true;
}

/* Report SHA-256 identities of the engine-owned ROM bytes before backend loading transforms
 * wave storage. Return false on hashing failure, true after all present ROMs are reported.
 * External files and storage ownership do not change.
 */
static bool sc55_fingerprint_roms(XeenSC55 *synth)
{
    /* Fingerprint supplied file bytes before LoadRomset unscrambles waves. */
    for (size_t i = 0; i < ROMLOCATION_COUNT; i++) {
        if (synth->roms.rom_data[i].empty()) {
            continue;
        }
        SHA256_Digest digest;
        if (!SHA256_HashBytes(synth->roms.rom_data[i], digest)) {
            return false;
        }
        std::fprintf(stderr, "SC-55 ROM SHA-256 %s ",
                     synth->roms.rom_paths[i].generic_string().c_str());
        for (auto byte : digest) {
            std::fprintf(stderr, "%02x", byte);
        }
        std::fputc('\n', stderr);
    }
    return true;
}

/* Load the engine-owned ROM set, establish native-rate output and perform bounded firmware
 * warm-up before attaching audio delivery. Return false on load, initialization or rate
 * failure. The caller owns cleanup of partially initialized engine state.
 */
static bool sc55_boot_emulator(XeenSC55 *synth, XeenSC55Progress progress, void *user)
{
    if (!LoadRomset(synth->roms, nullptr) || !synth->emu.Init({}) ||
        !synth->emu.LoadRoms(Romset::MK1, synth->roms)) {
        std::fputs("SC-55: ROM load or emulator initialization failed\n", stderr);
        return false;
    }
    synth->emu.Reset();
    /* MK1's native rate is 32000 Hz with optional 2x output disabled.
     * No fractional resampler or callback-dependent phase is necessary.
     */
    synth->emu.GetPCM().enable_oversampling = false;
    if (PCM_GetOutputFrequency(synth->emu.GetPCM()) != SC55_OUTPUT_RATE_HZ) {
        return false;
    }
    synth->emu.SetSampleCallback(sc55_receive_sample, synth);
    /* Match the upstream offline renderer's bounded warm-up. Startup happens
     * before attaching to SDL; discarded boot audio never delays a scene.
     */
    for (unsigned i = 0; i < SC55_BOOT_STEPS; i++) {
        synth->emu.Step();
        if (progress && (i + 1u) % SC55_PROGRESS_STEPS == 0u) {
            progress(user, (i + 1u) / SC55_PROGRESS_STEPS);
        }
    }
    return true;
}

/* Recognize one complete MK1 ROM set in the borrowed directory and boot an owned SC-55 engine
 * without changing ROM files. Return state for sc55_close or NULL after a setup, allocation or
 * exception diagnostic. Serialize every later message, render and close call.
 */
extern "C" XeenSC55 *sc55_open(const char *directory)
{
    return sc55_open_progress(directory, nullptr, nullptr);
}

/* sc55_open with the warm-up's progress reported to PROGRESS (sc55.h). */
extern "C" XeenSC55 *sc55_open_progress(const char *directory, XeenSC55Progress progress, void *user)
{
    try {
        if (!directory || !*directory) {
            return nullptr;
        }
        Diag_SetCallback([](Diag_Category cat, std::string_view message) {
            if (cat != Diag_Category::Debug) {
                std::fprintf(stderr, "SC-55: %.*s", (int)message.size(), message.data());
            }
        });
        HashedFileRegistry hashes;
        if (!HashDirectoryFiles(directory, HashDirectoryKind::TopLevel, hashes,
                                [](const std::filesystem::directory_entry &e) {
                                    return e.file_size() <= ROM_MAX_SIZE;
                                })) {
            std::fputs("SC-55: cannot read ROM directory\n", stderr);
            return nullptr;
        }
        auto registry = RomsetRegistry::CreateWithDefaultHashes();
        StringVector names;
        GetCompleteRomsetNames(registry, hashes, ROMLOCATION_ALL, names);
        std::string selected;
        if (!sc55_select_original_rom_set(registry, names, selected)) {
            return nullptr;
        }
        auto synth = std::make_unique<XeenSC55>();
        if (!GetRomsetInfo(registry, selected, hashes, ROMLOCATION_ALL, synth->roms)) {
            return nullptr;
        }
        if (!sc55_fingerprint_roms(synth.get())) {
            return nullptr;
        }
        if (!sc55_boot_emulator(synth.get(), progress, user)) {
            return nullptr;
        }
        /* Keep firmware power-on state. Only source MIDI may select a map/reset. */
        std::fprintf(stderr, "SC-55: %s, firmware power-on mode, stereo 32000 Hz\n",
                     selected.c_str());
        return synth.release();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "SC-55: %s\n", e.what());
        return nullptr;
    } catch (...) {
        std::fputs("SC-55: initialization failed\n", stderr);
        return nullptr;
    }
}

/* Copy borrowed MIDI bytes into the engine's bounded queue in order. Null, empty or failed
 * state is ignored; overflow latches stopped synthesis and emits a diagnostic. The caller can
 * reuse message storage on return.
 */
extern "C" void sc55_message(XeenSC55 *synth, const uint8_t *bytes, size_t length)
{
    if (!synth || synth->failed || !bytes || !length) {
        return;
    }
    if (length > synth->MIDI_CAPACITY_BYTES - synth->midi_count) {
        synth->failed = true;
        std::fputs("SC-55: MIDI queue overflow; synthesis stopped\n", stderr);
        return;
    }
    for (size_t i = 0; i < length; i++) {
        synth->midi[synth->midi_write] = bytes[i];
        synth->midi_write = (synth->midi_write + 1) % synth->MIDI_CAPACITY_BYTES;
    }
    synth->midi_count += length;
}

/* Fill caller-owned stereo S16 frames at the backend's native rate. Null or failed state
 * produces silence; a stall or exception silences the unfinished tail and latches failure with
 * a diagnostic. Null output or zero frames is ignored; serialize access with message delivery.
 */
extern "C" void sc55_render(XeenSC55 *synth, int16_t *output, size_t frames)
{
    if (!output || !frames) {
        return;
    }
    if (!synth || synth->failed) {
        std::memset(output, 0, frames * 2 * sizeof(*output));
        return;
    }
    synth->output = output;
    synth->remaining = frames;
    try {
        unsigned stalled_steps = 0;
        while (synth->remaining) {
            /* MCU_PostUART's ring has no overflow check. Retain excess MIDI in our
             * queue and pump only its free capacity, preserving byte order.
             */
            auto &m = synth->emu.GetMCU();
            while (synth->midi_count &&
                   (m.uart_write_ptr + 1) % uart_buffer_size != m.uart_read_ptr) {
                synth->emu.PostMIDI(synth->midi[synth->midi_read]);
                synth->midi_read = (synth->midi_read + 1) % synth->MIDI_CAPACITY_BYTES;
                --synth->midi_count;
            }
            size_t before = synth->remaining;
            synth->emu.Step();
            if (synth->remaining < before) {
                stalled_steps = 0;
            } else if (++stalled_steps == SC55_STALL_STEP_LIMIT) {
                /* Bound stalled firmware so rendering returns instead of blocking
                 * its caller indefinitely.
                 */
                synth->failed = true;
                std::memset(synth->output, 0, synth->remaining * 2 * sizeof(*output));
                std::fputs("SC-55: no audio progress; synthesis stopped\n", stderr);
                break;
            }
        }
    } catch (...) {
        synth->failed = true;
        std::memset(synth->output, 0, synth->remaining * 2 * sizeof(*output));
        std::fputs("SC-55: rendering failed\n", stderr);
    }
    synth->output = nullptr;
    synth->remaining = 0;
}

/* Delete owned SC-55 engine and ROM storage; null is safe. The caller must stop rendering and
 * message delivery before close. ROM files remain external and unchanged; no status is
 * returned.
 */
extern "C" void sc55_close(XeenSC55 *synth)
{
    delete synth;
}
