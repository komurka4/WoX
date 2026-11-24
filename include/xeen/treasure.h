#ifndef XEEN_TREASURE_H
#define XEEN_TREASURE_H
#include "xeen/navigation.h"
#include "xeen/items.h"

/* The pending treasure's item list of CATEGORY (0 weapons 99E8h, 1 armor 99C0h, 2 accessories 9998h, 3 misc 9970h):
 * ten 4-byte rows each, after the combat loot and the event gives (2F1AA clears them). */
/* The four pending item lists, ten 4-byte rows each, misc lowest (9970..9A0F). */
#define XEEN_TREASURE_LIST_ROWS  10u
#define XEEN_TREASURE_LIST_BYTES (XEEN_TREASURE_LIST_ROWS * XEEN_ITEM_ROW_BYTES)
#define XEEN_DS_TREASURE_WEAPONS     0x99e8u
#define XEEN_DS_TREASURE_ARMOR       0x99c0u
#define XEEN_DS_TREASURE_ACCESSORIES 0x9998u
#define XEEN_DS_TREASURE_MISC        0x9970u
static inline uint16_t xeen_treasure_list(unsigned category)
{
    static const uint16_t lists[4] = {XEEN_DS_TREASURE_WEAPONS, XEEN_DS_TREASURE_ARMOR, XEEN_DS_TREASURE_ACCESSORIES,
                                      XEEN_DS_TREASURE_MISC};
    return lists[category];
}
/* Row ROW (0..9) of the pending list of CATEGORY: its four bytes (XEEN_ITEM_ROW_*); the row is taken while its key
 * byte (XEEN_ITEM_KEY_OFFSET(CATEGORY)) is nonzero. */
static inline const uint8_t *xeen_treasure_row(const uint8_t *ds, unsigned category, unsigned row)
{
    return ds + xeen_treasure_list(category) + 4u * row;
}
/* The first free row (its key byte 0) of the pending list of CATEGORY, 0..9, or -1 when all ten are taken: where a
 * monster's drop (36CC3..) and an event's give put an item. */
static inline int xeen_treasure_free_row(const uint8_t *ds, unsigned category)
{
    for (unsigned k = 0; k < 10u; ++k) {
        if (!xeen_treasure_row(ds, category, k)[XEEN_ITEM_KEY_OFFSET(category)]) {
            return (int)k;
        }
    }
    return -1;
}

/* Gameplay mutations from 2EA68..2F22F and 43428. `preferred` is the
 * original external one-based value; zero means no selected character.
 * Invalid preference scans capacity only; valid preference first tries the
 * preferred eligible member, then eligible party order, then capacity only;
 * with no free last slot member 0's slot 8 is overwritten (2EEA7). When
 * every last slot of every category is full (2F27C), each slot iteration
 * either frees member 0's last weapon slot (pending weapon 22h at that
 * index) or discards every pending item (2EC59). Inserts into slot 8 and
 * stably compacts; misc uses byte 0 as its marker. A quest item (ID or
 * misc material >= 52h) still selects a recipient but is not inserted
 * (2EED7). The last item's name is left in the A827 text scratch, as the
 * original's 2EF29/2F0AE copies leave it. Currency uses original 32-bit
 * wrapping. */
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
xeen_status xeen_treasure_collect(xeen_movement *, unsigned preferred);
/* The same, also returning the number of 2EEA9 recipient passes (each plays
 * FX 20; quest items included). */
xeen_status xeen_treasure_collect_placed(xeen_movement *, unsigned preferred, unsigned *placed);
/* The same in the original's steps (the dialog shows each item
 * before its key): xeen_treasure_place_next runs the slot indices from
 * *cursor (0..39: category x 10 + slot), each with the 2EC59 full check,
 * until it places one item (1: *member and *category set, the name in A827)
 * or finishes (0); -1 for a bad party. xeen_treasure_compact is 43428;
 * xeen_treasure_finish the 2F1AA settlement after the key. */
int xeen_treasure_place_next(xeen_movement *, unsigned preferred, unsigned *cursor,
                             unsigned *member, unsigned *category);
void xeen_treasure_compact(xeen_movement *, unsigned member, unsigned category);
void xeen_treasure_finish(xeen_movement *);

/* 2EA68..2EB9C head, shared by every caller (volley 287FF, combat cleanup
 * 394C5, command join 17ACC, ...). NONE: signed byte 9A10 and words
 * 9A12..9A19 are all zero (2F22F). DEFERRED: 9A1C is not 9 and one of the
 * 26 visible-monster bytes 98B4..98CD is not FF; 2EA68 returns at 2F22F and
 * the pending treasure is kept for a later call. DIALOG: 2EB9C runs. */
typedef enum {
    XEEN_TREASURE_NONE,
    XEEN_TREASURE_DEFERRED,
    XEEN_TREASURE_DIALOG
} xeen_treasure_gate;
/* Where a caller hands pending treasure to the treasure dialog's owner (a boundary value; the original addresses
 * inside 2EA68). */
enum xeen_treasure_boundary {
    XEEN_TREASURE_CALL = 0x2ea68,           /* inside the treasure routine: a caller hands it the whole check and the
                                             * dialog (a volley's or an attack spell's kills), or the game waits in
                                             * its open dialog for the key */
    XEEN_TREASURE_FOUND = 0x2ea8e           /* the routine's first test found treasure (2EA8E follows it): the
                                             * dialog's owner takes over (the fight's cleanup, a join, a step) */
};
xeen_treasure_gate xeen_treasure_check(const xeen_movement *);
/* 2F27C: every member's last slot is occupied in all four categories. With
 * it, 2EC44 shows the "BackPacks Full" scroll (18F2F), which waits for a key. */
int xeen_treasure_packs_full(const xeen_movement *);
/* An open 2EA68 dialog's gameplay writes: the AC0F text scratch (2EBFB
 * "The Party Found" sprintf, then 2F14A's "Hit a key" line; item lines are
 * not reproduced) and 2EC30 9A1C=7 unless 9A1C is 2. */
xeen_status xeen_treasure_open(xeen_movement *);
/* Whether any treasure is pending in DS: an item waiting (the byte 9A10 nonzero), gems (9A12) or gold (9A16). The
 * first test of xeen_treasure_check, and the treasure prompt's "nothing to show". Original: 2EA8E's tests. */
int xeen_treasure_pending(const uint8_t *ds);
/* The found text, "The Party Found:" with the pending gold and gems of DS: FORMAT (the original's DS:6157 form, two
 * %lu: the gold, then the gems) into OUT of SIZE bytes. Returns snprintf's result. Original: 2EBF4's sprintf. */
int xeen_treasure_found_format(const uint8_t *ds, const char *format, char *out, size_t size);
/* The dialog's first text into the text scratch (XEEN_DS_TEXT_BUFFER_2, at most 1000 bytes) in the library's own
 * copy of the form (a DS without the program data works too). Returns snprintf's result. */
int xeen_treasure_found_text(xeen_movement *);
/* Nonzero while any of the four pending lists holds an item (a slot's key byte: a misc slot's byte 0, the others'
 * byte 1). */
int xeen_treasure_has_items(const xeen_movement *);
/* The key at 2F196: 2F19D 9A1C=1 unless 2, then xeen_treasure_collect_placed
 * (a failure restores 9A1C). */
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
xeen_status xeen_treasure_settle(xeen_movement *, unsigned preferred, unsigned *placed);

#endif
