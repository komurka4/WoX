#ifndef XEEN_MAP_OBJECTS_H
#define XEEN_MAP_OBJECTS_H
#include <stdint.h>
/* The .mob file (the map load 1A278.. and 19E82, the map save 30FC5..3109E): a 48-byte header, the sprite numbers of
 * the map's 16 object, 16 monster and 16 wall-item slots, then three lists of 4-byte entries (x, y, slot, a fourth
 * byte), each ended by an FFh FFh FFh FFh entry: the objects, the monsters, the wall items. */
#define XEEN_MOB_OBJECT_SPRITES  0u
#define XEEN_MOB_MONSTER_SPRITES 16u
#define XEEN_MOB_WALL_SPRITES    32u
#define XEEN_MOB_HEADER_BYTES    48u
#define XEEN_MOB_ENTRY_BYTES     4u
/*
 * The map-object table of the original's DS (docs/coverage/DS_GLOBALS.md, "Map objects"): one 8-byte
 * record per object of the map's .mob file, filled by 1A278.. (xeen_entities_objects) and cleared
 * (530h bytes, 166 records) with the map. The count of loaded objects is the byte at 9C5A. Code that
 * walks the table indexes it from 0 (E000 + 8 x index); the event code addresses the object numbered
 * N from 1, as DS:98D9 numbers it (DFF8 + 8 x N, the same record as E000 + 8 x (N - 1)).
 *
 * Fields (bytes; +5 is written only by the load, from the .mob entry's fourth byte, and +7 is never
 * accessed: both stay numeric):
 */
#define XEEN_DS_MAP_OBJECTS      0xe000u
#define XEEN_DS_MAP_OBJECT_COUNT 0x9c5au
#define XEEN_MAP_OBJECT_BYTES    8u
#define XEEN_MAP_OBJECTS_MAX     166u
#define XEEN_DS_NUMBERED_OBJECTS 0xdff8u /* XEEN_DS_MAP_OBJECTS - XEEN_MAP_OBJECT_BYTES */

#define XEEN_MAP_OBJECT_X 0u /* signed map x; 80h with y 80h: removed (op 14, 1A65F) */
#define XEEN_MAP_OBJECT_Y 1u /* signed map y */
/* Its sprite's index among the map's sixteen (the .mob header; its handle is at F3EA + 2 x slot). */
#define XEEN_MAP_OBJECT_SLOT   2u
#define XEEN_MAP_OBJECT_FRAME  3u /* 64h when loaded; a chest's 0/1 */
#define XEEN_MAP_OBJECT_SPRITE 4u /* the sprite number: the .mob header's entry for the slot */
/* Its facing: with the party's, the pose (XEEN_DS_OBJECT_POSES; the animation 1B572's pointer [BP-2] = E005). */
#define XEEN_MAP_OBJECT_DIRECTION 5u
/* Bit 0: drawn mirrored (the scene's view flag 8000h, 20011..); op 11 toggles it; each scene's animation sets it to
 * the pose's XEEN_OBJECT_ANIMATION_FLIP (1B6EF..1B6F6, pointer [BP-4] = E006). */
#define XEEN_MAP_OBJECT_FLIP   6u

/*
 * The map's monsters: XEEN_DS_MAP_MONSTER_COUNT records of XEEN_MAP_MONSTER_BYTES at XEEN_DS_MAP_MONSTERS, filled by
 * the map load 19E82 (the count at 19EEA/1A4C4, the records 1A364..1A3E8 from the .mob file's monster list) and by
 * the arena 3634B..365BC (the original's [bx-1AD0h].. accesses: xeen_dat_evidence/phase252/mon_fields.txt). Named
 * fields only; +2, +3 and the words +0Ch, +0Eh, +10h keep their numbers at their sites.
 */
#define XEEN_DS_MAP_MONSTERS      0xe530u
/* The object sprites' animation records (program data object_animation: 121 x 12 bytes, loaded per map by
 * xeen_entities_animation), by sprite id and then the pose's offset d (DS:4C27): +d the frame counter's reset value,
 * +4 + d the pose's flip, copied to the object's XEEN_MAP_OBJECT_FLIP (1B6EF), +8 + d the counter's limit (1B661,
 * 1B691). */
#define XEEN_DS_OBJECT_ANIMATION       0x00a2u
#define XEEN_OBJECT_ANIMATION_BYTES    12u
#define XEEN_OBJECT_ANIMATION_RESET    0u
#define XEEN_OBJECT_ANIMATION_FLIP     4u
#define XEEN_OBJECT_ANIMATION_LIMIT    8u
/* Words by a monster's XEEN_MAP_MONSTER_SPRITE_SLOT (16 each, FFFFh none: 1AA14.. at the map load, the arena 36456 and
 * 364B4): its sprite's handle (%03u.mon, the frames 0..7 the scene draws, 1EDF5 21938) and its attack sprite's
 * (%03u.att, the frames 8 up, 1EE08 21914). */
#define XEEN_DS_MONSTER_SPRITES        0xf3cau
#define XEEN_DS_MONSTER_ATTACK_SPRITES 0xf3aau
#define XEEN_MONSTER_SPRITE_SLOTS      16u
/* Words by a map object's XEEN_MAP_OBJECT_SLOT and by a wall item's XEEN_WALL_ITEM_SLOT (16 each, FFFFh none; the map
 * load 1A84B.. and 1AB46..): the object sprite's handle (%03u.obj, 1A888/1A917; drawn indoors 200C8, outdoors 21A63)
 * and the wall item picture's (%03u.pic, 1AB90; drawn 20A3A). */
#define XEEN_DS_OBJECT_SPRITES         0xf3eau
#define XEEN_DS_WALL_ITEM_SPRITES      0xf38au
#define XEEN_DS_MAP_MONSTER_COUNT 0x9c59u
#define XEEN_MAP_MONSTER_BYTES    20u
#define XEEN_MAP_MONSTER(n)       (XEEN_DS_MAP_MONSTERS + XEEN_MAP_MONSTER_BYTES * (n))
/*
 * The monster update's work arrays (2A727..2AAB9; it clears them first):
 * - MONSTER_CELL_COUNTS: 32 x 32 words, the map monsters standing on each cell (word 40h x y + 2 x x), every record
 *   counted, active or not (2A7D3); a monster moves only into a cell holding fewer than 3 (2A564..2A5B0);
 * - MONSTER_MOVED: one byte per map monster (6Bh), 1 once it moved in this update (2A5C9): the second pass skips it
 *   (2A85A);
 * - MONSTER_SHOT_SCHEDULED: one byte per map monster, 1 once this update queued its shot (2A903): not again (2A8B2).
 */
#define XEEN_DS_MONSTER_CELL_COUNTS    0x8da6u
#define XEEN_MONSTER_CELL_COUNT_BYTES  0x800u
#define XEEN_DS_MONSTER_MOVED          0xfa25u
#define XEEN_DS_MONSTER_SHOT_SCHEDULED 0xf9bau
#define XEEN_MONSTER_UPDATE_SLOTS      0x6bu
#define XEEN_MAP_MONSTER_X  0u    /* byte; with y, a record is present while both are below 20h (330A5) */
#define XEEN_MAP_MONSTER_Y  1u    /* byte */
/* A killed monster's x and y, both set by combat.c's monster_death: off the map, so the record no longer counts as
 * present (arena_begin tests either coordinate for it). */
#define XEEN_MAP_MONSTER_REMOVED 0x80u
/* Clouds map 15's test, at its load (1A5BF..1A602: party flag 56 cleared) and at its save (30E9D..30EC5: the
 * flag set): the map's first three monster records all off the map, each with x or y above 1Fh. */
static inline int xeen_map15_monsters_gone(const uint8_t *ds)
{
    unsigned i;
    for (i = 0; i < 3u; ++i) {
        if (ds[XEEN_MAP_MONSTER(i) + XEEN_MAP_MONSTER_X] <= 0x1fu &&
            ds[XEEN_MAP_MONSTER(i) + XEEN_MAP_MONSTER_Y] <= 0x1fu) {
            break;
        }
    }
    return i == 3u;
}
/* Byte: the sprite frame: 0..7 the idle cycle (the type's XEEN_MONSTER_BOUNCE runs it back and forth), 8 up the attack
 * (380B8: 8) and 11 the hit (368B6: 0Bh), held by +0Ah; the .mob load (1A405) and the arena (365BC) draw it 0..7, the
 * scene reads it (1EDDE, 218A6). */
#define XEEN_MAP_MONSTER_FRAME 2u
/* Byte: the map's monster sprite slot: the handles at F3CA/F3AA + 2 x slot (the scene 1EDEB..2192E); the .mob entry's
 * third byte (1A38C) or the arena's type (36549). */
#define XEEN_MAP_MONSTER_SPRITE_SLOT 3u
#define XEEN_MAP_MONSTER_ACTIVE 4u /* byte: nonzero once active: 2A846's update moves only these (or in 9A1C 5); op 16
                                     * clears it (3240B), the arena sets 1 (36657), the outdoor view 2126C sets it */
#define XEEN_MAP_MONSTER_ID 6u    /* byte: the monster's row in the monster data (60 bytes a row) */
#define XEEN_MAP_MONSTER_CONDITION 7u /* byte: 0, or the damage type that afflicted it (the volley's 3780A stores
                                        * 972E's low byte); the damage clears 7 and 13 (367C4..367E6), 2B5E9's save
                                        * the others */
/* Byte: 1 while a bouncing idle cycle runs back down (the scene), 0 after an attack (380CC). */
#define XEEN_MAP_MONSTER_BOUNCE_BACK 9u
/* Byte: the scenes the attack or hit frames hold: 3 at an attack (380C2), 5 at a hit (368C5). */
#define XEEN_MAP_MONSTER_FRAME_HOLD 0x0au
/* Words of its colour effect: the cycle (the type's XEEN_MONSTER_EFFECT; bit 80h flips as the step counts down; 0 after
 * a spell's hit, 3695A), the effect shown (the type's, or the spell's 3694C; 0 none) and its step, 0..7: the view
 * record's flags take word STEP of the effect's 16-byte row at 49CA (218C0..218D8). */
#define XEEN_MAP_MONSTER_EFFECT_CYCLE 0x0cu
#define XEEN_MAP_MONSTER_EFFECT 0x0eu
#define XEEN_MAP_MONSTER_EFFECT_STEP 0x10u
#define XEEN_MAP_MONSTER_HP 0x12u /* word: its hit points (the load, op 16, the damage 36A63..36AA6, Identify) */

/*
 * The wall-item table: one 8-byte record per wall item of the map's .mob file (its third list, four
 * bytes an entry), filled by 19E82's 1A4EE..1A57B after its 13FE7 cleared 2D8h bytes (91 records,
 * 19F26). The count is the byte at 9C58 (0 when the first record's sprite is FFh, 1A5B3). Operation
 * 34 (MoveWallObj) sets the position of record CBW(p6); the scene advances the frames (1B4D4) and
 * draws an item whose direction is the party's (208FB: 20A41 against ED96) with the handle at
 * F38A + 2 x slot (20A3A).
 *
 * Fields (bytes; +6 and +7 are never accessed):
 */
#define XEEN_DS_WALL_ITEMS      0xdd28u
#define XEEN_DS_WALL_ITEM_COUNT 0x9c58u
#define XEEN_WALL_ITEM_BYTES    8u
#define XEEN_WALL_ITEMS_MAX     91u

#define XEEN_WALL_ITEM_X         0u /* the .mob entry's first byte; op 34's p7 */
#define XEEN_WALL_ITEM_Y         1u /* the second; op 34's p8 */
/* The third byte: its sprite's slot among the map's (its handle is at F38A + 2 x slot). */
#define XEEN_WALL_ITEM_SLOT      2u
#define XEEN_WALL_ITEM_FRAME     3u /* the frame, advanced modulo the sprite's count 306D[sprite] */
#define XEEN_WALL_ITEM_SPRITE    4u /* the sprite number: the .mob header's entry for the slot */
/* The fourth byte: the direction it faces; it is drawn only when the party faces the same way. */
#define XEEN_WALL_ITEM_DIRECTION 5u

/*
 * Byte: the number (1-based, 0 for none) of the object the scene found at view position 0; the
 * event operations act on it (op 14's removal, the chests of op 20, 336DC's opened chest).
 */
#define XEEN_DS_EVENT_OBJECT 0x98d9u
/* Byte: the number (1-based, 0 for none) of the wall item the scene found in the party's own cell (208FB: 2092B clears
 * it, 20A99 stores it); written only, no original reader. */
#define XEEN_DS_PARTY_CELL_WALL_ITEM 0x98d8u

#endif
