#ifndef XEEN_MAP_RECORD_H
#define XEEN_MAP_RECORD_H

#include <stdint.h>
/*
 * A map's record (a mazeNNNN.dat, 37Ch bytes): resources.c's xeen_map_record and its accessors, and the nine loaded
 * copies in DS (XEEN_DS_MAP_DATA: slot S at XEEN_DS_MAP_DATA + XEEN_MAP_RECORD_BYTES x S, slot 0 the party's map).
 * Named are the fields whose readers agree; the others stay numeric at their sites (+30Ah, the 16 bytes at +30Eh,
 * +32Fh.. +33Bh except the threshold).
 */
#define XEEN_MAP_RECORD_BYTES 0x37cu
/* 16 x 16 words, the cell at (x, y) at 20h x y + 2 x x: the walls by facing (4 bits each) or, outdoors, the surface
 * kind in the low nibble (xeen_map_tile). */
#define XEEN_MAP_RECORD_TILES 0x000u
/* 16 x 16 bytes, the cell at 10h x y + x: indoors the surface kind in the low 3 bits (xeen_map_metadata). */
#define XEEN_MAP_RECORD_METADATA 0x200u
/* Word: the map's id. Slot 0's names the map save's member (30F34, save.c save_map); the location entry clears it
 * after its map save, the location's leave restores the party's map, the panel's dialog and the menu (2B292) clear
 * it. */
#define XEEN_MAP_RECORD_ID 0x300u
/* Words: the neighbouring maps, 0 for none (xeen_map_neighbor 0..3): past y 15 (facing north steps +y), past x 15,
 * below y 0, below x 0. */
#define XEEN_MAP_RECORD_NORTH 0x302u
#define XEEN_MAP_RECORD_EAST  0x304u
#define XEEN_MAP_RECORD_SOUTH 0x306u
#define XEEN_MAP_RECORD_WEST  0x308u
/* Word: the map's mode: 8000h outdoors (the cache copies it to F608), 4000h dark (the view's light level is 0 without
 * the party's Light: 1C952). */
#define XEEN_MAP_RECORD_MODE 0x30cu
#define XEEN_MAP_MODE_DARK   0x4000u
/* 16 bytes: an outdoor wall kind's class (the cell word's bits 4..7), 0 none: the outdoor load's wall sprite
 * "<stem>.wal" of DS:4CA8 + 4 x class ("", "mount", "ltree", "dtree", "grass", "snotree", "dsnotree", "snomnt",
 * "dedltree", "mount", "lavamnt", "palm", "dmount", "dedltree" x 3), and the outdoor minimaps' tile class + 10h
 * (1E4E3, 1DC0D). The party's and the monsters' gates test the kinds themselves (xeen_outdoor_mountain_kind). */
#define XEEN_MAP_RECORD_WALL_CLASSIFICATION 0x30eu
/* 16 bytes: each surface kind's class (xeen_map_classification; the cell query treats classes 10 and 15 as seen). */
#define XEEN_MAP_RECORD_CLASSIFICATION 0x31eu
/*
 * The surface classes (classification values): each is the surface sprite DS:4CE8 + 4 x class ("<stem>.srf") that
 * both map loads read for it (outdoors 1ACFB/1AF04, indoors 1AED8). Their rules by class:
 *   - after a step (17277): lava 100 fire damage; desert outdoors needs a Navigator; sky, or cloud without
 *     Levitate, a fall (100 physical damage and the fall scene); space defeats the party;
 *   - the party's step (17814) outdoors: deep water needs Walk on Water, water a Swimmer or it, space refuses;
 *     a monster's step (2A5D1) outdoors: water and space need a flier (water also the Clouds Water Dragon);
 *   - the cell query (1C7D0): sky and space count as seen;
 *   - the view (22034, 246CD) animates water, lava and sewer indoors, deep water and lava outdoors.
 */
#define XEEN_SURFACE_WATER      0u  /* "water.srf" */
#define XEEN_SURFACE_DIRT       1u  /* "dirt.srf" */
#define XEEN_SURFACE_GRASS      2u  /* "grass.srf" */
#define XEEN_SURFACE_SNOW       3u  /* "snow.srf" */
#define XEEN_SURFACE_SWAMP      4u  /* "swamp.srf" */
#define XEEN_SURFACE_LAVA       5u  /* "lava.srf" */
#define XEEN_SURFACE_DESERT     6u  /* "desert.srf" */
#define XEEN_SURFACE_ROAD       7u  /* "road.srf" */
#define XEEN_SURFACE_DEEP_WATER 8u  /* "dwater.srf" */
#define XEEN_SURFACE_TFLR       9u  /* "tflr.srf" */
#define XEEN_SURFACE_SKY        10u /* "sky.srf" */
#define XEEN_SURFACE_CROAD      11u /* "croad.srf" */
#define XEEN_SURFACE_SEWER      12u /* "sewer.srf" */
#define XEEN_SURFACE_CLOUD      13u /* "cloud.srf" */
#define XEEN_SURFACE_SCORTCH    14u /* "scortch.srf" */
#define XEEN_SURFACE_SPACE      15u /* "space.srf" */
/*
 * Outdoor wall kinds (a cell's wall nibble, F48E) that the party's gate (17814) and the monsters' (2A5D1) refuse by
 * number, without the map's kind -> class table at record +30Eh: 1, 7, 9, 10 and 12. Where a map gives a kind its own
 * class, as most shipped outdoor maps do, those are the mountains of the wall sprites DS:4CA8 + 4 x class ("mount",
 * "snomnt", "mount", "lavamnt", "dmount"); Clouds map 11 gives kind 8 the lava mountain's class and kind 9 the palm's,
 * and the tests stay on 9 and 10 (Phase 254 census).
 */
static inline int xeen_outdoor_mountain_kind(uint16_t kind)
{
    return kind == 1u || kind == 7u || kind == 9u || kind == 10u || kind == 12u;
}
/* Byte (xeen_map_threshold): indoors, the wall values below it are passed, by the party's step (1789A) and Bash's
 * (17E63), the monsters (2A6CF) and the combat's range tiers (281A7's tests 284B1..286F0). The shipped indoor maps
 * give 7 (234 of 235) or 6 (Darkside map 141). */
#define XEEN_MAP_RECORD_THRESHOLD 0x330u
/*
 * Indoor wall kinds: the 4-bit value of each side of an indoor cell (the cell word's nibbles at shift 12 north, 8 east,
 * 4 south, 0 west; 1C1C1 reads one by a view or step entry, 1C680 by mask and shift). The original gives a kind no one
 * meaning: each consumer has its own rule (Phase 257, xeen_dat_evidence/phase257/WALL_KINDS.md).
 *   - Passage: a side whose kind is below XEEN_MAP_RECORD_THRESHOLD is passed (the step, Bash, the monsters, the
 *     party's volley tiers). Under the shipped thresholds, 0..6 are passed and 7..15 are not.
 *   - A monster's shot (419F7) stops at a side with XEEN_INDOOR_WALL_STOPS_SHOTS (bit 3: kinds 8..15); Jump (3A112)
 *     fails at any nonzero kind.
 *   - Unlock (Space at 17B8F, its table CS:0F14) knows two pairs: the doors 1 and 13 (title "Open Door") and the
 *     grates 6 and 9 ("Open Grate"). Turning 13 into 1, or 9 into 6, takes a pick against XEEN_MAP_RECORD_DOOR_LOCK,
 *     unless the cell's lock is open (its flag 80h, XEEN_DS_CELL_UNLOCKED: set by a pick, 171B9, or by the map).
 *   - Bash (3D689) ends without a roll at 7, 14 and 15; it needs XEEN_MAP_RECORD_BASH_GRATE at 9 and
 *     XEEN_MAP_RECORD_BASH at any other kind (3D818 tests 4 and 13 by name, yet reads +336h for them too: an original
 *     quirk, kept). An opened side becomes XEEN_INDOOR_WALL_BASHED in both cells.
 *   - The view (1C9D4, the centre surface 24494) and the minimap (1E6AB) draw 5 as 8 and 4 as 13. Stepping through a
 *     face of kind 4 or 13 plays the door scene (178E2); under the shipped thresholds only kind 4 is passed.
 * The other kinds (2, 4, 5, 7, 8, 10, 11, 12, 14, 15) differ in these rules and in their view sprites and frames only;
 * they stay numbers.
 */
#define XEEN_INDOOR_WALL_DOOR         1u  /* a door: Unlock's pick turns 13 into it */
#define XEEN_INDOOR_WALL_BASHED       3u  /* what a successful Bash leaves */
#define XEEN_INDOOR_WALL_GRATE        6u  /* a grate: Unlock's pick turns 9 into it */
#define XEEN_INDOOR_WALL_LOCKED_GRATE 9u  /* a grate a pick opens; 6 becomes it at once */
#define XEEN_INDOOR_WALL_LOCKED_DOOR  13u /* a door a pick opens; 1 becomes it only in a cell with the flag 80h */
#define XEEN_INDOOR_WALL_STOPS_SHOTS  8u  /* the kind bit that stops a monster's shot */
/* Bytes: the map's run position, the cell 2125F (only reader, 2125F/21265) puts the party on: after a Run from
 * combat, Time Distortion, an event's x FFFFh, the arena's end. */
#define XEEN_MAP_RECORD_RUN_X 0x32fu
#define XEEN_MAP_RECORD_RUN_Y 0x338u
/* Bytes: the locks of the map's doors (the Unlock command 1715D: 17180, 1719F) and chests (operation 20: 2DFAA,
 * 2DFC9). A pick needs the thievery value + 504DA(1, 20) at least the lock, and is worth 10 x the levels x it in
 * experience. */
#define XEEN_MAP_RECORD_DOOR_LOCK  0x332u
#define XEEN_MAP_RECORD_CHEST_LOCK 0x333u
/* Bytes: the Might (both bashers' and 504DA(1, 30)) a Bash needs, against a locked grate (kind 9: 3D829) and any
 * other wall it can open (3D82E). The byte before them, +334h, has no reader. */
#define XEEN_MAP_RECORD_BASH_GRATE 0x335u
#define XEEN_MAP_RECORD_BASH       0x336u
/* Byte: the party's chance to run from a combat: Run succeeds when 504DA(1, 100) is below it (38B0C). */
#define XEEN_MAP_RECORD_RUN_CHANCE 0x337u
/* Byte: the damage of the map's traps, 2E68D's damage argument (a chest's trap 2DF77, the Unlock command's 1714D). */
#define XEEN_MAP_RECORD_TRAP_DAMAGE 0x339u
/* Byte: the town tavern's next tip, 0..14 (Darkside 0..9) after its first (F658); only the tavern's tip (25C89)
 * reads and advances it, and it is saved with the map. */
#define XEEN_MAP_RECORD_TAVERN_TIP 0x33bu
/* Byte +33Ah: an indoor map's style, the base name of its .sky, .gnd, .til, .fwl and .swl files (DS:4C90's names:
 * the map load 1AD7B..1AF9C) and its song group (DS:187A, styles 0..5); also tested by Unlock (170F1: the Darkside
 * towers' kind-9 walls) and the door frame (244A2). Read only (11 original reads). */
#define XEEN_MAP_RECORD_STYLE 0x33au
enum xeen_map_style {
    XEEN_MAP_STYLE_TOWN = 0,     /* "town" */
    XEEN_MAP_STYLE_CAVE = 1,     /* "cave" */
    XEEN_MAP_STYLE_TOWER = 2,    /* "towr" */
    XEEN_MAP_STYLE_CASTLE = 3,   /* "cstl" */
    XEEN_MAP_STYLE_DUNGEON = 4,  /* "dung" */
    XEEN_MAP_STYLE_SCIFI = 5,    /* "scfi"; 6 has no name, 7 "mount" */
    XEEN_MAP_STYLE_MOUNTAIN = 7
};
/* 32 bytes: one seen bit per cell (bit 80h >> (cell & 7) of byte cell >> 3, cell = 10h x y + x). */
#define XEEN_MAP_RECORD_SEEN 0x35cu
#endif
