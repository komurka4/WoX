/*
 * The party's money and the bank's (gold and gems, the DS dwords funds_word names): a payment taken when there is
 * enough, and the refusal's message prepared when there is not, for every service that charges: the shops' Buy, Fix
 * and Identify, the bank's deposits and withdrawals, the guild, the trainer and the temple's town services, the
 * events' payments. Each caller shows the refusal in its own way (a scroll, a location message) and pops the
 * refusal's button set at the message's end.
 * Original: 291FB (the refusal) and 29281 (the payment), the routines beside the yes/no 2937A.
 */
#include "game_internal.h"
#include <stdio.h>

/*
 * A refused payment, up to its message: the button set pushed, the refusal's sound, then "Not enough <gold / gems>
 * in the <Party / Bank>!" (KIND enum funds_kind, WHERE enum funds_place) or, when EMPTY, "No <gold / gems> in
 * the <place>!" in the text buffer. The caller shows the message in its own way and pops the button set at its end.
 * Original: 291FB(kind, where, empty, mode) before its 18F2F: 1880E(15h), A827 = DS:597A or 599B, the names DS:2F44
 * and DS:29A4; the callers' 18F2F and 18FFF.
 */
void value_refused(xeen_game *g, unsigned kind, unsigned where, int empty)
{
    xeen_movement *m = &g->movement;
    char out[0x60];
    xeen_button_stack(m->ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    (void)sound_effect(g, XEEN_CUE_REFUSED);
    (void)snprintf(out, sizeof out, empty ? ds_text(g, XEEN_TEXT_PAY_NONE) : ds_text(g, XEEN_TEXT_PAY_NOT_ENOUGH),
                   far_text(g, XEEN_DS_PARTY_VALUE_NAMES, kind), far_text(g, XEEN_DS_VALUE_PLACE_NAMES, where));
    ds_put_text(g, XEEN_DS_TEXT_BUFFER, out);
}
/* Pays AMOUNT of KIND (enum funds_kind) from WHERE (enum funds_place): subtracted and 1 when there is enough,
 * otherwise the "not enough" message prepared (value_refused) and 0. Original: 29281(kind, amount, where, mode)
 * (F00A/F00E the party's, F012/F016 the bank's). */
int value_take(xeen_game *g, unsigned kind, unsigned where, uint32_t amount)
{
    xeen_movement *m = &g->movement;
    uint16_t at = funds_word(kind, where);
    uint32_t have = xeen_ds_read32(m, at);
    if (have >= amount) {
        xeen_ds_write32(m, at, have - amount);
        return 1;
    }
    value_refused(g, kind, where, 0);
    return 0;
}
/* Pays COST from the party's gold (value_take). Original: 29281(0, cost, 0, mode). */
int party_pay(xeen_game *g, uint32_t cost)
{
    return value_take(g, FUNDS_GOLD, FUNDS_PARTY, cost);
}
