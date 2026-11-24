#include <stdio.h>
#include <string.h>
#include "xeen/items.h"
#include "xeen/party.h"
#include "xeen/ds_globals.h"
#include "xeen/item_tables.h"
#include "xeen/combat.h"
#include "xeen/character_record.h"

/* Original tables (analysis placement seg*16+off), one (low,high) pair per
 * level 1..7: element 57B4:14 (+14*row), attribute 57BB:6, material 57C5:A. */
static const uint8_t element_pairs[6][14] = {
        {0, 0, 1, 3, 2, 5, 3, 6, 4, 7, 5, 8, 8, 8}, {0, 0, 1, 3, 2, 5, 3, 6, 4, 7, 6, 7, 7, 7},
        {0, 0, 1, 2, 1, 3, 2, 4, 3, 5, 4, 5, 5, 5}, {0, 0, 1, 2, 1, 3, 2, 4, 3, 4, 4, 5, 5, 5},
        {0, 0, 1, 3, 2, 5, 3, 6, 4, 7, 5, 8, 8, 8}, {0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3}};
static const uint8_t attribute_pairs[10][14] = {{0, 0, 1, 4, 2, 5, 3, 6, 4, 7, 6, 10, 10, 10},
                                                {0, 0, 1, 3, 2, 5, 3, 6, 4, 7, 5, 8, 8, 8},
                                                {0, 0, 1, 3, 2, 5, 3, 6, 4, 7, 5, 8, 8, 8},
                                                {0, 0, 1, 3, 2, 5, 3, 6, 4, 7, 5, 8, 8, 8},
                                                {0, 0, 1, 2, 1, 3, 2, 4, 3, 5, 4, 6, 6, 6},
                                                {0, 0, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 6},
                                                {0, 0, 1, 2, 1, 3, 2, 4, 3, 4, 4, 5, 5, 5},
                                                {0, 0, 1, 2, 1, 3, 2, 4, 3, 5, 4, 6, 6, 6},
                                                {0, 0, 1, 2, 1, 3, 2, 4, 3, 4, 4, 5, 5, 5},
                                                {0, 0, 1, 2, 1, 4, 3, 6, 5, 8, 7, 10, 10, 10}};
static const uint8_t material_pairs[2][14] = {{0, 0, 1, 4, 3, 7, 4, 8, 5, 9, 8, 9, 9, 9},
                                              {0, 0, 1, 4, 2, 6, 4, 7, 6, 10, 9, 13, 13, 13}};
static const uint8_t element_offsets[6] = {0, 8, 15, 20, 25, 33};                     /* 57BB:0 */
static const uint8_t attribute_offsets[10] = {0, 10, 18, 26, 34, 40, 46, 51, 57, 62}; /* 57C5:0 */
static const uint8_t misc_low[8] = {0, 1, 16, 31, 41, 51, 61, 61};   /* 57C8:level */
static const uint8_t misc_high[8] = {0, 15, 30, 40, 50, 60, 73, 73}; /* 57C9:level */
static const uint8_t element_limits[5] = {25, 45, 60, 75, 95};
static const uint8_t attribute_limits[9] = {15, 25, 35, 50, 65, 80, 85, 90, 95};

/**
 * Draws one value from the (low, high) pair of LEVEL in ROW. The caller's AL is the low value, the
 * last one the original loads before the call.
 */
static uint16_t pair_draw(xeen_item_draw_fn draw, void *user, const uint8_t *row, uint8_t level)
{
    /* The low byte is the last value loaded before the call, so it is AX. */
    uint8_t low = row[2u * (level - 1u)], high = row[2u * (level - 1u) + 1u];
    return draw(user, low, high, low);
}

unsigned xeen_item_generate(xeen_movement *m, uint8_t level, xeen_item_draw_fn draw, void *user,
                            uint8_t out[3])
{
    return xeen_item_generate_reason(m, level, 0u, draw, user, out);
}
/* The item numbers across the categories (an id plus its category's first: 35 weapons, 14 armor, 11 accessories;
 * the class restriction table's offsets). */
enum { FIRST_ARMOR = 0x23, FIRST_ACCESSORY = 0x31, FIRST_MISC = 0x3c };
/* The enchantment an item draws: none (level 1), an element, an attribute or a metal; a misc item a power and its
 * charges. */
enum enchantment_kind {
    ENCHANT_NONE,
    ENCHANT_ELEMENT,
    ENCHANT_ATTRIBUTE,
    ENCHANT_METAL,
    ENCHANT_MISC_POWER
};

/* The item chosen before its enchantment: the category and the id within it, and AX at the enchantment's draw
 * (the id when it was drawn, else the value that was in AX: the sub-kind roll or the fixed item's id). */
typedef struct {
    unsigned category;
    uint16_t id, ax;
} item_choice;

/* One row of a sub-kind table: a sub-kind roll at most SUB_LIMIT gives CATEGORY and an id drawn from LOW..HIGH
 * (AX = the sub roll at the draw), or with LOW == HIGH that id, not drawn. */
typedef struct {
    uint16_t sub_limit;
    uint8_t category, low, high;
} sub_kind_row;

/* The weapons by sub-kind roll (30660..306A4). */
static const sub_kind_row weapon_rows[] = {{30, XEEN_ITEMS_WEAPONS, 1, 6},
                                           {60, XEEN_ITEMS_WEAPONS, 7, 17},
                                           {85, XEEN_ITEMS_WEAPONS, 18, 29},
                                           {0xffff, XEEN_ITEMS_WEAPONS, 30, 33}};
/* The rest at levels 2..7 above kind 60 (30703..30776): fixed armor and accessories between drawn ones. */
static const sub_kind_row other_rows[] = {
        {10, XEEN_ITEMS_ARMOR, 9, 9},          {20, XEEN_ITEMS_ARMOR, 13, 13},
        {35, XEEN_ITEMS_ACCESSORIES, 1, 1},    {45, XEEN_ITEMS_ARMOR, 10, 10},
        {55, XEEN_ITEMS_ARMOR, 11, 12},        {65, XEEN_ITEMS_ACCESSORIES, 2, 2},
        {75, XEEN_ITEMS_ACCESSORIES, 3, 7},    {80, XEEN_ITEMS_ACCESSORIES, 8, 10},
        {0xffff, XEEN_ITEMS_MISC, 1, 9}};

/* An id of CATEGORY drawn from LOW..HIGH with AX = SUB; AX becomes the id. */
static item_choice drawn_id(unsigned category, uint16_t low, uint16_t high, uint16_t sub,
                            xeen_item_draw_fn draw, void *user)
{
    uint16_t id = draw(user, low, high, sub);
    item_choice choice = {category, id, id};
    return choice;
}

/* The first row of ROWS the sub-kind roll SUB is at most (the last row takes any roll). */
static item_choice sub_kind_item(const sub_kind_row *rows, uint16_t sub, xeen_item_draw_fn draw,
                                 void *user)
{
    while (sub > rows->sub_limit) {
        ++rows;
    }
    if (rows->low == rows->high) {
        item_choice fixed = {rows->category, rows->low, sub};
        return fixed;
    }
    return drawn_id(rows->category, rows->low, rows->high, sub, draw, user);
}

/* Reason 12 (30644): the fixed item DS 8C74 (operation 45's CBW byte, compared signed), by its number across the
 * categories; AX at the enchantment's draw is the id. */
static item_choice fixed_item(const xeen_movement *m)
{
    int16_t number = (int16_t)(m->ds[XEEN_DS_FIXED_ITEM] | m->ds[XEEN_DS_FIXED_ITEM + 1u] << 8);
    item_choice choice;
    if (number < FIRST_ARMOR) {
        choice.category = XEEN_ITEMS_WEAPONS;
        choice.id = (uint16_t)number;
    } else if (number < FIRST_ACCESSORY) {
        choice.category = XEEN_ITEMS_ARMOR;
        choice.id = (uint16_t)(number - FIRST_ARMOR);
    } else if (number < FIRST_MISC) {
        choice.category = XEEN_ITEMS_ACCESSORIES;
        choice.id = (uint16_t)(number - FIRST_ACCESSORY);
    } else {
        choice.category = XEEN_ITEMS_MISC;
        choice.id = (uint16_t)(number - FIRST_MISC);
    }
    choice.ax = choice.id;
    return choice;
}

/* The item a KIND roll (1..100, or the reason's forced kind) and a SUB-kind roll choose at LEVEL: a weapon up to
 * kind 35 (40 at level 1); at level 1 armor up to 85, else a misc item; at levels 2..7 armor up to 60 (the
 * fixed id 8 above sub 70), else the other_rows. */
static item_choice drawn_item(uint8_t level, uint16_t kind, uint16_t sub, xeen_item_draw_fn draw,
                              void *user)
{
    if (kind <= (level == 1u ? 40u : 35u)) {
        return sub_kind_item(weapon_rows, sub, draw, user);
    }
    if (level == 1u) {
        return kind <= 85u ? drawn_id(XEEN_ITEMS_ARMOR, 1u, 7u, sub, draw, user)
                           : drawn_id(XEEN_ITEMS_MISC, 1u, 9u, sub, draw, user);
    }
    if (kind <= 60u) {
        if (sub <= 70u) {
            return drawn_id(XEEN_ITEMS_ARMOR, 1u, 7u, sub, draw, user);
        }
        {
            item_choice fixed = {XEEN_ITEMS_ARMOR, 8u, sub};
            return fixed;
        }
    }
    return sub_kind_item(other_rows, sub, draw, user);
}

/* The enchantment an item of CATEGORY at LEVEL takes for the roll ENCHANT (1..100): a misc item always its power;
 * none at level 1; an accessory a metal up to 20, an element up to 60, else an attribute; a weapon or armor a
 * metal up to 70, an element up to 98, else an attribute. */
static enum enchantment_kind enchantment_for(unsigned category, uint8_t level, uint16_t enchant)
{
    int accessory = category == XEEN_ITEMS_ACCESSORIES;
    if (category == XEEN_ITEMS_MISC) {
        return ENCHANT_MISC_POWER;
    }
    if (level == 1u) {
        return ENCHANT_NONE;
    }
    if (enchant <= (accessory ? 20u : 70u)) {
        return ENCHANT_METAL;
    }
    return enchant <= (accessory ? 60u : 98u) ? ENCHANT_ELEMENT : ENCHANT_ATTRIBUTE;
}

/* The drawn enchantment: an element (1..36), an attribute (1..72) or a metal (1..22) number, each a tier
 * drawn first and then a value from the tier's (low, high) pair at LEVEL plus the tier's first number; a misc
 * item's power (57C8/57C9 by level) and charges (3..10). Zero where not drawn. */
typedef struct {
    uint16_t element, attribute, material, misc, charges;
} enchantment;

/* The first row ROW of LIMITS (N of them) the roll is at most, N when above them all. */
static unsigned tier_of(const uint8_t *limits, unsigned n, uint16_t roll)
{
    unsigned row = 0;
    while (row < n && roll > limits[row]) {
        ++row;
    }
    return row;
}

/* KIND's draws (307DA..30BB7): AX at the tier draw is AX_ID, the item id's byte (AH from the zero slot offset). */
static enchantment draw_enchantment(enum enchantment_kind kind, uint8_t level, uint16_t ax_id,
                                    xeen_item_draw_fn draw, void *user)
{
    enchantment e = {0, 0, 0, 0, 0};
    unsigned row;
    switch (kind) {
        case ENCHANT_ELEMENT:
            row = tier_of(element_limits, sizeof element_limits, draw(user, 1u, 100u, ax_id));
            e.element = (uint16_t)(pair_draw(draw, user, element_pairs[row], level) +
                                   element_offsets[row]);
            break;
        case ENCHANT_ATTRIBUTE:
            row = tier_of(attribute_limits, sizeof attribute_limits, draw(user, 1u, 100u, ax_id));
            e.attribute = (uint16_t)(pair_draw(draw, user, attribute_pairs[row], level) +
                                     attribute_offsets[row]);
            break;
        case ENCHANT_METAL: {
            uint16_t roll = draw(user, 1u, 100u, ax_id);
            /* the better metals at level 7 or above 70 */
            row = (level == 7u || roll > 70u) ? 1u : 0u;
            e.material = (uint16_t)(pair_draw(draw, user, material_pairs[row], level) +
                                    (row ? 9u : 0u)); /* the upper metal tier starts at 9 */
            break;
        }
        case ENCHANT_MISC_POWER:
            e.misc = draw(user, misc_low[level], misc_high[level], misc_low[level]);
            e.charges = (uint16_t)(draw(user, 1u, 8u, e.misc) + 2u);
            break;
        case ENCHANT_NONE:
            break;
    }
    return e;
}

/* Stores E in the item row at BASE: a misc row's power and charges (its bytes 1 and 2); at levels 2..7 another
 * row's material byte, the byte sum of the element (1..36), the metal after the elements (+24h) and the attribute
 * after the metals (+3Ah) (30BB7..30BE9), and then a weapon's 1-in-20 power 1..6 (30BEE). */
static void store_enchantment(uint8_t *scratch, unsigned base, unsigned category, uint8_t level,
                              const enchantment *e, xeen_item_draw_fn draw, void *user)
{
    if (category == XEEN_ITEMS_MISC) {
        scratch[base + XEEN_ITEM_ROW_ID] = (uint8_t)e->misc;
        scratch[base + XEEN_ITEM_ROW_STATE] = (uint8_t)e->charges;
    } else if (level != 1u) {
        unsigned metal = e->material ? e->material + XEEN_ITEM_ELEMENT_MATERIALS : 0u;
        unsigned attribute =
                e->attribute ? e->attribute + XEEN_ITEM_ELEMENT_MATERIALS + XEEN_ITEM_METAL_MATERIALS : 0u;
        uint8_t value = (uint8_t)(e->element + metal + attribute);
        scratch[base + XEEN_ITEM_ROW_MATERIAL] = value;
        if (category == XEEN_ITEMS_WEAPONS && draw(user, 1u, 20u, 0u) == 10u) {
            scratch[base + XEEN_ITEM_ROW_STATE] = (uint8_t)draw(user, 1u, 6u, 10u);
        }
    }
}

/*
 * The generator in four stages, each draw in the original's order with its AX: the kind (1..100) and sub-kind
 * (1..100, 1..80 at levels 6 and 7: 3060A) rolls; the item (the fixed item for reason 12, else drawn_item with
 * the reason's forced kind); the enchantment roll; the first row of the item's category in the scratch record
 * cleared (a misc row's three bytes, another's four) and its id stored, then the enchantment drawn and stored.
 * Original: 305E8..30BF8.
 */
unsigned xeen_item_generate_reason(xeen_movement *m, uint8_t level, unsigned reason,
                                   xeen_item_draw_fn draw, void *user, uint8_t out[3])
{
    /* 30625: reasons 1..3 replace the kind roll */
    static const uint8_t forced_kind[4] = {0, 35, 60, 100};
    uint8_t *scratch = m->ds + XEEN_DS_SCRATCH_RECORD;
    uint16_t kind, sub, sub_limit, enchant;
    item_choice item;
    enchantment e;
    unsigned base;
    if (reason > 3u && reason != 12u) {
        return 0xffu;
    }
    if (!level || level > 7u) {
        return 0u;
    }
    kind = draw(user, 1u, 100u, level);
    sub_limit = level < 6u ? 100u : 80u; /* no wands at levels 6 and 7 */
    sub = draw(user, 1u, sub_limit, sub_limit);
    if (reason == 12u) {
        item = fixed_item(m);
    } else {
        item = drawn_item(level, reason ? forced_kind[reason] : kind, sub, draw, user);
    }
    enchant = draw(user, 1u, 100u, item.ax);
    base = XEEN_CHARACTER_ITEM(item.category, 0);
    memset(scratch + base, 0, item.category == XEEN_ITEMS_MISC ? 3u : 4u);
    scratch[base + XEEN_ITEM_KEY_OFFSET(item.category)] = (uint8_t)item.id;
    e = draw_enchantment(enchantment_for(item.category, level, enchant), level, (uint8_t)item.id, draw,
                         user);
    store_enchantment(scratch, base, item.category, level, &e, draw, user);
    memcpy(out, scratch + base, 3u);
    return item.category;
}

/* The blacksmiths' ware tables by world and category (weapons, armour, accessories, misc), and how many items each
 * town's smith (the table's 4-byte column SLOT) gets of each level, by world: [world][level][slot].
 * Original: EDA8/EE38/EEC8/EF58 and F0CC/F15C/F1EC/F27C; the counts 5815:4 (Clouds) and 5815:14 (Darkside). */
static const uint16_t wares_tables[2][4] = {
        {XEEN_DS_WARES(0u, 0u), XEEN_DS_WARES(0u, 1u), XEEN_DS_WARES(0u, 2u), XEEN_DS_WARES(0u, 3u)},
        {XEEN_DS_WARES(1u, 0u), XEEN_DS_WARES(1u, 1u), XEEN_DS_WARES(1u, 2u), XEEN_DS_WARES(1u, 3u)}};
static const uint8_t wares_counts[2][4][4] = {{{15, 5, 5, 5}, {5, 10, 5, 5}, {0, 5, 10, 5}, {0, 0, 0, 5}},
                                              {{10, 5, 0, 5}, {10, 5, 5, 5}, {0, 5, 5, 10}, {0, 5, 10, 0}}};
/* The ware in ROW (0..8) of town column COLUMN (0..3: xeen_ware_column) in CATEGORY's wares table of WORLD: each
 * table row holds the four towns' items side by side, four bytes each (the item's three, then one unused). */
static uint8_t *ware_at(xeen_movement *m, unsigned world, unsigned category, unsigned row, unsigned column)
{
    return m->ds + (uint16_t)(wares_tables[world][category] + XEEN_WARES_ROW_BYTES * row + 4u * column);
}
/* Stocks town SLOT's smith of WORLD: for each of the four levels its count of generated items, the item level being
 * the level + 1 (+ 3 for Darkside's last two towns); each goes into its category's next row of the smith's column, at
 * most 8 a category (an item past them is drawn and dropped). Misc rows take the item's three bytes as the others do. */
static void stock_smith(xeen_movement *m, unsigned world, unsigned slot, xeen_item_draw_fn draw, void *user)
{
    int16_t filled[4] = {0, 0, 0, 0};
    unsigned level, n, category;
    for (level = 0; level < 4u; ++level) {
        for (n = 0; n < wares_counts[world][level][slot]; ++n) {
            uint8_t item[3];
            category = xeen_item_generate(m, (uint8_t)(level + (world && slot >= 2u ? 3u : 1u)), draw, user, item);
            if (filled[category] >= 8) {
                continue;
            }
            memcpy(ware_at(m, world, category, (unsigned)filled[category], slot), item, 3u);
            ++filled[category];
        }
    }
}
void xeen_item_regenerate_wares(xeen_movement *m, xeen_item_draw_fn draw, void *user)
{
    unsigned world, slot, i;
    for (world = 0; world < 2u; ++world) {
        for (i = 0; i < 4u; ++i) {
            memset(m->ds + wares_tables[world][i], 0, XEEN_WARES_BYTES);
        }
    }
    for (world = 0; world < 2u; ++world) {
        for (slot = 0; slot < 4u; ++slot) {
            stock_smith(m, world, slot, draw, user);
        }
    }
}

/* ------------------------------------------------ item rules */
static const char *ds_string(const xeen_movement *m, uint16_t offset)
{
    return (const char *)m->ds + offset;
}
/** Returns string K of the DS table of far pointers at TABLE. */
static const char *far_string(const xeen_movement *m, uint16_t table, unsigned k)
{
    return ds_string(m, xeen_read16(m, (uint16_t)(table + 4u * k)));
}
static const uint16_t name_tables[4] = XEEN_ITEM_NAME_TABLES;

/* The item row ROW of CATEGORY (its low two bits) in the record at RECORD, read only (the DS offset wraps). */
static const uint8_t *item_row(const xeen_movement *m, uint16_t record, unsigned category, unsigned row)
{
    return m->ds + (uint16_t)(record + XEEN_CHARACTER_ITEM(category & 3u, row));
}
uint8_t *xeen_item_at(xeen_movement *m, uint16_t record, unsigned category, unsigned row)
{
    return m->ds + (uint16_t)(record + XEEN_CHARACTER_ITEM(category & 3u, row));
}
uint8_t xeen_item_key(const xeen_movement *m, uint16_t record, unsigned category, unsigned row)
{
    return item_row(m, record, category, row)[XEEN_ITEM_KEY_OFFSET(category & 3u)];
}
void xeen_item_clear(xeen_movement *m, uint16_t record, unsigned category, unsigned row)
{
    memset(xeen_item_at(m, record, category, row), 0, (category & 3u) == XEEN_ITEMS_MISC ? 3u : 4u);
}
void xeen_item_sort(xeen_movement *m, uint16_t record, unsigned category)
{
    unsigned i, k, size = (category & 3u) == XEEN_ITEMS_MISC ? 3u : 4u;
    for (i = 0; i < XEEN_ITEM_ROWS; ++i) {
        if (xeen_item_key(m, record, category, i)) {
            continue;
        }
        xeen_item_clear(m, record, category, i);
        for (k = i + 1u; k < XEEN_ITEM_ROWS; ++k) {
            if (xeen_item_key(m, record, category, k)) {
                memcpy(xeen_item_at(m, record, category, i), xeen_item_at(m, record, category, k),
                       size);
                xeen_item_clear(m, record, category, k);
                break;
            }
        }
    }
}
unsigned xeen_ware_column(const xeen_movement *m)
{
    static const uint8_t maps[2][4] = {{28, 30, 73, 49}, {29, 31, 37, 43}}; /* 5801:0 */
    unsigned i, world = m->ds[XEEN_DS_WORLD] ? 1u : 0u;
    for (i = 0; i < 4u && maps[world][i] != m->ds[XEEN_DS_PARTY_MAP]; ++i) {
    }
    return i > 3u ? 0u : i;
}
void xeen_wares_copy(xeen_movement *m, int to_record)
{
    unsigned cat, row, column = xeen_ware_column(m), world = m->ds[XEEN_DS_WORLD] ? 1u : 0u;
    for (row = 0; row < XEEN_ITEM_ROWS; ++row) {
        for (cat = 0; cat < 4u; ++cat) {
            uint8_t *ware = ware_at(m, world, cat, row, column), *item = xeen_item_at(m, XEEN_DS_SCRATCH_RECORD, cat, row);
            if (to_record) {
                memcpy(item, ware, 3u);
            } else {
                memcpy(ware, item, 3u);
            }
        }
    }
}
/*
 * The equip slot of an item by its category and id (xeen_item_equip's rules, which the wares' icons follow): a weapon
 * 1..17 takes one hand, 18..29 and 34 and above both, 30..33 is a missile weapon; armor 1..7 is worn on the body, 8 is
 * a shield, 9 a helm, 10 boots, 11 and 12 a cloak, 13 and above gauntlets; an accessory 1 is a ring, 2 a belt, 3..7 a
 * medal, 8 and above an amulet. Id 0 (an empty row) is each caller's: these return the 1..7 values for it.
 */
static uint8_t weapon_slot(unsigned id)
{
    return (uint8_t)(id <= 17u ? XEEN_SLOT_WEAPON : id <= 29u || id > 33u ? XEEN_SLOT_TWO_HANDED : XEEN_SLOT_MISSILE);
}
static uint8_t armor_slot(unsigned id)
{
    return (uint8_t)(id <= 7u    ? XEEN_SLOT_BODY
                     : id == 8u  ? XEEN_SLOT_SHIELD
                     : id == 9u  ? XEEN_SLOT_HELM
                     : id == 10u ? XEEN_SLOT_BOOTS
                     : id <= 12u ? XEEN_SLOT_CLOAK
                                 : XEEN_SLOT_GAUNTLETS);
}
static uint8_t accessory_slot(unsigned id)
{
    return (uint8_t)(id == 1u   ? XEEN_SLOT_RING
                     : id == 2u ? XEEN_SLOT_BELT
                     : id <= 7u ? XEEN_SLOT_MEDAL
                                : XEEN_SLOT_AMULET);
}
void xeen_ware_icons(xeen_movement *m)
{
    unsigned i;
    for (i = 0; i < XEEN_ITEM_ROWS; ++i) {
        uint8_t *weapon = xeen_item_at(m, XEEN_DS_SCRATCH_RECORD, XEEN_ITEMS_WEAPONS, i),
                *armor = xeen_item_at(m, XEEN_DS_SCRATCH_RECORD, XEEN_ITEMS_ARMOR, i),
                *accessory = xeen_item_at(m, XEEN_DS_SCRATCH_RECORD, XEEN_ITEMS_ACCESSORIES, i);
        /* the slots by id (an empty row's id 0 too: armor 0 gets the body's, accessory 0 the medals', where the
         * equip gives them 0 and the amulet's) */
        weapon[XEEN_ITEM_ROW_EQUIPPED] = weapon_slot(weapon[XEEN_ITEM_ROW_ID]);
        armor[XEEN_ITEM_ROW_EQUIPPED] = armor_slot(armor[XEEN_ITEM_ROW_ID]);
        accessory[XEEN_ITEM_ROW_EQUIPPED] = accessory_slot(accessory[XEEN_ITEM_ROW_ID]);
    }
}
/* The modes that ask for a price; every other mode prices nothing (a misc row in modes 3..6 gives its charges). */
static int price_mode(unsigned mode)
{
    return mode == XEEN_ITEM_COST_BUY || mode == XEEN_ITEM_COST_SELL || mode == XEEN_ITEM_COST_FIX ||
           mode == XEEN_ITEM_COST_IDENTIFY || mode == XEEN_ITEM_COST_TO_GOLD;
}
/* The divisor of a price mode's value. Buying pays the full value, fixing a tenth and identifying a hundredth. Selling
 * and Item to Gold pay half without the Merchant skill and the full value with it; the skill byte's top bit does not
 * count as the skill (the original masks it with 7Fh). Original: 46FEB..47012 picks an index into the divisor table
 * 581C {1, 2, 100, 10}: Buy 0, Sell and To Gold "skill & 7Fh is zero", Fix 3, Identify 2. */
static int32_t price_divisor(unsigned mode, unsigned skill)
{
    switch (mode) {
        case XEEN_ITEM_COST_SELL:
        case XEEN_ITEM_COST_TO_GOLD:
            return (skill & 0x7fu) ? 1 : 2;
        case XEEN_ITEM_COST_FIX:
            return 10;
        case XEEN_ITEM_COST_IDENTIFY:
            return 100;
        default: /* XEEN_ITEM_COST_BUY */
            return 1;
    }
}
/* A metal's effect on a weapon's, armour's or accessory's base price. The first four metals (37..40) cut it: to a
 * tenth, a quarter, a half and three quarters. Each later metal multiplies it by its signed factor in DS. */
static int32_t metal_price(const xeen_movement *m, int32_t base, uint8_t material)
{
    switch (material) {
        case XEEN_ITEM_FIRST_METAL:
            return base / 10;
        case XEEN_ITEM_FIRST_METAL + 1u:
            return base >> 2;
        case XEEN_ITEM_FIRST_METAL + 2u:
            return base >> 1;
        case XEEN_ITEM_FIRST_METAL + 3u:
            return base - (base >> 2);
        default:
            return base * (int8_t)m->ds[XEEN_DS_METAL_PRICE_BY_MATERIAL + material];
    }
}
/* The full value of a weapon, armour or accessory ROW: the item's base price by category and id, changed by a metal,
 * plus 100 per point of an element's damage (materials below the metals, 0 "none" among them) or of an attribute's
 * signed bonus (materials from 59). */
static int32_t equipment_value(const xeen_movement *m, const uint8_t *row, unsigned category)
{
    static const uint16_t base_prices[3] = {XEEN_DS_WEAPON_PRICE, XEEN_DS_ARMOR_PRICE, XEEN_DS_ACCESSORY_PRICE};
    uint8_t material = row[XEEN_ITEM_ROW_MATERIAL];
    int32_t base = xeen_read16(m, (uint16_t)(base_prices[category] + 2u * row[XEEN_ITEM_ROW_ID]));
    if (material < XEEN_ITEM_FIRST_METAL) {
        return base + (int32_t)m->ds[XEEN_DS_ELEMENT_DAMAGE + material] * 100;
    }
    if (material < XEEN_ITEM_FIRST_ATTRIBUTE) {
        return metal_price(m, base, material);
    }
    return base + (int32_t)(int8_t)m->ds[XEEN_DS_ATTRIBUTE_BY_MATERIAL + material] * 100;
}
/* The full value of a misc ROW: its kind's price (the material byte is the kind) plus its power's (the id byte). */
static int32_t misc_value(const xeen_movement *m, const uint8_t *row)
{
    return (int32_t)xeen_read16(m, (uint16_t)(XEEN_DS_MISC_PRICE + 2u * row[XEEN_ITEM_ROW_MATERIAL])) +
           (int32_t)xeen_read16(m, (uint16_t)(XEEN_DS_MISC_POWER_PRICE + 2u * row[XEEN_ITEM_ROW_ID]));
}
uint32_t xeen_item_cost(const xeen_movement *m, uint16_t record, unsigned row, unsigned mode,
                        unsigned skill, unsigned category)
{
    const uint8_t *s = item_row(m, record, category, row);
    int misc = (category & 3u) == XEEN_ITEMS_MISC;
    int32_t price;
    if (!price_mode(mode)) {
        /* modes 3..6 (Recharge 4 and Enchant 6 among them) read a misc row's charges */
        return misc && mode >= 3u && mode <= 6u ? (uint32_t)(s[XEEN_ITEM_ROW_STATE] & XEEN_ITEM_POWER) : 0u;
    }
    price = (misc ? misc_value(m, s) : equipment_value(m, s, category & 3u)) / price_divisor(mode, skill);
    /* a price that divides down to 0 costs 1; a negative one (a negative metal or attribute factor) is returned as
     * its 32-bit pattern */
    return price ? (uint32_t)price : 1u;
}
int xeen_item_allowed(const xeen_movement *m, unsigned cls, unsigned id, unsigned category)
{
    static const uint8_t offsets[4] = {0, FIRST_ARMOR, FIRST_ACCESSORY, FIRST_MISC}; /* 581B:0: each category's first */
    if (cls < XEEN_CLASS_ARCHER || cls > XEEN_CLASS_RANGER) {
        return 1; /* 46E73: Knights and Paladins may use every item */
    }
    /* the item's byte has one bit for each other class, Archer first: set, the class may not */
    return !(m->ds[(uint16_t)(XEEN_DS_ITEM_CLASS_RESTRICTION + id + offsets[category & 3u])] &
             (1u << (cls - XEEN_CLASS_ARCHER))); /* 581A:class-2 */
}
/* What every item name shows of the row's state byte: the name's colour, "broken " and "cursed " (each in its own
 * colour) for those bits, and whether either is set (damaged): a damaged item's name hides its material, its power and
 * the "of" and always ends with the back-up tail. */
typedef struct {
    unsigned colour;
    int damaged;
    const char *broken, *cursed;
} item_name_state;

static item_name_state item_state_words(const xeen_movement *m, const uint8_t *row, unsigned colour)
{
    unsigned flags = row[XEEN_ITEM_ROW_STATE] & (XEEN_ITEM_BROKEN | XEEN_ITEM_CURSED);
    item_name_state s = {colour, flags != 0, "", ""};
    if (flags & XEEN_ITEM_BROKEN) {
        s.broken = ds_string(m, XEEN_TEXT_ITEM_BROKEN);
    }
    if (flags & XEEN_ITEM_CURSED) {
        s.cursed = ds_string(m, XEEN_TEXT_ITEM_CURSED);
    }
    return s;
}
/* A misc item's name: its kind (the row's material byte), then "of" and its power's name (the id byte). With no power
 * (id 0) or when damaged, the kind alone and the back-up tail. A damaged item shows no power name; an undamaged one
 * with id 0 shows power 0's name with no "of" before it. */
static int misc_item_text(const xeen_movement *m, const uint8_t *row, const item_name_state *s, char out[200])
{
    int bare = s->damaged || !row[XEEN_ITEM_ROW_ID];
    return snprintf(out, 200, ds_string(m, XEEN_TEXT_ITEM_MISC_FORMAT), s->colour, s->broken, s->cursed, s->colour,
                    far_string(m, XEEN_DS_MISC_NAMES, row[XEEN_ITEM_ROW_MATERIAL]),
                    bare ? "" : ds_string(m, XEEN_TEXT_ITEM_OF),
                    s->damaged ? "" : far_string(m, XEEN_DS_MISC_POWER_NAMES, row[XEEN_ITEM_ROW_ID]),
                    bare ? ds_string(m, XEEN_TEXT_ITEM_BACK_UP) : "");
}
/* A weapon's, armour's or accessory's material word: the MAE.xen name its material byte selects, empty when damaged,
 * without a MAE block, past its end, or with XEEN_ITEM_TEXT_NO_MATERIAL. */
static const char *equipment_material(const xeen_movement *m, const uint8_t *row, const item_name_state *s,
                                      const uint8_t *mae, size_t mae_size, unsigned options)
{
    uint16_t offset = xeen_read16(m, (uint16_t)(XEEN_DS_MATERIAL_NAMES + 4u * row[XEEN_ITEM_ROW_MATERIAL]));
    if (s->damaged || !mae || offset >= mae_size || (options & XEEN_ITEM_TEXT_NO_MATERIAL)) {
        return "";
    }
    return (const char *)mae + offset;
}
/* A weapon's, armour's or accessory's name (CATEGORY 0..2): its material, its name by id, and for a weapon its power
 * (the state byte without the cursed bit). An undamaged item with no power (state 0) and any damaged one end with the
 * back-up tail. XEEN_ITEM_TEXT_TRIM (the add-on identify) drops that tail and the powers 1..6. */
static int equipment_item_text(const xeen_movement *m, const uint8_t *row, unsigned category, const item_name_state *s,
                               const char *material, unsigned options, char out[200])
{
    unsigned state = row[XEEN_ITEM_ROW_STATE];
    const char *name = far_string(m, name_tables[category], row[XEEN_ITEM_ROW_ID]);
    const char *tail = s->damaged || !state ? ds_string(m, XEEN_TEXT_ITEM_BACK_UP) : "";
    if (options & XEEN_ITEM_TEXT_TRIM) {
        tail = "";
    }
    if (category != XEEN_ITEMS_WEAPONS) {
        return snprintf(out, 200, ds_string(m, XEEN_TEXT_ITEM_ARMOR_FORMAT), s->colour, material, s->broken,
                        s->cursed, s->colour, name, tail);
    }
    {
        const char *power = s->damaged ? "" : far_string(m, XEEN_DS_WEAPON_POWER_NAMES, state & (uint8_t)~XEEN_ITEM_CURSED);
        if ((options & XEEN_ITEM_TEXT_TRIM) && (state & XEEN_ITEM_POWER) >= 1u && (state & XEEN_ITEM_POWER) <= 6u) {
            power = "";
        }
        return snprintf(out, 200, ds_string(m, XEEN_TEXT_ITEM_WEAPON_FORMAT), s->colour, material, s->broken,
                        s->cursed, s->colour, name, power, tail);
    }
}
/* Upper-cases the first letter of an item's name (an ASCII letter only): the character after its colour code (3
 * bytes), or after the second colour code when no state word comes between them. Original: 47944 (toupper). */
static void capitalize_item_name(char out[200], int n)
{
    unsigned at = (uint8_t)out[3] == 0x0cu ? 6u : 3u;
    if ((unsigned)n > at && out[at] >= 'a' && out[at] <= 'z') {
        out[at] = (char)(out[at] - 0x20);
    }
}
/* An item ROW's name, as the item lists show it, into OUT (200 bytes, the original's buffer): a misc item's or a
 * weapon's, armour's or accessory's (CATEGORY's low two bits), in COLOUR, with its state words, capitalized. A name
 * that does not fit is empty (0). OPTIONS is 0 for the original; the add-on identify (src/addon/identify/item_text.c)
 * passes XEEN_ITEM_TEXT_TRIM (no weapon power name 1..6 and no back-up tail, so its statistics follow the name's own
 * trailing space) and XEEN_ITEM_TEXT_NO_MATERIAL (no material word). Returns the text's length.
 * Original: 475F7 without its write into DS (xeen_item_text does that). */
int xeen_item_text_format(const xeen_movement *m, const uint8_t *s, unsigned colour,
                          unsigned category, const uint8_t *mae, size_t mae_size, unsigned options,
                          char out[200])
{
    item_name_state state = item_state_words(m, s, colour);
    int n;
    if ((category & 3u) == XEEN_ITEMS_MISC) {
        n = misc_item_text(m, s, &state, out);
    } else {
        n = equipment_item_text(m, s, category & 3u, &state, equipment_material(m, s, &state, mae, mae_size, options),
                                options, out);
    }
    if (n < 0 || n >= 200) {
        n = 0;
        out[0] = 0;
    }
    capitalize_item_name(out, n);
    return n;
}
const char *xeen_item_text(xeen_movement *m, uint16_t record, unsigned row, unsigned colour,
                           unsigned category, const uint8_t *mae, size_t mae_size)
{
    char out[200];
    int n = xeen_item_text_format(m, xeen_item_at(m, record, category, row), colour, category, mae,
                                  mae_size, 0u, out);
    memcpy(m->ds + XEEN_DS_TEXT_BUFFER_0, out, (size_t)n + 1u);
    return ds_string(m, XEEN_DS_TEXT_BUFFER_0);
}

/* The first row (0..8) of category CAT of character RECORD whose equipped slot (row byte 3) is A or B, else -1. */
static int equipped_row(xeen_movement *m, uint16_t record, unsigned cat, uint8_t a, uint8_t b)
{
    unsigned k;
    for (k = 0; k < XEEN_ITEM_ROWS; ++k) {
        uint8_t slot = xeen_item_at(m, record, cat, k)[XEEN_ITEM_ROW_EQUIPPED];
        if (slot == a || slot == b) {
            return (int)k;
        }
    }
    return -1;
}
/* The rows of category CAT of character RECORD worn in SLOT. */
static unsigned worn_count(xeen_movement *m, uint16_t record, unsigned cat, uint8_t slot)
{
    unsigned k, count = 0;
    for (k = 0; k < XEEN_ITEM_ROWS; ++k) {
        if (xeen_item_at(m, record, cat, k)[XEEN_ITEM_ROW_EQUIPPED] == slot) {
            ++count;
        }
    }
    return count;
}
/*
 * The result when row OTHER of OTHER_CATEGORY (OTHER >= 0) already takes the slot that row ROW of CATEGORY needs:
 * SAME when it is that row itself, else CONFLICT naming the other row; DONE (with no row) when OTHER is -1.
 */
static xeen_equip_result slot_taken(unsigned row, unsigned category, int other, unsigned other_category)
{
    xeen_equip_result r = {XEEN_EQUIP_DONE, -1, 0};
    if (other >= 0) {
        r.outcome = (other == (int)row && other_category == category) ? XEEN_EQUIP_SAME : XEEN_EQUIP_CONFLICT;
        r.other = other;
        r.other_category = other_category;
    }
    return r;
}
/*
 * A weapon: the class must be allowed it. A hand weapon (one- or two-handed) needs no other hand weapon equipped,
 * and a two-handed one no shield; a missile weapon needs no other missile weapon. Original: from 424F7 (the id
 * tests 424F7, 4254D, 425DE).
 */
static xeen_equip_result equip_weapon(xeen_movement *m, uint16_t record, unsigned row, unsigned cls)
{
    uint8_t *item = xeen_item_at(m, record, XEEN_ITEMS_WEAPONS, row), slot = weapon_slot(item[XEEN_ITEM_ROW_ID]);
    xeen_equip_result r = {XEEN_EQUIP_REFUSED, -1, 0};
    if (!xeen_item_allowed(m, cls, item[XEEN_ITEM_ROW_ID], XEEN_ITEMS_WEAPONS)) {
        return r;
    }
    if (slot == XEEN_SLOT_MISSILE) {
        r = slot_taken(row, XEEN_ITEMS_WEAPONS,
                       equipped_row(m, record, XEEN_ITEMS_WEAPONS, XEEN_SLOT_MISSILE, XEEN_SLOT_MISSILE),
                       XEEN_ITEMS_WEAPONS);
    } else {
        r = slot_taken(row, XEEN_ITEMS_WEAPONS,
                       equipped_row(m, record, XEEN_ITEMS_WEAPONS, XEEN_SLOT_WEAPON, XEEN_SLOT_TWO_HANDED),
                       XEEN_ITEMS_WEAPONS);
        if (r.outcome == XEEN_EQUIP_DONE && slot == XEEN_SLOT_TWO_HANDED) {
            r = slot_taken(row, XEEN_ITEMS_WEAPONS,
                           equipped_row(m, record, XEEN_ITEMS_ARMOR, XEEN_SLOT_SHIELD, XEEN_SLOT_SHIELD),
                           XEEN_ITEMS_ARMOR);
        }
    }
    if (r.outcome == XEEN_EQUIP_DONE) {
        item[XEEN_ITEM_ROW_EQUIPPED] = slot;
    }
    return r;
}
/*
 * Armor: body armor and shields (ids 1..8) must be allowed to the class, other armor is not tested. The piece's slot
 * must be free, and a shield needs no two-handed weapon. An empty row (id 0) takes slot 0, so the first carried
 * armor row holds it (SAME when that is the row itself). Original: from 42642 (the class tests 42669, 426BC).
 */
static xeen_equip_result equip_armor(xeen_movement *m, uint16_t record, unsigned row, unsigned cls)
{
    uint8_t *item = xeen_item_at(m, record, XEEN_ITEMS_ARMOR, row);
    unsigned id = item[XEEN_ITEM_ROW_ID];
    uint8_t slot = id ? armor_slot(id) : 0u;
    xeen_equip_result r = {XEEN_EQUIP_REFUSED, -1, 0};
    if (id <= 8u && !xeen_item_allowed(m, cls, id, XEEN_ITEMS_ARMOR)) {
        return r;
    }
    r = slot_taken(row, XEEN_ITEMS_ARMOR, equipped_row(m, record, XEEN_ITEMS_ARMOR, slot, slot), XEEN_ITEMS_ARMOR);
    if (r.outcome == XEEN_EQUIP_DONE && id == 8u) {
        r = slot_taken(row, XEEN_ITEMS_ARMOR,
                       equipped_row(m, record, XEEN_ITEMS_WEAPONS, XEEN_SLOT_TWO_HANDED, XEEN_SLOT_TWO_HANDED),
                       XEEN_ITEMS_WEAPONS);
    }
    if (r.outcome == XEEN_EQUIP_DONE) {
        item[XEEN_ITEM_ROW_EQUIPPED] = slot;
    }
    return r;
}
/*
 * An accessory (no class test): a ring or a medal may be worn twice, so a third (two already worn, the row itself
 * counted if it is one of them) is ALL, with other -1 for rings and -2 for medals; a belt or an amulet needs its
 * slot free. An empty row (id 0) takes the amulet's slot. Original: from 427ED (the ring count 42805..42827).
 */
static xeen_equip_result equip_accessory(xeen_movement *m, uint16_t record, unsigned row)
{
    uint8_t *item = xeen_item_at(m, record, XEEN_ITEMS_ACCESSORIES, row);
    unsigned id = item[XEEN_ITEM_ROW_ID];
    uint8_t slot = id ? accessory_slot(id) : XEEN_SLOT_AMULET;
    xeen_equip_result r = {XEEN_EQUIP_DONE, -1, 0};
    if (slot == XEEN_SLOT_RING || slot == XEEN_SLOT_MEDAL) {
        if (worn_count(m, record, XEEN_ITEMS_ACCESSORIES, slot) > 1u) {
            r.outcome = XEEN_EQUIP_ALL;
            r.other = id == 1u ? -1 : -2;
            r.other_category = XEEN_ITEMS_ACCESSORIES;
        }
    } else {
        r = slot_taken(row, XEEN_ITEMS_ACCESSORIES, equipped_row(m, record, XEEN_ITEMS_ACCESSORIES, slot, slot),
                       XEEN_ITEMS_ACCESSORIES);
    }
    if (r.outcome == XEEN_EQUIP_DONE) {
        item[XEEN_ITEM_ROW_EQUIPPED] = slot;
    }
    return r;
}
/**
 * Equips row ROW of CATEGORY for character RECORD by the category's rule (equip_weapon, equip_armor,
 * equip_accessory): DONE writes the row's slot byte; any other outcome writes nothing. A misc item is not equipped
 * (SAME). The category is the caller's 0..3; another value is treated as misc.
 */
xeen_equip_result xeen_item_equip(xeen_movement *m, uint16_t record, unsigned row,
                                  unsigned category)
{
    unsigned cls = m->ds[(uint16_t)(record + XEEN_CHARACTER_CLASS)];
    xeen_equip_result same = {XEEN_EQUIP_SAME, -1, 0};
    switch (category) {
        case XEEN_ITEMS_WEAPONS:
            return equip_weapon(m, record, row, cls);
        case XEEN_ITEMS_ARMOR:
            return equip_armor(m, record, row, cls);
        case XEEN_ITEMS_ACCESSORIES:
            return equip_accessory(m, record, row);
        default:
            return same; /* 424E1: misc is not equipped */
    }
}

/*
 * 3C96E's string tables and texts (src/data/text); its rule tables are xeen/item_tables.h's.
 */
#define DS_ELEMENT_NAMES         0x2896u /* by element category */
#define DS_ATTRIBUTE_NAMES       0x28aeu /* by attribute category */
#define DS_MONSTER_CLASS_NAMES   0x2620u /* by the state's monster class */
#define DS_TEXT_ALL              0x6cf9u /* "All" */
#define DS_TEXT_NONE             0x6cfdu /* "None": every detail's value before the rules set it (3CA78..3CAE0) */
#define DS_FORMAT_SIGNED         0x6d02u /* "%+d" */
#define DS_FORMAT_DAMAGE_RANGE   0x6d06u /* "%d to %d" */
#define DS_FORMAT_ELEMENT_DAMAGE 0x6d0fu /* "%+d %s Damage" */
#define DS_FORMAT_SIGNED_NAME    0x6d1du /* "%+d %s" */
#define DS_FORMAT_TIMES_THREE    0x6d24u /* "x3 vs %s" */
#define DS_TEXT_ITEM_DETAILS     0x6d2du /* "Proficient Classes..." with eight %s */
/* An item's attribute bonus (a material above 3Ah): "%+d %s" (DS_FORMAT_SIGNED_NAME) of the material's signed bonus
 * and its attribute's name. */
static void attribute_text(const xeen_movement *m, uint8_t material, char *out, size_t n)
{
    (void)snprintf(out, n, ds_string(m, DS_FORMAT_SIGNED_NAME),
                   (int)(int8_t)m->ds[(uint16_t)(XEEN_DS_ATTRIBUTE_BY_MATERIAL + material)],
                   far_string(m, DS_ATTRIBUTE_NAMES, xeen_combat_attribute_category(material)));
}
/* 3C96E(row, record, category). [BP-50h] (28h bytes, cleared):
 * for classes 0..9 that 46E56(class, id, 1, category) allows (no message),
 * the first two letters of DS:286A[class] and a space; exactly 30 letters
 * (all ten) become "All". The seven other fields start as "None" (6CFD).
 * The id is byte 1 (misc: byte 0). By category (CS:15D5):
 * - weapon: min = 2250[id], max = 2250[id] x 2273[id]; a material 37..58
 *   adds 20EF[material] to both and makes the hit "%+d" of 20D9[material]
 *   (signed); the damage "%d to %d" (min below 1 shows 0); a material
 *   below 36 with 20D9 != 0 "%+d %s Damage" (2896[2A26D]), else a
 *   material above 58 "%+d %s" of 211B (signed) and 28AE[2A29D]; the
 *   state's low six bits "x3 vs %s" (2620);
 * - armor: a material below 36 with 20B4 != 0 "%+d %s" (the resistance,
 *   2896[the first of 50D2:0..4 >= material, at most 5]), else above 58
 *   the attribute; then the AC "%+d" of 2242[id] (+ 2105[material],
 *   signed, for 37..58);
 * - accessory: the resistance or the attribute as armor, no AC;
 * - misc: a power: the spell name DS:[F868 + 4 x 28D6[power]].
 * The text is DS:6D2D with classes, hit, damage, elemental damage,
 * resistance, AC, attribute, special power. */
/* The classes field's room: ten classes' "Kn " (30 bytes), the terminator and spare. */
enum { CLASSES_BYTES = 40 };
/* The details text's eight fields, each "None" unless the item has it. */
typedef struct {
    char classes[CLASSES_BYTES], hit[12], damage[12], element[40], resistance[40], armor_class[12], attribute[40],
            power[40];
} detail_texts;
/* CLASSES: the first two letters of each class (of ten) that may use item ID of CATEGORY, each followed by a space,
 * or "All" when every class may. A list longer than the buffer keeps counting but stops storing (it cannot be: ten
 * classes take 30 bytes). */
static void class_initials(const xeen_movement *m, unsigned id, unsigned category,
                           char classes[CLASSES_BYTES])
{
    unsigned class_id, len = 0;
    memset(classes, 0, CLASSES_BYTES);
    for (class_id = 0; class_id < 10u; ++class_id) {
        if (xeen_item_allowed(m, class_id, id, category)) {
            const char *name = far_string(m, XEEN_DS_CLASS_NAMES, class_id);
            if (len + 3u < CLASSES_BYTES) {
                classes[len] = name[0];
                classes[len + 1] = name[1];
                classes[len + 2] = ' ';
            }
            len += 3u;
        }
    }
    if (len == 30u) { /* all ten classes */
        memcpy(classes, ds_string(m, DS_TEXT_ALL), 4);
    }
}
/* A weapon's fields: a metal's to-hit and damage bonuses, the damage range (dice to dice x sides, the minimum shown
 * as 0 below 1), an element's damage or an attribute, and the power against a monster class ("x3 vs ..."). */
static void weapon_details(const xeen_movement *m, const uint8_t *item, unsigned id, detail_texts *t)
{
    uint8_t material = item[XEEN_ITEM_ROW_MATERIAL];
    int min_damage = m->ds[(uint16_t)(XEEN_DS_WEAPON_DICE + id)],
        max_damage = (int)m->ds[(uint16_t)(XEEN_DS_WEAPON_DICE + id)] * m->ds[(uint16_t)(XEEN_DS_WEAPON_SIDES + id)];
    if (material > XEEN_ITEM_ELEMENT_MATERIALS && material < XEEN_ITEM_FIRST_ATTRIBUTE) { /* a metal */
        min_damage += (int8_t)m->ds[(uint16_t)(XEEN_DS_METAL_DAMAGE_BY_MATERIAL + material)];
        max_damage += (int8_t)m->ds[(uint16_t)(XEEN_DS_METAL_DAMAGE_BY_MATERIAL + material)];
        (void)snprintf(t->hit, sizeof t->hit, ds_string(m, DS_FORMAT_SIGNED),
                       (int)(int8_t)m->ds[(uint16_t)(XEEN_DS_METAL_TO_HIT_BY_MATERIAL + material)]);
    }
    (void)snprintf(t->damage, sizeof t->damage, ds_string(m, DS_FORMAT_DAMAGE_RANGE), min_damage < 1 ? 0 : min_damage,
                   max_damage);
    if (material < XEEN_ITEM_ELEMENT_MATERIALS) {
        int bonus = m->ds[(uint16_t)(XEEN_DS_ELEMENT_DAMAGE + material)];
        if (bonus) {
            (void)snprintf(t->element, sizeof t->element, ds_string(m, DS_FORMAT_ELEMENT_DAMAGE), bonus,
                           far_string(m, DS_ELEMENT_NAMES, xeen_combat_element_category(material)));
        }
    } else if (material >= XEEN_ITEM_FIRST_ATTRIBUTE) {
        attribute_text(m, material, t->attribute, sizeof t->attribute);
    }
    if (item[XEEN_ITEM_ROW_STATE] & XEEN_ITEM_POWER) { /* the power: a monster class */
        (void)snprintf(t->power, sizeof t->power, ds_string(m, DS_FORMAT_TIMES_THREE),
                       far_string(m, DS_MONSTER_CLASS_NAMES, item[XEEN_ITEM_ROW_STATE] & XEEN_ITEM_POWER));
    }
}
/* An armor piece's or an accessory's fields (KIND): an element's resistance or an attribute; armor also its AC, a
 * metal's AC bonus added. The element is the first category whose materials reach the item's (as the resistance
 * rules choose it). Original: 3CCCF (the first of 50D2:0..4 >= material, at most 5: 2A26D's answer below 36). */
static void protection_details(const xeen_movement *m, unsigned kind, unsigned id, uint8_t material, detail_texts *t)
{
    if (material < XEEN_ITEM_ELEMENT_MATERIALS) {
        int bonus = m->ds[(uint16_t)(XEEN_DS_ELEMENT_RESISTANCE + material)];
        if (bonus) {
            (void)snprintf(t->resistance, sizeof t->resistance, ds_string(m, DS_FORMAT_SIGNED_NAME), bonus,
                           far_string(m, DS_ELEMENT_NAMES, xeen_combat_element_category(material)));
        }
    } else if (material >= XEEN_ITEM_FIRST_ATTRIBUTE) {
        attribute_text(m, material, t->attribute, sizeof t->attribute);
    }
    if (kind == XEEN_ITEMS_ARMOR) {
        int ac = m->ds[(uint16_t)(XEEN_DS_ARMOR_AC + id)];
        if (material > XEEN_ITEM_ELEMENT_MATERIALS && material < XEEN_ITEM_FIRST_ATTRIBUTE) {
            ac += (int8_t)m->ds[(uint16_t)(XEEN_DS_METAL_AC_BY_MATERIAL + material)];
        }
        (void)snprintf(t->armor_class, sizeof t->armor_class, ds_string(m, DS_FORMAT_SIGNED), ac);
    }
}
int xeen_item_details(const xeen_movement *m, uint16_t record, unsigned row, unsigned category,
                      const char *spell, char *out, size_t n)
{
    const unsigned kind = category & 3u; /* 0 weapon, 1 armor, 2 accessory, 3 misc */
    /* The item row: material, id, state, equipped slot (misc: the id is byte 0). */
    const uint8_t *item = item_row(m, record, kind, row);
    unsigned id = kind == XEEN_ITEMS_MISC ? item[XEEN_ITEM_ROW_MATERIAL] : item[XEEN_ITEM_ROW_ID];
    /* The material tests compare with 24h, 3Ah and 3Bh as the original (3CB26..3CE5E): an element only below 36, so
     * the last element material, 36, shows no element here (the combat and price rules count it). */
    detail_texts t;
    int written;
    class_initials(m, id, category, t.classes);
    strcpy(t.hit, ds_string(m, DS_TEXT_NONE));
    strcpy(t.damage, ds_string(m, DS_TEXT_NONE));
    strcpy(t.element, ds_string(m, DS_TEXT_NONE));
    strcpy(t.resistance, ds_string(m, DS_TEXT_NONE));
    strcpy(t.armor_class, ds_string(m, DS_TEXT_NONE));
    strcpy(t.attribute, ds_string(m, DS_TEXT_NONE));
    strcpy(t.power, ds_string(m, DS_TEXT_NONE));
    if (kind == XEEN_ITEMS_WEAPONS) {
        weapon_details(m, item, id, &t);
    } else if (kind != XEEN_ITEMS_MISC) {
        protection_details(m, kind, id, item[XEEN_ITEM_ROW_MATERIAL], &t);
    } else if (item[XEEN_ITEM_ROW_ID]) { /* a misc item's power */
        (void)snprintf(t.power, sizeof t.power, "%s", spell ? spell : "");
    }
    written = snprintf(out, n, ds_string(m, DS_TEXT_ITEM_DETAILS), t.classes, t.hit, t.damage, t.element,
                       t.resistance, t.armor_class, t.attribute, t.power);
    return written >= 0 && (size_t)written < n;
}
