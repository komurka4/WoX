/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Keep raw ending digits alongside their wrapping numeric score. The ending_payload entry
 * points validate command inputs and choose digit frames for player_modes and the score
 * scenes.
 */
#include "ending_payload.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#define ENDING_PAYLOAD_DIGITS 10
#define ENDING_DIGIT_FRAME_BIAS 28u
#define ENDING_BLANK_DIGIT_FRAME 30u

/* Parse exactly ten borrowed ASCII digits into caller-owned raw digits and a wrapping 32-bit
 * score. Return one on success, zero for invalid input without changing payload. No memory is
 * allocated.
 */
int ending_payload_parse(const char *text, EndingPayload *payload)
{
    EndingPayload parsed = {{0}, 0};
    unsigned digit_index;
    if (!text || !payload || strlen(text) != ENDING_PAYLOAD_DIGITS) {
        return 0;
    }
    for (digit_index = 0; digit_index < ENDING_PAYLOAD_DIGITS; digit_index++) {
        unsigned char character = (unsigned char)text[digit_index];
        if (character < '0' || character > '9') {
            return 0;
        }
        parsed.digits[digit_index] = (char)character;
        /* Numeric scores wrap to 32 bits while the raw decimal digits stay intact. */
        parsed.score = parsed.score * UINT32_C(10) + (uint32_t)(character - '0');
    }
    *payload = parsed;
    return 1;
}

/* Format a numeric score as ten decimal digits in the borrowed payload and retain its numeric
 * value. The caller supplies a valid payload; there is no failure result or returned
 * allocation.
 */
void ending_payload_from_score(uint32_t score, EndingPayload *payload)
{
    snprintf(payload->digits, sizeof(payload->digits), "%010" PRIu32, score);
    payload->score = score;
}

/* Return the digit-sprite frame for a column in the borrowed raw payload. Columns outside the
 * ten digits select the blank frame; payload digits are assumed to have been validated.
 */
unsigned ending_payload_frame(const EndingPayload *payload, unsigned column)
{
    return column < ENDING_PAYLOAD_DIGITS
               ? (unsigned)(unsigned char)payload->digits[column] - ENDING_DIGIT_FRAME_BIAS
               : ENDING_BLANK_DIGIT_FRAME;
}
