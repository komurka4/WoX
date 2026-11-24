#include "xeen/party.h"
#include "xeen/items.h"
#include "xeen/item_tables.h"
#include "xeen/ds_globals.h"
#include "xeen/treasure.h"
#include "xeen/combat.h"
#include "xeen/character_record.h"
#include "xeen/byte_order.h"
#include <stdio.h>
#include <string.h>

/* The offset of a category's last row (slot 8), where a treasure item goes. */
#define LAST_ROW (XEEN_ITEM_ROW_BYTES * (XEEN_ITEM_ROWS - 1u))

/* 2EF29/2F0AE: the item's name into the A827 text scratch, through the
 * DS-relative name tables 2384/2410/2448/2474 (misc by material, byte 0);
 * quest items (52h+) get the 61B2 " * " prefix. */
static void item_name(xeen_movement *m, unsigned cat, const uint8_t item[3])
{
    static const uint16_t names[4] = XEEN_ITEM_NAME_TABLES;
    unsigned index = item[XEEN_ITEM_KEY_OFFSET(cat)];
    size_t n = 0;
    char name[201];
    uint16_t at = xeen_read16(m, (uint16_t)(names[cat] + 4u * index));
    while (n < 200u && (size_t)at + n < 65536u && m->ds[at + n]) {
        name[n] = (char)m->ds[at + n];
        ++n;
    }
    name[n] = 0;
    if (index >= XEEN_QUEST_ITEM_FIRST) {
        (void)snprintf((char *)m->ds + XEEN_DS_TEXT_BUFFER, XEEN_TEXT_BUFFER_BYTES, "\f04 * \fd%s", name);
    } else {
        memcpy(m->ds + XEEN_DS_TEXT_BUFFER, name, n + 1u);
    }
}

static const uint16_t inventory_at[4] = {
        XEEN_CHARACTER_ITEM(XEEN_ITEMS_WEAPONS, 0), XEEN_CHARACTER_ITEM(XEEN_ITEMS_ARMOR, 0),
        XEEN_CHARACTER_ITEM(XEEN_ITEMS_ACCESSORIES, 0), XEEN_CHARACTER_ITEM(XEEN_ITEMS_MISC, 0)};
/** The DS offset of member MEMBER's last row of category CAT (weapons, armor, accessories, misc). */
static unsigned last_row_at(unsigned member, unsigned cat)
{
    return XEEN_PARTY_RECORDS + member * XEEN_CHARACTER_BYTES + inventory_at[cat] + LAST_ROW;
}
/** Returns member MEMBER's last row of category CAT: where a found item goes. */
static uint8_t *member_last_row(xeen_movement *m, unsigned member, unsigned cat)
{
    return m->ds + last_row_at(member, cat);
}
/** Whether member MEMBER's last row of CAT is taken (its key byte nonzero). */
static int last_row_taken(const xeen_movement *m, unsigned member, unsigned cat)
{
    return m->ds[last_row_at(member, cat) + XEEN_ITEM_KEY_OFFSET(cat)] != 0;
}
/**
 * Returns nonzero when the last row of every category is taken for every one of the PARTY's members: a treasure
 * item has nowhere to go. Original: 2F27C counts the taken last rows over a copy of the packs and compares the count
 * divided by four with the party size, which (at most four a member) means all of them.
 */
static int party_inventory_full(const xeen_movement *m, unsigned party)
{
    for (unsigned member = 0; member < party; ++member) {
        for (unsigned cat = 0; cat < 4u; ++cat) {
            if (!last_row_taken(m, member, cat)) {
                return 0;
            }
        }
    }
    return 1;
}

/** Whether member K can receive an item of CAT: its last row free and it is able (its worst condition,
 * xeen_condition_cannot_act: 2ED15's test). */
static int can_receive(const xeen_movement *m, unsigned k, unsigned cat)
{
    uint8_t condition = xeen_character_worst_condition(m->ds + XEEN_PARTY_RECORDS + k * XEEN_CHARACTER_BYTES);
    return !last_row_taken(m, k, cat) && !xeen_condition_cannot_act(condition);
}
/* Who of the PARTY receives a found item of CAT, PREFERRED being the one-based member the player chose (any other
 * value: none). The preferred member when it can receive the item (a free last row and able to
 * act), else the first member who can; without a valid preference, or when no one can, the first member whose last
 * row is free whatever their condition; failing that member 0, whose last row is then overwritten.
 * Original: 2ED15..2EEA7. */
static unsigned treasure_recipient(const xeen_movement *m, unsigned preferred, unsigned party, unsigned cat)
{
    unsigned k;
    if (preferred >= 1u && preferred <= party) {
        if (can_receive(m, preferred - 1u, cat)) {
            return preferred - 1u;
        }
        for (k = 0; k < party; ++k) {
            if (can_receive(m, k, cat)) {
                return k;
            }
        }
    }
    for (k = 0; k < party; ++k) {
        if (!last_row_taken(m, k, cat)) {
            return k;
        }
    }
    return 0;
}
/* 2EC59, before each pending slot index SLOT (0..9) while no member has a free last row anywhere: a pending Xeen
 * Slayer Sword in row SLOT of the weapons list frees member 0's last weapon row (all four bytes); anything else there
 * makes every pending item be discarded. The weapons row is the one tested whatever the category being placed: the
 * original indexes it by the slot alone. */
static void packs_full_rule(xeen_movement *m, unsigned slot)
{
    if (xeen_treasure_row(m->ds, XEEN_ITEMS_WEAPONS, slot)[XEEN_ITEM_ROW_ID] == XEEN_WEAPON_XEEN_SLAYER_SWORD) {
        memset(member_last_row(m, 0, XEEN_ITEMS_WEAPONS), 0, XEEN_ITEM_ROW_BYTES);
    } else {
        for (unsigned c = 0; c < 4u; ++c) {
            memset(m->ds + xeen_treasure_list(c), 0, XEEN_TREASURE_LIST_BYTES);
        }
    }
}
/* Places the next pending treasure item from *CURSOR (0..39: the four categories' ten pending slots, weapons first),
 * one item a call so the dialog can show each before its key. At every slot index, first: when no member has a
 * free last row anywhere, a pending Xeen Slayer Sword at this index frees member 0's last weapon row, and any other
 * item makes the whole pending treasure be discarded. An item found goes into its recipient's last row
 * (treasure_recipient; its first three bytes, the fourth kept), except a quest item, which is only named; its name
 * goes into the text buffer. Returns 1 with *MEMBER and *CATEGORY set, 0 when no item is left, -1 without a cursor or
 * for a party of 0 or more than 6. The caller compacts the recipient's pack after showing it (xeen_treasure_compact).
 * Original: 2EC54..2F135 (the full check 2EC59, the recipient 2ED15..2EEA7, the inserts 2EEE7/2EF68/2EFDD/2F051,
 * the quest items from 2EED7, the name 2EF29/2F0AE into A827; the compaction 43428). */
int xeen_treasure_place_next(xeen_movement *m, unsigned preferred, unsigned *cursor,
                             unsigned *member, unsigned *category)
{
    unsigned party;
    if (!m || !cursor || (party = m->ds[XEEN_DS_PARTY_COUNT]) == 0u || party > 6u) {
        return -1;
    }
    for (; *cursor < 40u;) {
        unsigned cat = *cursor / 10u, slot = *cursor % 10u, selected;
        const uint8_t *src = xeen_treasure_row(m->ds, cat, slot);
        ++*cursor;
        if (party_inventory_full(m, party)) {
            packs_full_rule(m, slot);
        }
        if (!src[XEEN_ITEM_KEY_OFFSET(cat)]) {
            continue;
        }
        selected = treasure_recipient(m, preferred, party, cat);
        /* 2EEE7/2EF68/2EFDD/2F051 insert into slot 8, preserving byte 3; a
         * quest item (52h+, 2EED7..) is only named, never inserted. */
        {
            uint8_t copy[3];
            memcpy(copy, src, 3);
            if (copy[XEEN_ITEM_KEY_OFFSET(cat)] < XEEN_QUEST_ITEM_FIRST) {
                memcpy(member_last_row(m, selected, cat), copy, 3);
            }
            item_name(m, cat, copy);
        }
        if (member) {
            *member = selected;
        }
        if (category) {
            *category = cat;
        }
        return 1;
    }
    return 0;
}
/* 43428(member, category): the stable compaction after the item's line (xeen_item_sort: occupied rows keep their
 * order, it does not sort by id; a misc row moves its three bytes and leaves the fourth). */
void xeen_treasure_compact(xeen_movement *m, unsigned member, unsigned category)
{
    if (m && member < 6u && category < 4u) {
        xeen_item_sort(m, (uint16_t)(XEEN_PARTY_RECORDS + member * XEEN_CHARACTER_BYTES), category);
    }
}
/* After the treasure dialog's key (2F1AA..2F21C): the found gold and gems are added to the party's; the pending flag
 * 9A10 is cleared (not 9A11), then the found gems and gold (9A12..9A19), then the four item lists from the lowest, misc
 * (9970..9A0F: contiguous, XEEN_TREASURE_LIST_BYTES each); the input code F5FE becomes 0 and the member selector 98D7
 * 1. */
void xeen_treasure_finish(xeen_movement *m)
{
    if (!m) {
        return;
    }
    xeen_put_le32(m->ds + XEEN_DS_PARTY_GOLD,
                  xeen_get_le32(m->ds + XEEN_DS_PARTY_GOLD) + xeen_get_le32(m->ds + XEEN_DS_TREASURE_GOLD));
    xeen_put_le32(m->ds + XEEN_DS_PARTY_GEMS,
                  xeen_get_le32(m->ds + XEEN_DS_PARTY_GEMS) + xeen_get_le32(m->ds + XEEN_DS_TREASURE_GEMS));
    m->ds[XEEN_DS_TREASURE_ITEMS_PENDING] = 0;
    memset(m->ds + XEEN_DS_TREASURE_GEMS, 0, 2u * sizeof(uint32_t)); /* the gems and gold dwords */
    memset(m->ds + XEEN_DS_TREASURE_MISC, 0, 4u * XEEN_TREASURE_LIST_BYTES);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    m->ds[XEEN_DS_MEMBER_SELECTOR] = 1;
}

xeen_status xeen_treasure_collect_placed(xeen_movement *m, unsigned preferred, unsigned *placed)
{
    unsigned cursor = 0, member = 0, cat = 0, count = 0;
    int r;
    if (!m || m->ds[XEEN_DS_PARTY_COUNT] == 0u || m->ds[XEEN_DS_PARTY_COUNT] > 6u) {
        return XEEN_BAD_INPUT;
    }
    while ((r = xeen_treasure_place_next(m, preferred, &cursor, &member, &cat)) == 1) {
        xeen_treasure_compact(m, member, cat);
        ++count;
    }
    if (r < 0) {
        return XEEN_BAD_INPUT;
    }
    xeen_treasure_finish(m);
    if (placed) {
        *placed = count;
    }
    return XEEN_OK;
}

int xeen_treasure_packs_full(const xeen_movement *m)
{
    unsigned party;
    if (!m || (party = m->ds[XEEN_DS_PARTY_COUNT]) == 0u || party > 6u) {
        return 0;
    }
    return party_inventory_full(m, party);
}

int xeen_treasure_pending(const uint8_t *ds)
{
    return ds[XEEN_DS_TREASURE_ITEMS_PENDING] || xeen_get_le32(ds + XEEN_DS_TREASURE_GEMS) ||
           xeen_get_le32(ds + XEEN_DS_TREASURE_GOLD);
}

/**
 * Decides what a pending treasure (DS 9A10 items, 9A12 gems, 9A16 gold: 2F1AA's settlement) does next.
 *
 * Returns XEEN_TREASURE_NONE when there is nothing to collect, XEEN_TREASURE_DEFERRED while a
 * monster is still in the combat roster outside a monster-free encounter state (9A1C 9), else
 * XEEN_TREASURE_DIALOG.
 */
xeen_treasure_gate xeen_treasure_check(const xeen_movement *m)
{
    unsigned i;
    if (!xeen_treasure_pending(m->ds)) {
        return XEEN_TREASURE_NONE;
    }
    if (m->ds[XEEN_DS_UI_MODE] != XEEN_UI_MODE_EVENT) {
        for (i = XEEN_DS_VIEW_MONSTERS; i <= XEEN_DS_VIEW_MONSTER(XEEN_VIEW_MONSTER_SLOTS - 1u);
             ++i) {
            if (m->ds[i] != 0xffu) {
                return XEEN_TREASURE_DEFERRED;
            }
        }
    }
    return XEEN_TREASURE_DIALOG;
}

/* The library's own forms of the dialog's two texts: byte copies of the program data's DS:6157
 * (XEEN_TEXT_PARTY_FOUND) and DS:61FC (XEEN_TEXT_HIT_A_KEY). The rules write them themselves, so that they also run
 * on a DS without the program data (the rule tests' synthetic states); a game session, which has the program data,
 * formats its own copy of the found text from DS (xeen_treasure_found_format with DS:6157). */
static const char party_found[] = "\003cThe Party Found:\n\n\003r\t000%lu Gold\n%lu Gems";
static const char hit_a_key[] = "\003l\v120\t000\004077\003c\f37Hit a key\fd";
int xeen_treasure_found_format(const uint8_t *ds, const char *format, char *out, size_t size)
{
    return snprintf(out, size, format, (unsigned long)xeen_get_le32(ds + XEEN_DS_TREASURE_GOLD),
                    (unsigned long)xeen_get_le32(ds + XEEN_DS_TREASURE_GEMS));
}
int xeen_treasure_found_text(xeen_movement *m)
{
    return xeen_treasure_found_format(m->ds, party_found, (char *)m->ds + XEEN_DS_TEXT_BUFFER_2, XEEN_TEXT_BUFFER_BYTES);
}
int xeen_treasure_has_items(const xeen_movement *m)
{
    for (unsigned cat = XEEN_ITEMS_WEAPONS; cat <= XEEN_ITEMS_MISC; ++cat) {
        for (unsigned i = 0; i < XEEN_TREASURE_LIST_ROWS; ++i) {
            if (xeen_treasure_row(m->ds, cat, i)[XEEN_ITEM_KEY_OFFSET(cat)]) {
                return 1;
            }
        }
    }
    return 0;
}
/* Contract in xeen/treasure.h. The found text is formatted first (its length checked against the text buffer), then
 * the buffer is overwritten by "Hit a key": the original also formats at 2EBFB and replaces it at 2F14A. */
xeen_status xeen_treasure_open(xeen_movement *m)
{
    int length;
    if (!m || xeen_treasure_check(m) == XEEN_TREASURE_NONE) {
        return XEEN_BAD_INPUT;
    }
    length = xeen_treasure_found_text(m);
    if (length < 0 || length >= (int)XEEN_TEXT_BUFFER_BYTES) {
        return XEEN_BAD_INPUT;
    }
    memcpy(m->ds + XEEN_DS_TEXT_BUFFER_2, hit_a_key, sizeof hit_a_key);
    if (m->ds[XEEN_DS_UI_MODE] != XEEN_UI_MODE_COMBAT) {
        m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_TREASURE;
    }
    return XEEN_OK;
}
