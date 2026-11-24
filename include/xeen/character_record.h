#ifndef XEEN_CHARACTER_RECORD_H
#define XEEN_CHARACTER_RECORD_H

#include <stdint.h>

/*
 * Queries on one character record (XEEN_CHARACTER_BYTES = 162h bytes, xeen/party.h): a party
 * member at DS:B41E + 162h * n, a roster entry or the BC6A scratch record. These are the
 * original's shared record accessors; none of them modifies the record.
 *
 * Field offsets within the record. docs/coverage/CHARACTER_RECORD_LAYOUT.md gives each field's
 * original readers and the evidence; fields it lists as uncertain have no name here. Words and
 * dwords are little-endian (xeen/byte_order.h); several fields are read both signed and unsigned
 * by the original, so the names carry no type: each reader keeps its own conversion.
 */
#define XEEN_CHARACTER_SEX         0x10u /* byte, index of the DS:2EB4 sex names */
#define XEEN_CHARACTER_RACE        0x11u /* byte, index of the DS:2E94 race names */
#define XEEN_CHARACTER_INN_SIDE    0x12u /* byte: the side (DS:96E2) of the inn at +154h, stored with it */
#define XEEN_CHARACTER_CLASS       0x13u /* byte, index of the DS:286A class names */
#define XEEN_CHARACTER_STATS       0x14u /* 7 pairs: base byte at +2s, temporary byte at +2s+1 */
#define XEEN_CHARACTER_STAT_BYTES  2u    /* bytes per stat pair */
#define XEEN_CHARACTER_AC_BONUS    0x22u /* temporary AC, unsigned byte (4660F) */
#define XEEN_CHARACTER_LEVEL       0x23u /* permanent level, byte */
#define XEEN_CHARACTER_LEVEL_BONUS 0x24u /* temporary level, byte (event give 11, SetVar) */
#define XEEN_CHARACTER_BIRTH_DAY   0x25u /* byte: the day of the year (DS:EFF0) at creation */
#define XEEN_CHARACTER_AGE_BONUS \
    0x26u /* unsigned byte added to the age by 2BAC7 (magical ageing) */
#define XEEN_CHARACTER_SKILLS      0x27u /* one nonzero byte per skill the character has */
#define XEEN_SKILL_COUNT           18u   /* Thievery .. Danger Sense (DS:2E94 names 48..65) */
#define XEEN_CHARACTER_AWARDS      0x39u /* 64 bytes: award n at +39h + xeen_award_slot(n) (46384) */
#define XEEN_AWARD_WARZONE_WINS    9u    /* award.bin 9 "%u Warzone Wins": its byte counts the arena's wins (360C4) */
#define XEEN_AWARD_RACE            17u   /* award.bin 17: its text takes the member's race name (the awards view) */
#define XEEN_AWARD_COUNT           88u   /* award.bin's entries (the awards view's loop) */
#define XEEN_CHARACTER_SPELLS      0x79u /* 39 bytes: one per entry of the class's spell list, nonzero known */
#define XEEN_CHARACTER_BEACON_MAP  0xA0u /* byte: Lloyd's Beacon's map (0 none yet), with +A1h/+A2h and +136h */
#define XEEN_CHARACTER_BEACON_X    0xA1u /* byte: the beacon's x (ED97 when set) */
#define XEEN_CHARACTER_BEACON_Y    0xA2u /* byte: the beacon's y (ED98 when set) */
#define XEEN_CHARACTER_CASTER      0xA3u /* byte: 1 when the class has a spell list (creation 40119), else 0 */
#define XEEN_CHARACTER_READY_SPELL 0xA4u /* byte: the ready spell, an index into the class's list; FFh none */
#define XEEN_NO_READY_SPELL        0xffu /* XEEN_CHARACTER_READY_SPELL: no spell readied */
#define XEEN_CHARACTER_QUICK_FIGHT 0xA5u /* byte: 387AE's choice: 0 Attack, 1 the ready spell, 2 Block, 3 Run */
#define XEEN_CHARACTER_WEAPONS     0xA6u  /* item category 0: 9 rows of 4 bytes (xeen/items.h) */
#define XEEN_ITEM_ROWS             9u     /* rows per item category */
#define XEEN_ITEM_ROW_BYTES        4u     /* material, id, state, equipped slot */
#define XEEN_ITEM_ROW_ID           1u     /* the id byte of an item row */
#define XEEN_ITEM_ROW_MATERIAL     0u     /* the material byte (a misc row's key) */
#define XEEN_ITEM_ROW_STATE        2u     /* 40h cursed, 80h broken, the low six bits a power or the charges */
#define XEEN_ITEM_ROW_EQUIPPED     3u     /* the equipped slot (0: carried) */
/* The equipped slots (XEEN_ITEM_ROW_EQUIPPED), by the items 424C3 puts in each (the item names p2384, p2410, p2448). */
#define XEEN_SLOT_WEAPON     1u  /* weapons 1..17, "long sword".."spear" */
#define XEEN_SLOT_SHIELD     2u  /* armor 8 */
#define XEEN_SLOT_BODY       3u  /* armor 1..7, "robes".."plate armor" */
#define XEEN_SLOT_MISSILE    4u  /* weapons 30..33, the bows, crossbow and sling: the combat's ranged weapon */
#define XEEN_SLOT_HELM       5u  /* armor 9 */
#define XEEN_SLOT_GAUNTLETS  6u  /* armor 13 */
#define XEEN_SLOT_MEDAL      7u  /* accessories 3..7, "broach".."scarab": two at most */
#define XEEN_SLOT_RING       8u  /* accessory 1: two at most */
#define XEEN_SLOT_BOOTS      9u  /* armor 10 */
#define XEEN_SLOT_CLOAK      10u /* armor 11 and 12, "cloak", "cape" */
#define XEEN_SLOT_AMULET     11u /* accessories 8..10, "pendant", "necklace", "amulet" */
#define XEEN_SLOT_BELT       12u /* accessory 2 */
#define XEEN_SLOT_TWO_HANDED 13u /* weapons 18..29 and 34, "bardiche".."great axe" and the Xeen Slayer Sword */
#define XEEN_ITEM_CURSED           0x40u  /* state bits */
#define XEEN_ITEM_BROKEN           0x80u
#define XEEN_ITEM_POWER            0x3fu  /* the state's low six bits: a power or the charges */
/* The four item categories, each XEEN_ITEM_ROWS rows from XEEN_CHARACTER_WEAPONS + 24h * category. */
#define XEEN_ITEMS_WEAPONS         0u
#define XEEN_ITEMS_ARMOR           1u
#define XEEN_ITEMS_ACCESSORIES     2u
#define XEEN_ITEMS_MISC            3u
#define XEEN_CHARACTER_ITEM(category, row) \
    (XEEN_CHARACTER_WEAPONS + XEEN_ITEM_ROWS * XEEN_ITEM_ROW_BYTES * (category) + XEEN_ITEM_ROW_BYTES * (row))
#define XEEN_CHARACTER_BEACON_SIDE 0x136u /* byte: the beacon's side (0 Clouds, 1 Darkside) */
#define XEEN_CHARACTER_RESISTANCES 0x137u /* 6 pairs (xeen_resistance): base +2e, temporary +2e+1 */
#define XEEN_CHARACTER_CONDITIONS  0x143u /* one byte per condition, indexed by xeen_condition */
#define XEEN_CHARACTER_INN         0x154u /* word: the inn (DS:ED9D) a roster character lodges at */
#define XEEN_CHARACTER_HP          0x156u /* word, current hit points (signed and unsigned readers) */
#define XEEN_CHARACTER_SP          0x158u /* word, current spell points (signed and unsigned readers) */
#define XEEN_CHARACTER_BIRTH_YEAR  0x15Au /* word: the year (DS:EFF2) minus 18 at creation */
#define XEEN_CHARACTER_EXPERIENCE  0x15Cu /* dword, experience points (add/adc, sub/sbb pairs) */

/* The seven stats, in the order of the record's pairs and of the DS:2F04 stat names (2BD94). */
enum xeen_stat {
    XEEN_STAT_MIGHT = 0,
    XEEN_STAT_INTELLECT = 1,
    XEEN_STAT_PERSONALITY = 2,
    XEEN_STAT_ENDURANCE = 3,
    XEEN_STAT_SPEED = 4,
    XEEN_STAT_ACCURACY = 5,
    XEEN_STAT_LUCK = 6
};
#define XEEN_STAT_COUNT 7u

/* The classes (the record's +13h), in the order of the DS:286A class names. */
enum xeen_class {
    XEEN_CLASS_KNIGHT = 0,
    XEEN_CLASS_PALADIN = 1,
    XEEN_CLASS_ARCHER = 2,
    XEEN_CLASS_CLERIC = 3,
    XEEN_CLASS_SORCERER = 4,
    XEEN_CLASS_ROBBER = 5,
    XEEN_CLASS_NINJA = 6,
    XEEN_CLASS_BARBARIAN = 7,
    XEEN_CLASS_DRUID = 8,
    XEEN_CLASS_RANGER = 9
};
#define XEEN_CLASS_COUNT 10u

/* The races (the record's +11h), in the order of the DS:2E94 race names (its entries 0..4). */
enum xeen_race {
    XEEN_RACE_HUMAN = 0,
    XEEN_RACE_ELF = 1,
    XEEN_RACE_DWARF = 2,
    XEEN_RACE_GNOME = 3,
    XEEN_RACE_HALF_ORC = 4 /* "H-Orc" */
};

/* The skills: the record's byte XEEN_CHARACTER_SKILLS + skill, in the order of the DS:2F54 skill names. */
enum xeen_skill {
    XEEN_SKILL_THIEVERY = 0,
    XEEN_SKILL_ARMS_MASTER = 1,
    XEEN_SKILL_ASTROLOGER = 2,
    XEEN_SKILL_BODY_BUILDER = 3,
    XEEN_SKILL_CARTOGRAPHER = 4,
    XEEN_SKILL_CRUSADER = 5,
    XEEN_SKILL_DIRECTION_SENSE = 6,
    XEEN_SKILL_LINGUIST = 7,
    XEEN_SKILL_MERCHANT = 8,
    XEEN_SKILL_MOUNTAINEER = 9,
    XEEN_SKILL_NAVIGATOR = 10,
    XEEN_SKILL_PATH_FINDER = 11,
    XEEN_SKILL_PRAYER_MASTER = 12,
    XEEN_SKILL_PRESTIDIGITATOR = 13,
    XEEN_SKILL_SWIMMER = 14,
    XEEN_SKILL_TRACKER = 15,
    XEEN_SKILL_SPOT_SECRET_DOORS = 16,
    XEEN_SKILL_DANGER_SENSE = 17
};

/*
 * The six resistance pairs, in record order: the order the character view's resistance text
 * (DS:788E) lists them. Not the order of the DS:2896 element names nor of 37A02's save types.
 */
enum xeen_resistance {
    XEEN_RESISTANCE_FIRE = 0,
    XEEN_RESISTANCE_COLD = 1,
    XEEN_RESISTANCE_ELECTRICITY = 2,
    XEEN_RESISTANCE_POISON = 3,
    XEEN_RESISTANCE_ENERGY = 4,
    XEEN_RESISTANCE_MAGIC = 5
};
#define XEEN_RESISTANCE_COUNT 6u

/* Offsets of one stat's or resistance's base and temporary bytes, and of one condition byte. */
#define XEEN_CHARACTER_STAT(s)                 (XEEN_CHARACTER_STATS + XEEN_CHARACTER_STAT_BYTES * (s))
#define XEEN_CHARACTER_STAT_TEMPORARY(s)       (XEEN_CHARACTER_STAT(s) + 1u)
#define XEEN_CHARACTER_RESISTANCE(e)           (XEEN_CHARACTER_RESISTANCES + 2u * (e))
#define XEEN_CHARACTER_RESISTANCE_TEMPORARY(e) (XEEN_CHARACTER_RESISTANCE(e) + 1u)
#define XEEN_CHARACTER_CONDITION(c)            (XEEN_CHARACTER_CONDITIONS + (c))

/* Weapon id 22h, the Xeen Slayer Sword (the inn's Delete refusal text at 44269). */
#define XEEN_WEAPON_XEEN_SLAYER_SWORD 0x22u

/*
 * Condition indexes, in the order of the original's condition names (DS:2E94 entries 11..27).
 * A higher index is a worse condition; GOOD is the result when no condition byte is set.
 */
enum xeen_condition {
    XEEN_CONDITION_CURSED = 0,
    XEEN_CONDITION_HEART_BROKEN = 1,
    XEEN_CONDITION_WEAK = 2,
    XEEN_CONDITION_POISONED = 3,
    XEEN_CONDITION_DISEASED = 4,
    XEEN_CONDITION_INSANE = 5,
    XEEN_CONDITION_IN_LOVE = 6,
    XEEN_CONDITION_DRUNK = 7,
    XEEN_CONDITION_ASLEEP = 8,
    XEEN_CONDITION_DEPRESSED = 9,
    XEEN_CONDITION_CONFUSED = 10,
    XEEN_CONDITION_PARALYZED = 11,
    XEEN_CONDITION_UNCONSCIOUS = 12,
    XEEN_CONDITION_DEAD = 13,
    XEEN_CONDITION_STONE = 14,
    XEEN_CONDITION_ERADICATED = 15,
    XEEN_CONDITION_GOOD = 16
};
#define XEEN_CONDITION_COUNT 16u /* condition bytes in a record */

/* Whether condition index C is dead, stone or eradicated (13..15): the range the original tests as
 * one (the heal spells' refusal, Rest's meal, the event condition store's HP clear). */
static inline int xeen_condition_is_deathlike(unsigned c)
{
    return c >= XEEN_CONDITION_DEAD && c <= XEEN_CONDITION_ERADICATED;
}

/* Whether worst condition C keeps a member from acting: Asleep, or Paralyzed..Eradicated. The original tests this
 * set in each place (a combat turn, the training, Bash's bashers, a treasure's recipient, the items' Use). */
static inline int xeen_condition_cannot_act(uint8_t c)
{
    return c == XEEN_CONDITION_ASLEEP || (c >= XEEN_CONDITION_PARALYZED && c <= XEEN_CONDITION_ERADICATED);
}

/* Every function takes a readable, non-NULL record; DOS far pointers are flat pointers here. */

/*
 * Returns the character's current level, the permanent plus the temporary level (original 2C909).
 * Both bytes are unsigned, so the result is 0..510; the original's clamp of a negative sum to 0
 * can never apply.
 */
uint16_t xeen_character_current_level(const uint8_t *character);

/*
 * Returns the character's worst condition: the highest index whose condition byte is nonzero,
 * or XEEN_CONDITION_GOOD (16) when none is set (original 2AB96, which returns AL only).
 */
uint8_t xeen_character_worst_condition(const uint8_t *character);

/* Returns how many of the 18 skill bytes are nonzero, 0..18 (original 46364). */
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
uint16_t xeen_character_skill_count(const uint8_t *character);

/*
 * Returns 1 when one of the nine weapon rows holds the Xeen Slayer Sword, else 0 (original 3CF35).
 * Only each row's id byte is compared: the material, state and equipped bytes are ignored.
 */
uint16_t xeen_character_has_xeen_slayer_sword(const uint8_t *character);

/**
 * The award byte's slot (46384's test and 463DE's give/take): awards 49h and 51h are slots 3Eh and
 * 3Fh, the others VALUE mod 64 as a signed 16-bit remainder (CWD; IDIV 40h), so a negative award
 * gives a negative slot, here its 16-bit two's complement. The byte is XEEN_CHARACTER_AWARDS + slot.
 */
static inline uint16_t xeen_award_slot(uint16_t value)
{
    return (uint16_t)(value == 0x49u   ? 0x3eu
                      : value == 0x51u ? 0x3fu
                                       : (unsigned)(uint16_t)(int16_t)((int16_t)value % 64));
}
/** Award VALUE's nibble of its byte: the high one (mask F0h; bit 4 when given) from 40h (signed
 * compare, 46384's JGE), else the low one (0Fh; bit 0). */
static inline int xeen_award_high_nibble(uint16_t value)
{
    return (int16_t)value >= 0x40;
}

#endif
