#ifndef XEEN_DS_GLOBALS_H
#define XEEN_DS_GLOBALS_H
#include <stdint.h>
#include "xeen/map_record.h"
#include "xeen/ds_texts.h"
/*
 * Names of frequently used variables in the original's data segment (offsets into the DS image,
 * e.g. m->ds). A name is given only where every original reader and writer uses the bytes the
 * same way: docs/coverage/DS_GLOBALS.md lists each one's width, writers, readers and evidence, and
 * the addresses that stay numeric because their meaning depends on the context.
 */

/*
 * The segment word the native game stores in DS for a far pointer into the program's own data (the button and
 * routing tables, the modal and button stacks; MENU_SEGMENT before Phase 256): 4853h in a continuous session.
 * Outside one, the event dispatch is given XEEN_ANALYSIS_DS_SEGMENT, the data segment of the analysis load at 1000h
 * (DS:0 at 581D0h; include/xeen/event_dispatch.h). Under DOS the value depends on the load address.
 */
#define XEEN_DS_SEGMENT          0x4853u
#define XEEN_ANALYSIS_DS_SEGMENT 0x581du

/*
 * Byte: the world side, 0 Clouds, 1 Darkside: the side whose archive (xeen.cc or dark.cc), maps and tables are
 * current. 249 byte accesses, no word access over it (0091 and 0096 are separate words, 0095 a separate byte).
 * Its writers store only a side: the new game 191DE (1 and 0), 2AD6D (0, 1, DS 95 xor 1), Town Portal 3B0BF (the
 * destination's FB00), and the save-pair repair 18330/18363, which switches to the other side while it writes that
 * side's save and restores the saved side at 185DD.
 */
#define XEEN_DS_WORLD 0x94u

/*
 * Byte: 1 when the Clouds side is installed: its one writer, 15D15, stores 1 once 1AFB found xeen.cc (DS:41A6) at
 * start-up; else it keeps 0 (Darkside alone). 55 byte reads (the menu's options, the new game, the save pair, the
 * archive choice). The neighbours 0094 (byte) and 0096 (word) are separate variables.
 */
#define XEEN_DS_CLOUDS_INSTALLED 0x95u

/*
 * Byte: the side (0 Clouds, 1 Darkside) of the map that is or will next be loaded: the map load 19E82 copies it to
 * FB00 (0 for maps 71h..7Fh) and chooses that side's archive by it; the CD voice side test 19CC3 and the archive
 * selections before the side's data loads read it. Its writers store a side: the new game ([94], 1927B), the
 * restart side (Clouds when installed, else Darkside: 29B9C/39595), the Sphinx (the other side, 33F9C), op 59's
 * parameter (3347A), Lloyd's Beacon (the beacon's side, 3A845), Town Portal (FB00, 3B0BC), the cutscenes' Darkside
 * destinations and the falls; 19CC3 saves it, sets 0 and restores it.
 */
#define XEEN_DS_MAP_SIDE 0x96e2u
/* Bytes: the current game's name block, the maze.nam image (31 bytes: 19292/1997C load it, the saves write it): the
 * name, NUL-terminated (the name entry's copy from A827, 2B356/2B55F; the panel's "Current Game = %s" 29883 and the
 * save messages 29539.. print it), and at + 1Dh XEEN_DS_MAP_SIDE. Nine immediate uses (`phase253/imm_96c5.txt`). */
/*
 * XEEN.CFG's image: the start-up reads 29 bytes (47C1F), the main menu's D (16ECF) and the endings' tail (33536) write
 * 30. SOUND_DRIVER (word): the sound driver, 1..10 (audio.c's selection: 1 the PC speaker; the next words are its
 * port, the music driver and its port); a voice plays only from 2 up (185F0, 34276, 35FC4). CLOUDS_ENDING: the Clouds
 * ending's mark, a string the ending copies from the one at *(DS:96) (334E7) and the start-up compares with it
 * (15F69, 14DA8). DARKSIDE_ENDING / WORLD_ENDING (bytes): 1 after the Darkside ending (op 57, 334B4) / the World
 * ending (op 58, 3349F), read by Other Options (its E and W, 16B49..16F15). 8D52 (written only) stays numeric.
 */
/* Word: slot 0 of the program data's far-pointer table p0096 (offset 0096, segment 0098): the Clouds ending's mark,
 * the text t30A2 "ereiamjh". Its two readers: the ENDGAME operation copies the mark into XEEN.CFG's Clouds-ending
 * field (334DE/334E2: 14CB6(8D42, *(DS:96))) and the start-up compares that field with it (15F60/15F64: 14DA8(8D42,
 * *(DS:96), 8) into XEEN_DS_CLOUDS_ENDING_UNMARKED). The table's slots 1 and 2 are darkside_prefix and clouds_prefix. */
#define XEEN_DS_CLOUDS_ENDING_MARK 0x0096u
/* Byte: the start-up's comparison of XEEN.CFG's Clouds-ending field with the mark (15F74, its one writer: 14DA8's
 * difference, 0 when the field holds the mark, so the Clouds ending was seen); Other Options reads it (16B37, 16C60,
 * 16F00: V, the Clouds ending, is offered only while it is 0). */
#define XEEN_DS_CLOUDS_ENDING_UNMARKED 0x8c80u
#define XEEN_DS_CONFIG                 0x8d38u
#define XEEN_DS_CONFIG_SOUND_DRIVER    0x8d38u
#define XEEN_DS_CONFIG_CLOUDS_ENDING   0x8d42u
#define XEEN_DS_CONFIG_DARKSIDE_ENDING 0x8d4eu
#define XEEN_DS_CONFIG_WORLD_ENDING    0x8d4fu
#define XEEN_CONFIG_READ_BYTES         29u
#define XEEN_CONFIG_BYTES              30u
#define XEEN_DS_GAME_NAME     0x96c5u
#define XEEN_GAME_NAME_BYTES  31u
/* The name record (maze.nam, loaded at XEEN_DS_GAME_NAME): the game's name, then at byte 1Dh the side whose
 * archive is selected (96C5 + 1Dh is XEEN_DS_MAP_SIDE) and at byte 1Eh the save pair's id (XEEN_DS_PAIR_ID). */
#define XEEN_GAME_NAME_SIDE    0x1du
#define XEEN_GAME_NAME_PAIR_ID 0x1eu
/* Word: 1 only while a new game writes its slot files (2B572 sets it, 2B57D clears it, around the save 19278); the
 * save dialog's open also clears it (2AD85). Its two readers: the save pairs both worlds' files when it is set
 * (192D1), and the pair's name record then takes the name record's byte 20 (DS 96D9) as its side (182E6..182ED). */
#define XEEN_DS_NEW_GAME_SAVE 0xfa90u
/* The save-file listing (18E77): FILE_LIST_COUNT (word) the names found, at most 10; FILE_LIST the names, 14 bytes
 * each. The listing is their only writer (18E7B..18EC7) but for the name entry's end, which puts the chosen slot's
 * Darkside file name into the first entry (2B2FB); the main menu's Load tests the count (16AAE). */
#define XEEN_DS_FILE_LIST_COUNT   0x8c94u
#define XEEN_DS_FILE_LIST         0x8c96u
#define XEEN_FILE_LIST_ENTRY_BYTES 14u
#define XEEN_FILE_LIST_ENTRIES     10u

/* Byte: 1 after a spell changed the map (Lloyd's Beacon 3A85E, Town Portal 3B0C2): the Cast dialog's and the items
 * dialog's entries clear it (3D8CC, 44F98), and the items dialog's close then keeps its sprites (46219), which the
 * map load replaced. */
#define XEEN_DS_MAP_RELOADED 0x96e1u

/* Byte: 1 while a text or number entry is open (the name entry 28955, 28ABA's 28ADF; its previous value restored at
 * 28AAC/28BAC): the interface leaves out the compass text then (266F8). */
#define XEEN_DS_TEXT_ENTRY 0x8c52u

/* Bytes: a 2000-byte scratch area at 9C6F that several dialogs reuse: the spell lists' 50-byte lines (the far
 * pointer XEEN_DS_SPELL_LIST_LINES points at it), the item dialog's nine 100-byte lines (453C8), the character
 * view's text (47DFC), and the save's copy of the party file across a save (game_save.c). */
#define XEEN_DS_SCRATCH       0x9c6fu
#define XEEN_DS_SCRATCH_BYTES 0x7d0u

/* The current map's texts ("%s%c%03u.txt", loaded by 1A1B0..1A20D, its size in 8582): zero-terminated strings, the
 * first the map's name (the M map's caption 1E38C). Past the initialized data. */
#define XEEN_DS_MAP_TEXTS 0x8c53u

/*
 * Word: 8000h on an outdoor map, 0 indoors: the map record's mode word (+30Ch, DS C0D8) masked with 8000h. Its one
 * writer is the map load (19E82's 1A1A1, the native's cache commit); 55 word reads (the step, the scene builders,
 * combat, the spells, the cell queries). The neighbours F606 (word) and F60A (byte) are separate variables.
 */
#define XEEN_DS_OUTDOORS 0xf608u
/* Word: bit 10h of the queried cell's metadata byte (xeen_metadata_at, 1C93C; its neighbours F48A..F47E hold the
 * other bits): the cell's events run when the party steps on it (17ADE, 17ACC's step cells), and again after a fight
 * there (39570) and in the event dispatch (33853). */
#define XEEN_DS_CELL_STEP_EVENT 0xf482u
/* Words: bits 40h and 20h of that byte, each with one reader: Rest refused, "Too dangerous to rest here!" (28E4D);
 * a dark cell, costing the party one of its Light (1C96A). F480 (08h, the ground's variant) stays numeric. */
#define XEEN_DS_CELL_NO_REST 0xf488u
#define XEEN_DS_CELL_DARK    0xf486u
/* Word: bit 80h of that byte, the cell's lock open: Unlock is its only reader (170E0, 1710A): in such a cell the door
 * or grate opens without a pick, and only there can a door be locked again (xeen/map_record.h's indoor wall kinds).
 * A successful pick sets the bit (171B9: 2DE91(x, y, 80h)); 354 cells of 68 shipped indoor maps start with it. */
#define XEEN_DS_CELL_UNLOCKED 0xf48au
/* Words: the far pointer (offset, segment) of the event texts' block (writers 16150/16153, 31696/31699, 317E9/317EC;
 * readers 2F9F8.., 31575.., 318D8..): the map's texts, which the large text window points at the F42C block while it
 * shows and puts back after. */
#define XEEN_DS_EVENT_TEXTS_OFFSET  0x9c5cu
#define XEEN_DS_EVENT_TEXTS_SEGMENT 0x9c5eu
/* Word: that block's size, written with the pointer (2C8FF, 317E0; kept and put back at 3167A/31683), its texts'
 * bound (2C68B, 2C6BD). */
#define XEEN_DS_EVENT_TEXTS_SIZE    0x9c52u
/* Words: event text k's offset in that block (the text index writes them, 2C6A7; the readers add them to the
 * pointer, 2F9FE, 3157B, 316B0, 31819, 318DE, 32871). */
#define XEEN_DS_EVENT_TEXT_OFFSETS  0x9a1eu
/* Byte: the text index's count of the block's NULs (2C6C3..2C6C8: its entry count minus 1), the index's one write;
 * no XEEN.DAT instruction reads it (a full scan of the code finds only that store). */
#define XEEN_DS_EVENT_TEXT_TERMINATORS 0x97f4u
/* int8 x 10, the program data's race_sp_bonus: the spell points per level a race adds for the class's casting stat,
 * rows race 0..4, columns Intellect, Personality. Its one reader, the maximum spell points 46470, reads
 * [bx + 298F] with bx = 2 x race + stat (stat 1 Intellect for sorcerers and archers, 2 Personality; 46500, CBW), so
 * the reader's base is one byte before the table: XEEN_DS_RACE_SP_BONUS_BASE. */
#define XEEN_DS_RACE_SP_BONUS      0x2990u
#define XEEN_DS_RACE_SP_BONUS_BASE 0x298fu
/* Words of the map's event script (the .EVT load 2C5FD..2C8C6): its size in bytes (2C8C6 from the loaded file's
 * size 8582; the record scan's bound 2C7FA; passed to the event readers and the map save), its record count (cleared
 * 2C6D6, one per length-prefixed record 2C7E7; the record bound 31431, 32283..), and the largest record count of any
 * map loaded so far (2C604/2C60A, its only two accesses). */
#define XEEN_DS_EVENT_BYTES            0x9c56u
#define XEEN_DS_EVENT_RECORD_COUNT     0x9c54u
#define XEEN_DS_EVENT_RECORD_COUNT_MAX 0x9a1au
/* Words: the far pointer (offset, segment) of the buffer that event script is read into. One writer, the game's start
 * 16146/1614A (5056A's DX:AX: the runtime's free-memory segment, offset 0; set once, never moved); the .EVT load
 * reads the file there (2C6ED: 4A1B7(9C4E, 9C62:9C64, 9C56)) and scans its records (2C724..2C7EB), and the
 * interpreter reads them through it (31569, 31AB5.., 32ADA..). The native script lives in its own memory (all DOS
 * memory is segment 0 here): the pointer is 0:0. */
#define XEEN_DS_EVENT_SCRIPT_OFFSET  0x9c62u
#define XEEN_DS_EVENT_SCRIPT_SEGMENT 0x9c64u
/* Byte: the target an event's party damage takes (2E68D's third argument, damage(amount, type, target), at 2CA07 and
 * 2DBC9: the take and give damage modes); the event-variable store sets it (2E5B7) and each event's start clears it
 * (313F3). */
#define XEEN_DS_EVENT_DAMAGE_TARGET 0x97f5u
/* Byte: 1 once the party has eaten since the last dawn (Rest's meal 2909B, an event's 2DBEE); at dawn a party that
 * has not grows weaker, each member's Weak + 1 (30D78), and it is cleared (30DBD). */
#define XEEN_DS_PARTY_FED 0xf01eu
/* Byte: the step sound's alternation: each step flips it (17D74) and plays sound 7 + it (17D79), so steps alternate
 * sounds 7 and 8; the load's reset clears it (17944, with the flip words 1796B..). */
#define XEEN_DS_STEP_SOUND 0x9775u
/* Words: the map's sprite handles by kind (FFFFh none), filled by the map's sprite load: the surfaces' (16, by
 * surface kind through XEEN_MAP_RECORD_CLASSIFICATION; 1AED8 indoors, 1AD07 outdoors), read by the terrain
 * slots; the walls' (outdoors kind k's class "%s.wal" 1ACCE; indoors kind 0 none and the style's ".fwl" files). */
#define XEEN_DS_SURFACE_SPRITES 0xf432u
/* The map load's name tables (Phase 293; far pointers, string K the offset word at TABLE + 4 K, read live as the
 * original's push word [bx+TABLE]): the sky by world or, for the time of day's reload, by the night flag (1AC49,
 * 1AD39, 2A145); a style's base name for its .sky/.gnd/.til/.fwl/.swl (1AE4D..1AFAA), whose entries from 6 on (DS 4CA8)
 * are the outdoor wall kinds' names (1ACAC); a surface kind's sprite (1ACFB, 1AF04); the Clouds' song by group
 * (1AD90, 1ADBF; group 5 is the first Darkside entry) and the Darkside's, seven a group (1AC0B, 1AC2D, 1ADFC, 1AE33);
 * and style_song_group, the group of an indoor style 0..5 (bytes; 1AD82..1AE1A). */
#define XEEN_DS_SKY_NAMES          0x2a3cu
#define XEEN_DS_STYLE_NAMES        0x4c90u
#define XEEN_DS_OUTDOOR_WALL_NAMES (XEEN_DS_STYLE_NAMES + 0x18u)
#define XEEN_DS_SURFACE_NAMES      0x4ce8u
#define XEEN_DS_CLOUDS_SONGS       0x17beu
#define XEEN_DS_DARKSIDE_SONGS     0x17d2u
#define XEEN_DS_STYLE_SONG_GROUP   0x187au
/* Two effect cues (1880E's number, the music driver's cue script) by the one event every original caller plays them
 * for (phase255/cue_census.txt: 14 and 11 original sites): 15h where an action is refused (too little gold or gems,
 * a failed or refused spell, a full inn, no one able to shoot, an object event from the wrong side, ..), 33h where
 * the party is moved (Teleport, Jump, Town Portal, Lloyd's Beacon, the mirror, Mr Wizard, the combat's Run). The
 * other cues stay numbers: their callers' events are mixed (14h, 1Eh) or single. */
#define XEEN_CUE_REFUSED     0x15u
#define XEEN_CUE_TRANSPORTED 0x33u
/* Word: the first sprite handle of the map's sprites: the map load stores the next handle at its start (179C3) and
 * frees every sprite from it when it reloads (1A7A6), as the location exit does (27802). */
#define XEEN_DS_MAP_SPRITE_BASE 0x98d0u
/* Bytes by member (6): the member's portrait has flashed (2B9C7 skips a flashed member, 2B9FC, and marks it at its
 * end, 2BABF); cleared before each sequence that flashes (310E6, 33B20, 39783, 3980A). */
#define XEEN_DS_MEMBER_FLASHED 0x8c78u
/* Text: the message scroll's layout "\x03c\v010\t000%s" (centred from line 10), into which 18F2F formats its message
 * (18F55; its only DS reader: the loads at 14BC4/14BE6 are sprintf's own CS:4B7A). */
#define XEEN_DS_MESSAGE_FORMAT 0x4b7au
/* Words by roster character (30, 3Ch bytes): the face sprite handles of the inn's roster, copied from the loaded faces
 * (43E5B) and drawn by roster number (the inn 43951..440DB, creation 40B19/40FF8); word 0 zero = not loaded yet
 * (2AC2E, 43E39); the menu's loads clear them. */
#define XEEN_DS_ROSTER_FACE_SPRITES 0x8d66u
#define XEEN_DS_ROSTER_FACE_SPRITE(roster) ((uint16_t)(XEEN_DS_ROSTER_FACE_SPRITES + 2u * (roster)))
#define XEEN_ROSTER_FACE_BYTES      0x3cu
/* Bytes by member (6): trained during this visit to the training grounds: the first training of a member in a visit
 * advances the clock a day (5A0h minutes; 25583 tests, 25599 sets), the location's entry clears them (27982). */
#define XEEN_DS_MEMBER_TRAINED 0xf61eu
/* Byte: the world (0 XEEN.CC, 1 DARK.CC) whose archive holds the map's sprites: set to the world by both map loads
 * (1ABC3 outdoors, 1AD72 indoors), read by the input poll to select that archive again (18EEC). */
#define XEEN_DS_MAP_ARCHIVE 0xf61du
/* Word: the training grounds' trainee, a member number: set by the member key (25662) and passed to the highlight
 * (25672), read by the training (2557F, 25595), cleared at the location's entry (279DB). */
#define XEEN_DS_TRAINEE 0xf62cu
/* The training's text 271AE sets both for the training key (2549B..2561A): the trainee's experience still needed
 * (dword: 0 when the next level can be bought, 1 at the town's top level; 2721D, 2726D) and the town's top level
 * (byte, 271CB..27206: Clouds 10/15/20, Darkside 30/50/200/100 by map; read at 2561A, 2722D, 2724D). */
#define XEEN_DS_TRAINING_NEED      0xf646u
#define XEEN_DS_TRAINING_TOP_LEVEL 0xf64cu
/* Byte: the lock state of the event's object (98D9): 0 untouched (the dispatch clears it, 337C7); positive when it is
 * unlocked, the pick incrementing it (2DFB3) and an event of kind 43h setting 1 (32818); FFh when the pick failed
 * (2E05B), after which 327EF ends the event and clears it. Read by the pick (2DEE1: not yet tried), the dispatch
 * (336D2: positive opens the object, 336DC) and the scene (1B5CA: 1..7Fh animates the object being opened). */
#define XEEN_DS_OBJECT_LOCK     0x9755u
#define XEEN_OBJECT_LOCK_FAILED 0xffu
/* Byte: the step's cell event deferred because a monster stands in the party's cell (view slot 0): 310B4 sets it to 1
 * at 31404 when 98B4 is not FFh (and skips the event to 33690), to 0 at 3140C otherwise; the combat scene that the
 * contact starts tests it at 1C183 and runs the dispatcher again (1C18A). Its only three original accesses. */
#define XEEN_DS_EVENT_DEFERRED 0x9756u
/* The current text block, a far pointer (offset 97C0, segment 97C2): the readers add an entry of a far offset table to
 * the offset and push the segment (164EE, 25BB1, 340A8, 35BB8, 3F574, 4170A, 27 reads): a text of the block. The
 * locations and the menu point it at the program's strings (50D4:1C7Eh, 2B644..); the character view, Quests and
 * the awards save it, point it at the loaded file (F42C:0) and restore it (3E2B3/3E952, 415E5..). */
#define XEEN_DS_TEXT_BLOCK         0x97c0u
#define XEEN_DS_TEXT_BLOCK_SEGMENT 0x97c2u
/* The formatted file-name buffer: the names 14BBA (sprintf) writes for the sprite load 18931, the file reads 18975 and
 * 19AEB/199B7, the voice and text loads (115 original immediate uses, phase255/f5e8_consumers.txt). Indoors the
 * map load bumps its byte + 5 for the style's "f<style>N.fwl" files (sprites.c). */
#define XEEN_DS_FILE_NAME 0xf5e8u
/* The shared formatting buffer (1000 bytes, A827..AC0E): 77 of its 79 original immediate uses push it as a
 * destination or source of the formatted texts (14BBA's sprintf, the message window 18F2F, the event and location
 * texts, names); 19B34 formats a 13-character file name in it (A834 = 0), 43F17/440F5 clear it. */
/* Bytes: the formatting buffer just before XEEN_DS_TEXT_BUFFER (A827 - 1000): 19 sprintf, 3 strcpy and the
 * original's other text routines among its 55 pushes (the location refusals, the guild's spell description, a cleared
 * 1000 bytes at the guild, the inn's and the panel's names). Two word writes fall inside it (A5A4 at 4A037/4A13F, A805
 * at 1E44D): its end and any overlap are not established (Phase 257: a43f_census.txt). The three text buffers lie
 * 1000 bytes apart: XEEN_DS_TEXT_BUFFER_0, XEEN_DS_TEXT_BUFFER, XEEN_DS_TEXT_BUFFER_2. */
#define XEEN_DS_TEXT_BUFFER_0     0xa43fu
#define XEEN_DS_TEXT_BUFFER       0xa827u
#define XEEN_TEXT_BUFFER_BYTES    1000u
/* Bytes: the second formatting buffer, right after XEEN_DS_TEXT_BUFFER (AC0F): the destination of 76 sprintf (4BBA) and
 * 53 strcpy (3853:5E5C) of the original's 151 pushes, mostly the texts of windows and dialogs; also the CD
 * configuration's line buffer (47D33..47E5F, which parses it at fixed offsets up to +13Dh: AC41, ACCD, AD4C). Its end
 * is not established (Phase 257: ac0f_census.txt). */
#define XEEN_DS_TEXT_BUFFER_2     0xac0fu
#define XEEN_DS_WALL_SPRITES    0xf40au
/* Indoors (1AF24..1AFCC) slot 0 holds no sprite and slots 1..5 the map style's wall files: 1..4 "f<style>1.fwl" ..
 * "f<style>4.fwl" (2..4 by incrementing the name's digit, F5ED), 5 "s<style>.swl"; the map load copies them into the
 * indoor view records, the scene's side and centre surfaces read slots 1 and 2 (243EC..245C1). */
#define XEEN_DS_INDOOR_FWL_SPRITE(digit) (XEEN_DS_WALL_SPRITES + 2u * (digit))
#define XEEN_DS_INDOOR_SWL_SPRITE        (XEEN_DS_WALL_SPRITES + 2u * 5u)
/* Words: the results of the cell queries (1C1C1/1C680, their only writers 1C235..1C7CD): the queried cell's surface
 * kind (outdoors its low nibble; indoors its metadata & 7; constants 0, 6, 7, 0Fh in the queries' other branches),
 * which the map record's classification maps to a class (XEEN_MAP_RECORD_CLASSIFICATION); and its walls (outdoors
 * the wall kind, the nibble (cell >> 4) & 15; indoors the cell's wall word). */
#define XEEN_DS_CELL_SURFACE 0xf490u
#define XEEN_DS_CELL_WALLS   0xf48eu
/* The cell queries' result, and XEEN_DS_CELL_WALLS, for a cell beyond the maps (1C337, 1C3F0, 1C698, 1C729, 1C83D);
 * the minimap, the travel spells and Bash test for it. */
#define XEEN_CELL_OUTSIDE_MAPS 0x8888u

/*
 * The indoor view's wall flags, F4AB..F5DE (308 bytes). The view scan 1C9D4 clears them and then, for each of the 44
 * view slots (the cell queries' entries 0..43: XEEN_DS_STEP_X/Y and the wall shifts), adds 1 to one byte chosen by the
 * slot and its wall kind (1..15; 0 and the 8888h of a missing cell add nothing). Seen from the party, a slot is either
 * a side wall (the left or right wall of its cell: 24 slots) or a face (the wall across its cell, facing the party:
 * 20 slots). Three kinds of arrays:
 *   XEEN_DS_VIEW_WALL(p)    44 bytes by draw position p (far to near): a side wall of any kind but 2, or a face of
 *                           kind 5 or 8;
 *   XEEN_DS_VIEW_WALL_2(p)  44 bytes by draw position: a wall of kind 2, side or face;
 *   XEEN_DS_VIEW_FACE(k, f) eleven 20-byte arrays by face index f, one per face kind k (4 and 13 share one).
 * The readers: the scene's visibility rules and surfaces (22B25..245C1), XEEN_DS_SPOT_DOORS_LIT, and Bash, which adds
 * to the faces ahead (3D7B8..). The kinds' meanings are only partly known, so they stay numbers.
 */
#define XEEN_DS_VIEW_WALLS           0xf4abu
#define XEEN_VIEW_WALLS_BYTES        0x134u
#define XEEN_VIEW_SLOTS              44u
#define XEEN_DS_VIEW_WALL(position)   ((uint16_t)(0xf4abu + (position)))
#define XEEN_DS_VIEW_WALL_2(position) ((uint16_t)(0xf4d7u + (position)))
#define XEEN_DS_VIEW_FACE_ARRAY(kind)                                                                               \
    ((kind) == 1u    ? 0xf517u                                                                                     \
     : (kind) == 3u  ? 0xf553u                                                                                     \
     : (kind) == 6u  ? 0xf57bu                                                                                     \
     : (kind) == 7u  ? 0xf567u                                                                                     \
     : (kind) == 9u  ? 0xf52bu                                                                                     \
     : (kind) == 10u ? 0xf5cbu                                                                                     \
     : (kind) == 11u ? 0xf58fu                                                                                     \
     : (kind) == 12u ? 0xf53fu                                                                                     \
     : (kind) == 14u ? 0xf5a3u                                                                                     \
     : (kind) == 15u ? 0xf5b7u                                                                                     \
                     : 0xf503u /* 4 and 13 */)
#define XEEN_DS_VIEW_FACE(kind, face) ((uint16_t)(XEEN_DS_VIEW_FACE_ARRAY(kind) + (face)))
/* The draw positions: each is a view slot's wall in the cell (lateral, ahead) of the party (left negative), a face or
 * the cell's left or right wall:
 *    0..6  faces, 3 ahead: lateral -3, -2, -1, +3, +2, +1, 0 (face indices 1, 2, 3, 4, 6, 7, 8)
 *    7..10 sides, 3 ahead: (0, left), (-1, left), (0, right), (+1, right)
 *   11..15 faces, 2 ahead: -2, -1, +2, +1, 0 (face indices 9..13)
 *   16..19 sides, 2 ahead: (-1, left), (0, left), (+1, right), (0, right)
 *   20..22 faces, 1 ahead: -1, +1, 0 (face indices 14..16)
 *   23..24 sides, 1 ahead: (0, left), (0, right)
 *   25..27 faces of the party's row: -1, +1, 0 (face indices 17..19)
 *   28..29 the party cell's left and right walls
 *   30..33 sides, 3 ahead: (-2, left), (+2, right), (-3, left), (+3, right)
 *   34..35 faces, 3 ahead: -4, +4 (face indices 0 and 5)
 *   36..43 sides, 4 ahead: (0, -1, -2, -3: left), (0, +1, +2, +3: right)
 */
/* The draw positions and face indices by place, generated from the step tables and the scan's slot tables
 * (xeen_dat_evidence/phase257/vis/positions.py): XEEN_VIEW_POS_FACE(ahead, side) is the wall across the cell AHEAD in
 * front of the party (0 its own row) in column SIDE (C the party's, L1..L4 / R1..R4 to the left / right), on its far
 * side, and XEEN_VIEW_POS_LEFT / _RIGHT that cell's side walls; XEEN_VIEW_FACE_AT(ahead, side) is a face's index in
 * the face arrays (XEEN_DS_VIEW_FACE). */
enum xeen_view_position {
    XEEN_VIEW_POS_FACE_3_L3 = 0,
    XEEN_VIEW_POS_FACE_3_L2 = 1,
    XEEN_VIEW_POS_FACE_3_L1 = 2,
    XEEN_VIEW_POS_FACE_3_R3 = 3,
    XEEN_VIEW_POS_FACE_3_R2 = 4,
    XEEN_VIEW_POS_FACE_3_R1 = 5,
    XEEN_VIEW_POS_FACE_3_C = 6,
    XEEN_VIEW_POS_LEFT_3_C = 7,
    XEEN_VIEW_POS_LEFT_3_L1 = 8,
    XEEN_VIEW_POS_RIGHT_3_C = 9,
    XEEN_VIEW_POS_RIGHT_3_R1 = 10,
    XEEN_VIEW_POS_FACE_2_L2 = 11,
    XEEN_VIEW_POS_FACE_2_L1 = 12,
    XEEN_VIEW_POS_FACE_2_R2 = 13,
    XEEN_VIEW_POS_FACE_2_R1 = 14,
    XEEN_VIEW_POS_FACE_2_C = 15,
    XEEN_VIEW_POS_LEFT_2_L1 = 16,
    XEEN_VIEW_POS_LEFT_2_C = 17,
    XEEN_VIEW_POS_RIGHT_2_R1 = 18,
    XEEN_VIEW_POS_RIGHT_2_C = 19,
    XEEN_VIEW_POS_FACE_1_L1 = 20,
    XEEN_VIEW_POS_FACE_1_R1 = 21,
    XEEN_VIEW_POS_FACE_1_C = 22,
    XEEN_VIEW_POS_LEFT_1_C = 23,
    XEEN_VIEW_POS_RIGHT_1_C = 24,
    XEEN_VIEW_POS_FACE_0_L1 = 25,
    XEEN_VIEW_POS_FACE_0_R1 = 26,
    XEEN_VIEW_POS_FACE_0_C = 27,
    XEEN_VIEW_POS_LEFT_0_C = 28,
    XEEN_VIEW_POS_RIGHT_0_C = 29,
    XEEN_VIEW_POS_LEFT_3_L2 = 30,
    XEEN_VIEW_POS_RIGHT_3_R2 = 31,
    XEEN_VIEW_POS_LEFT_3_L3 = 32,
    XEEN_VIEW_POS_RIGHT_3_R3 = 33,
    XEEN_VIEW_POS_FACE_3_L4 = 34,
    XEEN_VIEW_POS_FACE_3_R4 = 35,
    XEEN_VIEW_POS_LEFT_4_C = 36,
    XEEN_VIEW_POS_LEFT_4_L1 = 37,
    XEEN_VIEW_POS_LEFT_4_L2 = 38,
    XEEN_VIEW_POS_LEFT_4_L3 = 39,
    XEEN_VIEW_POS_RIGHT_4_C = 40,
    XEEN_VIEW_POS_RIGHT_4_R1 = 41,
    XEEN_VIEW_POS_RIGHT_4_R2 = 42,
    XEEN_VIEW_POS_RIGHT_4_R3 = 43,
};
enum xeen_view_face_index {
    XEEN_VIEW_FACE_INDEX_3_L4 = 0,
    XEEN_VIEW_FACE_INDEX_3_L3 = 1,
    XEEN_VIEW_FACE_INDEX_3_L2 = 2,
    XEEN_VIEW_FACE_INDEX_3_L1 = 3,
    XEEN_VIEW_FACE_INDEX_3_R3 = 4,
    XEEN_VIEW_FACE_INDEX_3_R4 = 5,
    XEEN_VIEW_FACE_INDEX_3_R2 = 6,
    XEEN_VIEW_FACE_INDEX_3_R1 = 7,
    XEEN_VIEW_FACE_INDEX_3_C = 8,
    XEEN_VIEW_FACE_INDEX_2_L2 = 9,
    XEEN_VIEW_FACE_INDEX_2_L1 = 10,
    XEEN_VIEW_FACE_INDEX_2_R2 = 11,
    XEEN_VIEW_FACE_INDEX_2_R1 = 12,
    XEEN_VIEW_FACE_INDEX_2_C = 13,
    XEEN_VIEW_FACE_INDEX_1_L1 = 14,
    XEEN_VIEW_FACE_INDEX_1_R1 = 15,
    XEEN_VIEW_FACE_INDEX_1_C = 16,
    XEEN_VIEW_FACE_INDEX_0_L1 = 17,
    XEEN_VIEW_FACE_INDEX_0_R1 = 18,
    XEEN_VIEW_FACE_INDEX_0_C = 19,
};
#define XEEN_VIEW_POS_FACE(ahead, side)  XEEN_VIEW_POS_FACE_##ahead##_##side
#define XEEN_VIEW_POS_LEFT(ahead, side)  XEEN_VIEW_POS_LEFT_##ahead##_##side
#define XEEN_VIEW_POS_RIGHT(ahead, side) XEEN_VIEW_POS_RIGHT_##ahead##_##side
#define XEEN_VIEW_FACE_AT(ahead, side)   XEEN_VIEW_FACE_INDEX_##ahead##_##side
/* The nearest slots' draw positions and face indices: slot 1 is the party cell's left wall and slot 3 its right wall;
 * slot 2 is the wall ahead of the party, slots 0 and 4 the walls ahead of the cells to its left and right. */
#define XEEN_VIEW_POSITION_LEFT_FACE  25u
#define XEEN_VIEW_POSITION_RIGHT_FACE 26u
#define XEEN_VIEW_POSITION_AHEAD      27u
#define XEEN_VIEW_POSITION_LEFT       28u
#define XEEN_VIEW_POSITION_RIGHT      29u
#define XEEN_VIEW_FACE_LEFT           17u
#define XEEN_VIEW_FACE_RIGHT          18u
#define XEEN_VIEW_FACE_AHEAD          19u
/* Byte: 1 when, after view slot 7, the cell queried exists (F48E is not 8888h) and a face of kind 5 or 8 stands
 * ahead of the party (XEEN_DS_VIEW_WALL(AHEAD)), else 0 (1CC43..1CC59). Its one reader is the Spot Secret Doors
 * indicator (26369). */
#define XEEN_DS_SPOT_DOORS_LIT 0xf496u
/* Bytes: the wall items' view slots, 20 (F497..F4AA), FFh or the wall item (XEEN_DS_WALL_ITEMS index) each view shows;
 * set and read only by 208FB (the memset at 2091E, then each slot's test and store). */
#define XEEN_DS_VIEW_WALL_ITEMS    0xf497u
#define XEEN_VIEW_WALL_ITEM_SLOTS  20u
#define XEEN_VIEW_NO_WALL_ITEM     0xffu

/* The cell steps by facing (XEEN_DS_PARTY_DIRECTION): the program data's movement_offset_x (064E) and
 * movement_offset_y (070E), 48 signed bytes per facing; entry 7 is the cell ahead, 14 the cell two ahead (Jump's
 * 0655/0715 and 065C/071C), the scene's view cells start at entry 2 (0650/0710). */
#define XEEN_DS_STEP_X(facing, entry) ((uint16_t)(0x64eu + 0x30u * (facing) + (entry)))
#define XEEN_DS_STEP_Y(facing, entry) ((uint16_t)(0x70eu + 0x30u * (facing) + (entry)))
/* Words: the wall nibble's shift (low 5 bits used) by facing and the same entries (program data movement_wall_shift,
 * 07CE, 48 words per facing); entry 2, the first view cell's, is what the cell steps and the wall set read. */
#define XEEN_DS_WALL_SHIFT(facing, entry) ((uint16_t)(0x7ceu + 0x60u * (facing) + 2u * (entry)))
#define XEEN_STEP_HERE      2u /* the party's own cell: (0, 0) for every facing (1C1C1(2): its wall on the facing side) */
#define XEEN_STEP_AHEAD     7u
#define XEEN_STEP_TWO_AHEAD 14u
/* Program data tables (src/data tables.json): the party bar's portrait x per slot (portrait_x, 12 bytes), and the
 * spells' SP cost (spell_sp_cost, words; negative: per level, 3EB8B) and gem cost (spell_gem_cost, signed bytes). */
#define XEEN_DS_PORTRAIT_X     0x17b2u
#define XEEN_DS_SPELL_SP_COST  0x1ec2u
#define XEEN_DS_SPELL_GEM_COST 0x1f5cu
/* The statistics' bonus steps (attribute_bonus_threshold words, attribute_bonus signed bytes: the bonus of the first
 * threshold above the value) and the maximum HP's bases (class_base_hp by class, race_hp_bonus by race). */
#define XEEN_DS_ATTRIBUTE_BONUS_THRESHOLD 0x2da8u
#define XEEN_DS_ATTRIBUTE_BONUS           0x2dd8u
#define XEEN_DS_CLASS_BASE_HP             0x18b8u
/* Words by a cell side 0..3: the cell word's mask without that side's wall nibble (program data wall_side_masks). */
#define XEEN_DS_WALL_SIDE_MASKS 0x18c2u
/* Bytes: each wall item sprite's frame count (program data wall_item_frames, 53; the map load replaces them from the
 * map case's picture file, xeen_entities_wall_frames). */
#define XEEN_DS_WALL_ITEM_FRAMES 0x306du
#define XEEN_WALL_ITEM_SPRITES   53u
/* Byte pairs: a town location's exit cell (x, y), by world x 60 + location kind x 10 + town x 2 (program data
 * location_exit_cell; Sign In 25FF0). */
#define XEEN_DS_LOCATION_EXITS 0x2e1cu
#define XEEN_LOCATION_EXIT(world, kind, town) ((world) * 60 + (kind) * 10 + (town) * 2)
/* Bytes: a misc item's power 0..73 -> its spell (program data misc_power_spell; Identify's power name, 3CECA). */
#define XEEN_DS_MISC_POWER_SPELLS 0x28d6u
/* The view-record flag words of the monster colour effects, at + 16 x effect + 2 x step (program data
 * monster_effect_flags: effects 1..15 from 49DA; effect 0's row is never read). */
#define XEEN_DS_MONSTER_EFFECT_FLAGS 0x49cau
/* A flying monster's bob, its x and y offsets by the scene phase (program data monster_bob_x/_y, int8 x 8). */
#define XEEN_DS_MONSTER_BOB_X 0x3054u
#define XEEN_DS_MONSTER_BOB_Y 0x305cu
/* The scene's view-record positions (program data, Phase 255; the old opaque 4F9D region): an indoor monster's y by
 * its view cell's depth (monster_view_y, int8 x 4), an outdoor monster's y by its position in the outdoor record list
 * (outdoor_monster_view_y, int16 x 26) and that list (outdoor_monster_records: record 1166h + 12 x entry), a map
 * object's x indoors and outdoors and its y (object_view_x, outdoor_object_view_x, object_view_y: int16, 12 view
 * positions, a second row of 12 for the object sprite 71h Clouds / 2Fh Darkside). */
#define XEEN_DS_MONSTER_VIEW_Y          0x4faeu
/* The hit effects' view records and x (program data hit_effect_records, outdoor_hit_effect_records: 3 record numbers
 * by hit slot; hit_effect_x: int16 by 2 x slot + choice; hit_element_x_offset: int16 by slot), walked by 1BBE4/1B7DA. */
#define XEEN_DS_HIT_EFFECT_RECORDS         0x4d5cu
#define XEEN_DS_HIT_EFFECT_X               0x4d62u
#define XEEN_DS_HIT_ELEMENT_X_OFFSET       0x4d6eu
#define XEEN_DS_OUTDOOR_HIT_EFFECT_RECORDS 0x4d76u
#define XEEN_DS_OUTDOOR_MONSTER_VIEW_Y  0x4fb2u
#define XEEN_DS_OBJECT_VIEW_X           0x4fe6u
#define XEEN_DS_OBJECT_VIEW_Y           0x5016u
#define XEEN_DS_OUTDOOR_OBJECT_VIEW_X   0x5046u
#define XEEN_DS_OUTDOOR_MONSTER_RECORDS 0x5076u
#define XEEN_OBJECT_VIEW_ROW_BYTES      24u
/* The minimap's arrow row by an index from the party's y: 15 - index (program data minimap_rows, 16 bytes). */
#define XEEN_DS_MINIMAP_ROWS 0x4f9eu
/* The monsters' steps toward the party (program data monster_step_*, 49 entries by the offset in the 7 x 7 box): the
 * first step's x and y and the other step (one axis), each with its direction for the wall test. */
#define XEEN_DS_MONSTER_STEP_DX         0x56c4u
#define XEEN_DS_MONSTER_STEP_DY         0x5726u
#define XEEN_DS_MONSTER_OTHER_STEP      0x5788u
#define XEEN_DS_MONSTER_STEP_WALL       0x57eau
#define XEEN_DS_MONSTER_OTHER_STEP_WALL 0x581bu
#define XEEN_DS_CLASS_ATTACK_DIVISOR      0x2e01u /* class_attack_divisor: attacks a round = level / it + 1 */
#define XEEN_DS_CONDITION_COLOURS         0x2e0bu /* condition_colour: by condition 0..16 (Good) */
#define XEEN_DS_RACE_HP_BONUS             0x299au
/* More program data tables by their src/data ids: the stat view's window x/y (stat_window_x/_y), the monsters' ranged
 * attack icon by power (monster_pow_icon), the creation values by race (race_creation_values: its first row the
 * magic resistance), the run-time library's days per month (rtl_month_days), Town Portal's maps by side
 * (town_portal_maps, five per side) and the wall nibble shift per view slot (movement_wall_shift, 192 words). */
#define XEEN_DS_STAT_WINDOW_X        0x2d80u
#define XEEN_DS_STAT_WINDOW_Y        0x2d94u
#define XEEN_DS_MONSTER_POW_ICON     0x2780u
/* Bytes by race 0..4: a new character's starting resistances (the program data's race_creation_values, six tables of
 * five copied by 3FF1C at 40057..400BD into XEEN_CHARACTER_RESISTANCE: +141h magic, +137h fire, +13Bh electricity,
 * +139h cold, +13Fh energy, +13Dh poison). */
#define XEEN_DS_RACE_CREATION_VALUES     0x2096u
#define XEEN_DS_RACE_RESISTANCE_MAGIC       0x2096u
#define XEEN_DS_RACE_RESISTANCE_FIRE        0x209bu
#define XEEN_DS_RACE_RESISTANCE_ELECTRICITY 0x20a0u
#define XEEN_DS_RACE_RESISTANCE_COLD        0x20a5u
#define XEEN_DS_RACE_RESISTANCE_ENERGY      0x20aau
#define XEEN_DS_RACE_RESISTANCE_POISON      0x20afu
/* More program data by their src/data ids: monster_pow_cue (the projectile's sound by the monster's ranged type),
 * class_experience_base (words by class), condition_portrait_frame (the party bar's frame by worst condition),
 * location_animation_files / _frames (by world x 7 / x 11 + the location kind), and the main menu's first button set
 * (button_sets main0: x words ending FFFFh, then the y bytes). */
#define XEEN_DS_MONSTER_POW_CUE             0x2787u
/* Far-pointer text tables inside the program data's p2E94 (four bytes an entry), by their entries: the races (then
 * the alignments), the sexes, the conditions, the statistics (then Age and the others), the party values ("Gold",
 * "Gems", "Food", "Condition"), the skills and the rating words ("Nonexistant", "Very Poor", ...); and p56B4's
 * coloured "on" and "off" of the control panel. */
#define XEEN_DS_RACE_NAMES        0x2e94u
#define XEEN_DS_SEX_NAMES         0x2eb4u
#define XEEN_DS_CONDITION_NAMES   0x2ec0u
#define XEEN_DS_STAT_NAMES        0x2f04u
#define XEEN_DS_PARTY_VALUE_NAMES 0x2f44u
#define XEEN_DS_SKILL_NAMES       0x2f54u
#define XEEN_DS_CLASS_NAMES       0x286au /* p286A: "Knight".."Ranger", by xeen_class */
#define XEEN_DS_RATING_NAMES      0x2f9cu
#define XEEN_DS_PANEL_ON_TEXT     0x56b4u
#define XEEN_DS_PANEL_OFF_TEXT    0x56b8u
/* More far-pointer tables, by their entries: the four facings' names ("North".., p17A2), the locations' songs by
 * world x 1Ch + kind x 4 ("bank.m".., p1880), the number words ("Ten", "One".., p2934), operation 40's voices
 * ("ahh.voc".., p303C), the endings' commands for XEEN.EXE ("WORLDEND", "ENDGAME2", "ENDGAME", p62D4), the
 * cutscenes' sprite files ("tower.vga".., p63C6) and the C runtime's day and month names ("Sun".., p8A2E). */
#define XEEN_DS_DIRECTION_NAMES 0x17a2u
#define XEEN_DS_LOCATION_SONGS  0x1880u
#define XEEN_DS_NUMBER_WORDS    0x2934u
#define XEEN_DS_EVENT_VOICES    0x303cu
#define XEEN_DS_ENDING_NAMES    0x62d4u
#define XEEN_DS_CUTSCENE_FILES  0x63c6u
#define XEEN_DS_ORDINAL_SUFFIXES 0x63feu /* p63FE: "th", "st", "nd", "rd", "th".. by a number's last digit (360C4) */
#define XEEN_DS_WARZONE_TITLE   0x6426u /* p6426: "The Warzone\n\t125Battle Master", the arena's speaker */
#define XEEN_DS_CRT_DAY_NAMES   0x8a2eu
#define XEEN_DS_CRT_MONTH_NAMES 0x8a66u /* p8A66: "Jan".."Dec" (the RTL's 133CE, 13724, 138F2) */
/* Phase 259: the far-pointer tables of the program data that their original readers delimit (src/data
 * pointers.json offsets; the readers are listed at FAR_TABLE_STARTS in tools/data/ds_structs_p186.py). Each is read
 * as a far pointer (an entry four bytes; the native reads the offset word) and never written.
 * - Three far format pointers, each read only at its own address, the format of a sprintf: the Cast Spell window
 *   (6E4A: the caster, the ready spell, its cost and gems; 3DAAF, 3F83F), the guild member's line with "Buy
 *   Spells" and "Spell Info" (6ED0; 2770F, 3F751; 3F10F pushes 6ED0 itself) and the guild's options window
 *   ("Guild Options for%s" and the gold, 6F0B; 2774F, 3F13D, 3F780), which frames the member's line.
 * - The quests lists: the line every slot of the quest items list starts as ("\b \b*", 3E39F), and the Clouds and
 *   Darkside headings ("-- Clouds of Xeen --", "-- Darkside of Xeen --") before each world's first line. */
#define XEEN_DS_CAST_SPELL_TEXT    0x6e18u
#define XEEN_DS_GUILD_MEMBER_TEXT  0x6e1cu
#define XEEN_DS_GUILD_OPTIONS_TEXT 0x6e20u
#define XEEN_DS_QUEST_ITEMS_FILLER 0x6e26u
#define XEEN_DS_QUESTS_CLOUDS      0x6e2au
#define XEEN_DS_QUESTS_DARKSIDE    0x6e2eu
/* - The weapons' powers by the item's state ("", "Dragon Slayer", "Undead Eater".., 4766F). Its entry 0, the empty
 *   string, is also what the quests and awards lists put in every line before filling them (direct reads of
 *   [263C] at 3E59D, 3E6DC, 416B2). The misc items' powers by their id ("", "light", "awakening".., 478BD).
 * - The spell's cost names "Spell Points", "Gems", which 39BBA reads at 27B2 + 4 x the reason (1 or 2: a virtual
 *   base, XEEN_DS_SPELL_COST_NAME_BASE); the monsters' special attacks by the record's special ("None", "Magic"..
 *   "Stone", 3A2EC); "Spell Failed!"; the items dialog's buttons ("Equip", "Rem", "Disc", "Quest") and the shop's
 *   ("Buy", "Sell", "Identify", "Fix"), each slot read at its own address; the item actions by action ("Equip"..
 *   "Gold", 3BBBA); the item categories ("Weapons".."Miscellaneous").
 * - The places of a refused amount by WHERE ("Party", "Bank"); the "Who will" prompt's titles when its request has no
 *   destination ("Open Grate", "Open Door", "Open Scroll", "Select Char", 2FA49) and its 32 verbs ("search"..
 *   "try", 2FA22); the bank's window titles ("Deposit", "Withdrawal"); the tavern's options window ("Tavern Options
 *   for", the gold); the shop's "Charges" column of misc items (p7CEA, 45913, 45AE8).
 * - The inn's two member actions ("Remove", "Delete", 43A73); the quick-fight modes ("Attack", "Cast", "Block",
 *   "Run", 3EA29); the locations' file stems by kind ("bnkr", "blck", "gild", "tvrn", "tmpl", "trng", 27DA4);
 *   the party effects' column open and close ("\x03l\n\t024", "\x03r\t124"); the message when a member cannot act
 *   ("%s is not in any condition to perform actions!", 28D2A, 3BD39). */
#define XEEN_DS_WEAPON_POWER_NAMES     0x263cu
#define XEEN_DS_MISC_POWER_NAMES       0x2658u
#define XEEN_DS_SPELL_COST_NAMES       0x27b6u
#define XEEN_DS_SPELL_COST_NAME_BASE   0x27b2u /* the virtual base 39BBA indexes by the reason (1, 2) */
#define XEEN_DS_MONSTER_SPECIAL_NAMES  0x27beu
#define XEEN_DS_SPELL_FAILED_TEXT      0x281au
#define XEEN_DS_ITEMS_BUTTON_NAMES     0x281eu
#define XEEN_DS_SHOP_BUTTON_NAMES      0x282eu
#define XEEN_DS_ITEM_ACTION_NAMES      0x283eu
#define XEEN_DS_ITEM_CATEGORY_NAMES    0x285au
#define XEEN_DS_VALUE_PLACE_NAMES      0x29a4u
#define XEEN_DS_WHO_WILL_TITLES        0x29acu
#define XEEN_DS_WHO_WILL_VERBS         0x29bcu
#define XEEN_DS_BANK_TITLES            0x2a44u
#define XEEN_DS_TAVERN_OPTIONS_TEXT    0x2a4cu
#define XEEN_DS_SHOP_CHARGES_TEXT      0x7ceau
#define XEEN_DS_INN_MEMBER_ACTIONS     0x295cu
#define XEEN_DS_QUICK_FIGHT_NAMES      0x2964u
#define XEEN_DS_LOCATION_STEMS         0x2ffcu
#define XEEN_DS_EFFECT_COLUMN_OPEN     0x56bcu
#define XEEN_DS_EFFECT_COLUMN_CLOSE    0x56c0u
#define XEEN_DS_MEMBER_CANNOT_ACT_TEXT 0x18cau
#define XEEN_DS_CLASS_EXPERIENCE_BASE       0x2920u
#define XEEN_DS_CONDITION_PORTRAIT_FRAME    0x2df0u
#define XEEN_DS_LOCATION_ANIMATION_FILES    0x3018u
#define XEEN_DS_LOCATION_ANIMATION_FRAMES   0x3026u
#define XEEN_DS_MAIN_MENU_BUTTON_X          0x18d2u
#define XEEN_DS_MAIN_MENU_BUTTON_Y          0x18e6u
#define XEEN_DS_MAIN_MENU_BUTTON_ENABLED    0x1902u /* the main0 set's enabled bytes (the title draw 16417..1645D) */
/* The program data's spell orders: class_spell_lists (the spells of each casting group 0..2, XEEN_CLASS_SPELL_LIST
 * bytes apart; 76 no spell, index 27h the "no spell" slot of a member without a ready spell) and darkside_guild_spells
 * (the Darkside guilds' order, XEEN_DARKSIDE_GUILD_LIST per group, read while DS:0094 is set). */
#define XEEN_DS_CLASS_SPELL_LISTS     0x201eu
#define XEEN_CLASS_SPELL_LIST         40u
#define XEEN_DS_DARKSIDE_GUILD_SPELLS 0x1fa9u
#define XEEN_DARKSIDE_GUILD_LIST      0x27u
/* Byte pairs: a Darkside guild's (first, end) indexes into its category's list, at category x 8 + town x 2
 * (program data darkside_guild_ranges; 3EE12). */
#define XEEN_DS_DARKSIDE_GUILD_RANGES 0x6e32u
#define XEEN_DS_RTL_MONTH_DAYS       0x8a0au
/* The C runtime's DOS date conversion (dostounix 14DEC): days per month, int8 x 12, read with CBW (14E68 [si+8BE6];
 * also 150DB, 150F8). __isDST 15308: the days before each month, uint16 x 13, compared unsigned (15336, 15344, 15389
 * [bx+8BF2]; 15380 [bx+8BF0] for the month before). Both read live by main_menu.c (Phase 294). */
#define XEEN_DS_RTL_DOS_MONTH_DAYS        0x8be6u
#define XEEN_DS_RTL_MONTH_CUMULATIVE_DAYS 0x8bf2u
#define XEEN_DS_TOWN_PORTAL_MAPS     0x67c4u
#define XEEN_DS_TOWN_PORTAL_LOAD_BASE 0x67c3u /* 3B0D2's virtual base: 67C3[FB00 x 10 + n], n 1..5 (b67C3 unread) */
#define XEEN_DS_MOVEMENT_WALL_SHIFT  0x07ceu
/* The wall nibble's shift of the cell ahead by facing (N, E, S, W): the program data's direction_wall_shift words. */
#define XEEN_DS_DIRECTION_WALL_SHIFT(facing) ((uint16_t)(0x279eu + 2u * (facing)))

/* Byte: the step parity, 0 or 1: each step, turn and hazard scene flips it (`xor 1`: 1733D, 17370, 17626, 17659,
 * 17D93, 17DBD), the exploration loop's start sets it (17966); the scene construction (220xx..251xx) draws the
 * background's frame by it (the DS:94E table) and mirrored (8000h) when it is set. */
#define XEEN_DS_STEP_PARITY 0xf5dfu
/* Words the scene puts into view records' flags, each bit 8000h a horizontal flip (all 0 at 1796B..17971): the
 * animated surfaces' (water, lava: the special terrain slots and the outdoor water record), flipped every fourth scene
 * as SURFACE_STEP (byte, 0..3) wraps (1B534); the step's (indoor record 2), flipped with STEP_PARITY after a
 * step, a shot or a wait (17D8D, 17DB7); the turn's (the first two records: ground_handle's indoors, the sky
 * outdoors), flipped by each turn and strafe (17CD7). The combat's turn flips its bit 1 instead (391C5), as the
 * original does. */
#define XEEN_DS_SURFACE_STEP 0xf5e0u
#define XEEN_DS_SURFACE_FLIP 0xf5e2u
#define XEEN_DS_STEP_FLIP    0xf5e4u
#define XEEN_DS_TURN_FLIP    0xf5e6u
/* Byte: 1 after a turn (17CCD) until the scene clears it (1B7D5, 1BBDF): that scene keeps the map objects' frames
 * (1B5A1). */
#define XEEN_DS_TURNED 0x3068u
/* Byte: 1 once G, O, B, R were typed in the control panel (29C10): the panel then shows the debug time line (2991E);
 * the Quit and Wizard confirms keep it aside and clear it (29B2C..29B60, put back at 29BE0). */
#define XEEN_DS_PANEL_DEBUG_LINE 0x97fau

/* Byte: 1 during the forward step's door scene: a step forward (F2h) into a door (F516) sets it (178E2), plays FX 2Fh
 * and the 1B503(1) scene and clears it (178F7; the exploration loop's start 17974 also); meanwhile the scene
 * construction draws that door open (2449B, 24621: frame 1 or 10 by C106). */
#define XEEN_DS_DOOR_OPENING 0x9c6eu

/* Byte: 1 while a sign's text is up: the sign operation sets it after copying the text to 98DA (318BA); the steps
 * clear it (17270, 17D88, 17DB2); the scene shows the sign again while no monster stands in the party's cell
 * (1C0F6); the fight's start keeps it and clears it (38F6D, 38F74), 2887F/28893 keep it around their window. */
#define XEEN_DS_SIGN_SHOWN 0x3066u
/* Bytes: the sign's text, 150 (98DA..996F, up to the treasure's item markers at 9970): operations 2/3/4/39 format it
 * (318AF), the scene draws it in window 3 (1C104..1C107), Teleport clears it (3AE8A); three original pushes. */
#define XEEN_DS_SIGN_TEXT       0x98dau
#define XEEN_SIGN_TEXT_BYTES    150u
/* Byte: the side whose towns Town Portal lists: the map load's (19F49/19F53) map side, but 0 on the maps 71h..7Fh;
 * Town Portal is its only reader (3AFD1, 3AFE2, 3B0B9). */
#define XEEN_DS_TOWN_PORTAL_SIDE 0xfb00u

/* Byte: the scene's animation phase 0..7: each scene 1B503 steps it (1B10F..1B11C: +1 modulo 8); the scene
 * construction (1EExx..1FExx, 219xx) draws the monsters' and the effects' frames by it. */
#define XEEN_DS_SCENE_PHASE 0x3069u

/* Byte: 1 while the monsters may act: the monster update 2A701 (2A57A, 2A707), the scene's contact test (1C177) and
 * its countdown below (1B558) run only then. The commands clear it while their dialogs run (17959, 17C30, 17D9B,
 * 17DC4, 17DD1; the Cast dialog's entry 3D8C9) and set it again (17AAE, 17C48, 3E14C). */
#define XEEN_DS_MONSTERS_ENABLED 0x96c4u

/* Byte: the scenes left before the monsters' update 2A701 (0 none): a charge (28178) first runs a pending update,
 * then sets 3 (281A1); each exploration or combat scene with the monsters enabled counts it down and runs the update
 * at 0 (1B558..1B564; the native's XEEN_96C0 service), which clears it (2A713); 28890/2893F and 2BABC keep it
 * around their own work, the Cast dialog saves and clears it (3D8E4, restored at 3E15A). */
#define XEEN_DS_MONSTER_COUNTDOWN 0x96c0u

/* Byte: 1 while the main menu runs (16A28), 0 once the game starts (17024): the command poll's Ctrl+Q (11h) quits
 * only then (18AAC), and the name entry (2B048..2B348) and 28896 take their menu forms. */
#define XEEN_DS_IN_MENU 0x8c77u
/* Byte: nonzero lets a key that matches a live button show that button's press (the command poll's key stage, 18AC5).
 * Startup sets it to 1 (15F81) and the original never writes it again. */
#define XEEN_DS_KEY_BUTTON_FEEDBACK 0x8c76u

/* Byte: the current game's slot, the number in its save files' names (xeenNN.wox and darkNN.wox: the formats DS:4BBB
 * and 4ADE): the menu's Load (167FA, 16ACE), the new game (16A73), the panel's Load and Save (29A86, 29ACA), the name
 * entry (2B309) and 18451 store it; the save and load paths (18068..1826B, 1963B, 197A4) read it. */
#define XEEN_DS_GAME_SLOT 0x95a6u

/* Word: the exploration loop's exit value, nonzero ends the loop (17AED, 17C06, 17F1E; 17F57 returns it; the
 * location loop's 27F9F): 0 at the loop's start (17941), the step cell's event result (17AEA), the caller's AX
 * (17C03), 2 at 17C3F and the location exit 260D4. */
#define XEEN_DS_LOOP_EXIT 0xf64au

/* Byte: 1 from a new day (30D51; at the start or a load before 05:00, 43DF4) until the day's first clock charge at
 * or after 05:00, which runs the dawn (30D56: the temporaries' reset, an unrested party's weakness) and clears it
 * (30DCA). */
#define XEEN_DS_DAWN_PENDING 0x9842u

/* Byte: the number of party members (0..6; ED8E.. hold their roster indexes). */
#define XEEN_DS_PARTY_COUNT 0xed8cu
/* Word: the handle of a resident sprite slot (one of the seven startup reserves, 15E0F) that transient sprites load
 * into, replacing its contents: the volley's (2828E), the projectile delivery's (412FB), the character view's
 * (44B58, 44DDB) and the item dialog's (4503E). */
#define XEEN_DS_RESIDENT_SPRITE_SLOT 0x8c86u
/* Words: DOS segments of loaded files (the native host keeps none and writes 0): the song file's (15FCE, 1709D at the
 * music loads; passed with the song name to 18975 at 18EF8/18F19, freed 267E8; game_archive.c keeps a copy for the
 * audio sink), the attack's miss voice miss.voc (160FC, 33E60; played 36AAE), and the map's texts block (1613E; 1614D
 * copies it to XEEN_DS_EVENT_TEXTS_SEGMENT). */
#define XEEN_DS_SONG_SEGMENT       0xf42au
#define XEEN_DS_MISS_VOICE_SEGMENT 0xf386u
#define XEEN_DS_MAP_TEXTS_SEGMENT  0x9c60u
/* Word: the DOS segment of pow1.voc, the attack's hit voice, as F386's miss.voc: loaded at 1610F and 33E73 (beside
 * F386's 160FC and 33E60), played at 36A1D/36A4D; the native writes 0 when it releases it. */
#define XEEN_DS_POW_VOICE_SEGMENT  0xf388u
/* The two resident name files and their entry tables. Each load (the start 15FFE../1607F.., a location 27882../
 * 278F2.., 33D90../33DFA.., the menu's 43D31..) stores the file's far pointer (offset 0, its DOS segment; the
 * native keeps no DOS memory: 0) and walks its NUL-ended entries into the table, one far pointer an entry, up to the
 * file's size (8582; the native bounds the tables at 4Dh and 83h entries):
 * - spells.xen (9C6A:9C6C): XEEN_DS_SPELL_NAMES by spell id (writers 16056, 278CF, 33DD9; 11 readers,
 *   among them the Cast Spell window's 3DAA1 and 3F831 and the guild lists 3ECC7..3F5E3);
 * - MAE.xen (9C66:9C68): XEEN_DS_MATERIAL_NAMES by the item row's material (writers 160D8, 27940,
 *   33E44, 43D8F; the item names 476E1, 4778E, 47849). */
#define XEEN_DS_SPELLS_FILE         0x9c6au
#define XEEN_DS_SPELLS_FILE_SEGMENT 0x9c6cu
#define XEEN_DS_MAE_FILE            0x9c66u
#define XEEN_DS_MAE_FILE_SEGMENT    0x9c68u
#define XEEN_DS_SPELL_NAMES         0xf868u
#define XEEN_DS_MATERIAL_NAMES      0xf65cu
/* Word: the DOS segment of the shared file buffer, 0 when none: seven load calls store the block they return
 * (159A2, 15BC1, 16122, 16774, 16A9D, 2795E, 33E86) and 18693(&F42C) frees it (15B95, 161E3, 16A54, 17019, 33D01).
 * Its contents vary by user: a location's or an event's voice (2550E.., 28F7E.., the fall's scream.voc), the large
 * event window's texts (copied to XEEN_DS_EVENT_TEXTS_SEGMENT), the text block of qnotes.bin and the awards when
 * it is set (2B6D5: 97C2 = F42C), 1803D's copy to B41C. */
#define XEEN_DS_FILE_BUFFER_SEGMENT 0xf42cu
/* Far pointer (offset FB02, segment FB04): the spell list's lines, 50 bytes each. Its one writer is 3F05D's open
 * (3F07B/3F07F: DS:9C6F); its 16 readers are the list routines 3ECD4..3F35E. (9C6F itself is a scratch area other
 * dialogs use with other layouts: the shop's 100-byte lines, the view's text, the save's party copy.) */
#define XEEN_DS_SPELL_LIST_LINES         0xfb02u
#define XEEN_DS_SPELL_LIST_LINES_SEGMENT 0xfb04u
/* Far pointers (four bytes each): the control panel's four state texts, each "on" or "off" (the far pointer
 * XEEN_DS_PANEL_ON_TEXT or _OFF_TEXT copied): the effects, the music, the text and the speech. 296DE writes them
 * (296EC..29767) and only its own 5A88 sprintf reads them (297A8..297C4). */
#define XEEN_DS_PANEL_SOUND_STATE  0xf9aau
#define XEEN_DS_PANEL_MUSIC_STATE  0xf9aeu
#define XEEN_DS_PANEL_TEXT_STATE   0xf9b2u
#define XEEN_DS_PANEL_SPEECH_STATE 0xf9b6u
/* Word: the character creation's dice.vga sprite handle: loaded at 4064F, drawn at 3FB22 and 3FEEB, freed at 41219. */
#define XEEN_DS_DICE_SPRITE 0x8da4u
/* Word in the party file (so saved): 1 once the ENDGAME command ran (334D8, its only writer); then map 1Bh's object
 * sprite 55h is not loaded (1A85C) and Lloyd's Beacon's return to Clouds maps 4Bh..4Eh fails (3A817). */
#define XEEN_DS_ENDGAME_REACHED 0xefe8u
/* Byte: the save pair's id: a random 1..FEh stored when a slot's pair is made (18461, its only writer), carried as
 * byte 1Eh of the slot files' maze.nam (read at 180D3, 182F8, 18489; game_pairs.c). */
#define XEEN_DS_PAIR_ID 0xf61cu
/* Dwords of the play time, in the DOS time's seconds: when the session's count started (98B0, the time at 19C39;
 * the native save restarts it) and the play time before it (F01A, in the party block, so saved: 0 at a new game
 * 2B54E, the save adds the session's 1928E); the play time 19C3D shows is now - 98B0 + F01A (19C53, 19C5B). */
#define XEEN_DS_PLAY_CLOCK_START 0x98b0u
#define XEEN_DS_PLAY_SECONDS     0xf01au
/* Words: the archive (world) the map's sprites load from, by kind, set by the map load's cases (19F34..1A0AB: XEEN.CC
 * for a Darkside special map's monsters, 19FC4): the wall items' (read 1AB56), the objects' (1A850) and the monsters'
 * (1A944; combat's entry selects it again, 38FD0). */
#define XEEN_DS_MAP_WALL_ITEM_ARCHIVE 0x8c4cu
#define XEEN_DS_MAP_OBJECT_ARCHIVE    0x8c4eu
#define XEEN_DS_MAP_MONSTER_ARCHIVE   0x8c50u
/* The maze.pty block: a save's party file is the DS image from here (XEEN_CLOUDS_PARTY_BYTES or
 * XEEN_DARK_PARTY_BYTES by the save's profile, xeen/party.h); load (43DAC) and save copy it whole. */
#define XEEN_DS_PARTY_FILE XEEN_DS_PARTY_COUNT
/* A DS offset inside the maze.pty block as an offset into the party file. */
#define XEEN_PARTY_FILE_OFFSET(ds_offset) ((ds_offset) - XEEN_DS_PARTY_FILE)
/* Byte: the party count at the last save or inn visit (the inn's exit 4445E and the save's 29666 set it to
 * XEEN_DS_PARTY_COUNT): the next save writes that many members back to the roster (295D7..29657), and those beyond the
 * current count, dismissed since, return to the inn with their index FFh. Three original accesses. */
#define XEEN_DS_SAVED_PARTY_COUNT 0xed8du
/* Bytes: the party's members, in party order: each one's roster index (0..29), FFh for none (the inn's removal,
 * the save pair's dead members). 28 indexed byte accesses, no word access (`phase253/ds_ed8e.txt`). */
#define XEEN_DS_PARTY_MEMBERS 0xed8eu
#define XEEN_NO_MEMBER        0xffu /* an XEEN_DS_PARTY_MEMBERS slot without a member */

/*
 * Byte: the input context. Modal code saves it, writes its own value, and restores it at its end
 * (the restores are the 32 "mov [9A1C],al" writes). Only the values below are named; the others
 * (0, 6, 0Ah, 0Ch, 0Dh, 56h, FFh) each belong to one dialog and stay numeric at their sites.
 */
#define XEEN_DS_UI_MODE          0x9a1cu
#define XEEN_UI_MODE_EXPLORATION 1u
#define XEEN_UI_MODE_COMBAT      2u
#define XEEN_UI_MODE_SPELLS      3u    /* the spell dialogs (399FA's 39A1E, 3DB2E, 3E0A9) */
#define XEEN_UI_MODE_CREATION    4u    /* character creation (40708, 40CAB, 411D5) */
#define XEEN_UI_MODE_REST        5u    /* the party asleep in Rest (28EE7, its only writer) */
#define XEEN_UI_MODE_TREASURE    7u    /* a treasure's or a picked lock's display (2EC30, 2DFE3) */
#define XEEN_UI_MODE_EVENT       9u    /* an event script running (31539, its only writer) */
#define XEEN_UI_MODE_SHELTER     12u   /* Super Shelter's rest (3AF45, its only writer; Rest tests it at 28E5C) */
#define XEEN_UI_MODE_LOCATION    0x11u /* a town location's own loop (tavern, temple, ...) */

/*
 * Byte: 2 while a fight runs, 1 otherwise. Its writers: 1 at the exploration loop's start (1793A) and at the fight's
 * end (395C3), 2 at the fight's start (38F5F's 38F79). Its 25 readers test 2: then a member is a combat party index
 * (9720) and the combat party's count applies. Unlike 9A1C it keeps 2 through the fight's own dialogs.
 */
#define XEEN_DS_COMBAT_MODE 0xfaf8u
#define XEEN_COMBAT_MODE_EXPLORING 1u /* no fight runs (1793A, 395C3) */
#define XEEN_COMBAT_MODE_FIGHTING  2u /* a fight runs (38F79) */

/* Byte: the highlighted portrait (a member index; FFh none): 2A1F6 (2A260) and the fight's actor highlight (39052)
 * set it, 42298 (422CA) and the exploration loop (1797F) clear it to FFh; the party bar draws the frame there. */
#define XEEN_DS_HIGHLIGHTED_PORTRAIT 0x96c3u

/* Byte: the Cast dialog's caster outside combat, a member index: read at the dialog's start (3D991..3D9CD: when it
 * is not a present member, the last member with +A3h set, stored at 3D9BD); F1..F8 (3E119) and the spell list's
 * F1..F6 (3F734) store the member chosen. */
#define XEEN_DS_CAST_MEMBER 0x97beu
/* Byte: the character view's items dialog was opened in combat (the view's I stores UI mode == 2, 44DB6; the view's end
 * clears it, 44EDA): the items' Use then refuses with the "in combat" text (3BD51). Its only reader. */
#define XEEN_DS_VIEW_ITEMS_IN_COMBAT 0x97c6u

/*
 * Word: the code the command poll (18A22) returns: a mouse area's code, 1 for a click in the 3D
 * view, or a key's low byte (its high byte, the modifier state, is kept at F5FC). 0 means no input.
 */
#define XEEN_DS_INPUT_CODE 0xf5feu
/* Word: the command poll's last key reading with its low byte cleared (reading & FF00h, 18A91): the modifier state
 * in the high byte (the key ABI, input_poll.h). Only the poll writes it (18A94); the original never reads it. */
#define XEEN_DS_INPUT_MODIFIERS 0xf5fcu
/* Word: the size of the file the archive loader read last (4A877's writes 4A894, 4A94A, 4AA2B), which callers take as
 * the loaded file's length (the map's texts, the party file, a voice, the main menu's text). */
#define XEEN_DS_LOADED_FILE_SIZE 0x8582u
/* Byte: 1 while a click on member i's portrait gives the key F1 + i (18C0D, its only test); the dialogs that take
 * a member (inn, spells, items, Quick Reference, ...) save it, set it 1 or 0 and restore it (2AD73, 2F505, 39A09..). */
#define XEEN_DS_PORTRAIT_CLICKS 0x98d6u
/* Words: the sprite handles of the members' faces ("char%02u.fac" of each roster index, 2A040..2A090), by party
 * position (eight words: 4333F's compaction after a Dismiss scans them); the party bar draws them (2AC3F), the
 * character view's swap exchanges two (47AEE..47B14). */
#define XEEN_DS_MEMBER_FACE_SPRITES 0x9806u
#define XEEN_DS_MEMBER_FACE_SPRITE(position) ((uint16_t)(XEEN_DS_MEMBER_FACE_SPRITES + 2u * (position)))
/* Word: the sprite handle of dse.fac, loaded with the members' faces for a party of eight or fewer (2A0AC, its only
 * writer); the party bar draws a member's condition frame above 4 from it, as frame - 5, while the roster's face
 * sprites are not loaded (2AC35, its only reader). */
#define XEEN_DS_DARKSIDE_FACE_SPRITES 0x97fcu

/* Word: the mouse.icn sprite handle (the cursors): its one writer is the start-up's load 15F91/15F99; its 20 readers
 * pass it to 4E06B (the cursor select). */
#define XEEN_DS_MOUSE_SPRITES 0xf460u

/* Word: the command poll's mouse click (18A12 stores it; 18A15 tests 1): the animations and waits end on a click
 * (157ED, 15906, 16536..165F3, 17B34, 1E3D1, 296C2, 2995B/2996C). */
#define XEEN_DS_MOUSE_CLICK 0xf600u

/* Byte (the maze.pty block): the map of the inn the party signed in at (Sign In 25FF0: 26091); creation (3FFB3)
 * stores it as a new member's inn, the inn's roster lists the members lodged there (43EC9, 44306) and its exit
 * returns the party to it (44461). */
#define XEEN_DS_INN_MAP 0xed9du

/* Word: the temple's donations this visit, modulo 10: each Donate steps it (25824..25831); the one that brings it to
 * the day modulo 10 blesses the party with that count (10 for 0: 2583B..25859, its low byte); the member choice
 * (2598E) and the location's entry (27A8B) set it 0. */
#define XEEN_DS_TEMPLE_DONATIONS 0xf62au

/* Dwords: the temple's Uncurse price (2731C, the cursed items' sum 27579..275D6, 27620) and Donate price (27315,
 * 27647; 0 once the donation blessed, 258AD) for the member; the keys U and D pay them (258B9, 257CF). The Heal price
 * F642 stays numeric: the ending stores its score there too (33636). */
#define XEEN_DS_TEMPLE_UNCURSE_PRICE  0xf63au
#define XEEN_DS_TEMPLE_DONATION_PRICE 0xf63eu
/* The temple's other prices for the member (273F3, each read only there): dwords, the member's level (27407, the
 * prices' multiplier), raising the dead and stoned (27488..27510) and the eradicated (2755A); words, the town's
 * bases (27355..273ED, one set a town; another Clouds map keeps them, 2738B): the uncurse, the eradication, the
 * resurrection, the healing and the donation. */
#define XEEN_DS_TEMPLE_LEVEL              0xf62eu
#define XEEN_DS_TEMPLE_RESURRECTION_PRICE 0xf632u
#define XEEN_DS_TEMPLE_ERADICATION_PRICE  0xf636u
#define XEEN_DS_TEMPLE_UNCURSE_BASE       0xf650u
#define XEEN_DS_TEMPLE_ERADICATION_BASE   0xf652u
#define XEEN_DS_TEMPLE_RESURRECTION_BASE  0xf654u
#define XEEN_DS_TEMPLE_HEAL_BASE          0xf656u
#define XEEN_DS_TEMPLE_DONATION_BASE      0xf64eu

/* The tavern's state (game_tavern.c), each with no reader outside it. Words: the food packs that fill the party at
 * this town (members x 15, 60 or 120) and their price, the food's two writes (25AEC) for its answer (25C05..25C23);
 * a drink since the last tip, 1 after a drink (25A11), tested and cleared by the tip (25C9E/25CA8) and cleared at a
 * new customer (26101) and the location's entry (27B67). Byte: the town's first tip in tavern.bin (25C89's ladder),
 * plus the map's XEEN_MAP_RECORD_TAVERN_TIP. */
#define XEEN_DS_TAVERN_FULL_FOOD  0xf624u
#define XEEN_DS_TAVERN_FOOD_PRICE 0xf65au
#define XEEN_DS_TAVERN_DRANK      0xf626u
#define XEEN_DS_TAVERN_FIRST_TIP  0xf658u

/* Word: the global.icn sprite handle: its one writer is 43C56 (the load of DS:7B4F global.icn); its readers draw its
 * frames (the portraits' highlight, the party bar, the location and event windows' frame 7). */
#define XEEN_DS_GLOBAL_SPRITES 0xf458u

/* Word: the charpow.icn sprite handle (DS:7B9C), its one load 43CCD: the scene's and combat's effect frames draw from
 * it (1B8E3..1BDB6, 2E811, 37CD5, 3D71A/3D744); 4456C frees the handles after it. */
#define XEEN_DS_CHARPOW_SPRITES 0x98ceu
/* Words: the world entry's other sprite handles (one writer each, its load 43C5C..43CEE of the file named at DS:7B5A..
 * 7BA8; every reader pushes the handle to a draw): border.icn (the border indicators and the Clairvoyance sprites,
 * 26341..264A1), spellfx.icn (2BA65), fecp.brd (the four protections' border icons, 264EA..265F4), bless.icn (the
 * border's effect icons 2660D..266EC), hpbars.icn (the party bar's HP gems, 2AC7D/2AD0B), restorex.icn (2ABDC) and
 * inn.icn (the inn's buttons, 43F5F..441BF). `phase253/ds_sprite_handles.txt`. */
#define XEEN_DS_BORDER_SPRITES   0xf456u
#define XEEN_DS_SPELLFX_SPRITES  0x8c7eu
#define XEEN_DS_FECP_SPRITES     0x973au
#define XEEN_DS_BLESS_SPRITES    0x9736u
#define XEEN_DS_HPBARS_SPRITES   0x9734u
#define XEEN_DS_RESTOREX_SPRITES 0x9738u
#define XEEN_DS_INN_SPRITES      0x8da2u

/* Word: the minimap's tile sprites: outdoor.til (1AC77) or the map's own tile file named at F5E8 (1AED5); only the
 * minimap's draws read it. */
#define XEEN_DS_MINIMAP_TILES 0xf454u

/* Word: 1 at night, 0 by day. Its one writer, 2A0FB..2A112, stores 0 while the time of day (EFF4, minutes) is in
 * 12Ch..4EBh (5:00 to 20:59), else 1; the readers redraw the sky when it changes and pick the night frames. */
#define XEEN_DS_NIGHT 0x8c92u

/* Byte: the scene's part in a fall: 2 while the fall's second scene saves the screen (173D7; that scene's 1C0E5
 * 186B2(8C90)), 1 on the arrival map, whose scene draws the fall (175F7 and the step and post-action hazards; the
 * scene's 1BF78..1C0DB, cleared at 1C0CF), 0 otherwise (the exploration loop 1794C, 17309, 173C2/173CA). */
#define XEEN_DS_FALL_SCENE 0x8c85u

/* Bytes: the fall's destination for 175D3 (its one reader of each: 175DA..175F1): the map and cell that operation
 * 52 (3341C, event_fall) stores from the record's p6, p7, p8 (33424/3342B/33432). FALL_DAMAGE: its p9 (33439),
 * read once (17644), sign-extended, as the amount of the party damage 2E68D after the fall's scene. */
#define XEEN_DS_FALL_MAP    0x8c84u
#define XEEN_DS_FALL_X      0x8c83u
#define XEEN_DS_FALL_Y      0x8c82u
#define XEEN_DS_FALL_DAMAGE 0x8c81u

/* Word: the cutscene subtitle's position in its text (A827): 185E3 steps it (`inc`, then modulo the text length)
 * and each cutscene's entry clears it (33FE4, 3441B, 34FD4, 359B7); the talk loops repeat until it is 0 again. */
#define XEEN_DS_SUBTITLE_POSITION 0xf9a4u
/* 99 bytes: the speaking NPC's name, copied from the formatted name text when the speech window opens (301D3,
 * strncpy 63h), and pushed as the name to draw by the speech face's redraw (28DB2 in 28DAF) and the face form
 * (2F623 in 2F60D); no other access. */
#define XEEN_DS_SPEECH_NAME       0xfa92u
#define XEEN_DS_SPEECH_NAME_BYTES 99u

/* Word: the box.vga sprite handle (DS:6567) of the cutscene subtitles: each cutscene's entry loads it (3401A, 3448A,
 * 351A3, 35BA2); the subtitle frame 185E3 draws its frame 0 (1866C). */
/* Bytes: the current song's file name (16 bytes, NUL-terminated): the music start loads the file it names (18EFF);
 * the scene's and the locations' music choices compare it with their song (1ABD1..1AE01, 2A0B8..) and, before
 * replacing it, copy it to PREVIOUS_SONG_NAME (15 bytes; 42257, 2A0C9), which the location's leave plays again
 * (2809B). 9 + 4 immediate uses. */
#define XEEN_DS_SONG_NAME          0x9766u
#define XEEN_DS_PREVIOUS_SONG_NAME 0x9757u
#define XEEN_DS_SUBTITLE_BOX 0xfaf6u

/*
 * Word: the current attack's damage type (0 physical; the spells' and hazards' types up to 12h). Each attack stores
 * its type before the damage: the spell handlers 39DA8..3AEAE, the volley 281A7 (its type argument), the step's
 * hazards 172A2..172E1, the melee 371E2 (0, at 371F2). Readers: the monsters' resistance lookups by type (36934, 3697F,
 * 3704E, 3770E, 3B4D6/3B4F0), the physical resistance only for type 0 (367EF, 36882), the volley's types 11h and 12h
 * (281D9, 3745C), the party damage 2A152 (pushed at 17359).
 */
#define XEEN_DS_DAMAGE_TYPE 0x972eu
/* Byte: the game's preference, chosen once at a new game ("Select Game Preference", 5FAE: 'A' 0, 'W' 1; written
 * only at 2B557). Its one reader, the weapon attack's preparation (38A8C), gives the 'A' preference +5 to hit and
 * three times the damage. */
#define XEEN_DS_GAME_PREFERENCE     0xeda7u
#define XEEN_PREFERENCE_ADVENTURER  0u /* 'A' */
#define XEEN_PREFERENCE_WARRIOR     1u /* 'W' */

/* Words: the spell volley's arguments beside its type (281A7(damage, type, mode)): the fixed damage (the spells'
 * amounts; 0 for the party's missiles, whose damage then comes from each weapon: 28213), read by the volley's hit
 * 37452 (374C9..376CC); and the target mode (XEEN_VOLLEY_ONE, _CELL, _COLUMN), read by 281A7's target loops
 * (2845D..287D7). */
/* Byte: the party's target, a map-monster index (xeen/map_objects.h), FFh none: the volley's target loops (28445..
 * 287F6), the melee (36EC6, FFh at 36EAF) and the fight's target choices by view slot (38CE1..39318) store it, the
 * fight's end clears it (39553); 58 readers (the damage routines, the monster's record and data row). */
#define XEEN_DS_TARGET_MONSTER 0x306au
/* Byte: which members an event's party damage and member tests apply to. Operation 15 (3232F) stores the script's
 * selector there for selectors 0..6; the party damage (2E68D) damages every member for 0 and only its target
 * otherwise, 2 being the first two able members (2E6B6); the If operations' selection 8 skips the member it names.
 * The surfaces' and the fall's whole-party damage save it, clear it and restore it (17349, 17264); the command
 * loop's start (1791F..17984) sets 1. */
#define XEEN_DS_MEMBER_SELECTOR 0x98d7u
#define XEEN_NO_TARGET         0xffu /* XEEN_DS_TARGET_MONSTER: no monster targeted */

/*
 * The pending treasure, settled by 2F1AA after the treasure dialog: the gold dword (2F1AD..2F1B5 add it to F00A) and
 * the gems dword (2F1B9..2F1C4 add it to F00E), zeroed then with the byte that marks pending items (2F1DC) and the
 * four item lists 9970..9A0F (xeen_treasure_list). Combat's loot adds a monster's gold (its data +2Ah, 36C02) and gems
 * (+2Ch, 36C43); the events add both (2E0D2, 2E127).
 */
#define XEEN_DS_TREASURE_ITEMS_PENDING 0x9a10u /* byte: 1 once an item waits in the lists */
#define XEEN_DS_TREASURE_GEMS          0x9a12u /* dword */
#define XEEN_DS_TREASURE_GOLD          0x9a16u /* dword */
/*
 * The blacksmiths' wares: per world (Clouds EDA8h, Darkside F0CCh) and item category, nine rows of the four shops'
 * items (4 bytes each, a row 16 bytes; the shop is xeen_ware_column's), 90h bytes a category. 41D34.. clears them,
 * 41E31.. generates them, 3D06B.. and 3D2E0.. copy a shop's column from and to the scratch record.
 */
#define XEEN_DS_WARES(world, category) ((uint16_t)(((world) ? 0xf0ccu : 0xeda8u) + XEEN_WARES_BYTES * (category)))
#define XEEN_WARES_BYTES               0x90u
#define XEEN_WARES_ROW_BYTES           16u
/* Word: the fixed item of the next random item (Give 66): an item number across the categories (weapons from 0,
 * armor from 23h, accessories from 31h, misc from 3Ch), 0 none. Operation 45 (ItemType) sets it (33320), the event
 * dispatch clears it (310C9), Give 66 tests it (2D94E) and 30644 maps it to its category and id (10 accesses). */
#define XEEN_DS_FIXED_ITEM 0x8c74u

/* Byte: the attack's hit slot (0..11), the index of the twelve-entry hit arrays (96EF.., 97A6..): the target choices
 * store the view slot 0..2 with the target (38CFF, 392F3/39307/3931B), the melee 0 (36EC9), the volley steps it per
 * tier (FFh at 2831B, `inc` at 28412..28779). */
/*
 * The scene's draw lists (src/data structs.json indoor_view_list and outdoor_view_list: 170 and 132 records and an
 * end record, drawn by 4ADB1 at 2469A and 2538B) and their 12-byte records (the record type view_record): the
 * sprite handle (0 ends the list, negative skips), the frame (FFFFh skips), x and y (added to the window's origin),
 * the scale (FFFFh skips; 8000h the 2 x 2 enlarge) and the flags (the drawer's bits 8000h, 4000h, 2000h).
 * The indoor list is drawn in record order, so later records cover earlier ones. Its sprites of the party's column
 * and the cells beside it come by depth, farthest first, and within a depth in one order: the wall items, the map
 * objects, the monsters, the projectile level (Phase 257, from the view tables' record numbers): depth 3 records
 * 46..54, 55..59, 60..70, 79..86; depth 2 92..96, 97..99, 100..106, 111..118; depth 1 122..124, 125..127, 128..132,
 * 135..142; the party's own cell 148, 149, 150..158 (and the front group 159..161), 162..169. The surfaces are 143..147.
 * The walls come before each depth's sprites, one record per view position (Phase 258, from the visibility rules,
 * each of which sets one record's frame from one position's flags): the sides of the cells 4 ahead 29..32
 * (LEFT(4, L3..C)) and 33..36 (RIGHT(4, R3..C)), the faces 3 ahead 37..45 (FACE(3, L4..R4)), the sides 3 ahead
 * 71..74 (LEFT(3, L3..C)) and 75..78 (RIGHT(3, C..R3)), the faces 2 ahead 87..91 (L2..R2), the sides 107, 108
 * (LEFT(2, L1, C)) and 109, 110 (RIGHT(2, C, R1)), the faces 1 ahead 119..121 (L1..R1), the sides 133 and 134
 * (LEFT(1, C), RIGHT(1, C)).
 */
#define XEEN_DS_INDOOR_VIEW_LIST  0x962u
#define XEEN_DS_OUTDOOR_VIEW_LIST 0x1166u
#define XEEN_VIEW_RECORD_BYTES    12u
#define XEEN_VIEW_RECORD_HANDLE   0u
#define XEEN_VIEW_RECORD_FRAME    2u
#define XEEN_VIEW_RECORD_X        4u
#define XEEN_VIEW_RECORD_Y        6u
#define XEEN_VIEW_RECORD_SCALE    8u
#define XEEN_VIEW_RECORD_FLAGS    10u
/* A view record's handle word, or the frame word of a monster or object view record, when the record draws nothing
 * this frame: the scene clears records to it before filling them, and skips or takes a record by it. */
#define XEEN_VIEW_RECORD_UNUSED   0xffffu
#define XEEN_DS_INDOOR_VIEW_RECORD(n)  (XEEN_DS_INDOOR_VIEW_LIST + XEEN_VIEW_RECORD_BYTES * (n))
#define XEEN_DS_OUTDOOR_VIEW_RECORD(n) (XEEN_DS_OUTDOOR_VIEW_LIST + XEEN_VIEW_RECORD_BYTES * (n))
/*
 * The projectile records: for each of four levels a run of eight view records, one per member (XEEN_DS_MISSILE_SLOTS),
 * level 0 nearest the party (indoors records 162, 135, 111, 79; outdoors 124, 95, 76, 53). The scene animation steps
 * a monster's shot toward level 0, where it ends and frees the slot, and the party's missiles toward level 3;
 * xeen_projectile_setup gives level L the scale 3 + 4L, and 39654 clears the levels far to near.
 */
/* Words: the outdoor view records whose frame each scene sets to FFFFh first (1B71B..1B735, before the projectiles):
 * the program data's outdoor_hidden_records, 44 record numbers. */
#define XEEN_DS_OUTDOOR_HIDDEN_RECORDS 0x4c38u
#define XEEN_OUTDOOR_HIDDEN_RECORDS    44u
/* Bytes: a map object's pose 0..3 by its facing x 4 + the party's facing, (party - object) mod 4 (the program data's
 * object_poses, read by the scene's object animation 1B648..1B6D6; no original writer). */
#define XEEN_DS_OBJECT_POSES 0x4c27u
#define XEEN_OBJECT_POSES    16u
/* The monster records of the party's own cell (the monster view's cell 0, 1ED46: step entry 2, offset (0, 0); its
 * monsters are XEEN_DS_VIEW_MONSTER(0..2)): view slot 0's three records from 156,
 * slot 1's from 150, slot 2's from 153. The scene's shuffle (1BDE4) copies the group of the first slot whose monster
 * has a frame of 8 or more (8 attacking, 11 hit) to records 159..161, which are drawn after them, and 1BED9 copies
 * it back. */
#define XEEN_DS_PARTY_CELL_MONSTER_RECORDS(slot)                                                                   \
    XEEN_DS_INDOOR_VIEW_RECORD((slot) == 0u ? 156u : (slot) == 1u ? 150u : 153u)
#define XEEN_DS_FRONT_MONSTER_RECORDS XEEN_DS_INDOOR_VIEW_RECORD(159u)
/* The projectile levels (8 records each, one per party member): level L is drawn with the monsters of depth L (the
 * indoor list's order above), and its scale grows with L (3 + 4 L for a monster's shot, xeen_projectile_setup; 4 L for
 * the party's missiles, arm_projectiles): level 0 is the party's own cell. */
#define XEEN_DS_INDOOR_PROJECTILES(level)                                                                          \
    XEEN_DS_INDOOR_VIEW_RECORD((level) == 0u ? 162u : (level) == 1u ? 135u : (level) == 2u ? 111u : 79u)
#define XEEN_DS_OUTDOOR_PROJECTILES(level)                                                                         \
    XEEN_DS_OUTDOOR_VIEW_RECORD((level) == 0u ? 124u : (level) == 1u ? 95u : (level) == 2u ? 76u : 53u)
/* Record N of the view_record list at LIST. The program data's other lists of view_records: the command icons
 * (window 0; 28D80, the cutscenes), the control panel's (window 17h; 296DE writes the frames), the item dialog's icon rows (44F92
 * draws them one by one), the inn's 2 x 2 grid (window 0Bh) and the character view's 24 icons (window 18h). */
#define XEEN_VIEW_LIST_RECORD(list, n) ((list) + XEEN_VIEW_RECORD_BYTES * (n))
/* The address of FIELD of record I of the list at LIST (the sites' own arithmetic: list + field + 12 x I). */
#define XEEN_VIEW_FIELD(list, i, field) ((uint16_t)((list) + XEEN_VIEW_RECORD_##field + XEEN_VIEW_RECORD_BYTES * (i)))
/* The right column's command icons (program data's command_icon_list; face_draw_list before Phase 264): record 0 the
 * icon box's background (frame 7 at 232, 74), records 1..9 a 3 x 3 grid at x 235/260/286, y 75/96/117, records 10..15
 * two rows at y 148/169; the world entry and the combat starts give records 1..15 the current icon sprites (combat.icn
 * in a fight); 28D80 draws it (window 0) before showing the icon box. */
#define XEEN_DS_COMMAND_ICONS   0x2a5cu
#define XEEN_DS_PANEL_DRAW_LIST 0x2b28u
#define XEEN_DS_ITEM_ICON_ROWS  0x2ba0u
#define XEEN_DS_INN_DRAW_LIST   0x2c18u
#define XEEN_DS_ICON_DRAW_LIST  0x2c54u
#define XEEN_DS_HIT_SLOT 0x9804u
/*
 * The twelve-entry hit arrays (index XEEN_DS_HIT_SLOT), written only by the monster damage 36738 and read only by the
 * scene's hit effects (indoors 1BC3C..1BD72, outdoors 1B813..1B949, slots 0..2 drawn: scene.c effects):
 * - HIT_FRAMES (bytes): the scene passes the hit stays shown: 3 on a damaging hit (3687D), one less per effects pass
 *   while nonzero (1B833/1BC5C); 0 at the monster's death (36EAA) and the combat start; the monster update clears all.
 * - HIT_PHYSICAL (bytes): 1 when the hit shows the blow (charpow frame 0): damage type 0 and a melee or volley-missile
 *   hit (36899), else 0 (368A7).
 * - HIT_ELEMENT (bytes): the extra damage's element category + 1 when it was positive (368F6), its effect's frame;
 *   0 none (36923).
 * - HIT_DAMAGE_SCALE, HIT_ELEMENT_SCALE (words): the blow's and the element effect's draw scale for the damage and the
 *   extra (369E2, 36911): XEEN_HIT_SCALE(amount).
 */
#define XEEN_DS_HIT_FRAMES        0x96efu
#define XEEN_DS_HIT_PHYSICAL      0x97b2u
#define XEEN_DS_HIT_ELEMENT       0x97a6u
#define XEEN_DS_HIT_ELEMENT_SCALE 0x9776u
#define XEEN_DS_HIT_DAMAGE_SCALE  0x978eu
#define XEEN_HIT_SLOTS            12u
/* The hit effect's scale for AMOUNT (above 0): 5 (shrunk) below 10, 0 (full size) below 100, else 8000h (the raster's
 * 2 x 2 enlarge; the effects then take a third of the record's x). */
#define XEEN_HIT_SCALE_100 0x8000u /* the scale word of a hit of 100 or more (the scene's own case) */
#define XEEN_HIT_SCALE(amount) ((amount) < 10 ? 5u : (amount) < 100 ? 0u : XEEN_HIT_SCALE_100)

/* Bytes: the party attack's weapon, prepared by 3891F (cleared at 38926..3893F, then from the equipped weapon row
 * 38A05..38A53) for the damage 36738..: its id (the Xeen Slayer 22h test 367F6, the power table 369FA/36A2F), its
 * dice count and sides (the weapon tables by id; the roll 38A5A/38A6E) and its element material (0 none, below 37;
 * the element ranges 36FE3..37040). 96EC (the power) is written only. */
#define XEEN_DS_ATTACK_WEAPON  0x96eeu
#define XEEN_DS_ATTACK_DICE    0x96e5u
#define XEEN_DS_ATTACK_SIDES   0x96e4u
#define XEEN_DS_ATTACK_ELEMENT 0x96edu
/* The weapon preparation's other results (3891F clears them at 3892D/38939): ATTACK_DAMAGE (word), the rolled weapon
 * damage (the dice 38A1B/38A69, at least 0 at 38A7F), read by the hits (374DA, 38ACA); ATTACK_HIT_BONUS (dword with
 * 96EA), the to-hit bonus (38A0E, + 5 at 38A93) the hit roll adds (38849). 10 and 5 word accesses. */
#define XEEN_DS_ATTACK_DAMAGE    0x96e6u
#define XEEN_DS_ATTACK_HIT_BONUS 0x96e8u

#define XEEN_DS_VOLLEY_DAMAGE 0x9730u
#define XEEN_DS_VOLLEY_MODE   0x972cu
/* XEEN_DS_VOLLEY_MODE values, by 281A7's tier loop (2845D..287D7; combat.c volley_tiers): the volley walks the
 * party's column from its own cell outwards (tier = depth: the view cells 0, 5, 12, 25), a wall at or above the map
 * threshold between two depths stopping it, and ends after the first target hit (ONE), after the first depth with a
 * target (CELL), or only when the depths, the walls or the arrows end it (COLUMN). */
#define XEEN_VOLLEY_ONE    0u
#define XEEN_VOLLEY_CELL   1u
#define XEEN_VOLLEY_COLUMN 2u
/* Word: 1 when the volley's hit 371E2 rolls each member's missile (37452: every party position whose missile slot
 * 9744 is set makes its own to-hit roll against the target) unless the damage type is 12h; 0 a single spell hit
 * (37621). Its writers are 281A7's: 1 for damage type 11h (made 5, all six slots set, 281E9) and for the missile
 * icon 0Bh (28206; 2A2D4's physical volley passes it), 0 for any other icon's single missile (28270). 4 word
 * accesses. */
#define XEEN_DS_VOLLEY_BY_MEMBER 0x972au
/* Bytes, one per party position: the member's misses in this volley (3760B); the member volley skips a member with a
 * miss (3749E); the volley's start clears them. 2 indexed accesses. */
#define XEEN_DS_VOLLEY_MISSED 0x973cu
/* Byte: 1 once a member ran from the fight (the Run command's end 38B37); the combat start clears it (39019); at the
 * fight's end (39435) a run with attackers left moves the party to the run position. 3 byte accesses. */
#define XEEN_DS_COMBAT_RAN 0xfafeu

/*
 * Party variables of the maze.pty block (ED8C..) that the event interpreter's If conditions, take/give
 * modes and SetVar types read and write by the same number (the event variables of event_exec.c).
 * Widths are the original's: gold and gems are dwords (always two word accesses), food, the day and
 * the year words.
 */
#define XEEN_DS_DAY        0xeff0u /* word; character creation keeps its low byte as the birth day */
#define XEEN_DS_YEAR       0xeff2u /* word; a member's age is the year minus the birth year (2BAC7) */
#define XEEN_DS_PARTY_FOOD 0xeff6u /* word */
#define XEEN_DS_PARTY_GOLD 0xf00au /* dword (291FB kind 0, "Not enough gold") */
#define XEEN_DS_PARTY_GEMS 0xf00eu /* dword (291FB kind 1) */

/*
 * Bytes of the maze.pty block, the control panel's two switches (1 on, 0 off; its E and M keys toggle them with
 * `xor 1`, 299A1 and 299BB): sound (the effects and the voices: 1880E, 187E9, the cutscene subtitles' test) and music.
 * Both are 1 at start-up (15F77/15F7C) and set to 1 again by the ending before its score (3355E/33561).
 */
#define XEEN_DS_SOUND_ON 0xed9au
#define XEEN_DS_MUSIC_ON 0xed9bu
/*
 * Bytes: the control panel's Speech and Text options (the CD version's talking faces): 'p' toggles SPEECH_ON
 * (29994) and 'T' toggles TEXT_OFF (2998C); the panel shows Speech "On" for SPEECH_ON = 1 and Text "On" for
 * TEXT_OFF = 0, and with Speech off it stores TEXT_OFF = 0 (2976D): text is off only while speech is on.
 * SPEECH_ON is read by the CD voice side test 19CC3 and the panel (29740). TEXT_OFF chooses the talking forms'
 * face-only layout (2F35B/2F60D/3000D: the face row F384 x 1Ah + 4Bh, no text page) and the speech face 28D9E..;
 * the event script saves and clears it on Darkside map 1Dh at (1Eh, 14h) (313BD) and restores it at its end (33B18).
 * 29 and 3 byte accesses.
 */
#define XEEN_DS_TEXT_OFF  0xf384u
#define XEEN_DS_SPEECH_ON 0xf385u
/* The PlayCD tuple (program data cd_play, three words): event operation 60 writes it before an NPC (33489 CBW of the
 * record's byte +6, 33490 its word +7, 33497 its word +9) and the CD voice 483C7 plays it (all three zero: no play,
 * 483D8..483E7); 17E10 (F8) and the map loads (188E8..) replay it; the disc swap 47F7F stores 0 into all three at
 * its end (4813A..48146; the native drive never swaps: game_panel.c cd_side_check). The track: one above 99 plays as
 * track - 100 (483FC/48403), its positions below 60000 then 65536 later (48408/4841A). The start and end positions
 * are decimal MMSSFF numbers (minutes x 10000 + seconds x 100 + frames: 48431.. and 4848E.. split them by 1074B for
 * 48167). */
#define XEEN_DS_CD_PLAY_TRACK 0x84e0u
#define XEEN_DS_CD_PLAY_START 0x84e2u
#define XEEN_DS_CD_PLAY_END   0x84e4u
/* Word: the CD voice replay. 47B7D sets it to 1 after stopping a busy drive (483B4) when the side's word F614 + 2 x
 * [96E2] is 0; 48505 clears it when 483C7 starts a play, and the start-up 47C1F at 47C2D (its initial 0); after a
 * resource load 188E8, 18923, 18964 and 189AB call 483C7 again while it is set (Phases 131, 136; seven direct sites). */
#define XEEN_DS_CD_REPLAY 0xfb06u
/* The CD track table: an entry of 7 bytes per track at FB15 + 7 x track. 48170 and 4817E write an entry's first two
 * bytes (0Bh, the track's low byte) before 48167's 482F4 request; the driver's answer fills +2..+6 (Phase 131). */
#define XEEN_DS_CD_TRACK_TABLE 0xfb15u
/* Two words, by archive (0 xeen.cc, 1 dark.cc; indexed by the world DS 94 or the side 96E2): 1 when the start-up found
 * the archive on the disk (47ED5 xeen.cc, 47EB6 dark.cc: 19AEB of the names at 298A/298E's pointers is not FFFFh),
 * else 0 (it is read from the CD). 1933A selects the drive for the archive (106CD: F60E when 1, else the CD's F612);
 * 47BA2 stops a busy CD voice before a load (483B4, then XEEN_DS_CD_REPLAY) only for a side read from the CD; 15D1A
 * reads dark.cc's word. */
#define XEEN_DS_CC_ON_DISK 0xf614u
/* Word: the disc in the CD drive, the volume label's digit - '1' (47DB6 and the disc swap's 4810A: [AC41] - 31h);
 * 19D36 (the map load's side check 19CC3) asks for a swap (47F7F) when the side differs; 47F96 reads it. */
#define XEEN_DS_CD_DISC 0xf61au
/* Word: the minute of the day, 0..59Fh (30CFE adds, 30D04 carries each 5A0h into the day; 2FC5A subtracts,
 * 2FC60 borrows a day). */
#define XEEN_DS_MINUTE_OF_DAY 0xeff4u
/* The night's minutes of the day: before 05:00 or from 21:00 (2FFE7's night value; the dawn's notice before 05:00). */
#define XEEN_NIGHT_ENDS   300u
#define XEEN_NIGHT_BEGINS 1260u
/* The C runtime's time zone (the DOS date and time conversions): TIMEZONE (dword, seconds west of UTC; 4650h = 18000,
 * five hours, by default: 151D5/151DB) and DAYLIGHT (word, 1 when the zone has a daylight rule: 151CF, 1526C/152EC
 * from TZ), read by the conversions (13426, 135C7, 14DF8, 14F1E; 13295, 135D5, 14EA3, 15010). */
#define XEEN_DS_CRT_TIMEZONE 0x8c14u
#define XEEN_DS_CRT_DAYLIGHT 0x8c18u
/* The C runtime's static storage above the initialized data, written by the native date conversions (main_menu.c):
 * the time-zone names that tzname[0] and tzname[1] point at (p8C0C, p8C10: "EST" and "EDT", copied by tzset 151E2 and
 * 151F7), the struct tm that localtime's comtime fills (131AE; nine words sec, min, hour, mday, mon, year, wday,
 * yday, isdst, read in that layout by asctime 133A5) and asctime's text (13447: 26 bytes, "Www Mmm dd hh:mm:ss yyyy\n").
 * Read back only by the save list's date line (game_save.c). */
#define XEEN_DS_CRT_ASCTIME_TEXT 0xfd42u
#define XEEN_DS_CRT_TM           0xfd5cu
#define XEEN_DS_CRT_TZNAME_DST   0xfd70u
#define XEEN_DS_CRT_TZNAME_STD   0xfd74u
/* Dwords: the bank's gold and gems (the bank 26E90/27198 moves amounts between these and F00A/F00E). */
#define XEEN_DS_BANK_GOLD 0xf012u
#define XEEN_DS_BANK_GEMS 0xf016u

/*
 * The party's spell effects. 29168 clears all of them (the morning step and Rest); the take/give,
 * SetVar and If variables 70..73 and 17 (2C94C, 2E285, 2BF6E) read and write them by number; the
 * Info screen (29CA0) lists them with the DS:5CCA..5D44 texts named here.
 * Words: the Light count ("Light"), incremented by Light (39CDA) and decremented by each scene
 * build on a map that needs light (1C952, F486 set); the four elemental protections ("Fire",
 * "Electricity", "Cold", "Poison/Acid"), the amount subtracted from damage of that type (37B6D,
 * 2E68D: party_resist[type]). Protection from Elements (39FEC) sets one, Day of Protection
 * (3B764) all four and the light to the caster's level.
 */
#define XEEN_DS_PARTY_LIGHT            0xeff8u
#define XEEN_DS_PROTECTION_FIRE        0xeffcu
#define XEEN_DS_PROTECTION_ELECTRICITY 0xeffeu
#define XEEN_DS_PROTECTION_COLD        0xf000u
#define XEEN_DS_PROTECTION_POISON      0xf002u
/* Bytes: Levitate, Wizard Eye, Clairvoyance and Walk on Water (each 1 while active, the Info
 * texts); Bless (the armour class: 428F1, 4666A), Power Shield (less damage taken: 37B6D, 2E68D),
 * Holy Bonus and Heroism (the 3891F damage and hit bonuses), each the caster's level. */
#define XEEN_DS_EFFECT_LEVITATE      0xed9eu
#define XEEN_DS_EFFECT_WIZARD_EYE    0xeda0u
#define XEEN_DS_EFFECT_CLAIRVOYANCE  0xeda1u
#define XEEN_DS_EFFECT_WALK_ON_WATER 0xeda2u
#define XEEN_DS_EFFECT_BLESS         0xeda3u
#define XEEN_DS_EFFECT_POWER_SHIELD  0xeda4u
#define XEEN_DS_EFFECT_HOLY_BONUS    0xeda5u
#define XEEN_DS_EFFECT_HEROISM       0xeda6u
/*
 * Bytes: the frames of three border indicators. The border frame 26306 steps each of them modulo 12 on every call
 * (2632D..263EB), whether or not its indicator shows, and draws the indicator from sheet F456: the Levitate
 * indicator (frame + 10h at 0,52h while EFFECT_LEVITATE), the Spot Secret Doors indicator (frame + 1Ch at C2h,5Bh
 * while F496 is set and a member has skill 16) and the Danger Sense indicator (frame + 28h at 6Bh,9 while
 * MONSTER_NEARBY and a member has skill 17); otherwise the indicator's first frame. The location entries (26D42,
 * 27E04) clear all three together with the Clairvoyance frames. No other access.
 */
#define XEEN_DS_LEVITATE_FRAME     0x974du
#define XEEN_DS_SECRET_DOORS_FRAME 0x974eu
#define XEEN_DS_DANGER_SENSE_FRAME 0x974fu
/*
 * Byte: 1 while monsters' ranged attacks are queued: the range check 41CDA sets it when it queues a monster within
 * shooting distance into the 36-entry shooter list 97D0 (and a missile slot 9744 for a distance above 1); 4151B
 * clears it (with 97F8) after 4128F ran the queued attacks. Readers: the monster update's end 2AAAA (then 4128F),
 * the monster attack 3804F (the ranged branch 38158), the scene's missile records (1B2F2.., moved toward the party),
 * the countdown 1B54F and the melee checks 17ED8/1C165 (no fight while set). 11 byte accesses.
 */
#define XEEN_DS_MONSTER_SHOTS_QUEUED 0x97f6u
/* Bytes: the queued shooters, map-monster indexes up to the first FFh (36 entries): the monster update fills them
 * with FFh (2A77E), the range check 41CB0/41CE2 appends a monster within shooting distance, 4128F runs them in order
 * (4129B..41311) and 414FC.. ends at the FFh. 7 indexed accesses, 1 immediate. */
#define XEEN_DS_MONSTER_SHOOTERS 0x97d0u
#define XEEN_MONSTER_SHOOTERS    36u
/*
 * Bytes: the eight missile slots, nonzero while the slot's missile is shown: the level of the scene's missile records
 * its sprite starts at (1..3; 413C7/41467). The party's shots use the members' positions (the volley 281A7: 1 for
 * each shooting member, 281F8..2826B, 28236), a monster's shot the first free slot with its distance tier (1, 2, 3
 * for distances 2, 3, 4; an adjacent monster takes none; 41D01, its id at 97C8, written only). The scene's record
 * moves clear a slot whose missile ended
 * (1B314/1B400), the member volley's hit clears the member's (374EB); 2EBAC clears all. 16 byte accesses.
 */
#define XEEN_DS_MISSILE_SLOTS 0x9744u
/*
 * Byte: 1 while a missile slot (the eight bytes 9744) is in use: the scans 246C8/253BB copy 97F7 (set when a slot
 * of a party position holds a missile), the party volley 28276 stores 1 with its slot; 2EBAC (after clearing the
 * slots) and 3BE09 store 0. Readers: the monster update 2A718 (deferred while set: 97F8 = 1), the scene's missile
 * records (1B369.., moved away from the party unless MONSTER_SHOTS_QUEUED) and the melee checks 17EE1/1C16E.
 * 12 byte accesses; 97F7 (its source, with other readers 284BD.., 39656, 414BD) and 97F8 (written only) stay numeric.
 */
#define XEEN_DS_MISSILES_FLYING 0x97f9u
/* Word: the sprite handle of the flying projectiles ("pow<n>.icn", loaded into XEEN_DS_RESIDENT_SPRITE_SLOT). Its
 * writers are the party's volley (2829E) and a monster's projectile delivery (4130B), each right after the load;
 * their own drawing reads it (2837C, 283D4; 413DF..4149D). Valid from that load until the slot is reloaded. */
#define XEEN_DS_PROJECTILE_SPRITE 0xf452u
/* Byte: 1 when the monster update found a map monster within three cells of the party: 2A789 clears it and 2A83B
 * sets it for any monster in the 7 x 7 square around the party, active or not; its one reader is the Danger Sense
 * indicator (263B1). */
#define XEEN_DS_MONSTER_NEARBY 0x974cu
/*
 * Bytes: Clairvoyance's two border sprites (sheet F456, the left one at 0,20h, the right one at D7h,20h).
 * The states: when a talk or question form opens, F467 takes byte 0 and F466 byte 1 of the party's cell in the map's
 * .hed (50D4:5F7C; 2F4C9/2F4FB, 2FAAE/2FAE0, 303AE/303E0); every other writer stores XEEN_CLAIRVOYANCE_STILL
 * (17977, the forms' closes 2F577, 2F5CC, 2FC17, 303ED, 30538, the event script's end 33690).
 * The frames: the border frame steps each modulo 4 (263EF, 2641D), then state 0 moves the left one to 4..7 and the
 * right one to 8..11, state 2 holds the still frames (left 0, right 8), and any other state keeps the plain cycle
 * (left 0..3, right 12..15); without EFFECT_CLAIRVOYANCE both show the still frames (2644E). The location entries
 * clear both frames (26D42, 27E04). These are the states' only readers (26400..26442).
 */
#define XEEN_DS_CLAIRVOYANCE_LEFT_STATE  0xf467u
#define XEEN_DS_CLAIRVOYANCE_RIGHT_STATE 0xf466u
#define XEEN_DS_CLAIRVOYANCE_LEFT_FRAME  0xf469u
#define XEEN_DS_CLAIRVOYANCE_RIGHT_FRAME 0xf468u
#define XEEN_CLAIRVOYANCE_STILL 2u
/* The location entries' clear of the five border frames (26D42, 27E04), in the original's store order. */
static inline void xeen_border_frames_clear(uint8_t *ds)
{
    ds[XEEN_DS_CLAIRVOYANCE_RIGHT_FRAME] = ds[XEEN_DS_CLAIRVOYANCE_LEFT_FRAME] = ds[XEEN_DS_DANGER_SENSE_FRAME] =
        ds[XEEN_DS_SECRET_DOORS_FRAME] = ds[XEEN_DS_LEVITATE_FRAME] = 0;
}
/* Byte: the minimap switch: '=' (or a click on the minimap) toggles it (17E20 XOR 1); 1E467 draws
 * the minimap while it or Wizard Eye is set; Wizard Eye (3A21F) and Day of Sorcery clear it. */
#define XEEN_DS_MINIMAP_SHOWN 0xed9fu
/*
 * The nine loaded maps (the party's map and its neighbours): MAP_SLOT_IDS holds each slot's map id (nine words; the
 * searches 19DC2.., 1C70B.. compare it), MAP_SLOT the slot (0..8) that the last search found: the slot's map data
 * then lies at BDCC + 37Ch x slot (the cell words, BFCC.. the cell bytes, C128.. the seen bits). 85 word accesses of
 * MAP_SLOT, 15 indexed ones of the ids.
 * CELL_SEEN (word): whether the cell that the cell query 1C680 looked up shows on the minimap: the slot's seen bit of
 * the cell (1C801), 1 for the surface classes 10 and 15 (1C7E2) and outside the loaded maps (1C717), the outdoors flag
 * F608 for a missing neighbour (1C82E); read by the minimap's draws (1DBC5..1EC42). 30 word accesses.
 */
/* The nine loaded maps' records (xeen/map_record.h), slot S's at XEEN_DS_MAP_DATA + XEEN_MAP_RECORD_BYTES x S; slot 0
 * holds the party's map (the cache's commit, resources.c), which the direct accesses (C0CC.., C0EA..) read. */
#define XEEN_DS_MAP_DATA     0xbdccu
/* FIELD of slot 0's record (the party's map); with + XEEN_MAP_RECORD_BYTES x S, of slot S's. */
#define XEEN_DS_MAP_FIELD(field) (XEEN_DS_MAP_DATA + XEEN_MAP_RECORD_##field)
#define XEEN_DS_MAP_SLOT_IDS 0xf46au
#define XEEN_DS_MAP_SLOT     0xf494u
#define XEEN_DS_CELL_SEEN    0xf492u

/*
 * Far pointer (offset word; the segment word at + 2): the character record a command acts for: the
 * caster (3D8C1 Cast, 44F92 an item's power) or the member chosen in the items dialog (3BAEE).
 * Every reader (the spell handlers, 281A7, 371E2, the items dialog) pushes both words as the far
 * character argument; nothing reads either word alone.
 */
#define XEEN_DS_ACTING_CHARACTER 0x9800u

/*
 * The animated picture of a location's scene or of an NPC's talking face: 277E5 sets it for a
 * location kind, 3000D for a face (kind 7, 4 frames, its position by F384); 26978 and the scene
 * redraws (28D80, 2F2FF, 2F5B1, 2F9BA) read it.
 *   ANIMATION_X, _Y      words: the draw position (pushed as the sprite call's x and y);
 *   ANIMATION_FRAME      word: the frame, the 26978 step's (frame + 1) IDIV FRAME_COUNT;
 *   ANIMATION_KIND       word: the location kind 0..5 (26978 animates by it), or ANIMATION_FACE;
 *   ANIMATION_FRAME_COUNT word: the IDIV divisor: per location kind from DS:3026 (world x 11 +
 *                        kind, signed bytes); on Clouds the counts 20h and 1Ah restart a cycle
 *                        at frame 11h and 14h instead of 0;
 *   ANIMATION_SPRITES    8 words (8D56..8D65): the sprite handles of the %s1.twn files (DS:3018
 *                        world x 7 + kind of them) or the face; a face's frame f is frame f % 8
 *                        of file f / 8. The location exit clears all eight.
 */
#define XEEN_DS_ANIMATION_Y           0x8d2eu
#define XEEN_DS_ANIMATION_X           0x8d30u
#define XEEN_DS_ANIMATION_FRAME       0x8d32u
#define XEEN_DS_ANIMATION_KIND        0x8d34u
#define XEEN_ANIMATION_FACE           7u
#define XEEN_DS_ANIMATION_FRAME_COUNT 0x8d36u
#define XEEN_DS_ANIMATION_SPRITES     0x8d56u
#define XEEN_DS_ANIMATION_SPRITE(n)   ((uint16_t)(XEEN_DS_ANIMATION_SPRITES + 2u * (n)))
/*
 * Word: the minutes a location visit charges: 0 at the entry (277E5), + 1440 for each temple cure
 * (257A8) and at the exit (280CE), which then passes it to 30CF3 (the Sign In exit does not).
 */
#define XEEN_DS_LOCATION_TIME_CHARGE 0xf628u

/*
 * Word: the view's light level, the argument of the driver's viewport darkening (1BF62 -> 4A68A):
 * 4 or more leaves the 3D view as drawn, 0 blacks it out, 1..3 select a darkening table. Each scene
 * build sets 4, or 0 on a dark map (C0D8 bit 4000h) without light (1C952); the inn's exit (4441A)
 * fades it from 4 to 0 and leaves FFFFh. Light (39CDA) sets PARTY_ACTED (below) when it is nonzero.
 */
#define XEEN_DS_VIEW_LIGHT_LEVEL 0xf60cu

/*
 * Byte: the party has acted since the last scene (Phase 234 census: 12 direct byte sites, no indexed or
 * word access). Set to 1 by each step (17264's 1726B), a map load (19ECB), the events' 319F9 and 32472,
 * combat after its 2A701 (391D6) and at its end (394CF), Light (39CE5, with F60C set), the spell at
 * 3A16B and an item's use (44DD3); cleared by 1B503's tail (1C18F). Read twice: the scene builds' 1C952
 * counts the step (Light decremented on a dark map, the party's cell marked mapped), and the Use
 * command's 17DFE runs the monster update 2A701 when the use set it.
 */
#define XEEN_DS_PARTY_ACTED 0x3067u

/*
 * Word: the current map's restriction bits (the map data, read only). Each reader tests the one
 * bit of its own action, which is refused on such a map.
 */
#define XEEN_DS_MAP_RESTRICTIONS    0xc0d6u
#define XEEN_MAP_NO_ETHEREALIZE     0x0040u /* 3B2D6 */
#define XEEN_MAP_NO_TOWN_PORTAL     0x0100u /* 3AF94 */
#define XEEN_MAP_NO_SUPER_SHELTER   0x0200u /* 3AF2B */
#define XEEN_MAP_NO_TIME_DISTORTION 0x0400u /* 3AC41 */
#define XEEN_MAP_NO_LLOYDS_BEACON   0x0800u /* 3A5DA */
#define XEEN_MAP_NO_TELEPORT        0x1000u /* 3AC88 */
#define XEEN_MAP_NO_REST            0x4000u /* 28E35 */
#define XEEN_MAP_NO_SAVE            0x8000u /* 29502 */

/*
 * Bytes: the party's position (the xeen_navigation fields): the direction 0..3 (the step's switch index),
 * the signed x and y on the map (the steps inc/dec them; readers sign-extend with CBW), and the map number
 * (19E82 stores its argument there).
 */
#define XEEN_DS_PARTY_DIRECTION 0xed96u
#define XEEN_DS_PARTY_X         0xed97u
#define XEEN_DS_PARTY_Y         0xed98u
#define XEEN_DS_PARTY_MAP       0xed99u

/*
 * The monsters the scene sees (98B4..98CD): one byte per view slot, the index of the monster
 * standing there (its 20-byte record at E530: E530 + 20 x index), FFh for none. Both scene builders
 * (1ED46 indoors, 2126C outdoors) first fill all 26 with FFh (13FE7(DS:98B4, FFh, 1Ah)), then each
 * slot from the view's cells. Slots 0..2 are the party's own cell (Identify Monster reads them;
 * 38F5F's combat takes its three attackers from them; the event dispatch tests slot 0 at 313FD).
 */
#define XEEN_DS_VIEW_MONSTERS          0x98b4u
#define XEEN_DS_VIEW_MONSTER(n)        ((uint16_t)(XEEN_DS_VIEW_MONSTERS + (n)))
#define XEEN_VIEW_MONSTER_SLOTS        26u
#define XEEN_VIEW_NO_MONSTER           0xffu
/* Whether slots 0..2, the party's own cell, hold no monster (the contact tests of the scene, Shoot, Identify and
 * combat). */
static inline int xeen_view_party_cell_empty(const uint8_t *ds)
{
    return ds[XEEN_DS_VIEW_MONSTER(0)] == XEEN_VIEW_NO_MONSTER && ds[XEEN_DS_VIEW_MONSTER(1)] == XEEN_VIEW_NO_MONSTER &&
           ds[XEEN_DS_VIEW_MONSTER(2)] == XEEN_VIEW_NO_MONSTER;
}
/* Whether a monster in the party's cell is fought now: one stands there, the party explores or rests, no monster
 * shot is queued and no missile flies. Shoot's melee starts a fight on this alone; the scene's encounter also needs
 * the monsters enabled (xeen_scene_contact_due). Original: 17EB5..17EE8 (Shoot), the same tests at 1C142..1C175. */
static inline int xeen_party_cell_fight_due(const uint8_t *ds)
{
    return !xeen_view_party_cell_empty(ds) &&
           (ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_EXPLORATION || ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_REST) &&
           !ds[XEEN_DS_MONSTER_SHOTS_QUEUED] && !ds[XEEN_DS_MISSILES_FLYING];
}
/* The scene pass's encounter test: xeen_party_cell_fight_due with the monsters enabled (96C4), tested last.
 * Original: 1C142..1C17C. */
static inline int xeen_scene_contact_due(const uint8_t *ds)
{
    return xeen_party_cell_fight_due(ds) && ds[XEEN_DS_MONSTERS_ENABLED];
}

/*
 * Byte: 1 once the party is defeated (no member can act: 2F869; a step onto surface 15: 17264; the combat
 * loss 383D6), 0 at the start of play and when 2F869 finds an able member. The exploration and event loops
 * end on it (17377, 33670).
 */
#define XEEN_DS_PARTY_DEFEATED 0xf60au

/*
 * The event flag-bit arrays (2F847 tests, 2F8F3 sets and clears; bit 0 is a byte's most
 * significant). The world is DS:94 (0 Clouds, 1 Darkside where these are indexed by it).
 */
#define XEEN_DS_GAME_FLAGS     0xf01fu /* bit world x 256 + n (If 20, take/give 20) */
#define XEEN_DS_AUTO_NOTES     0xf05fu /* bit n (take/give 103, If 103); qnotes entry 37h + n */
#define XEEN_DS_QUEST_FLAGS    0xf06fu /* bit world x 30 + n (If, take/give 104) */
#define XEEN_DS_CHARACTER_BITS 0xf30cu /* 4 bytes per roster index (If, give 107) */
/*
 * Two of the program data's button sets (src/data structs.json button_sets: parallel arrays, x words ending FFFFh,
 * then a byte per button), as the code installs them into the BUTTON_*_TABLE far pointers:
 * - the exploration buttons (set197C, 20 buttons): installed by 4328B with the exploration keys and by 43248 with the
 *   combat keys; the choice forms (2F5B1) move the first three buttons' rows and end the set early at their count;
 * - the yes/no buttons (set1C20, two buttons): installed by 2F466 and 3006E; the talk forms set both rows.
 */
#define XEEN_DS_EXPLORATION_BUTTON_X       0x197cu
#define XEEN_DS_EXPLORATION_BUTTON_Y       0x19a6u
#define XEEN_DS_EXPLORATION_BUTTON_WIDTH   0x19bau
#define XEEN_DS_EXPLORATION_BUTTON_HEIGHT  0x19ceu
#define XEEN_DS_EXPLORATION_BUTTON_KEYS    0x19e2u
#define XEEN_DS_EXPLORATION_BUTTON_ENABLED 0x19f6u
#define XEEN_DS_COMBAT_BUTTON_KEYS         0x1a0au
#define XEEN_DS_YESNO_BUTTON_X             0x1c20u
#define XEEN_DS_YESNO_BUTTON_Y             0x1c26u
/* The yes/no buttons' row, their program-data y (4Bh): the talk forms move both buttons and their ends put them back. */
#define XEEN_YESNO_BUTTON_ROW              0x4bu
#define XEEN_DS_YESNO_BUTTON_WIDTH         0x1c28u
#define XEEN_DS_YESNO_BUTTON_HEIGHT        0x1c2au
#define XEEN_DS_YESNO_BUTTON_KEYS          0x1c2cu
/*
 * The window stack of 1871B (open) and 18765 (close): a byte flag per window at 986C + window (set
 * while it is open; 1871B increments it, 18765 clears it), the depth word 3064 (FFFFh: empty, 186C6
 * closes all) and the open windows in order at 9843 + depth (41 slots). Windows 42..47's flags
 * would overlap the button sprites at 9896: no window above 40 is used.
 */
#define XEEN_DS_WINDOW_OPEN    0x986cu
#define XEEN_DS_WINDOW_FLAG(n) (XEEN_DS_WINDOW_OPEN + (n))
#define XEEN_DS_WINDOW_DEPTH   0x3064u
#define XEEN_DS_WINDOW_STACK   0x9843u

/* Bytes indexed by a quest item's value (52h.. as an item value), so the first counter is F077. */
#define XEEN_DS_QUEST_ITEMS 0xf025u
/* The scripts' item numbers (the item tests 2C0F6, the take and give modes 2CB3F/2D1C8, GiveEnchanted 3312E):
 * one number per item id, the categories in a row: weapons from 0, then armor, accessories and misc, each id the
 * number minus its category's first; the quest items from XEEN_QUEST_ITEM_FIRST. */
#define XEEN_SCRIPT_ITEM_ARMOR       0x23u /* the first armor number (after 35 weapons) */
#define XEEN_SCRIPT_ITEM_ACCESSORIES 0x31u /* the first accessory number (after 14 armor) */
#define XEEN_SCRIPT_ITEM_MISC        0x3cu /* the first misc number (after 11 accessories) */
#define XEEN_QUEST_ITEM_FIRST 0x52u /* the first quest item's value: its counter is F077 */
#define XEEN_QUEST_ITEM_COUNT 0x55u
/* p24CC: the 85 quest items' names ("Deed to New Castle"..), by value - XEEN_QUEST_ITEM_FIRST (the Quests list). */
#define XEEN_DS_QUEST_ITEM_NAMES 0x24ccu

/*
 * The sprite handle stack of 190A3/190CF. SPRITE_HANDLE (word) is the handle the drawing code
 * uses now; 190A3 increments the signed byte HANDLE_DEPTH, saves the handle at
 * HANDLE_STACK + 2 * depth and loads a new one; 190CF frees the current handle, restores the saved
 * one and decrements the depth.
 */
#define XEEN_DS_SPRITE_HANDLE 0x98d4u
#define XEEN_DS_HANDLE_STACK  0x95a8u
#define XEEN_DS_HANDLE_DEPTH  0x96c1u

/* The DS offset of the stack slot at the current depth (CBW, so a depth of FFh is slot -1). */
#define XEEN_DS_HANDLE_SLOT(ds) ((uint16_t)(0x95a8 + 2 * (int)(int8_t)(ds)[XEEN_DS_HANDLE_DEPTH]))

/*
 * Words: the mouse position the command poll (18A22) has the mouse driver store, compared signed
 * by the click tests (18C0C, 17B3B). Item code (2C06C, 2CAC5, 2D150) reuses F604 as a 0..2
 * category index; those sites keep the address.
 */
#define XEEN_DS_MOUSE_X 0xf604u
#define XEEN_DS_MOUSE_Y 0xf602u

/*
 * The live button set that 18C0C tests the mouse position against. Each name is a far pointer
 * (the offset word here, the segment word at +2) to one table indexed by the button number: the
 * x words (an x of FFFFh ends the set), then bytes for y, width, height, the key returned, and
 * whether the button is enabled (a click on an enabled button draws its pressed frame).
 */
#define XEEN_DS_BUTTON_X_TABLE       0x98acu
#define XEEN_DS_BUTTON_Y_TABLE       0x98a8u
#define XEEN_DS_BUTTON_WIDTH_TABLE   0x98a4u
#define XEEN_DS_BUTTON_HEIGHT_TABLE  0x98a0u
#define XEEN_DS_BUTTON_KEY_TABLE     0x989cu
#define XEEN_DS_BUTTON_ENABLED_TABLE 0x9898u

/*
 * BUTTON_SPRITES (word) is the sprite handle that draws the live buttons: frame 2n + 1 is button n
 * pressed, 2n released (18B2F's feedback). Each button-set setup stores it with the six pointers
 * (mostly the current SPRITE_HANDLE); the empty set (1919B) stores FFFFh. BUTTON_SET is the whole
 * set, this word and the six far pointers above (9896..98AF), as the button-set stack (190ED, at
 * depth 96C2) saves and restores it and as screens that keep the set across a push copy it.
 */
#define XEEN_DS_BUTTON_SET       0x9896u
#define XEEN_DS_BUTTON_SET_BYTES 26u
#define XEEN_DS_BUTTON_SPRITES   XEEN_DS_BUTTON_SET

/*
 * The button-set stack. BUTTON_DEPTH (signed byte, 0 at start) is the current level. 190ED
 * increments it, then saves each live field into its own stack at the new level: the six table far
 * pointers (offset word, then segment word) into the *_STACK arrays of 4-byte slots, BUTTON_SPRITES
 * into a word slot. 18FFF copies the slots of the current level back and decrements the depth.
 * Each stack has BUTTON_STACK_SLOTS slots; the first push uses slot 1. Nothing checks the depth: a
 * pop at 0 leaves FFh (slot -1 next), and level 10 would write X_STACK's slot over 96C0..96C3
 * (BUTTON_DEPTH included). 3F15B and 3F862 read the saved set of the current level in place.
 */
#define XEEN_DS_BUTTON_DEPTH         0x96c2u
#define XEEN_DS_BUTTON_X_STACK       0x9698u
#define XEEN_DS_BUTTON_Y_STACK       0x965cu
#define XEEN_DS_BUTTON_WIDTH_STACK   0x9634u
#define XEEN_DS_BUTTON_HEIGHT_STACK  0x960cu
#define XEEN_DS_BUTTON_KEY_STACK     0x95e4u
#define XEEN_DS_BUTTON_ENABLED_STACK 0x95bcu
#define XEEN_DS_BUTTON_SPRITES_STACK 0x9684u
#define XEEN_BUTTON_STACK_SLOTS      10u

/* The DS offset of a table stack's slot at level `depth` (an int: the CBW of BUTTON_DEPTH). */
#define XEEN_DS_BUTTON_TABLE_SLOT(stack, depth) ((uint16_t)((int)(stack) + 4 * (depth)))
/* The DS offset of the sprite stack's slot at level `depth`. */
#define XEEN_DS_BUTTON_SPRITES_SLOT(depth) \
    ((uint16_t)((int)XEEN_DS_BUTTON_SPRITES_STACK + 2 * (depth)))

/*
 * The party members still in the fight, in party order. COMBAT_PARTY is a byte array: entry i is
 * the party slot (0..5, an index into the party records) of the member at combat position i; FFh
 * marks an empty entry. Each combat entry sets its 9 bytes to FFh and entries 0..count-1 to
 * 0..count-1. A member who runs (38B00) gets FFh, and 379B8 moves the later entries of the first
 * eight down over the gap. COMBAT_PARTY_COUNT (byte) is the number of listed members: the party
 * count at the fill, minus one per member who ran. The turn order is a separate list (9713, 12
 * bytes, built by 38D25), which ends just before the count byte.
 */
#define XEEN_DS_COMBAT_PARTY       0x9720u
#define XEEN_COMBAT_PARTY_EMPTY    0xffu /* an XEEN_DS_COMBAT_PARTY entry without a member */
#define XEEN_DS_COMBAT_PARTY_COUNT 0x971fu
#define XEEN_COMBAT_PARTY_BYTES    9u
/* Hazard: code that indexes COMBAT_PARTY with the CBW of a current-actor byte of FFh reads entry
 * -1, which is COMBAT_PARTY_COUNT (39232, at the 97FF = FFh Use). */

/*
 * The combat turn state. A combatant code is a combat position (0..COMBAT_PARTY_COUNT-1, a party
 * member through COMBAT_PARTY) or, from COMBAT_PARTY_COUNT up, an attacker slot (98B4 + code -
 * count). Three byte arrays of COMBATANT_BYTES (12) are indexed by that code and lie end to end
 * below COMBAT_PARTY_COUNT:
 *   COMBAT_BLOCKING  1 = the member chose Block this round (38B45); the defense (38049) reads it.
 *   TURN_TAKEN       1 = the combatant has had its turn this round (set for the previous actor when
 *                    the turn moves on, 386BA; 37923 ends the round when every one is set or
 *                    incapacitated; 379B8 moves a member's byte with its COMBAT_PARTY entry).
 *   TURN_ORDER       the combatant codes by falling speed (38D25), FFh after the last.
 * Combat entry (38FE0) and each new round (3935A) clear BLOCKING and TAKEN; 38D25 rebuilds ORDER.
 * TURN_INDEX (signed byte) is the position in TURN_ORDER of the turn now being played, FFh before
 * the round's first turn; the next actor is (index + 1) modulo the combatant count (386BF, IDIV).
 * TURN_ACTOR (byte) is TURN_ORDER[TURN_INDEX], FFh between turns and after a Run.
 * Hazards: the CBW of an FFh TURN_INDEX reads TURN_ORDER[-1] = TURN_TAKEN[11] (38D3C); the CBW of
 * an FFh TURN_ACTOR writes COMBAT_BLOCKING[-1] = 96FA (Block, 38B45).
 */
#define XEEN_DS_COMBAT_BLOCKING 0x96fbu
#define XEEN_DS_TURN_TAKEN      0x9707u
#define XEEN_DS_TURN_ORDER      0x9713u
#define XEEN_COMBATANT_BYTES    12u
#define XEEN_DS_TURN_INDEX      0x97feu
#define XEEN_DS_TURN_ACTOR      0x97ffu
#define XEEN_NO_TURN            0xffu /* TURN_INDEX before the round's first turn; TURN_ACTOR between turns */
#define XEEN_NO_PORTRAIT        0xffu /* XEEN_DS_HIGHLIGHTED_PORTRAIT: no portrait highlighted */

/* The number of members the party loops take part in: the combat party in combat (UI mode 2),
 * otherwise the whole party. */
static inline unsigned xeen_active_party_count(const uint8_t *ds)
{
    return ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_COMBAT ? ds[XEEN_DS_COMBAT_PARTY_COUNT]
                                                      : ds[XEEN_DS_PARTY_COUNT];
}

/* The party slot of the member at position in those loops (the position itself outside combat). */
static inline unsigned xeen_active_party_slot(const uint8_t *ds, unsigned position)
{
    return ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_COMBAT
                   ? ds[(uint16_t)(XEEN_DS_COMBAT_PARTY + position)]
                   : position;
}

#endif
