/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Resolve command and sequence selection, ending payloads and typed outcomes. The xeen_resolve
 * entry points work with player and ending_payload. Phase 308: the former out-of-process handoff
 * (stub, --handoff-exec) is gone; the game's host starts the game program in process (include/xeen/movie.h).
 */
#include "player.h"
#include <string.h>
#include <stdlib.h>
#define START_COMMAND_CAPACITY_BYTES 2048
#define START_COMPARE_BYTES 8
#define START_DARKSIDE_KEY 6
#define START_WORLD_KEY 18
#define START_CLOUDS_KEY 19
#define START_WORLD_SCORE_END 19
#define START_WORLD_BADGE_OFFSET 20

static unsigned player_fold_command_character(unsigned character)
{
    return character >= 'a' && character <= 'z' ? character - 32 : character;
}

/* Parse a signed decimal prefix from borrowed command bytes, allowing leading whitespace and
 * stopping at the first nondigit. Return a wrapping 32-bit value, or zero when no digits are
 * present; there is no separate error result.
 */
static uint32_t player_parse_command_number(const unsigned char *cursor)
{
    uint32_t value = 0;
    int negative = 0;
    while (*cursor == ' ' || (*cursor >= 9 && *cursor <= 13)) {
        cursor++;
    }
    if (*cursor == '+' || *cursor == '-') {
        negative = *cursor == '-';
        cursor++;
    }
    while (*cursor >= '0' && *cursor <= '9') {
        value = value * 10 + (*cursor - '0');
        cursor++;
    }
    return negative ? 0u - value : value;
}

/* Return the static sequence name for a mode, or NULL for an invalid mode. The returned string
 * is borrowed and must not be freed.
 */
const char *xeen_mode_name(XeenMode mode)
{
    static const char *const names[] = {"darkside",        "clouds",       "clouds-ending",
                                        "darkside-ending", "world-ending", "clouds-logo"};
    return (unsigned)mode < sizeof(names) / sizeof(*names) ? names[mode] : NULL;
}

/* Resolve a borrowed canonical sequence name into caller-owned startup state with a zero
 * score. Return one on success or zero for invalid input or an unknown name; a failed lookup
 * can leave start reset.
 */
int xeen_resolve_sequence(const char *name, XeenStart *start)
{
    unsigned i;
    if (!start || !name) {
        return 0;
    }
    memset(start, 0, sizeof(*start));
    ending_payload_from_score(0, &start->payload);
    for (i = 0; i <= XEEN_MODE_CLOUDS_LOGO; i++) {
        if (!strcmp(name, xeen_mode_name((XeenMode)i))) {
            start->mode = (XeenMode)i;
            return 1;
        }
    }
    return 0;
}

/* Resolve a borrowed command string into sequence, raw ending payload, score and badge in
 * caller-owned startup state. Preserve the command comparison aliases and wrapping score
 * rules. Return one on success, zero for an oversized command or invalid Clouds payload;
 * output can already be initialized on failure.
 */
int xeen_resolve_start(const char *command, XeenStart *start)
{
    static const unsigned char START_COMMAND_REFERENCE[] = "endgame,";
    unsigned char buffer[START_COMMAND_CAPACITY_BYTES] = {0};
    size_t command_bytes;
    size_t index;
    int key = 0;
    if (!start) {
        return 0;
    }
    if (!command) {
        command = "(null)";
    }
    command_bytes = strlen(command);
    if (command_bytes >= sizeof(buffer)) {
        return 0; /* Bound the native command buffer before copying the command string. */
    }
    memcpy(buffer, command, command_bytes);
    for (index = 0; index < START_COMPARE_BYTES; index++) {
        unsigned command_character = buffer[index];
        unsigned reference_character = START_COMMAND_REFERENCE[index];
        if (!command_character) {
            key = -(int)reference_character;
            break;
        }
        key = (int)player_fold_command_character(command_character) -
              (int)player_fold_command_character(reference_character);
        if (key) {
            break;
        }
    }
    memset(start, 0, sizeof(*start));
    start->comparison = key;
    ending_payload_from_score(0, &start->payload);
    start->mode = key == 0                    ? XEEN_MODE_CLOUDS_ENDING
                  : key == START_DARKSIDE_KEY ? XEEN_MODE_DARKSIDE_ENDING
                  : key == START_WORLD_KEY    ? XEEN_MODE_WORLD_ENDING
                  : key == START_CLOUDS_KEY   ? XEEN_MODE_CLOUDS
                                              : XEEN_MODE_DARKSIDE;
    if (start->mode == XEEN_MODE_CLOUDS_ENDING) {
        char digits[11];
        memcpy(digits, buffer + 8, 10);
        digits[10] = 0;
        /* Ending payloads accept canonical decimal digits so sprite frames stay in range. */
        if (!ending_payload_parse(digits, &start->payload)) {
            return 0;
        }
    } else if (start->mode == XEEN_MODE_DARKSIDE_ENDING || start->mode == XEEN_MODE_WORLD_ENDING) {
        if (start->mode == XEEN_MODE_WORLD_ENDING) {
            buffer[START_WORLD_SCORE_END] = 0;
            start->badge = buffer[START_WORLD_BADGE_OFFSET] == '1'   ? 1
                           : buffer[START_WORLD_BADGE_OFFSET] == '2' ? 2
                                                                     : 0;
        }
        ending_payload_from_score(player_parse_command_number(buffer + 9), &start->payload);
    }
    return 1;
}

/* Return the static display name for a typed cinematic outcome. Unknown values use the
 * invalid-input name; the borrowed result needs no release.
 */
const char *xeen_outcome_name(XeenOutcome outcome)
{
    static const char *const OUTCOME_NAMES[] = {"completed",      "skipped",       "aborted",
                                                "invalid-input",  "asset-failure", "evidence-stop"};
    return (unsigned)outcome < sizeof(OUTCOME_NAMES) / sizeof(*OUTCOME_NAMES)
               ? OUTCOME_NAMES[outcome]
               : "invalid-input";
}

/* Translate a typed outcome into the standalone process status: zero for completion, two for
 * skip or invalid input, and one for other failures. No allocation or failure channel is
 * involved.
 */
int xeen_outcome_exit(XeenOutcome outcome)
{
    /* Preserve the established standalone ABI: skipped and invalid both2;
     * failures1. The typed outcome disambiguates them.
     */
    return outcome == XEEN_OUT_COMPLETED                                ? 0
           : outcome == XEEN_OUT_SKIPPED || outcome == XEEN_OUT_INVALID ? 2
                                                                        : 1;
}
