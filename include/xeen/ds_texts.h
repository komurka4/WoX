#ifndef XEEN_DS_TEXTS_H
#define XEEN_DS_TEXTS_H
/*
 * Program texts by the role their reader gives them: offsets of zero-terminated strings in the original's data
 * segment (src/data/texts/), read with ds_text(g, XEEN_TEXT_...). A text is named only when its original readers all
 * use it for the same purpose (tools/refactor/ds_census.py immediate OFFSET lists them); each entry quotes the text
 * and names that reader. tools/data/extract_program_data.py resolves these names like XEEN_DS_* ones, so a named
 * text still counts as a native reference of its offset.
 */

/* The Warzone arena (game_arena.c; the original's handler 360C4 and the location tail 27CCA). */
#define XEEN_TEXT_ARENA_LEVEL_PROMPT 0x6712u /* "What level of monsters? (1-10)" (362A0) */
#define XEEN_TEXT_ARENA_COUNT_PROMPT 0x6732u /* "How many monsters? (1-20)" (362DF) */
#define XEEN_TEXT_ARENA_WINS_NUMBER  0x66d8u /* "%05u": the first member's win count, for its ordinal (36177) */
#define XEEN_TEXT_ARENA_AGAIN        0x66ddu /* "What!  You again?  Go pick on someone your own size!" (361E2) */
#define XEEN_TEXT_ARENA_MONSTER_FILE 0x674du /* "%03u.mon": an opponent's sprite file (36434) */
#define XEEN_TEXT_ARENA_ATTACK_FILE  0x6756u /* "%03u.att": an opponent's attack sprite file (36492) */
#define XEEN_TEXT_MAP_TEXTS_FILE     0x567bu /* "aaze%c%03u.txt": the current map's texts, reloaded after the arena (27D1E) */

/* Detect Monster and Identify Monster (game_cast_info.c; the handlers 3A9C1 and 3A232). */
#define XEEN_TEXT_DETECT_SPRITES_DARKSIDE 0x69a1u /* "detectmn.icn" (3A9DC) */
#define XEEN_TEXT_DETECT_SPRITES_CLOUDS   0x69aeu /* "detctmon.icn" (3A9E3) */
#define XEEN_TEXT_DETECT_TITLE            0x69bbu /* "\x03cDetect Monsters" (3AA0F) */
#define XEEN_TEXT_IDENTIFY_LINE           0x68c9u /* one monster's line: name, HP, AC, attacks, special (3A386) */
#define XEEN_TEXT_IDENTIFY_TABLE          0x68ebu /* the column headings and the three lines (3A3C0) */
#define XEEN_TEXT_NUMBER_THOUSANDS        0x5228u /* "%uk": a number above 999 in thousands (2692E: 26909, 26953) */
#define XEEN_TEXT_NUMBER                  0x522cu /* "%u" (2692E: 26915, 2695F) */
#define XEEN_TEXT_AMOUNT_MILLIONS         0x521cu /* "%lu mil": a location's gold amount above 999,999 (2689A: 268BF) */
#define XEEN_TEXT_DIRECTION_SENSE_LINE    0x51fau /* "\2\f08\3c\v139\t116%c\fd\1": Direction Sense's letter (26721) */

/* Saving the game (game_save.c; the control panel's save 29502). */
#define XEEN_TEXT_SAVE_REFUSED 0x59c0u /* "...Sorry, no saving in this maze." (29522) */
#define XEEN_TEXT_SAVING       0x5a21u /* "Saving your game '%s'." (2953D) */
#define XEEN_TEXT_SAVED        0x5a3fu /* "Your Game '%s' has been saved. Press any key." (2968F) */

/* Startup's pointer sprites and the map's .hed file (game_startup.c, game_event_forms.c hed_cell). */
#define XEEN_TEXT_MOUSE_SPRITES  0x4720u /* "mouse.icn": the pointer's sprites (15F8E) */
#define XEEN_TEXT_MAP_HED_FORMAT 0x4effu /* "aaze%c%03u.hed" (1A257) */

/* The map load's case of files (game_resources.c map_case, Phase 294): 19F31's five cases load each with 7FF:0985,
 * the .dat into DS 00A2, the .mon at 40D4:0000, the picture file into DS 306D; and the map's text file (1A1B0). */
#define XEEN_TEXT_CLOUDS_ANIMATION_FILE   0x4e96u /* "clouds.dat" (19F8E, 19FC9, 1A006, 1A076) */
#define XEEN_TEXT_DARKSIDE_MONSTER_FILE   0x4ea1u /* "dark.mon" (19FA0, 1A018, 1A054) */
#define XEEN_TEXT_DARKSIDE_PICTURE_FILE   0x4eaau /* "darkpic.dat" (19FB0, 19FEB, 1A064) */
#define XEEN_TEXT_CLOUDS_MONSTER_FILE     0x4eb6u /* "xeen.mon" (19FDB, 1A088) */
#define XEEN_TEXT_CLOUDS_PICTURE_FILE     0x4ebfu /* "xeenpic.dat" (1A028, 1A098) */
#define XEEN_TEXT_DARKSIDE_ANIMATION_FILE 0x4ecbu /* "dark.dat" (1A042) */
#define XEEN_TEXT_MAP_TEXT_FILE_FORMAT    0x4ee3u /* "%s%c%03u.txt": the world prefix (DS 94: 9A or 9E), 'x' or '0' and
                                                   * the map, into F5E8, loaded into DS 8C53 (1A1DC) */

/* The map load's sprite and song files (sprites.c, Phase 293):the objects, monsters and wall items (1A84B, 1A929,
 * 1AB46), the outdoor branch (1ABC0..1AD26) and the indoor branch's style files (1AE4D..1AFAF). */
#define XEEN_TEXT_MAP_MOB_FILE            0x4ef0u /* "maze%c%03u.mob": the map's sprite ids (1A223, 1A809) */
#define XEEN_TEXT_OBJECT_085_FILE         0x4f0eu /* "085.obj": object 118 shown as 85 (1A8BE) */
#define XEEN_TEXT_OBJECT_SPRITE_FORMAT    0x4f16u /* "%03u.%cbj" (1A8FA) */
#define XEEN_TEXT_MONSTER_SPRITE_FORMAT   0x4f20u /* "%03u.mon" (1A983) */
#define XEEN_TEXT_MONSTER_ATTACK_FORMAT   0x4f29u /* "%03u.att" (1AA5F) */
#define XEEN_TEXT_WALL_ITEM_SPRITE_FORMAT 0x4f32u /* "%03u.pic" (1AB73) */
#define XEEN_TEXT_OUTDOOR_SONG            0x4f3bu /* "outdoors.m": the Clouds' outdoor song (1ABCD, 1ABE4) */
#define XEEN_TEXT_OUTDOOR_GROUND          0x4f46u /* "water.out" (1AC5B) */
#define XEEN_TEXT_OUTDOOR_TILES           0x4f50u /* "outdoor.til": the outdoor minimap tiles (1AC6C) */
#define XEEN_TEXT_OUTDOOR_WALL_FORMAT     0x4f5cu /* "%s.wal" of an outdoor wall name (1ACB1) */
#define XEEN_TEXT_STYLE_SKY_FORMAT        0x4f63u /* "%s.sky" of a style name (1AE52) */
#define XEEN_TEXT_STYLE_GROUND_FORMAT     0x4f6au /* "%s.gnd" (1AE85) */
#define XEEN_TEXT_STYLE_TILES_FORMAT      0x4f71u /* "%s.til" (1AEB8) */
#define XEEN_TEXT_STYLE_FRONT_WALL_FORMAT 0x4f78u /* "f%s1.fwl" (1AF3D) */
#define XEEN_TEXT_STYLE_SIDE_WALL_FORMAT  0x4f81u /* "s%s.swl" (1AFAF) */

/* A save's three state files (in a .wox, XEEN.CUR and DARK.CUR): the game's name record, the roster and the party.
 * Two copies each: the save's and the restore's (19278: 19296..192BA; 19953: 19980..199A2) and Start a New Game's
 * (2B4F5..2B519). */
#define XEEN_TEXT_STATE_NAME_FILE        0x4b99u /* "maze.nam" */
#define XEEN_TEXT_STATE_ROSTER_FILE      0x4ba2u /* "maze.chr" */
#define XEEN_TEXT_STATE_PARTY_FILE       0x4babu /* "maze.pty" */
#define XEEN_TEXT_NEWGAME_NAME_FILE      0x5fcfu /* "maze.nam" */
#define XEEN_TEXT_NEWGAME_ROSTER_FILE    0x5fd8u /* "maze.chr" */
#define XEEN_TEXT_NEWGAME_PARTY_FILE     0x5fe1u /* "maze.pty" */

/* The title menu, its credits and options, loading a game and the inn restart (game_menu.c, game_menu_load.c). */
#define XEEN_TEXT_LAUGH_VOICE            0x45f9u /* "laff1.voc" (also Darkside's death laugh, 15BAC) */
/* The exit sequence (game_exit.c): the character book's frames, the death's flames and their voices. */
#define XEEN_TEXT_BOOK_HAND_FRAMES       0x4603u /* "hand%02u.vga": the book's pages 0..15 (15685) */
#define XEEN_TEXT_BOOK_MARBLE_FRAMES     0x4610u /* "marb%02u.vga": its last four, 1..4 (156BB) */
#define XEEN_TEXT_DEATH_FIRE_VOICE       0x461du /* "fire.voc" (15997) */
#define XEEN_TEXT_DEATH_FIRE_1           0x4626u /* "fire1.vga" (159B2); fire2..4 follow at 4630, 463A, 4644 */
#define XEEN_TEXT_DEATH_FIRE_2           0x4630u /* "fire2.vga" (159C3) */
#define XEEN_TEXT_DEATH_FIRE_3           0x463au /* "fire3.vga" (159D4) */
#define XEEN_TEXT_DEATH_FIRE_4           0x4644u /* "fire4.vga" (159E5) */
#define XEEN_TEXT_DEATH_PICTURE          0x464eu /* "death.vga" (159F6) */
#define XEEN_TEXT_DEATH_PICTURE_2        0x4658u /* "death1.vga" (15A07) */
#define XEEN_TEXT_DEATH_LAUGH_CLOUDS     0x4663u /* "xeenlaff.voc": Clouds' death laugh (15BB6) */
#define XEEN_TEXT_FALL_SCREAM_VOICE      0x4f89u /* a fall's "scream.voc" (1BF7F) */
#define XEEN_TEXT_FALL_LANDING_VOICE     0x4f94u /* the landing's "unnh.voc" (1C03B) */
#define XEEN_TEXT_HIT_VOICE_FORMAT       0x6760u /* "pow%d.voc": a damaging hit's voice by power (36A0E) */
#define XEEN_TEXT_MONSTER_VOICE_FORMAT   0x676au /* "%s.voc": a monster's attack voice (3811B) */
#define XEEN_TEXT_SHOP_BUY_VOICE         0x6ccfu /* "choice2.voc": a purchase from the blacksmith's stock (3C321) */
#define XEEN_TEXT_LOAD_SPELL_NAMES       0x472au /* "spells.xen": the loaded game's resident spell names */
#define XEEN_TEXT_LOAD_MATERIAL_NAMES    0x4735u /* "MAE.xen" */
#define XEEN_TEXT_MISS_VOICE             0x473du /* "miss.voc" */
#define XEEN_TEXT_POW_VOICE              0x4746u /* "pow1.voc" */
#define XEEN_TEXT_LOAD_MAP_TEXTS         0x474fu /* "aaze0029.txt" */
#define XEEN_TEXT_MENU_TITLE_WORLD       0x4793u /* "Might and Magic Options / World of Xeen": both worlds installed */
#define XEEN_TEXT_MENU_TITLE_FORMAT      0x48c9u /* "Might and Magic Options / %s of Xeen": one world installed */
#define XEEN_TEXT_CREDITS_BACKGROUND     0x47f6u /* "marb.raw" */
#define XEEN_TEXT_CREDITS_TEXT           0x47ffu /* "credits.bin" */
#define XEEN_TEXT_MENU_SPECIAL_SPRITES   0x480bu /* "special.icn" */
#define XEEN_TEXT_MENU_ANIMATION_0       0x4817u /* "world0.int" */
#define XEEN_TEXT_MENU_ANIMATION_1       0x4822u /* "world1.int" */
#define XEEN_TEXT_MENU_ANIMATION_2       0x482du /* "world2.int" */
#define XEEN_TEXT_MENU_START_SPRITES     0x4838u /* "start.icn" */
#define XEEN_TEXT_MENU_PALETTE           0x4866u /* "dark.pal" */
#define XEEN_TEXT_MENU_BACKGROUND        0x486fu /* "world.raw" */
#define XEEN_TEXT_DARK_SAVES_PATTERN     0x489au /* "dark??.wox": the Darkside save slots */
#define XEEN_TEXT_NO_SAVED_GAMES         0x48a5u /* "There are no saved games to load!" */
#define XEEN_TEXT_EXIT_SONG              0x49cdu /* "twnnitea.m" */
#define XEEN_TEXT_CUR_FILE_FORMAT        0x4bb4u /* "%s.cur": a world's state archive */
#define XEEN_TEXT_SLOT_FILE_FORMAT       0x4bbbu /* "%s%02d.wox": a world's save slot */
#define XEEN_TEXT_MAIN_SPRITES           0x5893u /* "main.icn" */
#define XEEN_TEXT_FACE_FILE_FORMAT       0x5df2u /* "char%02d.fac" */
#define XEEN_TEXT_DARKSIDE_FACES         0x5dffu /* "dse.fac" */
#define XEEN_TEXT_DIALOG_SPRITES         0x5e07u /* "scroll.icn": the save and load dialog */
#define XEEN_TEXT_DARK_SLOT_FORMAT       0x5e12u /* "dark%02d.wox" */
#define XEEN_TEXT_DARK_SLOT_FORMAT_UPPER 0x5e37u /* "DARK%02d.WOX" */
#define XEEN_TEXT_DIALOG_SLOT_MARK       0x5e1fu /* the dialog's right-aligned "%c %u" slot mark */
#define XEEN_TEXT_DIALOG_MENU            0x5e44u /* "%s Game Menu": the dialog's ten slots */
#define XEEN_TEXT_DIALOG_EMPTY_SLOT      0x5f2bu /* "Empty": an unused slot's label (2AEFB..2B02A, one reader per slot) */
#define XEEN_TEXT_EMPTY_STRING           0x5855u /* "": the dialog's title without a selected game (2AEE6), and the
                                                  * name entry's (2892A); a name for the content, the roles differ */
#define XEEN_TEXT_THANK_YOU              0x607eu /* "Thank you for playing Might and Magic: %s of Xeen." */
#define XEEN_TEXT_INN_SPRITES            0x7ba8u /* "inn.icn" */
#define XEEN_TEXT_RESTART_MATERIAL_NAMES 0x7bb0u /* "MAE.xen": the inn restart's */

/* The character screen's details and the awards (game_details.c; the detail windows 429AE..43193, the awards 41609..). */
#define XEEN_TEXT_DETAILS_STAT            0x777eu /* "%s / Current / Maximum ... Rating: %s": a stat (429AE) */
#define XEEN_TEXT_DETAILS_AGE             0x77b9u /* "%s / Current / Natural ... Born: %u / %u" (42A07) */
#define XEEN_TEXT_DETAILS_LEVEL           0x77f5u /* "... %u Attack%s/Round": the level and attacks (42A80) */
#define XEEN_TEXT_DETAILS_HIT_POINTS      0x7835u /* "%s / Current / Maximum %d / %lu": a signed current (42AE7) */
#define XEEN_TEXT_DETAILS_CURRENT_MAXIMUM 0x7861u /* "%s / Current / Maximum %lu / %lu": AC and SP (42B36) */
#define XEEN_TEXT_DETAILS_RESISTANCES     0x788eu /* "%s / Fire %u / Cold %u / Electricity ..." (42C41) */
#define XEEN_TEXT_DETAILS_SKILL_VALUE     0x78fcu /* "\n\t020%s%u": the first skill (Thievery) with its value (42CA2) */
#define XEEN_TEXT_DETAILS_LIST_LINE       0x7906u /* "\n\t020%s": a skill's or a condition's line (42CCA, 42F67, 42FBF) */
#define XEEN_TEXT_DETAILS_NO_SKILLS       0x790eu /* "\n\t020None" (42D09) */
#define XEEN_TEXT_DETAILS_SKILLS_PAGE     0x7918u /* the title and eighteen skill lines (42DBD) */
#define XEEN_TEXT_DETAILS_ELIGIBLE        0x7944u /* "Eligible": experience enough for the next level (42E1A) */
#define XEEN_TEXT_DETAILS_EXPERIENCE      0x7952u /* "%s / Current: %lu / Next Level: %s" (42E62) */
#define XEEN_TEXT_DETAILS_PARTY_FUNDS     0x7985u /* "Party %s / %lu on hand / %lu in bank": gold or gems (42EB3) */
#define XEEN_TEXT_DETAILS_FOOD            0x79acu /* "Party %s / %u on hand / Enough for %u day%s" (42EF6) */
#define XEEN_TEXT_DETAILS_CONDITION_VALUE 0x79dau /* "\n\t020%s\t095-%d": a condition with its strength (42F3F) */
#define XEEN_TEXT_DETAILS_CONDITIONS_PAGE 0x7a41u /* the title and the condition lines (43117) */
#define XEEN_TEXT_EMPTY                    0x7cf6u /* "" (the terminator of "cast.icn"): a singular or an empty field
                                                    * (459DD, 466AD..466FB, 46DA4, 47642..476B7) */
#define XEEN_TEXT_PLURAL                   0x7e15u /* "s" (the tail of "\x03l%s"): a plural (459D6, 46DAB) */
#define XEEN_TEXT_DETAILS_PLURAL           0x777cu /* "s": the plural of "Attack" and "day" (no immediate reader
                                                    * found) */
#define XEEN_TEXT_DETAILS_EMPTY            0x777du /* "" (the "s"'s terminator): the singular, an empty line
                                                    * (42CE5, 42D20, 42F83, 42FD2) */
#define XEEN_TEXT_DETAILS_FONT_RESET       0x77b7u /* "\x01": only the normal-font control; a closed detail's window
                                                    * 28 gets no text (43193) */
#define XEEN_TEXT_AWARDS_FILE    0x769cu /* "award.bin": the award texts (41609) */
#define XEEN_TEXT_AWARDS_BUTTONS 0x76a6u /* the "Up", "Down" and "Exit" captions (41643) */
#define XEEN_TEXT_AWARDS_NONE              0x76cfu /* "\x03cNo Awards": the list's line when there are none (no
                                                    * immediate reader found) */
#define XEEN_TEXT_AWARDS_PAGE 0x76dbu /* "Awards for %s the %s" and nine award lines (418EF) */

/* The item dialog: a member's items and the shops (game_shop.c; 44F92's screen 4500D..45A7C). */
#define XEEN_TEXT_MATERIAL_NAMES_FILE   0x55a6u /* "MAE.xen": the item materials, loaded on a location's entry (278E2) */
#define XEEN_TEXT_ITEMS_SPRITES_FORMAT  0x7e56u /* "%s.icn" (4501A), with one of: */
#define XEEN_TEXT_ITEMS_OWN_SPRITES     0x7e5du /* "items": a member's own items (4500D) */
#define XEEN_TEXT_ITEMS_SHOP_SPRITES    0x7e63u /* "buy": a shop's (45014) */
#define XEEN_TEXT_ITEMS_EQUIP_SPRITES   0x7e67u /* "equip.icn" (45054, 505CF) */
#define XEEN_TEXT_ITEMS_BUTTONS         0x7e71u /* the "Weap", "Armor", "Acces", "Misc" buttons and four more (451F3) */
#define XEEN_TEXT_ITEMS_USE_BUTTON      0x7ed0u /* "Use": a misc item's first button (45236) */
#define XEEN_TEXT_ITEMS_SPELL_BUTTONS   0x7ed9u /* the buttons of the Enchant/Recharge/Gold dialogs (45268), with: */
#define XEEN_TEXT_ITEMS_ENCHANT_BUTTON  0x7f29u /* "Enchant" (4524E) */
#define XEEN_TEXT_ITEMS_RECHARGE_BUTTON 0x7f33u /* "Rechrg" (4525B) */
#define XEEN_TEXT_ITEMS_GOLD_BUTTON     0x7f3cu /* "Gold" (45262) */
#define XEEN_TEXT_ITEMS_LINE            0x7f43u /* "%02u %2d) %s": a numbered item line (45440, 4558F, 456D3) */
#define XEEN_TEXT_ITEMS_PRICED_LINE     0x7f5eu /* the same with a price (454B0, 455FF, 45743, 45867) */
#define XEEN_TEXT_ITEMS_NONE            0x7f82u /* "No items available." (45887) */
#define XEEN_TEXT_ITEMS_OWN_PAGE        0x7f9du /* "%s for %s the %s%s" and nine lines: a member's items (4595E) */
#define XEEN_TEXT_ITEMS_LEFT            0x7e53u /* "\x03l": the misc page's left-justified title (45951) */
#define XEEN_TEXT_ITEMS_CENTRED         0x7fd8u /* "\x03c": the other pages' centred title (45958) */
#define XEEN_TEXT_ITEMS_NO_HEADING      0x7fdbu /* " ": no column heading (the misc page has "Charges") (4591A) */
#define XEEN_TEXT_ITEMS_BUY_PAGE        0x7fddu /* "Available %s%s  Gold - %lu  Cost" and nine lines (459F1) */
#define XEEN_TEXT_ITEMS_SERVICE_PAGE    0x802eu /* "%s for %s  %s" and nine lines: sell, fix, identify ... (45A7C) */
#define XEEN_TEXT_ITEMS_CHARGES_HEADING 0x806au /* "Charges": Enchant's and Recharge's column (45A5B) */
#define XEEN_TEXT_ITEMS_COST_HEADING    0x8072u /* "Cost": the other services' column (45A62) */
#define XEEN_TEXT_SHOP_CURSED             0x80bbu /* "You cannot remove a cursed item!" (45F36; the item options have
                                                   * their own copy) */
#define XEEN_TEXT_NOT_PROFICIENT 0x8403u /* "%ss are not proficient with a %s!" (46F0C) */

/* A member's item options (game_item_options.c; 3B989..3C28C, the equip checks 42441/42460, payments 29239). */
#define XEEN_TEXT_PAY_NOT_ENOUGH        0x597au /* "Not enough %s in the %s!" (29239) */
#define XEEN_TEXT_PAY_NONE              0x599bu /* "No %s in the %s!" (29259) */
#define XEEN_TEXT_CAST_ENGAGED          0x6887u /* "Can't cast %s while engaged!" (39B28) */
#define XEEN_TEXT_ITEM_CURSED_DISCARD   0x6a62u /* "You cannot discard a cursed item!" (3B989) */
#define XEEN_TEXT_ITEM_KEEP_XEEN_SLAYER 0x6a86u /* "Don't you think you'll need the Xeen Slayer to kill Lord Xeen!" (3B9AC) */
#define XEEN_TEXT_ITEM_CURSED_REMOVE    0x6af3u /* "You cannot remove a cursed item!" (3BA87) */
#define XEEN_TEXT_ITEM_ESC_SPRITES      0x6b42u /* "esc.icn" (3BB63) */
#define XEEN_TEXT_ITEM_WHICH            0x6b4au /* "%s which item?" (3BBBF) */
#define XEEN_TEXT_ITEM_USE_HURRY        0x6b61u /* "What's your hurry? Wait till you get out of here!" (3BD03) */
#define XEEN_TEXT_ITEM_USE_IN_COMBAT    0x6b97u /* "To use an item in Combat, invoke the Use command on your turn!" (3BD5B) */
#define XEEN_TEXT_ITEM_NO_ABILITIES     0x6bdau /* "%s has no special abilities!" (3BE3C) */
#define XEEN_TEXT_ITEM_NOT_NEEDED       0x6c22u /* "We have no need of this %s!" (3BF3A) */
#define XEEN_TEXT_ITEM_NOT_BROKEN       0x6c48u /* "That item is not broken!" (3BFC7) */
#define XEEN_TEXT_ITEM_IDENTIFY         0x6c63u /* "Identify Item", the item and its description (3C216) */
#define XEEN_TEXT_ITEM_BACKPACK_FULL    0x6c8du /* "%s's backpack is full." (3C28C) */
#define XEEN_TEXT_EQUIP_ALL             0x771du /* "You have equipped all the %ss you can!" (42441) */
#define XEEN_TEXT_EQUIP_REMOVE_FIRST    0x7755u /* "You must remove %sto equip %s!" (42460) */
#define XEEN_TEXT_CAST_SPRITES          0x7ceeu /* "cast.icn" (4489E, 449AA) */

/* The game information screen, the M map's caption, Dismiss, QuickFight and the Quick Reference (game_combat.c). */
#define XEEN_TEXT_MAP_WINDOW_CLEAR      0x50aau /* "\r": clears the map's window 5 (1DB6C) */
#define XEEN_TEXT_MAP_CAPTION           0x50acu /* "%s  X = %d  Y = %d  %s": the map's name, position, facing (1E390) */
#define XEEN_TEXT_INFO_LIGHT            0x5ccau /* "Light %u" (29CDC) */
#define XEEN_TEXT_INFO_FIRE             0x5ce0u /* "%c%sFire%s%u": a protection's line (29D1E) */
#define XEEN_TEXT_INFO_ELECTRICITY      0x5cedu /* "%c%sElectricity%s%u" (29D60) */
#define XEEN_TEXT_INFO_COLD             0x5d01u /* "%c%sCold%s%u" (29DA2) */
#define XEEN_TEXT_INFO_POISON           0x5d0eu /* "%c%sPoison/Acid%s%u" (29DE4) */
#define XEEN_TEXT_INFO_CLAIRVOYANCE     0x5d22u /* "%c%sClairvoyance%s" (29E23) */
#define XEEN_TEXT_INFO_LEVITATE         0x5d35u /* "%c%sLevitate%s" (29E62) */
#define XEEN_TEXT_INFO_WALK_ON_WATER    0x5d44u /* "%c%sWalk on Water" (29E98) */
#define XEEN_TEXT_INFO_EFFECTS          0x5d56u /* the eight effect lines joined (29ED6) */
#define XEEN_TEXT_INFO_PAGE             0x5d67u /* "%s of Xeen / Game Information / Today is %ssday / Time ..." (29F92) */
#define XEEN_TEXT_INFO_CLOUDS_TITLE     0x5de3u /* "World": the title's word with the Clouds installed (29F85) */
#define XEEN_TEXT_INFO_DARKSIDE_TITLE   0x5de9u /* "Darkside" (29F8C) */
#define XEEN_TEXT_DISMISS_SPRITES       0x5feau /* "esc.icn" (2B767) */
#define XEEN_TEXT_DISMISS_WHOM          0x5ff2u /* "Dismiss whom?" (2B7CC) */
#define XEEN_TEXT_DISMISS_WHOM_LEFT     0x6009u /* the same, left-justified (2B810) */
#define XEEN_TEXT_DISMISS_LAST          0x6022u /* "You cannot dismiss your last character!" (2B89A) */
#define XEEN_TEXT_QUICKFIGHT_SPRITES    0x7166u /* "train.icn" (3E9C1) */
#define XEEN_TEXT_QUICKFIGHT_OPTIONS    0x7170u /* "QuickFight Options" with the current choice and the four keys (3EA34) */
#define XEEN_TEXT_QUICK_REFERENCE_LINE  0x82f1u /* a member's line: name, class, level, HP, SP, AC (46D55) */
#define XEEN_TEXT_QUICK_REFERENCE_CHART 0x8358u /* "Quick Reference Chart" and its headings (46DF1) */

/* The guilds and the spell lists they share with Cast (game_guild.c; the lists 3ECCC..3F36A, the guild 3F10F..3F5EC). */
#define XEEN_TEXT_GUILD_MEMBERS_ONLY     0x54e7u /* "You have to be a member to shop here." (27722) */
#define XEEN_TEXT_SPELL_NAMES_FILE       0x559bu /* "spells.xen": the spell names, loaded on a location's entry (27872) */
#define XEEN_TEXT_GUILD_OPTIONS          0x6ed0u /* "%s / Buy Spells / Spell Info": the member line and the keys (3F10F) */
#define XEEN_TEXT_SPELL_LIST_NO_NUMBERS  0x6ecfu /* "": an empty list's number column (3F20F) */
#define XEEN_TEXT_SPELL_LIST_NUMBERS     0x7263u /* "1 .. 9, 0": the list's number column (3F209) */
#define XEEN_TEXT_SPELL_LIST_PRICE_LINE  0x71c6u /* "\x03l%s\x03r\t000%u": a spell (left) and its price
                                                  * (right-justified) (3ECCC, 3EDCC, 3EEB2) */
#define XEEN_TEXT_SPELL_LIST_ALL_LEARNED 0x71d3u /* "You have learned all we can teach you." (3EF0B) */
#define XEEN_TEXT_SPELL_LIST_NOT_CASTER  0x71feu /* "Not a spell caster..." (3EF34) */
#define XEEN_TEXT_SPELL_LIST_COST_LINE   0x7214u /* "%s %u/%u": a known spell, its SP and gems (3EFE1) */
#define XEEN_TEXT_SPELL_LIST_NONE_KNOWN  0x7224u /* "You don't know any spells..." (3F029) */
#define XEEN_TEXT_GUILD_SPRITES          0x7241u /* "scroll.icn" (3F069) */
#define XEEN_TEXT_SPELL_LIST_TITLE       0x7282u /* "Spells for %s", with the number column (3F229) */
#define XEEN_TEXT_SPELL_LIST_PAGE        0x72a0u /* the ten coloured spell lines (3F36A) */
#define XEEN_TEXT_GUILD_SPELL_INFO       0x735cu /* "%s / %s / Press a Key!": a spell's name and description (3F5B7) */
#define XEEN_TEXT_GUILD_PURCHASE         0x7382u /* "%s  Do you wish to purchase %s for %u?" (3F5EC) */

/* The save's label (game_save.c; 19B37..19CB4: the clock in 12-hour form and the playing time) and its reloads. */
#define XEEN_TEXT_SAVE_TIME_AM          0x4bc6u /* "a": the clock's suffix before noon (19B58) */
#define XEEN_TEXT_SAVE_TIME_TWELVE      0x4bc9u /* "12": the hour shown for 0 and 12 (19B6C, 19B7B) */
#define XEEN_TEXT_SAVE_TIME_HOUR        0x4bccu /* "%02d": an afternoon hour (19B8E) */
#define XEEN_TEXT_SAVE_TIME_PM          0x4bd1u /* "p" (19B9E) */
#define XEEN_TEXT_SAVE_PLAY_TIME        0x4bd4u /* "%02u:%02u:%02u": the hours, minutes and seconds played (19C12) */
#define XEEN_TEXT_SAVE_TIME_LINE        0x4be3u /* the clock, left, and the playing time, right (19C9B) */
#define XEEN_TEXT_RELOAD_EVENTS_FILE    0x60c3u /* "maze%c%03u.evt": the party's map's events (2C8A4, 30F7F) */
#define XEEN_TEXT_RELOAD_MAP_TEXTS_FILE 0x60d2u /* "aaze%c%03u.txt": its texts (2C8D9) */

/* A location's entry (game_location.c; 27953..2802D). */
#define XEEN_TEXT_LOCATION_HELLO_VOICE    0x55aeu /* "hello1.voc" (27953, 27B88) */
#define XEEN_TEXT_TRAINING_SPRITES        0x55b9u /* "train.icn" (2798E) */
#define XEEN_TEXT_LOCATION_ESC_SPRITES    0x55dcu /* "esc.icn" (27A15, 27A92, 27B97) */
#define XEEN_TEXT_TAVERN_SPRITES          0x560fu /* "tavern.icn" (27B09) */
#define XEEN_TEXT_TAVERN_TEXTS_FILE       0x561au /* "tavern.bin" (27B1A) */
#define XEEN_TEXT_SPELL_DESCRIPTIONS_FILE 0x562fu /* "spldesc.bin" (27BA8) */
#define XEEN_TEXT_BANK_SPRITES            0x5653u /* "bank.icn" (27C38) */
#define XEEN_TEXT_BANK_SPRITES_2          0x565cu /* "bank2.icn" (27C46) */
#define XEEN_TEXT_LOCATION_TOWN_FILE      0x568au /* "%s1.twn": the location's town animation (27DA9) */
#define XEEN_TEXT_LOCATION_COME_VOICE     0x56aau /* "come1.voc" (2802D) */

/* The character view (game_view.c; 44AFF..47A10). */
#define XEEN_TEXT_VIEW_TEXTS_FILE         0x7cf7u /* "viewtext.bin" (44AFF) */
#define XEEN_TEXT_VIEW_SPRITES            0x7d04u /* "view.icn" (44B5D, 44DE0) */
#define XEEN_TEXT_VIEW_HEADINGS           0x7d0du /* "Mgt Acy H.P. Experience / Int Lck S.P. ...", around the stats (44BD1) */
#define XEEN_TEXT_VIEW_NO_COMBAT_EXCHANGE 0x7e17u /* "Exchanging in combat is not allowed!" (44E0C) */
#define XEEN_TEXT_VIEW_STATS              0x818au /* the member's name, race, class and every stat (46B53) */
#define XEEN_TEXT_VIEW_PARTY_GOLD         0x82e1u /* "Party Gold" (46B4F) */
#define XEEN_TEXT_VIEW_EFFECT_MARK        0x82ecu /* "\x0c14+": a mark per active Bless, Power Shield, Holy Bonus and
                                                   * Heroism after a good condition (466A6, 466C0, 466DA) */
#define XEEN_TEXT_VIEW_EXCHANGE_WHOM 0x84c4u /* "Exchange with whom?" (47A10) */

/* The inn's roster (game_inn.c; 43850..44523). */
#define XEEN_TEXT_INN_IN_PARTY         0x7a72u /* "In Party": a party member's roster mark (43850) */
#define XEEN_TEXT_INN_ROSTER           0x7a81u /* the roster's columns of names (43A36) */
#define XEEN_TEXT_INN_ESC_SPRITES      0x7b29u /* "esc.icn" (43A5F) */
#define XEEN_TEXT_INN_WHICH_CHARACTER  0x7b31u /* "%s which character?" (43A78) */
#define XEEN_TEXT_INN_BACKGROUND       0x7bc9u /* "back.raw" (43F66) */
#define XEEN_TEXT_INN_BUTTONS          0x7bd2u /* "Up Down Del Rem Create ..." (43F92, 4410E) */
#define XEEN_TEXT_INN_KEEP_XEEN_SLAYER 0x7c27u /* "This character has the Xeen Slayer Sword and cannot be deleted!" (4426C) */
#define XEEN_TEXT_INN_DELETE_CONFIRM   0x7c6fu /* "Are you sure you want to delete %s the %s?" (442A1) */
#define XEEN_TEXT_INN_NO_ONE           0x7cafu /* "You have no one to adventure with" (4440D) */
#define XEEN_TEXT_INN_PARTY_FULL       0x7cd1u /* "Your party is full!" (44523) */

/* The bank and the text/number entries (game_bank.c, game_name_entry.c). */
#define XEEN_TEXT_BANK_BUTTONS     0x550fu /* "Dep", "With", "ESC" (277AF) */
#define XEEN_TEXT_BANK_COIN_VOICE  0x5139u /* "coina.voc" (2580B, 26FA2) */
#define XEEN_TEXT_BANK_FUNDS       0x5239u /* the title, gold and gems and the keys: the window's first text (26F13) */
#define XEEN_TEXT_BANK_FUNDS_AFTER 0x52a2u /* the same after an amount, without the keys' redraw (27149) */
#define XEEN_TEXT_BANK_AMOUNT      0x528eu /* "Amount" (2701D) */
#define XEEN_TEXT_ENTRY_FIELD      0x5871u /* "\x03l\t000\x04%03d\x03c": an entry's field at its width (2896A, 28AE8) */
#define XEEN_TEXT_ENTRY_FIELD_TEXT 0x587fu /* the same with the text typed so far (28A32, 28B3B) */
#define XEEN_TEXT_ENTRY_TITLE      0x613eu /* "\r\x03c%s": an entry's centred prompt (2E905, 2E923) */
#define XEEN_TEXT_MIRROR_FILE      0x614cu /* "%smirr.txt": the mirror's destinations (2E9D9) */
#define XEEN_TEXT_PASSWORD_PROMPT  0x637au /* "What's the Password?" (328E9) */

/* Casting (game_cast.c, game_cast_attack.c, game_cast_party.c, game_cast_travel.c). */
#define XEEN_TEXT_CAST_DIALOG_SPRITES      0x6e0eu /* "cast.icn": the Cast dialog's (3D939; the item dialog has its own copy) */
#define XEEN_TEXT_CAST_NOT_ENOUGH          0x68aau /* "Not enough %s to Cast %s" (39BBF) */
#define XEEN_TEXT_CAST_ELEMENT_SPRITES     0x67d3u /* "element.icn" (3982F) */
#define XEEN_TEXT_CAST_WHICH_ELEMENT       0x67dfu /* "Which Element?" and the four keys (39887) */
#define XEEN_TEXT_CAST_WHICH_ELEMENT_AGAIN 0x682cu /* the same without the font reset, after the choice's scene (39901) */
#define XEEN_TEXT_CAST_ON_WHOM             0x6878u /* "On Whom?" (39A2E) */
#define XEEN_TEXT_TELEPORT_PROMPT          0x69cdu /* "Teleport / How many squares %s (1-9)" (3ACB8) */
#define XEEN_TEXT_TOWN_PORTAL_TEXTS_FILE   0x69f3u /* "%s%04u.txt": the town names' map texts (3AFFC) */
#define XEEN_TEXT_TOWN_PORTAL_MENU         0x69feu /* "Town Portal" and the five towns (3B04F) */
#define XEEN_TEXT_BEACON_TEXTS_FILE        0x6925u /* "%s%c%03u.txt": the beacon map's texts, for its name (3A68F) */
#define XEEN_TEXT_BEACON_MENU              0x6932u /* "Lloyd's Beacon / Last Location / %s / x = .. y = .." (3A721) */

/* Character creation (game_creation.c; 3FA2E..443FB). */
#define XEEN_TEXT_CREATION_CLASS_LINE   0x743cu /* "\t062\x0b168%s": an available class's line (40485) */
#define XEEN_TEXT_CREATION_ATTRIBUTES   0x7447u /* "Race: %s / Sex: %s / Class: %s" and the stats (405D2) */
#define XEEN_TEXT_CREATION_BUTTONS      0x75cdu /* "Roll", "Create" ... (4084F) */
#define XEEN_TEXT_CREATION_DICE         0x75aeu /* "dice.vga" (40644) */
#define XEEN_TEXT_CREATION_SPRITES      0x75b7u /* "create.icn" (40710) */
#define XEEN_TEXT_CREATION_BACKGROUND   0x75c2u /* "create.raw" (40763) */
#define XEEN_TEXT_CREATION_EXCHANGE     0x73deu /* "Exchange %s with..." (3FA2E) */
#define XEEN_TEXT_CREATION_SELECT_CLASS 0x73f2u /* "Select a Class before saving." (3FF2B) */
#define XEEN_TEXT_ROSTER_FULL           0x7c9au /* "Your Roster is full!" (443FB) */

/* The talking cutscenes and their subtitles (game_cutscene.c and the four game_cut_*.c units). */
#define XEEN_TEXT_CUTSCENE_TEXTS_FILE 0x6550u /* "special.bin" (33FEB, 34410, 34FC9) */
#define XEEN_TEXT_CUTSCENE_BOX        0x6567u /* "box.vga": the subtitle box (3400F, 3447F, 35198) */
#define XEEN_TEXT_CUTSCENE_BACKGROUND 0x66a5u /* "back.raw" (36068) */
#define XEEN_TEXT_SUBTITLE_LINE       0x4b6au /* "\x0c35\x03c\x0b190\t000%s": a subtitle's visible part (18649) */
#define XEEN_TEXT_GATE_DWARF_VOICE_1  0x668du /* "dwarf11.voc" (35F05) */
#define XEEN_TEXT_GATE_DWARF_VOICE_2  0x6699u /* "dwarf12.voc" (35F21) */
#define XEEN_TEXT_GUARD_ANIMATION     0x65b0u /* "dung3.zom" (34469) */
#define XEEN_TEXT_GUARD_WHAT_VOICE    0x65bau /* "what2.voc" (345CF) */
#define XEEN_TEXT_GUARD_OGRE_VOICE    0x65f4u /* "ogre.voc" (3463B) */
#define XEEN_TEXT_REAPER_VOICE        0x660eu /* "howdid1.voc" (351B6) */
#define XEEN_TEXT_SPHINX_SPRITES      0x655cu /* "sphinx.vga" (33FF9) */
#define XEEN_TEXT_SPHINX_VOICE        0x65a3u /* "sphinx12.voc" (341CF) */

/* The game's start, its end and the event dialogs (game_startup.c, game_ending.c, game_event_*.c, game_events.c). */
#define XEEN_TEXT_STARTUP_SONG         0x3157u /* "newbrigh.m" (15FC3) */
#define XEEN_TEXT_MOVIE_PLAYER         0x475cu /* "xeen.exe" (1623E) */
#define XEEN_TEXT_ENDING_COMMAND       0x6398u /* "XEENCOMMAND=%s,%010lu,%d": the ending's hand-off (3364B) */
#define XEEN_TEXT_CONFIRM_SPRITES      0x621bu /* "confirm.icn" (2F32D, 3005F) */
#define XEEN_TEXT_FACE_SPRITES_FORMAT  0x6261u /* an NPC's face file: "face%02u.fac" (300AD, 30564) */
#define XEEN_TEXT_FACE_FRAME_SPRITES   0x626eu /* the face's frame: "frame.fac" (300D7, 3058E) */
#define XEEN_TEXT_CHOOSE_SPRITES       0x6227u /* "choose.icn" (2F5D5) */
#define XEEN_TEXT_EVENT_MAP_TEXTS_FILE 0x6300u /* "aaze2%03u.txt": the map text window's file (3163A) */
#define XEEN_TEXT_HIT_A_KEY            0x61fcu /* "Hit a key": a dialog's last line (2F14B) */
#define XEEN_TEXT_PARTY_FOUND          0x6157u /* the treasure's "The Party Found:" with its gold and gems (2EBF4;
                                                * treasure.c keeps its own copy for DS states without the program
                                                * data) */
#define XEEN_TEXT_NPC_PAGE_LAYOUT      0x6278u /* the first NPC page's text: the name centred at column 125, then the
                                                * text from line 54: "\r\v014\x03c\t125%s\t000\v054%s" (3011F) */
#define XEEN_TEXT_SPEAKER_NAME_LAYOUT  0x6290u /* the speaker's name centred in the speech window from line 56:
                                                * "\r\x01\v056\x03c%s" (30151) */

/* The town services' option screens and sounds (game_location_frames.c, game_town.c, game_tavern.c). */
#define XEEN_TEXT_STORE_OPTIONS        0x53acu /* "Store Options for %s" (27309) */
#define XEEN_TEXT_TAVERN_OPTIONS       0x54a7u /* "Drink", "Food", "Tip" ... (276D4) */
#define XEEN_TEXT_LOCATION_SPIT_VOICE  0x522fu /* "spit1.voc" (26E2F) */
#define XEEN_TEXT_TAVERN_LINE          0x51bfu /* "\x03c\x0b012%s": a tavern text's line (25E6A) */
#define XEEN_TEXT_TRAINING_NEEDS       0x52e9u /* "%s needs %lu experience for level %u." (27241) */
#define XEEN_TEXT_TRAINING_ALL_LEARNED 0x530fu /* "%s has learned all we can teach!" (27258) */
#define XEEN_TEXT_TRAINING_ELIGIBLE    0x5330u /* "%s is eligible for level %d. / Cost %lu" (272B0) */
#define XEEN_TEXT_TRAINING_OPTIONS     0x5361u /* "Training Options / %s / Gold %s" (272DA) */
#define XEEN_TEXT_TEMPLE_OPTIONS       0x540eu /* "Temple Options for %s" (276AB) */

/* The menus, the control panel and the files (game_newgame.c, game_panel.c, game_pairs.c). */
#define XEEN_TEXT_PLEASE_WAIT          0x5856u /* "Please Wait..." (28862) */
#define XEEN_TEXT_SAVE_OVER            0x5f31u /* "Ok to save over %s?" (2B2BB) */
#define XEEN_TEXT_PREFERENCE_SPRITES   0x5fa3u /* "choice.icn" (2B393) */
#define XEEN_TEXT_PREFERENCE_TITLE     0x5faeu /* "Select Game Preference" (2B3EB) */
#define XEEN_TEXT_CREATING_FILES       0x4ae9u /* "Creating World of Xeen files. Please Wait..." (1833B) */
#define XEEN_TEXT_FILE_INVALID         0x4b1cu /* "World of Xeen File: %s (xeen%02u.wox) is invaild!" (18525) */
#define XEEN_TEXT_PANEL_SETTINGS       0x5a88u /* the sound, music and text settings and the save/load keys (297C9) */
#define XEEN_TEXT_PANEL_TITLE          0x5ae9u /* "Control Panel" and its keys (2988B) */
#define XEEN_TEXT_PANEL_CLEAR          0x5bc6u /* "\r": clears the panel's window (298E1, 2B070, 2B13A) */
#define XEEN_TEXT_PANEL_HOME           0x5bbcu /* "\x0b000\t000\x01": home, normal font (298B9, 2990B) */
#define XEEN_TEXT_PANEL_SPRITES        0x5adeu /* "cpanel.icn" (297FD) */
#define XEEN_TEXT_PANEL_NO_LOADING     0x5c2du /* "No Loading Allowed in Combat!" (29A97) */
#define XEEN_TEXT_PANEL_QUIT_CONFIRM   0x5c73u /* "Are you sure you want to quit?" (29B3C) */
#define XEEN_TEXT_PANEL_WIZARD_CONFIRM 0x5c92u /* "Are you sure you want Mr. Wizard's Help?" (29B68) */
#define XEEN_TEXT_PANEL_NO_SAVING      0x5c56u /* "No Saving Allowed in Combat!" (29B15) */

/* The quest screen (game_quests.c; 3E25B..3E890). */
#define XEEN_TEXT_QUESTS_SPRITES      0x6f95u /* "quest.icn" (3E25B) */
#define XEEN_TEXT_QUESTS_NOTES_FILE   0x6f9fu /* "qnotes.bin" (3E2D7) */
#define XEEN_TEXT_QUESTS_BUTTONS      0x6faau /* "Items", "Quests", "Notes" ... (3E316) */
#define XEEN_TEXT_QUEST_ITEM_COUNT    0x7003u /* "%d %s%c": a quest item with its count (3E461) */
#define XEEN_TEXT_QUESTS_NO_ITEMS     0x700bu /* "Quest Items / No Quest Items" (3E4C9) */
#define XEEN_TEXT_QUESTS_ITEMS_PAGE   0x703au /* "Quest Items" and its lines (3E587) */
#define XEEN_TEXT_QUESTS_CURRENT_PAGE 0x70d8u /* "Current Quests" and its lines (3E6BA) */
#define XEEN_TEXT_QUESTS_NOTES_PAGE   0x7113u /* "Auto Notes" and its lines (3E870) */
#define XEEN_TEXT_QUESTS_HOME         0x715cu /* "\x0b000\t000\x01": home, normal font (3E890) */

/* The treasure dialog (game_treasure.c; 2EC41..2F0F7). */
#define XEEN_TEXT_BACKPACKS_FULL      0x6183u /* "Warning!  BackPacks Full! / Press a Key" (2EC41) */
#define XEEN_TEXT_TREASURE_ITEM_LINES 0x61bdu /* the dialog's six item lines (2F0BF) */
#define XEEN_TEXT_TREASURE_FOUND      0x61e5u /* "%s found: %s" (2F0F7) */

/* The character screen's active effects (game_details.c; the character_details add-on reads the same table). */
#define XEEN_TEXT_AWARDS_SPRITES       0x7692u /* "award.icn" (4158D) */
#define XEEN_TEXT_DETAILS_BLESSED      0x79e9u /* an active effect's line: "/ Blessed095%+d" (43024) */
#define XEEN_TEXT_DETAILS_POWER_SHIELD 0x79fdu /* "/ Power Shield095%+d" (43044) */
#define XEEN_TEXT_DETAILS_HOLY_BONUS   0x7a16u /* "/ Holy Bonus095%+d" (43063) */
#define XEEN_TEXT_DETAILS_HEROISM      0x7a2du /* "/ Heroism095%+d" (43082) */

/* An item's name (items.c xeen_item_text_format): one format per kind of item and the words they take. The readers
 * are 475F7's four branches (weapons, armour, accessories, misc items). */
#define XEEN_TEXT_ITEM_WEAPON_FORMAT   0x845cu /* a weapon: colour, material, "broken", "cursed", colour, name, power,
                                                * tail: "{0C}%02u%s%s%s{0C}%02u%s%s%s" (476E9) */
#define XEEN_TEXT_ITEM_BROKEN          0x8473u /* an item name's prefix: "32broken" (476B0, 4775D, 47818, 47925) */
#define XEEN_TEXT_ITEM_CURSED          0x847eu /* "09cursed" (4769C, 47749, 47804, 47911) */
#define XEEN_TEXT_ITEM_BACK_UP         0x8489u /* the tail of a name with nothing after it: 08 20, which the text engine
                                                * draws as a step back of 2 (text_window.c), taking back the name's
                                                * trailing space; three opaque program-data bytes, not a text entry
                                                * (4763B, 47716, 477D1, 4788C) */
#define XEEN_TEXT_ITEM_ARMOR_FORMAT    0x848cu /* armour and accessories: colour, material, "broken", "cursed", colour,
                                                * name, tail: "{0C}%02u%s%s%s{0C}%02u%s%s" (47796, 47851) */
#define XEEN_TEXT_ITEM_MISC_FORMAT     0x84a1u /* a misc item: colour, "broken", "cursed", colour, its kind, "of", its
                                                * power, tail: "{0C}%02u%s%s{0C}%02u%s%s%s%s" (47933) */
#define XEEN_TEXT_ITEM_OF              0x84b8u /* before an item's special power: "of" (478E5) */

/* The combat window (combat.c). */
#define XEEN_TEXT_COMBAT_SPRITES       0x67b8u /* "combat.icn" (38F8B) */
#define XEEN_TEXT_COMBAT_MONSTER_LINE_1 0x6771u /* a monster's line in the combat window: "/ %2u%sd" (38C0C) */
#define XEEN_TEXT_COMBAT_MONSTER_LINE_2 0x677fu /* "/ %2u%sd" (38C66) */
#define XEEN_TEXT_COMBAT_MONSTER_LINE_3 0x678du /* "/ %2u%sd" (38CC0) */
#define XEEN_TEXT_COMBAT_WINDOW         0x679bu /* the combat window: the title and the three monster lines: "00c000000Combat%s%s%s" (38D14) */

/* Events, the fall and Rest. */
#define XEEN_TEXT_FALL_OOPS       0x4e84u /* the fall's message: "c000002Oops..." (19E9E) */
#define XEEN_TEXT_YES_NO_SPRITES  0x59b4u /* the yes/no box's "confirm.icn" (29383; the event yes/no's own copy is
                                           * XEEN_TEXT_CONFIRM_SPRITES) */
#define XEEN_TEXT_REST_CONFIRM    0x58b8u /* "Some Chars may die. Rest anyway?" (28E9A) */
#define XEEN_TEXT_REST_DREAM_SCREEN 0x58e2u /* the dream's picture: "scene1.raw" in intro.cc (28F6B) */
#define XEEN_TEXT_REST_DREAM_VOICE  0x58edu /* "dreams2.voc" (28F85) */
#define XEEN_TEXT_REST_DREAM_LAUGH  0x58f9u /* "laff1.voc" (28FDE) */
#define XEEN_TEXT_REST_DONE       0x5903u /* "8 hours pass.  Rest complete." with its two lines: "%s", "%d food
                                           * consumed." (29142) */
#define XEEN_TEXT_REST_STARVING   0x593eu /* the rest's middle line: "The Party is Starving!" (29135) */
#define XEEN_TEXT_REST_RESTORED   0x595au /* "Hit Pts and Spell Pts restored." (2913C) */
#define XEEN_TEXT_LOCK_PICKED     0x60e1u /* "c010%s picks the lock! / Press any key." (2DFF4) */
#define XEEN_TEXT_LOCK_NOT_PICKED 0x6109u /* "c010%s was unable to pick the lock! / Press any key." (2E02A) */
#define XEEN_TEXT_NOTHING_HERE    0x63b1u /* "c010Nothing here." (337FB) */
/* The event interpreter's sprintf (14BBA) formats (event_exec.c event_format, Phases 295 and 296): each pushed by its
 * handlers as the format of the record's text (A827). */
#define XEEN_TEXT_EVENT_CENTRED_FORMAT   0x62fau /* ops 1, 41, 53: "\r\3c%s" of A827 (315F9, 31713; Phase 296) */
#define XEEN_TEXT_EVENT_TWO_LINES_FORMAT 0x6314u /* op 49: "\r\3c\t000\v007%s\n\n%s" of A827 and A43F (31837) */
#define XEEN_TEXT_EVENT_SIGN_FORMAT_2    0x6326u /* op 2: "\2\f08\3c\t116\v025%s\3l\fd\1" (3188D) */
#define XEEN_TEXT_EVENT_SIGN_FORMAT_3    0x633cu /* op 3: "\f04\3c\t116\v030%s\3l\fd" (31897) */
#define XEEN_TEXT_EVENT_SIGN_FORMAT_4    0x6350u /* op 4: "\f08\3c\t120\v088%s\3l\fd" (318A1) */
#define XEEN_TEXT_EVENT_SIGN_FORMAT_39   0x6364u /* op 39: "\2\f08\3c\t116\v090%s\3l\fd\1" (318AB) */

/* The travel spells (game_cast_travel.c). */
#define XEEN_TEXT_BEACON_SPRITES      0x691au /* "lloyds.icn" (3A5F4, 3A882) */
#define XEEN_TEXT_TRAVEL_CAST_SPRITES 0x6998u /* the travel spells': "cast.icn" (3A876, 3B0DF) */

/* Character creation and the name entries. */
#define XEEN_TEXT_ENTRY_GAME_NAME           0x4772u /* "cEnter a Name for this Gamel /  /" (1634B) */
#define XEEN_TEXT_CREATION_EXCHANGE_SPRITES 0x73d2u /* "create2.icn" (3F9CD) */
#define XEEN_TEXT_ENTRY_CHARACTER_NAME      0x7418u /* "cEnter a Name for this Character /  /" (3FF50) */

/* The talking cutscenes' voices, chosen by whether the party holds the scene's key (cut_key). */
#define XEEN_TEXT_SPHINX_VOICE_10       0x656fu /* with the key: "sphinx10.voc" (34094) */
#define XEEN_TEXT_SPHINX_VOICE_13       0x657cu /* without it: "sphinx13.voc" (340BA) */
#define XEEN_TEXT_SPHINX_VOICE_11       0x6589u /* with the key: "sphinx11.voc" (341A0) */
#define XEEN_TEXT_SPHINX_VOICE_14       0x6596u /* without it: "sphinx14.voc" (341AA) */
#define XEEN_TEXT_GUARD_GOLEM_VOICE_15  0x65c4u /* with the key: "golem15.voc" (345EF) */
#define XEEN_TEXT_GUARD_GOLEM_VOICE_16  0x65d0u /* with the key: "golem16.voc" (34602) */
#define XEEN_TEXT_GUARD_GOLEM_VOICE_13  0x65dcu /* without it: "golem13.voc" (3460C) */
#define XEEN_TEXT_GUARD_GOLEM_VOICE_14  0x65e8u /* without it: "golem14.voc" (3461F) */
#define XEEN_TEXT_GUARD_GO_VOICE        0x65fdu /* with the key: "go2.voc" (34BC5) */
#define XEEN_TEXT_GUARD_KEY_VOICE       0x6605u /* without it: "key2.voc" (34BCF) */
#define XEEN_TEXT_REAPER_VOICE_12       0x661au /* with the key: "reaper12.voc" (351D6) */
#define XEEN_TEXT_REAPER_VOICE_14       0x6627u /* without it: "reaper14.voc" (351FC) */
#define XEEN_TEXT_REAPER_GOIN_VOICE     0x6634u /* "goin1.voc" (3566B) */
#define XEEN_TEXT_REAPER_VOICE_13       0x663eu /* "reaper13.voc" (35675) */
#define XEEN_TEXT_REAPER_NEED_KEY_VOICE 0x664bu /* "needkey1.voc" (35686) */
#define XEEN_TEXT_REAPER_VOICE_15       0x6658u /* "reaper15.voc" (35690, 5AB2E, 5ABD6) */
#define XEEN_TEXT_GATE_DWARF_VOICE      0x6665u /* "dwarf10.voc" (35B77) */
#define XEEN_TEXT_GATE_PASS_VOICE       0x6671u /* "pass2.voc" (35B81) */
#define XEEN_TEXT_GATE_OK_VOICE         0x667bu /* with the key: "ok2.voc" (35EE4) */
#define XEEN_TEXT_GATE_BACK_VOICE       0x6683u /* without it: "back2.voc" (35EEE) */

/* The guilds and the spell lists (Darkside's voice or the Clouds'). */
#define XEEN_TEXT_GUILD_MEMBER_KEY_VOICE_DARKSIDE 0x51e3u /* a non-member chosen with F1..F6: "skull1.voc" (261F9) */
#define XEEN_TEXT_GUILD_MEMBER_KEY_VOICE_CLOUDS   0x51eeu /* "guild11.voc" (26201) */
#define XEEN_TEXT_GUILD_NOT_MEMBER_VOICE_DARKSIDE 0x724cu /* a non-member at the guild's list: "skull1.voc" (3F1A7) */
#define XEEN_TEXT_GUILD_NOT_MEMBER_VOICE_CLOUDS   0x7257u /* "guild11.voc" (3F1AE) */
#define XEEN_TEXT_SPELL_LIST_SP_HEADING           0x734du /* the Cast list's column: "Spell Pts" (3F268) */
#define XEEN_TEXT_SPELL_LIST_GOLD_HEADING         0x7357u /* the guild's: "Gold" (3F26F) */
#define XEEN_TEXT_GUILD_PURCHASE_VOICE_DARKSIDE   0x73bau /* a spell bought: "parrot2.voc" (3F658) */
#define XEEN_TEXT_GUILD_PURCHASE_VOICE_CLOUDS     0x73c6u /* "guild12.voc" (3F65F) */

/* The inn. */
#define XEEN_TEXT_INN_SONG_DARKSIDE 0x7bb8u /* "newbrigh.m" (43E20) */
#define XEEN_TEXT_INN_SONG_CLOUDS   0x7bc3u /* "inn.m" (43E27) */

/* The item options and their confirmations. */
#define XEEN_TEXT_OPTION_DISCARD_CONFIRM  0x6acfu /* "000000lPermanently discard %sd?" (3BA06) */
#define XEEN_TEXT_OPTION_NOT_ENCHANTABLE  0x6b16u /* "012000cNot Enchantable.  %s" (no immediate reader found) */
#define XEEN_TEXT_OPTION_SPELL_FAILED     0x6b35u /* a refused item spell's line: "012000c%s" (no immediate reader found) */
#define XEEN_TEXT_OPTION_SELL_CONFIRM     0x6bfeu /* "l000000dSell %sd for %lu gold?" (3BED5) */
#define XEEN_TEXT_OPTION_BUY_CONFIRM      0x6cacu /* "l000000dBuy %sd for %lu gold?" (3C2C8) */
#define XEEN_TEXT_OPTION_NOT_RECHARGEABLE 0x6cdbu /* "cdNot Rechargeable.  %s" (3C593) */
#define XEEN_TEXT_EQUIP_RING              0x774au /* the second ring: "ring" (42434) */
#define XEEN_TEXT_EQUIP_MEDAL             0x774fu /* "medal" (4243B) */
#define XEEN_TEXT_OPTION_SERVICE_CONFIRM  0x842fu /* Fix or Identify: "l000000%s %sd for %lu gold?" (46FA4) */
#define XEEN_TEXT_OPTION_IDENTIFY         0x844fu /* "Identify" (46F97) */
#define XEEN_TEXT_OPTION_FIX              0x8458u /* "Fix" (46F9E) */

/* The shops and the character view. */
#define XEEN_TEXT_SHOP_WEAPONS_FULL     0x80deu /* "010000c%s's weapons backpack is full." (45F5A) */
#define XEEN_TEXT_SHOP_ARMOR_FULL       0x8107u /* "010000c%s's armor backpack is full." (45FFC) */
#define XEEN_TEXT_SHOP_ACCESSORIES_FULL 0x812eu /* "010000c%s's accessories backpack is full." (4609E) */
#define XEEN_TEXT_SHOP_MISC_FULL        0x815bu /* "010000c%s's miscellaneous backpack is full." (46140) */
#define XEEN_TEXT_VIEW_ESC_SPRITES      0x84bcu /* the character view's: "esc.icn" (479AB) */

/* The locations' voices (Darkside's or the Clouds') and a map's files. */
#define XEEN_TEXT_TRAINING_VOICE_DARKSIDE     0x55c3u /* "youtrn1.voc" (279FC) */
#define XEEN_TEXT_TRAINING_VOICE_CLOUDS       0x55cfu /* "training.voc" (27A04) */
#define XEEN_TEXT_BLACKSMITH_VOICE_DARKSIDE   0x55e4u /* "see2.voc" (27A7D) */
#define XEEN_TEXT_BLACKSMITH_VOICE_CLOUDS     0x55edu /* "whaddayo.voc" (27A85) */
#define XEEN_TEXT_TEMPLE_VOICE_DARKSIDE       0x55fau /* "help2.voc" (27AFA) */
#define XEEN_TEXT_TEMPLE_VOICE_CLOUDS         0x5604u /* "maywe2.voc" (27B02) */
#define XEEN_TEXT_LOCATION_HELLO_VOICE_CLOUDS 0x5625u /* "hello.voc" (27B90) */
#define XEEN_TEXT_GUILD_VOICE_DARKSIDE        0x563bu /* "parrot1.voc" (27C29) */
#define XEEN_TEXT_GUILD_VOICE_CLOUDS          0x5647u /* "guild10.voc" (27C31) */
#define XEEN_TEXT_BANK_VOICE_DARKSIDE         0x5666u /* "bank1.voc" (27CB7) */
#define XEEN_TEXT_BANK_VOICE_CLOUDS           0x5670u /* "banker.voc" (27CBF) */
#define XEEN_TEXT_GOODBYE_VOICE_DARKSIDE      0x5692u /* "gdluck1.voc" (27FD7) */
#define XEEN_TEXT_GOODBYE_VOICE_CLOUDS        0x569eu /* "goodbye.voc" (27FDE) */
#define XEEN_TEXT_MAP_MONSTERS_FILE           0x62c4u /* a map's monster positions: "maze%c%03u.mob" (30FB6) */

/* The menus, the save dialog, the save files and the movie player's commands. */
#define XEEN_TEXT_DARKSIDE_PREFIX          0x30abu /* a world's file-name prefix: "dark" (read through the far
                                                   * pointer DS 009A: 13 readers, e.g. 1A1CD, 2E9CA, 3A680) */
#define XEEN_TEXT_CLOUDS_PREFIX            0x30b0u /* "xeen" (through DS 009E: 13 readers, e.g. 1A1D6, 2E9D3, 3A689) */
#define XEEN_TEXT_OPTIONS_CLOUDS_TITLE     0x492au /* "World" (16CD8, 16DD4) */
#define XEEN_TEXT_OPTIONS_DARKSIDE_TITLE   0x4930u /* "Darkside" (16CDF, 16DDB) */
#define XEEN_TEXT_COMMAND_DARKSIDE_INTRO   0x4939u /* the movie player's command: "XEENCOMMAND=DARKINTRO" (16EA6) */
#define XEEN_TEXT_COMMAND_CLOUDS_INTRO     0x4958u /* "XEENCOMMAND=XEENINTRO" (16EEE, 5A772, 5AA18, 5E59F) */
#define XEEN_TEXT_COMMAND_DARKSIDE_ENDING  0x496eu /* "XEENCOMMAND=ENDGAME2,1234567890" (16EFB) */
#define XEEN_TEXT_COMMAND_CLOUDS_ENDING    0x498eu /* "XEENCOMMAND=ENDGAME,1234567890" (16F10) */
#define XEEN_TEXT_COMMAND_WORLD_ENDING     0x49adu /* "XEENCOMMAND=WORLDEND,1234567890" (16F24) */
#define XEEN_TEXT_SAVE_FILE_FORMAT         0x4adeu /* "%s%02u.wox" (18076, 18168, 1821E, 18279) */
#define XEEN_TEXT_PANEL_DARK_SAVES         0x5c22u /* "dark??.wox" (29A62) */
#define XEEN_TEXT_PANEL_CLOUDS_SAVES       0x5c4bu /* "xeen??.wox" (29AA6) */
#define XEEN_TEXT_DIALOG_LOAD              0x5f1bu /* the save dialog's title word: "Load" (2B043) */
#define XEEN_TEXT_DIALOG_START             0x5f20u /* "Start" (2B051) */
#define XEEN_TEXT_DIALOG_SAVE              0x5f26u /* "Save" (2B058) */
#define XEEN_TEXT_DIALOG_SLOT_LINE         0x5f45u /* a slot's line: "r000027r000? 1l" (2B327) */
#define XEEN_TEXT_THANKS_CLOUDS_TITLE      0x60b4u /* "World" (2C85A) */
#define XEEN_TEXT_THANKS_DARKSIDE_TITLE    0x60bau /* "Darkside" (2C861) */
#define XEEN_TEXT_RESIDENT_GLOBAL          0x7b4fu /* a loaded game's resident sprites: "global.icn" (43C4B) */
#define XEEN_TEXT_RESIDENT_BORDER          0x7b5au /* "border.icn" (43C5C) */
#define XEEN_TEXT_RESIDENT_SPELL_EFFECTS   0x7b65u /* "spellfx.icn" (43C6D) */
#define XEEN_TEXT_RESIDENT_FECP            0x7b71u /* "fecp.brd" (43C7E) */
#define XEEN_TEXT_RESIDENT_BLESS           0x7b7au /* "bless.icn" (43C8F) */
#define XEEN_TEXT_RESIDENT_HP_BARS         0x7b84u /* "hpbars.icn" (43CA0) */
#define XEEN_TEXT_RESIDENT_RESTORE         0x7b8fu /* "restorex.icn" (43CB1) */
#define XEEN_TEXT_RESIDENT_CHARACTER_POWER 0x7b9cu /* "charpow.icn" (43CC2) */
#define XEEN_TEXT_DOS_CTIME_FORMAT         0x8bafu /* the C library's date line: "%s %s %02d %02d:%02d:%02d %4d" (133E4) */

/* The quest screen. */
#define XEEN_TEXT_QUESTS_NONE_CURRENT 0x70bau /* "c000000 /  / No Current Quests" (no immediate reader found) */
#define XEEN_TEXT_QUESTS_NO_NOTES     0x7103u /* "cNo Auto Notes" (no immediate reader found) */

/* The tavern, the training grounds and the temple (Darkside's voice or the Clouds'). */
#define XEEN_TEXT_TRAINING_DONE_VOICE_DARKSIDE    0x50f4u /* "prtygd.voc" (2551D) */
#define XEEN_TEXT_TRAINING_DONE_VOICE_CLOUDS      0x50ffu /* "trainin2.voc" (25524) */
#define XEEN_TEXT_TRAINING_REFUSED_VOICE_DARKSIDE 0x510cu /* lacking experience or unpaid: "gtlost.voc" (255F3) */
#define XEEN_TEXT_TRAINING_REFUSED_VOICE_CLOUDS   0x5117u /* at the top level, or unpaid: "trainin1.voc" (255FB) */
#define XEEN_TEXT_TRAINING_NEED_VOICE_CLOUDS      0x5124u /* lacking experience below the top level: "trainin0.voc" (25642) */
#define XEEN_TEXT_TEMPLE_AHH_VOICE                0x5131u /* "ahh.voc" (257C9, 25883) */
#define XEEN_TEXT_TAVERN_GULP_VOICE               0x5143u /* "gulp.voc" (259F8) */
#define XEEN_TEXT_TAVERN_GOOD_STUFF               0x514cu /* "/  / Good Stuff /  / Hit a key!" (25A2F) */
#define XEEN_TEXT_THANKS_VOICE_DARKSIDE           0x5165u /* "thanks2.voc" (25C44, 25D8F) */
#define XEEN_TEXT_THANKS_VOICE_CLOUDS             0x5171u /* "thankyou.voc" (25C4B, 25D96) */
#define XEEN_TEXT_TAVERN_FOOD_FULL                0x517eu /* "Your food packs are already full!" (25C6B) */
#define XEEN_TEXT_TAVERN_DRUNK                    0x51a4u /* "/  / You're Drunk /  / Hit a key!" (25CD4) */
#define XEEN_TEXT_TAVERN_HAVE_A_DRINK             0x51c8u /* "/  / Have a Drink /  / Hit a key!" (25EC8) */

#endif
