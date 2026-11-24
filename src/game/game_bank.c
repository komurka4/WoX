#include "game_location.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------- the bank (277E5 kind 0)
 * Its own translation unit (Phase 242), over game_internal.h. 27C37 sets the bank up (the
 * location head of game_location.c runs first), 27756 writes its text, 2621E
 * takes D (26E90(0), deposit) and W (26E90(1), withdrawal); every other key
 * returns to the location loop. 26E90 runs in window 23h over the same
 * 26E60 key loop: O gold, E gems, Esc back; the amount is 28ABA's digit
 * entry (the 28876 key loop of the name entry); 29281 takes it from the
 * party or the bank and the other side gets it. Leaving is 28056 (a day).
 * The addresses in the comments are the analysis placement. */
static xeen_status bank_amount(xeen_game *, uint32_t);
static xeen_status bank_redraw(xeen_game *);

/* The bank's funds page into OUT (N bytes): the bank's gold and gems and the party's, each written as an amount
 * (location_amount). Returns 0 when it does not fit. Original: 27756: AC0F = DS:550F of F012, F016, F00A and F00E,
 * each through 2689A. */
int bank_text(xeen_game *g, char *out, size_t n)
{
    xeen_movement *m = &g->movement;
    char a[4][24];
    location_amount(g, a[0], sizeof a[0], xeen_ds_read32(m, XEEN_DS_BANK_GOLD));
    location_amount(g, a[1], sizeof a[1], xeen_ds_read32(m, XEEN_DS_BANK_GEMS));
    location_amount(g, a[2], sizeof a[2], xeen_ds_read32(m, XEEN_DS_PARTY_GOLD));
    location_amount(g, a[3], sizeof a[3], xeen_ds_read32(m, XEEN_DS_PARTY_GEMS));
    int k = snprintf(out, n, ds_text(g, XEEN_TEXT_BANK_BUTTONS), a[0], a[1], a[2], a[3]);
    return k >= 0 && (size_t)k < n;
}
/* The bank's window: its title (Deposit or Withdrawal) and the source's gold and gems (the party's for a deposit,
 * the bank's for a withdrawal), framed for FORMAT XEEN_TEXT_BANK_FUNDS, drawn over the window for _AFTER (after an
 * amount). Original: 26E90's window 35 text (DS:2A44). */
static void bank_window(xeen_game *g, uint16_t format)
{
    xeen_movement *m = &g->movement;
    unsigned source = g->bank_mode - 1u;
    char gold[24], gems[24], out[512];
    location_amount(g, gold, sizeof gold, xeen_ds_read32(m, funds_word(FUNDS_GOLD, source)));
    location_amount(g, gems, sizeof gems, xeen_ds_read32(m, funds_word(FUNDS_GEMS, source)));
    (void)snprintf(out, sizeof out, ds_text(g, format), far_text(g, XEEN_DS_BANK_TITLES, source), gold, gems);
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, out);
    menu_window_text(g, XEEN_WINDOW_BANK, format == XEEN_TEXT_BANK_FUNDS, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
}
/* Opens the bank's Deposit (SOURCE FUNDS_PARTY, the D key) or Withdraw (FUNDS_BANK, W) window: the key code is SOURCE, the bank's key
 * table (DS:1A35) and the location's sprites (98D2) are the buttons', the bank's mode is SOURCE + 1, the window shows
 * the party's funds with its buttons, the voice is cancelled and coina.voc plays; then the location's poll.
 * Original: 2621E: F5FE = 0 or 1, 26E90(F5FE): 989C = 1A35, 9896 = 98D2, the text, 1871B(23h), 4E38C, the buttons,
 * 50BF6(23h), 187E9(0, 0), coina.voc; its 26E60 loop. */
xeen_status bank_begin(xeen_game *g, unsigned source)
{
    xeen_movement *m = &g->movement;
    xeen_write16(m, XEEN_DS_INPUT_CODE, (uint16_t)source);
    menu_far(m, XEEN_DS_BUTTON_KEY_TABLE, 0x1a35);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, xeen_read16(m, 0x98d2));
    g->bank_mode = (uint8_t)(source + 1u);
    panel_window(g, XEEN_WINDOW_BANK, 1);
    bank_window(g, XEEN_TEXT_BANK_FUNDS);
    location_buttons(g);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_BANK);
    voice_cancel(g, 0);
    location_voc(g, ds_text(g, XEEN_TEXT_BANK_COIN_VOICE));
    g->location_sampled = 0;
    return panel_set(g, LOCATION_POLL, LOCATION_POLL_BOUNDARY);
}
/* Esc in a deposit or withdrawal: the location's key table (DS:1A2F) and the dialog sprites as the buttons' back, the
 * key code cleared, the bank window closed and the bank's mode reset; then the location is redrawn and its loop goes
 * on. Original: 27176: 989C = 1A2F, 9896 = 98D4, F5FE = 0, 18765(23h); the loop 255D4, 26247, 27F20. */
static xeen_status bank_close(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    menu_far(m, XEEN_DS_BUTTON_KEY_TABLE, 0x1a2f);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    panel_window(g, XEEN_WINDOW_BANK, 0);
    g->bank_mode = 0;
    return location_redraw(g);
}
/* A key at the bank's deposit or withdrawal: E gems, O gold, Escape leaves; any other key polls again. With none of
 * it at the source, "No %s in the %s!" and the loop; else "Amount" and the amount entry (up to 10 digits).
 * Original: 26FAD (Esc 2716E); 291FB(kind, where, 1, 2); DS:528E and 28ABA(10, 4Dh, 23h). */
xeen_status bank_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    unsigned source = g->bank_mode - 1u, kind;
    if (key == XEEN_KEY_ESCAPE) {
        return bank_close(g);
    }
    if (key != 'E' && key != 'O') {
        g->location_sampled = 0;
        return panel_set(g, LOCATION_POLL, LOCATION_POLL_BOUNDARY);
    }
    kind = key == 'E' ? FUNDS_GEMS : FUNDS_GOLD;
    g->bank_kind = (uint8_t)kind;
    if (!xeen_ds_read32(m, funds_word(kind, source))) {
        value_refused(g, kind, source, 1);
        return location_message(g, LOCATION_MESSAGE_BANK_EMPTY, XEEN_DS_TEXT_BUFFER);
    }
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, ds_text(g, XEEN_TEXT_BANK_AMOUNT));
    menu_window_text(g, XEEN_WINDOW_BANK, WINDOW_TEXT_ONLY, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_BANK);
    g->amount_window = 0x23;
    g->amount_width = 0x4d;
    g->amount_max = 10;
    g->amount_owner = AMOUNT_BANK;
    return amount_begin(g);
}
/* Starts an amount entry in amount_window with amount_width (the bank's 10 digits; Teleport's and Town Portal's one;
 * the arena's two, in its prompt's window): the entry flag set, no digits yet, the empty entry line drawn and shown;
 * then the line entry's key loop with the amount as its owner (no button set of its own).
 * Original: 28ABA(digits, x, window): 8C52 = 1, DS:5871 at x, 28876 (the bank (10, 4Dh, 23h), Teleport (1, C8h, 6),
 * Town Portal (1, A0h, 14h), the arena (2, C8h, 0Bh) in 3000D's window). */
xeen_status amount_begin(xeen_game *g)
{
    char line[64];
    uint16_t w = g->amount_window;
    g->movement.ds[XEEN_DS_TEXT_ENTRY] = 1;
    g->panel_name_length = 0;
    memset(g->bank_digits, 0, sizeof g->bank_digits);
    (void)snprintf(line, sizeof line, ds_text(g, XEEN_TEXT_ENTRY_FIELD), g->amount_width);
    menu_window_text_on(g, w, line);
    input_line(g, w, line);
    (void)xeen_presentation_update_window(g->presentation, w);
    g->name_owner = NAME_AMOUNT;
    return name_poll(g);
}
/* The amount entry's keys. The exploration state the line entry put aside (the monsters' countdown, the sign) comes
 * back first. Backspace or Left deletes the last digit; a digit is appended below amount_max; an edit redraws the line
 * in the amount's window. Enter ends the entry with the digits' decimal value, Esc with 0 (and the entry flag left
 * set, as the original leaves it); either clears the key code and hands the value to the amount's owner: the arena,
 * Teleport, Town Portal or the bank. Any other key keeps polling.
 * Original: 28ABA after a 28876 key: 08h/F0h, '0'..'9' below the limit (28B6E), DS:587F; F5FE = 0; Esc 0 with 8C52
 * still 1, Enter 8C52 = 0 and sscanf("%lu"). */
xeen_status amount_key(xeen_game *g, uint8_t ch)
{
    uint8_t *ds = g->movement.ds;
    unsigned length = g->panel_name_length;
    int changed = 0;
    char line[64];
    uint16_t w = g->amount_window;
    ds[XEEN_DS_MONSTER_COUNTDOWN] = g->panel_name_monster_countdown;
    ds[XEEN_DS_SIGN_SHOWN] = g->panel_name_sign_shown;
    if ((ch == XEEN_KEY_BACKSPACE || ch == XEEN_KEY_LEFT) && length) {
        g->bank_digits[--length] = 0;
        changed = 1;
    } else if (ch >= '0' && ch <= '9' && length < g->amount_max) {
        g->bank_digits[length++] = (char)ch;
        g->bank_digits[length] = 0;
        changed = 1;
    }
    if (changed) {
        (void)snprintf(line, sizeof line, ds_text(g, XEEN_TEXT_ENTRY_FIELD_TEXT), g->amount_width, g->bank_digits);
        menu_window_text_on(g, w, line);
        input_line(g, w, line);
        (void)xeen_presentation_update_window(g->presentation, w);
    }
    g->panel_name_length = length;
    if (ch != XEEN_KEY_ESCAPE && ch != XEEN_KEY_ENTER) {
        return name_poll(g);
    }
    g->name_owner = NAME_GAME;
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
    {
        uint32_t value = 0;
        if (ch == XEEN_KEY_ENTER) {
            ds[XEEN_DS_TEXT_ENTRY] = 0;
            for (unsigned i = 0; i < length; ++i) {
                value = value * 10u + (uint32_t)(g->bank_digits[i] - '0');
            }
        }
        switch (g->amount_owner) {
            case AMOUNT_ARENA:
                return arena_amount(g, value);
            case AMOUNT_TELEPORT:
                return teleport_amount(g, value);
            case AMOUNT_TOWN_PORTAL:
                return portal_amount(g, value);
            case AMOUNT_BANK:
            default:
                return bank_amount(g, value);
        }
    }
}
/* The bank's typed amount VALUE: when nonzero it is taken from the source (the party for a deposit, the bank for a
 * withdrawal; too little shows its message instead) and added to the other side, gold or gems as chosen; then the
 * bank redraws (its coin voice) and its loop goes on.
 * Original: 27050: 29281; then 270D1: 187E9(0, F42C), DS:52A2 and the loop. */
static xeen_status bank_amount(xeen_game *g, uint32_t value)
{
    xeen_movement *m = &g->movement;
    unsigned source = g->bank_mode - 1u, kind = g->bank_kind;
    if (value) {
        if (!value_take(g, kind, source, value)) {
            return location_message(g, LOCATION_MESSAGE_BANK_SHORT, XEEN_DS_TEXT_BUFFER);
        }
        uint16_t to = funds_word(kind, source ? FUNDS_PARTY : FUNDS_BANK); /* the other side */
        xeen_ds_write32(m, to, xeen_ds_read32(m, to) + value);
    }
    return bank_redraw(g);
}
static xeen_status bank_redraw(xeen_game *g)
{
    voice_cancel(g, 0);
    bank_window(g, XEEN_TEXT_BANK_FUNDS_AFTER);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_BANK);
    g->location_sampled = 0;
    return panel_set(g, LOCATION_POLL, LOCATION_POLL_BOUNDARY);
}
/* A bank message closed: too little to move redraws the bank; "No %s in the %s!" goes back to its key loop.
 * Original: 270D1, 2716E. */
xeen_status bank_message_closed(xeen_game *g, unsigned owner)
{
    if (owner == LOCATION_MESSAGE_BANK_SHORT) {
        return bank_redraw(g);
    }
    g->location_sampled = 0;
    return panel_set(g, LOCATION_POLL, LOCATION_POLL_BOUNDARY);
}

