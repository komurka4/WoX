/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Keep raw ending digits alongside their wrapping numeric score. The ending_payload entry
 * points validate command inputs and choose digit frames for player_modes and the score
 * scenes.
 */
#ifndef XEEN_ENDING_PAYLOAD_H
#define XEEN_ENDING_PAYLOAD_H
#include <stdint.h>

/* Ending digits and numeric score are independent: the digit sprites use the
 * raw decimal bytes while numeric parsing wraps to 32 bits. Keep both forms.
 */
typedef struct {
    char digits[11];
    uint32_t score;
} EndingPayload;

/* Parse exactly ten borrowed ASCII digits into caller-owned raw digits and a wrapping 32-bit
 * score. Return one on success, zero for invalid input without changing payload. No memory is
 * allocated.
 */
int ending_payload_parse(const char *text, EndingPayload *payload);
/* Format a numeric score as ten decimal digits in the borrowed payload and retain its numeric
 * value. The caller supplies a valid payload; there is no failure result or returned
 * allocation.
 */
void ending_payload_from_score(uint32_t score, EndingPayload *payload);
/* Return the digit-sprite frame for a column in the borrowed raw payload. Columns outside the
 * ten digits select the blank frame; payload digits are assumed to have been validated.
 */
unsigned ending_payload_frame(const EndingPayload *payload, unsigned column);
#endif
