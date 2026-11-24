#ifndef XEEN_ADDON_IDENTIFY_H
#define XEEN_ADDON_IDENTIFY_H
#include <stddef.h>
#include <stdint.h>
#include "xeen/movement.h"
/* Owner-approved add-on identify (Phase187; NOT original XEEN.DAT behavior,
 * off unless --addon identify or wox.cfg [addons] identify = on):
 * identified item statistics. An item type (category, material, id and, for
 * weapons, the special power; not the cursed/broken flags) that the
 * blacksmith's Identify has shown becomes known; the items dialog's list then
 * appends that type's Identify statistics to the name, and draws the name of a
 * weapon, armor or accessory whose type was never identified in teal (colour
 * 19; misc items are not tracked) (presentation only: the DS is unchanged). The known types belong to a save slot: ADDON%02d.WOX
 * beside the slot's xeen/dark files, written by Save, read by Load, emptied by
 * a new game and removed with the slot (the file's base block, shared with
 * other add-ons since Phase 211: xeen/addon/slot_file.h).
 *
 * Code: src/addon/identify/ (known_items.c the known set and its file,
 * item_text.c the statistics text, item_list.inc the items-list table,
 * included by the game; the slot file service is src/addon/slot_files.inc). */
typedef struct xeen_addons xeen_addons;   /* xeen/addons.h: item_stats, known, count, capacity */

/* ---- the known item types (known_items.c) */
void xeen_identify_free(xeen_addons *);
void xeen_addons_clear_items(xeen_addons *);
/* category 0 weapon, 1 armor, 2 accessory (misc is never tracked); row: the
 * 4-byte item row (material, id, state, slot). */
int xeen_addons_item_known(const xeen_addons *,unsigned category,const uint8_t *row);
int xeen_addons_item_learn(xeen_addons *,unsigned category,const uint8_t *row);
/* Serialization: "WOXADDON", version 1, count (LE u32), keys (LE u32). */
uint8_t *xeen_addons_serialize(const xeen_addons *,size_t *size);
/* Replaces the known types; 0 (and empty) for a malformed file. */
int xeen_addons_deserialize(xeen_addons *,const uint8_t *,size_t);

/* ---- the statistics text (item_text.c) */
/* The row's 475F7 name without the weapon's power word (and, with
 * drop_material, without the material word) followed by its Identify
 * statistics in short form, e.g. "Blazing long sword 3-9 +20" (+20 in the
 * Fire colour). Nothing is written to the DS. 0 when the item has nothing
 * to add (misc, a plain accessory) or the text does not fit. */
int xeen_item_text_addon(const xeen_movement *,uint16_t record,unsigned row,unsigned colour,unsigned category,
    const uint8_t *mae,size_t mae_size,int drop_material,char *out,size_t n);
/* The same statistics as cells for a table: to-hit (weapons
 * of metal), the main value (a weapon's zero-padded "mm..MM", armor's zero-padded "ACnn"), the
 * modifier (the element, coloured, or the attribute "+nAbr"; armor's
 * resistance "+n Res"), an accessory's label (its modifier's "Res" or
 * attribute name, the modifier then only the value) and the power
 * ("x3 Kind"); "" where none. Colour codes are closed in each cell. 0 when every cell is empty. */
enum { XEEN_STAT_HIT, XEEN_STAT_MAIN, XEEN_STAT_MOD, XEEN_STAT_LABEL, XEEN_STAT_POWER, XEEN_STAT_CELLS };
typedef struct { char cell[XEEN_STAT_CELLS][24]; } xeen_item_stats;
int xeen_item_stats_cells(const xeen_movement *,uint16_t record,unsigned row,unsigned colour,unsigned category,
    xeen_item_stats *);
/* The row's name as xeen_item_text_addon starts it (no power word, no "\b "
 * tail; drop_material: no material word either). */
int xeen_item_name_addon(const xeen_movement *,uint16_t record,unsigned row,unsigned colour,unsigned category,
    const uint8_t *mae,size_t mae_size,int drop_material,char *out,size_t n);
#endif
