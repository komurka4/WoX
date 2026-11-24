#ifndef XEEN_ITEMS_H
#define XEEN_ITEMS_H
#include <stddef.h>
#include "xeen/movement.h"
#include "xeen/character_record.h"
/* The byte that identifies an item row of CATEGORY (XEEN_ITEMS_WEAPONS..XEEN_ITEMS_MISC): a misc row's byte 0, the
 * others' id (byte 1). */
#define XEEN_ITEM_KEY_OFFSET(category) ((category) == XEEN_ITEMS_MISC ? 0u : 1u)
/* One 504DA(low,high) draw. incoming_ax is the caller's AX at the call (the
 * RNG wrapper's pre-time-service evidence); the result is the converted value. */
typedef uint16_t (*xeen_item_draw_fn)(void *user, uint16_t low, uint16_t high,
                                      uint16_t incoming_ax);
/* Original 305E8(level, DS:BC6A scratch character, slot 0, reason 0), the
 * ordinary generator shared by monster drops (36CBA) and the blacksmith wares
 * (41D20). Levels 1..7 use the original level columns of the 57B4/57BB/57C5
 * tables and the 57C8/57C9 misc ranges. Writes the selected BC6A scratch
 * record (cleared first by 43385; misc keeps byte 3), copies its three item
 * bytes to out and returns the category (0 weapon .. 3 misc). Level 0 returns
 * category 0 without a draw or write, as at 305F5. */
unsigned xeen_item_generate(xeen_movement *, uint8_t level, xeen_item_draw_fn, void *,
                            uint8_t out[3]);
/* 305E8 with its fourth argument, the reason (CS:535B by reason-1):
 * after both draws the first one's kind becomes 35 (reason 1: a weapon at
 * every level), 60 (2: armor) or 100 (3); 0 keeps it. Reason 12
 * (30644): after both draws the item is the fixed 8C74 (operation 45) and the
 * rest is drawn as usual. Enchant Item (3C63A, 3C6A2) passes 1 and 2, the
 * scripts' item operation 0 and 12. Native guards, nothing drawn or written:
 * another reason returns 0FFh (the original would index past its jump table),
 * and a level above 7 returns 0 (the original tests only level 0, 305F5, and
 * would read past its level tables). Each draw's AX is the original's: the
 * level, the sub-kind bound (100, or 80 at levels 6 and 7), then the last
 * id drawn or the value in AX before (items.c). */
unsigned xeen_item_generate_reason(xeen_movement *, uint8_t level, unsigned reason,
                                   xeen_item_draw_fn, void *, uint8_t out[3]);
/* Original 41D20, the day-ends-in-1 blacksmith regeneration called from the
 * 30D41 clock tail: clears both worlds' ware tables (EDA8/EE38/EEC8/EF58 and
 * F0CC/F15C/F1EC/F27C, 90h bytes each), then fills them with 305E8 items:
 * Clouds level idx+1 with the 5815:4 counts, Darkside level idx+1 (slots 0-1)
 * or idx+3 (slots 2-3) with the 5815:14 counts; at most 8 per category. */
void xeen_item_regenerate_wares(xeen_movement *, xeen_item_draw_fn, void *);

/* Item rules of the 44F92 items dialog over a 162h-byte character
 * record at DS offset `record` (a member at B41E + 162h*n, or the BC6A
 * scratch record that holds a blacksmith's wares). Each of the four
 * categories (0 weapons, 1 armor, 2 accessories, 3 misc) has nine rows of
 * four bytes at +A6h + 24h*category: material, id, state (40h cursed, 80h
 * broken, the low six bits a power or the charges) and the equipped slot
 * (0: carried). A misc row's key is its byte 0, the others' their id. */
uint8_t *xeen_item_at(xeen_movement *, uint16_t record, unsigned category, unsigned row);
uint8_t xeen_item_key(const xeen_movement *, uint16_t record, unsigned category, unsigned row);
/* 43385: clears a row (misc: its first three bytes). */
void xeen_item_clear(xeen_movement *, uint16_t record, unsigned category, unsigned row);
/* 43428: every empty row is cleared and takes the next occupied row below
 * it (misc moves three bytes). */
void xeen_item_sort(xeen_movement *, uint16_t record, unsigned category);
/* 3D277 (to_record 1) / 3CFF2 (0): bytes 0..2 of the 9 x 4 ware rows of the
 * town's column (the map's index in 28, 30, 73, 49 / 29, 31, 37, 43; else
 * column 0) to or from BC6A. */
unsigned xeen_ware_column(const xeen_movement *);
void xeen_wares_copy(xeen_movement *, int to_record);
/* 445E5: BC6A's equip.icn frames (its slot bytes) by id. */
void xeen_ware_icons(xeen_movement *);
/* The modes xeen_item_cost prices: the items dialog's modes of the same numbers (game_internal.h's enum items_mode,
 * which asserts that they agree). */
enum xeen_item_cost_mode {
    XEEN_ITEM_COST_BUY = 1,      /* the blacksmith's wares: the full value */
    XEEN_ITEM_COST_SELL = 2,     /* half the value, the full value with the Merchant skill */
    XEEN_ITEM_COST_FIX = 9,      /* a tenth */
    XEEN_ITEM_COST_IDENTIFY = 10, /* a hundredth */
    XEEN_ITEM_COST_TO_GOLD = 11  /* the Item to Gold spell: as Sell */
};
/* The price of item ROW of CATEGORY in the character record at DS offset RECORD in MODE, SKILL being the Merchant
 * skill byte (Sell and To Gold). A price that divides down to 0 is 1. In modes 3..6 a misc row gives its charges (the
 * state's low six bits); every other mode, and an unpriced row, gives 0. Reads DS only.
 * Original: 46FBD(record, row, mode, skill, category). */
uint32_t xeen_item_cost(const xeen_movement *, uint16_t record, unsigned row, unsigned mode,
                        unsigned skill, unsigned category);
/* 46E56 without its message: 1 when class `cls` may use the item. */
int xeen_item_allowed(const xeen_movement *, unsigned cls, unsigned id, unsigned category);
/* 475F7(record, row, A43F, colour, category): the full description into
 * DS A43F (returned). mae: MAE.xen's bytes, which the DS F65C offsets
 * address (the material and attribute names). */
/* 3C96E(row, record, category), the Identify details (into
 * 50D4:17ED in the original, DOS memory outside DS): DS:6D2D's eight
 * fields; power: a misc row's spell name (F868[28D6[power]], spells.xen
 * in DOS memory, the caller's); 0 when the text does not fit. */
int xeen_item_details(const xeen_movement *, uint16_t record, unsigned row, unsigned category,
                      const char *power, char *out, size_t n);
const char *xeen_item_text(xeen_movement *, uint16_t record, unsigned row, unsigned colour,
                           unsigned category, const uint8_t *mae, size_t mae_size);
/* An item ROW's (4 bytes) name in COLOUR, as the item lists show it, into OUT:
 * xeen_item_text's formatter without its DS write (Original: 475F7). OPTIONS
 * is 0 for the original; the add-on identify (xeen/addon/identify.h) passes
 * XEEN_ITEM_TEXT_TRIM (no weapon power name 1..6, no back-up tail
 * XEEN_TEXT_ITEM_BACK_UP) and XEEN_ITEM_TEXT_NO_MATERIAL (no material word).
 * Returns the length, 0 (and an empty OUT) when the name does not fit. */
#define XEEN_ITEM_TEXT_TRIM        1u
#define XEEN_ITEM_TEXT_NO_MATERIAL 2u
int xeen_item_text_format(const xeen_movement *, const uint8_t *row, unsigned colour,
                          unsigned category, const uint8_t *mae, size_t mae_size, unsigned options,
                          char out[200]);
/* 424C3: equip a row. done: the slot byte is set; same: the item itself
 * holds the slot (nothing); conflict: `other` of `other_category` holds it
 * (422D8's "You must remove %sto equip %s"); all: two rings (-1) or two
 * medals (-2) are worn; refused: the class may not (46E56's message). */
typedef enum {
    XEEN_EQUIP_DONE,
    XEEN_EQUIP_SAME,
    XEEN_EQUIP_CONFLICT,
    XEEN_EQUIP_ALL,
    XEEN_EQUIP_REFUSED
} xeen_equip_outcome;
typedef struct {
    xeen_equip_outcome outcome;
    int other;
    unsigned other_category;
} xeen_equip_result;
xeen_equip_result xeen_item_equip(xeen_movement *, uint16_t record, unsigned row,
                                  unsigned category);
#endif
