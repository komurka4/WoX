#include "game_items.h"
#include "xeen/item_tables.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/*
 * Where native play stops when the items screen cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json).
 */
enum {
    ITEMS_STOP_NO_PRESENTATION = 0x44f92,
    ITEMS_STOP_UNSHIPPED_MODE = 0x44f92,     /* modes 3, 5 and 8: no caller passes them (the original never assigns them) */
    ITEMS_STOP_NESTED = 0x44f92,             /* a spell's items screen inside one that is itself nested */
    ITEMS_STOP_RESIDENT_SPRITES = 0x45046,
    ITEMS_STOP_SPRITES = 0x45057,            /* equip.icn */
    ITEMS_STOP_FULL_TEXT = 0x451fa,          /* "full" message does not fit */
    ITEMS_STOP_LINE = 0x45451,               /* an item's line does not fit */
    ITEMS_STOP_PRICED_LINE = 0x45878,        /* an item's line with its cost does not fit */
    ITEMS_STOP_UNKNOWN_MODE = 0x45a8e,       /* an items mode without a heading */
    ITEMS_STOP_TEXT = 0x45965                /* the screen's text does not fit */
};

/* --------------------- the blacksmith, items and character view
 * The 277E5 blacksmith (kind 1: 25679 keys B and F1..F6) opens the shared 44F92 items dialog in its Buy
 * mode (1) over the town's wares (3D277 into the DS:BC6A scratch record,
 * 3CFF2 back); exploration's F1..F6 (17DDE) open the 44AE6 character view,
 * whose I opens 44F92 in mode 0 (the member's own items). 3BAEE carries
 * the item actions: Buy, Equip (424C3), Remove (3BA59). The addresses in
 * the comments are the analysis placement. */
static xeen_status items_text(xeen_game *);
static xeen_status location_after_items(xeen_game *, uint16_t);
/** Stops a shop or items dialog at unsupported BOUNDARY, clearing its owners first. */
xeen_status shop_stop(xeen_game *g, uint32_t boundary)
{
    g->items_owner = ITEMS_OWNER_NONE;
    g->message_owner = MESSAGE_NONE;
    g->panel_stage = 0;
    return panel_stop(g, boundary);
}

/* ----------------------------------------------------------- item records
 * The rules (rows, sort, wares, prices, restrictions, descriptions, equip)
 * are xeen_item_* in items.c; these wrappers bind them to the session. */
uint8_t *item_at(xeen_game *g, uint16_t record, unsigned cat, unsigned i)
{
    return xeen_item_at(&g->movement, record, cat, i);
}
/** The game's view of xeen_item_key: the key of row I in category CAT of character RECORD. */
uint8_t item_key(xeen_game *g, uint16_t record, unsigned cat, unsigned i)
{
    return xeen_item_key(&g->movement, record, cat, i);
}
/** The game's view of xeen_item_clear: clears row I in category CAT of character RECORD (43385). */
void item_clear(xeen_game *g, uint16_t record, unsigned cat, unsigned i)
{
    xeen_item_clear(&g->movement, record, cat, i);
}
/** The game's view of xeen_item_sort: compacts category CAT of character RECORD (43428). */
void item_sort(xeen_game *g, uint16_t record, unsigned cat)
{
    xeen_item_sort(&g->movement, record, cat);
}
/** The game's view of xeen_wares_copy: copies the town's wares to or from the scratch record. */
static void wares_copy(xeen_game *g, int to_record)
{
    xeen_wares_copy(&g->movement, to_record);
}
/** The game's view of xeen_ware_icons: sets the scratch record's equip icons. */
static void ware_icons(xeen_game *g)
{
    xeen_ware_icons(&g->movement);
}
const char *far_text(const xeen_game *g, uint16_t table, unsigned k)
{
    return ds_text(g, xeen_read16(&g->movement, (uint16_t)(table + 4u * k)));
}
/* Loads the material names (MAE.xen, from Darkside's archive) once for the item lines: the resident table addresses
 * its strings by their offsets into the file. Returns 0 when it is missing. Original: the table F65C (offsets as
 * 2793C / 16098 / 43D50 store them). */
static int mae_ready(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t world = m->ds[XEEN_DS_WORLD], *b;
    size_t n = 0;
    if (g->mae) {
        return 1;
    }
    xeen_select_archive(m, 1);
    b = menu_extract(g, ds_text(g, XEEN_TEXT_MATERIAL_NAMES_FILE), &n);
    xeen_select_archive(m, world);
    if (!b) {
        return 0;
    }
    if (!(g->mae = malloc(n + 1u))) {
        free(b);
        return 0;
    }
    memcpy(g->mae, b, n);
    free(b);
    g->mae[n] = 0;
    g->mae_size = n;
    return 1;
}
/**
 * Returns the description of row I in category CAT of character RECORD in COLOUR (loading the item
 * name file first when needed).
 */
const char *item_text(xeen_game *g, uint16_t record, unsigned i, unsigned colour,
                             unsigned cat)
{
    (void)mae_ready(g);
    return xeen_item_text(&g->movement, record, i, colour, cat, g->mae, g->mae_size);
}
/**
 * The game's view of xeen_item_cost: the price of row I for MODE (buy, sell, fix, identify, to
 * gold).
 */
uint32_t item_cost(xeen_game *g, uint16_t record, unsigned i, unsigned mode, unsigned skill,
                          unsigned cat)
{
    return xeen_item_cost(&g->movement, record, i, mode, skill, cat);
}
/** The game's view of xeen_item_allowed: nonzero when class CLS may use item ID of category CAT. */
static int item_allowed(xeen_game *g, unsigned cls, unsigned id, unsigned cat)
{
    return xeen_item_allowed(&g->movement, cls, id, cat);
}
/** Formats the "a CLASS cannot use the ITEM" message for class CLS and item ID of category CAT. */
void item_not_proficient(xeen_game *g, unsigned cls, unsigned id, unsigned cat, char *out,
                                size_t n)
{
    static const uint16_t names[4] = XEEN_ITEM_NAME_TABLES;
    (void)snprintf(out, n, ds_text(g, XEEN_TEXT_NOT_PROFICIENT), far_text(g, XEEN_DS_CLASS_NAMES, cls),
                   far_text(g, names[cat & 3u], id));
}

/* ------------------------------------------------ the mode-0 message
 * A message in the scroll box with a plain key wait (no scene): the button tables saved, the key cleared, the UI mode
 * saved and set to none, the text in the message format; the key closes it, the tables and the mode come back, and
 * its owner (enum message_owner, game_internal.h) continues. Equip's own wait keeps the mode.
 * Original: 18F2F(text, 0): 190ED/1919B, F5FE = 0, 9A1C saved and FFh, the 4B7A format, window 6, 189E3 (18F97: no
 * scene in mode 0), 18765(6), 189BC, 18FFF, 9A1C back. The owners' continuations: 3BAEE ends (1); 424C3 (2); 291FB
 * inside 29281 (4, 5: the guild 3F6AF); an item spell, 3BAEE answering 2 (6); 44E0F then 44E89 (7); 3FF2E then 40CAB
 * 9A1C = 4 and the 40BD4 poll (8); 44526 then the 441D3 poll (9); 29A94 then 29C26's 298D1 scene (12); 29B12 then
 * 2987E (13). */
/* The message window: the scroll box opened with TEXT and shown. Original: 1871B(6), 4E38C(TEXT, 6), 50BF6(6). */
void message_window(xeen_game *g, const char *text)
{
    panel_window(g, XEEN_WINDOW_SCROLL, 1);
    menu_window_text(g, XEEN_WINDOW_SCROLL, WINDOW_FRAMED, text);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCROLL);
}
/* Shows a mode-0 message (scroll0_open's and the locations' location_message): the button tables saved, the key
 * cleared, the UI mode set to none, TEXT in the message format in the scroll box. Returns the UI mode it replaced.
 * Original: 18F2F mode 0: 190ED/1919B, F5FE = 0, 9A1C = FFh, 4B7A, 1871B, 4E38C, 50BF6. */
uint8_t message_show(xeen_game *g, const char *text)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, mode;
    char out[0x98];
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    mode = ds[XEEN_DS_UI_MODE];
    ds[XEEN_DS_UI_MODE] = 0xff;
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_DS_MESSAGE_FORMAT), text);
    message_window(g, out);
    return mode;
}
/* Closes a mode-0 message: the scroll box closed, the key cleared and the button tables message_show saved restored.
 * The UI mode is the caller's to restore (message_closed). */
void message_hide(xeen_game *g)
{
    panel_window(g, XEEN_WINDOW_SCROLL, 0);
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
    xeen_button_stack(g->movement.ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
}
/* Opens a mode-0 message with TEXT for OWNER (enum message_owner): message_show, the replaced UI mode kept, then the
 * message's key poll (a stage); the key that closes it continues the owner (message_closed). */
xeen_status scroll0_open(xeen_game *g, int owner, const char *text)
{
    g->message_mode = message_show(g, text);
    g->message_owner = owner;
    return panel_set(g, MESSAGE_POLL, SCROLL0_BOUNDARY);
}
/*
 * A key closed a mode-0 message: the message hidden and, except for Equip's wait, the UI mode it replaced restored (a
 * payment's message also pops the button set its dialog pushed); then its owner continues: the character view's tick,
 * creation's class poll, the inn's poll, the panel's Load scene or Save redraw, the guild's poll (after the refusal
 * sound), the items dialog reset (a refused transfer), an item spell's end, or else the items option's follow-up.
 */
static xeen_status message_closed(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    int owner = g->message_owner;
    message_hide(g);
    if (owner != MESSAGE_EQUIP_WAIT) {
        ds[XEEN_DS_UI_MODE] = g->message_mode;
    }
    if (owner == MESSAGE_PAYMENT || owner == MESSAGE_GUILD_PAYMENT) {
        xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT); /* 29278: 291FB's 18FFF */
    }
    g->message_owner = MESSAGE_NONE;
    if (owner == MESSAGE_VIEW_EXCHANGE) {
        return panel_set(g, VIEW_TICK, VIEW_TICK_BOUNDARY); /* 44E89 (F5FE 0) */
    }
    if (owner == MESSAGE_CREATION_CLASS) {
        ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_CREATION;
        return panel_set(g, CREATE_POLL, CREATE_POLL_BOUNDARY);
    }
    if (owner == MESSAGE_INN) {
        return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
    }
    if (owner == MESSAGE_PANEL_LOAD) {
        return panel_scene(g, PANEL_SCENE);
    }
    if (owner == MESSAGE_PANEL_SAVE) {
        return panel_redraw(g);
    }
    if (owner == MESSAGE_GUILD_PAYMENT) { /* The guild, 3F6AF */
        (void)sound_effect(g, XEEN_CUE_REFUSED);
        return panel_set(g, GUILD_POLL, GUILD_POLL_BOUNDARY);
    }
    if (g->use_marked) {
        g->use_marked = 0;
        xeen_presentation_trace("mark spell-end 96");
    }
    if (owner == MESSAGE_ITEMS_REFUSED) {
        return items_reset(g); /* 45F3E: the transfer refused */
    }
    if (owner == MESSAGE_ITEM_SPELL) {
        return items_end(g, 1); /* 3C8EE AX 2 */
    }
    return items_after_option(g, 0);
}


/* ------------------------------------------------------- 44F92 items */
/* Saves the outer items dialog's state while an item power's dialog (Use's spell) runs inside it; marks the dialog
 * nested (items_frame_restore undoes both). */
static void items_frame_save(xeen_game *g)
{
    g->items_outer.owner = g->items_owner;
    g->items_outer.mode = g->items_mode;
    g->items_outer.cat = g->items_cat;
    g->items_outer.index = g->items_index;
    g->items_outer.combat = g->items_combat;
    g->items_outer.stock = g->items_stock;
    g->items_outer.action = g->option_action;
    g->items_outer.confirm = g->option_owner;
    g->items_outer.oindex = g->option_index;
    g->items_outer.chr = g->items_char;
    g->items_outer.start = g->items_start;
    g->items_outer.equip = g->items_equip;
    g->items_outer.residue = g->option_residue;
    memcpy(g->items_outer.marks, g->items_marks, sizeof g->items_marks);
    g->items_outer.cost = g->option_cost;
    g->items_nested = 1;
    ++g->items_nests;
}
/** Restores the outer items dialog's state after a nested items dialog (an item spell's) closed. */
static void items_frame_restore(xeen_game *g)
{
    g->items_owner = g->items_outer.owner;
    g->items_mode = g->items_outer.mode;
    g->items_cat = g->items_outer.cat;
    g->items_index = g->items_outer.index;
    g->items_combat = g->items_outer.combat;
    g->items_stock = g->items_outer.stock;
    g->option_action = g->items_outer.action;
    g->option_owner = g->items_outer.confirm;
    g->option_index = g->items_outer.oindex;
    g->items_char = g->items_outer.chr;
    g->items_start = g->items_outer.start;
    g->items_equip = g->items_outer.equip;
    g->option_residue = g->items_outer.residue;
    memcpy(g->items_marks, g->items_outer.marks, sizeof g->items_marks);
    g->option_cost = g->items_outer.cost;
    g->items_nested = 0;
}
/* Whether MODE is an item spell's dialog (Enchant, Recharge, Item to Gold): its own button tables and page text, no
 * blacksmith mode buttons, no F1..F6 member keys. Original: 44F92's second tables (1CB8..), 45152's 7ED9, 452EA. */
static int items_spell_dialog(int mode)
{
    return mode == ITEMS_ENCHANT || mode == ITEMS_RECHARGE || mode == ITEMS_GOLD;
}
/* Whether MODE's rows show no price: the member's own items, mode 8, Enchant and Recharge. Original: 452EA's
 * "%02u %2d) %s" (7F43). */
static int items_unpriced(int mode)
{
    return mode == ITEMS_OWN || mode == 8 || mode == ITEMS_ENCHANT || mode == ITEMS_RECHARGE;
}
/*
 * The items dialog's own state for character RECORD opened in MODE for OWNER: the record shown and the one it started
 * from, misc as the first category in combat's and Recharge's dialogs (else weapons), Buy's stock, no row or option
 * chosen. Returns the mode the dialog runs as: combat's dialog is the member's own items.
 */
static int items_dialog_state(xeen_game *g, uint16_t record, int mode, int owner)
{
    g->items_owner = owner;
    g->items_start = record;
    g->items_char = record;
    g->items_cat = (mode == ITEMS_COMBAT || mode == ITEMS_RECHARGE) ? 3 : 0;
    g->items_combat = mode == ITEMS_COMBAT;
    if (g->items_combat) {
        mode = ITEMS_OWN;
    }
    g->items_mode = mode;
    g->items_stock = mode == ITEMS_BUY;
    g->items_index = -1;
    g->option_index = -1;
    return mode;
}
static const char ITEMS_BUY_KEYS[4] = {'B', 'S', 'I', 'F'};
static const char ITEMS_OWN_KEYS[4] = {'E', 'R', 'D', 'Q'};
/* Writes the items dialog's four mode keys KEYS, the key bytes of its button rows 4..7 (DS 1C95..1C98): Buy's, else
 * the others'. Original: 45114..45123 ('B' 'S' 'I' 'F'), 4512E..4513D ('E' 'R' 'D' 'Q'). */
static void items_mode_keys(uint8_t *ds, const char keys[4])
{
    for (unsigned k = 0; k < 4u; ++k) {
        ds[XEEN_BUTTON_KEY(XEEN_BUTTONS_ITEMS, 4u + k)] = (uint8_t)keys[k];
    }
}
/*
 * Opens the items dialog for character RECORD in MODE for OWNER (enum items_owner): an item spell's dialog opened
 * inside another saves the outer one (a third level stops). The map's reload mark is cleared; the category is misc
 * in combat's and Recharge's dialogs, else weapons; combat's dialog runs as the member's own items; Buy shows the
 * town's stock. The button set is pushed, the mouse cursor selected; the mode's sprites ("items.icn" for the member's
 * own items, else "buy.icn") become the current sprites (pushed on the handle stack, into the resident slot) and
 * equip.icn is loaded beside them; the item spells' dialogs get their own button tables. In Buy the member becomes
 * the acting character, the town's wares are copied into the scratch record (the dialog's record) with their slot
 * icons, and the mode keys are B S I F; otherwise E R D Q. Then the list and its buttons' windows open and the page
 * is drawn (items_full). Modes 3, 5 and 8 (no caller passes them) and a missing presentation stop.
 * Original: 44F92(record, mode) (96E1 = 0, 190ED, 4E06B(F460, 0), 18931 into 8C86, the tables 1C30.. or 1CB8..,
 * 3D277, 445E5; windows 1Dh, 1Eh).
 */
xeen_status items_begin(xeen_game *g, uint16_t record, int mode, int owner)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char name[16];
    uint16_t h = 0;
    if (!g->presentation) {
        return shop_stop(g, ITEMS_STOP_NO_PRESENTATION);
    }
    if (mode != ITEMS_OWN && mode != ITEMS_BUY && mode != ITEMS_RECHARGE && mode != ITEMS_ENCHANT &&
        mode != ITEMS_COMBAT && mode != ITEMS_GOLD) {
        return shop_stop(g, ITEMS_STOP_UNSHIPPED_MODE);
    }
    if (owner == ITEMS_OWNER_SPELL && g->items_owner) { /* Use's power inside 44F92 */
        if (g->items_nested) {
            return shop_stop(g, ITEMS_STOP_NESTED);
        }
        items_frame_save(g);
    }
    ds[XEEN_DS_MAP_RELOADED] = 0;
    mode = items_dialog_state(g, record, mode, owner);
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    (void)xeen_presentation_select_cursor(g->presentation, xeen_read16(m, XEEN_DS_MOUSE_SPRITES),
                                          0);
    (void)snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_ITEMS_SPRITES_FORMAT), ds_text(g, mode == ITEMS_OWN ? XEEN_TEXT_ITEMS_OWN_SPRITES : XEEN_TEXT_ITEMS_SHOP_SPRITES));
    ds_put_text(g, XEEN_DS_FILE_NAME, name);
    ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(ds[XEEN_DS_HANDLE_DEPTH] + 1u);
    xeen_write16(m, XEEN_DS_HANDLE_SLOT(ds), xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
    if (!sprite_load_resident(g, name)) {
        return shop_stop(g, ITEMS_STOP_RESIDENT_SPRITES);
    }
    if (!sprite_load(g, ds_text(g, XEEN_TEXT_ITEMS_EQUIP_SPRITES), &h)) {
        return shop_stop(g, ITEMS_STOP_SPRITES);
    }
    g->items_equip = h;
    if (items_spell_dialog(mode)) {
        button_tables(m, XEEN_BUTTONS_ITEMS_SPELL);
    } else {
        button_tables(m, XEEN_BUTTONS_ITEMS);
    }
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
    if (mode == ITEMS_BUY) {
        xeen_write16(m, XEEN_DS_ACTING_CHARACTER + 2u, XEEN_DS_SEGMENT);
        xeen_write16(m, XEEN_DS_ACTING_CHARACTER, record);
        g->items_char = XEEN_DS_SCRATCH_RECORD;
        wares_copy(g, 1);
        items_mode_keys(ds, ITEMS_BUY_KEYS); /* after the wares, before their icons (445E5) */
        ware_icons(g);
    } else {
        items_mode_keys(ds, ITEMS_OWN_KEYS);
    }
    panel_window(g, XEEN_WINDOW_LIST_BUTTONS, 1);
    panel_window(g, XEEN_WINDOW_LIST, 1);
    return items_full(g);
}
/* Draws the enabled buttons of the current button tables (location_buttons). Original: 4AC92 of each. */
static void items_buttons(xeen_game *g)
{
    location_buttons(g);
}
/*
 * Draws the items dialog's buttons for its mode and category, then resets and redraws the list (items_reset): the
 * first action button is R(echarge), E(nchant) or G(old) in an item spell's dialog; with the member's own misc items
 * it moves off the panel (x 0) and the Use button (U) is enabled; otherwise it stands at x 94h and Use is disabled.
 * The buttons' text: the blacksmith's Buy, Sell, Identify, Fix (shops' button names); the member's items' Equip (or
 * Use for misc), Remove, Discard, Quest; an item spell's dialog its one action. A text that does not fit stops.
 * Original: 45152 (the formats 7E71 and 7ED9 into A43F; window 1Dh), then 452D4.
 */
xeen_status items_full(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char out[0x3e8];
    int mode = g->items_mode, n;
    if ((mode == ITEMS_OWN && g->items_cat == XEEN_ITEMS_MISC) || items_spell_dialog(mode)) {
        if (mode == ITEMS_RECHARGE) {
            ds[XEEN_BUTTON_KEY(XEEN_BUTTONS_ITEMS_SPELL, 4)] = 'R';
        } else if (mode == ITEMS_ENCHANT) {
            ds[XEEN_BUTTON_KEY(XEEN_BUTTONS_ITEMS_SPELL, 4)] = 'E';
        } else if (mode == ITEMS_GOLD) {
            ds[XEEN_BUTTON_KEY(XEEN_BUTTONS_ITEMS_SPELL, 4)] = 'G';
        } else {
            xeen_write16(m, XEEN_BUTTON_X(XEEN_BUTTONS_ITEMS, 4), 0);
            ds[XEEN_BUTTON_ENABLED(XEEN_BUTTONS_ITEMS, 9)] = 1;
            ds[XEEN_BUTTON_KEY(XEEN_BUTTONS_ITEMS, 9)] = 'U';
        }
    } else {
        xeen_write16(m, XEEN_BUTTON_X(XEEN_BUTTONS_ITEMS, 4), 0x94);
        ds[XEEN_BUTTON_ENABLED(XEEN_BUTTONS_ITEMS, 9)] = 0;
    }
    if (mode != ITEMS_OWN && mode != 8 && !items_spell_dialog(mode)) {
        n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_ITEMS_BUTTONS), far_text(g, XEEN_DS_SHOP_BUTTON_NAMES, 0),
                     far_text(g, XEEN_DS_SHOP_BUTTON_NAMES, 1), far_text(g, XEEN_DS_SHOP_BUTTON_NAMES, 2), far_text(g, XEEN_DS_SHOP_BUTTON_NAMES, 3));
    } else if (!items_spell_dialog(mode)) {
        n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_ITEMS_BUTTONS),
                     g->items_cat == XEEN_ITEMS_MISC ? ds_text(g, XEEN_TEXT_ITEMS_USE_BUTTON) : far_text(g, XEEN_DS_ITEMS_BUTTON_NAMES, 0),
                     far_text(g, XEEN_DS_ITEMS_BUTTON_NAMES, 1), far_text(g, XEEN_DS_ITEMS_BUTTON_NAMES, 2), far_text(g, XEEN_DS_ITEMS_BUTTON_NAMES, 3));
    } else {
        n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_ITEMS_SPELL_BUTTONS),
                     ds_text(g, mode == ITEMS_ENCHANT    ? XEEN_TEXT_ITEMS_ENCHANT_BUTTON
                                : mode == ITEMS_RECHARGE ? XEEN_TEXT_ITEMS_RECHARGE_BUTTON
                                                         : XEEN_TEXT_ITEMS_GOLD_BUTTON));
    }
    if (n < 0 || (size_t)n >= sizeof out) {
        return shop_stop(g, ITEMS_STOP_FULL_TEXT);
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER_0, out, (size_t)n + 1u);
    menu_window_text(g, XEEN_WINDOW_LIST_BUTTONS, WINDOW_FRAMED, ds_text(g, XEEN_DS_TEXT_BUFFER_0));
    items_buttons(g);
    return items_reset(g);
}
/* Clears the rows' colours (marks) and the selection, then redraws the list (items_text). Original: 452D4, then 452EA. */
xeen_status items_reset(xeen_game *g)
{
    memset(g->items_marks, 0, sizeof g->items_marks);
    g->items_index = -1;
    return items_text(g);
}
/* The items dialog's mode buttons, drawn at every redraw: the first button (frame 8) unless the member's own misc
 * items are shown; the three others (frames 10, 12, 14) unless an item spell's dialog; then the blacksmith's current
 * mode highlighted on its button (Buy 9, Sell 11, Identify 13, Fix 15).
 * Original: 452EA (98D4 at 94h, B6h, D8h, FAh by 6Dh; CS:1D2D). */
static void items_mode_buttons(xeen_game *g, int mode, unsigned cat)
{
    uint16_t h = xeen_read16(&g->movement, XEEN_DS_SPRITE_HANDLE);
    if (mode != ITEMS_OWN || cat != XEEN_ITEMS_MISC) {
        exit_draw(g, h, 8, 148, 109, 0);
    }
    if (!items_spell_dialog(mode)) {
        exit_draw(g, h, 10, 182, 109, 0);
        exit_draw(g, h, 12, 216, 109, 0);
        exit_draw(g, h, 14, 250, 109, 0);
    }
    if (mode == ITEMS_BUY) {
        exit_draw(g, h, 9, 148, 109, 0);
    } else if (mode == ITEMS_SELL) {
        exit_draw(g, h, 11, 182, 109, 0);
    } else if (mode == ITEMS_IDENTIFY) {
        exit_draw(g, h, 13, 216, 109, 0);
    } else if (mode == ITEMS_FIX) {
        exit_draw(g, h, 15, 250, 109, 0);
    }
}
/* The price shown for row I: the mode's cost at the starting member's Merchant skill (Item to Gold at skill 1); a
 * misc row in an unpriced mode is priced as Enchant prices it (its charges). Original: 452EA's price (7F5E; +2Fh). */
static uint32_t items_row_cost(xeen_game *g, int mode, uint16_t record, uint16_t start, unsigned i,
                               unsigned cat)
{
    const uint8_t *ds = g->movement.ds;
    if (items_unpriced(mode)) {
        return item_cost(g, record, i, ITEMS_ENCHANT,
                         ds[(uint16_t)(start + (XEEN_CHARACTER_SKILLS + XEEN_SKILL_MERCHANT))], cat);
    }
    return item_cost(g, record, i, (unsigned)mode,
                     mode == ITEMS_GOLD ? 1u
                                        : ds[(uint16_t)(start + (XEEN_CHARACTER_SKILLS + XEEN_SKILL_MERCHANT))],
                     cat);
}
#include "../addon/identify/item_list.inc" /* add-on identify: addons_item_list */
/* The empty list's "None" line, written into the first row's LINE. */
static void items_none_line(xeen_game *g, char *line)
{
    memcpy(line, g->movement.ds + XEEN_TEXT_ITEMS_NONE, strlen(ds_text(g, XEEN_TEXT_ITEMS_NONE)) + 1u);
}
/* What items_row made of a row. */
enum items_row_result {
    ROW_EMPTY,   /* no item (the first row then says "None") */
    ROW_WRITTEN, /* its line and icon written */
    ROW_TOO_LONG /* its line would pass 99 characters: nothing written but a misc row's unused icon */
};
/* Row I of category CAT of the dialog's record as its line in LINE (100 bytes): the row's number and its text in the
 * row's mark colour, and its price unless the mode is unpriced (a misc row always shows one: its charges' price in
 * an unpriced mode). A weapon, armour or accessory row also gets its icon record: the frame of its equip slot, or
 * frame 14 when the starting member's class may not use it; an empty one and any misc row an unused record.
 * Original: 452EA's row loop (the lines into 9C6F + 100n, cleared, 7D0h bytes; "%02u %2d) %s" 7F43 or with the price
 * 7F5E; the equip.icn frame in 2BA0/2BA2 + 12n, 14 when 46E56 refuses the class; "No items available." for an empty
 * first row; the mode's header in AC0F 7F9D/7FDD/802E, window 1Eh, 50BF6(0), 189BC). */
static enum items_row_result items_row(xeen_game *g, int mode, unsigned cat, unsigned i, char *line)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, *item = item_at(g, g->items_char, cat, i);
    uint16_t record = g->items_char, start = g->items_start,
             icon = (uint16_t)(XEEN_DS_ITEM_ICON_ROWS + XEEN_VIEW_RECORD_BYTES * i);
    unsigned colour = g->items_marks[i];
    int equipment = cat < 3u; /* else a misc row: its key is the material byte */
    char out[1000];
    int n;
    if (!equipment) {
        xeen_write16(m, icon, XEEN_VIEW_RECORD_UNUSED);
    }
    if (!item[equipment ? XEEN_ITEM_ROW_ID : XEEN_ITEM_ROW_MATERIAL]) {
        if (equipment) {
            xeen_write16(m, icon, XEEN_VIEW_RECORD_UNUSED);
        }
        if (!i) {
            items_none_line(g, line);
        }
        return ROW_EMPTY;
    }
    if (equipment && items_unpriced(mode)) {
        n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_ITEMS_LINE), colour, (int)i + 1,
                     item_text(g, record, i, colour, cat));
    } else {
        uint32_t cost = items_row_cost(g, mode, record, start, i, cat);
        n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_ITEMS_PRICED_LINE), colour, (int)i + 1,
                     item_text(g, record, i, colour, cat), (unsigned long)cost);
    }
    if (n < 0 || n >= 100) {
        return ROW_TOO_LONG;
    }
    memcpy(line, out, (size_t)n + 1u);
    if (equipment) {
        xeen_write16(m, icon, g->items_equip);
        xeen_write16(m, (uint16_t)(icon + XEEN_VIEW_RECORD_FRAME),
                     item_allowed(g, ds[(uint16_t)(start + XEEN_CHARACTER_CLASS)], item[XEEN_ITEM_ROW_ID], cat)
                             ? item[XEEN_ITEM_ROW_EQUIPPED]
                             : 14u);
    }
    return ROW_WRITTEN;
}
/* The mode's page around the nine lines L into OUT (SIZE bytes), *N snprintf's result: the member's own items (the
 * category, the member, the class and the charges heading for misc), the shop's stock with the party's gold, or a
 * service's list with its cost or charges heading. Returns 0 for a mode without a page. Original: 459CE. */
static int items_page(xeen_game *g, int mode, unsigned cat, const char *const l[9], char *out, size_t size, int *n)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t record = g->items_char, start = g->items_start;
    switch (mode) {
        case ITEMS_OWN:
        case 8:
            *n = snprintf(out, size, ds_text(g, XEEN_TEXT_ITEMS_OWN_PAGE),
                          ds_text(g, cat == XEEN_ITEMS_MISC ? XEEN_TEXT_ITEMS_LEFT : XEEN_TEXT_ITEMS_CENTRED),
                          far_text(g, XEEN_DS_ITEM_CATEGORY_NAMES, cat), ds_text(g, record),
                          far_text(g, XEEN_DS_CLASS_NAMES, ds[(uint16_t)(record + XEEN_CHARACTER_CLASS)]),
                          cat == XEEN_ITEMS_MISC ? far_text(g, XEEN_DS_SHOP_CHARGES_TEXT, 0)
                                                 : ds_text(g, XEEN_TEXT_ITEMS_NO_HEADING),
                          l[0], l[1], l[2], l[3], l[4], l[5], l[6], l[7], l[8]);
            return 1;
        case ITEMS_BUY:
            /* The heading's plural "s" depends on a stack word the dialog never writes: it was nonzero (no "s")
             * on every route replayed, so the native writes none. Original: 459CE tests [bp-8], which 44F92
             * never writes (xeen_dat_evidence/phase59/ORIGINAL_BYTE_COVERAGE.md). */
            *n = snprintf(out, size, ds_text(g, XEEN_TEXT_ITEMS_BUY_PAGE), far_text(g, XEEN_DS_ITEM_CATEGORY_NAMES, cat),
                          "", (unsigned long)xeen_ds_read32(m, XEEN_DS_PARTY_GOLD), l[0], l[1], l[2], l[3], l[4], l[5],
                          l[6], l[7], l[8]);
            return 1;
        case ITEMS_SELL:
        case ITEMS_RECHARGE:
        case ITEMS_ENCHANT:
        case ITEMS_FIX:
        case ITEMS_IDENTIFY:
        case ITEMS_GOLD:
            *n = snprintf(out, size, ds_text(g, XEEN_TEXT_ITEMS_SERVICE_PAGE),
                          far_text(g, XEEN_DS_ITEM_CATEGORY_NAMES, cat), ds_text(g, start),
                          ds_text(g, mode == ITEMS_ENCHANT || mode == ITEMS_RECHARGE ? XEEN_TEXT_ITEMS_CHARGES_HEADING
                                                                                    : XEEN_TEXT_ITEMS_COST_HEADING),
                          l[0], l[1], l[2], l[3], l[4], l[5], l[6], l[7], l[8]);
            return 1;
        default:
            return 0;
    }
}
/* The nine rows' icons, each at x 8 and its row's y + 8 (an unused row's record draws nothing). Original: 45B28,
 * 4AC92 (which skips the handle FFFFh). */
static void items_row_icons(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    for (unsigned i = 0; i < 9u; ++i) {
        exit_draw(g, xeen_read16(m, XEEN_VIEW_FIELD(XEEN_DS_ITEM_ICON_ROWS, i, HANDLE)),
                  xeen_read16(m, XEEN_VIEW_FIELD(XEEN_DS_ITEM_ICON_ROWS, i, FRAME)), 8,
                  (int16_t)(xeen_read16(m, XEEN_VIEW_FIELD(XEEN_DS_ITEM_ICON_ROWS, i, Y)) + 8u),
                  xeen_read16(m, XEEN_VIEW_FIELD(XEEN_DS_ITEM_ICON_ROWS, i, FLAGS)));
    }
}
/* The action a row already chosen in MODE goes on to at once: option_begin's (3BAEE's) action argument, which
 * item_cost also takes as its skill, 0 in Buy, 1 in Sell, 2 in Identify, 3 in Fix; -1 in the other modes, which wait
 * for a key. */
static int items_row_option(int mode)
{
    switch (mode) {
        case ITEMS_BUY:
            return 0;
        case ITEMS_SELL:
            return 1;
        case ITEMS_FIX:
            return 3;
        case ITEMS_IDENTIFY:
            return 2;
        default:
            return -1;
    }
}
/*
 * The items dialog's page: the mode's buttons, the category's nine rows as lines (items_row), the mode's page in the
 * list window (items_page; unless the identify add-on draws its own), the rows' icons and the screen shown. A chosen
 * row in Buy, Sell, Fix or Identify goes on to that option at once; else the dialog's key poll. Stops: a row's line
 * too long, a mode without a page, a page too long.
 * Original: 452EA (the lines), 459CE (the pages), 45B28 (the icons); 4AC92.
 */
static xeen_status items_text(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    int mode = g->items_mode;
    unsigned cat = (unsigned)g->items_cat, i;
    uint16_t record = g->items_char, start = g->items_start;
    const char *l[9];
    char out[1000];
    int n;
    items_mode_buttons(g, mode, cat);
    memset(ds + XEEN_DS_SCRATCH, 0, XEEN_DS_SCRATCH_BYTES);
    for (i = 0; i < 9u; ++i) {
        if (items_row(g, mode, cat, i, (char *)ds + XEEN_DS_SCRATCH + 100u * i) == ROW_TOO_LONG) {
            /* The original's 452EA formats each row with one of seven sprintf calls, by category and by price:
             * weapons 45451 / 454C1, armour 455A0 / 45610, accessories 456E4 / 45754, misc 45878 (unpriced line
             * 7F43 / priced line 7F5E). This native bound (100 bytes, the line's slot) reports the weapons'
             * unpriced site for every weapon, armour or accessory row and the misc site otherwise. */
            if (cat < 3u) {
                return shop_stop(g, ITEMS_STOP_LINE);
            }
            return shop_stop(g, ITEMS_STOP_PRICED_LINE);
        }
    }
    for (i = 0; i < 9u; ++i) {
        l[i] = ds_text(g, (uint16_t)(XEEN_DS_SCRATCH + 100u * i));
    }
    if (!items_page(g, mode, cat, l, out, sizeof out, &n)) {
        return shop_stop(g, ITEMS_STOP_UNKNOWN_MODE);
    }
    if (n < 0 || (size_t)n >= sizeof out || n >= 1000) {
        return shop_stop(g, ITEMS_STOP_TEXT);
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    if (!addons_item_list(g, record, start, cat, mode)) { /* add-on identify draws its own */
        menu_window_text(g, XEEN_WINDOW_LIST, WINDOW_FRAMED, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    }
    items_row_icons(g);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    if (g->items_index != -1 && items_row_option(mode) >= 0) {
        return option_begin(g, items_row_option(mode));
    }
    return panel_set(g, ITEMS_POLL, ITEMS_POLL_BOUNDARY);
}
/* The sprite driver's four decimal digits of V into OUT, the thousands counted on past '9' as the driver does (so
 * 10000 shows ':000'), the last digit with no terminator after it.
 * Original: 4AC4A for 4AD19's fatal exit 505E3 (xeen_dat_evidence fatal_bytes.py): "Error drawing sprite in
 * window: 0000 handle: 0000 frame: 000" (CS:7F2C; the fourth frame digit replaces its NUL, "!" after it); INT 9
 * restored, the memory and files released, text mode 3, the message in red on row 0, INT 21h 4C02h. */
static void driver_digits(char *out, unsigned v)
{
    unsigned k, step[3] = {1000u, 100u, 10u};
    for (k = 0; k < 3u; ++k) {
        out[k] = '0';
        while (v >= step[k]) {
            ++out[k];
            v -= step[k];
        }
    }
    out[3] = (char)('0' + v);
}
/* The sprite driver's fatal exit for the sprite FRAME of HANDLE that window WINDOW cannot draw: the message
 * (xeen_game_driver_exit), a trace mark, and the session stopped there as the original exits: nothing is saved
 * (the original writes no slot file). Original: 4AD19 -> 505E3. */
void driver_exit(xeen_game *g, uint16_t window, uint16_t handle, uint16_t frame)
{
    char *s = g->driver_exit;
    memcpy(s, "Error drawing sprite in window: 0000 handle: 0000 frame: 0000!", 63);
    driver_digits(s + 32, window);
    driver_digits(s + 45, handle);
    driver_digits(s + 57, frame);
    xeen_presentation_trace("mark driver-exit %u %u %u", window, handle, frame);
    g->items_owner = ITEMS_OWNER_NONE;
    g->panel_stage = 0;
    g->phase = XG_STOPPED;
    g->lifecycle_boundary = DRIVER_EXIT_BOUNDARY;
    g->command_status = XGC_ORIGINAL_EXIT;
}
/* Judges the party bar's draws by the sprite driver's tests, in their order (the bar, the faces, the HP gems, the
 * highlight): the first that fails takes the fatal exit (driver_exit) and answers 1; when all pass the check is
 * disarmed and 0 answered. Original: 2ABB8(1) (2ABE0, 2AC5D, 2AD58), 4AC92's tests. */
static int driver_bar_check(xeen_game *g)
{
    xeen_sprite_draw d[20];
    size_t n = 0, i;
    if (xeen_world_entry_party_bar_draws(&g->movement, d, 20, &n) != XEEN_OK) {
        return 0;
    }
    for (i = 0; i < n; ++i) {
        if (d[i].scale == 0xffffu ||
            !xeen_sprite_driver_check(&g->sprites, d[i].handle, d[i].frame)) {
            continue;
        }
        driver_exit(g, d[i].window, d[i].handle, d[i].frame);
        return 1;
    }
    g->driver_check = 0; /* every draw passed: no exit (no shipped case) */
    return 0;
}
/*
 * Ends the items dialog: with CLOSE (Esc, or an item spell's end) the list and button windows close and the result is
 * the starting member, else 0. The dialog's sprites are popped from the handle stack, equip.icn freed unless the map
 * was reloaded meanwhile, the button set popped; the party bar is checked against the sprite driver (a failing draw
 * is the original's fatal exit) and redrawn; portrait clicks are enabled again (98D6 = 1); Buy's stock returns to the
 * town. Then the owner continues: an item spell (its UI mode back, an outer dialog restored), combat's Use, the
 * location (the result as its member) or the character view. Original: 461E1 (Esc, ignored in mode 8), 461EA, 46206
 * (18765(1Eh), 18765(1Dh), 18FFF, 2ABB8(1), 3CFF2).
 */
xeen_status items_end(xeen_game *g, int close)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    int owner = g->items_owner;
    uint16_t result = close ? g->items_start : 0;
    if (close) {
        panel_window(g, XEEN_WINDOW_LIST, 0);
        panel_window(g, XEEN_WINDOW_LIST_BUTTONS, 0);
    }
    xeen_write16(m, XEEN_DS_SPRITE_HANDLE, xeen_read16(m, XEEN_DS_HANDLE_SLOT(ds)));
    ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(ds[XEEN_DS_HANDLE_DEPTH] - 1u);
    if (!ds[XEEN_DS_MAP_RELOADED]) {
        (void)xeen_sprite_free_from(&g->sprites, g->items_equip);
    }
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    if (g->driver_check && driver_bar_check(g)) {
        return XEEN_SERVICE_REQUIRED; /* 2ABB8's 4AC92 draws, 4AD19 */
    }
    if (g->presentation) {
        (void)xeen_world_entry_party_bar(m, g->presentation,
                                         1); /* 2ABB8(1) (the party bar, 50BF6(21h)) */
    }
    ds[XEEN_DS_PORTRAIT_CLICKS] = 1;
    if (g->items_stock) {
        wares_copy(g, 0);
    }
    g->items_owner = ITEMS_OWNER_NONE;
    if (owner == ITEMS_OWNER_SPELL) { /* 3B48F, the handler's RETF */
        ds[XEEN_DS_UI_MODE] = g->item_spell_mode;
        if (g->items_nested) {
            items_frame_restore(g); /* Back in Use's 44F92 (3BDE3 next) */
        }
        xeen_presentation_trace("mark spell-end %u", g->cast_spell);
        return cast_handled(g);
    }
    if (owner == ITEMS_OWNER_COMBAT) {
        return combat_use_return(g);
    }
    return owner == ITEMS_OWNER_LOCATION ? location_after_items(g, result) : view_after_items(g, result);
}
/* B S I F: the blacksmith's dialog in another MODE over RECORD (refused in the member's own items
 * and in the item spells' dialogs: the key is then ignored). */
static xeen_status items_shop_mode(xeen_game *g, int mode, uint16_t record)
{
    int current = g->items_mode;
    if (current == ITEMS_OWN || items_spell_dialog(current)) {
        return panel_set(g, ITEMS_POLL, ITEMS_POLL_BOUNDARY);
    }
    g->items_mode = mode;
    g->items_char = record;
    return items_reset(g);
}
/* 1..9: F5FE = the row (45D5B F5FE - 31h); the selected row again resets the dialog, an occupied
 * row becomes the selection (its mark 15). Not in mode 3. */
static xeen_status items_row_key(xeen_game *g, int row)
{
    if (g->items_mode == 3) {
        return panel_set(g, ITEMS_POLL, ITEMS_POLL_BOUNDARY);
    }
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, (uint16_t)row);
    if (row == g->items_index) {
        return items_reset(g);
    }
    if (!item_key(g, g->items_char, (unsigned)g->items_cat, (unsigned)row)) {
        return items_text(g);
    }
    g->items_index = row;
    memset(g->items_marks, 0, sizeof g->items_marks);
    g->items_marks[row] = 15;
    return items_text(g);
}
/* Gives the selected item to MEMBER: a cursed one is refused ("You cannot remove a cursed item!"), as is one for a
 * member whose category is full (its ninth row taken: "%s's <category> backpack is full."); else it moves to the
 * member's ninth row (not equipped), the source row is emptied (its id, or a misc row's key), and both characters'
 * rows are sorted. Original: 45F0A. */
static xeen_status items_give(xeen_game *g, uint16_t member)
{
    static const uint16_t full[4] = {XEEN_TEXT_SHOP_WEAPONS_FULL, XEEN_TEXT_SHOP_ARMOR_FULL, XEEN_TEXT_SHOP_ACCESSORIES_FULL, XEEN_TEXT_SHOP_MISC_FULL};
    unsigned cat = (unsigned)g->items_cat;
    uint8_t *from = item_at(g, g->items_char, cat, (unsigned)g->items_index);
    uint8_t *to;
    char out[0x3e8];
    if (from[XEEN_ITEM_ROW_STATE] & XEEN_ITEM_CURSED) {
        return scroll0_open(g, MESSAGE_ITEMS_REFUSED, ds_text(g, XEEN_TEXT_SHOP_CURSED));
    }
    if (item_key(g, member, cat, 8)) {
        (void)snprintf(out, sizeof out, ds_text(g, full[cat]), ds_text(g, member));
        ds_put_text(g, XEEN_DS_TEXT_BUFFER_0, out);
        return scroll0_open(g, MESSAGE_ITEMS_REFUSED, ds_text(g, XEEN_DS_TEXT_BUFFER_0));
    }
    to = item_at(g, member, cat, 8);
    if (cat == XEEN_ITEMS_MISC) {
        to[1] = from[1];
        to[0] = from[0];
        to[2] = from[2];
        from[0] = 0;
    } else {
        to[0] = from[0];
        to[1] = from[1];
        to[2] = from[2];
        from[1] = 0;
        to[3] = 0;
    }
    item_sort(g, g->items_char, cat);
    item_sort(g, member, cat);
    return items_reset(g);
}
/* F1..F6 (member N): refused in combat's dialog, in modes 3, Enchant, Recharge and Item to Gold,
 * and off the map; F5FE = N, then a present member (in combat the combat party's Nth): in Buy the
 * shopper (BC6A stays the dialog's record), in Sell, Fix and Identify the member whose items are
 * shown, with nothing selected likewise, else the selected item goes to the member (items_give).
 * 45F01 2A1F6 highlights the member's portrait (its frames). */
static xeen_status items_member_key(xeen_game *g, unsigned n)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    int mode = g->items_mode;
    int combat = ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_COMBAT;
    unsigned count;
    uint16_t member;
    if (g->items_combat || mode == 3 || items_spell_dialog(mode) || !ds[XEEN_DS_PARTY_MAP]) {
        return panel_set(g, ITEMS_POLL, ITEMS_POLL_BOUNDARY);
    }
    count = combat ? ds[XEEN_DS_COMBAT_PARTY_COUNT] : ds[XEEN_DS_PARTY_COUNT];
    xeen_write16(m, XEEN_DS_INPUT_CODE, (uint16_t)n);
    if (count <= n) {
        return panel_set(g, ITEMS_POLL, ITEMS_POLL_BOUNDARY);
    }
    member = (uint16_t)(XEEN_PARTY_RECORDS +
                        XEEN_CHARACTER_BYTES * (combat ? ds[XEEN_DS_COMBAT_PARTY + n] : n));
    if (mode == ITEMS_BUY || mode == ITEMS_SELL || mode == ITEMS_FIX || mode == ITEMS_IDENTIFY) {
        xeen_write16(m, XEEN_DS_ACTING_CHARACTER + 2u, XEEN_DS_SEGMENT);
        xeen_write16(m, XEEN_DS_ACTING_CHARACTER, member);
        g->items_start = member;
        g->items_char = mode == ITEMS_BUY ? XEEN_DS_SCRATCH_RECORD : member;
    } else if (g->items_index == -1) {
        g->items_char = member;
        g->items_start = member;
    } else {
        return items_give(g, member);
    }
    portrait_highlight(g, n);
    return items_reset(g);
}
/* A key in the items dialog: W A C M choose the category; B S I F the blacksmith's modes; E Equip (Enchant in its
 * dialog), R Remove (Recharge in its), G Item to Gold, U Use (misc), D Discard, Q the quests; Esc ends the dialog
 * (not in mode 8); 1..9 choose a row; F1..F6 a member. Any other key, or a refused one, waits at the dialog's poll
 * again. Original: 45BB9 (CS:1C81). */
static xeen_status items_key(xeen_game *g, uint16_t key)
{
    int mode = g->items_mode;
    g->option_residue = g->input_poll.last_residue; /* This key's 189E3 */
    switch (key) {
        case 'W':
        case 'A':
        case 'C':
        case 'M':
            g->items_cat = key == 'W' ? 0 : key == 'A' ? 1 : key == 'C' ? 2 : 3;
            return items_full(g);
        case 'B':
            return items_shop_mode(g, ITEMS_BUY, XEEN_DS_SCRATCH_RECORD);
        case 'S':
            return items_shop_mode(g, ITEMS_SELL, g->items_start);
        case 'I':
            return items_shop_mode(g, ITEMS_IDENTIFY, g->items_start);
        case 'F':
            return items_shop_mode(g, ITEMS_FIX, g->items_start);
        case 'E':
            if ((mode == ITEMS_OWN || mode == ITEMS_ENCHANT || mode == ITEMS_GOLD) &&
                g->items_cat != XEEN_ITEMS_MISC) {
                return option_begin(g, mode == ITEMS_ENCHANT ? 4 : 0);
            }
            break;
        case 'R':
            if (mode == ITEMS_OWN || mode == ITEMS_RECHARGE) {
                return option_begin(g, mode == ITEMS_RECHARGE ? 5 : 1);
            }
            break;
        case 'G':
            if (mode == ITEMS_GOLD) {
                return option_begin(g, 6);
            }
            break;
        case 'U':
            if (mode == ITEMS_OWN && g->items_cat == XEEN_ITEMS_MISC) {
                return option_begin(g, 2);
            }
            break;
        case 'D':
            if (mode == ITEMS_OWN) {
                return option_begin(g, 3);
            }
            break;
        case 'Q':
            if (mode == ITEMS_OWN) {
                return quests_begin(g, QUESTS_FROM_ITEMS); /* 45D4A 3E24F, then 45152 */
            }
            break;
        case XEEN_KEY_ESCAPE:
            if (mode != 8) {
                return items_end(g, 1);
            }
            break;
        default:
            if (key >= '1' && key <= '9') {
                return items_row_key(g, key - '1');
            }
            if (key >= XEEN_KEY_F1 && key <= XEEN_KEY_F6) {
                return items_member_key(g, key - XEEN_KEY_F1);
            }
            break;
    }
    return panel_set(g, ITEMS_POLL, ITEMS_POLL_BOUNDARY);
}

/* --------------------------------------------- 25679 blacksmith keys */
/*
 * A key at the blacksmith's counter: B opens the items dialog in Buy mode for the location's member (its result
 * becomes the member: location_after_items); F1..F6 choose a present member (location_member_key); every key but B
 * then redraws the location. No other key has an action here (selling and the rest are the dialog's modes).
 * Original: 25679 (44F92(member, 1); F5FE - C9h and 2A1F6), then 27F20.
 */
xeen_status shop_key(xeen_game *g, uint16_t key)
{
    if (key == 'B') {
        return items_begin(g, g->location_member, ITEMS_BUY, ITEMS_OWNER_LOCATION);
    }
    (void)location_member_key(g, key);
    return location_redraw(g);
}
/* The items dialog closed for a location: RECORD (its starting member, or 0) becomes the location's member, the key
 * is cleared and the location redraws. Original: the result of 44F92 at 25679, F5FE = 0, 27F20. */
static xeen_status location_after_items(xeen_game *g, uint16_t record)
{
    g->location_member = record;
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
    return location_redraw(g);
}

/* ------------------------------------------------ the stages' resume */
/*
 * Resumes this unit's stages: each poll stage reads one key (none: it waits again) and passes it on: the items
 * dialog's keys, the option's "which" answer, the quests (the quest_journal add-on may answer first), a mode-0
 * message's close, the Identify result's close, the character view (its tick and poll), the exchange, the details
 * (the character_details add-on's click keeps them open) and the awards. Other stages are the Cast unit's.
 */
xeen_status shop_resume(xeen_game *g, xeen_timer_bios bios, void *user)
{
    uint16_t key;
    switch (g->panel_stage) {
        case ITEMS_POLL:
            key = panel_poll(g);
            if (!key) {
                return XEEN_SERVICE_REQUIRED;
            }
            return items_key(g, key);
        case OPTION_WHICH:
            key = panel_poll(g);
            if (!key) {
                return XEEN_SERVICE_REQUIRED;
            }
            return option_which_key(g, key);
        case QUESTS_POLL: /* 3E8A3 */
            key = panel_poll(g);
            {
                xeen_status st;
                if (quests_addon_answer(g, key, &st)) {
                    /* The add-on took the input and the dialog stays at its poll: the same poll again as a new poll
                     * session (stage, boundary and status unchanged), so a replay's next key arms (Phase 330). */
                    (void)st;
                    return panel_set(g, QUESTS_POLL, QUESTS_POLL_BOUNDARY);
                }
            } /* add-on quest_journal (off: 0) */
            if (!key) {
                return XEEN_SERVICE_REQUIRED;
            }
            return quests_key(g, key);
        case MESSAGE_POLL:
            key = panel_poll(g);
            if (!key) {
                return XEEN_SERVICE_REQUIRED;
            }
            return message_closed(g);
        case IDENTIFY_POLL_STAGE: /* 3C24D */
            key = panel_poll(g);
            if (!key) {
                return XEEN_SERVICE_REQUIRED;
            }
            return identify_closed(g);
        case VIEW_TICK: /* 44C04 4EA0D, 44C09 4EA1C */
            return view_tick(g, bios, user);
        case VIEW_POLL:
            key = panel_poll(g);
            if (!key) {
                g->panel_stage = VIEW_TICK;
                g->lifecycle_boundary = VIEW_TICK_BOUNDARY;
                return XEEN_SERVICE_REQUIRED;
            }
            return view_key(g, key);
        case EXCHANGE_POLL: /* 47A42 */
            key = panel_poll(g);
            if (!key) {
                return XEEN_SERVICE_REQUIRED;
            }
            return exchange_key(g, key);
        case DETAILS_POLL: /* 43183 */
            key = panel_poll(g);
            if (!key) {
                return XEEN_SERVICE_REQUIRED;
            }
            if (key == 1u && xeen_character_details_on &&
                xeen_character_details_click(g)) { /* add-on character_details: */
                return panel_set(g, DETAILS_POLL,
                                 DETAILS_POLL_BOUNDARY); /* the click is its own; the poll again */
            }
            return details_end(g);
        case AWARDS_POLL: /* 41914 */
            key = panel_poll(g);
            if (!key) {
                return XEEN_SERVICE_REQUIRED;
            }
            return awards_key(g, key);
        default:
            return cast_resume(g, bios, user); /* Stages */
    }
}
