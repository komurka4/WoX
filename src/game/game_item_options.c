/*
 * The items dialog's options: what each does to the row the player picks (equip, buy, sell, fix, identify, discard,
 * remove), the item spells' modes that reuse the dialog (Recharge, Enchant, Item to Gold), and Use (an item's
 * power). The dialog itself (its frame, its text, its keys) is game_shop.c's.
 * Original: 3BAEE (the options), 424C3/422D8 (equip), 3BCF7 (Use); the dialog 44F92.
 */
#include "game_items.h"
#include "xeen/item_tables.h"
#include "xeen/monster_record.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when an item option cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json). A sound stop means the host's audio refused the effect.
 */
enum {
    OPTION_STOP_SPRITES = 0x3bb66,           /* esc.icn */
    OPTION_STOP_UNKNOWN_ACTION = 0x3bcbf,    /* an option that none of the actions serves */
    OPTION_STOP_DISCARD_RETURN = 0x3c8ee,    /* a discard in items mode 8 returns AX 2 to its caller: not served */
    OPTION_STOP_CONFIRM = 0x2937a,           /* the confirmation could not be shown */
    IDENTIFY_ITEM_STOP_DETAILS = 0x3cf19,    /* the item's details could not be formed */
    IDENTIFY_ITEM_STOP_TEXT = 0x3c21d,       /* the window's text does not fit */
    RECHARGE_STOP_DRAW = 0x3c53d,            /* the charges' roll */
    ENCHANT_STOP_GENERATE = 0x3c645,         /* the new item could not be generated */
    ENCHANT_STOP_SOUND = 0x3c67d,
    OPTION_GOLD_STOP_SOUND = 0x3c78a,
    USE_STOP_TEXT = 0x3bd41,                 /* the Use window's text does not fit */
    USE_STOP_SPELL_TEXT = 0x3bf41,           /* the item spell's text does not fit */
    USE_STOP_ENGAGED_SOUND = 0x39b13,        /* "Can't cast ... while engaged!" */
    USE_STOP_ENGAGED_TEXT = 0x39b30,
    /* Use's own volleys (powers 33 and 64, use_power): where each plays its sound effect (cast_volley's last
     * argument, a variable stop site) */
    USE_STOP_FROST_VOLLEY_SOUND = 0x3a5cb,   /* power 33: 35 cold damage at one monster */
    USE_STOP_FIRE_VOLLEY_SOUND = 0x3b4ac,    /* power 64: 250 fire damage at one monster */
    /* variable stop sites (stops_variable.json) */
    RECHARGE_STOP_REFUSED_SOUND = 0x3c584,   /* "Not Rechargeable": the refusal's sound effect */
    ENCHANT_STOP_REFUSED_SOUND = 0x3c703,    /* "Not Enchantable": the refusal's sound effect */
    GOLD_STOP_REFUSED_SOUND = 0x3c75e,       /* the Xeen Slayer Sword refuses Gold: the sound effect */
    GOLD_STOP_ACCESSORY_RESIDUE = 0x3c82f,   /* Gold of an accessory with no modelled poll residue */
    GOLD_STOP_MISC_RESIDUE = 0x3c88c,        /* Gold of a misc item with no modelled poll residue */
    USE_STOP_BEACON_SPRITES = 0x448a1,       /* Use's Lloyd's Beacon: cast.icn could not be loaded */
    USE_STOP_PORTAL_SPRITES = 0x449ad,       /* Use's Town Portal: cast.icn could not be loaded */
    USE_STOP_COMBAT_TAIL = 0x3be0e           /* the step after a power (3BE09..3BE13) did not finish */
};
static xeen_status use_power(xeen_game *, unsigned);
static xeen_status identify_show(xeen_game *);
/* ------------------------------------------------- equip
 * Equipping row I of category CAT is blocked by item OTHER (of OTHER_CAT): the same item does nothing; otherwise the
 * message window says "You must remove <other> to equip <item> !" (their base names) or, for OTHER < 0, "You have
 * equipped all the <rings / medals> you can!" (-1 rings, -2 medals), and waits for a key.
 * Original: 422D8(record, row, other, other's category, row's category), its own 189E3 wait. */
static xeen_status equip_conflict(xeen_game *g, uint16_t record, unsigned i, int other,
                                  unsigned other_cat, unsigned cat)
{
    static const uint16_t names[4] = XEEN_ITEM_NAME_TABLES;
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char out[0x6c];
    if (other == (int)i && other_cat == cat) {
        return items_after_option(g, 0);
    }
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (other < 0) {
        (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_EQUIP_ALL),
                       ds_text(g, other == -1 ? XEEN_TEXT_EQUIP_RING : XEEN_TEXT_EQUIP_MEDAL));
    } else {
        (void)snprintf(
                out, sizeof out, ds_text(g, XEEN_TEXT_EQUIP_REMOVE_FIRST),
                far_text(g, names[other_cat], item_key(g, record, other_cat, (unsigned)other)),
                far_text(g, names[cat], item_key(g, record, cat, i)));
    }
    message_window(g, out);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    g->message_owner = MESSAGE_EQUIP_WAIT;
    return panel_set(g, MESSAGE_POLL, EQUIP_MESSAGE_BOUNDARY);
}
/* Equips row I of category CAT for RECORD (xeen_item_equip): a class that cannot use it gets the "not proficient"
 * scroll; a slot already taken, the conflict message (equip_conflict). Original: 424C3(record, row, category); the
 * refusal 46E56. */
static xeen_status equip_item(xeen_game *g, uint16_t record, unsigned i, unsigned cat)
{
    xeen_equip_result r = xeen_item_equip(&g->movement, record, i, cat);
    char out[0x6a];
    switch (r.outcome) {
        case XEEN_EQUIP_REFUSED:
            item_not_proficient(g, g->movement.ds[(uint16_t)(record + XEEN_CHARACTER_CLASS)],
                                item_key(g, record, cat, i), cat, out, sizeof out);
            return scroll0_open(g, MESSAGE_EQUIP_REFUSED, out);
        case XEEN_EQUIP_CONFLICT:
        case XEEN_EQUIP_ALL:
            return equip_conflict(g, record, i, r.other, r.other_category, cat);
        case XEEN_EQUIP_DONE:
            ++g->equips;
            break;
        default:
            break;
    }
    return items_after_option(g, 0);
}

/* ------------------------------------------------- the options
 * Starts option ACTION on the dialog's category: an empty category resets the dialog and nothing else happens.
 * Otherwise portrait clicks are off while the option runs and, when no row is chosen yet, "<Action> which item?"
 * asks for one in the item-action window (keys 1..9 on occupied rows, 0 or Esc cancel) before the option runs
 * (option_run). Portrait clicks come back when it ends (items_after_option).
 * Original: 3BAEE(record, action, row, category, mode): AX 0 for an empty category; 98D6 = 0; window 0Dh; 3C8F3
 * 98D6 = 1, AX 0. */
static xeen_status option_run(xeen_game *g);
xeen_status option_begin(xeen_game *g, int action)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char out[64];
    uint16_t h = 0;
    if (!item_key(g, g->items_char, (unsigned)g->items_cat, 0)) {
        return items_reset(g);
    }
    ds[XEEN_DS_PORTRAIT_CLICKS] = 0;
    g->option_action = action;
    g->option_index = g->items_index;
    if (g->option_index >= 0 && g->option_index <= 8) {
        return option_run(g);
    }
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (!handle_push(g, ds_text(g, XEEN_TEXT_ITEM_ESC_SPRITES), &h)) {
        return shop_stop(g, OPTION_STOP_SPRITES);
    }
    button_tables(m, XEEN_BUTTONS_ESC_1_9);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, h);
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_ITEM_WHICH), far_text(g, XEEN_DS_ITEM_ACTION_NAMES, (unsigned)action));
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, out);
    panel_window(g, XEEN_WINDOW_ITEM_ACTION, 1);
    menu_window_text(g, XEEN_WINDOW_ITEM_ACTION, WINDOW_FRAMED, out);
    exit_draw(g, h, 0, 235, 111, 0);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_ITEM_ACTION);
    return panel_set(g, OPTION_WHICH, OPTION_WHICH_BOUNDARY);
}
/** Answers an option's "which item" prompt with KEY (Escape cancels; 1..9 picks the row). */
xeen_status option_which_key(xeen_game *g, uint16_t key)
{
    int row;
    if (!key || key == XEEN_KEY_ESCAPE) {
        row = -1;
    } else if (key < '1' || key > '9') {
        return panel_set(g, OPTION_WHICH, OPTION_WHICH_BOUNDARY);
    } else {
        row = key - '1';
        if (!item_key(g, g->items_char, (unsigned)g->items_cat, (unsigned)row)) {
            return panel_set(g, OPTION_WHICH, OPTION_WHICH_BOUNDARY);
        }
    }
    window_pop(g, 13);
    if (row < 0) {
        return items_after_option(g, 0);
    }
    g->option_index = row;
    return option_run(g);
}
static xeen_status option_confirm(xeen_game *, enum option_owner, uint16_t, uint32_t, unsigned);
/* Buy: a buyer (the acting character) whose last row of the category is taken is refused ("<name>'s backpack is
 * full."); otherwise "Buy <item> for <price> gold?" (the shop's price, no skill) asks to confirm, and the purchase
 * itself waits for the answer (option_bought). Original: 3C266 (9800 the buyer; 46FBD, skill 0; colour 15; 2937A,
 * owner 7). */
static xeen_status option_buy(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    char out[0x3e8];
    unsigned cat = (unsigned)g->items_cat, i = (unsigned)g->option_index;
    uint16_t buyer = xeen_read16(m, XEEN_DS_ACTING_CHARACTER);
    if (item_key(g, buyer, cat, 8)) {
        (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_ITEM_BACKPACK_FULL), ds_text(g, buyer));
        ds_put_text(g, XEEN_DS_TEXT_BUFFER_0, out);
        return scroll0_open(g, MESSAGE_ITEM_OPTION, ds_text(g, XEEN_DS_TEXT_BUFFER_0));
    }
    return option_confirm(g, OPTION_OWNER_BUY, XEEN_TEXT_OPTION_BUY_CONFIRM, item_cost(g, g->items_char, i, ITEMS_BUY, 0, cat), 15);
}
/*
 * An option's confirmation answered (ANSWER 0: no, which ends the option). Buy: the party pays (too little: the "not
 * enough" scroll), Darkside's shopkeeper voice plays, the ware moves from the stock row into the buyer's last row
 * (unequipped) and both lists are sorted. Sell: the row cleared, its price added to the party's gold, the list
 * sorted. Fix: the party pays, the broken bit cleared. Identify: the party pays, then the details (identify_show).
 * Discard: the row cleared and the list sorted (a discard in items mode 8 would return to a caller that is not
 * reconstructed, and stops).
 * Original: 2937A's owners 7..11: Buy 29281 (291FB), choice2.voc, 43385, 43428; Sell 3BEF9; Fix 3C000; Identify
 * 3C1CC; Discard 3BA27 (3C8EE).
 */
xeen_status option_bought(xeen_game *g, int answer)
{
    xeen_movement *m = &g->movement;
    unsigned cat = (unsigned)g->items_cat, i = (unsigned)g->option_index;
    uint16_t record = g->items_char;
    enum option_owner owner = (enum option_owner)g->option_owner;
    if (!answer) {
        return items_after_option(g, 0);
    }
    switch (owner) {
        case OPTION_OWNER_BUY: {
            uint16_t buyer = xeen_read16(m, XEEN_DS_ACTING_CHARACTER);
            uint8_t *from, *to;
            if (!party_pay(g, g->option_cost)) {
                return scroll0_open(g, MESSAGE_PAYMENT, ds_text(g, XEEN_DS_TEXT_BUFFER));
            }
            if (m->ds[XEEN_DS_WORLD]) {
                exit_event(g, ds_text(g, XEEN_TEXT_SHOP_BUY_VOICE));
            }
            from = item_at(g, record, cat, i);
            to = item_at(g, buyer, cat, 8);
            memcpy(to, from, 3u);
            if (cat < 3u) {
                to[XEEN_ITEM_ROW_EQUIPPED] = 0;
            }
            item_clear(g, record, cat, i);
            item_sort(g, record, cat);
            item_sort(g, buyer, cat);
            ++g->purchases;
            break;
        }
        case OPTION_OWNER_SELL:
            item_clear(g, record, cat, i);
            xeen_ds_write32(m, XEEN_DS_PARTY_GOLD,
                            xeen_ds_read32(m, XEEN_DS_PARTY_GOLD) + g->option_cost);
            item_sort(g, record, cat);
            break;
        case OPTION_OWNER_FIX:
            if (!party_pay(g, g->option_cost)) {
                return scroll0_open(g, MESSAGE_PAYMENT, ds_text(g, XEEN_DS_TEXT_BUFFER));
            }
            item_at(g, record, cat, i)[XEEN_ITEM_ROW_STATE] &= (uint8_t)~XEEN_ITEM_BROKEN;
            break;
        case OPTION_OWNER_IDENTIFY: /* 3C1CC */
            if (!party_pay(g, g->option_cost)) {
                return scroll0_open(g, MESSAGE_PAYMENT, ds_text(g, XEEN_DS_TEXT_BUFFER));
            }
            return identify_show(g);
        default: /* OPTION_OWNER_DISCARD */
            item_clear(g, record, cat, i);
            item_sort(g, record, cat);
            if (g->items_mode == 8) {
                return shop_stop(g, OPTION_STOP_DISCARD_RETURN);
            }
            break;
    }
    return items_after_option(g, 0);
}
/* Asks to confirm OWNER's option on the chosen row: FORMAT with the item's description in COLOUR and, but for a
 * discard, COST (a fix or identify names its service too); the cost and owner wait for the answer (option_bought).
 * A confirmation that cannot be shown stops. Original: 2937A with AC0F (owners 7..11). */
static xeen_status option_confirm(xeen_game *g, enum option_owner owner, uint16_t format, uint32_t cost,
                                  unsigned colour)
{
    char out[0x3e8];
    unsigned cat = (unsigned)g->items_cat, i = (unsigned)g->option_index;
    const char *desc = item_text(g, g->items_char, i, colour, cat);
    if (owner == OPTION_OWNER_FIX || owner == OPTION_OWNER_IDENTIFY) {
        (void)snprintf(out, sizeof out, ds_text(g, format),
                       ds_text(g, owner == OPTION_OWNER_FIX ? XEEN_TEXT_OPTION_FIX : XEEN_TEXT_OPTION_IDENTIFY), desc, (unsigned long)cost);
    } else if (owner == OPTION_OWNER_DISCARD) {
        (void)snprintf(out, sizeof out, ds_text(g, format), desc);
    } else {
        (void)snprintf(out, sizeof out, ds_text(g, format), desc, (unsigned long)cost);
    }
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, out);
    g->option_cost = cost;
    g->option_owner = owner;
    return open_confirm(g, CONFIRM_BUY, XEEN_DS_TEXT_BUFFER_2) ? XEEN_SERVICE_REQUIRED : shop_stop(g, OPTION_STOP_CONFIRM);
}
/* Sell: a cursed item, or the Xeen Slayer Sword, gets "We have no need of this <item>!"; otherwise "Sell <item> for
 * <price> gold?", the price with the member's merchant skill. Original: 3BE64 (owner 8). */
static xeen_status option_sell(xeen_game *g)
{
    unsigned cat = (unsigned)g->items_cat, i = (unsigned)g->option_index;
    const uint8_t *s = item_at(g, g->items_char, cat, i);
    char out[0x3e8];
    if ((s[2] & XEEN_ITEM_CURSED) || (cat == XEEN_ITEMS_WEAPONS && s[1] == XEEN_WEAPON_XEEN_SLAYER_SWORD)) {
        (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_ITEM_NOT_NEEDED),
                       item_text(g, g->items_char, i, 15, cat));
        ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, out);
        return scroll0_open(g, MESSAGE_ITEM_OPTION, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    }
    return option_confirm(g, OPTION_OWNER_SELL, XEEN_TEXT_OPTION_SELL_CONFIRM,
                          item_cost(g, g->items_char, i, ITEMS_SELL,
                                    g->movement.ds[(uint16_t)(g->items_char + XEEN_CHARACTER_SKILLS +
                                                              XEEN_SKILL_MERCHANT)],
                                    cat),
                          15);
}
/* Fix: an item that is not broken gets "That item is not broken!"; otherwise "Fix <item> for <price> gold?".
 * Original: 3BF9D, 46F4D (owner 9). */
static xeen_status option_fix(xeen_game *g)
{
    unsigned cat = (unsigned)g->items_cat, i = (unsigned)g->option_index;
    if (!(item_at(g, g->items_char, cat, i)[XEEN_ITEM_ROW_STATE] & XEEN_ITEM_BROKEN)) {
        return scroll0_open(g, MESSAGE_ITEM_OPTION, ds_text(g, XEEN_TEXT_ITEM_NOT_BROKEN));
    }
    return option_confirm(g, OPTION_OWNER_FIX, XEEN_TEXT_OPTION_SERVICE_CONFIRM,
                          item_cost(g, g->items_char, i, ITEMS_FIX, (unsigned)g->option_action, cat), 15);
}
/*
 * Identify: the price, with the action as its skill, and "Identify %s for %lu gold?"; on yes the party pays (too
 * little: the "not enough" scroll and the dialog's redraw), the item's details are formed under its description and
 * shown in the Identify window until a key; then the dialog redraws. The item's bytes do not change: nothing marks
 * an item identified.
 * Original: 3C199 (mode 10): 46F4D(record, row, 10, action, 1, category): 46FBD, A43F = 475F7 (colour 15), AC0F with
 * 844F; 2937A (owner 11). Yes: 29281 (291FB, 3C8F3); 3C96E (50D4:17ED), AC0F = DS:6C63; 1871B(0Eh), 4E38C(0Eh, AC0F),
 * 50BF6(0Eh), 190ED, 1919B, 189E3, 18FFF, 18765(0Eh); 3C8F3.
 */
static xeen_status option_identify(xeen_game *g)
{
    unsigned cat = (unsigned)g->items_cat, i = (unsigned)g->option_index;
    return option_confirm(g, OPTION_OWNER_IDENTIFY, XEEN_TEXT_OPTION_SERVICE_CONFIRM,
                          item_cost(g, g->items_char, i, ITEMS_IDENTIFY, (unsigned)g->option_action, cat), 15);
}
static xeen_status identify_show(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned cat = (unsigned)g->items_cat, i = (unsigned)g->option_index;
    char details[0x3e8], out[0x3e8];
    int n;
    {
        const uint8_t *row = item_at(g, g->items_char, cat, i);
        const char *spell =
                cat == XEEN_ITEMS_MISC && row[1]
                        ? spell_name(g, ds[(uint16_t)(XEEN_DS_MISC_POWER_SPELLS + row[1])])
                        : NULL;
        if (!xeen_item_details(m, g->items_char, i, cat, spell, details, sizeof details)) {
            return shop_stop(g, IDENTIFY_ITEM_STOP_DETAILS);
        }
        if (addons_items(g)) {
            (void)xeen_addons_item_learn(g->addons, cat, row); /* add-on identify */
        }
    }
    n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_ITEM_IDENTIFY), item_text(g, g->items_char, i, 15, cat),
                 details);
    if (n < 0 || (size_t)n >= sizeof out) {
        return shop_stop(g, IDENTIFY_ITEM_STOP_TEXT);
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    xeen_presentation_trace("mark spell-begin 95"); /* Evidence: 3C1E7 */
    panel_window(g, XEEN_WINDOW_ITEM_IDENTIFY, 1);
    menu_window_text(g, XEEN_WINDOW_ITEM_IDENTIFY, WINDOW_FRAMED, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_ITEM_IDENTIFY);
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    ++g->identifies;
    return panel_set(g, IDENTIFY_POLL_STAGE, IDENTIFY_ITEM_BOUNDARY);
}
/**
 * Continues after the Identify message closed: pops the button set and restores window 0Eh and
 * returns to the items dialog.
 */
xeen_status identify_closed(xeen_game *g)
{
    xeen_button_stack(g->movement.ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    panel_window(g, XEEN_WINDOW_ITEM_IDENTIFY, 0);
    xeen_presentation_trace("mark spell-end 95");                     /* Evidence: 3C263 */
    return items_after_option(g, 0);
}
/* Discard: a cursed item gets "You cannot discard a cursed item!", the Xeen Slayer Sword its own refusal; otherwise
 * "Permanently discard <item>?" (colour 4). Original: 3B958 (owner 10). */
static xeen_status option_discard(xeen_game *g)
{
    unsigned cat = (unsigned)g->items_cat, i = (unsigned)g->option_index;
    const uint8_t *s = item_at(g, g->items_char, cat, i);
    if (s[2] & XEEN_ITEM_CURSED) {
        return scroll0_open(g, MESSAGE_ITEM_OPTION, ds_text(g, XEEN_TEXT_ITEM_CURSED_DISCARD));
    }
    if (cat == XEEN_ITEMS_WEAPONS && s[1] == XEEN_WEAPON_XEEN_SLAYER_SWORD) {
        return scroll0_open(g, MESSAGE_ITEM_OPTION, ds_text(g, XEEN_TEXT_ITEM_KEEP_XEEN_SLAYER));
    }
    return option_confirm(g, OPTION_OWNER_DISCARD, XEEN_TEXT_OPTION_DISCARD_CONFIRM, 0, 4);
}
/* Remove (unequip): a misc item has no equipped slot and nothing happens; a cursed item gets "You cannot remove a
 * cursed item!"; otherwise its equipped-slot byte is cleared. Original: 3BA59. */
static xeen_status option_remove(xeen_game *g)
{
    uint8_t *s = item_at(g, g->items_char, (unsigned)g->items_cat, (unsigned)g->option_index);
    if (g->items_cat == XEEN_ITEMS_MISC) {
        return items_after_option(g, 0);
    }
    if (s[2] & XEEN_ITEM_CURSED) {
        return scroll0_open(g, MESSAGE_ITEM_OPTION, ds_text(g, XEEN_TEXT_ITEM_CURSED_REMOVE));
    }
    s[3] = 0;
    return items_after_option(g, 0);
}
/* ------------------------------------- the item spells' modes
 * Recharge, Enchant and Item to Gold act on the chosen item whatever the action (the E key in Item to Gold's dialog
 * is the Equip action: Gold all the same). Every outcome closes the items dialog (no redraw of it, the portrait
 * clicks not restored). No payment is returned on a refusal.
 * Original: 3BAEE by mode (CS:0FF6): 4 Recharge (3C4EE), 6 Enchant (3C5B6), 0Bh Item to Gold (3C71D); every outcome
 * answers AX 2 (3C8EE): 44F92 ends at 461EA (18765(1Eh), 18765(1Dh), 46206), no 3C8F3 98D6 = 1. */
/* An item spell refused: the refusal sound, then FORMAT with "Spell Failed!" in a message scroll (the item spell's
 * owner). Stops at BOUNDARY when the sound cannot play.
 * Original: 1880E(15h); A43F = the format with DS:281A in an 18F2F mode-0 scroll (owner 6). */
static xeen_status option_spell_refused(xeen_game *g, uint16_t format, uint32_t boundary)
{
    char out[0x80];
    if (sound_effect(g, XEEN_CUE_REFUSED) !=
        XEEN_OK) {
        return shop_stop(g, boundary);
    }
    (void)snprintf(out, sizeof out, ds_text(g, format), far_text(g, XEEN_DS_SPELL_FAILED_TEXT, 0));
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_0, out);
    return scroll0_open(g, MESSAGE_ITEM_SPELL, ds_text(g, XEEN_DS_TEXT_BUFFER_0));
}
/* Recharge Item: a misc item whose material byte is at most 9 and whose power is neither none nor 35h gets 1..6
 * charges (the state's low six bits): when the state without its 40h bit plus the draw passes 63 all six are set
 * (the broken bit kept), else the draw is added to the byte as it is. Any other item is refused ("Not
 * Rechargeable."). Then the items dialog ends.
 * Original: 3C4EE: "Not Rechargeable.  %s" (6CDB); 504DA(1, 6) (AL = row x 4), 1880E(14h); OR 3Fh / byte ADD. */
static xeen_status option_recharge(xeen_game *g)
{
    unsigned i = (unsigned)g->option_index;
    uint8_t *s = item_at(g, g->items_char, XEEN_ITEMS_MISC, i);
    uint16_t v = 0;
    if (g->items_cat != XEEN_ITEMS_MISC || s[0] > 9u || s[1] == 0x35u || !s[1]) {
        return option_spell_refused(g, XEEN_TEXT_OPTION_NOT_RECHARGEABLE, RECHARGE_STOP_REFUSED_SOUND);
    }
    (void)xeen_entity_rng_range(g->event_random(g->event_random_user, (uint16_t)((4u * i) & 255u)),
                                1u, 6u, &v);
    if (sound_effect(g, 20u) !=
        XEEN_OK) {
        return shop_stop(g, RECHARGE_STOP_DRAW);
    }
    if ((s[2] & 0xbfu) + v > 63u) {
        s[2] |= 0x3fu;
    } else {
        s[2] = (uint8_t)(s[2] + v);
    }
    ++g->item_spells;
    return items_end(g, 1);
}
/* Enchant Item: a level is drawn first, 1..(the caster's level / 5 + 1), at most 5 (compared signed), even when the
 * item is then refused. A weapon or armour with no enchantment byte and no state (and not the Xeen Slayer Sword)
 * takes the enchantment byte and state of an item generated at that level into the scratch record's first row of
 * its category; a level 1 item has neither, so the row stays as it was. Any other item is refused ("Not
 * Enchantable."). Then the sound and the items dialog's end.
 * Original: 3C5B6: 13FE7 clears the BC6A scratch record (162h bytes); 504DA(1, 2C909(9800) / 5 + 1) (AL the bound);
 * 305E8(v, BC6A, 0, 1 or 2); 1880E(13h); "Not Enchantable.  %s" (6B16); weapon 22h. */
static xeen_status option_enchant(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned cat = (unsigned)g->items_cat, i = (unsigned)g->option_index;
    uint8_t *s = item_at(g, g->items_char, cat, i), item[3];
    uint16_t bound, v;
    memset(ds + XEEN_DS_SCRATCH_RECORD, 0, XEEN_CHARACTER_BYTES);
    bound = (uint16_t)(xeen_character_current_level(ds + xeen_read16(m, XEEN_DS_ACTING_CHARACTER)) /
                               5u +
                       1u);
    v = newgame_draw(g, 1u, bound, bound);
    if ((int16_t)v > 5) {
        v = 5;
    }
    if (cat > 1u || s[0] || s[2] || (cat == XEEN_ITEMS_WEAPONS && s[1] == XEEN_WEAPON_XEEN_SLAYER_SWORD)) {
        return option_spell_refused(g, XEEN_TEXT_OPTION_NOT_ENCHANTABLE, ENCHANT_STOP_REFUSED_SOUND);
    }
    if (xeen_item_generate_reason(m, (uint8_t)v, cat + 1u, newgame_draw, g, item) == 0xffu) {
        return shop_stop(g, ENCHANT_STOP_GENERATE);
    }
    s[0] = ds[XEEN_DS_SCRATCH_RECORD + XEEN_CHARACTER_ITEM(cat, 0)];
    s[2] = ds[XEEN_DS_SCRATCH_RECORD + XEEN_CHARACTER_ITEM(cat, 0) + XEEN_ITEM_ROW_STATE];
    if (sound_effect(g, 19u) != XEEN_OK) {
        return shop_stop(g, ENCHANT_STOP_SOUND);
    }
    ++g->item_spells;
    return items_end(g, 1);
}
/*
 * Item to Gold: the Xeen Slayer Sword is refused ("Spell Failed!"); an empty tested row ends silently; else the
 * sound, the party's gold plus the item's value (32-bit, wrapping), the row cleared (a misc item's three bytes) and
 * the list sorted. Cursed and equipped items are converted all the same. A preserved quirk: the accessory and misc
 * branches test a row the dialog never wrote on this path — the stack residue of the items dialog's last key poll
 * (input_poll.last_residue): 8722h after the G button's key, so the test reads the record's byte at +1C88h + EFh
 * (the accessory id) or + 112h (misc byte 0), 16-bit; 4853h after the E key (+214Ch), 581Dh after a click on G
 * (+6074h). Any other residue (none reaches this branch) is an explicit stop.
 * Original: 3C71D (CS:0FAE): weapon 22h, DS:6B35; 1880E(14h); F00A += 46FBD(record, row, 0Bh, 1, category) (ADD/ADC);
 * 43428; the branches 3C82F and 3C88C test [BP-4], which 3BAEE never writes here; 44F92's last 189E3.
 */
static xeen_status option_gold(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned cat = (unsigned)g->items_cat, i = (unsigned)g->option_index;
    uint8_t *s = item_at(g, g->items_char, cat, i);
    if (cat == XEEN_ITEMS_WEAPONS && s[1] == XEEN_WEAPON_XEEN_SLAYER_SWORD) {
        return option_spell_refused(g, XEEN_TEXT_OPTION_SPELL_FAILED, GOLD_STOP_REFUSED_SOUND);
    }
    if (cat < 2u) {
        if (!s[1]) {
            return items_end(g, 1);
        }
    } else {
        if (!g->option_residue) {
            return shop_stop(g, cat == 2u ? GOLD_STOP_ACCESSORY_RESIDUE : GOLD_STOP_MISC_RESIDUE);
        }
        /* the stale option row's key byte (an accessory's id, a misc item's byte 0) */
        if (!ds[(uint16_t)(g->items_char + 4u * g->option_residue +
                           (cat == 2u ? XEEN_CHARACTER_ITEM(XEEN_ITEMS_ACCESSORIES, 0) + XEEN_ITEM_ROW_ID
                                      : XEEN_CHARACTER_ITEM(XEEN_ITEMS_MISC, 0)))]) {
            return items_end(g, 1);
        }
    }
    if (sound_effect(g, 20u) != XEEN_OK) {
        return shop_stop(g, OPTION_GOLD_STOP_SOUND);
    }
    xeen_ds_write32(m, XEEN_DS_PARTY_GOLD,
                    xeen_ds_read32(m, XEEN_DS_PARTY_GOLD) +
                            item_cost(g, g->items_char, i, ITEMS_GOLD, 1u, cat));
    item_clear(g, g->items_char, cat, i);
    item_sort(g, g->items_char, cat);
    ++g->item_spells;
    return items_end(g, 1);
}
/* ------------------------------------------- Use
 * Use (U, misc items only; the dialog's mode 0, and mode 7 runs as 0 in combat) refuses with a scroll, then the
 * dialog's redraw: on a map that forbids it ("What's your hurry?..."); a member whose worst condition is 8 or 11..15
 * (the "cannot act" text with the name); the character view's I in combat ("To use an item in Combat..."); an item
 * without a power, without charges or cursed or broken ("%s has no special abilities!", with its description).
 * Otherwise a charge is spent, the item's owner becomes the acting character, the list, its buttons and the upper
 * screen close, and the power runs (use_power); then an item with no charge left is removed, the monsters move
 * (97F9 cleared first), nobody's turn, portrait clicks on, and the dialog returns no item (no window closes).
 * Original: 3BCF7, 3BAEE action 2: ED99 0 (6B61); 2AB96 (DS:[18CA], A43F); 97C6 (6B97); byte 1 0, state & 3Fh, C0h
 * (6BDA, 475F7 colour 15); 18F2F mode 0 and 3C8F3; [BP-4]; 9800; 18765(1Eh), (1Dh), (18h); 4478B(power); 3BDE3:
 * 43385, 43428, 97F9 = 0, 2A701, 97FF = FFh, 98D6 = 1, AX 1; 44F92 returns NULL at 46206.
 */
static xeen_status option_use(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t record = g->items_char;
    unsigned i = (unsigned)g->option_index;
    uint8_t worst;
    uint8_t *s = item_at(g, record, XEEN_ITEMS_MISC, i);
    char out[0x3e8];
    int n;
    xeen_presentation_trace("mark spell-begin 96");
    g->use_marked = 1; /* Evidence: 3BCF7 */
    if (!ds[XEEN_DS_PARTY_MAP]) {
        return scroll0_open(g, MESSAGE_ITEM_OPTION, ds_text(g, XEEN_TEXT_ITEM_USE_HURRY));
    }
    worst = xeen_character_worst_condition(ds + record);
    if (xeen_condition_cannot_act(worst)) {
        n = snprintf(out, sizeof out, far_text(g, XEEN_DS_MEMBER_CANNOT_ACT_TEXT, 0), ds_text(g, record));
        if (n < 0 || (size_t)n >= sizeof out) {
            return shop_stop(g, USE_STOP_TEXT);
        }
        memcpy(ds + XEEN_DS_TEXT_BUFFER_0, out, (size_t)n + 1u);
        return scroll0_open(g, MESSAGE_ITEM_OPTION, ds_text(g, XEEN_DS_TEXT_BUFFER_0));
    }
    if (ds[XEEN_DS_VIEW_ITEMS_IN_COMBAT]) {
        return scroll0_open(g, MESSAGE_ITEM_OPTION, ds_text(g, XEEN_TEXT_ITEM_USE_IN_COMBAT));
    }
    if (!s[1] || !(s[2] & 0x3fu) || (s[2] & 0xc0u)) {
        n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_ITEM_NO_ABILITIES), item_text(g, record, i, 15, 3));
        if (n < 0 || (size_t)n >= sizeof out) {
            return shop_stop(g, USE_STOP_SPELL_TEXT);
        }
        memcpy(ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
        return scroll0_open(g, MESSAGE_ITEM_OPTION, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    }
    g->use_charges = (int)(s[2] & 0x3fu) - 1;
    s[2] = (uint8_t)g->use_charges;
    xeen_write16(m, XEEN_DS_ACTING_CHARACTER + 2u, XEEN_DS_SEGMENT);
    xeen_write16(m, XEEN_DS_ACTING_CHARACTER, record);
    panel_window(g, XEEN_WINDOW_LIST, 0);
    panel_window(g, XEEN_WINDOW_LIST_BUTTONS, 0);
    panel_window(g, XEEN_WINDOW_UPPER_SCREEN, 0);
    g->use_record = record;
    g->use_row = i;
    g->use_power = s[1];
    return use_power(g, s[1]);
}
/* Use's two powers that are no spell of the table: fixed volleys at one monster (use_power). */
enum {
    USE_POWER_FROST_VOLLEY = 33, /* 35 cold damage */
    USE_POWER_FIRE_VOLLEY = 64   /* 250 fire damage */
};
/*
 * Use's item powers 1..73 call a spell's handler directly (with the power in AX); powers 33 and 64 are two volleys of
 * their own (Frost Bite's and Incinerate's numbers, with their own damage, sound and icon). In combat Jump, Wizard
 * Eye, Lloyd's Beacon, Teleport, Super Shelter, Town Portal and Etherealize are refused as "engaged" instead, with no
 * refund (the charge is gone) — unlike the Cast dialog, Enchant Item is not refused here. Town Portal then runs all
 * the same, without its sprites (an original quirk): its town menu is native, then explicit stops: a town (a map
 * load inside combat), Esc (the unmatched sprite release frees the fight's handles). Outside combat Lloyd's Beacon
 * and Town Portal load the Cast dialog's sprites around the handler; the item spells (Enchant, Recharge) open the
 * items dialog inside this one (items_outer). Any other power does nothing.
 * Original: 4478B(power) by the table CS:0474; 3A5B7 (9730 23h, 972E 4, 972C 0, 1880E(0Fh), 281A7(9)) and 3B498
 * (250, 2, 0, 1880E(0Dh), 281A7(2)); in combat (9A1C 2) 39AF6(spell); Town Portal 449A7 -> 449B5's 3AF94 without
 * 190A3, the town 3B0AC (19E82), Esc 449BA (190CF); outside combat 190A3(cast.icn) around the handler.
 */
static xeen_status use_engaged(xeen_game *g, unsigned spell);
static xeen_status use_power(xeen_game *g, unsigned power)
{
    /* Use's own table of powers 1..73 (the original's 4478B does not read DS:28D6): XEEN_DS_MISC_POWER_SPELLS'
     * entries 1..73 except powers 33 and 64 (Frost Bite and Incinerate there), FFh here: Use casts them as its own
     * fixed volleys below (checked against XEEN.DAT). */
    static const uint8_t spell[73] = {42, 1,  45,  26,  27, 20, 61, 59, 10, 62, 60, 38, 72, 55, 50,
                                      40, 2,  4,   73,  41, 75, 3,  34, 43, 31, 52, 49, 44, 53, 30,
                                      33, 74, 255, 15,  25, 5,  9,  0,  70, 17, 7,  69, 24, 8,  29,
                                      51, 14, 66,  12,  13, 6,  23, 57, 22, 71, 64, 56, 21, 11, 48,
                                      46, 54, 19,  255, 32, 58, 18, 47, 37, 65, 35, 63, 16};
    uint8_t *ds = g->movement.ds;
    unsigned id;
    ++g->item_uses;
    if (power < 1u || power > sizeof spell) {
        return use_after_power(g);
    }
    id = spell[power - 1u];
    g->use_active = 1;
    g->cast_active = 1;
    g->cast_mode = ds[XEEN_DS_UI_MODE];
    g->cast_spell = id;
    g->handler_ax = (uint16_t)power;
    xeen_presentation_trace("mark use-begin %u", power);
    if (g->use_marked) {
        g->use_marked = 0;
        xeen_presentation_trace("mark spell-end 96");
    }
    /* Original: 3BDDE, 4478B next. */
    /* In combat an item refuses the spells that only work exploring, except Enchant Item: its power runs. */
    if (ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_COMBAT && exploration_only_spell(id) && id != XEEN_SPELL_ENCHANT_ITEM) {
        return use_engaged(g, id);
    }
    /* cast_volley(damage, type, mode, sound effect, volley icon, the sound's stop) */
    if (power == USE_POWER_FROST_VOLLEY) {
        return cast_volley(g, 35, XEEN_ATTACK_COLD, XEEN_VOLLEY_ONE, 15, 9, USE_STOP_FROST_VOLLEY_SOUND);
    }
    if (power == USE_POWER_FIRE_VOLLEY) {
        return cast_volley(g, 250, XEEN_ATTACK_FIRE, XEEN_VOLLEY_ONE, 13, 2, USE_STOP_FIRE_VOLLEY_SOUND);
    }
    if (id == XEEN_SPELL_LLOYDS_BEACON || id == XEEN_SPELL_TOWN_PORTAL) {
        /* 4489D/449A9: 190A3(cast.icn) (96C1 + 1, 95A8[96C1] = 98D4, 98D4
         * = the handle) for the handler's own pops and pushes around 19E82;
         * 449BA's 190CF follows its RETF (use_after_power). */
        uint16_t h = 0;
        if (!handle_push(g, ds_text(g, XEEN_TEXT_CAST_SPRITES), &h)) {
            ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(ds[XEEN_DS_HANDLE_DEPTH] - 1u);
            return shop_stop(g, id == XEEN_SPELL_LLOYDS_BEACON ? USE_STOP_BEACON_SPRITES : USE_STOP_PORTAL_SPRITES);
        }
    }
    /* Enchant Item (power 63) and Recharge Item (53): 4478B calls
     * 3B443/3B1C5 itself (no dispatcher, so no engaged refusal, in combat
     * too: cast_dispatch skips it for use_active); their 399FA and 44F92
     * (mode 6 or 4) run inside this dialog (items_outer). */
    return cast_dispatch(g, id);
}
/* Use refuses an item's spell that cannot be cast in combat: the button set pushed, the UI mode saved and FFh, the
 * refusal sound and "Can't cast %s while engaged!" with the spell's name in the message window; then the Cast
 * dialog's refusal wait (CAST_LACK), whose close (cast_lack_closed) puts the mode back. Stops when the sound cannot
 * play or the text cannot be built.
 * Original: 39AF6(spell) from 4478B: 190ED, 1919B, 9A1C, 1880E(15h), DS:6887 in window 6, 189BC, the busy 189E3,
 * 18765(6), 189BC, 18FFF. */
static xeen_status use_engaged(xeen_game *g, unsigned spell)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char out[160];
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    g->saved_ui_mode = ds[XEEN_DS_UI_MODE];
    ds[XEEN_DS_UI_MODE] = 0xff;
    if (sound_effect(g, XEEN_CUE_REFUSED) != XEEN_OK) {
        return panel_stop(g, USE_STOP_ENGAGED_SOUND);
    }
    if (snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_CAST_ENGAGED), spell_name(g, spell)) < 0) {
        return panel_stop(g, USE_STOP_ENGAGED_TEXT);
    }
    message_window(g, out);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    return panel_set(g, CAST_LACK, CAST_ENGAGED_BOUNDARY);
}
/* An item's power, cast through Use, has returned from its spell handler: the cast is over; the item's sprites are
 * popped for the two powers that pushed them (28, 55; 55 in combat also arms the driver check, which runs combat's
 * Use tail); a misc item without charges left is removed and the pack sorted. In combat (or from combat's items
 * dialog) combat's own Use tail follows (use_done); exploring, the monsters update (delivering queued shots) and no
 * one has the turn. Then the portrait clicks come back and the items dialog ends. Stops when a tail fails.
 * Original: 3BDE3 (449BA's 190CF; 3BE09..3BE13; 97F9, 2A701, 97FF). */
xeen_status use_after_power(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    xeen_status status;
    g->use_active = 0;
    g->cast_active = 0;
    g->panel_stage = 0; /* the handler's stages are over */
    xeen_presentation_trace("mark use-end %u", g->use_power);
    /* 449BA: 190CF. from combat (449B5: 449A9's push skipped) it
     * pops what Use's 44F92 pushed: 98D4 is the items buffer (7), so 4AA3E(7)
     * frees every handle from 7 (the cursor, faces, main.icn, the map,
     * combat.icn, equip.icn; after a town, the new map's), last = 6. The
     * driver's tests then judge the next 4AC92 draws (driver_check): the
     * original exits at 2ABB8's (xeen_dat_evidence/phase118). */
    if (g->use_power == USE_POWER_TOWN_PORTAL && ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_COMBAT) {
        g->driver_check = 1;
    }
    if (g->use_power == USE_POWER_LLOYDS_BEACON || g->use_power == USE_POWER_TOWN_PORTAL) {
        handle_pop(g);
    }
    if (g->use_charges < 1) {
        item_clear(g, g->use_record, XEEN_ITEMS_MISC, g->use_row);
        item_sort(g, g->use_record, XEEN_ITEMS_MISC);
    }
    if (g->driver_check) { /* 3BE09..3BE13 here, before 2ABB8 */
        uint32_t b = 0;
        if (xeen_combat_use_tail(&g->combat, m, g->monster_database, g->monster_database_size,
                                 g->event_random, g->event_random_user,
                                 game_audio(g), &b) != XEEN_OK) {
            return game_stop(g, b ? b : USE_STOP_COMBAT_TAIL);
        }
    }
    if (ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_COMBAT || g->items_combat) {
        g->use_done = 1; /* 97F9, 2A701, 97FF: xeen_combat_after_use */
    } else {
        ds[XEEN_DS_MISSILES_FLYING] = 0;
        g->service_boundary = 0;
        status = run_monster_update(g);
        if (status != XEEN_OK) {
            return game_stop(g, g->service_boundary ? g->service_boundary : USE_STOP_COMBAT_TAIL);
        }
        m->status = XEEN_OK;
        ds[XEEN_DS_TURN_ACTOR] = XEEN_NO_TURN;
    }
    ds[XEEN_DS_PORTRAIT_CLICKS] = 1;
    return items_end(g, 0);
}
/*
 * Runs the chosen option on row OPTION_INDEX by the dialog's mode: the member's own items by ACTION (3BAEE's SI, the
 * keys E, R, U and D: 0 Equip, 1 Remove, 2 Use, 3 and above Discard), the shop's and the spells' modes their own
 * option; an unknown mode stops. Original: 3BAEE's switch (mode 8 shares the own items' case; no native caller
 * opens it).
 */
static xeen_status option_run(xeen_game *g)
{
    unsigned cat = (unsigned)g->items_cat, i = (unsigned)g->option_index;
    switch (g->items_mode) {
        case ITEMS_OWN:
        case 8: /* the original's switch: as the own items */
            switch (g->option_action) {
                case 0:
                    return cat < 3u ? equip_item(g, g->items_char, i, cat)
                                    : items_after_option(g, 0);
                case 1:
                    return option_remove(g);
                case 2:
                    return cat == XEEN_ITEMS_MISC ? option_use(g) : items_after_option(g, 0); /* 3BCF7 Use */
                default:
                    return option_discard(g);
            }
        case ITEMS_BUY:
            return option_buy(g);
        case ITEMS_SELL:
            return option_sell(g);
        case ITEMS_FIX:
            return option_fix(g);
        case ITEMS_IDENTIFY:
            return option_identify(g); /* 3C199 Identify */
        case ITEMS_RECHARGE:
            return option_recharge(g);
        case ITEMS_ENCHANT:
            return option_enchant(g);
        case ITEMS_GOLD:
            return option_gold(g);
        default:
            return shop_stop(g, OPTION_STOP_UNKNOWN_ACTION);
    }
}
/* An item option is over without closing the items dialog: the portrait clicks come back, no item is chosen, and
 * the dialog is redrawn and polled again (items_reset). AX, the original's answer, is always 0 here.
 * Original: 3C8F3: 98D6 = 1, AX 0 (the dialog redraws at 452D4). */
xeen_status items_after_option(xeen_game *g, int ax)
{
    g->movement.ds[XEEN_DS_PORTRAIT_CLICKS] = 1;
    g->option_index = -1;
    (void)ax;
    return items_reset(g);
}
