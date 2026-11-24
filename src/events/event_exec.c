#include "xeen/map_objects.h"
#include "xeen/party.h"
#include "xeen/ds_globals.h"
#include "xeen/character_record.h"
#include "xeen/event_exec.h"
#include "xeen/addon/damage_numbers.h"
#include "xeen/byte_order.h"
#include "xeen/treasure.h"
#include "xeen/items.h"
#include "xeen/windows.h"
/* add-on damage_numbers: its HP hook (NULL while the add-on is off). */
void (*xeen_damage_numbers_event_hp)(const uint8_t *ds, uint16_t record, uint16_t value,
                                     int restore);
#include <string.h>
#include <stdio.h>
/*
 * The original interpreter (313FD) keeps its working state in the stack frame of the dispatcher 310B4, and the
 * operations' comments below cite those slots as the listing names them. Each slot's native home:
 *   BP-4           the record's sequence number                        xeen_event_state.sequence
 *   BP-1E, BP-20   the event's cell (x, y)                             xeen_event_state.x, .y
 *   BP-4F, BP-50   the event flag and the host's marker                xeen_event_state.event_flag
 *   BP-C           the selected member, 1..6 (0: the whole party)      selected_character
 *   BP-18          the window cleanup the end of the script owes      window_cleanup
 *                  (3369E's window close and 2ABB8(1), before 2F869)
 *   BP-A           the dispatcher's AX (operation 17's location result) return_ax
 *   BP-4E          the dispatch's one 1B503(1) scene has been shown    scene_shown
 *   BP-6           operation 12's next member; operation 48's answer   loop_member; choice_ax
 *   BP-16          operation 12's take mode                            loop_take
 *   BP-E           a word answer, a mirror row or a map; never         word_ax
 *                  initialised by the frame (XEEN_BP_E_UNSET)
 */
/*
 * Where an event operation stops as unsupported (an XEE_UNSUPPORTED result); each value is the original address of
 * the step (tools/xeen_replay/stops.json). They mark operand values and states no shipped script was found to use.
 */
enum {
    IF_STOP_UNKNOWN_CONDITION = 0x2c4c4,          /* a condition none of If's tests serves */
    TAKE_GIVE_STOP_RANGE = 0x2c0cf,               /* a world past Darkside or more than eight members */
    TAKE_GIVE_STOP_COMBAT = 0x2ba15,              /* a take or give inside combat */
    CONDITIONAL_GIVE_STOP_MODE_43 = 0x31f2f,      /* the second pair's mode 43h */
    CONDITIONAL_GIVE_STOP_CONDITION_44 = 0x2c25d, /* condition 44 */
    CONDITIONAL_GIVE_STOP_CONDITION_VALUE = 0x2bf6e, /* the condition's value was not served */
    GIVE_STOP_SPRITE = 0x2df19,                   /* a give of the map objects' special sprites */
    GIVE_STOP_FLASH = 0x2b9c7,                    /* a give that flashes a member */
    MEMBER_TEXT_STOP_FORMAT = 0x315c2,            /* a format other than %s, or a full text */
    CENTRED_TEXT_STOP_FORMAT = 0x31600,           /* op 1's DS format with conversions other than its %s */
    TWO_LINES_STOP_FORMAT = 0x3183e,              /* op 49's DS format with conversions other than its two %s */
    BOTTOM_TEXT_STOP_FORMAT = 0x3171a,            /* ops 41/53's DS format with conversions other than its %s */
    SIGN_TEXT_STOP_FORMAT = 0x318b2,              /* a sign's DS format with conversions other than its %s */
    SET_CHARACTER_STOP_VALUE = 0x32356,           /* a value above 7 */
    AFTER_MAP_STOP_FIRST = 0x2de1d,               /* a first operand above 3 */
    VOICE_STOP_INDEX = 0x33084,                   /* a voice past the five */
    TELEPORT_STOP_UNSET_ROW = 0x3196b,            /* map 0 without a mirror row from this invocation */
    GATE_OPERATION_STOP_TIER = 0x31590,           /* an operation or operand outside the supported tier */
    ENDING_STOP_CLOUDS = 0x334d8,                 /* the Clouds ending, outside a tier that runs endings */
    ENDING_STOP_DARKSIDE = 0x334af,               /* ... the Darkside ending */
    ENDING_STOP_WORLD = 0x3349d,                  /* ... the World of Xeen ending */
    /* The take/give core hands these back through its `stop` out-parameter (the caller stops there): */
    TAKE_GIVE_STOP_AMOUNT_PROMPT = 0x2f92f,       /* a take of no amount: the party is asked how much */
    TAKE_GIVE_STOP_RANDOM_AMOUNT = 0x2c9d6,       /* the same mode and value on both sides: a random amount */
    TAKE_STOP_MODE = 0x2c9ff,                     /* a take of a variable the native does not take */
    TAKE_STOP_ITEM_REMOVE = 0x43385,              /* the host could not remove (clear and re-sort) the item */
    GIVE_STOP_MODE = 0x2cfd9,                     /* a give of a variable the native does not give */
    GIVE_STOP_ITEM_GENERATE = 0x305e8,            /* the host could not generate the random item */
    GIVE_STOP_YEARS_TEMPORARIES = 0x29168,        /* the host could not renew the temporaries after the years */
    GIVE_STOP_DAYS_CLOCK = 0x30cf3,               /* the host could not advance the clock by the days */
    GIVE_STOP_DAYS_REST = 0x30db3,                /* the days reached the "The Party needs rest!" branch */
    GIVE_STOP_MAXIMUM_HP = 0x2aac2,               /* the host could not compute the member's maximum HP */
    GIVE_STOP_MAXIMUM_SP = 0x46470                /* ... the maximum SP */
};
/** Adds V to the little-endian dword at P (the original's two-word ADD/ADC). */
static void add_le32(uint8_t *p, uint32_t v)
{
    xeen_put_le32(p, xeen_get_le32(p) + v);
}
/** Adds V's low word to the little-endian word at P (wrapping, as the original's word ADD). */
static void add_le16(uint8_t *p, uint32_t v)
{
    xeen_put_le16(p, (uint16_t)(xeen_get_le16(p) + (uint16_t)v));
}
/* The width in bytes of a take/give value in a record, by its mode: 4 for experience (16), gold (34) and 100, 2 for
 * 25, gems (35), 101 and 106, else 1. Original: the 31C73/31D5E tables of the take/give core 2C94C. */
static unsigned tg_width(unsigned mode)
{
    return mode == 16u || mode == 34u || mode == 100u                   ? 4u
           : mode == 25u || mode == 35u || mode == 101u || mode == 106u ? 2u
                                                                        : 1u;
}
/** The length of the DS text at AT (A827, A43F: the 1000-byte text buffers), at most 1000 (no NUL within it). */
static size_t ds_text_length(const uint8_t *ds, uint16_t at)
{
    size_t length = 0;
    while (length < 1000u && ds[at + length]) {
        ++length;
    }
    return length;
}
/* What event_format did. */
enum { EVENT_FORMAT_DONE, EVENT_FORMAT_REFUSED, EVENT_FORMAT_TOO_LONG };
/**
 * The original's sprintf (14BBA) of an event format of the program data, the DS text at FORMAT (read where the
 * handler runs, so a changed DS is followed), with the DS texts ARGS[0..COUNT-1] for its %s in order, into DS at OUT.
 * Each argument is its text up to its NUL, at most 1000 bytes (ds_text_length: the text buffers' contract); "%%" is
 * '%'; every other byte is itself. Served only when the format's conversions are exactly COUNT plain %s (no flag,
 * width, precision or other conversion: none of the program data's event formats has one) and its NUL lies within
 * DS (DN bytes); else EVENT_FORMAT_REFUSED. EVENT_FORMAT_TOO_LONG when the expansion and its NUL exceed ROOM bytes
 * or the end of DS. A refusal writes nothing. event_format_check is the same test without the expansion (a handler
 * whose bound precedes other writes tests first, then formats where the original's sprintf runs).
 */
static int event_format_check(const uint8_t *ds, size_t dn, uint16_t format, const uint16_t *args, unsigned count,
                              uint16_t out, size_t room)
{
    size_t i, total = 0;
    unsigned k = 0;
    for (i = format; i < dn && ds[i]; ++i) {
        if (ds[i] != '%') {
            ++total;
        } else if (i + 1u < dn && ds[i + 1u] == '%') {
            ++total;
            ++i;
        } else if (i + 1u < dn && ds[i + 1u] == 's' && k < count) {
            total += ds_text_length(ds, args[k++]);
            ++i;
        } else {
            return EVENT_FORMAT_REFUSED;
        }
    }
    if (i >= dn || k != count) {
        return EVENT_FORMAT_REFUSED;
    }
    if (out >= dn || total + 1u > room || total + 1u > dn - out) {
        return EVENT_FORMAT_TOO_LONG;
    }
    return EVENT_FORMAT_DONE;
}
static int event_format(uint8_t *ds, size_t dn, uint16_t format, const uint16_t *args, unsigned count, uint16_t out,
                        size_t room)
{
    size_t i, o;
    unsigned k;
    int f = event_format_check(ds, dn, format, args, count, out, room);
    if (f != EVENT_FORMAT_DONE) {
        return f;
    }
    for (i = format, o = out, k = 0; ds[i]; ++i) {
        if (ds[i] == '%' && ds[i + 1u] == '%') {
            ds[o++] = '%';
            ++i;
        } else if (ds[i] == '%') {
            size_t n = ds_text_length(ds, args[k]);
            memmove(ds + o, ds + args[k++], n);
            o += n;
            ++i;
        } else {
            ds[o++] = ds[i];
        }
    }
    ds[o] = 0;
    return EVENT_FORMAT_DONE;
}
/** Reads a take/give value of WIDTH bytes (1, 2 or 4) at V. */
static uint32_t tg_value(const uint8_t *v, unsigned width)
{
    return width == 4u ? xeen_get_le32(v) : width == 2u ? xeen_get_le16(v) : v[0];
}
/*
 * The operand pairs of the take, give and conditional give operations (12, 20, 28, 29, 38): from byte 6 of the
 * record, each pair is a mode byte (the event variable) and a value of that mode's width (tg_width), so a pair's
 * position depends on every mode before it. operand_pair reads the pair at AT (its mode and value) and returns the
 * index after it; operand_pairs_end measures COUNT pairs for the operation gate.
 */
static size_t operand_pair(const uint8_t *p, size_t at, unsigned *mode, uint32_t *value)
{
    unsigned width = tg_width(p[at]);
    *mode = p[at];
    *value = tg_value(p + at + 1u, width);
    return at + 1u + width;
}
/*
 * XEE_DONE with *END the index after the last of COUNT pairs, or XEE_MALFORMED when a pair's mode byte lies past the
 * record's end (byte P[0]; *END unchanged). A last value running past the end is the caller's length test
 * (accept_operation). Original: the handlers' widths CS:2B94 (If), CS:2B78/2B5C/2B40 (31C62), 2DECE (324E6).
 */
static xeen_exec_status operand_pairs_end(const uint8_t *p, unsigned count, size_t *end)
{
    size_t at = 6u;
    for (unsigned k = 0; k < count; ++k) {
        if (at > p[0]) {
            return XEE_MALFORMED;
        }
        at += 1u + tg_width(p[at]);
    }
    *end = at;
    return XEE_DONE;
}
/*
 * The event variables: the operand number that selects what an If condition tests (2BF6E by CS:0BAB),
 * what a take or a give changes (2C94C by CS:23D5 / CS:22F1) and what SetVar stores (2E285 by
 * CS:2C55). The tables share the numbering; a number a table does not list takes that table's
 * default. Named here are the numbers whose meaning every consumer that lists them agrees on; the
 * others (10, 25, 44, 67, 74, 83, 86..93, 99, 102, 105) stay numeric at their sites. The party's
 * protections and effects (17, 69..73, 79, 94) are the same variable in all four tables; 74 is not
 * (take and give change six of them, SetVar eight).
 * Stats and resistances are a first number plus an index: stats in XEEN_STAT_ order, resistances in
 * the operand order Fire, Electricity, Cold, Poison, Energy, Magic (not the record's).
 */
enum {
    VAR_SEX = 3,
    VAR_RACE = 4,
    VAR_CLASS = 5,
    VAR_HP = 8,
    VAR_SP = 9,
    VAR_LEVEL_BONUS = 11,
    VAR_AGE = 12, /* If: the computed age; take, give, SetVar: the age bonus byte */
    VAR_SKILL = 13,
    VAR_AWARD = 15,
    VAR_EXPERIENCE = 16,
    VAR_PROTECTION_POISON = 17,
    VAR_CONDITION = 18,
    VAR_SPELL = 19,
    VAR_GAME_FLAG = 20,
    VAR_ITEM = 21,
    VAR_GOLD = 34,
    VAR_GEMS = 35,
    VAR_TEMPORARY_STAT = 37,       /* ..43 */
    VAR_STAT = 45,                 /* ..51 */
    VAR_RESISTANCE = 52,           /* ..57 */
    VAR_TEMPORARY_RESISTANCE = 58, /* ..63 */
    VAR_LEVEL = 64,
    VAR_FOOD = 65,
    VAR_RANDOM_ITEM = 66, /* give only */
    VAR_LEVITATE = 69,
    VAR_LIGHT = 70,
    VAR_PROTECTION_FIRE = 71,
    VAR_PROTECTION_ELECTRICITY = 72,
    VAR_PROTECTION_COLD = 73,
    VAR_DAY = 76,         /* If, SetVar: the day; give: that many days pass */
    VAR_AC_BONUS = 77,
    VAR_MAX_HP = 78, /* If: HP at most the maximum; give: HP = the maximum */
    VAR_WIZARD_EYE = 79, /* take: off; give, SetVar: on whatever the value */
    VAR_MAX_SP = 81,
    VAR_DIRECTION = 84, /* If and SetVar only */
    VAR_YEAR = 85,
    VAR_WALK_ON_WATER = 94, /* take: off; give: on */
    VAR_AUTO_NOTE = 103,
    VAR_QUEST = 104,
    VAR_CHARACTER_BIT = 107,
};
/* Two If conditions whose numbers mean something only to the If (take, give and SetVar read them otherwise): the
 * host's yes/no question (its answer, or the value itself) and the party's day mod 10. */
enum {
    IF_YES_NO = 44,     /* 2C25D: 2F2FF(value, 0) */
    IF_DAY_MOD_10 = 93, /* the IDIV remainder of the day */
};
/* SetVar's store of VALUE into event variable TYPE of MEMBER (types 3..94; any other stores nothing). A byte
 * variable takes the value's low byte, a word its low word, experience, gold and gems the whole value. A condition
 * (give_condition): 16 clears all sixteen, In Love is set to 1, any other is incremented, and Dead, Stone or
 * Eradicated then takes a positive HP to 0. Wizard Eye is switched on whatever the value; 74 stores the low byte
 * into every party effect and protection (walk on water, the four protections, wizard eye, light, levitate).
 * Original: 2E285(type, value, member), CS:2C55 by type - 3; the record B41E + member x 162h (16-bit); 13FE7. */
static void give_condition(uint8_t *ds, uint16_t record, uint32_t value);
static void setvar_store(uint8_t *ds, unsigned type, uint32_t value, unsigned member)
{
    static const uint16_t byte_at[95] = {
            [VAR_SEX] = XEEN_CHARACTER_SEX,
            [VAR_RACE] = XEEN_CHARACTER_RACE,
            [VAR_CLASS] = XEEN_CHARACTER_CLASS,
            [10] = XEEN_CHARACTER_AC_BONUS,
            [VAR_LEVEL_BONUS] = XEEN_CHARACTER_LEVEL_BONUS,
            [VAR_AGE] = XEEN_CHARACTER_AGE_BONUS,
            [VAR_TEMPORARY_STAT + XEEN_STAT_MIGHT] = XEEN_CHARACTER_STAT_TEMPORARY(XEEN_STAT_MIGHT),
            [VAR_TEMPORARY_STAT + XEEN_STAT_INTELLECT] = XEEN_CHARACTER_STAT_TEMPORARY(XEEN_STAT_INTELLECT),
            [VAR_TEMPORARY_STAT + XEEN_STAT_PERSONALITY] = XEEN_CHARACTER_STAT_TEMPORARY(XEEN_STAT_PERSONALITY),
            [VAR_TEMPORARY_STAT + XEEN_STAT_ENDURANCE] = XEEN_CHARACTER_STAT_TEMPORARY(XEEN_STAT_ENDURANCE),
            [VAR_TEMPORARY_STAT + XEEN_STAT_SPEED] = XEEN_CHARACTER_STAT_TEMPORARY(XEEN_STAT_SPEED),
            [VAR_TEMPORARY_STAT + XEEN_STAT_ACCURACY] = XEEN_CHARACTER_STAT_TEMPORARY(XEEN_STAT_ACCURACY),
            [VAR_TEMPORARY_STAT + XEEN_STAT_LUCK] = XEEN_CHARACTER_STAT_TEMPORARY(XEEN_STAT_LUCK),
            [VAR_STAT + XEEN_STAT_MIGHT] = XEEN_CHARACTER_STAT(XEEN_STAT_MIGHT),
            [VAR_STAT + XEEN_STAT_INTELLECT] = XEEN_CHARACTER_STAT(XEEN_STAT_INTELLECT),
            [VAR_STAT + XEEN_STAT_PERSONALITY] = XEEN_CHARACTER_STAT(XEEN_STAT_PERSONALITY),
            [VAR_STAT + XEEN_STAT_ENDURANCE] = XEEN_CHARACTER_STAT(XEEN_STAT_ENDURANCE),
            [VAR_STAT + XEEN_STAT_SPEED] = XEEN_CHARACTER_STAT(XEEN_STAT_SPEED),
            [VAR_STAT + XEEN_STAT_ACCURACY] = XEEN_CHARACTER_STAT(XEEN_STAT_ACCURACY),
            [VAR_STAT + XEEN_STAT_LUCK] = XEEN_CHARACTER_STAT(XEEN_STAT_LUCK),
            [VAR_RESISTANCE + 0] = XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_FIRE),
            [VAR_RESISTANCE + 1] = XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_ELECTRICITY),
            [VAR_RESISTANCE + 2] = XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_COLD),
            [VAR_RESISTANCE + 3] = XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_POISON),
            [VAR_RESISTANCE + 4] = XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_ENERGY),
            [VAR_RESISTANCE + 5] = XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_MAGIC),
            [VAR_TEMPORARY_RESISTANCE + 0] = XEEN_CHARACTER_RESISTANCE_TEMPORARY(XEEN_RESISTANCE_FIRE),
            [VAR_TEMPORARY_RESISTANCE + 1] = XEEN_CHARACTER_RESISTANCE_TEMPORARY(XEEN_RESISTANCE_ELECTRICITY),
            [VAR_TEMPORARY_RESISTANCE + 2] = XEEN_CHARACTER_RESISTANCE_TEMPORARY(XEEN_RESISTANCE_COLD),
            [VAR_TEMPORARY_RESISTANCE + 3] = XEEN_CHARACTER_RESISTANCE_TEMPORARY(XEEN_RESISTANCE_POISON),
            [VAR_TEMPORARY_RESISTANCE + 4] = XEEN_CHARACTER_RESISTANCE_TEMPORARY(XEEN_RESISTANCE_ENERGY),
            [VAR_TEMPORARY_RESISTANCE + 5] = XEEN_CHARACTER_RESISTANCE_TEMPORARY(XEEN_RESISTANCE_MAGIC),
            [VAR_LEVEL] = XEEN_CHARACTER_LEVEL,
            [VAR_AC_BONUS] = XEEN_CHARACTER_AC_BONUS};
    static const uint16_t word_ds[95] = {[VAR_PROTECTION_POISON] = XEEN_DS_PROTECTION_POISON,
                                           [25] = XEEN_DS_MINUTE_OF_DAY,
                                           [VAR_FOOD] = XEEN_DS_PARTY_FOOD,
                                           [VAR_LIGHT] = XEEN_DS_PARTY_LIGHT,
                                           [VAR_PROTECTION_FIRE] = XEEN_DS_PROTECTION_FIRE,
                                           [VAR_PROTECTION_ELECTRICITY] = XEEN_DS_PROTECTION_ELECTRICITY,
                                           [VAR_PROTECTION_COLD] = XEEN_DS_PROTECTION_COLD,
                                           [VAR_DAY] = XEEN_DS_DAY,
                                           [VAR_YEAR] = XEEN_DS_YEAR};
    uint16_t record = XEEN_PARTY_RECORD(member);
    uint8_t lo = (uint8_t)value;
    if (type < 3u || type > 94u) {
        return; /* the table covers 3..94 (Original: 2E29F: JBE 5Bh after type - 3) */
    }
    if (byte_at[type]) {
        ds[(uint16_t)(record + byte_at[type])] = lo;
        return;
    }
    if (word_ds[type]) {
        xeen_put_le16(ds + word_ds[type], (uint16_t)value);
        return;
    }
    switch (type) {
        case VAR_HP:
            XEEN_DAMAGE_NUMBERS_EVENT(ds, record, (uint16_t)value, 0); /* add-on hook */
            xeen_put_le16(ds + (uint16_t)(record + XEEN_CHARACTER_HP), (uint16_t)value);
            break;
        case VAR_SP:
            xeen_put_le16(ds + (uint16_t)(record + XEEN_CHARACTER_SP), (uint16_t)value);
            break;
        case VAR_EXPERIENCE:
            xeen_put_le16(ds + (uint16_t)(record + XEEN_CHARACTER_EXPERIENCE),
                          (uint16_t)value);
            xeen_put_le16(ds + (uint16_t)(record + XEEN_CHARACTER_EXPERIENCE + 2u),
                          (uint16_t)(value >> 16));
            break;
        case VAR_CONDITION: /* the same rule as the give of a condition (Original: 2E32D, as 2D0C2) */
            give_condition(ds, record, value);
            break;
        case VAR_GOLD:
            xeen_put_le16(ds + XEEN_DS_PARTY_GOLD, (uint16_t)value);
            xeen_put_le16(ds + XEEN_DS_PARTY_GOLD + 2u, (uint16_t)(value >> 16));
            break;
        case VAR_GEMS:
            xeen_put_le16(ds + XEEN_DS_PARTY_GEMS, (uint16_t)value);
            xeen_put_le16(ds + XEEN_DS_PARTY_GEMS + 2u, (uint16_t)(value >> 16));
            break;
        case VAR_LEVITATE:
            ds[XEEN_DS_EFFECT_LEVITATE] = lo;
            break;
        case 74: /* every party effect and protection at once (Original: 2E578) */
            ds[XEEN_DS_EFFECT_WALK_ON_WATER] = lo;
            xeen_put_le16(ds + XEEN_DS_PROTECTION_POISON, lo);
            ds[XEEN_DS_EFFECT_WIZARD_EYE] = lo;
            xeen_put_le16(ds + XEEN_DS_PROTECTION_COLD, lo);
            xeen_put_le16(ds + XEEN_DS_PROTECTION_ELECTRICITY, lo);
            xeen_put_le16(ds + XEEN_DS_PROTECTION_FIRE, lo);
            xeen_put_le16(ds + XEEN_DS_PARTY_LIGHT, lo);
            ds[XEEN_DS_EFFECT_LEVITATE] = lo;
            break;
        case VAR_WIZARD_EYE:
            ds[XEEN_DS_EFFECT_WIZARD_EYE] = 1;
            break;
        case 83:
            ds[XEEN_DS_EVENT_DAMAGE_TARGET] = lo;
            break;
        case VAR_DIRECTION:
            ds[XEEN_DS_PARTY_DIRECTION] = lo;
            break;
        case VAR_WALK_ON_WATER:
            ds[XEEN_DS_EFFECT_WALK_ON_WATER] = lo;
            break;
        default:
            break;
    }
}
/* 463DE: awards 49h and 51h are slots 3Eh and 3Fh, the others value mod 64
 * (signed); giving sets bit 0 (values below 40h) or bit 4 of +39h+slot;
 * taking ORs F0h or 0Fh instead (the original's own clear). */
static void award(uint8_t *ds, unsigned member, uint16_t value, int give)
{
    unsigned slot = xeen_award_slot(value);
    int high = xeen_award_high_nibble(value);
    uint16_t a = (uint16_t)(XEEN_PARTY_RECORDS + member * XEEN_CHARACTER_BYTES +
                            XEEN_CHARACTER_AWARDS + slot);
    ds[a] |= give ? (!high ? 0x01u : 0x10u) : (!high ? 0xf0u : 0x0fu);
}
/*
 * The leftover value an If test compares when its condition produced none (an original quirk). Each test computes
 * a 32-bit value and compares it with the operand (if_compare). A condition the test does not list, and the "not
 * present" outcome of the skill, award, condition and spell tests, compute nothing: the comparison then reads
 * whatever its variable still holds. That is the previous test's value when an If ran before at the same depth;
 * before any, the two words the record's text copy left there: the low word the record's offset (known), the high
 * word a register of the executor's caller (not modelled: unknown).
 *   slot      the leftover value (its high word valid only with hi_known);
 *   hi_known  0 while the high word is the caller's unknown register;
 *   lost      1 when a deeper call overwrote the variable (a take/give in between): nothing is known.
 * Original: 2BF6E's result at [BP-2]:[BP-4], compared at 2C4C4; the record's text copy 15616 (31588) saves SI:DI in
 * the same frame slot; an earlier operation 12/20's 31DC8/3264C sets SI. The item test (2C0F6) first stores
 * FFFFFFFFh, so its "absent" outcome is that value (IF_ABSENT), not a leftover.
 */
typedef struct {
    uint32_t slot;
    int hi_known, lost;
} if_residue;
/** The If operation's comparison MODE: 0 at least, 1 equal, 2 at most (unsigned). */
static int if_compare(unsigned mode, uint32_t got, uint32_t want)
{
    return mode == 0u ? got >= want : mode == 1u ? got == want : mode == 2u ? got <= want : 0;
}
/* The record bytes of the stat and resistance variables 37..64 (44 is none): what If tests and a give
 * raises. */
static const uint16_t variable_byte[VAR_LEVEL - VAR_TEMPORARY_STAT + 1] = {
        XEEN_CHARACTER_STAT_TEMPORARY(XEEN_STAT_MIGHT),
        XEEN_CHARACTER_STAT_TEMPORARY(XEEN_STAT_INTELLECT),
        XEEN_CHARACTER_STAT_TEMPORARY(XEEN_STAT_PERSONALITY),
        XEEN_CHARACTER_STAT_TEMPORARY(XEEN_STAT_ENDURANCE),
        XEEN_CHARACTER_STAT_TEMPORARY(XEEN_STAT_SPEED),
        XEEN_CHARACTER_STAT_TEMPORARY(XEEN_STAT_ACCURACY),
        XEEN_CHARACTER_STAT_TEMPORARY(XEEN_STAT_LUCK),
        0,
        XEEN_CHARACTER_STAT(XEEN_STAT_MIGHT),
        XEEN_CHARACTER_STAT(XEEN_STAT_INTELLECT),
        XEEN_CHARACTER_STAT(XEEN_STAT_PERSONALITY),
        XEEN_CHARACTER_STAT(XEEN_STAT_ENDURANCE),
        XEEN_CHARACTER_STAT(XEEN_STAT_SPEED),
        XEEN_CHARACTER_STAT(XEEN_STAT_ACCURACY),
        XEEN_CHARACTER_STAT(XEEN_STAT_LUCK),
        XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_FIRE),
        XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_ELECTRICITY),
        XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_COLD),
        XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_POISON),
        XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_ENERGY),
        XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_MAGIC),
        XEEN_CHARACTER_RESISTANCE_TEMPORARY(XEEN_RESISTANCE_FIRE),
        XEEN_CHARACTER_RESISTANCE_TEMPORARY(XEEN_RESISTANCE_ELECTRICITY),
        XEEN_CHARACTER_RESISTANCE_TEMPORARY(XEEN_RESISTANCE_COLD),
        XEEN_CHARACTER_RESISTANCE_TEMPORARY(XEEN_RESISTANCE_POISON),
        XEEN_CHARACTER_RESISTANCE_TEMPORARY(XEEN_RESISTANCE_ENERGY),
        XEEN_CHARACTER_RESISTANCE_TEMPORARY(XEEN_RESISTANCE_MAGIC),
        XEEN_CHARACTER_LEVEL};
/* The record byte of event variable V when V is one of the stat, resistance and level variables, else 0. */
static uint16_t record_byte_of(unsigned v)
{
    return v >= VAR_TEMPORARY_STAT && v <= VAR_LEVEL ? variable_byte[v - VAR_TEMPORARY_STAT] : 0u;
}
/* The class spell lists (XEEN_DS_CLASS_SPELL_LISTS, Phase 186 class_spell_lists): 39 spell numbers per casting group
 * 0..2, 28h bytes apart. */
#define CLASS_SPELL_LIST_SIZE 0x27u
/*
 * Selects the casting group of the member at RECORD (CS:0B99 for 2C069, CS:22DF for 2D14B; the two
 * tables are equal): class 1 or 3 group 0, 2 or 4 group 1, 8 or 9 group 2, stored in F604. The other
 * classes (0, 5..7) leave F604 as it is: a stale group from an earlier selection. Returns the group.
 */
static uint16_t select_spell_group(uint8_t *ds, uint16_t record)
{
    static const int8_t group[9] = {[XEEN_CLASS_PALADIN - 1] = 0,  [XEEN_CLASS_ARCHER - 1] = 1,
                                    [XEEN_CLASS_CLERIC - 1] = 0,   [XEEN_CLASS_SORCERER - 1] = 1,
                                    [XEEN_CLASS_ROBBER - 1] = -1,  [XEEN_CLASS_NINJA - 1] = -1,
                                    [XEEN_CLASS_BARBARIAN - 1] = -1, [XEEN_CLASS_DRUID - 1] = 2,
                                    [XEEN_CLASS_RANGER - 1] = 2};
    unsigned c = (unsigned)ds[(uint16_t)(record + XEEN_CHARACTER_CLASS)] - 1u;
    if (c < 9u && group[c] >= 0) {
        xeen_put_le16(ds + 0xf604, (uint16_t)group[c]);
    }
    return xeen_get_le16(ds + 0xf604);
}
/** The index (0..38) of the first spell number VALUE in casting group GROUP's list, else 39. */
static unsigned spell_list_index(const uint8_t *ds, uint16_t group, uint32_t value)
{
    unsigned k;
    for (k = 0; k < CLASS_SPELL_LIST_SIZE; ++k) {
        if (value == ds[(uint16_t)(XEEN_DS_CLASS_SPELL_LISTS + (uint16_t)(group * XEEN_CLASS_SPELL_LIST) + k)]) {
            break;
        }
    }
    return k;
}
/** Award VALUE's record byte (+39h + slot, 463DE's slots: 49h and 51h are 3Eh and 3Fh, the others
 * VALUE mod 64, signed) holds its bit: the low nibble below 40h, the high nibble from 40h. */
static int has_award(const uint8_t *ds, uint16_t record, uint16_t value)
{
    uint8_t b = ds[(uint16_t)(record + XEEN_CHARACTER_AWARDS + xeen_award_slot(value))];
    return b && (!xeen_award_high_nibble(value) ? (b & 0x0fu) : (b & 0xf0u));
}
static unsigned tg_item(uint32_t v, uint8_t *key);
/** Whether the member has item VALUE (a script item number) in one of the nine rows of its category, or, for a quest
 * item, a nonzero counter. Original: 2C0F6. */
static int has_item(const uint8_t *ds, uint16_t record, uint32_t value)
{
    uint8_t id;
    unsigned i, c, at;
    if (value >= XEEN_QUEST_ITEM_FIRST) {
        return ds[(uint16_t)((uint16_t)value + XEEN_DS_QUEST_ITEMS)] != 0;
    }
    c = tg_item(value, &id);
    at = XEEN_ITEM_KEY_OFFSET(c); /* where a row holds its id: misc byte 0, the others byte 1 */
    for (i = 0; i < XEEN_ITEM_ROWS; ++i) {
        if (ds[(uint16_t)(record + XEEN_CHARACTER_ITEM(c, i) + at)] == id) {
            return 1;
        }
    }
    return 0;
}
/** 2C069: the member knows spell VALUE: its first entry in the casting group's list (F604 selected
 * by class, select_spell_group) has the record's known-spell byte +79h + index set. */
static int knows_spell(uint8_t *ds, uint16_t record, uint32_t value)
{
    unsigned k = spell_list_index(ds, select_spell_group(ds, record), value);
    return k < CLASS_SPELL_LIST_SIZE && ds[(uint16_t)(record + XEEN_CHARACTER_SPELLS + k)];
}
/* The tested value of an absent item or a clear flag: all ones (the condition's own value when present). */
#define IF_ABSENT 0xffffffffu
static uint32_t if_present(int present, uint32_t value)
{
    return present ? value : IF_ABSENT;
}
/*
 * Where the If, the take and the give find the event variables they share. The skills (13) and conditions (18) are
 * record tables indexed by the value's low word; the game flags (20) and the quests (104) are bit arrays with a block
 * per world; the auto notes (103) are one array for both worlds; the character bits (107) belong to the member's
 * roster entry. Every DS offset is 16 bits.
 */
enum {
    GAME_FLAGS_PER_WORLD = 256,
    QUESTS_PER_WORLD = 30,
};
/** The byte at index VALUE's low word of record table TABLE (the skills, the conditions) of the record at RECORD. */
static uint8_t *record_table_byte(uint8_t *ds, uint16_t record, uint16_t table, uint32_t value)
{
    return ds + (uint16_t)(record + table + (uint16_t)value);
}
/**
 * The bit number of flag VALUE in a bit array with PER_WORLD bits for each world: the block of the world (DS 94) plus
 * the value's low word. The original adds them in a 16-bit register (2CB2A: SHL AX, 8; ADD DX, AX). The take and the
 * give pass the sum on as it is and the If reduces it to 16 bits first; they agree for every operand, since the values
 * of these modes are one byte (tg_width) and 255 x 256 + 255 is still below 10000h.
 */
static unsigned world_flag_number(const uint8_t *ds, unsigned per_world, uint32_t value)
{
    return (unsigned)ds[XEEN_DS_WORLD] * per_world + (uint16_t)value;
}
/** The DS offset of the character bits of MEMBER's roster entry (four bytes per roster index). */
static uint16_t character_bits(const uint8_t *ds, unsigned member)
{
    return (uint16_t)(XEEN_DS_CHARACTER_BITS + 4u * ds[(uint16_t)(XEEN_DS_PARTY_MEMBERS + member)]);
}
/** Adds VALUE's low byte to the byte FIELD of the member record at RECORD (wrapping, as the original's byte ADD). */
static void add_record_byte(uint8_t *ds, uint16_t record, uint16_t field, uint32_t value)
{
    ds[(uint16_t)(record + field)] = (uint8_t)(ds[(uint16_t)(record + field)] + (uint8_t)value);
}
/** Subtracts VALUE's low byte from the byte FIELD of the member record at RECORD (wrapping, as the byte SUB). */
static void subtract_record_byte(uint8_t *ds, uint16_t record, uint16_t field, uint32_t value)
{
    ds[(uint16_t)(record + field)] = (uint8_t)(ds[(uint16_t)(record + field)] - (uint8_t)value);
}
/*
 * The tested value of a condition only the host can compute, from its answer HOST_AX, for VALUE and the member at
 * RECORD: 44 (the yes/no question) VALUE itself, but 2 when VALUE and the answer are both 0; the maximum HP and SP
 * whether the maximum is at least the current value (1/0); 99 VALUE when the answer is nonzero, else absent;
 * 102 the answer sign-extended; the others (10, 86..92) the answer as it is.
 * Original: 2C25D 2F2FF(value, 0); 4660F, 2AAC2, 46470 (JB: 0, else 1), 2BD94, 2BEF9, 3CF5B (2C486 CDQ); 2BFE0's
 * high word 0.
 */
static uint32_t host_condition(const uint8_t *ds, unsigned cond, uint32_t value, uint16_t record,
                               uint16_t host_ax)
{
    uint32_t result;
    switch (cond) {
        case IF_YES_NO: /* 2C25D: 2F2FF(value, 0), the yes/no modal */
            result = (uint32_t)(int32_t)(int16_t)host_ax;
            return value || result ? value : 2u;
        case VAR_MAX_HP: /* the maximum (2AAC2, 46470) at least the current value: JB 0, else 1 */
        case VAR_MAX_SP:
            return host_ax >=
                   xeen_get_le16(ds + (uint16_t)(record + (cond == VAR_MAX_HP ? XEEN_CHARACTER_HP : XEEN_CHARACTER_SP)));
        case 99:
            return if_present(host_ax != 0, value);
        case 102:
            return (uint32_t)(int32_t)(int16_t)host_ax; /* 2C486 CDQ */
        default:
            return host_ax; /* 10, 86..92 (2BFE0: hi 0) */
    }
}
/**
 * The comparison 2C4C4 makes when a branch wrote no result (an unlisted condition, the false paths of
 * 13, 15, 18, 19): the stack slot's residue RES (see if_residue) against VALUE by MODE. Returns
 * 1/0, or -2 when the outcome depends on what the residue does not know (a slot a deeper call
 * overwrote, or the unknown high word of SI where the two possible answers differ).
 */
static int compare_residue(unsigned mode, uint32_t value, const if_residue *res)
{
    uint32_t low;
    int r0, r1;
    if (res->lost) {
        return -2;
    }
    if (res->hi_known) {
        return if_compare(mode, res->slot, value);
    }
    low = res->slot & 0xffffu;
    r0 = if_compare(mode, low, value);
    r1 = if_compare(mode, low | 0xffff0000u, value);
    if (mode == 1u) {
        return low != (uint16_t)value ? 0 : -2;
    }
    return r0 == r1 ? r0 : -2;
}
/* How an If test reads a plain variable: a byte, a byte tested for nonzero, a word, a signed word (sign-extended, as
 * the original's CDQ) or a dword. */
enum { READ_BYTE, READ_NONZERO, READ_WORD, READ_SIGNED_WORD, READ_DWORD };
typedef struct {
    uint8_t variable, read; /* the event variable, its READ_ kind */
    uint16_t at;            /* a record offset (member_variables) or a DS offset (party_variables) */
} plain_variable;
/* The member's plain variables, read from the member's record. */
static const plain_variable member_variables[] = {
        {VAR_SEX, READ_BYTE, XEEN_CHARACTER_SEX},
        {VAR_RACE, READ_BYTE, XEEN_CHARACTER_RACE},
        {VAR_CLASS, READ_BYTE, XEEN_CHARACTER_CLASS},
        {VAR_HP, READ_SIGNED_WORD, XEEN_CHARACTER_HP},
        {VAR_SP, READ_SIGNED_WORD, XEEN_CHARACTER_SP},
        {VAR_LEVEL_BONUS, READ_BYTE, XEEN_CHARACTER_LEVEL_BONUS},
        {VAR_EXPERIENCE, READ_DWORD, XEEN_CHARACTER_EXPERIENCE},
        {VAR_AC_BONUS, READ_BYTE, XEEN_CHARACTER_AC_BONUS},
};
/* The party's plain variables (25 the minute of the day, 105 the byte F091: numeric, see the enum's note). */
static const plain_variable party_variables[] = {
        {VAR_PROTECTION_POISON, READ_WORD, XEEN_DS_PROTECTION_POISON},
        {25, READ_WORD, XEEN_DS_MINUTE_OF_DAY},
        {VAR_GOLD, READ_DWORD, XEEN_DS_PARTY_GOLD},
        {VAR_GEMS, READ_DWORD, XEEN_DS_PARTY_GEMS},
        {VAR_FOOD, READ_WORD, XEEN_DS_PARTY_FOOD},
        {VAR_LEVITATE, READ_NONZERO, XEEN_DS_EFFECT_LEVITATE},
        {VAR_LIGHT, READ_WORD, XEEN_DS_PARTY_LIGHT},
        {VAR_PROTECTION_FIRE, READ_WORD, XEEN_DS_PROTECTION_FIRE},
        {VAR_PROTECTION_ELECTRICITY, READ_WORD, XEEN_DS_PROTECTION_ELECTRICITY},
        {VAR_PROTECTION_COLD, READ_WORD, XEEN_DS_PROTECTION_COLD},
        {VAR_DAY, READ_SIGNED_WORD, XEEN_DS_DAY},
        {VAR_WIZARD_EYE, READ_BYTE, XEEN_DS_EFFECT_WIZARD_EYE},
        {VAR_DIRECTION, READ_BYTE, XEEN_DS_PARTY_DIRECTION},
        {VAR_YEAR, READ_WORD, XEEN_DS_YEAR},
        {VAR_WALK_ON_WATER, READ_BYTE, XEEN_DS_EFFECT_WALK_ON_WATER},
        {105, READ_BYTE, 0xf091},
};
/* The value of plain variable COND when VARS (N entries, offsets from BASE: a record, or 0 for the DS) lists it:
 * into *RESULT, returning 1; 0 when it is not listed. */
static int plain_variable_value(const uint8_t *ds, const plain_variable *vars, size_t n, uint16_t base, unsigned cond,
                                uint32_t *result)
{
    for (size_t i = 0; i < n; ++i) {
        if (vars[i].variable == cond) {
            const uint8_t *at = ds + (uint16_t)(base + vars[i].at);
            *result = vars[i].read == READ_BYTE          ? *at
                      : vars[i].read == READ_NONZERO     ? *at != 0u
                      : vars[i].read == READ_WORD        ? xeen_get_le16(at)
                      : vars[i].read == READ_SIGNED_WORD ? (uint32_t)(int32_t)(int16_t)xeen_get_le16(at)
                                                         : xeen_get_le32(at);
            return 1;
        }
    }
    return 0;
}
/* What an If condition's value stage (2BF6E's switch) produced: a value, no value (a presence test that failed or a
 * condition the switch does not list: the comparison reads the slot's residue), or a value only the host has. */
typedef enum { IF_VALUE, IF_NO_VALUE, IF_HOST_VALUE } if_value_kind;
/**
 * The value an If condition tests (2BF6E by CS:0BAB, conditions 3..107; the others go straight to the compare
 * 2C4C4): event variable COND of MEMBER (or of the party), into *RESULT for IF_VALUE. The tested value is the record's
 * or the party's variable, or for the presence tests (skill 13, award 15, condition 18, spell 19, item 21, the flag
 * bits 20/103/104/107) the value itself when present. The plain variables are the tables above (member_variables,
 * party_variables: where each is read and how wide); the switch keeps the computed ones (the age, the day mod 10),
 * the presence tests and the host's, which need HOST_AX (the host's AX; NULL: no host yet, IF_HOST_VALUE before any
 * read). Condition 19 may write F604 (select_spell_group).
 */
static if_value_kind if_tested_value(uint8_t *ds, unsigned cond, uint32_t value, uint16_t member,
                                     const uint16_t *host_ax, uint32_t *result)
{
    uint16_t rec = (uint16_t)(XEEN_PARTY_RECORDS + (uint16_t)(member * XEEN_CHARACTER_BYTES)),
             lo = (uint16_t)value, hi = (uint16_t)(value >> 16);
    switch (cond) {
        /* The member's age, computed (the plain variables are below, in default). */
        case VAR_AGE: { /* 2BAC7(record, 0): the years since birth, at most 254, plus the bonus */
            int16_t years =
                    (int16_t)(xeen_get_le16(ds + XEEN_DS_YEAR) -
                              xeen_get_le16(ds + (uint16_t)(rec + XEEN_CHARACTER_BIRTH_YEAR)));
            if (years > 254) {
                years = 254;
            }
            *result = (uint16_t)((uint16_t)years + ds[(uint16_t)(rec + XEEN_CHARACTER_AGE_BONUS)]);
            return IF_VALUE;
        }
        /* Presence tests: the value when present, else no value (the residue compare). */
        case VAR_SKILL: /* 2C000 */
            *result = value;
            return *record_table_byte(ds, rec, XEEN_CHARACTER_SKILLS, value) != 0 ? IF_VALUE : IF_NO_VALUE;
        case VAR_AWARD: /* 46384 */
            *result = value;
            return has_award(ds, rec, lo) ? IF_VALUE : IF_NO_VALUE;
        case VAR_CONDITION: /* 2C043; 16 (any condition) always */
            *result = value;
            return (*record_table_byte(ds, rec, XEEN_CHARACTER_CONDITIONS, value) || (!hi && lo == 16u)) ? IF_VALUE
                                                                                                           : IF_NO_VALUE;
        case VAR_SPELL:
            *result = value;
            return knows_spell(ds, rec, value) ? IF_VALUE : IF_NO_VALUE;
        /* An item carried and the flag bits (the world's game flags and quests, the auto-notes, the member's roster
         * bits): the value when present, else IF_ABSENT. */
        case VAR_ITEM:
            *result = if_present(has_item(ds, rec, value), value);
            return IF_VALUE;
        case VAR_GAME_FLAG:
            *result = if_present(
                    xeen_ds_get_bit(ds, XEEN_DS_GAME_FLAGS, (uint16_t)world_flag_number(ds, GAME_FLAGS_PER_WORLD, value)),
                    value);
            return IF_VALUE;
        case VAR_AUTO_NOTE:
            *result = if_present(xeen_ds_get_bit(ds, XEEN_DS_AUTO_NOTES, lo), value);
            return IF_VALUE;
        case VAR_QUEST:
            *result = if_present(
                    xeen_ds_get_bit(ds, XEEN_DS_QUEST_FLAGS, (uint16_t)world_flag_number(ds, QUESTS_PER_WORLD, value)),
                    value);
            return IF_VALUE;
        case VAR_CHARACTER_BIT:
            *result = if_present(xeen_ds_get_bit(ds, character_bits(ds, member), lo), value);
            return IF_VALUE;
        /* The party's day mod 10 (the IDIV remainder). */
        case IF_DAY_MOD_10:
            *result = (uint32_t)(int32_t)(int16_t)((int16_t)xeen_get_le16(ds + XEEN_DS_DAY) % 10);
            return IF_VALUE;
        /* The host's. */
        case 10:
        case IF_YES_NO:
        case VAR_MAX_HP:
        case VAR_MAX_SP:
        case 86:
        case 87:
        case 88:
        case 89:
        case 90:
        case 91:
        case 92:
        case 99:
        case 102:
            if (!host_ax) {
                return IF_HOST_VALUE;
            }
            *result = host_condition(ds, cond, value, rec, *host_ax);
            return IF_VALUE;
        default:
            if (plain_variable_value(ds, member_variables, sizeof member_variables / sizeof *member_variables, rec,
                                     cond, result) ||
                plain_variable_value(ds, party_variables, sizeof party_variables / sizeof *party_variables, 0, cond,
                                     result)) {
                return IF_VALUE; /* a plain variable of the member or the party */
            }
            if (record_byte_of(cond)) { /* a stat, resistance or level byte of the record */
                *result = ds[(uint16_t)(rec + record_byte_of(cond))];
                return IF_VALUE;
            }
            return IF_NO_VALUE; /* not listed: the leftover is compared (2C4C4) */
    }
}
/**
 * Evaluates an If condition: the value of COND for MEMBER (if_tested_value; HOST_AX as there) against VALUE by MODE
 * (if_compare, 2C4C4). A value becomes the stack slot RES (its high word then known, not lost); without one the
 * comparison is the slot's residue (compare_residue), the slot unchanged. Returns 1/0, -1 for a host condition
 * without HOST_AX, -2 for a residue the native cannot resolve (an explicit stop at 2C4C4).
 */
static int if_condition(uint8_t *ds, unsigned cond, uint32_t value, unsigned mode, uint16_t member,
                        if_residue *res, const uint16_t *host_ax)
{
    uint32_t result = 0;
    switch (if_tested_value(ds, cond, value, member, host_ax, &result)) {
        case IF_HOST_VALUE:
            return -1;
        case IF_NO_VALUE:
            return compare_residue(mode, value, res); /* the slot keeps its residue */
        case IF_VALUE:
            break;
    }
    res->slot = result;
    res->hi_known = 1;
    res->lost = 0;
    return if_compare(mode, result, value);
}
static int request(xeen_event_execution *, xeen_event_request_fn, xeen_event_service_fn, void *,
                   uint32_t, uint16_t, uint16_t, uint16_t, uint16_t, uint8_t *, size_t, uint16_t *);
/* What a take or give needs to ask the host for: an item row cleared and the list sorted, an item generated, the
 * temporaries reset. Each is served at once or is a stop. Original: 2C94C's calls 43385/43428, 305E8, 29168. */
typedef struct {
    xeen_event_execution *e;
    xeen_event_request_fn fn;
    void *u;
    size_t dn;
} tg_host;
/**
 * Asks the host to run the original routine at ADDRESS for a take/give mode; returns nonzero when
 * it ran.
 */
static int tg_call(const tg_host *h, uint32_t address, uint16_t a, uint16_t b, uint16_t d,
                   uint8_t *ds, uint16_t *ax)
{
    uint16_t unused = 0;
    return h && h->e &&
           request(h->e, h->fn, NULL, h->u, address, a, b, d, 0, ds, h->dn, ax ? ax : &unused) == 1;
}
/* A script item number's category (0..3) and its id within it (KEY); 4 for a quest item, whose KEY is the number
 * (its counter at F025 + number). Original: 2CB3F.., 2D1C8.. */
static unsigned tg_item(uint32_t v, uint8_t *key)
{
    static const uint8_t limit[4] = {XEEN_SCRIPT_ITEM_ARMOR, XEEN_SCRIPT_ITEM_ACCESSORIES, XEEN_SCRIPT_ITEM_MISC,
                                     XEEN_QUEST_ITEM_FIRST},
                         base[4] = {0, XEEN_SCRIPT_ITEM_ARMOR, XEEN_SCRIPT_ITEM_ACCESSORIES, XEEN_SCRIPT_ITEM_MISC};
    unsigned c;
    for (c = 0; c < 4u; ++c) {
        if (v < limit[c]) {
            *key = (uint8_t)(v - base[c]);
            return c;
        }
    }
    *key = (uint8_t)v;
    return 4u;
}
/* The pending treasure (9A10's lists): ten 4-byte rows per category, a row free when its key (misc byte 0,
 * else the id +1) is 0 (xeen_treasure_free_row, xeen/treasure.h). */
/*
 * take_give's results. DONE and REFUSED are the original 2C94C's own return values (AX 0 from
 * 2DC50, AX 1 from 2C992); the callers that read them end a party-wide loop or the script on
 * REFUSED. SHORT and UNSUPPORTED exist only in the native.
 */
enum {
    TAKE_GIVE_UNSUPPORTED = -1, /* a mode or host service not reconstructed: *stop says where */
    TAKE_GIVE_DONE = 0,
    TAKE_GIVE_REFUSED = 1, /* an item the member lacks, no free pending row, an absent member */
    /* Take 34/35: less gold or gems than asked. Nothing is taken; the original shows 291FB's
     * "Not enough" message and returns REFUSED, which the native caller does as a host request. */
    TAKE_GIVE_SHORT = 2,
    /* give's alone, never take_give's: done, and the member's portrait flashes (the exit 2DBB8, which runs 2B9C7);
     * take_give returns DONE with the member in *flash. */
    GIVE_FLASHED = 3,
};
/*
 * Take an item (variable 21): the member's first item of the category whose key (a misc item's first byte, else the
 * id) is the value is removed by the host (cleared, then the category re-sorted). A quest item's counter is
 * decremented instead (it can wrap: no test). No such item: REFUSED.
 * Original: 2CB3A; 43385(record, row, category), 43428(record, category).
 */
static int take_item(uint8_t *ds, uint16_t record, uint32_t value, uint32_t *stop,
                     const tg_host *host)
{
    uint8_t key;
    unsigned c = tg_item(value, &key), row;
    if (c == 4u) {
        --ds[(uint16_t)(XEEN_DS_QUEST_ITEMS + (uint16_t)value)];
        return TAKE_GIVE_DONE;
    }
    for (row = 0; row < XEEN_ITEM_ROWS; ++row) {
        if (ds[(uint16_t)(record + XEEN_CHARACTER_ITEM(c, row) +
                          XEEN_ITEM_KEY_OFFSET(c))] == key) {
            break;
        }
    }
    if (row == XEEN_ITEM_ROWS) {
        return TAKE_GIVE_REFUSED;
    }
    if (!tg_call(host, XEEN_REQUEST_ITEM_CLEAR, record, (uint16_t)row, (uint16_t)c, ds, NULL) ||
        !tg_call(host, XEEN_REQUEST_ITEM_SORT, record, 0, (uint16_t)c, ds, NULL)) {
        *stop = TAKE_STOP_ITEM_REMOVE;
        return TAKE_GIVE_UNSUPPORTED;
    }
    return TAKE_GIVE_DONE;
}
/*
 * Take gold or gems (variables 34, 35): the party's 32-bit count at AT minus the value. With too little (compared
 * unsigned) nothing is taken and SHORT tells the caller to show "Not enough %s in the %s!" first; the take then
 * counts as refused.
 * Original: 2CC9A, 2CCB8: 29281(0 gold / 1 gems, value, 0, 3); the test 292B8, 29338 (high word first); 291FB in
 * 18F2F mode 3; 2CCB5/2CCD3 to 2C992 (AX 1).
 */
static int take_currency(uint8_t *ds, uint16_t at, uint32_t value)
{
    uint32_t have = xeen_get_le32(ds + at);
    if (have < value) {
        return TAKE_GIVE_SHORT;
    }
    xeen_put_le32(ds + at, have - value);
    return TAKE_GIVE_DONE;
}
/**
 * Takes VALUE of event variable MODE from the member at RECORD. Returns DONE (go on with the give), REFUSED, SHORT
 * or UNSUPPORTED (*STOP set). Each mode's original handler is listed in src/events/take_give_modes.json (checked
 * against the image). Original: the take half of 2C94C (2CA01 by mode - 8).
 */
static int take(uint8_t *ds, unsigned mode, uint32_t value, uint16_t record, uint32_t *stop,
                const tg_host *host)
{
    switch (mode) {
        case 0:
            return TAKE_GIVE_DONE; /* nothing to take */
        case VAR_AGE:
            subtract_record_byte(ds, record, XEEN_CHARACTER_AGE_BONUS, value);
            return TAKE_GIVE_DONE;
        case VAR_SKILL:
            *record_table_byte(ds, record, XEEN_CHARACTER_SKILLS, value) = 0;
            return TAKE_GIVE_DONE;
        case VAR_CONDITION:
            *record_table_byte(ds, record, XEEN_CHARACTER_CONDITIONS, value) = 0;
            return TAKE_GIVE_DONE;
        case VAR_GAME_FLAG:
            xeen_ds_set_bit(ds, XEEN_DS_GAME_FLAGS, world_flag_number(ds, GAME_FLAGS_PER_WORLD, value), 0);
            return TAKE_GIVE_DONE;
        case VAR_QUEST:
            xeen_ds_set_bit(ds, XEEN_DS_QUEST_FLAGS, world_flag_number(ds, QUESTS_PER_WORLD, value), 0);
            return TAKE_GIVE_DONE;
        case VAR_AUTO_NOTE:
            xeen_ds_set_bit(ds, XEEN_DS_AUTO_NOTES, (uint16_t)value, 0);
            return TAKE_GIVE_DONE;
        case VAR_ITEM:
            return take_item(ds, record, value, stop, host);
        case VAR_GOLD:
            return take_currency(ds, XEEN_DS_PARTY_GOLD, value);
        case VAR_GEMS:
            return take_currency(ds, XEEN_DS_PARTY_GEMS, value);
        default:
            *stop = TAKE_STOP_MODE;
            return TAKE_GIVE_UNSUPPORTED;
    }
}
/*
 * Give a condition (variable 18; SetVar uses the same rule): 16 clears all sixteen, In Love is set to 1, any other is
 * incremented; then Dead, Stone or Eradicated take a positive HP to 0.
 * Original: 2D0C2 (13FE7 for 16).
 */
static void give_condition(uint8_t *ds, uint16_t record, uint32_t value)
{
    if (value == 16u) {
        memset(ds + (uint16_t)(record + XEEN_CHARACTER_CONDITIONS), 0, 16);
    } else if (value == 6u) {
        ds[(uint16_t)(record + XEEN_CHARACTER_CONDITION(XEEN_CONDITION_IN_LOVE))] = 1;
    } else {
        ++*record_table_byte(ds, record, XEEN_CHARACTER_CONDITIONS, value);
    }
    if (xeen_condition_is_deathlike(value) &&
        (int16_t)xeen_get_le16(ds + (uint16_t)(record + XEEN_CHARACTER_HP)) > 0) {
        xeen_put_le16(ds + (uint16_t)(record + XEEN_CHARACTER_HP), 0);
    }
}
/*
 * The give of a record byte (the temporary stats, stats, resistances and the level, variables 37..64): the byte plus
 * the whole 32-bit value; a sum above 255 makes the byte 255, else it gains the value's low byte.
 * Original: 2D2EF (temporary stats), 2D485 (stats), 2D61B (resistances), 2D903 (the level); the sum in DX:AX.
 */
static void give_saturating(uint8_t *ds, uint16_t at, uint32_t value)
{
    if ((uint32_t)(ds[at] + value) > 0xffu) {
        ds[at] = 0xff;
    } else {
        ds[at] = (uint8_t)(ds[at] + (uint8_t)value);
    }
}
/*
 * Give an item (variable 21): its key (a misc item's first byte, else the id) goes into the first free row of the
 * category's pending-treasure list, and treasure is marked pending. A quest item adds 1 to its counter and then,
 * an original quirk, also goes into the weapons' list with the value's low byte. REFUSED for a member outside the
 * party (tested first) or a full list (the loop over the remaining members repeats the same test, so it refuses
 * too).
 * Original: 2D1C3; 9A10 = 1 (2DACF); 2D2AE's jump to 2D1D6; 2D2B2; the loop 2D2B1.
 */
static int give_item(uint8_t *ds, unsigned member, uint32_t value)
{
    uint8_t key;
    unsigned c = tg_item(value, &key);
    int k;
    if (member >= ds[XEEN_DS_PARTY_COUNT]) {
        return TAKE_GIVE_REFUSED;
    }
    if (c == 4u) {
        ++ds[(uint16_t)(XEEN_DS_QUEST_ITEMS + (uint16_t)value)];
        c = 0;
    }
    if ((k = xeen_treasure_free_row(ds, c)) < 0) {
        return TAKE_GIVE_REFUSED;
    }
    ds[(uint16_t)(xeen_treasure_list(c) + 4u * (unsigned)k + XEEN_ITEM_KEY_OFFSET(c))] = key;
    ds[XEEN_DS_TREASURE_ITEMS_PENDING] = 1;
    return TAKE_GIVE_DONE;
}
/*
 * Give a random item (variable 66): the host generates an item of the value's level into row 0 of its category in
 * the member's record (three bytes), which is copied to the first free pending-treasure row; treasure is marked
 * pending. When a fixed item was asked for (XEEN_DS_FIXED_ITEM, set by operation 45 and cleared at each dispatch)
 * the generator makes that item instead (reason 12) and a free misc row is needed first. REFUSED: no free row, or
 * a category above 3.
 * Original: 2D946; 305E8(value, record, 0, reason) into BC6A; 8C74 (310C9), reason 0Ch at 30644; 9A10 = 1.
 */
static int give_random_item(uint8_t *ds, uint16_t record, uint32_t value, uint32_t *stop,
                            const tg_host *host)
{
    uint16_t c = 0, reason = 0;
    int k;
    if (xeen_get_le16(ds + XEEN_DS_FIXED_ITEM)) {
        if (xeen_treasure_free_row(ds, 3) < 0) {
            return TAKE_GIVE_REFUSED;
        }
        reason = 12;
    }
    if (!tg_call(host, XEEN_REQUEST_GENERATE_ITEM, (uint16_t)(uint8_t)value, reason, record, ds, &c)) {
        *stop = GIVE_STOP_ITEM_GENERATE;
        return TAKE_GIVE_UNSUPPORTED;
    }
    if (c > 3u || (k = xeen_treasure_free_row(ds, c)) < 0) {
        return TAKE_GIVE_REFUSED;
    }
    memcpy(ds + (uint16_t)(xeen_treasure_list(c) + 4u * (unsigned)k),
           ds + (uint16_t)(record + XEEN_CHARACTER_ITEM(c, 0)), 3);
    ds[XEEN_DS_TREASURE_ITEMS_PENDING] = 1;
    return TAKE_GIVE_DONE;
}
/*
 * Give years (variable 85): the year plus the value; four of Clouds' game flags are cleared (37h, 9Bh, DEh and E7h),
 * the host renews the members' temporaries, and the party counts as fed.
 * Original: 2DBDC; 2B983; 29168; F01E = 1.
 */
static int give_years(uint8_t *ds, uint32_t value, uint32_t *stop, const tg_host *host)
{
    xeen_put_le16(ds + XEEN_DS_YEAR,
                  (uint16_t)(xeen_get_le16(ds + XEEN_DS_YEAR) + (uint16_t)value));
    xeen_ds_set_bit(ds, XEEN_DS_GAME_FLAGS, 0x37u, 0);
    xeen_ds_set_bit(ds, XEEN_DS_GAME_FLAGS, 0x9bu, 0);
    xeen_ds_set_bit(ds, XEEN_DS_GAME_FLAGS, 0xdeu, 0);
    xeen_ds_set_bit(ds, XEEN_DS_GAME_FLAGS, 0xe7u, 0);
    if (!tg_call(host, XEEN_REQUEST_RESET_TEMPORARIES, 0, 0, 0, ds, NULL)) {
        *stop = GIVE_STOP_YEARS_TEMPORARIES;
        return TAKE_GIVE_UNSUPPORTED;
    }
    ds[XEEN_DS_PARTY_FED] = 1;
    return TAKE_GIVE_DONE;
}
/*
 * Give days (variable 76): the host advances the clock by the value's days (in minutes, the low word of value x
 * 1440), with the calendar, the shops' new wares and the bank's interest as at a new day. In an event (UI mode 9)
 * the clock's "The Party needs rest!" branch is skipped; should the host report it anyway, that is an explicit stop.
 * Original: 2DB58; 30CF3 (x 5A0h minutes), 41D20, 30DD2 (a new day and more than 1440 minutes, or day % 10 = 1),
 * 9842; 30D65's 29168 / weak / rest branch (AX 1).
 */
static int give_days(uint8_t *ds, uint32_t value, uint32_t *stop, const tg_host *host)
{
    uint16_t rest = 0;
    if (!tg_call(host, XEEN_REQUEST_CLOCK_ADVANCE, (uint16_t)(value * 0x5a0u), 0, 0, ds, &rest)) {
        *stop = GIVE_STOP_DAYS_CLOCK;
        return TAKE_GIVE_UNSUPPORTED;
    }
    if (rest) {
        *stop = GIVE_STOP_DAYS_REST;
        return TAKE_GIVE_UNSUPPORTED;
    }
    return TAKE_GIVE_DONE;
}
/** Give 78 or 81: the member's HP or SP set to its maximum (computed by the host). Original: 2DB82, 2DBA2 (2AAC2,
 * 46470). */
static int give_maximum(uint8_t *ds, unsigned mode, uint16_t record, uint32_t *stop,
                        const tg_host *host)
{
    uint16_t max = 0;
    if (!tg_call(host, mode == VAR_MAX_HP ? XEEN_REQUEST_MAXIMUM_HP : XEEN_REQUEST_MAXIMUM_SP, record, 0, 0, ds, &max)) {
        *stop = mode == VAR_MAX_HP ? GIVE_STOP_MAXIMUM_HP : GIVE_STOP_MAXIMUM_SP;
        return TAKE_GIVE_UNSUPPORTED;
    }
    if (mode == VAR_MAX_HP) {
        XEEN_DAMAGE_NUMBERS_EVENT(ds, record, max, 1); /* add-on hook */
    }
    xeen_put_le16(ds + (uint16_t)(record + (mode == VAR_MAX_HP ? XEEN_CHARACTER_HP : XEEN_CHARACTER_SP)), max);
    return TAKE_GIVE_DONE;
}
/*
 * Give a spell (variable 19): the member learns it when it is in their casting group's list (the record's known-spell
 * byte at its index; the caller then flashes the portrait); a spell not in the list: nothing. Original: 2D14B (+79h).
 */
static int give_spell(uint8_t *ds, uint16_t record, uint32_t value)
{
    unsigned k = spell_list_index(ds, select_spell_group(ds, record), value);
    if (k == CLASS_SPELL_LIST_SIZE) {
        return 0;
    }
    ds[(uint16_t)(record + XEEN_CHARACTER_SPELLS + k)] = 1;
    return 1;
}
/**
 * Gives VALUE of event variable MODE to MEMBER (record RECORD): DONE, or GIVE_FLASHED when the give ends in the
 * member's portrait flash; REFUSED and UNSUPPORTED (*STOP set) as for the take. Each mode's original handler is
 * listed in src/events/take_give_modes.json.
 * Original: the give half of 2C94C (2CFD9, by mode - 3); its ends 2DC50 and, with the flash, 2DBB8.
 */
static int give(uint8_t *ds, unsigned mode, uint32_t value, unsigned member, uint16_t record, uint32_t *stop,
                const tg_host *host)
{
    int outcome;
    switch (mode) {
        case 0:
            return TAKE_GIVE_DONE;
        case VAR_HP: /* 2D005: the one give whose 2B9C7 comes first: the flash, then the caller
                      * adds the value's low word to HP (at once when 2B9C7 would return at its
                      * 8C78 test), then 2DC50 */
            return GIVE_FLASHED;
        case VAR_SP:
            add_le16(ds + (uint16_t)(record + XEEN_CHARACTER_SP), value);
            return GIVE_FLASHED;
        case VAR_LEVEL_BONUS:
            add_record_byte(ds, record, XEEN_CHARACTER_LEVEL_BONUS, value);
            return GIVE_FLASHED;
        case VAR_AGE: /* no flash */
            add_record_byte(ds, record, XEEN_CHARACTER_AGE_BONUS, value);
            return TAKE_GIVE_DONE;
        case VAR_SKILL:
            ++*record_table_byte(ds, record, XEEN_CHARACTER_SKILLS, value);
            return GIVE_FLASHED;
        case VAR_AWARD: /* award 8 without the flash */
            award(ds, member, (uint16_t)value, 1);
            if (!(value >> 16) && (uint16_t)value == 8u) {
                return TAKE_GIVE_DONE;
            }
            return GIVE_FLASHED;
        case VAR_EXPERIENCE:
            add_le32(ds + record + XEEN_CHARACTER_EXPERIENCE, value);
            return GIVE_FLASHED;
        case VAR_CONDITION:
            give_condition(ds, record, value);
            return TAKE_GIVE_DONE;
        case VAR_SPELL:
            return give_spell(ds, record, value) ? GIVE_FLASHED : TAKE_GIVE_DONE;
        case VAR_GAME_FLAG:
            xeen_ds_set_bit(ds, XEEN_DS_GAME_FLAGS, world_flag_number(ds, GAME_FLAGS_PER_WORLD, value), 1);
            return TAKE_GIVE_DONE;
        case VAR_ITEM:
            return give_item(ds, member, value);
        case VAR_GOLD:
            add_le32(ds + XEEN_DS_PARTY_GOLD, value);
            return GIVE_FLASHED;
        case VAR_GEMS:
            add_le32(ds + XEEN_DS_PARTY_GEMS, value);
            return GIVE_FLASHED;
        case VAR_FOOD:
            add_le16(ds + XEEN_DS_PARTY_FOOD, value);
            return GIVE_FLASHED;
        case VAR_RANDOM_ITEM:
            return give_random_item(ds, record, value, stop, host);
        case 67:
            return TAKE_GIVE_DONE; /* 2DC50, nothing */
        case VAR_DAY:
            return give_days(ds, value, stop, host);
        case VAR_AC_BONUS:
            add_record_byte(ds, record, XEEN_CHARACTER_AC_BONUS, value);
            return GIVE_FLASHED;
        case VAR_MAX_HP:
        case VAR_MAX_SP:
            outcome = give_maximum(ds, mode, record, stop, host);
            return outcome == TAKE_GIVE_DONE ? GIVE_FLASHED : outcome;
        case VAR_YEAR:
            return give_years(ds, value, stop, host);
        case VAR_AUTO_NOTE:
            xeen_ds_set_bit(ds, XEEN_DS_AUTO_NOTES, (uint16_t)value, 1);
            return TAKE_GIVE_DONE;
        case VAR_QUEST:
            xeen_ds_set_bit(ds, XEEN_DS_QUEST_FLAGS, world_flag_number(ds, QUESTS_PER_WORLD, value), 1);
            return TAKE_GIVE_DONE;
        case VAR_CHARACTER_BIT:
            xeen_ds_set_bit(ds, character_bits(ds, member), (uint16_t)value, 1);
            return TAKE_GIVE_DONE;
        default:
            if (record_byte_of(mode)) { /* a stat, resistance or level byte: raised, saturating */
                give_saturating(ds, (uint16_t)(record + record_byte_of(mode)), value);
                return GIVE_FLASHED;
            }
            *stop = GIVE_STOP_MODE;
            return TAKE_GIVE_UNSUPPORTED;
    }
}
/**
 * The take/give core (2C94C): takes TAKE_VALUE of event variable TAKE_MODE from MEMBER, then, unless
 * the take ended the call, gives GIVE_VALUE of GIVE_MODE, for the modes in
 * src/events/take_give_modes.json; returns a TAKE_GIVE_ result. FLASH receives the member whose
 * portrait flashes (only with DONE), else -1; STOP receives the boundary of an unsupported mode or
 * host service.
 */
static int take_give(uint8_t *ds, unsigned take_mode, uint32_t take_value, unsigned give_mode,
                     uint32_t give_value, unsigned member, int *flash, uint32_t *stop,
                     const tg_host *host)
{
    uint16_t record;
    int outcome;
    *flash = -1;
    if (member > 7u) {
        member &= 7u;
        take_mode = 0;
    }
    record = XEEN_PARTY_RECORD(member);
    if (take_mode && !take_value && take_mode != VAR_QUEST) { /* 2C982: the amount prompt */
        *stop = TAKE_GIVE_STOP_AMOUNT_PROMPT;
        return TAKE_GIVE_UNSUPPORTED;
    }
    if (take_mode == give_mode && take_value == give_value) {
        if (give_value) {                                     /* a random amount */
            *stop = TAKE_GIVE_STOP_RANDOM_AMOUNT;
            return TAKE_GIVE_UNSUPPORTED;
        }
        give_mode = 0;
        give_value = 0;
    }
    outcome = take(ds, take_mode, take_value, record, stop, host);
    if (outcome != TAKE_GIVE_DONE) {
        return outcome; /* a refused take gives nothing */
    }
    outcome = give(ds, give_mode, give_value, member, record, stop, host);
    if (outcome == GIVE_FLASHED) {
        *flash = (int)member;
        return TAKE_GIVE_DONE;
    }
    return outcome;
}
xeen_exec_status xeen_event_begin(xeen_event_state *s, const uint8_t *ds, size_t n)
{
    if (!s || !ds || n < 65536u) {
        return XEE_MALFORMED;
    }
    memset(s, 0, sizeof(*s));
    s->x = xeen_sign8(ds[XEEN_DS_PARTY_X]);
    s->y = xeen_sign8(ds[XEEN_DS_PARTY_Y]);
    s->logical_result = XEE_NO_MATCH;
    return XEE_DONE;
}
/* Validate the complete index against the borrowed resource. A forged index
 * cannot make one record borrow operation-specific operands from its neighbor.
 * The original common text-selector read at +6 may cross a row boundary. */
static int valid(const uint8_t *b, size_t n, const xeen_event_index *idx)
{
    size_t p = 0;
    if (n > 65535u || idx->record_count > XEEN_EVENT_MAX_RECORDS) {
        return 0;
    }
    for (size_t i = 0; i < idx->record_count; ++i) {
        const xeen_event_record *r = &idx->records[i];
        size_t len;
        if (p >= n || r->offset != p) {
            return 0;
        }
        len = b[p];
        if (len < 4u || len > 127u || len + 1u > n - p) {
            return 0;
        }
        if (r->field1 != xeen_sign8(b[p + 1]) || r->field2 != xeen_sign8(b[p + 2]) ||
            r->field3 != xeen_sign8(b[p + 3]) || r->field4 != b[p + 4]) {
            return 0;
        }
        p += len + 1u;
    }
    return p == n;
}
/** Ends the script successfully: the executor returns external at the script's end (the original's exit 33690),
 * the logical result DONE. Returns XEE_EXTERNAL for the caller's status. */
static xeen_exec_status script_end(xeen_event_state *s)
{
    s->boundary = XEEN_EVENT_SCRIPT_END;
    s->logical_result = XEE_DONE;
    return XEE_EXTERNAL;
}
/** Jumps the script to LINE, a record's signed byte: the next line is LINE - 1 here, and the dispatcher's common
 * tail steps it on to LINE after the operation (the original's BP-4 = CBW(LINE) - 1 before 3346B). The If
 * operations' unsigned target and the mirror's word are other computations (no CBW) and do not use this. */
static void jump_to_line(xeen_event_state *s, uint8_t line)
{
    s->sequence = (uint16_t)((uint16_t)xeen_sign8(line) - 1u);
}
/**
 * Sends a service request to the host: through the extended request function when EXTENDED is set,
 * else through the legacy service callback.
 */
static int request(xeen_event_execution *extended, xeen_event_request_fn dispatch,
                   xeen_event_service_fn legacy, void *user, uint32_t address, uint16_t argument,
                   uint16_t second, uint16_t destination, uint16_t count, uint8_t *ds, size_t dn,
                   uint16_t *ax)
{
    if (extended) {
        xeen_event_request r = {address, argument, second, destination, count};
        return dispatch ? dispatch(user, &r, ds, dn, ax) : 0;
    }
    return legacy ? legacy(user, address, (uint8_t)argument, ds, dn) : 0;
}
/**
 * One step of the event executor: the record an operation handler runs and what it hands back.
 *
 * Inputs: the event store (its bytes B, length N and record index IDX), the current record R and its operation
 * bytes P (P[0] the record's length, P[5] the operation, P[6].. its operands), DS, the script state S, and the host
 * that serves the original routines a handler needs (EXTENDED and DISPATCH for the extended entries, the legacy copy
 * service COPY otherwise; CU is the host's user pointer).
 *
 * Outputs: AX, the last host answer (some handlers keep or pass it on); BRANCH, the conditional operations' decision
 * for the trace (-1 when the operation has none); RESULT, the step's status. RESULT is XEE_DONE when the handler
 * starts; a handler that cannot finish sets it (a refused request leaves it external or internal with S's
 * boundary at the request) and later stages of the same handler test it before going on.
 */
typedef struct {
    uint8_t *b;
    size_t n;
    const xeen_event_index *idx;
    const xeen_event_record *r;
    const uint8_t *p;
    uint8_t *ds;
    size_t dn;
    xeen_event_state *s;
    xeen_event_execution *extended;
    xeen_event_request_fn dispatch;
    xeen_event_service_fn copy;
    void *cu;
    uint16_t ax;
    int branch;
    xeen_exec_status result;
} exec_step;

/** Asks the step's host to run the original routine SERVICE with its four arguments; its answer's AX goes to *AX.
 * Returns the host's answer: 1 served, 0 suspended (the host runs it outside), anything else failed. */
static int host_request(exec_step *st, uint32_t service, uint16_t argument, uint16_t second, uint16_t destination,
                        uint16_t count, uint16_t *ax)
{
    return request(st->extended, st->dispatch, st->copy, st->cu, service, argument, second, destination, count,
                   st->ds, st->dn, ax);
}
/** host_request() with the answer's AX in the step's own AX. */
static int step_request(exec_step *st, uint32_t service, uint16_t argument, uint16_t second, uint16_t destination,
                        uint16_t count)
{
    return host_request(st, service, argument, second, destination, count, &st->ax);
}
/** Whether the host refused the request at AT (its answer HANDLED is not 1). A refusal ends the step there: external
 * when the host suspended it, internal when it failed, with S's boundary AT. */
static int step_refused(exec_step *st, int handled, uint32_t at)
{
    if (handled != 1) {
        st->result = handled == 0 ? XEE_EXTERNAL : XEE_INTERNAL;
        st->s->boundary = at;
        return 1;
    }
    return 0;
}
/** Asks the host for SERVICE (step_request); returns whether it was served. A refusal ends the step there
 * (step_refused). */
static int step_served(exec_step *st, uint32_t service, uint16_t argument, uint16_t second, uint16_t destination,
                       uint16_t count)
{
    return !step_refused(st, step_request(st, service, argument, second, destination, count), service);
}
/**
 * The scene redraw a dispatch shows once, before the first handler that needs it (the original's flag BP-4E): from
 * the handler phase without a shown scene the step enters SCENE_PHASE (the plain or the damage scene) and asks the
 * host to draw it; once drawn, the scene is marked shown and the handler phase resumes. A refused redraw ends the step
 * at the request (step_refused). Returns the host's answer (1 when no redraw was needed).
 */
static int step_scene_once(exec_step *st, uint8_t scene_phase)
{
    xeen_event_execution *extended = st->extended;
    int handled = 1;
    if (extended->phase == XEEN_EXEC_PHASE_HANDLER && !extended->scene_shown) {
        extended->phase = scene_phase;
    }
    if (extended->phase == scene_phase) {
        handled = step_request(st, XEEN_REQUEST_DRAW_SCENE, 1, 0, 0, 0);
        if (!step_refused(st, handled, XEEN_REQUEST_DRAW_SCENE)) {
            extended->scene_shown = 1;
            extended->phase = XEEN_EXEC_PHASE_HANDLER;
        }
    }
    return handled;
}
/* The end of an ending operation, after the shared tail saved the game (which reloads the event script of the map
 * an ending head may have moved the party to). The operation returns 1 and the script steps to line 257, which no
 * record can have (a record's line is a byte), so the script ends without reading the new store. On the way, unless
 * the party was defeated: with a monster in the first view slot the events are deferred, else they are not and the
 * host restores the map's events.
 * Original: after 33518 (29502(1) ends with 2C88D(ED99)), BP-A = 1, BP-4 = 100h; 33670 (F60A clear) steps to 101h;
 * 313FD: 98B4 not FFh sets 9756 = 1, else 9756 = 0 and the 4A1B7 restore; the end 33690. */
static xeen_exec_status ending_end(exec_step *st)
{
    xeen_event_execution *extended = st->extended;
    xeen_event_state *s = st->s;
    uint8_t *ds = st->ds;
    uint16_t ax = 0;
    extended->return_ax = 1;
    s->sequence = 0x101u;
    if (!ds[XEEN_DS_PARTY_DEFEATED]) {
        if (ds[XEEN_DS_VIEW_MONSTER(0)] != 255u) {
            ds[XEEN_DS_EVENT_DEFERRED] = 1;
        } else {
            int service;
            ds[XEEN_DS_EVENT_DEFERRED] = 0;
            service = host_request(st, XEEN_REQUEST_RESTORE_EVENTS, 0, 0, 0, 0, &ax);
            if (service != 1) {
                s->boundary = XEEN_REQUEST_RESTORE_EVENTS;
                return service == 0 ? XEE_EXTERNAL : XEE_INTERNAL;
            }
        }
    }
    return script_end(s);
}
/* test_with_host's answer when the host could not serve a value. */
#define IF_HOST_FAILED (-3)
/* The request that asks the host for condition COND's value: the yes/no question for 44, else the value. */
static uint32_t condition_request(unsigned cond)
{
    return cond == IF_YES_NO ? XEEN_REQUEST_YES_NO : XEEN_REQUEST_CONDITION_VALUE;
}
/*
 * One member's If test (if_condition) for MEMBER, asking the host for a value the native does not compute
 * (condition_request: the yes/no question with VALUE, or the value of COND for MEMBER with VALUE) and testing again
 * with its AX. Returns 1/0, -2 when the residue cannot be resolved, or IF_HOST_FAILED when the host did not serve
 * it. *HANDLED is written only when the host is asked: host_request's answer (1 served; 0 suspended, the host runs it
 * outside; anything else a host failure). Shared by the If operations and the conditional gives (2BF6E for both).
 */
static int test_with_host(exec_step *st, unsigned cond, uint32_t value, unsigned mode, uint16_t member,
                          if_residue *res, int *handled)
{
    uint16_t host_ax = 0;
    int r = if_condition(st->ds, cond, value, mode, member, res, NULL);
    if (r == -1) {
        *handled = cond == IF_YES_NO
                           ? host_request(st, XEEN_REQUEST_YES_NO, (uint16_t)value, 0, 0, 0, &host_ax)
                           : host_request(st, XEEN_REQUEST_CONDITION_VALUE, (uint16_t)cond, member, (uint16_t)value,
                                          0, &host_ax);
        if (*handled != 1) {
            return IF_HOST_FAILED;
        }
        r = if_condition(st->ds, cond, value, mode, member, res, &host_ax);
    }
    return r;
}
/*
 * An If operation's test for the member WHO (test_with_host). Returns 1/0, -2 when the residue cannot be resolved,
 * or IF_HOST_FAILED with the boundary at the service and st->result XEE_EXTERNAL (the yes/no unanswered) or
 * XEE_UNSUPPORTED (another value), XEE_INTERNAL on a host error.
 */
static int if_member_test(exec_step *st, unsigned cond, uint32_t value, unsigned mode, uint16_t who,
                          if_residue *res)
{
    int handled = 1;
    int r = test_with_host(st, cond, value, mode, who, res, &handled);
    if (r == IF_HOST_FAILED) {
        st->s->boundary = condition_request(cond);
        st->result = handled != 0 ? XEE_INTERNAL : cond == IF_YES_NO ? XEE_EXTERNAL : XEE_UNSUPPORTED;
    }
    return r;
}
/**
 * Operations 8, 9 and 10 (If1..If3, the original's shared handler 31A6D): tests the record's
 * condition (mode >=, = or <= by operation) for the selected member, or ORed over the party, through the
 * host's 2BF6E service (if_member_test); a true test jumps to the record's line.
 */
static int op_if_condition(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    const uint8_t *p = st->p;
    /* The record holds the condition (p6), its value (its width by condition) and the target line. With the whole
     * party selected (0), or everyone but the member the selector names (8), each member is tested and the test is
     * true when any member's is; otherwise only the selected member is. Condition 44, the yes/no question, is asked
     * once whatever the selection. A value the native does not compute for a member comes from the host; when no
     * member's test can be decided, the operation stops. A true test moves the script to the target line. The
     * host's failure to answer the yes/no leaves the step (traced) with its status; any other value's is a stop.
     * Original: 31A6D; the party loop 31B5C..31BB0, the one member BP-C - 1 at 31BBA (selection 0 would be member
     * FFFFh there, never reached), the jump BP-4 = line - 1 then 3346B; the yes/no 2F2FF; the host-side values
     * 4660F, 2AAC2, 46470, 2BD94, 2BEF9 and 3CF5B. */
    unsigned cond = p[6], w = tg_width(cond), mode = s->operation - 8u,
             sel = st->extended->selected_character;
    unsigned count = ds[XEEN_DS_PARTY_COUNT], last = 1, member;
    uint32_t value = tg_value(p + 7, w);
    uint8_t target = p[7u + w];
    if_residue res = {s->offset, 0, 0};
    int any = 0, unknown = 0, loop = (sel == 0u || sel == 8u) && cond != IF_YES_NO;
    if (loop) {
        last = count;
    }
    for (member = 0; member < last; ++member) {
        int r;
        if (loop && sel == 8u && ds[XEEN_DS_MEMBER_SELECTOR] == member) {
            continue;
        }
        r = if_member_test(st, cond, value, mode, loop ? (uint16_t)member : (uint16_t)(sel - 1u), &res);
        if (r == IF_HOST_FAILED) {
            return cond != IF_YES_NO;
        }
        if (r == -2) {
            unknown = 1;
        } else {
            any |= r;
        }
    }
    if (!any && unknown) {
        s->boundary = IF_STOP_UNKNOWN_CONDITION;
        st->result = XEE_UNSUPPORTED;
        return 1;
    }
    st->branch = any;
    if (st->branch) {
        s->sequence = (uint16_t)(target - 1u);
    }
    s->event_flag = 1;
    return 0;
}

/*
 * extended->loop_flash: the step of a take/give loop that is pending across a host suspension
 * (phase 10): none, the member's 2B9C7 portrait flash, or 291FB's "Not enough" message for the
 * gold or the gems (the message's kind is the state - SHORT_GOLD). No other value is stored.
 */
enum {
    LOOP_PENDING_NONE = 0,
    LOOP_PENDING_FLASH = 1,
    LOOP_PENDING_SHORT_GOLD = 3,
    LOOP_PENDING_SHORT_GEMS = 4,
};
/*
 * How a take/give over the whole party goes on after each member's take and give: first by the take's mode, then
 * by the give's; a mode in neither table goes on to the next member. A party-wide quantity (gold, gems, food) is
 * taken once for the party; a member's HP, an item or a random item are tried member by member until one succeeds.
 * Modes 66 (as a take), 82, 100, 101 and 106 have no verified meaning in these tables and stay numeric; the take and
 * give themselves stop for take 8, 65, 66, 100, 101, 106 and give 82, 100, 101, 106 (take_give_modes.json), so
 * those entries are kept for the original's tables, not reached.
 * Original: 31C62's party loop 32139..32213 after each 2C94C: 321A1 by CS:2B1C, then 321DE by CS:2AF8 (nine words
 * each; decoded in xeen_dat_evidence/phase229/analysis/loop_tables.txt, checked against the image by the CTest
 * phase229_party_loop_tables).
 */
enum {
    LOOP_NEXT_MEMBER,    /* in neither table: the next member (Original: 32203) */
    LOOP_ONCE_FOR_PARTY, /* the step ends the loop; refused, it ends the script (Original: 321C7, 33690) */
    LOOP_UNTIL_DONE,     /* the first DONE step ends the loop (Original: 321BC, 321F9) */
};
/** How the party loop goes on after a take of TAKE_MODE (Original: 321A1's table CS:2B1C). */
static int loop_take_kind(unsigned take_mode)
{
    switch (take_mode) {
        case VAR_GOLD:
        case VAR_GEMS:
        case VAR_FOOD:
        case 100:
        case 101:
        case 106:
            return LOOP_ONCE_FOR_PARTY;
        case VAR_HP: /* the loop's take becomes 0 first (Original: 321B7) */
        case VAR_ITEM:
        case 66:
            return LOOP_UNTIL_DONE;
        default:
            return LOOP_NEXT_MEMBER;
    }
}
/** Whether a give of GIVE_MODE ends the party loop at the first DONE step (Original: 321DE's table CS:2AF8,
 * 321F9). */
static int loop_give_until_done(unsigned give_mode)
{
    switch (give_mode) {
        case VAR_ITEM:
        case VAR_GOLD:
        case VAR_GEMS:
        case VAR_FOOD:
        case VAR_RANDOM_ITEM:
        case 82: /* 321F4: the loop's take becomes 0 first */
        case 100:
        case 101:
        case 106:
            return 1;
        default:
            return 0;
    }
}
/**
 * The member loop's next member (31C62: 32139 for BP-C 0 or 8, 32215 for another): SELECTED 0 (the
 * party) or 8 (the party but member 98D7) takes extended->loop_member, skipping 98D7 for 8, until
 * COUNT; another SELECTED is member SELECTED - 1, once. Returns 0 past the last member.
 */
static int loop_next_member(xeen_event_execution *extended, const uint8_t *ds, unsigned selected,
                            unsigned count, unsigned *member)
{
    if (selected != 0u && selected != 8u) {
        *member = selected - 1u;
        return 1;
    }
    while (extended->loop_member < count) {
        if (selected == 8u && ds[XEEN_DS_MEMBER_SELECTOR] == extended->loop_member) {
            ++extended->loop_member;
            continue;
        }
        *member = extended->loop_member;
        return 1;
    }
    return 0;
}
/**
 * A give's 2DBB8 tail flashes the receiving portrait FLASH (-1: none) when it is a party member's
 * (below COUNT) not yet flashed in this dispatch (8C78).
 */
static int loop_flash_due(const uint8_t *ds, int flash, unsigned count)
{
    return flash >= 0 && !ds[XEEN_DS_MEMBER_FLASHED + (unsigned)flash] && (unsigned)flash < count;
}
/** Give 8 (HP) after its portrait flash: MEMBER's HP word + VALUE (the add-on hook first). */
static void loop_give_hp(uint8_t *ds, unsigned member, uint32_t value)
{
    uint16_t hp =
            (uint16_t)(XEEN_PARTY_RECORDS + member * XEEN_CHARACTER_BYTES + XEEN_CHARACTER_HP);
    XEEN_DAMAGE_NUMBERS_EVENT(ds, (uint16_t)(hp - XEEN_CHARACTER_HP),
                              (uint16_t)(xeen_get_le16(ds + hp) + (uint16_t)value),
                              0); /* add-on hook */
    xeen_put_le16(ds + hp, (uint16_t)(xeen_get_le16(ds + hp) + (uint16_t)value));
}
/**
 * The pending portrait flash of MEMBER (2B9C7, the host's), then give GIVE_MODE 8's HP. Returns 1
 * when the host suspends it or fails: the loop stops in phase 10 with the step's result and the
 * boundary 2B9C7, the flash still pending.
 */
static int loop_run_flash(exec_step *st, unsigned member, unsigned give_mode, uint32_t give_value)
{
    xeen_event_execution *extended = st->extended;
    int handled = step_request(st, XEEN_REQUEST_MEMBER_FLASH, (uint16_t)member, 0, 0, 0);
    if (handled != 1) {
        extended->phase = XEEN_EXEC_PHASE_MEMBER_LOOP;
        st->result = handled == 0 ? XEE_EXTERNAL : XEE_INTERNAL;
        st->s->boundary = XEEN_REQUEST_MEMBER_FLASH;
        return 1;
    }
    extended->loop_flash = LOOP_PENDING_NONE;
    if (give_mode == 8u) {
        loop_give_hp(st->ds, member, give_value);
    }
    return 0;
}
/* After one member of operation 12's loop: whether the loop ends (1) or goes on to the next member (0). With one
 * member selected it ends, a refused or short OUTCOME ending the script (*ENDED); a take of a once-for-the-party
 * kind ends it the same way. A take that runs until done ends it at its first DONE (an HP take is cleared after
 * the first member, so the others only receive), and so does a give that runs until done (82's take cleared).
 * Original: 31C62's party loop, its kind switches 321A1/321DE. */
static int take_loop_ends(xeen_event_execution *extended, unsigned selected, unsigned give_mode, int outcome,
                          int *ended)
{
    unsigned t = extended->loop_take;
    int kind;
    if (selected != 0u && selected != 8u) {
        *ended = outcome != TAKE_GIVE_DONE;
        return 1;
    }
    kind = loop_take_kind(t);
    if (kind == LOOP_ONCE_FOR_PARTY) {
        *ended = outcome != TAKE_GIVE_DONE;
        return 1;
    }
    if (kind == LOOP_UNTIL_DONE) {
        if (t == VAR_HP) {
            extended->loop_take = 0;
        }
        if (outcome == TAKE_GIVE_DONE) {
            return 1;
        }
    }
    if (loop_give_until_done(give_mode)) {
        if (give_mode == 82u) {
            extended->loop_take = 0;
        }
        if (outcome == TAKE_GIVE_DONE) {
            return 1;
        }
    }
    return 0;
}
/* What a member's give leaves for the member loop to do about its portrait flash (give_flash). */
enum give_flash {
    GIVE_FLASH_NONE,     /* nothing pending (give 8's HP, when the give named a member, already added) */
    GIVE_FLASH_PENDING,  /* the flash is pending (loop_flash): loop_run_flash flashes, then adds give 8's HP */
    GIVE_FLASH_IN_COMBAT /* a flash is due in combat: the caller stops, nothing was written */
};
/*
 * After a member's give of GIVE_MODE (GIVE_VALUE) that named portrait FLASH (-1: none) in a party of COUNT: a
 * party member's portrait not yet flashed in this dispatch is marked pending, outside combat; otherwise give 8's HP
 * is added at once when the give named a member. Shared by operation 12 and the conditional gives.
 * Original: the give's 2DBB8 tail (2B9C7 the flash, once per member and dispatch: 8C78).
 */
static enum give_flash give_flash(uint8_t *ds, xeen_event_execution *extended, unsigned member, int flash,
                                  unsigned count, unsigned give_mode, uint32_t give_value)
{
    if (loop_flash_due(ds, flash, count)) {
        if (ds[XEEN_DS_COMBAT_MODE] == 2u) {
            return GIVE_FLASH_IN_COMBAT;
        }
        extended->loop_flash = LOOP_PENDING_FLASH;
        return GIVE_FLASH_PENDING;
    }
    if (give_mode == 8u && flash >= 0) {
        loop_give_hp(ds, member, give_value);
    }
    return GIVE_FLASH_NONE;
}
/*
 * A take the party could not pay (loop_flash SHORT_GOLD or SHORT_GEMS): the host's payment refusal, its message and
 * key (291FB with the kind, 0 gold or 1 gems, the party, 0 and mode 3). Returns 1 when the host suspends or fails
 * it: the loop stops in phase 10 at the refusal's boundary, the kind kept in loop_flash for the resumption. Else 0:
 * the refusal was served and nothing is pending. Original: 291FB, then 2C992's AX 1 (the refusal, 33690).
 */
static int loop_run_refusal(exec_step *st)
{
    xeen_event_execution *extended = st->extended;
    int handled = step_request(st, XEEN_REQUEST_PAYMENT_REFUSAL,
                               (uint16_t)(extended->loop_flash - LOOP_PENDING_SHORT_GOLD), 0, 3, 0);
    if (handled != 1) {
        extended->phase = XEEN_EXEC_PHASE_MEMBER_LOOP;
        st->result = handled == 0 ? XEE_EXTERNAL : XEE_INTERNAL;
        st->s->boundary = XEEN_REQUEST_PAYMENT_REFUSAL;
        return 1;
    }
    extended->loop_flash = LOOP_PENDING_NONE;
    return 0;
}
/* Whether the pending step is a payment refusal (loop_flash SHORT_GOLD or SHORT_GEMS) rather than a portrait flash. */
static int loop_refusal_pending(const xeen_event_execution *extended)
{
    return extended->loop_flash >= LOOP_PENDING_SHORT_GOLD;
}
/* The pending refusal of a take the party could not pay: the gold message for take 34, the gems message for take 35
 * (the only takes that come back SHORT). Original: 2CC9A / 2CCB8 pass 0 / 1 to 29281. */
static uint8_t loop_refusal_kind(unsigned take_mode)
{
    return (uint8_t)(take_mode == VAR_GOLD ? LOOP_PENDING_SHORT_GOLD : LOOP_PENDING_SHORT_GEMS);
}
/*
 * Operation 12's pending step for MEMBER, after its take and give or on its resumption: a payment refusal (the
 * outcome becomes REFUSED once it is served) or a portrait flash (then give GIVE_MODE 8's HP), or nothing. Returns 1
 * when the host suspends or fails the step: the loop stops in phase 10, the step still pending.
 */
static int loop_run_pending(exec_step *st, unsigned member, unsigned give_mode, uint32_t give_value, int *outcome)
{
    if (loop_refusal_pending(st->extended)) {
        if (loop_run_refusal(st)) {
            return 1;
        }
        *outcome = TAKE_GIVE_REFUSED;
        return 0;
    }
    return st->extended->loop_flash != LOOP_PENDING_NONE && loop_run_flash(st, member, give_mode, give_value);
}
/* The end of a member loop (operation 12, the conditional gives) that was not suspended or stopped (st->result still
 * XEE_DONE): back to the handler's phase with the event flag set, and the script ends when ENDED. */
static void member_loop_finished(exec_step *st, int ended)
{
    if (st->result == XEE_DONE) {
        st->extended->phase = XEEN_EXEC_PHASE_HANDLER;
        st->s->event_flag = 1;
        if (ended) {
            st->result = script_end(st->s);
        }
    }
}
/* Operation 12's operands: the take pair, then the give pair; each a mode byte and a value of the mode's width
 * (tg_width), so the give pair's position depends on the take's mode. */
typedef struct {
    unsigned take_mode, give_mode;
    uint32_t take_value, give_value;
} take_or_give_pairs;

static take_or_give_pairs take_or_give_operands(const uint8_t *p)
{
    take_or_give_pairs o;
    size_t at = operand_pair(p, 6u, &o.take_mode, &o.take_value);
    (void)operand_pair(p, at, &o.give_mode, &o.give_value);
    return o;
}
/**
 * Operation 12 (TakeOrGive, handler 31C62): takes the first pair from the selected member or the
 * party and, once that succeeds, gives the second, flashing each receiving portrait and refusing the
 * script (33690) when a take fails. Resumable across the portrait flash and the payment refusal
 * (extended->phase 10).
 *
 * With the whole party selected (or all but the member the selector names) the take and give run for each member
 * until the take's or the give's kind ends the loop (take_loop_ends): a per-member give (modes 15, 16, 20, 104 ...)
 * reaches everyone. With one member selected, a refused take ends the script. Per member, in order: the take and
 * give (take_give); a take the party cannot pay becomes a pending refusal (its kind kept in loop_flash); a give that
 * ends in a portrait flash becomes a pending flash (give_flash); then the pending step runs (loop_run_pending: the
 * refusal or the flash), which may suspend the loop. A resumed member skips the take and give: only its pending step
 * runs, with the outcome it belongs to.
 *
 * Stops: the world or party count out of range, an unsupported take or give, a portrait flash in combat.
 *
 * Original: 31C62 (BP-18 = 1; the party loop 32139, its kind switches 321A1/321DE; one member at 32215,
 * 2C94C(BP-C - 1), refused: 33690; the give's 2DBB8 tail flashes 2B9C7 once per member and dispatch, 8C78).
 */
static int op_take_or_give(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    const take_or_give_pairs o = take_or_give_operands(st->p);
    uint32_t stop = 0;
    unsigned selected = extended->selected_character, count = ds[XEEN_DS_PARTY_COUNT];
    int flash = -1, outcome = TAKE_GIVE_DONE, ended = 0;
    tg_host host = {extended, st->dispatch, st->cu, st->dn};
    extended->window_cleanup = 1;
    if (ds[XEEN_DS_WORLD] > 1u || count > 8u) {
        s->boundary = TAKE_GIVE_STOP_RANGE;
        st->result = XEE_UNSUPPORTED;
        return 1;
    }
    if (extended->phase != XEEN_EXEC_PHASE_MEMBER_LOOP) {
        extended->loop_member = 0;
        extended->loop_take = (uint8_t)o.take_mode;
        extended->loop_flash = LOOP_PENDING_NONE;
    }
    for (;;) {
        unsigned member;
        if (!loop_next_member(extended, ds, selected, count, &member)) {
            break;
        }
        if (extended->loop_flash != LOOP_PENDING_NONE) {
            /* Resumed: only the pending step runs. A pending flash's member was given (DONE); a pending refusal's
             * outcome is the one its step sets (REFUSED), or the loop stops again. */
            outcome = TAKE_GIVE_DONE;
        } else {
            outcome = take_give(ds, extended->loop_take, o.take_value, o.give_mode, o.give_value, member, &flash,
                                &stop, &host);
            if (outcome == TAKE_GIVE_UNSUPPORTED) {
                s->boundary = stop;
                st->result = XEE_UNSUPPORTED;
                return 1;
            }
            if (outcome == TAKE_GIVE_SHORT) { /* the refusal is pending; a flash comes only with DONE */
                extended->loop_flash = loop_refusal_kind(extended->loop_take);
                flash = -1;
            }
            if (give_flash(ds, extended, member, flash, count, o.give_mode, o.give_value) == GIVE_FLASH_IN_COMBAT) {
                s->boundary = TAKE_GIVE_STOP_COMBAT;
                st->result = XEE_UNSUPPORTED;
                return 1;
            }
        }
        if (loop_run_pending(st, member, o.give_mode, o.give_value, &outcome)) {
            break;
        }
        if (take_loop_ends(extended, selected, o.give_mode, outcome, &ended)) {
            break;
        }
        ++extended->loop_member;
    }
    member_loop_finished(st, ended);
    return 0;
}

/*
 * The operands of operations 28, 29 and 38, each a (mode, value) pair whose value width the mode sets (tg_width).
 * Operations 28 and 38 hold a test pair and a give pair; operation 29 two test pairs and a give pair. The test's
 * mode is the If operations' comparison: 1 (equal) for every test but operation 38's first, which is 2 (at most).
 * SECOND_MODE is the mode of the pair after the first, whichever role it has (the give for 28 and 38, the second
 * test for 29).
 */
typedef struct {
    unsigned tests;          /* 1, or 2 for operation 29 */
    unsigned cond[2], mode[2];
    uint32_t value[2];
    unsigned second_mode;
    unsigned give_mode;
    uint32_t give_value;
} conditional_give;

static conditional_give conditional_give_operands(unsigned op, const uint8_t *p)
{
    conditional_give g;
    size_t at = operand_pair(p, 6u, &g.cond[0], &g.value[0]);
    at = operand_pair(p, at, &g.second_mode, &g.value[1]);
    g.tests = op == 29u ? 2u : 1u;
    g.mode[0] = op == 38u ? 2u : 1u;
    g.cond[1] = g.second_mode;
    g.mode[1] = 1u;
    if (op == 29u) {
        (void)operand_pair(p, at, &g.give_mode, &g.give_value);
    } else {
        g.give_mode = g.second_mode;
        g.give_value = g.value[1];
    }
    return g;
}

/* What one member's tests decided: met or not, or why the operation cannot go on (each a stop of its caller). */
enum member_test {
    MEMBER_TEST_FAILED,
    MEMBER_TEST_MET,
    MEMBER_TEST_CONDITION_44, /* condition 44 (a per-member 2F2FF; no shipped record has it) */
    MEMBER_TEST_HOST_REFUSED, /* a condition only the host can evaluate, which it did not (*HANDLED) */
    MEMBER_TEST_UNKNOWN       /* a condition if_condition does not know */
};

/* Runs G's tests for MEMBER in order, stopping at the first that fails. A condition if_condition leaves to the
 * host is asked of the host (test_with_host: XEEN_REQUEST_CONDITION_VALUE, the host's answer in *HANDLED). RES is
 * the If operations' residue slot, carried from member to member. */
static enum member_test test_member(exec_step *st, const conditional_give *g, unsigned member, if_residue *res,
                                    int *handled)
{
    unsigned k;
    for (k = 0; k < g->tests; ++k) {
        unsigned cond = g->cond[k];
        int c;
        if (cond == IF_YES_NO) {
            return MEMBER_TEST_CONDITION_44;
        }
        c = test_with_host(st, cond, g->value[k], g->mode[k], (uint16_t)member, res, handled);
        if (c == IF_HOST_FAILED) {
            return MEMBER_TEST_HOST_REFUSED;
        }
        if (c == -2) {
            return MEMBER_TEST_UNKNOWN;
        }
        if (!c) {
            return MEMBER_TEST_FAILED;
        }
    }
    return MEMBER_TEST_MET;
}

/**
 * Operations 28, 29 and 38 (conditional gives): each selected member (the whole party, all but the member the
 * selector names, or the one member) is tested and, when every test holds, given the give pair; operation 28
 * tests for equality, 38 for at most, 29 two pairs for equality (conditional_give_operands). A member who fails
 * receives nothing; the give's own result is not read, so a refused give does not end the script. A met give of
 * mode 82 ends the member loop, as does a single selected member. A give that flashes the member's portrait
 * suspends the loop until the host has flashed it (phase XEEN_EXEC_PHASE_MEMBER_LOOP; on resumption the member is
 * not tested or given again). The tests keep the If operations' residue slot, which the first give then marks
 * lost.
 *
 * Stops: a second pair of mode 67 (it would first close every window; no shipped record has it), the world or
 * party count out of range, condition 44, a host condition the host does not serve, an unknown condition, an
 * unsupported give, a portrait flash in combat.
 *
 * Original: 31C62 with the opcode 1Ch, 1Dh or 26h (31F38, 3203C); BP-18 = 1; mode 43h runs 186C0 (31F29); the
 * tests 2BF6E(p6, value, 1 or 2, member) at 32079/320DF and, for 29, 2BF6E(p6, value 1, 1) then 2BF6E(mode 2,
 * value 2, 1) at 31F58/31FDF; the give 2C94C(0, 0, give, member), its AX unread (no 33690); 3224D: 3346B; the
 * residue slot is the If's (the same depth in 310B4's frame).
 */
static int op_conditional_give(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    const conditional_give g = conditional_give_operands(s->operation, st->p);
    uint32_t stop = 0;
    unsigned selected = extended->selected_character, count = ds[XEEN_DS_PARTY_COUNT];
    int several = selected == 0u || selected == 8u, flash = -1; /* the party, or the party but one member */
    tg_host host = {extended, st->dispatch, st->cu, st->dn};
    if_residue res = {s->offset, 0, extended->phase == XEEN_EXEC_PHASE_MEMBER_LOOP};
    extended->window_cleanup = 1;
    if (g.second_mode == 67u) {
        s->boundary = CONDITIONAL_GIVE_STOP_MODE_43;
        st->result = XEE_UNSUPPORTED;
        return 1;
    }
    if (ds[XEEN_DS_WORLD] > 1u || count > 8u) {
        s->boundary = TAKE_GIVE_STOP_RANGE;
        st->result = XEE_UNSUPPORTED;
        return 1;
    }
    if (extended->phase != XEEN_EXEC_PHASE_MEMBER_LOOP) {
        extended->loop_member = 0;
        extended->loop_flash = LOOP_PENDING_NONE;
    }
    for (;;) {
        unsigned member;
        int resumed, met = 1, handled = 1;
        if (!loop_next_member(extended, ds, selected, count, &member)) {
            break;
        }
        /* A member whose portrait flash is still pending was tested and given before the suspension. */
        resumed = extended->loop_flash != LOOP_PENDING_NONE;
        if (!resumed) {
            switch (test_member(st, &g, member, &res, &handled)) {
            case MEMBER_TEST_CONDITION_44:
                s->boundary = CONDITIONAL_GIVE_STOP_CONDITION_44;
                st->result = XEE_UNSUPPORTED;
                return 1;
            case MEMBER_TEST_HOST_REFUSED:
                s->boundary = CONDITIONAL_GIVE_STOP_CONDITION_VALUE;
                st->result = handled == 0 ? XEE_UNSUPPORTED : XEE_INTERNAL;
                return 1;
            case MEMBER_TEST_UNKNOWN:
                s->boundary = IF_STOP_UNKNOWN_CONDITION;
                st->result = XEE_UNSUPPORTED;
                return 1;
            case MEMBER_TEST_FAILED:
                met = 0;
                break;
            case MEMBER_TEST_MET:
                break;
            }
        }
        if (!resumed && met) {
            int outcome = take_give(ds, 0, 0, g.give_mode, g.give_value, member, &flash, &stop, &host);
            res.lost = 1;
            if (outcome == TAKE_GIVE_UNSUPPORTED) { /* the other results are not read */
                s->boundary = stop;
                st->result = XEE_UNSUPPORTED;
                return 1;
            }
            if (give_flash(ds, extended, member, flash, count, g.give_mode, g.give_value) == GIVE_FLASH_IN_COMBAT) {
                s->boundary = TAKE_GIVE_STOP_COMBAT;
                st->result = XEE_UNSUPPORTED;
                return 1;
            }
        }
        /* A pending flash's member was given: it counts as met once flashed. */
        if (extended->loop_flash != LOOP_PENDING_NONE) {
            if (loop_run_flash(st, member, g.give_mode, g.give_value)) {
                break;
            }
            met = 1;
        }
        /* One selected member ends the loop, and so does a met give of mode 82. */
        if (!several || (met && g.give_mode == 82u)) {
            break;
        }
        ++extended->loop_member;
    }
    member_loop_finished(st, 0);
    return 0;
}

/* Two explicit ends of these operations that the interpreter reports as external: the lock pick's roll routine
 * (3CF5B), whose random draw the host refused, and the sign's formatted text (its sprintf at 318AE), which would
 * run past the sign buffer into the treasure's item markers at 9970. */
enum {
    PICK_LOCK_ROLL_REFUSED = 0x3cf5b,
    SIGN_TEXT_PAST_BUFFER = 0x318ae
};
/*
 * op_give_multi's resume phases while it opens a chest (extended->phase; the executor's resume check accepts 20..26
 * for operation 20 only), in the order they run.
 */
enum {
    PICK_TRAP_ROLL = 20,   /* the trap's roll, 1..4: a 1 springs it (Original: 504DA(1, 4)) */
    PICK_TRAP_KIND = 21,   /* the trap's kind, 0..6 (504DA(0, 6)) */
    PICK_ROLL = 22,        /* the pick: the member's thievery plus 1..20 against the lock's difficulty (3CF5B:
                            * 2BF6E(102) + 504DA(1, 20) against C0FF) */
    PICK_PICKED_WAIT = 23, /* "%s picks the lock!": a scene pass first (1B503(1)) */
    PICK_PICKED_TEXT = 24, /* then its message (18F2F) */
    PICK_FAILED_TEXT = 25, /* "%s was unable to pick the lock!", then the script ends */
    PICK_TRAP_SCENE = 26,  /* the trap's party damage, which may suspend for its scene (2E68D(C105, the kind,
                            * member)) */
};
/* A field of the event's map object OBJECT (XEEN_DS_NUMBERED_OBJECTS, XEEN_MAP_OBJECT_*), its DS offset in 16 bits. */
#define EVENT_OBJECT_FIELD(object, field) \
    ((uint16_t)(XEEN_DS_NUMBERED_OBJECTS + (field) + (object) * XEEN_MAP_OBJECT_BYTES))

/* Whether an object of sprite SPRITE is a chest: sprites 16, 58 and 73, and 15 when FIFTEEN (on Darkside for the
 * pick; the basic entry's stop counts 15 on both worlds). Original: CS:28F5 (10h, 3Ah, 49h; 0Fh on Darkside). */
static int chest_sprite(unsigned sprite, int fifteen)
{
    return sprite == 16u || sprite == 58u || sprite == 73u || (sprite == 15u && fifteen);
}

/* The pick's message TEXT (a format naming the picker) with the member named at RECORD, into the text buffer:
 * 0 when it does not fit the 1000 bytes the scroll takes. */
static int pick_message(uint8_t *ds, uint16_t text, uint16_t record)
{
    int n = snprintf((char *)ds + XEEN_DS_TEXT_BUFFER, XEEN_TEXT_BUFFER_BYTES, (const char *)ds + text,
                     (const char *)ds + record);
    return n >= 0 && n < 1000;
}

/* What a picked lock does: the lock counts one more pick here (XEEN_DS_OBJECT_LOCK), the picker at RECORD gains
 * (level + temporary level) x the lock's difficulty x 10 experience (the products in 16 bits, the sum in 32), and the
 * treasure's UI mode is set. The experience is read before the lock's count changes, as the original does. */
static void lock_picked(uint8_t *ds, uint16_t record)
{
    uint16_t levels = (uint16_t)(ds[(uint16_t)(record + XEEN_CHARACTER_LEVEL)] +
                                 ds[(uint16_t)(record + XEEN_CHARACTER_LEVEL_BONUS)]);
    uint16_t gain = (uint16_t)((uint16_t)(levels * ds[XEEN_DS_MAP_FIELD(CHEST_LOCK)]) * 10u);
    uint32_t xp = (uint32_t)(ds[(uint16_t)(record + XEEN_CHARACTER_EXPERIENCE)] |
                             ds[(uint16_t)(record + XEEN_CHARACTER_EXPERIENCE + 1u)] << 8) |
                  (uint32_t)(ds[(uint16_t)(record + XEEN_CHARACTER_EXPERIENCE + 2u)] |
                             ds[(uint16_t)(record + XEEN_CHARACTER_EXPERIENCE + 3u)] << 8)
                          << 16;
    ++ds[XEEN_DS_OBJECT_LOCK];
    xp += gain;
    for (unsigned k = 0; k < 4u; ++k) {
        ds[(uint16_t)(record + XEEN_CHARACTER_EXPERIENCE + k)] = (uint8_t)(xp >> (8u * k));
    }
    ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_TREASURE;
}

/* The pick itself (phase PICK_ROLL): the member's thievery (condition value 102) plus 1..20 against the lock's
 * difficulty. A pick that holds rewards the picker (lock_picked) and plays sound 0Ah before its message waits for a
 * scene (PICK_PICKED_WAIT); one that fails plays the refusal sound, closes the chest again and clears the lock before
 * its message (PICK_FAILED_TEXT): the success's message is formatted before its sound, the failure's after it. A
 * refused request ends the operation as external (the roll) or internal (a sound). Returns 1 when op_give_multi must
 * return at once (a message too long), else 0. */
static int pick_roll(exec_step *st, unsigned object, uint16_t member, uint16_t record)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    int handled = step_request(st, XEEN_REQUEST_CONDITION_VALUE, 102, member, 0, 0);
    uint16_t value = st->ax, roll = 0;
    if (handled == 1) {
        handled = host_request(st, XEEN_REQUEST_RANDOM, 1, 20, value, 1, &roll);
    }
    if (handled != 1) {
        st->result = handled == 0 ? XEE_EXTERNAL : XEE_INTERNAL;
        s->boundary = PICK_LOCK_ROLL_REFUSED;
        return 0;
    }
    if ((int16_t)(uint16_t)(value + roll) >= (int16_t)ds[XEEN_DS_MAP_FIELD(CHEST_LOCK)]) {
        lock_picked(ds, record);
        extended->phase = PICK_PICKED_WAIT;
        if (!pick_message(ds, XEEN_TEXT_LOCK_PICKED, record)) {
            st->result = XEE_MALFORMED;
            return 1;
        }
        handled = step_request(st, XEEN_REQUEST_EVENT_SOUND, 0x0a, 0, 0, 0);
    } else {
        handled = step_request(st, XEEN_REQUEST_EVENT_SOUND, XEEN_CUE_REFUSED, 0, 0, 0);
        if (!pick_message(ds, XEEN_TEXT_LOCK_NOT_PICKED, record)) {
            st->result = XEE_MALFORMED;
            return 1;
        }
        ds[EVENT_OBJECT_FIELD(object, XEEN_MAP_OBJECT_FRAME)] = 0;
        ds[XEEN_DS_OBJECT_LOCK] = 0;
        extended->phase = PICK_FAILED_TEXT;
    }
    if (handled != 1) { /* the pick's own 1880E calls (2E006, 2E018); the sound service answers 1 or 0 (failed) */
        st->result = XEE_INTERNAL;
        s->boundary = XEEN_REQUEST_SOUND;
    }
    return 0;
}

/* Whether PHASE is one of the lock's resume phases (PICK_TRAP_ROLL..PICK_TRAP_SCENE). */
static int pick_phase(unsigned phase)
{
    return phase >= PICK_TRAP_ROLL && phase <= PICK_TRAP_SCENE;
}
/* The pick's start at the handler phase: a chest at the party's cell (chest_sprite; 15 only on Darkside) whose lock
 * byte (9755) is clear is opened (the object's frame byte 1) and, unless its sprite is 20 (never a chest sprite, so
 * always), its lock is picked from the trap's roll. */
static void pick_start(exec_step *st, unsigned object)
{
    uint8_t *ds = st->ds;
    if (st->extended->phase == XEEN_EXEC_PHASE_HANDLER && object && !ds[XEEN_DS_OBJECT_LOCK]) {
        unsigned sprite = ds[EVENT_OBJECT_FIELD(object, XEEN_MAP_OBJECT_SPRITE)];
        if (chest_sprite(sprite, ds[XEEN_DS_WORLD] != 0u)) {
            ds[EVENT_OBJECT_FIELD(object, XEEN_MAP_OBJECT_FRAME)] = 1;
            if (sprite != 20u) {
                st->extended->phase = PICK_TRAP_ROLL;
            }
        }
    }
}
/* The end of the pick's message (PICK_PICKED_TEXT or PICK_FAILED_TEXT, its scroll read): the handler phase again for
 * the gives; a failed pick ends the script there (the lock byte 2, then 0; Original: 2E05B, then 327F6). Returns 1
 * when the script ended. */
static int pick_message_read(exec_step *st)
{
    int failed = st->extended->phase == PICK_FAILED_TEXT;
    st->extended->phase = XEEN_EXEC_PHASE_HANDLER;
    if (!failed) {
        return 0;
    }
    st->ds[XEEN_DS_OBJECT_LOCK] = XEEN_OBJECT_LOCK_FAILED;
    st->ds[XEEN_DS_OBJECT_LOCK] = 0;
    st->s->event_flag = 1;
    st->result = script_end(st->s);
    return 1;
}
/**
 * The chest lock of operation 20 (2DECE's first part): with a chest at the party's cell
 * (pick_start), its lock is picked through the PICK_ phases above: the trap roll and scene,
 * the pick roll and its message (a failure clears the lock byte again and ends the script).
 *
 * Each phase asks the host for one service. Served, the phase moves on, always to a later stage
 * of the order below (the trap's roll may skip to the pick); refused or suspended, the step's
 * result is no longer XEE_DONE and the loop ends there, the phase kept for the resume. One call
 * therefore runs the phases from where the step resumed until a request suspends or the lock
 * is done. Returns 1 when op_give_multi must return at once (st->result set: a message too
 * long, or the failed pick's end), else 0; a picked or absent lock leaves the handler phase for
 * the gives.
 */
static int pick_chest_lock(exec_step *st, unsigned object, uint16_t member, uint16_t record)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    int handled;
    pick_start(st, object);
    while (st->result == XEE_DONE && pick_phase(extended->phase)) {
        switch (extended->phase) {
            case PICK_TRAP_ROLL: /* 1..4: a 1 springs the trap */
                handled = step_request(st, XEEN_REQUEST_RANDOM, 1, 4,
                                       EVENT_OBJECT_FIELD(object, XEEN_MAP_OBJECT_SPRITE), 1);
                if (!step_refused(st, handled, XEEN_REQUEST_RANDOM)) {
                    extended->phase = st->ax == 1u ? PICK_TRAP_KIND : PICK_ROLL;
                }
                break;
            case PICK_TRAP_KIND: /* the kind 0..6; a refusal is reported at the party damage it is for */
                handled = step_request(st, XEEN_REQUEST_RANDOM, 0, 6, 1, 1);
                if (handled != 1) {
                    st->result = handled == 0 ? XEE_EXTERNAL : XEE_INTERNAL;
                    s->boundary = XEEN_REQUEST_PARTY_DAMAGE;
                } else {
                    extended->choice_ax = st->ax;
                    extended->phase = PICK_TRAP_SCENE;
                }
                break;
            case PICK_TRAP_SCENE: /* 2E68D may suspend for its 30E2D(2) scene. */
                handled = step_request(st, XEEN_REQUEST_PARTY_DAMAGE, ds[XEEN_DS_MAP_FIELD(TRAP_DAMAGE)],
                                       extended->choice_ax, member, 0);
                if (!step_refused(st, handled, XEEN_REQUEST_PARTY_DAMAGE)) {
                    extended->phase = PICK_ROLL;
                }
                break;
            case PICK_ROLL:
                if (pick_roll(st, object, member, record)) {
                    return 1;
                }
                break;
            case PICK_PICKED_WAIT:
                handled = step_request(st, XEEN_REQUEST_DRAW_SCENE, 1, 0, 0, 0);
                if (!step_refused(st, handled, XEEN_REQUEST_DRAW_SCENE)) {
                    extended->phase = PICK_PICKED_TEXT;
                }
                break;
            default: /* PICK_PICKED_TEXT, PICK_FAILED_TEXT */
                if (step_served(st, XEEN_REQUEST_SCROLL, 1, 0, XEEN_DS_TEXT_BUFFER, 0) && pick_message_read(st)) {
                    return 1;
                }
                break;
        }
    }
    return 0;
}

/* Operation 20's three (mode, value) pairs, read in order from byte 6; each value has its mode's width (tg_width),
 * so each pair's position depends on the modes before it. Mode 0 is an empty pair. */
typedef struct {
    unsigned mode[3];
    uint32_t value[3];
} give_multi_pairs;

static give_multi_pairs give_multi_operands(const uint8_t *p)
{
    give_multi_pairs o;
    size_t at = 6u;
    for (unsigned k = 0; k < 3u; ++k) {
        at = operand_pair(p, at, &o.mode[k], &o.value[k]);
    }
    return o;
}
/* Whether a pair of MODE adds to the party's pending treasure or its food (give_treasure) rather than being given
 * through take_give. */
static int treasure_pair(unsigned mode)
{
    return mode == 34u || mode == 35u || mode == 100u || mode == 101u || mode == 106u;
}
/*
 * A treasure pair of operation 20: 34 and 35 add VALUE to the pending gold and gems; 100 and 101 add a random
 * 1..VALUE of them (the roll's word sign-extended into the dword), 106 that much food (a word). Returns 0 when the
 * roll's step stopped there (st->result set), else 1.
 * Original: 2E07E's table CS:28D9 per pair (the random amounts 504DA(1, value), CDQ).
 */
static int give_treasure(exec_step *st, unsigned mode, uint32_t value)
{
    uint8_t *ds = st->ds;
    switch (mode) {
        case 34u:
            add_le32(ds + XEEN_DS_TREASURE_GOLD, value);
            return 1;
        case 35u:
            add_le32(ds + XEEN_DS_TREASURE_GEMS, value);
            return 1;
        default: /* 100, 101, 106 */
            if (!step_served(st, XEEN_REQUEST_RANDOM, 1, (uint16_t)value, 0, 0)) {
                return 0;
            }
            if (mode == 106u) {
                add_le16(ds + XEEN_DS_PARTY_FOOD, st->ax);
            } else {
                add_le32(ds + (mode == 100u ? XEEN_DS_TREASURE_GOLD : XEEN_DS_TREASURE_GEMS),
                         (uint32_t)(int32_t)(int16_t)st->ax);
            }
            return 1;
    }
}
/*
 * The script after operation 20's pairs, unless a step stopped or suspended them: the event flag is set; a refused
 * pair when one of the pairs is mode 67 closes the cell's lock again (9755 = 1) and the script goes on. Otherwise the
 * script ends when a pair was refused (and no pair is 67) or a 67 pair was given; when nothing was refused and no
 * pair is 67, it goes on. Original: the outcomes at 327EB (33670 goes on, 33690 ends).
 */
static void give_multi_finished(exec_step *st, int refused, int any67)
{
    if (st->result == XEE_DONE) {
        st->s->event_flag = 1;
        if (refused && any67) {
            st->ds[XEEN_DS_OBJECT_LOCK] = 1;
        }
        if (refused != any67) {
            st->result = script_end(st->s);
        }
    }
}
/**
 * Operation 20 (GiveMulti, handler 324E6): with a chest at the party's cell, the lock first
 * (pick_chest_lock), then up to three (mode, value) pairs given to the BP-C member (gold, gems,
 * random gold and gems, food, items through take_give).
 *
 * A chest at the party's cell whose lock is still closed is picked first (pick_chest_lock: its trap, the picking
 * roll, the experience and the message); a failed pick ends the script. Then the record's pairs go to the selected
 * member in order: a treasure pair adds to the pending treasure or the food (give_treasure); 66 gives to the scratch
 * record (member 6) after clearing it; any other nonzero mode is an ordinary give, and 67 ends the pairs after
 * itself. A refusal ends the pairs; give_multi_finished decides the script's end.
 *
 * Stops: on the basic entry (no service state), a closed chest lock; an unsupported give; a give that would flash
 * a portrait (no shipped operation 20 give flashes).
 *
 * Original: 324E6 sets BP-4F = 1 and calls 2DECE(the BP-C member, the pairs); the chests are the objects with
 * sprites 10h, 3Ah, 49h (CS:28F5) and, on Darkside only, 0Fh; the lock byte is 9755. The pairs: 2E07E's table
 * CS:28D9 (66 clears BC6A, 13FE7 162h).
 */
static int op_give_multi(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    const give_multi_pairs o = give_multi_operands(st->p);
    unsigned object = ds[XEEN_DS_EVENT_OBJECT];
    uint16_t member, record;
    int refused = 0, any67;
    if (extended) {
        /* The script's end closes the windows and redraws the party bar (Original: BP-18 = 1 for 33698's 186C0 and
         * 2ABB8(1)). */
        extended->window_cleanup = 1;
    } else {
        /* The basic entry has no service state: a lock stays an explicit stop there. */
        unsigned sprite = object ? ds[EVENT_OBJECT_FIELD(object, XEEN_MAP_OBJECT_SPRITE)] : 0u;
        if (object && !ds[XEEN_DS_OBJECT_LOCK] && chest_sprite(sprite, 1)) {
            s->boundary = GIVE_STOP_SPRITE;
            st->result = XEE_UNSUPPORTED;
            return 1;
        }
    }
    member = (uint16_t)(extended && extended->selected_character ? extended->selected_character - 1u
                                                                 : 0u);
    record = XEEN_PARTY_RECORD(member);
    if (extended && pick_chest_lock(st, object, member, record)) {
        return 1;
    }
    if (st->result != XEE_DONE || (extended && extended->phase != XEEN_EXEC_PHASE_HANDLER)) {
        return 0; /* the lock's step suspended, failed or is still running */
    }
    any67 = o.mode[0] == 67u || o.mode[1] == 67u || o.mode[2] == 67u;
    tg_host host = {extended, st->dispatch, st->cu, st->dn};
    for (unsigned k = 0; k < 3u && st->result == XEE_DONE && !refused; ++k) {
        unsigned m = o.mode[k];
        int flash = -1, outcome;
        uint32_t stop = 0;
        if (!m) {
            continue;
        }
        if (treasure_pair(m)) {
            if (!give_treasure(st, m, o.value[k])) {
                break; /* the step stops at the roll */
            }
            continue;
        }
        if (m == VAR_RANDOM_ITEM) {
            memset(ds + XEEN_DS_SCRATCH_RECORD, 0, XEEN_CHARACTER_BYTES);
        }
        /* a random item is generated into the scratch record (member index XEEN_PARTY_SLOTS: the record after the six) */
        outcome = take_give(ds, 0, 0, m, o.value[k], m == VAR_RANDOM_ITEM ? XEEN_PARTY_SLOTS : member, &flash, &stop, &host);
        if (outcome == TAKE_GIVE_UNSUPPORTED) {
            s->boundary = stop;
            st->result = XEE_UNSUPPORTED;
            return 1;
        }
        if (flash >= 0) {
            s->boundary = GIVE_STOP_FLASH;
            st->result = XEE_UNSUPPORTED;
            return 1;
        } /* no shipped op-20 give flashes */
        refused = outcome == TAKE_GIVE_REFUSED; /* a give alone is never SHORT */
        if (m == 67u) {
            break;
        }
    }
    give_multi_finished(st, refused, any67);
    return 0;
}

/* Where copying operation 37's text stopped (member_text_copy). */
enum member_text_step {
    MEMBER_TEXT_END,        /* the NUL copied */
    MEMBER_TEXT_CONVERSION, /* at a conversion other than %% */
    MEMBER_TEXT_OVERFLOW    /* the text or the result would pass its 1000-byte buffer */
};
/*
 * Copies operation 37's text (A827) from index *I into A43F at *O, each %% as one %, until its NUL is copied
 * (END), a conversion other than %% is reached (CONVERSION: *I at its %, *F the conversion's character) or a buffer
 * would overflow (OVERFLOW). Bytes copied before an end stay copied.
 */
static enum member_text_step member_text_copy(uint8_t *ds, size_t *i, size_t *o, uint8_t *f)
{
    for (;; ++*i) {
        uint8_t c;
        if (*i >= 1000u) {
            return MEMBER_TEXT_OVERFLOW;
        }
        c = ds[XEEN_DS_TEXT_BUFFER + *i];
        if (c == '%') {
            *f = ds[XEEN_DS_TEXT_BUFFER + *i + 1u];
            if (*f != '%') {
                return MEMBER_TEXT_CONVERSION;
            }
            if (*o + 1u >= 1000u) {
                return MEMBER_TEXT_OVERFLOW;
            }
            ds[XEEN_DS_TEXT_BUFFER_2 + (*o)++] = '%';
            ++*i;
            continue;
        }
        if (*o >= 1000u) {
            return MEMBER_TEXT_OVERFLOW;
        }
        ds[XEEN_DS_TEXT_BUFFER_2 + (*o)++] = c;
        if (!c) {
            return MEMBER_TEXT_END;
        }
    }
}
/**
 * Operation 37 (DisplayStat): the record's text as a format with the selected member's name for its %s, shown in
 * window 12 without a wait. Only %s and %% are formatted (member_text_copy copies the rest); any other conversion
 * stops (no shipped text has one), as does a %s whose name has no NUL within 1000 bytes. A text or result past the
 * 1000-byte buffers is malformed. With no member selected the name is the record-sized bytes before the party (an
 * original quirk).
 * Original: 315AE: AC0F = 14BBA(A827, B2BC + BP-C x 162h); 1871B(0Ch) unless 9878, 4E38C(0Ch, AC0F), 50BF6(0Ch)
 * (no 18765, no wait); 3346B.
 */
static int op_display_member_text(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    uint16_t name = (uint16_t)(XEEN_DS_NUMBERED_MEMBERS + extended->selected_character * XEEN_CHARACTER_BYTES);
    size_t i = 0, o = 0, nl = 0;
    uint8_t f = 0;
    while (nl < 1000u && ds[(uint16_t)(name + nl)]) { /* the name's bytes wrap in the segment */
        ++nl;
    }
    for (;;) {
        enum member_text_step step = member_text_copy(ds, &i, &o, &f);
        if (step == MEMBER_TEXT_END) {
            break;
        }
        if (step == MEMBER_TEXT_OVERFLOW) {
            st->result = XEE_MALFORMED;
            return 1;
        }
        if (f != 's' || nl == 1000u) {
            s->boundary = MEMBER_TEXT_STOP_FORMAT;
            st->result = XEE_UNSUPPORTED;
            return 1;
        }
        if (o + nl >= 1000u) {
            st->result = XEE_MALFORMED;
            return 1;
        }
        for (size_t k = 0; k < nl; ++k) { /* the %s: the name */
            ds[XEEN_DS_TEXT_BUFFER_2 + o++] = ds[(uint16_t)(name + k)];
        }
        i += 2u;
    }
    if (step_served(st, XEEN_REQUEST_STAT_LINE_WINDOW, 12, 0, XEEN_DS_TEXT_BUFFER_2, 1000)) {
        s->event_flag = 1;
    }
    return 0;
}

/**
 * Operation 50 (DisplayLarge, handler 31633): loads the map's aaze2NNN.txt text file, selects the
 * record's line and pages it in window 11; resumable across the load and the pages (extended->phase
 * 1, 11, 12).
 */
static void op_display_large(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    const uint8_t *p = st->p;
    /* The map's long text file (aaze2NNN.txt, by map) replaces the map's texts for a while: the record's line of
     * it is shown in the 3D view's window a page at a time, a key awaited between pages, then the map's own texts
     * come back and the script goes on. The scene is redrawn once per dispatch first. The load, the line table, the
     * pages and the restore are one host request after the scene (nothing between them polls or draws the RNG).
     * Original: 31633 (F5E8 = "aaze2%03u.txt" of ED99, 18931 loads it into BP-6, 4A1B7 copies it to F42C:0; 9C52 =
     * 8582, 9C5C:9C5E = F42C:0, 2C661 the line table, the text record +6's line; BP-12 = 1, window 0Bh); 316EB
     * (1B503(1) once per dispatch, BP-4E; 18765(26h), 1871B(0Bh) unless open; the pages by 4A1B7, 4E38C and 50BF6,
     * the 31786 key loop and "\r" (630E) while text is left); BP-4F = 1, 9C52 and 9C5C:9C5E back, 2C661,
     * 4AA3E(BP-6), 33670. */
    step_scene_once(st, XEEN_EXEC_PHASE_SCENE);
    if (st->result == XEE_DONE && extended->phase == XEEN_EXEC_PHASE_HANDLER) {
        /* Window 38 closes and window 11 opens unless it is open already; nothing goes into the text buffer.
         * Original: 31700 18765(26h); 3172A 1871B(0Bh) unless 9877 (986C + 0Bh) is set; BP-12 = 1. */
        if (ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_EVENT_TEXT)]) {
            ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_EVENT_TEXT)] = 0;
            xeen_put_le16(ds + XEEN_DS_WINDOW_DEPTH,
                          (uint16_t)(xeen_get_le16(ds + XEEN_DS_WINDOW_DEPTH) - 1u));
        }
        if (!ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)]) {
            uint16_t top = (uint16_t)(xeen_get_le16(ds + XEEN_DS_WINDOW_DEPTH) + 1u);
            ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)] = 1;
            xeen_put_le16(ds + XEEN_DS_WINDOW_DEPTH, top);
            ds[(uint16_t)(XEEN_DS_WINDOW_STACK + top)] = 11;
        }
        extended->phase = XEEN_EXEC_PHASE_PAGED_TEXT;
    }
    if (st->result == XEE_DONE && extended->phase == XEEN_EXEC_PHASE_PAGED_TEXT) {
        if (step_served(st, XEEN_REQUEST_MAP_TEXT, p[6], 0, 0, 0)) {
            extended->phase = XEEN_EXEC_PHASE_HANDLER;
            s->event_flag = 1;
        }
    }
}

/**
 * Operations 41 and 53 (DisplayBottom, DisplayMain; handler 316DC): shows the record's text in window
 * 12 a page at a time, with the scene redrawn and a key awaited per page, then continues the script.
 */
static int op_display_bottom(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    /* The scene is redrawn once per dispatch; window 38 closes; the text buffer, centred, goes to the bottom window
     * (12; DisplayMain uses the 3D view's window, 11), opened unless it is open already, and the host shows it a
     * page at a time, the scene redrawn and a key awaited between pages; then the script goes on.
     * Original: 316DC (BP-12 = 0, window 12) and 316CB for operation 53 (window 0Bh: BP-48, open flag 9877 =
     * 986C + 0Bh); 316EB 1B503(1) once per dispatch (BP-4E); 18765(26h); AC0F = "\r\3c" + A827; 1871B(12) unless
     * open; the pages 4E38C, 50BF6 and per page left 189D7 and the 31786 scene and key loop; BP-4F = 1, 33670.
     * AC0F is the program data's format (DS:62FA "\r\3c%s", pushed at 31713, formatted at 3171A) with A827: a page
     * past AC0F's 1000 bytes is malformed before window 38 closes, as before; a format with other conversions stops
     * at 3171A after the close (none in the program data). */
    static const uint16_t text[] = {XEEN_DS_TEXT_BUFFER};
    unsigned window = s->operation == XEEN_OP_DISPLAY_MAIN ? XEEN_WINDOW_VIEW : XEEN_WINDOW_BOTTOM;
    step_scene_once(st, XEEN_EXEC_PHASE_SCENE);
    if (st->result == XEE_DONE && extended->phase == XEEN_EXEC_PHASE_HANDLER) {
        int f = event_format_check(ds, st->dn, XEEN_TEXT_EVENT_CENTRED_FORMAT, text, 1u, XEEN_DS_TEXT_BUFFER_2,
                                   XEEN_TEXT_BUFFER_BYTES);
        if (f == EVENT_FORMAT_TOO_LONG) {
            st->result = XEE_MALFORMED;
            return 1;
        }
        if (ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_EVENT_TEXT)]) {
            ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_EVENT_TEXT)] = 0;
            xeen_put_le16(ds + XEEN_DS_WINDOW_DEPTH,
                          (uint16_t)(xeen_get_le16(ds + XEEN_DS_WINDOW_DEPTH) - 1u));
        }
        if (f == EVENT_FORMAT_REFUSED) {
            s->boundary = BOTTOM_TEXT_STOP_FORMAT;
            st->result = XEE_UNSUPPORTED;
            return 1;
        }
        (void)event_format(ds, st->dn, XEEN_TEXT_EVENT_CENTRED_FORMAT, text, 1u, XEEN_DS_TEXT_BUFFER_2,
                           XEEN_TEXT_BUFFER_BYTES);
        if (!ds[XEEN_DS_WINDOW_OPEN + window]) {
            uint16_t top = (uint16_t)(xeen_get_le16(ds + XEEN_DS_WINDOW_DEPTH) + 1u);
            ds[XEEN_DS_WINDOW_OPEN + window] = 1;
            xeen_put_le16(ds + XEEN_DS_WINDOW_DEPTH, top);
            ds[(uint16_t)(XEEN_DS_WINDOW_STACK + top)] = (uint8_t)window;
        }
        extended->phase = XEEN_EXEC_PHASE_PAGED_TEXT;
    }
    if (st->result == XEE_DONE && extended->phase == XEEN_EXEC_PHASE_PAGED_TEXT) {
        if (step_served(st, XEEN_REQUEST_PAGED_TEXT, (uint16_t)window, 0, XEEN_DS_TEXT_BUFFER_2, 1000)) {
            extended->phase = XEEN_EXEC_PHASE_HANDLER;
            s->event_flag = 1;
        }
    }
    return 0;
}

/**
 * Operation 49 (DisplayBottomTwoLines, handler 31804): the record's two texts in window 12 in one
 * page (no paging), then the 2F2FF wait (phase 18) and the script's end (33690).
 */
static int op_display_two_lines(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    const uint8_t *p = st->p;
    /* The two texts (the text buffer's and the record's text p7) in window 12 as one page, without paging; then the
     * script waits for any key and ends. The page is the program data's format with the two texts.
     * Original: 31804 (A43F = text p7 by 15616, 1000; AC0F = 14BBA(DS:6314 "\r\3c\t000\v007%s\n\n%s", A827, A43F)
     * at 3183E; 18765(12), 1871B(12) unless 9878, one 4E38C(AC0F, 12) and 50BF6(12): the host's 31804; BP-4F = 1;
     * 2F2FF(1, 0) in phase 18; 33690). The original's sprintf is unbounded: a page past AC0F's 1000 bytes is
     * malformed here; a format with other conversions stops (none in the program data). */
    if (extended->phase == XEEN_EXEC_PHASE_HANDLER) {
        static const uint16_t texts[] = {XEEN_DS_TEXT_BUFFER, XEEN_DS_TEXT_BUFFER_0};
        if (step_served(st, XEEN_REQUEST_COPY_TEXT, p[7], 0, XEEN_DS_TEXT_BUFFER_0, 1000)) {
            int f = event_format(ds, st->dn, XEEN_TEXT_EVENT_TWO_LINES_FORMAT, texts, 2u, XEEN_DS_TEXT_BUFFER_2,
                                 XEEN_TEXT_BUFFER_BYTES);
            if (f == EVENT_FORMAT_REFUSED) {
                s->boundary = TWO_LINES_STOP_FORMAT;
                st->result = XEE_UNSUPPORTED;
                return 1;
            }
            if (f == EVENT_FORMAT_TOO_LONG) {
                st->result = XEE_MALFORMED;
                return 1; /* AC0F's 1000 bytes */
            }
            if (step_served(st, XEEN_REQUEST_TWO_LINES_WINDOW, 12, 0, XEEN_DS_TEXT_BUFFER_2, 1000)) {
                s->event_flag = 1;
                extended->phase = XEEN_EXEC_PHASE_TWO_LINES_WAIT;
            }
        }
    }
    if (st->result == XEE_DONE && extended->phase == XEEN_EXEC_PHASE_TWO_LINES_WAIT) {
        if (step_served(st, XEEN_REQUEST_YES_NO, 1, 0, 0, 0)) {
            st->result = script_end(s);
        }
    }
    return 0;
}

/* The pending list (0 weapons, 1 armour, 2 accessories, 3 misc) of an event's signed item number N, with the id within
 * that list into *ID: armour, accessories and misc items by their number ranges (XEEN_SCRIPT_ITEM_*); weapons below
 * the armour's; a quest item (XEEN_QUEST_ITEM_FIRST and above) takes the weapon list with its own number. */
static unsigned script_item_list(uint8_t n, uint8_t *id)
{
    int8_t v = (int8_t)n;
    *id = n;
    if (v >= (int)XEEN_QUEST_ITEM_FIRST || v < (int)XEEN_SCRIPT_ITEM_ARMOR) {
        return 0u;
    }
    if (v < (int)XEEN_SCRIPT_ITEM_ACCESSORIES) {
        *id = (uint8_t)(n - XEEN_SCRIPT_ITEM_ARMOR);
        return 1u;
    }
    if (v < (int)XEEN_SCRIPT_ITEM_MISC) {
        *id = (uint8_t)(n - XEEN_SCRIPT_ITEM_ACCESSORIES);
        return 2u;
    }
    *id = (uint8_t)(n - XEEN_SCRIPT_ITEM_MISC);
    return 3u;
}
/* The misc items an event gives with one charge (ids 10 and 11); every other misc item gets 3..10. */
static int single_charge_misc(uint8_t id)
{
    return id == 11u || id == 10u;
}
/**
 * Operation 44 (GiveEnchanted, handler 3312E): adds the record's item (weapon, armor, accessory or
 * misc by the signed id range) with its material and attribute bytes to the party's pending item
 * lists.
 */
static void op_give_enchanted(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    const uint8_t *p = st->p;
    /* The record's signed item number p6 chooses the pending list and the id; the item goes into the list's first
     * free row (none free: nothing): a weapon, armor or accessory with material p7 and attribute p8, a misc item
     * with p7 and charges 1 (ids 10 and 11) or a random 3..10. A quest item counts its counter up and then takes the
     * weapon path with its own number, as give 21 does.
     * Original: 3312E (the misc charges 504DA(1, 8) + 2, AX p7 at the call; the quest item's 3330D jumps to 33139;
     * 9A10 = 1 for a filled row; 3346B either way). */
    int8_t v = (int8_t)p[6];
    uint8_t key;
    unsigned c = script_item_list(p[6], &key);
    int row = xeen_treasure_free_row(ds, c);
    if (row >= 0 && c == 3u && !single_charge_misc(key)) {
        int handled = step_request(st, XEEN_REQUEST_RANDOM, 1, 8, p[7], 1);
        (void)step_refused(st, handled, XEEN_REQUEST_RANDOM);
    }
    if (st->result == XEE_DONE) {
        if (v >= (int)XEEN_QUEST_ITEM_FIRST) {
            ++ds[(uint16_t)(XEEN_DS_QUEST_ITEMS + (uint16_t)v)];
        }
        if (row >= 0) {
            uint16_t a = (uint16_t)(xeen_treasure_list(c) + 4u * (unsigned)row);
            if (c == 3u) {
                ds[a] = key;
                ds[(uint16_t)(a + 1u)] = p[7];
                ds[(uint16_t)(a + 2u)] = single_charge_misc(key) ? 1u : (uint8_t)(st->ax + 2u);
            } else {
                ds[(uint16_t)(a + 1u)] = key;
                ds[a] = p[7];
                ds[(uint16_t)(a + 2u)] = p[8];
            }
            ds[XEEN_DS_TREASURE_ITEMS_PENDING] = 1;
        }
        s->event_flag = 1;
    }
}

/**
 * Operations 2, 3, 4 and 39 (DoorTextSml, DoorTextLrg, SignText, SeatTextSml; handlers 31888..318A6):
 * formats the record's text into 98DA with the operation's DS format and shows it through 1B503's scene
 * draw, then waits for a key.
 */
static int op_sign_text(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    /* The record's text, inside its operation's format (the program data's), goes into the sign buffer (98DA), and
     * the scene draws it.
     * Original: handlers 31888 (op 2), 31892 (3), 3189C (4), 318A6 (39) push the formats DS:6326, 633C, 6350, 6364;
     * 318AE 14BBA(98DA, the format, A827) at 318B2; 3066 = 1; 1B503(1), whose 1BF6E draws 98DA (4E38C(3, 98DA));
     * 315F0 to 3346B. */
    static const uint16_t text[] = {XEEN_DS_TEXT_BUFFER};
    uint16_t format = s->operation == 2   ? XEEN_TEXT_EVENT_SIGN_FORMAT_2
                      : s->operation == 3 ? XEEN_TEXT_EVENT_SIGN_FORMAT_3
                      : s->operation == 4 ? XEEN_TEXT_EVENT_SIGN_FORMAT_4
                                          : XEEN_TEXT_EVENT_SIGN_FORMAT_39;
    if (extended->phase == XEEN_EXEC_PHASE_HANDLER) {
        /* The original's sprintf is unbounded. 98DA..996F (150 bytes) is the room before the next data the original
         * references (9970, the treasure's item markers; phase157/census/ds98da.txt); the longest shipped sign is
         * Clouds map 30 (29, 22)'s 67-byte advertisement. A longer text would overwrite 9970: an explicit stop. A
         * format with other conversions than its %s stops too (none in the program data). */
        int f = event_format(ds, st->dn, format, text, 1u, XEEN_DS_SIGN_TEXT, XEEN_SIGN_TEXT_BYTES);
        if (f == EVENT_FORMAT_REFUSED) {
            s->boundary = SIGN_TEXT_STOP_FORMAT;
            st->result = XEE_UNSUPPORTED;
            return 1;
        }
        if (f == EVENT_FORMAT_TOO_LONG) {
            s->boundary = SIGN_TEXT_PAST_BUFFER;
            st->result = XEE_EXTERNAL;
            return 1;
        }
        ds[XEEN_DS_SIGN_SHOWN] = 1;
        extended->phase = XEEN_EXEC_PHASE_SIGN_SCENE;
    }
    if (step_served(st, XEEN_REQUEST_DRAW_SCENE, 1, 0, 0, 0)) {
        s->event_flag = 1;
    }
    return 0;
}

/*
 * The mirror's special words. A mirror's word entry answers the matched row of the world's mirror table + 1
 * (xeenmirr.txt on Clouds, darkmirr.txt on Darkside, both in DARK.CC); most rows are destinations, these are not:
 * three replay an ending through the host, by its name in XEEN_DS_ENDING_NAMES (Genesis WORLDEND, Showdown
 * ENDGAME2, Showtime ENDGAME, which Darkside replays only with Clouds installed); Bloopers ends the script (the
 * original's call there does nothing); and "i lost it" on Clouds gives a pending weapon 22h (the treasure's next free
 * weapon row).
 */
enum {
    MIRROR_CLOUDS_SHOWTIME = 40, /* row 27h */
    MIRROR_CLOUDS_I_LOST_IT = 59,
    MIRROR_CLOUDS_SHOWDOWN = 60,
    MIRROR_CLOUDS_GENESIS = 61,
    MIRROR_CLOUDS_BLOOPERS = 62, /* row 3Dh, the table's last */
    MIRROR_DARK_SHOWDOWN = 33,   /* row 20h */
    MIRROR_DARK_GENESIS = 34,
    MIRROR_DARK_SHOWTIME = 35,
    MIRROR_DARK_BLOOPERS = 36,   /* row 23h, the table's last */
};
/**
 * Operations 21 and 55 (ConfirmWord, ConfirmWord2): the party types a word and the answer steers the script.
 *
 * The first pass puts the prompt (the record's text p8, or with p8 = 0 the text buffer as it stands) into the text
 * buffer and asks the host for the word (XEEN_REQUEST_COPY_TEXT, then the XEEN_EVENT_WORD_ENTRY boundary). The host's
 * title is the record's text p9 or "What's the Password?"; operation 55 has none, and its entry can also answer
 * FFFFh.
 *
 * The second pass reads the answer. Some answers are the mirror's special words, numbered per world: the ending words
 * replay an ending through XEEN.EXE (Darkside 21h, 22h and, with the Clouds installed, 23h; Clouds 3Ch, 3Dh, 28h);
 * 24h (Darkside) and 3Eh (Clouds) end the script; 3Bh (Clouds) gives weapon 22h through the pending treasure and ends
 * the script, unless the pending weapons are full. Any other nonzero answer moves the script to line p7 (p9 for
 * FFFFh) and takes the branch; 0, a wrong word, leaves the script on its line.
 *
 *
 * Original: handler 32855 (operation 55 enters at 32849, BP-6 = 55, its title zeroed at 328AD); the word entry
 * 2E8E5(p6, A43F, title, 21) answers in BP-E; the answers are tested at 32924..32A0A with DS 94 as the world; an
 * ending replay enters the ending handlers' shared tail at its name load (334CF ENDGAME2, 334A6 WORLDEND, 33511
 * ENDGAME); 24h/3Eh call 310B3, a bare RETF, before the end (33690); operation 55's FFFFh is 2E8E5's answer for
 * the A43F text.
 */
/* The ending a mirror word W replays on WORLD (nonzero: Darkside; CLOUDS the Clouds installed): the ending-names
 * entry's DS offset (Genesis +0, Showdown +4, Showtime +8; Showtime on Darkside only with the Clouds installed),
 * or 0 when W replays none. */
static uint16_t mirror_ending(uint16_t w, uint8_t world, uint8_t clouds)
{
    if ((w == MIRROR_DARK_SHOWDOWN && world) || (w == MIRROR_CLOUDS_SHOWDOWN && !world)) {
        return XEEN_DS_ENDING_NAMES + 4u;
    }
    if ((w == MIRROR_DARK_GENESIS && world) || (w == MIRROR_CLOUDS_GENESIS && !world)) {
        return XEEN_DS_ENDING_NAMES;
    }
    if ((w == MIRROR_DARK_SHOWTIME && world && clouds) || (w == MIRROR_CLOUDS_SHOWTIME && !world)) {
        return XEEN_DS_ENDING_NAMES + 8u;
    }
    return 0;
}
/* "I lost it" on the Clouds: weapon 22h into the first free pending weapon row (its id byte 0, the row's other bytes
 * 0) and the pending items flagged. Returns 0 (nothing given) when the pending weapons are full. */
static int give_lost_weapon(uint8_t *ds)
{
    int k = xeen_treasure_free_row(ds, 0);
    uint16_t row;
    if (k < 0) {
        return 0;
    }
    row = (uint16_t)(XEEN_DS_TREASURE_WEAPONS + 4u * (unsigned)k);
    ds[(uint16_t)(row + 1u)] = 0x22;
    ds[row] = 0;
    ds[(uint16_t)(row + 2u)] = 0;
    ds[XEEN_DS_TREASURE_ITEMS_PENDING] = 1;
    return 1;
}
/* The word entry's first pass: the title text into text buffer 0 (the record's text p8 through the host, else the
 * text buffer's own text, which must end within its 1000 bytes), then the entry itself is the host's
 * (XEEN_EVENT_WORD_ENTRY, external). Returns 1 when the step must return at once (a malformed buffer). */
static int ask_word(exec_step *st)
{
    uint8_t *ds = st->ds;
    int handled = 1;
    if (st->p[8]) {
        handled = step_request(st, XEEN_REQUEST_COPY_TEXT, st->p[8], 0, XEEN_DS_TEXT_BUFFER_0, 1000);
    } else {
        size_t length = ds_text_length(ds, XEEN_DS_TEXT_BUFFER);
        if (length == 1000u) {
            st->result = XEE_MALFORMED;
            return 1;
        }
        memmove(ds + XEEN_DS_TEXT_BUFFER_0, ds + XEEN_DS_TEXT_BUFFER, length + 1u);
    }
    if (!step_refused(st, handled, XEEN_REQUEST_COPY_TEXT)) {
        st->extended->phase = XEEN_EXEC_PHASE_WORD_ENTERED;
        st->s->boundary = XEEN_EVENT_WORD_ENTRY;
        st->result = XEE_EXTERNAL;
    }
    return 0;
}
static int op_confirm_word(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    uint16_t w, replay;
    uint8_t world;
    if (st->extended->phase != XEEN_EXEC_PHASE_WORD_ENTERED) {
        return ask_word(st);
    }
    /* The second pass: the answer (word_ax). */
    w = st->extended->word_ax;
    world = ds[XEEN_DS_WORLD]; /* nonzero: Darkside */
    replay = mirror_ending(w, world, ds[XEEN_DS_CLOUDS_INSTALLED]);
    if (replay) {
        if (step_request(st, XEEN_REQUEST_ENDING, replay, 0, 0, 0) != 1) {
            s->boundary = XEEN_REQUEST_ENDING;
            st->result = XEE_INTERNAL;
            return 1;
        }
        st->result = ending_end(st);
    } else if ((w == MIRROR_DARK_BLOOPERS && world) || (w == MIRROR_CLOUDS_BLOOPERS && !world) ||
               (w == MIRROR_CLOUDS_I_LOST_IT && !world && give_lost_weapon(ds))) {
        s->event_flag = 1;
        st->result = script_end(s);
    } else {
        if (w) {
            s->sequence = (uint16_t)((uint16_t)(w == 0xffffu ? st->p[9] : st->p[7]) - 1u);
        }
        st->branch = w != 0;
        s->event_flag = 1;
    }
    return 0;
}

/**
 * Operation 33 (RndDamage): random damage. After the dispatch's scene redraw (as Damage's), the host rolls 1..p7
 * (a signed byte) and that much damage of type p6 hits the selected member, or the party when the whole party is
 * selected. The roll is kept (choice_ax, phase RANDOM_DAMAGE) so that a damage request the host suspends for its own
 * scene resumes with the same amount. p8 is unused.
 * Original: 32F0C; the roll 504DA(1, CBW p7), sign-extended (CDQ); the damage 2E68D(roll, CBW p6, BP-C - 1), which
 * reads the roll's low word; 3346B.
 */
static void op_random_damage(exec_step *st)
{
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    const uint8_t *p = st->p;
    step_scene_once(st, XEEN_EXEC_PHASE_DAMAGE_SCENE);
    if (st->result == XEE_DONE && extended->phase != XEEN_EXEC_PHASE_RANDOM_DAMAGE) {
        if (step_served(st, XEEN_REQUEST_RANDOM, 1, (uint16_t)xeen_sign8(p[7]), 0, 0)) {
            extended->choice_ax = st->ax;
            extended->phase = XEEN_EXEC_PHASE_RANDOM_DAMAGE;
        }
    }
    if (st->result == XEE_DONE) {
        if (step_served(st, XEEN_REQUEST_PARTY_DAMAGE, extended->choice_ax, (uint16_t)xeen_sign8(p[6]),
                        (uint16_t)(extended->selected_character - 1u), 0)) {
            s->event_flag = 1;
            extended->phase = XEEN_EXEC_PHASE_HANDLER;
        }
    }
}

/**
 * Operation 48 (ChooseNumeric): the player chooses a number through the host (with the record's p6). The answer is
 * kept (choice_ax, phase CHOICE_MADE), so a restore the host suspends resumes with it. A nonzero answer N restores the
 * event resources and jumps to the line in the record's byte 6 + N, read as a signed byte; the original reads that
 * byte even past the record's end (the next record's bytes), and so does this, as long as it lies inside the store.
 * The branch is whether a number was chosen.
 * Original: 33329; the choice 2F5B1(CBW p6) into BP-6, the restore 4A1B7(9C4E, 9C62:9C64, 9C56), the jump
 * BP-4 = CBW(record[6 + N]) - 1; 3346B either way.
 */
static void op_choose_numeric(exec_step *st)
{
    uint8_t *b = st->b;
    size_t n = st->n;
    const xeen_event_index *idx = st->idx;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    const xeen_event_record *r = st->r;
    const uint8_t *p = st->p;
    if (extended->phase != XEEN_EXEC_PHASE_CHOICE_MADE &&
        step_served(st, XEEN_REQUEST_NUMERIC_CHOICE, (uint16_t)xeen_sign8(p[6]), 0, 0, 0)) {
        extended->choice_ax = st->ax;
        extended->phase = XEEN_EXEC_PHASE_CHOICE_MADE;
    }
    if (st->result == XEE_DONE && extended->choice_ax &&
        step_served(st, XEEN_REQUEST_RESTORE_EVENTS, 0, 0, 0, 0)) {
        size_t at = 6u + (size_t)extended->choice_ax;
        if (!valid(b, n, idx) || n - (size_t)r->offset <= at) {
            st->result = XEE_MALFORMED;
        } else {
            jump_to_line(s, b[(size_t)r->offset + at]);
        }
    }
    if (st->result == XEE_DONE) {
        st->branch = extended->choice_ax != 0;
        s->event_flag = 1;
    }
}

/* Whether index record TARGET is one operation 24 rewrites: at the event's cell, facing the party's direction or any
 * direction (4), with its fourth field equal to the operation's p[6]. Original: 32ADA..32BCF (the 10-byte rows at
 * 50D4:1C7E against ED97, ED98, ED96 or 4, then [9C62] + 6). */
static int after_event_target(const xeen_event_record *target, const xeen_event_state *s, const uint8_t *ds,
                              const uint8_t *p)
{
    return target->field1 == s->x && target->field2 == s->y &&
           (target->field3 == (int16_t)ds[XEEN_DS_PARTY_DIRECTION] || target->field3 == 4) && target->field4 == p[6];
}
/* Operation 24's phase 1: every matching record's operation byte (+5) becomes p[7]; a matched PlayCD (60) record's
 * following record is rewritten too, first. The original reads that following row whether or not it exists; the
 * native refuses a match on the index's last row (XEE_MALFORMED, the loop ended). Original: 32ACE..32C96 (the 3Ch
 * test 32C00, the next row 32C06..32C44, the matched row 32C47..). */
static void rewrite_after_event_records(exec_step *st)
{
    const xeen_event_index *idx = st->idx;
    for (size_t row = 0; row < idx->record_count; ++row) {
        const xeen_event_record *target = &idx->records[row];
        if (!after_event_target(target, st->s, st->ds, st->p)) {
            continue;
        }
        if (st->b[target->offset + 5u] == XEEN_OP_PLAY_CD) {
            if (row + 1u >= idx->record_count) {
                st->result = XEE_MALFORMED;
                return;
            }
            st->b[idx->records[row + 1u].offset + 5u] = st->p[7];
        }
        st->b[target->offset + 5u] = st->p[7];
    }
}
/**
 * Operation 24 (AfterEvent): rewrites the matching index records of the record's cell (phase 1) and
 * runs the host's follow-up request.
 */
static void op_after_event(exec_step *st)
{
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    if (extended->phase == XEEN_EXEC_PHASE_HANDLER) {
        rewrite_after_event_records(st);
        if (st->result == XEE_DONE) {
            extended->phase = XEEN_EXEC_PHASE_AFTER_EVENT;
        }
    }
    if (st->result == XEE_DONE) {
        if (step_served(st, XEEN_REQUEST_STORE_EVENTS, 0, 0, 0, 0)) {
            s->event_flag = 1;
        }
    }
}

/**
 * Operation 5 (NPC): the map's event text p7 is copied for the speaker (at most 1000 bytes), then the host runs the NPC
 * dialogue with face p8 and mode p9; a yes (a nonzero answer) jumps to line p10. The copy is made once (phase
 * NPC_TALK), so a dialogue the host suspends resumes at the dialogue, not at the copy.
 * p10 may be the next record's length byte (Clouds mode-2 records omit the operand): the original reads it there, and
 * so does the native while it lies inside the store; past the store's end the native refuses the branch
 * (XEE_MALFORMED, the event flag not set).
 * Original: 318C9: 15616(A43F, 9C5E:9C5C + [9A1E + 2 p7], 3E8h); 3000D(p8, A827, A43F, p9); AX nonzero:
 * BP-4 = CBW p10 - 1; 3346B.
 */
static void op_npc(exec_step *st)
{
    size_t n = st->n;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    const xeen_event_record *r = st->r;
    const uint8_t *p = st->p;
    if (extended->phase == XEEN_EXEC_PHASE_HANDLER &&
        step_served(st, XEEN_REQUEST_COPY_TEXT, p[7], 0, XEEN_DS_TEXT_BUFFER_0, 1000)) {
        extended->phase = XEEN_EXEC_PHASE_NPC_TALK;
    }
    if (st->result != XEE_DONE || !step_served(st, XEEN_REQUEST_NPC_DIALOGUE, p[8], p[9], 0, 0)) {
        return;
    }
    st->branch = st->ax != 0;
    if (st->branch && n - (size_t)r->offset < 11u) {
        st->result = XEE_MALFORMED;
        return;
    }
    if (st->branch) {
        jump_to_line(s, p[10]);
    }
    s->event_flag = 1;
}

/**
 * Operation 22 (Damage, handler 32A2C): the scene redraw once per dispatch (phase 16), then the
 * record's 16-bit damage of its type dealt to the selected member or the party (2E68D).
 */
static void op_damage(exec_step *st)
{
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    const uint8_t *p = st->p;
    /* The scene is redrawn once per dispatch; then the record's damage (p6 + 256 x p7) of type p8 hits the selected
     * member, or the party. Original: 32A2C (1B503(1) once per dispatch: BP-4E, phase 16; 2E68D(p6 + 256 * p7
     * (107F9), CBW(p8), BP-C - 1) at 32F41; 3346B). */
    step_scene_once(st, XEEN_EXEC_PHASE_DAMAGE_SCENE);
    if (st->result == XEE_DONE) {
        if (step_served(st, XEEN_REQUEST_PARTY_DAMAGE, (uint16_t)(p[6] | p[7] << 8), (uint16_t)xeen_sign8(p[8]),
                        (uint16_t)(extended->selected_character - 1u), 0)) {
            s->event_flag = 1;
        }
    }
}

/**
 * Operation 15 (SetChar): the selector p6 (a signed byte) chooses whom the following operations affect. 0 selects
 * the whole party and stores 0 in the member selector byte; 1..6 and the negative selectors only store the selector
 * there and keep the selected member of an earlier choice; 7 selects a random member, 1..the
 * party count, and stores 1. 8 and above would ask the player who (the original's Who-will prompt); no shipped
 * script uses it and it is not reconstructed: it stops.
 * Original: 3232F (BP-C the selected member, 98D7 the selector); the random member 32341..3236F; the prompt
 * 2F9BA(22, 3, 0).
 */
static int op_set_character(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    const uint8_t *p = st->p;
    int8_t v = (int8_t)p[6];
    if (v > 7) {
        s->boundary = SET_CHARACTER_STOP_VALUE;
        st->result = XEE_UNSUPPORTED;
        return 1;
    }
    if (v == 7) {
        if (step_served(st, XEEN_REQUEST_RANDOM, 1, ds[XEEN_DS_PARTY_COUNT], 0, 0)) {
            extended->selected_character = (uint8_t)st->ax;
            ds[XEEN_DS_MEMBER_SELECTOR] = 1;
            s->event_flag = 1;
        }
    } else {
        if (!v) {
            extended->selected_character = 0;
        }
        ds[XEEN_DS_MEMBER_SELECTOR] = p[6];
        s->event_flag = 1;
    }
    return 0;
}

/**
 * Operations 14 and 46 (Remove, MakeNothingHere): Remove first takes the cell's object off the map (to the "removed"
 * position 80h, 80h); both then disable every event at the party's cell, in any direction and on any line, by
 * setting its operation to 0 in the event store, and ask the host to keep the changed store (a store the host
 * suspends is not resumable: see resumable_phase).
 * Original: 32253 (Remove; the object 98D9, one-based), 3227B (both: the cell ED97/ED98), 32C96 (BP-4F, the 4A24C
 * store, the next line).
 */
static void op_remove_object(exec_step *st)
{
    uint8_t *b = st->b;
    const xeen_event_index *idx = st->idx;
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    if (extended->phase == XEEN_EXEC_PHASE_HANDLER) {
        if (s->operation == 14 && ds[XEEN_DS_EVENT_OBJECT]) {
            uint16_t a = (uint16_t)(XEEN_DS_NUMBERED_OBJECTS +
                                    ds[XEEN_DS_EVENT_OBJECT] * XEEN_MAP_OBJECT_BYTES);
            ds[a + XEEN_MAP_OBJECT_X] = 0x80;
            ds[(uint16_t)(a + XEEN_MAP_OBJECT_Y)] = 0x80;
        }
        for (size_t row = 0; row < idx->record_count; ++row) {
            const xeen_event_record *target = &idx->records[row];
            if (target->field1 == (int16_t)xeen_sign8(ds[XEEN_DS_PARTY_X]) &&
                target->field2 == (int16_t)xeen_sign8(ds[XEEN_DS_PARTY_Y])) {
                b[target->offset + 5u] = 0;
            }
        }
        extended->phase = XEEN_EXEC_PHASE_REMOVE_STORE;
    }
    if (step_served(st, XEEN_REQUEST_STORE_EVENTS, 0, 0, 0, 0)) {
        s->event_flag = 1;
    }
}

/**
 * Operation 16 (Spawn): places map monster p6 (a signed byte) at (p7, p8): a random animation frame 0..7, no
 * condition, not yet active, and the hit points of its monster type from the monster data (a host request). The
 * position is written before the roll, so a roll the host refuses leaves the monster moved and otherwise unchanged.
 * Original: 32399; the record E530 + 14h x CBW p6, its frame 504DA(0, 7), its hit points the word at +14h of its row
 * (+6) in the 3Ch-byte rows at 50D4:0000 (the host's 3242C); 3346B.
 */
static void op_spawn_monster(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    const uint8_t *p = st->p;
    uint16_t a = (uint16_t)XEEN_MAP_MONSTER((uint16_t)xeen_sign8(p[6]));
    ds[(uint16_t)(a + XEEN_MAP_MONSTER_Y)] = p[8];
    ds[a + XEEN_MAP_MONSTER_X] = p[7];
    if (!step_served(st, XEEN_REQUEST_RANDOM, 0, 7, (uint16_t)(xeen_sign8(p[6]) * 0x14), 1)) {
        return;
    }
    ds[(uint16_t)(a + XEEN_MAP_MONSTER_FRAME)] = (uint8_t)st->ax;
    ds[(uint16_t)(a + XEEN_MAP_MONSTER_CONDITION)] = 0;
    ds[(uint16_t)(a + XEEN_MAP_MONSTER_ACTIVE)] = 0;
    if (step_served(st, XEEN_REQUEST_MONSTER_WORD, ds[(uint16_t)(a + XEEN_MAP_MONSTER_ID)], 0, 0, 0)) {
        xeen_put_le16(ds + (uint16_t)(a + XEEN_MAP_MONSTER_HP), st->ax);
        s->event_flag = 1;
    }
}

/**
 * Operation 19 (AfterMap): sets the wall of cell (p6, p7) on side p8 (0..3), or on all four sides when p8 is 4, to
 * p9, through the host's wall store (all values signed bytes). A side above 4 would index past the original's
 * four-row side tables and no shipped script has one: it stops. The stores are idempotent, so a request the host
 * suspends repeats the earlier sides unchanged when it resumes.
 * Original: 32481; 2DE1D(CBW p6, CBW p7, side, CBW p9) per side, the side in BP-6 for p8 = 4 (2DE1D is the host's
 * xeen_maze_set_wall: 1C680 for F494, then the slot's wall nibble; the side tables 18C2/7D2); 3346B.
 */
static int op_after_map(exec_step *st)
{
    xeen_event_state *s = st->s;
    const uint8_t *p = st->p;
    unsigned first = p[8] == 4u ? 0u : p[8], last = p[8] == 4u ? 3u : p[8];
    if (first > 3u) {
        s->boundary = AFTER_MAP_STOP_FIRST;
        st->result = XEE_UNSUPPORTED;
        return 1;
    }
    for (unsigned side = first; side <= last && st->result == XEE_DONE; ++side) {
        (void)step_served(st, XEEN_REQUEST_SET_WALL, (uint16_t)xeen_sign8(p[6]), (uint16_t)xeen_sign8(p[7]),
                          (uint16_t)side, (uint16_t)xeen_sign8(p[9]));
    }
    if (st->result == XEE_DONE) {
        s->event_flag = 1;
    }
    return 0;
}

/**
 * Operation 35 (AlterCellFlag): sets the flags of cell (p6, p7) to p8. The host first selects the map slot that holds
 * the cell (its metadata words). Outdoors the cell's map word loses its low four bits and is ORed with p8
 * sign-extended; indoors the cell's metadata byte loses its low three bits and is ORed with p8. A p8 wider than the
 * cleared bits (or negative, outdoors) also sets the bits above them, as in the original. The outdoor address uses
 * the signed coordinates without wrapping them into the slot; the indoor one masks them to 0..15.
 * Original: 32F7D; 1C844(CBW p6, CBW p7) (F494 and the F48A..F47E words); F608 set: the BDCC word at F494 x 37Ch +
 * 32 CBW p7 + 2 CBW p6, AND FFF0h, OR CBW p8; clear: the BFCC byte at (p7 & 15, p6 & 15), AND F8h, OR p8; 3346B.
 */
static void op_alter_cell_flag(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    const uint8_t *p = st->p;
    if (step_served(st, XEEN_REQUEST_CELL_METADATA, (uint16_t)xeen_sign8(p[6]), (uint16_t)xeen_sign8(p[7]), 0, 0)) {
        uint16_t base = (uint16_t)(xeen_get_le16(ds + XEEN_DS_MAP_SLOT) * XEEN_MAP_RECORD_BYTES), a;
        if (xeen_get_le16(ds + XEEN_DS_OUTDOORS)) {
            a = (uint16_t)(XEEN_DS_MAP_DATA + base + (uint16_t)(xeen_sign8(p[7]) * 32) +
                           (uint16_t)(xeen_sign8(p[6]) * 2));
            xeen_put_le16(ds + a, (uint16_t)((xeen_get_le16(ds + a) & 0xfff0u) |
                                             (uint16_t)xeen_sign8(p[8])));
        } else {
            a = (uint16_t)(XEEN_DS_MAP_FIELD(METADATA) + base + (uint16_t)((p[7] & 15u) * 16u) +
                           (uint16_t)(p[6] & 15u));
            ds[a] = (uint8_t)((ds[a] & 0xf8u) | p[8]);
        }
        s->event_flag = 1;
    }
}

/**
 * Operation 18 (Exit): ends the script (33690). Every entry runs it.
 */
static void op_exit(exec_step *st)
{
    xeen_event_state *s = st->s;
    st->result = script_end(s);
}

/**
 * Operation 54 (map query jump): the host's map query of the party's cell (its word result in XEEN_DS_CELL_SURFACE);
 * a result equal to p6 (a signed byte) jumps to line p7. Every entry runs it. Unlike step_refused, a failed query
 * (not a suspension) leaves the boundary as it was.
 * Original: 1C1C1(1), the word at F490.
 */
static void op_if_query_value(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    const uint8_t *p = st->p;
    int queried = step_request(st, XEEN_REQUEST_NAVIGATION_QUERY, 1, 0, 0, 0);
    if (!queried) {
        st->result = XEE_EXTERNAL;
        s->boundary = XEEN_REQUEST_NAVIGATION_QUERY;
    } else if (queried != 1) {
        st->result = XEE_INTERNAL;
    } else {
        uint16_t value = (uint16_t)(ds[XEEN_DS_CELL_SURFACE] | ((uint16_t)ds[XEEN_DS_CELL_SURFACE + 1u] << 8));
        st->branch = value == (uint16_t)xeen_sign8(p[6]);
        if (st->branch) {
            jump_to_line(s, p[7]);
        }
        s->event_flag = 1;
    }
}

/**
 * Operation 45 (ItemType): 8C74 = CBW(p6). Every entry runs it.
 */
static void op_item_type(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    const uint8_t *p = st->p;
    xeen_put_le16(ds + XEEN_DS_FIXED_ITEM, (uint16_t)xeen_sign8(p[6]));
    s->event_flag = 1;
}

/**
 * Operation 59 (FlipWorld): 96E2 = p6 (no BP-4F). Every entry runs it.
 */
static void op_flip_world(exec_step *st)
{
    uint8_t *ds = st->ds;
    const uint8_t *p = st->p;
    ds[XEEN_DS_MAP_SIDE] = p[6];
}

/**
 * Operation 60 (PlayCD): the PlayCD tuple (XEEN_DS_CD_PLAY_TRACK = CBW(p6); its start and end words p7..p10; no
 * BP-4F). Every entry runs it.
 */
static void op_play_cd(exec_step *st)
{
    uint8_t *ds = st->ds;
    const uint8_t *p = st->p;
    xeen_put_le16(ds + XEEN_DS_CD_PLAY_TRACK, (uint16_t)xeen_sign8(p[6]));
    ds[XEEN_DS_CD_PLAY_START] = p[7];
    ds[XEEN_DS_CD_PLAY_START + 1u] = p[8];
    ds[XEEN_DS_CD_PLAY_END] = p[9];
    ds[XEEN_DS_CD_PLAY_END + 1u] = p[10];
}

/**
 * Operation 17 (DoTownEvent): enters the location p6 (a shop, a guild, a temple ...), which the host runs. The first
 * pass hands the location to the host (phase LOCATION, external at XEEN_EVENT_ENTER_LOCATION); when it resumes, the
 * location's result becomes the dispatcher's return value, the party counts as having acted, and the script ends.
 * Original: 3245F; 277E5(record + 6), then BP-A = AX, 3067 = 1, BP-4F = 1 and the end 33690.
 */
static void op_location(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    if (extended->phase != XEEN_EXEC_PHASE_LOCATION) {
        extended->phase = XEEN_EXEC_PHASE_LOCATION;
        s->boundary = XEEN_EVENT_ENTER_LOCATION;
        st->result = XEE_EXTERNAL;
    } else {
        extended->return_ax = extended->location_ax;
        ds[XEEN_DS_PARTY_ACTED] = 1;
        s->event_flag = 1;
        st->result = script_end(s);
    }
}

/**
 * Operation 40 (PlayEventVoc): plays event voice p6 through the host: 0..5 are ahh, whereto, gulp, null, scream and
 * laff1.voc, whose names the voice table XEEN_DS_EVENT_VOICES points at. A larger p6 would read past the table; no
 * shipped script has one: it stops.
 * Original: 33064; 187E9(0, 0), 18975(the name at DS:303C + 4 x p6, 0, F42C), 187E9(F42C, 1); 3346B.
 */
static int op_play_voice(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    const uint8_t *p = st->p;
    if (p[6] > 5u) {
        s->boundary = VOICE_STOP_INDEX;
        st->result = XEE_UNSUPPORTED;
        return 1;
    }
    if (step_served(st, XEEN_REQUEST_VOICE, p[6], 0, xeen_get_le16(ds + XEEN_DS_EVENT_VOICES + 4u * p[6]), 0)) {
        s->event_flag = 1;
    }
    return 0;
}

/**
 * Operations 7 and 31 (TeleportAndExit, TeleportAndContinue): the host moves the party (phase TELEPORT, external at
 * XEEN_EVENT_TELEPORT). A nonzero map p6 is the destination and is kept in word_ax; map 0 means "the mirror's
 * destination": the row word_ax - 1 of the mirror table, word_ax being what an earlier operation of this invocation
 * left there (a word answer, a mirror row or a map: the frame slot BP-E). A word_ax this invocation never wrote
 * would be the original's stack residue; no reachable shipped record has one (map 49's map-0 teleport follows a
 * DoTownEvent, which ends the script), so it stops.
 * Original: 3193B; BP-E (word_ax) = map at 31955; the mirror row of the B416 table and the teleport 3196B.
 */
static int op_teleport(exec_step *st)
{
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    const uint8_t *p = st->p;
    if (p[6]) {
        extended->word_ax = p[6];
    } else if (extended->word_ax == XEEN_BP_E_UNSET) {
        s->boundary = TELEPORT_STOP_UNSET_ROW;
        st->result = XEE_UNSUPPORTED;
        return 1;
    }
    extended->phase = XEEN_EXEC_PHASE_TELEPORT;
    s->boundary = XEEN_EVENT_TELEPORT;
    st->result = XEE_EXTERNAL;
    return 0;
}

/**
 * Operation 36 (AlterHed): the party's cell in the loaded map's .hed data takes p6, and the byte after it p7. The
 * host owns the loaded .hed (its hed_cell service); no DS byte changes here.
 * Original: 32E6D; the .hed in DOS memory at 50D4:5F7C + ED98 x 64 + ED97 x 2 (CBW; 1083B's huge pointer); 3346B.
 */
static void op_alter_hed(exec_step *st)
{
    xeen_event_state *s = st->s;
    const uint8_t *p = st->p;
    if (step_served(st, XEEN_REQUEST_HED_CELL, p[6], p[7], 0, 0)) {
        s->event_flag = 1;
    }
}

/**
 * Operation 27 (SetVar, handler 32D3C): stores the value (width by type) through 2E285 for the selected
 * member, or for each member of the selection 0 (all) or 8 (all but 98D7).
 */
static void op_set_variable(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    const uint8_t *p = st->p;
    /* The value is p7 for byte variables, p7..p8 for the word variables 19h, 23h, 65h and 6Ah, and p7..p10 for the
     * dword variables 10h, 22h and 64h (little-endian). It is stored (setvar_store) for each member of the whole
     * party (selection 0) or of everyone but the member the selector names (8), else for the selected member.
     * Variable 84 is the party's facing; variable 8 the member's HP, its low word.
     * Original: 32D3C (the widths at 32DE9, 32DC5, 32D67); 2E285(type, value, member); 2E5BC (ED96) and 2E2D5
     * (+156h, p7 zero-extended). */
    unsigned sel = extended->selected_character, count = ds[XEEN_DS_PARTY_COUNT], k;
    uint32_t value = tg_value(p + 7, tg_width(p[6]));
    /* The script's end closes the windows and redraws the party bar (Original: 32D3C sets BP-18 = 1 for 33698's
     * 186C0 and 2ABB8(1)). */
    extended->window_cleanup = 1;
    /* one member: the selection minus one (Original: 32E4D, BP-C 1..6 here) */
    if (sel == 0u || sel == 8u) {
        for (k = 0; k < count; ++k) {
            if (sel == 0u || ds[XEEN_DS_MEMBER_SELECTOR] != k) {
                setvar_store(ds, p[6], value, k);
            }
        }
    } else {
        setvar_store(ds, p[6], value, sel - 1u);
    }
    s->event_flag = 1; /* 3346B BP-4F */
}

/**
 * Operation 11 (MoveObj, handler 31BEE): map object p6 already at (p7, p8) flips its mirrored bit, else moves
 * there.
 */
static void op_move_object(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    const uint8_t *p = st->p;
    /* 31BEE: object p6 already at (p7, p8) flips (+6 bit
     * 0), else moves there; 3346B. */
    uint16_t a = (uint16_t)(XEEN_DS_MAP_OBJECTS + (unsigned)(xeen_sign8(p[6]) * 8));
    if (ds[(uint16_t)(a + XEEN_MAP_OBJECT_Y)] == p[8] && ds[a + XEEN_MAP_OBJECT_X] == p[7]) {
        ds[(uint16_t)(a + XEEN_MAP_OBJECT_FLIP)] ^= 1u;
    } else {
        ds[a + XEEN_MAP_OBJECT_X] = p[7];
        ds[(uint16_t)(a + XEEN_MAP_OBJECT_Y)] = p[8];
    }
    s->event_flag = 1;
}

/**
 * Operation 23 (JumpRnd): the host rolls 1..p6; a roll equal to p7 jumps to line p8 (all signed bytes). The branch
 * is whether it jumped.
 * Original: 32A71; 504DA(1, CBW p6), equal to CBW p7: BP-4 = CBW p8 - 1; 3346B either way.
 */
static void op_jump_random(exec_step *st)
{
    xeen_event_state *s = st->s;
    const uint8_t *p = st->p;
    if (step_served(st, XEEN_REQUEST_RANDOM, 1, (uint16_t)xeen_sign8(p[6]), 0, 0)) {
        st->branch = st->ax == (uint16_t)xeen_sign8(p[7]);
        if (st->branch) {
            jump_to_line(s, p[8]);
        }
        s->event_flag = 1;
    }
}

/**
 * Operation 43 (SelectRandomChar): a random member, 1..the party count, becomes the selected member. Unlike
 * SelectChar's random selector (7) it leaves the member selector byte as it is.
 * Original: 33118; BP-C = 504DA(1, ED8C), the draw entering with AX = the count (the generator 50525 reads AL); 3346B.
 */
static void op_select_random_member(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    if (step_served(st, XEEN_REQUEST_RANDOM, 1, ds[XEEN_DS_PARTY_COUNT], 0, 0)) {
        extended->selected_character = (uint8_t)st->ax;
        s->event_flag = 1;
    }
}

/**
 * Operation 56 (GotoRandom): the host rolls 1..p6 and the script jumps to the line in the record's byte 6 + the roll
 * (as ChooseNumeric's jump, without its restore).
 */
static void op_goto_random(exec_step *st)
{
    uint8_t *b = st->b;
    size_t n = st->n;
    xeen_event_state *s = st->s;
    const xeen_event_record *r = st->r;
    const uint8_t *p = st->p;
    /* A random 1..p6 picks the line: the record's byte at 6 + the roll (sign-extended) is the target. Original:
     * 32AA0 (AX = 504DA(1, CBW(p6)); BP-4 = CBW(record[6 + AX]) - 1, as op 48's 3336B tail without its 4A1B7;
     * 3346B). */
    if (step_served(st, XEEN_REQUEST_RANDOM, 1, (uint16_t)xeen_sign8(p[6]), 0, 0)) {
        size_t at = 6u + (size_t)st->ax;
        if (n - (size_t)r->offset <= at) {
            st->result = XEE_MALFORMED;
        } else {
            jump_to_line(s, b[(size_t)r->offset + at]);
            st->branch = 1;
            s->event_flag = 1;
        }
    }
}

/**
 * Operation 42 (handler 330A5; ScummVM's IfMapFlag): jumps to line p7 when map monster p6 (FFh: every one of
 * the 9C59 records) is absent: a record (E530 + 14h * index) is present when its x and y are below 20h.
 */
static void op_if_monster_absent(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    const uint8_t *p = st->p;
    /* A map monster is present while it stands on the map (x and y below 32; a killed one is moved off it). Operand
     * 6 = FFh asks for any of the map's monsters, else for the monster it numbers (sign-extended). Absent: the script
     * jumps to operand 7's line. Original: 330A5 (E530 + 14h x index: x, y, unsigned), 9C59, CBW(p6); [BP-4] = p7 - 1;
     * 3346B either way. */
    int present = 0;
    if (p[6] == 0xffu) { /* any monster */
        for (unsigned k = 0; k < ds[XEEN_DS_MAP_MONSTER_COUNT] && !present; ++k) {
            uint16_t a = (uint16_t)XEEN_MAP_MONSTER(k);
            present = ds[a + XEEN_MAP_MONSTER_X] < 32u && ds[(uint16_t)(a + XEEN_MAP_MONSTER_Y)] < 32u;
        }
    } else {
        uint16_t a = (uint16_t)XEEN_MAP_MONSTER((uint16_t)xeen_sign8(p[6]));
        present = ds[a + XEEN_MAP_MONSTER_X] < 32u && ds[(uint16_t)(a + XEEN_MAP_MONSTER_Y)] < 32u;
    }
    st->branch = !present;
    if (st->branch) {
        jump_to_line(s, p[7]);
    }
    s->event_flag = 1;
}

/**
 * Operation 1 (Display0x01, handler 315F4): the record's text centred (CR, 03h, 'c', the text) into AC0F once
 * (XEEN_EXEC_PHASE_MAIN_TEXT), then window 38 by the host's 315F4.
 */
static int op_display_centered(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    if (extended->phase == XEEN_EXEC_PHASE_HANDLER) {
        /* AC0F = 14BBA(DS:62FA "\r\3c%s", A827) at 31600 (the format pushed at 315F9): the program data's format,
         * read here. A827 without its NUL in 1000 bytes is malformed, as before; the original's sprintf is
         * unbounded, so AC0F's expansion is bounded by DS only (1003 bytes for the program data's format); a format
         * with other conversions stops at 31600 with nothing written (none in the program data). */
        static const uint16_t text[] = {XEEN_DS_TEXT_BUFFER};
        int f;
        if (ds_text_length(ds, XEEN_DS_TEXT_BUFFER) == 1000u) {
            st->result = XEE_MALFORMED;
            return 1;
        }
        f = event_format(ds, st->dn, XEEN_TEXT_EVENT_CENTRED_FORMAT, text, 1u, XEEN_DS_TEXT_BUFFER_2,
                         st->dn - XEEN_DS_TEXT_BUFFER_2);
        if (f == EVENT_FORMAT_REFUSED) {
            s->boundary = CENTRED_TEXT_STOP_FORMAT;
            st->result = XEE_UNSUPPORTED;
            return 1;
        }
        if (f == EVENT_FORMAT_TOO_LONG) {
            st->result = XEE_MALFORMED;
            return 1; /* the end of DS */
        }
        extended->phase = XEEN_EXEC_PHASE_MAIN_TEXT;
    }
    if (step_served(st, XEEN_REQUEST_TEXT_WINDOW, 38, 0, XEEN_DS_TEXT_BUFFER, 1000)) {
        s->event_flag = 1;
    }
    return 0;
}

/* Operation 52 (FallToMap): the party falls to another map. The interpreter suspends; the host runs the fall and
 * resumes it in XEEN_EXEC_PHASE_FALL_DONE, where the dispatch restarts as after any host operation.
 * Original: 3341C (173A0(1)); resume_host_operation's 313CE restart. */
static void op_fall_to_map(exec_step *st)
{
    xeen_event_state *s = st->s;
    /* 3341C: 8C84..8C81 = record +6..+9 (+9 lies past a
     * three-parameter record: the store's next byte, or a
     * stale byte of the resident buffer), 173A0(1): the
     * host's (phase 17), then the 313CE restart. */
    s->boundary = XEEN_EVENT_FALL;
    st->result = XEE_EXTERNAL;
}

/**
 * Operation 32 (WhoWill, handler 2F9BA): the selected member: 1 with a party of one (2F9C2), else the host's
 * prompt; 0 (none) ends the script.
 */
static void op_who_will(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    const uint8_t *p = st->p;
    int handled = 1;
    if (ds[XEEN_DS_PARTY_COUNT] == 1u) {
        st->ax = 1; /* original 2F9C2 early return */
    } else {
        handled = step_request(st, XEEN_REQUEST_WHO_WILL, (uint16_t)xeen_sign8(p[6]), (uint16_t)xeen_sign8(p[7]), 1, 0);
    }
    if (!step_refused(st, handled, XEEN_REQUEST_WHO_WILL)) {
        extended->selected_character = (uint8_t)st->ax;
        s->event_flag = 1;
        if (!st->ax) {
            st->result = script_end(s);
        }
    }
}

/**
 * Operation 6 (PlayFX, handler 31929): with ED9A set (or already pending, XEEN_EXEC_PHASE_SOUND) the host's
 * 50ABB(CBW(p6)).
 */
static void op_play_sound(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    const uint8_t *p = st->p;
    if (ds[XEEN_DS_SOUND_ON] || extended->phase == XEEN_EXEC_PHASE_SOUND) {
        int handled;
        extended->phase = XEEN_EXEC_PHASE_SOUND;
        handled = step_request(st, XEEN_REQUEST_EVENT_SOUND, (uint16_t)xeen_sign8(p[6]), 0, 0, 0);
        (void)step_refused(st, handled, XEEN_REQUEST_EVENT_SOUND);
    }
    if (st->result == XEE_DONE) {
        s->event_flag = 1;
    }
}

/**
 * Operation 25 (CallEvent): pushes the return cell and line (at most five frames) and continues at (p6, p7)
 * line p8.
 */
static void op_call_event(exec_step *st)
{
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    const uint8_t *p = st->p;
    if (extended->stack_index != UINT16_MAX && extended->stack_index >= 4u) {
        st->result = XEE_MALFORMED;
    } else {
        size_t frame = (uint16_t)(extended->stack_index + 1u);
        extended->stack_index = (uint16_t)frame;
        extended->return_x[frame] = s->x;
        extended->return_y[frame] = s->y;
        extended->return_sequence[frame] = s->sequence;
        s->x = xeen_sign8(p[6]);
        s->y = xeen_sign8(p[7]);
        jump_to_line(s, p[8]);
        s->event_flag = 1;
    }
}

/**
 * Operation 26 (Return): pops the frame pushed by operation 25.
 */
static void op_return(exec_step *st)
{
    xeen_event_state *s = st->s;
    xeen_event_execution *extended = st->extended;
    if (extended->stack_index >= 5u) {
        st->result = XEE_MALFORMED;
    } else {
        size_t frame = extended->stack_index;
        s->x = extended->return_x[frame];
        s->y = extended->return_y[frame];
        s->sequence = extended->return_sequence[frame];
        extended->stack_index = (uint16_t)(extended->stack_index - 1u);
        s->event_flag = 1;
    }
}

/**
 * Operation 34 (MoveWallObj): record CBW(p6) of the eight-byte DD28 table gets p7 and p8 in its first two
 * bytes.
 */
static void op_move_wall_object(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    const uint8_t *p = st->p;
    uint16_t a = (uint16_t)(XEEN_DS_WALL_ITEMS + (unsigned)(xeen_sign8(p[6]) * 8));
    ds[a + XEEN_WALL_ITEM_X] = p[7];
    ds[(uint16_t)(a + XEEN_WALL_ITEM_Y)] = p[8];
    s->event_flag = 1;
}

/**
 * Operation 51 (ExchObj): map objects p6 and p7 exchange their positions.
 */
static void op_exchange_objects(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    const uint8_t *p = st->p;
    uint16_t a = (uint16_t)(XEEN_DS_MAP_OBJECTS + (unsigned)(xeen_sign8(p[6]) * 8));
    uint16_t c = (uint16_t)(XEEN_DS_MAP_OBJECTS + (unsigned)(xeen_sign8(p[7]) * 8));
    uint8_t x = ds[a + XEEN_MAP_OBJECT_X], y = ds[(uint16_t)(a + XEEN_MAP_OBJECT_Y)];
    ds[a + XEEN_MAP_OBJECT_X] = ds[c + XEEN_MAP_OBJECT_X];
    ds[(uint16_t)(a + XEEN_MAP_OBJECT_Y)] = ds[(uint16_t)(c + XEEN_MAP_OBJECT_Y)];
    ds[c + XEEN_MAP_OBJECT_X] = x;
    ds[(uint16_t)(c + XEEN_MAP_OBJECT_Y)] = y;
    s->event_flag = 1;
}

/*
 * Operations 30, 57 and 58, the game's endings (Clouds, Darkside, the World): each writes its own DS head, then the
 * shared tail runs on the host (the configuration file, the save, the score and the command line for XEEN.EXE,
 * which plays the ending's movie) and the script ends (ending_end).
 * Original: 334D8 (Clouds), 334AF (Darkside), 3349D (the World); the tail 33518 (xeen.cfg, 29502(1), 50AB1(CFh),
 * the score, XENCOMMAND). The heads' words EFEC (the World: 334A3), EFEA and the byte F0AC (the Darkside: 334B8,
 * 334B1) stay numeric: they lie in the party file (saved with it) and these stores are their only accesses in XEEN.DAT
 * (no reader to name them by). The Clouds head's arrival is map 1Ch (18, 4), the Darkside's map 1Dh (25, 21) north.
 */
static void op_ending(exec_step *st)
{
    uint8_t *ds = st->ds;
    xeen_event_state *s = st->s;
    uint16_t name;
    if (s->operation == XEEN_OP_ENDING_WORLD) {
        ds[XEEN_DS_CONFIG_WORLD_ENDING] = 1;
        xeen_put_le16(ds + 0xefec, 1);
        name = XEEN_DS_ENDING_NAMES;
    } /* 3349D: WORLDEND */
    else if (s->operation == XEEN_OP_ENDING_DARKSIDE) { /* 334AF: ENDGAME2 */
        ds[0xf0ac] = 1;
        ds[XEEN_DS_CONFIG_DARKSIDE_ENDING] = 1;
        xeen_put_le16(ds + 0xefea, 1);
        ds[XEEN_DS_PARTY_MAP] = 0x1d;
        ds[XEEN_DS_PARTY_X] = 0x19;
        ds[XEEN_DS_PARTY_Y] = 0x15;
        ds[XEEN_DS_PARTY_DIRECTION] = 0;
        name = XEEN_DS_ENDING_NAMES + 4u;
    } else { /* 334D8: ENDGAME */
        uint16_t from = xeen_get_le16(ds + XEEN_DS_CLOUDS_ENDING_MARK);
        size_t k = 0; /* 14CB6(8D42, *(DS:96)): the mark's string */
        xeen_put_le16(ds + XEEN_DS_ENDGAME_REACHED, 1);
        while (k < 64u && ds[(uint16_t)(from + k)]) {
            ++k;
        }
        if (k == 64u) {
            st->result = XEE_MALFORMED;
            return;
        }
        memmove(ds + XEEN_DS_CONFIG_CLOUDS_ENDING, ds + from, k + 1u);
        xeen_ds_set_bit(ds, XEEN_DS_GAME_FLAGS, 0x4bu, 1); /* 334F2: 2F8F3(F01F, 4Bh, 1) */
        ds[XEEN_DS_PARTY_MAP] = 0x1c;
        ds[XEEN_DS_PARTY_X] = 0x12;
        ds[XEEN_DS_PARTY_Y] = 4;
        name = XEEN_DS_ENDING_NAMES + 8u;
    }
    {
        int handled = step_request(st, XEEN_REQUEST_ENDING, name, 0, 0, 0);
        if (handled != 1) {
            st->result = XEE_INTERNAL;
            s->boundary = XEEN_REQUEST_ENDING;
            return;
        }
    }
    st->result = ending_end(st);
}


/*
 * The entry tiers. The three public entries fix them: xeen_event_execute has no extended state
 * (LEGACY), xeen_event_execute_expanded has it (EXPANDED), xeen_event_execute_interactive has it
 * and is interactive (INTERACTIVE). An operation needs a minimum tier; a lower tier stops it at the
 * dispatch (31590), or at the handler for the endings.
 */
typedef enum {
    ENTRY_LEGACY,
    ENTRY_EXPANDED,
    ENTRY_INTERACTIVE,
    ENTRY_NONE /* no entry runs it: an operation above 60 */
} entry_tier;

/**
 * The lowest entry tier that runs OPERATION. Operations above 60 have no handler (the original's 3159C sends them
 * to 33670): ENTRY_NONE, which no entry reaches.
 */
static entry_tier operation_tier(unsigned operation)
{
    switch (operation) {
        case 0:  /* the original's out-of-range default */
        case 13: /* no handler */
        case 47: /* no handler */
        case XEEN_OP_EXIT:
        case XEEN_OP_ITEM_TYPE:
        case XEEN_OP_MAP_QUERY_JUMP:
        case XEEN_OP_FLIP_WORLD:
        case XEEN_OP_PLAY_CD:
            return ENTRY_LEGACY;
        case XEEN_OP_NPC:
        case XEEN_OP_PLAY_FX:
        case XEEN_OP_SPAWN:
        case XEEN_OP_JUMP_RANDOM:
        case XEEN_OP_CALL_EVENT:
        case XEEN_OP_RETURN:
        case XEEN_OP_MOVE_WALL_OBJECT:
        case XEEN_OP_IF_NO_MONSTER:
        case XEEN_OP_SELECT_RANDOM_MEMBER:
        case XEEN_OP_EXCHANGE_OBJECTS:
        case XEEN_OP_GOTO_RANDOM:
            return ENTRY_EXPANDED;
        default: /* every other operation up to 60 */
            return operation <= 60u ? ENTRY_INTERACTIVE : ENTRY_NONE;
    }
}

/*
 * Each operation's handler (the original's handler table 3346B, as reconstructed; 0, 13 and 47 have none). A handler
 * that can end the step at once (a stop, a malformed record, a script end of its own) is a STEP handler returning
 * that int (1: the executor returns its status at once); the others are RUN handlers and the step always continues.
 * Each handler leaves the step's status in st->result.
 */
typedef struct {
    int (*step)(exec_step *st);
    void (*run)(exec_step *st);
} operation_handler;
static const operation_handler operation_handlers[XEEN_OP_PLAY_CD + 1] = {
        [XEEN_OP_EXIT] = {.run = op_exit},
        [XEEN_OP_ITEM_TYPE] = {.run = op_item_type},
        [XEEN_OP_MAP_QUERY_JUMP] = {.run = op_if_query_value},
        [XEEN_OP_FLIP_WORLD] = {.run = op_flip_world},
        [XEEN_OP_PLAY_CD] = {.run = op_play_cd},
        [XEEN_OP_DISPLAY_TEXT] = {.step = op_display_centered},
        [XEEN_OP_DOOR_TEXT_SMALL] = {.step = op_sign_text},
        [XEEN_OP_DOOR_TEXT_LARGE] = {.step = op_sign_text},
        [XEEN_OP_SIGN_TEXT] = {.step = op_sign_text},
        [XEEN_OP_SEAT_TEXT] = {.step = op_sign_text},
        [XEEN_OP_NPC] = {.run = op_npc},
        [XEEN_OP_PLAY_FX] = {.run = op_play_sound},
        [XEEN_OP_TELEPORT_AND_EXIT] = {.step = op_teleport},
        [XEEN_OP_TELEPORT_AND_CONTINUE] = {.step = op_teleport},
        [XEEN_OP_IF1] = {.step = op_if_condition},
        [XEEN_OP_IF2] = {.step = op_if_condition},
        [XEEN_OP_IF3] = {.step = op_if_condition},
        [XEEN_OP_MOVE_OBJECT] = {.run = op_move_object},
        [XEEN_OP_TAKE_OR_GIVE] = {.step = op_take_or_give},
        [XEEN_OP_REMOVE] = {.run = op_remove_object},
        [XEEN_OP_MAKE_NOTHING_HERE] = {.run = op_remove_object},
        [XEEN_OP_SELECT_MEMBER] = {.step = op_set_character},
        [XEEN_OP_SPAWN] = {.run = op_spawn_monster},
        [XEEN_OP_TOWN_EVENT] = {.run = op_location},
        [XEEN_OP_ALTER_MAP] = {.step = op_after_map},
        [XEEN_OP_GIVE_MULTI] = {.step = op_give_multi},
        [XEEN_OP_CONFIRM_WORD] = {.step = op_confirm_word},
        [XEEN_OP_CONFIRM_WORD_2] = {.step = op_confirm_word},
        [XEEN_OP_DAMAGE] = {.run = op_damage},
        [XEEN_OP_JUMP_RANDOM] = {.run = op_jump_random},
        [XEEN_OP_AFTER_EVENT] = {.run = op_after_event},
        [XEEN_OP_CALL_EVENT] = {.run = op_call_event},
        [XEEN_OP_RETURN] = {.run = op_return},
        [XEEN_OP_SET_VARIABLE] = {.run = op_set_variable},
        [XEEN_OP_TAKE_OR_GIVE_2] = {.step = op_conditional_give},
        [XEEN_OP_TAKE_OR_GIVE_3] = {.step = op_conditional_give},
        [XEEN_OP_TAKE_OR_GIVE_4] = {.step = op_conditional_give},
        [XEEN_OP_ENDING_CLOUDS] = {.run = op_ending},
        [XEEN_OP_ENDING_DARKSIDE] = {.run = op_ending},
        [XEEN_OP_ENDING_WORLD] = {.run = op_ending},
        [XEEN_OP_WHO_WILL] = {.run = op_who_will},
        [XEEN_OP_RANDOM_DAMAGE] = {.run = op_random_damage},
        [XEEN_OP_MOVE_WALL_OBJECT] = {.run = op_move_wall_object},
        [XEEN_OP_ALTER_CELL_FLAG] = {.run = op_alter_cell_flag},
        [XEEN_OP_ALTER_HED] = {.run = op_alter_hed},
        [XEEN_OP_DISPLAY_STAT] = {.step = op_display_member_text},
        [XEEN_OP_PLAY_VOICE] = {.step = op_play_voice},
        [XEEN_OP_DISPLAY_BOTTOM] = {.step = op_display_bottom},
        [XEEN_OP_DISPLAY_MAIN] = {.step = op_display_bottom},
        [XEEN_OP_IF_NO_MONSTER] = {.run = op_if_monster_absent},
        [XEEN_OP_SELECT_RANDOM_MEMBER] = {.run = op_select_random_member},
        [XEEN_OP_GIVE_ENCHANTED] = {.run = op_give_enchanted},
        [XEEN_OP_CHOOSE_NUMBER] = {.run = op_choose_numeric},
        [XEEN_OP_DISPLAY_TWO_LINES] = {.step = op_display_two_lines},
        [XEEN_OP_DISPLAY_LARGE] = {.run = op_display_large},
        [XEEN_OP_EXCHANGE_OBJECTS] = {.run = op_exchange_objects},
        [XEEN_OP_FALL_TO_MAP] = {.run = op_fall_to_map},
        [XEEN_OP_GOTO_RANDOM] = {.run = op_goto_random},
};
/**
 * Runs operation ST's handler (operation_handlers) once the gate (accept_operation) has accepted it in this entry:
 * the legacy tier's operations (operation_tier: 18, 45, 54, 59 and 60) in every entry, the others only with the
 * extended state (the gate has already refused every interactive-only operation outside the interactive entry, and
 * every extended one outside the extended entries). Returns 1 when the executor must return the step's status at
 * once (no trace, the step not counted), 0 when the step continues.
 */
static int perform_operation(exec_step *st)
{
    unsigned op = st->s->operation;
    const operation_handler *h;
    if (op > XEEN_OP_PLAY_CD || (!st->extended && operation_tier(op) != ENTRY_LEGACY)) {
        return 0;
    }
    h = &operation_handlers[op];
    if (h->step) {
        return h->step(st);
    }
    if (h->run) {
        h->run(st);
    }
    return 0;
}

/* Where an ending operation stops when its entry's tier does not run endings: the head of its handler
 * (Original: 334D8 Clouds, 334AF Darkside, 3349D the World). */
static uint32_t ending_stop(unsigned operation)
{
    switch (operation) {
        case XEEN_OP_ENDING_CLOUDS:
            return ENDING_STOP_CLOUDS;
        case XEEN_OP_ENDING_DARKSIDE:
            return ENDING_STOP_DARKSIDE;
        default:
            return ENDING_STOP_WORLD;
    }
}
/*
 * The record length (from its length byte through its last operand) of each operation whose operands have a fixed
 * size; 0: the default six bytes. The operations whose length depends on their operands (the Ifs, the take and give
 * records, SetVar) are measured in gate_operation. Six-byte records: 0, 13, 18 and 47 (no operands), 14 Remove and 46
 * MakeNothingHere (no parameters), 43 SelectRandomChar, 37 (the record's text with a member's name in window 12) and
 * the endings 30, 57 and 58 (XEEN.EXE plays the scene; here the heads, then the shared tail: the configuration,
 * the save, the score and the hand-off command; then the script ends, ending_end).
 * Original: the 3346B table, as reconstructed; the endings' tail 33518 (xeen.cfg, 29502(1), 50AB1(CFh), the score,
 * XEENCOMMAND); Damage 32A2C reads 2E68D(p6 + 256 * p7, CBW(p8), BP-C - 1).
 */
static const uint8_t fixed_record_length[XEEN_OP_PLAY_CD + 1] = {
        [XEEN_OP_DISPLAY_TEXT] = 7,
        [XEEN_OP_DOOR_TEXT_SMALL] = 7,
        [XEEN_OP_DOOR_TEXT_LARGE] = 7,
        [XEEN_OP_SIGN_TEXT] = 7,
        [XEEN_OP_NPC] = 10,
        [XEEN_OP_PLAY_FX] = 7,
        [XEEN_OP_TELEPORT_AND_EXIT] = 9,
        [XEEN_OP_MOVE_OBJECT] = 9,
        [XEEN_OP_SELECT_MEMBER] = 7,
        [XEEN_OP_SPAWN] = 9,
        [XEEN_OP_TOWN_EVENT] = 7,
        [XEEN_OP_ALTER_MAP] = 10,
        [XEEN_OP_CONFIRM_WORD] = 10,
        [XEEN_OP_DAMAGE] = 9,
        [XEEN_OP_JUMP_RANDOM] = 9,
        [XEEN_OP_AFTER_EVENT] = 8,
        [XEEN_OP_CALL_EVENT] = 9,
        [XEEN_OP_RETURN] = 6,
        [XEEN_OP_TELEPORT_AND_CONTINUE] = 9,
        [XEEN_OP_WHO_WILL] = 8,
        [XEEN_OP_RANDOM_DAMAGE] = 8,
        [XEEN_OP_MOVE_WALL_OBJECT] = 9,
        [XEEN_OP_ALTER_CELL_FLAG] = 9,
        [XEEN_OP_ALTER_HED] = 8,
        [XEEN_OP_SEAT_TEXT] = 7,
        [XEEN_OP_PLAY_VOICE] = 7,
        [XEEN_OP_DISPLAY_BOTTOM] = 7,
        [XEEN_OP_IF_NO_MONSTER] = 8,
        [XEEN_OP_GIVE_ENCHANTED] = 9,
        [XEEN_OP_ITEM_TYPE] = 7,
        [XEEN_OP_CHOOSE_NUMBER] = 8,
        [XEEN_OP_DISPLAY_TWO_LINES] = 8,
        [XEEN_OP_DISPLAY_LARGE] = 7,
        [XEEN_OP_EXCHANGE_OBJECTS] = 8,
        [XEEN_OP_FALL_TO_MAP] = 9,
        [XEEN_OP_DISPLAY_MAIN] = 7,
        [XEEN_OP_MAP_QUERY_JUMP] = 8,
        [XEEN_OP_CONFIRM_WORD_2] = 10,
        [XEEN_OP_GOTO_RANDOM] = 7,
        [XEEN_OP_FLIP_WORLD] = 7,
        [XEEN_OP_PLAY_CD] = 11,
};
/**
 * The tier gate and the operand length of operation ST. An operation that TIER does not run (operation_tier) stops
 * at the dispatch 31590, the endings at their heads. Otherwise returns XEE_DONE with NEED the record's length in
 * bytes (fixed_record_length, or measured here from the operands), or XEE_UNSUPPORTED (31590) for an If the entry
 * cannot run, or XEE_MALFORMED when a record's (mode, value) pairs run past its end. NEED stays 6 when the operation
 * does not run.
 */
static xeen_exec_status gate_operation(const exec_step *st, entry_tier tier, size_t *need)
{
    xeen_event_state *s = st->s;
    const uint8_t *p = st->p;
    if (tier < operation_tier(s->operation)) {
        if (s->operation == XEEN_OP_ENDING_CLOUDS || s->operation == XEEN_OP_ENDING_DARKSIDE ||
            s->operation == XEEN_OP_ENDING_WORLD) {
            s->boundary = ending_stop(s->operation);
            return XEE_UNSUPPORTED;
        }
        s->boundary = GATE_OPERATION_STOP_TIER;
        return XEE_UNSUPPORTED;
    }
    switch (s->operation) {
        case XEEN_OP_IF1:
        case XEEN_OP_IF2:
        case XEEN_OP_IF3:
            /* The three Ifs: the condition, its value (its width by condition), the line to jump to. Condition 44
             * (the host's yes/no) runs only as the yes/no (0) or "any key" (1); a record too short for its header,
             * or another form of 44, stops. Original: 31A6D (mode = operation - 8), the widths CS:2B94; 2F2FF. */
            if (p[0] < 6u || (p[6] == IF_YES_NO && p[7] > 1u)) {
                s->boundary = GATE_OPERATION_STOP_TIER;
                return XEE_UNSUPPORTED;
            }
            *need = 8u + tg_width(p[6]);
            return XEE_DONE;
        case XEEN_OP_TAKE_OR_GIVE:
        case XEEN_OP_TAKE_OR_GIVE_2:
        case XEEN_OP_TAKE_OR_GIVE_4:
            /* Two pairs: the take and the give (12), or the member test and the give (28, 38: op_conditional_give).
             * Original: 31C62 (op 12's handler, also with the opcode 1Ch or 26h). */
            return operand_pairs_end(p, 2u, need);
        case XEEN_OP_TAKE_OR_GIVE_3:
            /* Three pairs: two member tests, then the give. Original: 31C62 with the opcode 1Dh. */
            return operand_pairs_end(p, 3u, need);
        case XEEN_OP_GIVE_MULTI:
            /* Three pairs given in order. Original: 324E6 for 2DECE. */
            return operand_pairs_end(p, 3u, need);
        case XEEN_OP_SET_VARIABLE:
            *need = 7u + tg_width(p[0] >= 6u ? p[6] : 0u);
            return XEE_DONE;
        default: /* the tier gate leaves no operation above 60 */
            if (fixed_record_length[s->operation]) {
                *need = fixed_record_length[s->operation];
            }
            return XEE_DONE;
    }
}

/**
 * The operation gate: XEE_DONE when this entry runs operation ST and the record holds its operands,
 * else the status of gate_operation (XEE_UNSUPPORTED with the boundary of the original handler, or
 * 31590 for one this entry does not run; XEE_MALFORMED for a record's pairs past its end). A record
 * shorter than the operation's length (6 bytes for one that does not run) is XEE_MALFORMED, keeping
 * any boundary already set. INTERACTIVE is the interactive entry, which always has the extended
 * state.
 */
static xeen_exec_status accept_operation(const exec_step *st, int interactive)
{
    const entry_tier tier = !st->extended ? ENTRY_LEGACY
                            : interactive ? ENTRY_INTERACTIVE
                                          : ENTRY_EXPANDED;
    size_t need = 6u;
    xeen_exec_status result = gate_operation(st, tier, &need);
    if ((size_t)st->p[0] + 1u < need) {
        result = XEE_MALFORMED;
    }
    return result;
}

/**
 * The phases a pending record may resume in (after the resumes resume_host_operation completes
 * first). Phase 13 (operations 14/46's pending 4A24C) is written but never resumable here: a host that
 * suspends that store gets XEE_MALFORMED on the next call (native behaviour, kept).
 */
static int resumable_phase(unsigned phase)
{
    switch (phase) {
        case XEEN_EXEC_PHASE_TELEPORT:     /* consumed by resume_host_operation */
        case XEEN_EXEC_PHASE_REMOVE_STORE: /* see above */
        case XEEN_EXEC_PHASE_FALL_DONE:    /* consumed by resume_host_operation */
        case 19:
            return 0;
        default:
            return phase <= XEEN_EXEC_PHASE_RANDOM_DAMAGE;
    }
}

/**
 * The resumed record's operation must be the one PHASE belongs to (the phase validator): each suspended phase names
 * the operations that suspend in it (a sign's scene the door, sign and seat texts; the member loop the four take or
 * give forms; a chest's pick phases the multiple give, whose chest they belong to); the phases 0 (a new record), 1
 * (its handler) and 4 (its text copy pending) accept any operation.
 */
static int phase_accepts_operation(unsigned phase, unsigned operation)
{
    switch (phase) {
        case XEEN_EXEC_PHASE_NPC_TALK:
            return operation == XEEN_OP_NPC;
        case XEEN_EXEC_PHASE_SOUND:
            return operation == XEEN_OP_PLAY_FX;
        case XEEN_EXEC_PHASE_MAIN_TEXT:
            return operation == XEEN_OP_DISPLAY_TEXT;
        case XEEN_EXEC_PHASE_SIGN_SCENE:
            return operation == XEEN_OP_DOOR_TEXT_SMALL || operation == XEEN_OP_DOOR_TEXT_LARGE ||
                   operation == XEEN_OP_SIGN_TEXT || operation == XEEN_OP_SEAT_TEXT;
        case XEEN_EXEC_PHASE_AFTER_EVENT:
            return operation == XEEN_OP_AFTER_EVENT;
        case XEEN_EXEC_PHASE_LOCATION:
            return operation == XEEN_OP_TOWN_EVENT;
        case XEEN_EXEC_PHASE_MEMBER_LOOP:
            return operation == XEEN_OP_TAKE_OR_GIVE || operation == XEEN_OP_TAKE_OR_GIVE_2 ||
                   operation == XEEN_OP_TAKE_OR_GIVE_3 || operation == XEEN_OP_TAKE_OR_GIVE_4;
        case XEEN_EXEC_PHASE_SCENE:
        case XEEN_EXEC_PHASE_PAGED_TEXT:
            return operation == XEEN_OP_DISPLAY_BOTTOM || operation == XEEN_OP_DISPLAY_MAIN ||
                   operation == XEEN_OP_DISPLAY_LARGE;
        case XEEN_EXEC_PHASE_REMOVE_STORE: /* unreachable: resumable_phase refuses it */
            return operation == XEEN_OP_REMOVE || operation == XEEN_OP_MAKE_NOTHING_HERE;
        case XEEN_EXEC_PHASE_CHOICE_MADE:
            return operation == XEEN_OP_CHOOSE_NUMBER;
        case XEEN_EXEC_PHASE_WORD_ENTERED:
            return operation == XEEN_OP_CONFIRM_WORD || operation == XEEN_OP_CONFIRM_WORD_2;
        case XEEN_EXEC_PHASE_DAMAGE_SCENE:
            return operation == XEEN_OP_DAMAGE || operation == XEEN_OP_RANDOM_DAMAGE;
        case XEEN_EXEC_PHASE_TWO_LINES_WAIT:
            return operation == XEEN_OP_DISPLAY_TWO_LINES;
        case PICK_TRAP_ROLL:
        case PICK_TRAP_KIND:
        case PICK_ROLL:
        case PICK_PICKED_WAIT:
        case PICK_PICKED_TEXT:
        case PICK_FAILED_TEXT:
        case PICK_TRAP_SCENE:
            return operation == XEEN_OP_GIVE_MULTI;
        case XEEN_EXEC_PHASE_RANDOM_DAMAGE:
            return operation == XEEN_OP_RANDOM_DAMAGE;
        default:
            return 1;
    }
}

/**
 * The script restarts at the party's new cell (after operation 31's teleport scene and operation 52's fall): from
 * line 0 of the cell's events with the return value, the object lock and the shown scene cleared, the first member
 * selected, the call stack empty, and the map's events reloaded. The finished operation counts as a step.
 * Original: 313CE: BP-4 = 0, BP-A = 0, 9755 = 0, BP-4E = 0, BP-1E/BP-20 = ED97/ED98 (CBW), BP-C = 1 and 98D7 = 1,
 * 97F5 = 0, BP-40 = FFFFh; then 313FD (the reload).
 */
static void restart_at_party_cell(xeen_event_execution *extended, xeen_event_state *s, uint8_t *ds,
                                  size_t *used)
{
    extended->phase = XEEN_EXEC_PHASE_NEW_RECORD;
    s->event_flag = 1;
    ++*used;
    ++s->steps;
    s->sequence = 0;
    extended->return_ax = 0;
    ds[XEEN_DS_OBJECT_LOCK] = 0;
    extended->scene_shown = 0;
    s->x = xeen_sign8(ds[XEEN_DS_PARTY_X]);
    s->y = xeen_sign8(ds[XEEN_DS_PARTY_Y]);
    extended->selected_character = 1;
    ds[XEEN_DS_MEMBER_SELECTOR] = 1;
    ds[XEEN_DS_EVENT_DAMAGE_TARGET] = 0;
    extended->stack_index = UINT16_MAX;
    s->reload_pending = 1;
}

/**
 * The script ends after an operation the host finished (the finished operation counts as a step). Original: 33690.
 */
static xeen_exec_status end_after_host(xeen_event_execution *extended, xeen_event_state *s,
                                       size_t *used)
{
    extended->phase = XEEN_EXEC_PHASE_NEW_RECORD;
    s->event_flag = 1;
    ++*used;
    ++s->steps;
    return script_end(s);
}

/* What one call of the interpreter runs with, shared by its steps: the event store B (N bytes) and its index, DS (DN
 * bytes), the state S, the host's services (the legacy entry's COPY with CU; the extended entries' DISPATCH with the
 * extended state EXTENDED, NULL for the legacy entry) and whether this is the interactive entry. */
typedef struct {
    uint8_t *b;
    size_t n;
    const xeen_event_index *idx;
    uint8_t *ds;
    size_t dn;
    xeen_event_state *s;
    xeen_event_service_fn copy;
    void *cu;
    xeen_event_execution *extended;
    xeen_event_request_fn dispatch;
    int interactive;
} exec_run;

/** resume_host_operation's outcomes. */
enum {
    HOST_RESUME_NONE,
    HOST_RESUME_RESTART,
    HOST_RESUME_RETURN
};

/**
 * Completes an operation the host ran before any record is read again (its record may be gone): op 31's
 * teleport (its 1B503(1), then the restart), op 7's teleport and a location that moved to another map
 * (the script ends), op 52's fall (the restart). Returns HOST_RESUME_RETURN with the status in
 * *STATUS, HOST_RESUME_RESTART when the record search starts again, HOST_RESUME_NONE when no such
 * phase is pending.
 */
static int resume_host_operation(const exec_run *run, size_t *used, xeen_exec_status *status)
{
    xeen_event_execution *extended = run->extended;
    xeen_event_state *s = run->s;
    uint8_t *ds = run->ds;
    if (extended->phase == XEEN_EXEC_PHASE_TELEPORT && s->operation == 31u) {
        /* TeleportAndContinue: the scene at the new place, then the record search restarts at the party's new cell
         * without reading the record again. Original: 31A57 in 3193B (DI = 1Fh), 1B503(1), the 313CE restart as in
         * phase 17. */
        int handled =
                request(extended, run->dispatch, run->copy, run->cu, XEEN_REQUEST_DRAW_SCENE, 1, 0, 0, 0, ds, run->dn,
                        &(uint16_t){0});
        if (handled != 1) {
            s->boundary = XEEN_REQUEST_DRAW_SCENE;
            *status = handled == 0 ? XEE_EXTERNAL : XEE_INTERNAL;
            return HOST_RESUME_RETURN;
        }
        restart_at_party_cell(extended, s, ds, used);
        return HOST_RESUME_RESTART;
    }
    if (extended->phase == XEEN_EXEC_PHASE_TELEPORT) {
        /* TeleportAndExit: the host ran the teleport, possibly to another map; the script ends without reading its
         * record again, whose map may be gone. Original: 3193B..31A52 (another map's 19E82), 31A5C ends the script
         * (33690). */
        *status = end_after_host(extended, s, used);
        return HOST_RESUME_RETURN;
    }
    if (extended->phase == XEEN_EXEC_PHASE_FALL_DONE) {
        /* FallToMap: the host ran the fall; the record search restarts at the party's cell. Original: 173A0(1),
         * 33444 BP-4F = 1, the 313CE restart. */
        restart_at_party_cell(extended, s, ds, used);
        return HOST_RESUME_RESTART;
    }
    if (extended->phase == XEEN_EXEC_PHASE_LOCATION && extended->location_moved) {
        /* A town location that left for another map: its result is kept and the script ends, as op_location's
         * resume does. Original: 3246F (BP-A = AX, 3067 = 1, BP-4F = 1, 33690). */
        extended->location_moved = 0;
        extended->return_ax = extended->location_ax;
        ds[XEEN_DS_PARTY_ACTED] = 1;
        *status = end_after_host(extended, s, used);
        return HOST_RESUME_RETURN;
    }
    return HOST_RESUME_NONE;
}

/**
 * After a step: with a monster in view the step's event is deferred (a later scene's tail runs it, Original:
 * 1C183) and the script ends; otherwise the host restores the event store, which must still match
 * the index. Returns XEE_DONE to continue, else the status to return. AX is the step's AX word (the host may write
 * it). Original: 313FD (98B4 not FFh: 9756 = 1 and 33690; else 9756 = 0 and 4A1B7).
 */
static xeen_exec_status reload_store(const exec_run *run, uint16_t *ax)
{
    uint8_t *ds = run->ds;
    xeen_event_state *s = run->s;
    int service;
    if (ds[XEEN_DS_VIEW_MONSTER(0)] != XEEN_VIEW_NO_MONSTER) {
        ds[XEEN_DS_EVENT_DEFERRED] = 1;
        return script_end(s);
    }
    ds[XEEN_DS_EVENT_DEFERRED] = 0;
    service = request(run->extended, run->dispatch, run->copy, run->cu, XEEN_REQUEST_RESTORE_EVENTS, 0, 0, 0, 0, ds,
                      run->dn, ax);
    if (!service) {
        s->boundary = XEEN_REQUEST_RESTORE_EVENTS;
        return XEE_EXTERNAL;
    }
    if (service != 1) {
        return XEE_INTERNAL;
    }
    if (!valid(run->b, run->n, run->idx)) {
        return XEE_MALFORMED;
    }
    s->reload_pending = 0;
    return XEE_DONE;
}

/**
 * 31429's record search: the first index record at the cell (S's x, y; never the origin facing 0)
 * on the current line, facing the party (ED96) or any way (4); a record at the cell and line facing
 * elsewhere sets direction_mismatch. Returns its row, or the record count when none matches.
 */
static size_t find_record(const xeen_event_index *idx, const uint8_t *ds, xeen_event_state *s)
{
    size_t i;
    for (i = 0; i < idx->record_count; ++i) {
        const xeen_event_record *r = &idx->records[i];
        if (r->field1 != s->x || r->field2 != s->y ||
            (((uint16_t)s->x | (uint16_t)s->y | ds[XEEN_DS_PARTY_DIRECTION]) == 0) ||
            r->field4 != s->sequence) {
            continue;
        }
        if (r->field3 != (int16_t)ds[XEEN_DS_PARTY_DIRECTION] && r->field3 != 4) {
            s->direction_mismatch = 1;
            continue;
        }
        break;
    }
    return i;
}

/**
 * The record this step runs: a pending record resumes in its phase (it must still be the state's row and offset),
 * else the cell's next matching record (find_record). 1 with *ROW; 0 with *STATUS: XEE_MALFORMED for a resume that
 * does not fit, or the script's end when no record matches (XEE_EXTERNAL at XEEN_EVENT_SCRIPT_END; the logical
 * result says whether any step ran).
 */
static int select_record(const exec_run *run, size_t *row, xeen_exec_status *status)
{
    const xeen_event_index *idx = run->idx;
    xeen_event_state *s = run->s;
    size_t i;
    if (run->extended && run->extended->phase) {
        if (!resumable_phase(run->extended->phase) || s->row >= idx->record_count) {
            *status = XEE_MALFORMED;
            return 0;
        }
        i = s->row;
        if (idx->records[i].offset != s->offset) {
            *status = XEE_MALFORMED;
            return 0;
        }
    } else {
        i = find_record(idx, run->ds, s);
    }
    if (i == idx->record_count) {
        s->boundary = XEEN_EVENT_SCRIPT_END;
        s->logical_result = s->steps ? XEE_DONE : XEE_NO_MATCH;
        *status = XEE_EXTERNAL;
        return 0;
    }
    *row = i;
    return 1;
}

/**
 * Runs the selected record R (its bytes at P) after its setup: the record's text copied once (the host's 15616;
 * a resumed record has it already), the gate (accept_operation), the handler (perform_operation), then the next
 * line, or the script's end when the party has been defeated. *AX is the value the handler is entered with (the
 * reload's or the copy's), *BRANCH the handler's branch for the trace. Returns the step's status; *FINISHED is set
 * when the handler ended the step itself (execute returns its status at once, without the step count or the trace).
 */
static xeen_exec_status run_record(const exec_run *run, const xeen_event_record *r, const uint8_t *p, uint16_t *ax,
                                   int *branch, int *finished)
{
    xeen_event_execution *extended = run->extended;
    xeen_event_state *s = run->s;
    uint8_t *ds = run->ds;
    int service;
    /* A parameterless last record (the store's final Return) puts its text selector past the store, in DOS memory
     * the native does not keep; selector 0 stands for it. The copy pads the text buffer to 1000 bytes, so the next
     * record's copy leaves no trace of it. Original: +6, 15616 into A827. */
    if (run->n - (size_t)r->offset < 6u) {
        return XEE_MALFORMED;
    }
    service = (extended && extended->phase && extended->phase != XEEN_EXEC_PHASE_COPY_PENDING)
                      ? 1
                      : request(extended, run->dispatch, run->copy, run->cu, XEEN_REQUEST_COPY_TEXT,
                                run->n - (size_t)r->offset > 6u ? p[6] : 0u, 0, XEEN_DS_TEXT_BUFFER, 1000, ds,
                                run->dn, ax);
    if (service == 0) {
        s->boundary = XEEN_REQUEST_COPY_TEXT;
        return XEE_EXTERNAL;
    }
    if (service != 1) {
        return XEE_INTERNAL;
    }
    {
        exec_step st = {.b = run->b,
                        .n = run->n,
                        .idx = run->idx,
                        .r = r,
                        .p = p,
                        .ds = ds,
                        .dn = run->dn,
                        .s = s,
                        .extended = extended,
                        .dispatch = run->dispatch,
                        .copy = run->copy,
                        .cu = run->cu,
                        .ax = *ax,
                        .branch = *branch};
        if (extended && extended->phase == XEEN_EXEC_PHASE_COPY_PENDING) {
            extended->phase = XEEN_EXEC_PHASE_HANDLER;
        }
        st.result = accept_operation(&st, run->interactive);
        if (st.result == XEE_DONE && perform_operation(&st)) {
            *finished = 1;
            return st.result;
        }
        *branch = st.branch;
        if (st.result == XEE_DONE) {
            if (ds[XEEN_DS_PARTY_DEFEATED]) {
                return script_end(s);
            }
            s->sequence = (uint16_t)(s->sequence + 1u);
            s->reload_pending = 1;
        }
        return st.result;
    }
}

/* execute's work before the record search, left by the previous step: the store reload (reload_store; *AX the AX word
 * its host request may write) and, with the extended state, an operation the host has finished
 * (resume_host_operation). */
enum {
    STEP_SEARCH,  /* go on with the record search */
    STEP_RESTART, /* the record search starts again */
    STEP_RETURN   /* execute returns *STATUS */
};
static int pending_work(const exec_run *run, size_t *used, uint16_t *ax, xeen_exec_status *status)
{
    if (run->s->reload_pending && (*status = reload_store(run, ax)) != XEE_DONE) {
        return STEP_RETURN;
    }
    if (run->extended) {
        switch (resume_host_operation(run, used, status)) {
            case HOST_RESUME_RESTART:
                return STEP_RESTART;
            case HOST_RESUME_RETURN:
                return STEP_RETURN;
            default:
                break;
        }
    }
    return STEP_SEARCH;
}
/* Enters the selected record ROW as the step's: the state's row, offset and operation; *BEFORE the line it starts on.
 * A record shorter than its operation byte, or (resuming) an operation its phase does not belong to (26, 27), is
 * XEE_MALFORMED (0). A newly selected record sets the event UI mode once, before its common copy (the copy then
 * pending). */
static int enter_record(const exec_run *run, size_t row, uint16_t *before)
{
    const xeen_event_record *r = &run->idx->records[row];
    const uint8_t *p = run->b + r->offset;
    xeen_event_state *s = run->s;
    xeen_event_execution *extended = run->extended;
    s->row = (uint16_t)row;
    s->offset = r->offset;
    if (p[0] < 5u) {
        return 0;
    }
    s->operation = p[5];
    *before = s->sequence;
    if (extended && !phase_accepts_operation(extended->phase, s->operation)) {
        return 0;
    }
    if (!extended || !extended->phase) {
        run->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_EVENT; /* occurs once before a newly selected record's copy */
        if (extended) {
            extended->phase = XEEN_EXEC_PHASE_COPY_PENDING; /* pending common copy */
        }
    }
    return 1;
}
/* A step's end: a completed record (or the script's end it reached) counts as a step (the extended state's next
 * record then new), and the trace gets the step (row, offset, lines BEFORE and after, operation, the record's bytes,
 * RESULT, the boundary and the handler's BRANCH). */
static void end_step(const exec_run *run, size_t row, uint16_t before, xeen_exec_status result, int branch,
                     size_t *used, xeen_event_trace_fn trace, void *tu)
{
    xeen_event_state *s = run->s;
    const xeen_event_record *r = &run->idx->records[row];
    const uint8_t *p = run->b + r->offset;
    if (result == XEE_DONE || (result == XEE_EXTERNAL && s->boundary == XEEN_EVENT_SCRIPT_END)) {
        ++*used;
        ++s->steps;
        if (run->extended) {
            run->extended->phase = XEEN_EXEC_PHASE_NEW_RECORD;
        }
    }
    if (trace) {
        xeen_event_trace t = {
                (uint16_t)row, r->offset,         before, s->sequence, s->operation,
                p,             (size_t)p[0] + 1u, result, s->boundary, (int8_t)branch};
        trace(tu, &t);
    }
}
/**
 * The event script interpreter: runs the operations of the event selected by RUN's index from its state until it
 * ends, needs the host, or uses BUDGET operations, reproducing each original operation's effects on DS. Each step:
 * the work the previous step left (pending_work: the store reload 313FD, an operation the host finished), the record
 * (select_record), its entry and the phase validator (enter_record), the record's run (run_record: the common text
 * copy, the gate, the handler, the next line), then the step count and the trace (end_step). A handler that ends the
 * step at once returns its status without the count or the trace. Returns the execution status.
 */
static xeen_exec_status execute(const exec_run *run, size_t budget, xeen_event_trace_fn trace, void *tu)
{
    xeen_event_state *s = run->s;
    size_t used = 0;
    if (!s || !run->idx || !run->ds || run->dn < 65536u || (!run->b && run->n) || !valid(run->b, run->n, run->idx)) {
        return XEE_MALFORMED;
    }
    s->boundary = 0;
    for (;;) {
        size_t i;
        xeen_exec_status result = XEE_DONE;
        uint16_t before, ax = 0;
        int branch = -1, finished = 0;
        switch (pending_work(run, &used, &ax, &result)) {
            case STEP_RESTART:
                continue;
            case STEP_RETURN:
                return result;
            default:
                break;
        }
        if (!select_record(run, &i, &result)) {
            return result;
        }
        if (used == budget) {
            return XEE_BUDGET;
        }
        if (!enter_record(run, i, &before)) {
            return XEE_MALFORMED;
        }
        result = run_record(run, &run->idx->records[i], run->b + run->idx->records[i].offset, &ax, &branch, &finished);
        if (finished) {
            return result;
        }
        end_step(run, i, before, result, branch, &used, trace, tu);
        if (result != XEE_DONE) {
            return result;
        }
    }
}

xeen_exec_status xeen_event_execute(uint8_t *b, size_t n, const xeen_event_index *idx, uint8_t *ds,
                                    size_t dn, xeen_event_state *s, size_t budget,
                                    xeen_event_service_fn service, void *user,
                                    xeen_event_trace_fn trace, void *tu)
{
    const exec_run run = {b, n, idx, ds, dn, s, service, user, NULL, NULL, 0};
    return execute(&run, budget, trace, tu);
}
/**
 * Starts an extended event execution with a copy of DS's event state; returns XEE_MALFORMED without
 * a full DS.
 */
xeen_exec_status xeen_event_execution_begin(xeen_event_execution *e, const uint8_t *ds, size_t dn)
{
    if (!e || !ds || dn < 65536u) {
        return XEE_MALFORMED;
    }
    memset(e, 0, sizeof(*e));
    e->stack_index = UINT16_MAX;
    e->word_ax = XEEN_BP_E_UNSET;
    return xeen_event_begin(&e->state, ds, dn);
}
xeen_exec_status xeen_event_execute_expanded(uint8_t *b, size_t n, const xeen_event_index *idx,
                                             uint8_t *ds, size_t dn, xeen_event_execution *e,
                                             size_t budget, xeen_event_request_fn service,
                                             void *user, xeen_event_trace_fn trace, void *tu)
{
    if (!e) {
        return XEE_MALFORMED;
    }
    {
        const exec_run run = {b, n, idx, ds, dn, &e->state, NULL, user, e, service, 0};
        return execute(&run, budget, trace, tu);
    }
}

xeen_exec_status xeen_event_execute_interactive(uint8_t *b, size_t n, const xeen_event_index *idx,
                                                uint8_t *ds, size_t dn, xeen_event_execution *e,
                                                size_t budget, xeen_event_request_fn service,
                                                void *user, xeen_event_trace_fn trace, void *tu)
{
    if (!e) {
        return XEE_MALFORMED;
    }
    {
        const exec_run run = {b, n, idx, ds, dn, &e->state, NULL, user, e, service, 1};
        return execute(&run, budget, trace, tu);
    }
}
