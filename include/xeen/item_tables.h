#ifndef XEEN_ITEM_TABLES_H
#define XEEN_ITEM_TABLES_H
/*
 * The item rule tables of the initialized DS (src/data/tables.json ids, Phase 186), shared by
 * the item screens and prices (characters/items.c), combat (combat/combat.c) and the world entry's
 * maximum HP (world/world_entry.c). Each is indexed as the original reader indexes it: by an item
 * id, or by a material through an origin (a table's offset minus its first material). Where one
 * origin serves two adjacent tables, as in the original, each use has its own name with the same
 * value. Bytes are unsigned unless the table is int8 (read through an int8_t cast); the price
 * tables are 16-bit words.
 */

/* The item names by category (far-pointer tables of the program data's p2384, one per category, the entries the
 * names with their trailing space: weapons "long sword ".., armor "robes ".., accessories "ring ".., misc "rod "..).
 * XEEN_ITEM_NAME_TABLES initializes a by-category array of them. */
#define XEEN_DS_WEAPON_NAMES    0x2384u
#define XEEN_DS_ARMOR_NAMES     0x2410u
#define XEEN_DS_ACCESSORY_NAMES 0x2448u
#define XEEN_DS_MISC_NAMES      0x2474u
#define XEEN_ITEM_NAME_TABLES {XEEN_DS_WEAPON_NAMES, XEEN_DS_ARMOR_NAMES, XEEN_DS_ACCESSORY_NAMES, XEEN_DS_MISC_NAMES}

/* A material byte: 0 none, 1..36 an element, 37..58 a metal, 59..130 an attribute (the tables below). */
#define XEEN_ITEM_ELEMENT_MATERIALS 36u
#define XEEN_ITEM_METAL_MATERIALS   22u
#define XEEN_ITEM_FIRST_METAL       37u
#define XEEN_ITEM_FIRST_ATTRIBUTE   59u
#define XEEN_ITEM_LAST_ATTRIBUTE    130u

/* By material 0..36 (the element materials). */
#define XEEN_DS_ELEMENT_RESISTANCE 0x20b4u /* material_element_resistance */
#define XEEN_DS_ELEMENT_DAMAGE     0x20d9u /* material_element_damage */

/* By material 37..58 (the metals), int8. */
#define XEEN_DS_METAL_TO_HIT_BY_MATERIAL 0x20d9u /* metal_to_hit (20FE) */
#define XEEN_DS_METAL_DAMAGE_BY_MATERIAL 0x20efu /* metal_damage (2114) */
#define XEEN_DS_METAL_AC_BY_MATERIAL     0x2105u /* metal_armor_class (212A) */
#define XEEN_DS_METAL_PRICE_BY_MATERIAL  0x211bu /* metal_price_factor (2140), materials 41..58 */

/* By material 59..130 (the attribute materials), int8. */
#define XEEN_DS_ATTRIBUTE_BY_MATERIAL 0x211bu /* attribute_material_bonus (2156) */

/* By item id. */
#define XEEN_DS_WEAPON_PRICE    0x219eu /* weapon_base_price, words, weapon ids 0..34 */
#define XEEN_DS_ARMOR_PRICE     0x21e4u /* armor_base_price, words, armor ids 0..13 */
#define XEEN_DS_ACCESSORY_PRICE 0x2200u /* accessory_base_price, words, ids 0..10 */
#define XEEN_DS_MISC_PRICE      0x2216u /* misc_base_price, words, misc ids 0..21 */
#define XEEN_DS_ARMOR_AC        0x2242u /* armor_ac, armor ids 0..13 */
#define XEEN_DS_WEAPON_DICE     0x2250u /* weapon_dice_count, weapon ids 0..34 */
#define XEEN_DS_WEAPON_SIDES    0x2273u /* weapon_dice_sides, weapon ids 0..34 */
/* item_class_restriction: by id + the category's offset (0, 35, 49, 60); bit class - 2 set = the
 * class may not use the item. */
#define XEEN_DS_ITEM_CLASS_RESTRICTION 0x2296u
#define XEEN_DS_MISC_POWER_PRICE       0x22ecu /* misc_power_price, words, by a misc power 0..75 */

#endif
