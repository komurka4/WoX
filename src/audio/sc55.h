/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Expose the versioned SC-55 C ABI around the optional Nuked-SC55 backend. sc55_open owns
 * firmware and ROM storage until sc55_close; sc55_loader connects it to the C11 synth
 * interface.
 */
#ifndef XEEN_SC55_H
#define XEEN_SC55_H
#include <stddef.h>
#include <stdint.h>
#define XEEN_SC55_ABI_VERSION 1u
#if defined(_WIN32) && defined(XEEN_SC55_BUILD)
#define XEEN_SC55_API __declspec(dllexport)
#elif defined(_WIN32)
#define XEEN_SC55_API __declspec(dllimport)
#elif defined(__GNUC__)
#define XEEN_SC55_API __attribute__((visibility("default")))
#else
#define XEEN_SC55_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Original SC-55 (mk1), detected by ROM hashes. No ROM files are written.
 * Calls must be serialized. Output is S16 interleaved stereo at 32000 Hz.
 */
typedef struct XeenSC55 XeenSC55;
/* Return the shared library C ABI version so the native loader can reject incompatible
 * backends. This query allocates nothing and cannot fail.
 */
XEEN_SC55_API unsigned sc55_abi_version(void);
/* Recognize one complete MK1 ROM set in the borrowed directory and boot an owned SC-55 engine
 * without changing ROM files. Return state for sc55_close or NULL after a setup, allocation or
 * exception diagnostic. Serialize every later message, render and close call.
 */
XEEN_SC55_API XeenSC55 *sc55_open(const char *directory);
/* sc55_open reporting its boot's progress (owner-requested host start-up bar; optional: a loader treats a library
 * without it as one with no progress). PROGRESS (may be NULL) is called on the opening thread with 0..1000 per
 * mille of the firmware warm-up, in increasing order, the last call 1000 once the warm-up has finished. */
typedef void (*XeenSC55Progress)(void *user, unsigned permille);
XEEN_SC55_API XeenSC55 *sc55_open_progress(const char *directory, XeenSC55Progress progress, void *user);
/* Copy borrowed MIDI bytes into the engine's bounded queue in order. Null, empty or failed
 * state is ignored; overflow latches stopped synthesis and emits a diagnostic. The caller can
 * reuse message storage on return.
 */
XEEN_SC55_API void sc55_message(XeenSC55 *, const uint8_t *, size_t);
/* Fill caller-owned stereo S16 frames at the backend's native rate. Null or failed state
 * produces silence; a stall or exception silences the unfinished tail and latches failure with
 * a diagnostic. Null output or zero frames is ignored; serialize access with message delivery.
 */
XEEN_SC55_API void sc55_render(XeenSC55 *, int16_t *, size_t frames);
/* Delete owned SC-55 engine and ROM storage; null is safe. The caller must stop rendering and
 * message delivery before close. ROM files remain external and unchanged; no status is
 * returned.
 */
XEEN_SC55_API void sc55_close(XeenSC55 *);
#ifdef __cplusplus
}
#endif
#endif
