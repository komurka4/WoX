/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* The movie player's command resolution and typed outcomes (player_modes.c). Phase 308: the
 * public entry point is xeen_movie_play (include/xeen/movie.h, player.c).
 */
#ifndef XEEN_PLAYER_H
#define XEEN_PLAYER_H
#include "ending_payload.h"
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    XEEN_MODE_DARKSIDE,
    XEEN_MODE_CLOUDS,
    XEEN_MODE_CLOUDS_ENDING,
    XEEN_MODE_DARKSIDE_ENDING,
    XEEN_MODE_WORLD_ENDING,
    XEEN_MODE_CLOUDS_LOGO
} XeenMode;

typedef struct {
    XeenMode mode;
    int comparison;
    EndingPayload payload;
    unsigned badge;
} XeenStart;

typedef enum {
    XEEN_OUT_COMPLETED,
    XEEN_OUT_SKIPPED,
    XEEN_OUT_ABORTED,
    XEEN_OUT_INVALID,
    XEEN_OUT_ASSET_FAILURE,
    XEEN_OUT_EVIDENCE_STOP
} XeenOutcome;

/* Resolve a borrowed command string into sequence, raw ending payload, score and badge in
 * caller-owned startup state. Preserve the command comparison aliases and wrapping score
 * rules. Return one on success, zero for an oversized command or invalid Clouds payload;
 * output can already be initialized on failure.
 */
int xeen_resolve_start(const char *command, XeenStart *start);
/* Resolve a borrowed canonical sequence name into caller-owned startup state with a zero
 * score. Return one on success or zero for invalid input or an unknown name; a failed lookup
 * can leave start reset.
 */
int xeen_resolve_sequence(const char *name, XeenStart *start);
/* Return the static sequence name for a mode, or NULL for an invalid mode. The returned string
 * is borrowed and must not be freed.
 */
const char *xeen_mode_name(XeenMode mode);
/* Return the static display name for a typed cinematic outcome. Unknown values use the
 * invalid-input name; the borrowed result needs no release.
 */
const char *xeen_outcome_name(XeenOutcome outcome);
/* Translate a typed outcome into the standalone process status: zero for completion, two for
 * skip or invalid input, and one for other failures. No allocation or failure channel is
 * involved.
 */
int xeen_outcome_exit(XeenOutcome outcome);
#ifdef __cplusplus
}
#endif
#endif
