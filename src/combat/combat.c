#include "xeen/party.h"
#include "xeen/monster_record.h"
#include "xeen/ds_globals.h"
#include "xeen/ds_texts.h"
#include "xeen/combat.h"
#include "xeen/character_record.h"
#include "xeen/modal.h"
#include "xeen/gameplay_tick.h"
#include "xeen/monster_update.h"
#include "xeen/items.h"
#include "xeen/map_objects.h"
#include "xeen/item_tables.h"
#include "xeen/scene.h"
#include "xeen/scene_construct.h"
#include "xeen/projectile.h"
#include "xeen/minimap.h"
#include "xeen/interface.h"
#include "xeen/treasure.h"
#include "xeen/world_entry.h"
#include "xeen/addon/damage_numbers.h"
#include "xeen/addon/town_icons.h"
#include "xeen/addon/wizard_eye_plus.h"
#include "xeen/byte_order.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where a combat round stops when it cannot go on natively; each value is the original address of the step
 * (tools/xeen_replay/stops.json). Most guard records, tables and buffers the original reads without a bounds check,
 * or a host service that failed.
 */
enum {
    COMBAT_STOP_SOUND = 0x1880e,                  /* an effect was refused */
    COMBAT_STOP_HIT_SOUND = 0x1880e,              /* the hit's effect needs the host */
    COMBAT_STOP_SKY_RELOAD = 0x30004,             /* Phase 284: the fight's sky reload still needed (XEEN_TICK_SKY_RELOAD) */
    DROP_STOP_LEVEL = 0x36cba,                    /* a monster's drop above level 7 */
    NEST_STOP_ENTRY = 0x1c17e,                    /* a nested fight without a live contact site, or too deep */
    NEST_STOP_ICONS_PUSH = 0x190a3,
    NEST_STOP_ICONS_POP = 0x190cf,
    NEST_STOP_DEFEATED_AT_ENTRY = 0x39063,        /* the party defeated by the entry's own attack: not witnessed */
    NEST_STOP_NOT_DEFEATED = 0x39063,             /* the nested fight itself: not reached */
    NEST_STOP_SIGN = 0x3955e,                     /* the sign is never set inside combat */
    SCENE_STOP_OUTDOOR_MONSTERS = 0x2126c,
    SCENE_STOP_OUTDOOR_OBJECTS = 0x219e4,
    SCENE_STOP_MONSTERS = 0x1ed46,
    SCENE_STOP_OBJECTS = 0x20011,
    SCENE_STOP_WALLS = 0x208fb,
    SCENE_STOP_DRAW = 0x1e467,                    /* the scene could not be drawn */
    SCENE_STOP_CELL_EVENT = 0x1c18a,              /* the party cell's event during the scene */
    ORDER_STOP_COMBATANTS = 0x38d25,              /* the turn order past its table, or a member past the six */
    WEAPON_STOP_DICE = 0x38a61,                   /* a weapon's dice without sides */
    HIT_STOP_CLASS = 0x3885f,                     /* a member's class past the ten */
    EXPERIENCE_STOP_NO_ELIGIBLE = 0x36f6b,        /* no member can take the experience */
    MONSTER_DAMAGE_STOP_SLOT = 0x36738,           /* a slot, record or damage type out of range */
    MONSTER_DAMAGE_STOP_SPELL = 0x36931,          /* a damage spell past Magic Arrow */
    MONSTER_DAMAGE_STOP_WEAPON = 0x36a2f,         /* a weapon index past the 40 */
    ATTACK_STOP_TARGET = 0x371e2,                 /* no target, or its record */
    ATTACK_STOP_CLASS = 0x3720a,
    VOLLEY_STOP_PARTY = 0x37612,                  /* more than eight members */
    VOLLEY_STOP_TARGET = 0x38818,
    VOLLEY_STOP_ROWS = 0x1c680,                   /* the volley's rows of cells */
    PROJECTILE_STOP_SPRITES = 0x18931,
    SPELL_HIT_STOP_TARGET = 0x37621,
    SPELL_HIT_STOP_CASTER = 0x37699,              /* the caster's record past the data segment */
    TIER_STOP_QUERY = 0x1c1c1,                    /* the map query for the attack tier */
    PARTY_DAMAGE_STOP_DICE = 0x37bb5,             /* dice without a die */
    TARGET_STOP_RANDOM = 0x382e3,                 /* no target for the random pick */
    MONSTER_ACTOR_STOP_RANGED_ID = 0x38163,       /* a ranged monster's id outside the database */
    MONSTER_ACTOR_STOP_SLOT = 0x3805b,            /* an attacker's view slot holds no monster */
    MONSTER_ACTOR_STOP_RECORD = 0x380ed,
    MEMBER_ATTACK_STOP_TO_HIT = 0x38500,          /* a monster without a to-hit value */
    MONSTER_TURN_STOP_TARGET = 0x38419,           /* a target past the party */
    TEXT_STOP_MONSTER = 0x38b51,                  /* the message's monster record */
    TEXT_STOP_NAME = 0x38c14,                     /* the monster's name has no end, or the message no room */
    TEXT_STOP_SPACE = 0x38d1b,                    /* the message would reach the party records */
    LOOP_TOP_STOP_TEXT = 0x3909c,
    LOOP_TOP_STOP_DRAW = 0x390c0,
    LOOP_TOP_STOP_FRAME = 0x390ca,
    CONTINUATION_STOP_DATABASE = 0x393b2,         /* the monster database shorter than its Ice Troll row */
    COMMAND_STOP_ATTACK_ACTOR = 0x39203,          /* the actor is not a party member */
    COMMAND_STOP_BLOCK_ACTOR = 0x39291,
    COMMAND_STOP_RUN_ACTOR = 0x39260,
    COMMAND_STOP_QUICK_FIGHT_ACTOR = 0x391de,
    RUN_STOP_DEFEATED_ESCAPE = 0x38b16,           /* the defeated party's run succeeds in a nested fight */
    USE_STOP_DEFEATED_ITEM = 0x39249,              /* an item used by the defeated party in a nested fight */
    /* variable stop sites (stops_variable.json) */
    MONSTER_UPDATE_STOP_ROUND = 0x2a701      /* the monsters' update stopped without reporting where */
};
/* Two stops that report the command poll's address: the fight would go on there, but this call cannot take it. */
enum {
    COMMAND_STOP_NO_SERVICES = 0x3912c,           /* a command needs the RNG, the monster database and (sound on) the
                                                   * audio */
    CHARGE_STOP_ATTACKERS_AGAIN = 0x3912c         /* the no-attackers time charge's scene found attackers again */
};
/* Explicit ends that a caller reports (SERVICE_REQUIRED or BAD_INPUT with the address), outside the stop registry's
 * idioms: the caller decides whether they stop the game. */
enum {
    IDLE_VOICE_PAST_DATABASE = 0x3911f,           /* the target's attack voice lies past the monster database */
    PARTY_DAMAGE_TARGETS_PAST_PARTY = 0x2e72b,    /* the members to damage run past the party or the six */
    PARTY_DAMAGE_TYPE_UNKNOWN = 0x2e74f,          /* a damage type above 7 (the original would leave DI unset) */
    PARTY_DAMAGE_CONTACT_FIGHT = 0x1c17e          /* the damage's timed wait would start a contact fight */
};
/* Which of the round's two time charges (2FC7E calls) a sky reload interrupted (c->sky_site): the round reset's,
 * after which the round goes on with its attackers, or the no-attackers charge before the victory tests. */
enum {
    TIME_CHARGE_ROUND_RESET = 0x393d3,
    TIME_CHARGE_NO_ATTACKERS = 0x393f0
};
/* The exploration set's button 16 ('=', the minimap) has its x word at 199Ch: combat's entry moves it to 0 (38F85),
 * its end back to ECh (3950E), the set's own value (written only there). */
#define MINIMAP_BUTTON_X      (XEEN_DS_EXPLORATION_BUTTON_X + 2u * 16u)
#define MINIMAP_BUTTON_HOME_X 0xecu

/* Addresses are analysis placement (file offset = address - A940). The
 * combat code segment is 2667; far rule tables quoted below were read from
 * the protected XEEN.DAT image (segments 40D0, 40D2, 47B1, 47B4, 47FA). */

#define MONSTER_RECORD(i) ((uint16_t)XEEN_MAP_MONSTER((unsigned)(i)))
#define MEMBER(m, i)                \
    ((m)->ds + XEEN_PARTY_RECORDS + \
     (unsigned)(m)->ds[XEEN_DS_COMBAT_PARTY + (unsigned)(i)] * XEEN_CHARACTER_BYTES)
#define ROW(db, id) ((db) + (size_t)(id) * XEEN_MONSTER_RECORD_BYTES)


typedef struct {
    xeen_combat *c;
    xeen_movement *m;
    const uint8_t *db;
    size_t n;
    xeen_entity_random_fn random;
    void *user;
    xeen_native_audio *audio;
    uint32_t boundary; /* first unsupported original address, 0 while valid */
} run;

/** Records ADDRESS as the fight's unsupported boundary (the first one wins) and returns 0. */
static int fail(run *r, uint32_t address)
{
    if (!r->boundary) {
        r->boundary = address;
    }
    return 0;
}
/** Returns how many of the three attacker slots (DS 98B4..98B6) hold a monster. */
static unsigned attacker_count(const xeen_movement *m)
{
    return (unsigned)(m->ds[XEEN_DS_VIEW_MONSTER(0)] != XEEN_VIEW_NO_MONSTER) +
           (unsigned)(m->ds[XEEN_DS_VIEW_MONSTER(1)] != XEEN_VIEW_NO_MONSTER) +
           (unsigned)(m->ds[XEEN_DS_VIEW_MONSTER(2)] != XEEN_VIEW_NO_MONSTER);
}
/** Returns the monster database row of combat RECORD, or NULL when its id is past the database. */
static const uint8_t *monster_row(const run *r, uint8_t record)
{
    uint8_t id = r->m->ds[(uint16_t)(MONSTER_RECORD(record) + XEEN_MAP_MONSTER_ID)];
    if ((size_t)id * XEEN_MONSTER_RECORD_BYTES + XEEN_MONSTER_RECORD_BYTES > r->n) {
        return NULL;
    }
    return ROW(r->db, id);
}

/* ------------------------------------------------------------------ rules */

/* 37923/38633/385E0 test xeen_condition_cannot_act (Asleep or Paralyzed..Eradicated) on a member's worst
 * condition. */
/** Returns nonzero for the worst conditions Paralyzed..Eradicated (a member who is not merely asleep). */
static int incapacitated(uint8_t condition)
{
    return condition >= XEEN_CONDITION_PARALYZED && condition <= XEEN_CONDITION_ERADICATED;
}

int xeen_combat_stat_bonus(const xeen_movement *m, uint16_t value)
{
    unsigned i;
    /* 2C92E: first threshold strictly above the value; the original scan is
     * unbounded, the final 65535 entry terminates every 16-bit value except
     * 65535 itself, which the native bound treats as the last entry. */
    for (i = 0; i < 23u && xeen_read16(m, (uint16_t)(XEEN_DS_ATTRIBUTE_BONUS_THRESHOLD + 2u * i)) <= value; ++i) {
    }
    return (int)(int8_t)m->ds[XEEN_DS_ATTRIBUTE_BONUS + i];
}

/**
 * Returns the attribute group of an item material (2A29D: the first limit not below material - 59).
 */
static unsigned attribute_category(uint8_t material)
{
    /* 2A29D over segment 40D0: first limit >= material-59. */
    static const uint8_t limits[10] = {9, 17, 25, 33, 39, 45, 50, 56, 61, 72};
    int value = (int)material - (int)XEEN_ITEM_FIRST_ATTRIBUTE;
    unsigned i;
    for (i = 0; i < 10u && (int)limits[i] < value; ++i) {
    }
    return i;
}
/** Returns the element group of an item material below 37 (2A26D). */
/* An element material's element (materials 1..36 by the ranges' last 8, 15, 20, 25, 33, 36): 0 fire, 1 electricity,
 * 2 cold, 3 poison, 4 energy, 5 magic, the order of DS:2896's element names 1..6 and of a monster's resistances
 * (XEEN_MONSTER_RESISTANCES); the item bonus kinds 11..16 (XEEN_ITEM_BONUS_RESISTANCES + it) and the hit slot's
 * element (it + 1) count from it. */
static unsigned element_category(uint8_t material)
{
    /* 2A26D over segment 40D2 for materials below 37. */
    static const uint8_t limits[6] = {8, 15, 20, 25, 33, 36};
    unsigned i;
    for (i = 0; i < 6u && limits[i] < material; ++i) {
    }
    return i;
}

unsigned xeen_combat_element_category(uint8_t material)
{
    return element_category(material);
}
unsigned xeen_combat_attribute_category(uint8_t material)
{
    return attribute_category(material);
}
/* The bonus one usable equipped ITEM of GROUP (0 weapons, 1 armour, 2 accessories) gives stat STAT: an attribute
 * material's bonus to its own stat (not to Endurance, and not to the resistances); an armour's or
 * accessory's element material's resistance; an armour's AC and its metal's. Original: 2BAFA's row. */
static int item_bonus(const xeen_movement *m, unsigned group, const uint8_t *item, unsigned stat)
{
    int total = 0;
    uint8_t material = item[XEEN_ITEM_ROW_MATERIAL];
    if (stat < XEEN_ITEM_BONUS_RESISTANCES && stat != XEEN_STAT_ENDURANCE && material >= XEEN_ITEM_FIRST_ATTRIBUTE &&
        material <= XEEN_ITEM_LAST_ATTRIBUTE) {
        unsigned category = attribute_category(material);
        if (category > 2u) {
            ++category;
        }
        if (category == stat) {
            total += (int8_t)m->ds[XEEN_DS_ATTRIBUTE_BY_MATERIAL + material];
        }
    }
    if (group && stat > XEEN_ITEM_BONUS_THIEVERY && material < XEEN_ITEM_FIRST_METAL &&
        element_category(material) + XEEN_ITEM_BONUS_RESISTANCES == stat) {
        total += m->ds[XEEN_DS_ELEMENT_RESISTANCE + material];
    }
    if (group == XEEN_ITEMS_ARMOR && stat == XEEN_ITEM_BONUS_ARMOR_CLASS) {
        total += m->ds[XEEN_DS_ARMOR_AC + item[XEEN_ITEM_ROW_ID]];
        if (material >= XEEN_ITEM_FIRST_METAL && material < XEEN_ITEM_FIRST_ATTRIBUTE) {
            total += (int8_t)m->ds[XEEN_DS_METAL_AC_BY_MATERIAL + material];
        }
    }
    return total;
}
/* The bonus member CH's equipped weapons, armour and accessories give stat STAT: each item worn and neither cursed
 * nor broken adds its item_bonus. Original: 2BAFA. */
static int item_scan(const xeen_movement *m, const uint8_t *ch, unsigned stat)
{
    int total = 0;
    unsigned group, i;
    for (group = 0; group < 3u; ++group) {
        for (i = 0; i < XEEN_ITEM_ROWS; ++i) {
            const uint8_t *item = ch + XEEN_CHARACTER_ITEM(group, i);
            if (!item[XEEN_ITEM_ROW_EQUIPPED] ||
                (item[XEEN_ITEM_ROW_STATE] & (XEEN_ITEM_CURSED | XEEN_ITEM_BROKEN))) {
                continue;
            }
            total += item_bonus(m, group, item, stat);
        }
    }
    return total;
}

int xeen_combat_item_bonus(const xeen_movement *m, const uint8_t *ch, unsigned stat)
{
    return m && ch ? item_scan(m, ch, stat) : 0;
}
/* 2A2E8: condition modifiers by stat (dead/stoned/eradicated contribute 0): each condition byte's value is subtracted
 * from (or added to) the stats it affects: Cursed: Luck; Insane: Personality, Intellect, Accuracy (Might and Speed
 * gain); Poisoned: Might, Speed, Accuracy; Diseased: Endurance, Personality, Intellect; Heart Broken and Weak: every
 * stat; In Love: every stat gains; Drunk: every stat but Personality and Luck, which gain. A stat above Accuracy reads
 * Luck's value (the original's index clamp). */
static int condition_mod(const uint8_t *ch, unsigned stat)
{
    int v[7] = {0, 0, 0, 0, 0, 0, 0};
    unsigned i;
    const uint8_t *c = ch + XEEN_CHARACTER_CONDITIONS;
    if (c[XEEN_CONDITION_DEAD] || c[XEEN_CONDITION_STONE] || c[XEEN_CONDITION_ERADICATED]) {
        return 0;
    }
    v[XEEN_STAT_LUCK] -= c[XEEN_CONDITION_CURSED];
    v[XEEN_STAT_PERSONALITY] -= c[XEEN_CONDITION_INSANE];
    v[XEEN_STAT_INTELLECT] -= c[XEEN_CONDITION_INSANE];
    v[XEEN_STAT_ACCURACY] -= c[XEEN_CONDITION_INSANE];
    v[XEEN_STAT_MIGHT] += c[XEEN_CONDITION_INSANE];
    v[XEEN_STAT_SPEED] += c[XEEN_CONDITION_INSANE];
    v[XEEN_STAT_MIGHT] -= c[XEEN_CONDITION_POISONED];
    v[XEEN_STAT_SPEED] -= c[XEEN_CONDITION_POISONED];
    v[XEEN_STAT_ACCURACY] -= c[XEEN_CONDITION_POISONED];
    v[XEEN_STAT_ENDURANCE] -= c[XEEN_CONDITION_DISEASED];
    v[XEEN_STAT_PERSONALITY] -= c[XEEN_CONDITION_DISEASED];
    v[XEEN_STAT_INTELLECT] -= c[XEEN_CONDITION_DISEASED];
    for (i = 0; i < 7u; ++i) {
        v[i] -= c[XEEN_CONDITION_HEART_BROKEN];
    }
    for (i = 0; i < 7u; ++i) {
        v[i] += c[XEEN_CONDITION_IN_LOVE];
    }
    for (i = 0; i < 7u; ++i) {
        v[i] -= c[XEEN_CONDITION_WEAK];
    }
    v[XEEN_STAT_MIGHT] -= c[XEEN_CONDITION_DRUNK];
    v[XEEN_STAT_INTELLECT] -= c[XEEN_CONDITION_DRUNK];
    v[XEEN_STAT_PERSONALITY] += c[XEEN_CONDITION_DRUNK];
    v[XEEN_STAT_ENDURANCE] -= c[XEEN_CONDITION_DRUNK];
    v[XEEN_STAT_SPEED] -= c[XEEN_CONDITION_DRUNK];
    v[XEEN_STAT_ACCURACY] -= c[XEEN_CONDITION_DRUNK];
    v[XEEN_STAT_LUCK] += c[XEEN_CONDITION_DRUNK];
    return v[stat > 5u ? 6u : stat];
}

uint16_t xeen_combat_stat(const xeen_movement *m, const uint8_t *ch, unsigned stat, int base_only)
{
    static const uint16_t age_ranges[10] = {1, 6, 11, 18, 36, 51, 76, 101, 201, 65535};
    /* by the age's bracket (below 1, 1..5, 6..10, 11..17, 18..35, 36..50, 51..75, 76..100, 101..200, 201 up): the
     * physical stats lose with age, Intellect and Personality (row 1) gain from 36 on */
    static const int16_t age_adjust[2][10] = {{-250, -50, -20, -10, 0, -2, -5, -10, -20, -50},
                                              {-250, -50, -20, -10, 0, 2, 5, 10, 20, 50}};
    int16_t value;
    int temporary;
    unsigned mode, index;
    uint16_t age, years;
    if (!m || !ch || stat > XEEN_STAT_LUCK) {
        return 0;
    }
    /* 2BD94: base/temporary pair; Intellect and Personality use the second
     * age row and Luck has no age adjustment. */
    value = (int16_t)ch[XEEN_CHARACTER_STAT(stat)];
    temporary = ch[XEEN_CHARACTER_STAT_TEMPORARY(stat)];
    mode = (stat == XEEN_STAT_INTELLECT || stat == XEEN_STAT_PERSONALITY) ? 1u : stat == XEEN_STAT_LUCK ? 2u : 0u;
    if (mode < 2u) {
        /* 2BAC7(ch,0): year difference capped at 254, plus the age byte. */
        years = (uint16_t)(xeen_read16(m, XEEN_DS_YEAR) -
                           xeen_get_le16(ch + XEEN_CHARACTER_BIRTH_YEAR));
        if (xeen_sign16(years) > 254) {
            years = 254u;
        }
        age = (uint16_t)(years + ch[XEEN_CHARACTER_AGE_BONUS]);
        for (index = 0; index < 9u && age_ranges[index] <= age; ++index) {
        }
        value = (int16_t)(value + age_adjust[mode][index]);
    }
    value = (int16_t)(value + item_scan(m, ch, stat));
    if (!base_only) {
        value = (int16_t)(value + condition_mod(ch, stat) + temporary);
    }
    return value < 1 ? 0u : (uint16_t)value;
}

/* 3CF5B, the thievery value: 2C909 level x 2, Ninja (6) +15 or
 * Robber (5) +30, race Elf/Gnome (1, 3) +10, Dwarf (2) +5, Half-Orc (4) -10,
 * the 2BAFA(10) item bonus; 0 without the Thievery byte (+27h) or below 1. */
int16_t xeen_combat_thievery(const xeen_movement *m, const uint8_t *ch)
{
    int16_t value;
    if (!m || !ch) {
        return 0;
    }
    value = (int16_t)(xeen_character_current_level(ch) * 2u);
    if (ch[XEEN_CHARACTER_CLASS] == XEEN_CLASS_NINJA) {
        value = (int16_t)(value + 15);
    } else if (ch[XEEN_CHARACTER_CLASS] == XEEN_CLASS_ROBBER) {
        value = (int16_t)(value + 30);
    }
    if (ch[XEEN_CHARACTER_RACE] == XEEN_RACE_ELF || ch[XEEN_CHARACTER_RACE] == XEEN_RACE_GNOME) {
        value = (int16_t)(value + 10);
    } else if (ch[XEEN_CHARACTER_RACE] == XEEN_RACE_DWARF) {
        value = (int16_t)(value + 5);
    } else if (ch[XEEN_CHARACTER_RACE] == XEEN_RACE_HALF_ORC) {
        value = (int16_t)(value - 10);
    }
    value = (int16_t)(value + item_scan(m, ch, XEEN_ITEM_BONUS_THIEVERY));
    if (!ch[XEEN_CHARACTER_SKILLS + XEEN_SKILL_THIEVERY]) {
        value = 0;
    }
    return value < 1 ? 0 : value;
}

uint16_t xeen_combat_armor_class(const xeen_movement *m, const uint8_t *ch, int base_only)
{
    int value;
    /* 4660F: Speed bonus + item AC; the temporary term is read unsigned. */
    value = xeen_combat_stat_bonus(m, xeen_combat_stat(m, ch, XEEN_STAT_SPEED, 0)) +
            item_scan(m, ch, XEEN_ITEM_BONUS_ARMOR_CLASS);
    if (!base_only) {
        value += m->ds[XEEN_DS_EFFECT_BLESS] + ch[XEEN_CHARACTER_AC_BONUS];
    }
    return value < 1 ? 0u : (uint16_t)value;
}

/* One draw of the generator with the caller's AL (its low byte) into LOW..HIGH (504E0's range), recorded in the
 * combat's diagnostic trace (byte-wide bounds and result; past XEEN_COMBAT_RNG_CAPACITY the trace is marked
 * incomplete) and counted. *CONVERTED is the ranged value; returns the raw word. */
static uint16_t traced_draw(run *r, uint16_t low, uint16_t high, unsigned al, uint16_t *converted)
{
    xeen_combat *c = r->c;
    uint16_t raw = r->random(r->user, (uint16_t)(al & 255u));
    unsigned slot = c->rng_draws;
    *converted = 0;
    (void)xeen_entity_rng_range(raw, low, high, converted);
    if (slot < XEEN_COMBAT_RNG_CAPACITY) {
        c->rng_raw[slot] = raw;
        c->rng_low[slot] = (uint8_t)low;
        c->rng_high[slot] = (uint8_t)high;
        c->rng_result[slot] = (uint8_t)*converted;
    } else {
        c->rng_trace_complete = 0u;
    }
    ++c->rng_draws;
    return raw;
}
/* A draw LOW..HIGH (504DA, both inclusive) for the combat: AL is the caller's register as it entered 504DA, passed to
 * the generator (the pre-time-service AL kept for evidence: the time adapter determines the AX after the service, and
 * MS-DOS 4 clears AL). Records the draw (traced_draw) and returns the converted word. */
static uint16_t random_word(run *r, unsigned low, unsigned high, unsigned al)
{
    uint16_t converted;
    (void)traced_draw(r, (uint16_t)low, (uint16_t)high, al, &converted);
    return converted;
}
/** Draws a random number LOW..HIGH (with the caller's AL) and returns its low byte. */
static uint8_t random_range(run *r, unsigned low, unsigned high, unsigned al)
{
    return (uint8_t)random_word(r, low, high, al);
}

/* 1B98:4C68 / analysis 305E8 through the shared generator (items.c). */
static uint16_t item_draw(void *user, uint16_t low, uint16_t high, uint16_t al)
{
    return random_range(user, low, high, al);
}

/**
 * Rolls a killed monster's item drop by its LEVEL (57F9:0000, read only by 36CBC). Level 7 has
 * chance 0: its draw is made with AL 0 and never passes, so no level-7 item is generated.
 */
static int monster_drop(run *r, uint8_t level)
{
    /* By level 1..7; level 7 (Doom Knight and Sandro, the shipped maximum) has 0. Original: 57F9:0000 (36CBC). */
    static const uint8_t chance[7] = {10, 20, 50, 100, 100, 100, 0};
    uint8_t item[3];
    unsigned category;
    int slot;
    if (level > 7u) {
        return fail(r, DROP_STOP_LEVEL);
    }
    /* The chance is both the draw's argument and its threshold (compared signed). Original: 36CC3, 36CC8, 36CD8. */
    if (random_range(r, 1u, 100u, chance[level - 1u]) > chance[level - 1u]) {
        return 1;
    }
    category = xeen_item_generate(r->m, level, item_draw, r, item);
    if ((slot = xeen_treasure_free_row(r->m->ds, category)) >= 0) {
        memcpy(r->m->ds + xeen_treasure_list(category) + 4u * (unsigned)slot, item, 3);
        r->m->ds[XEEN_DS_TREASURE_ITEMS_PENDING] = 1;
    }
    return 1;
}
/** Plays sound effect NUMBER; a failing audio service stops the fight at 1880E. */
static int play_effect(run *r, uint16_t number)
{
    if (xeen_native_audio_effect(r->audio, r->m, number) != XEEN_OK) {
        return fail(r, COMBAT_STOP_SOUND);
    }
    return 1;
}

/* The DS file-name buffer that the sound service reads a sample's name from (presentation only: the silent backend
 * plays nothing), as the original's 0:4BBA sprintf into F5E8 of a DS voice format; the native keeps the name to 23
 * characters. */
#define SAMPLE_NAME_BYTES 24u
static char *sample_name(xeen_movement *m)
{
    return (char *)m->ds + XEEN_DS_FILE_NAME;
}

/* ------------------------------------------------------- scene and order */

/* The combat scene pass (Shoot's too). What it changes in the game: the pass's opening and the monsters' countdown
 * (which may run the monster update); the effects stage, which counts the hit-slot flags down; the choice of the
 * attackers, which rebuilds the view's monster list and the visibility byte the update reads; and the renderer's
 * party scan, whose "a party row is in view" flags a volley reads between its tiers. The 3D render, the animation
 * counters and the frame wait are presentation and are not repeated per call.
 * Every pass builds the view as the exploration scene does: indoors the cell queries before the effects and the
 * wall lists after the attacker choice; outdoors the queries after the choice. Then the minimap, unless the rounds'
 * window (2), the treasure dialog's (10) or the speech window is open: so the last query's result stays the view's
 * or the minimap's, as in the original. The draw records, the shuffle and the object pass stay presentation.
 * Original: 1B503(1): 1B51A..1B571 and the 96C0 countdown (2A701); the effects 1BBDF indoors, 1B7F1 outdoors (the
 * 96EF hit slots); the attackers 1ED46 / 2126C (98B4..); the party scan 2469F / 25390 (97F7 = 97F9 = any 9744 row).
 * The view: indoors 1C9D4 (44 queries, 1C952, F4AB.., F496) before 1BBDF and 22034/22B25 after 1ED46; outdoors
 * 246CD (48 1C1C1 queries, 2515A) after 2126C; then 1E467, left out by its own test of 986E, 9876 and 9894, so
 * F48C..F495 (phase69 trace-*.json); 1B0F6. */
/* The combat pass's presentation, the exploration scene's own (game.c): the minimap's draws, the darkening, the
 * border (its icon and eye animations step as in every pass) with its compass text, then the view and its frame
 * presented, and the speech window when it is open. Presentation only: a rejected draw changes nothing. The map's
 * sprites stay loaded through the fight (the combat icons are numbered after them), so the minimap's tiles are
 * drawn as in the original.
 * Original: 1E467's draws, 1BF62's 4A68A, 26306's border, 1C112's 50BF6(1), 50BF6(3) and 4E33F (50BF6(28h) with
 * 9894). */
static void present_suffix(run *r, const xeen_minimap_draws *draws)
{
    xeen_presentation *p = r->c->presentation;
    xeen_border_ops ops;
    if (!p) {
        return;
    }
    if (draws && xeen_wizard_eye_plus_on) {
        xeen_wizard_eye_plus_minimap_begin(r->m, p); /* add-on wizard_eye_plus */
    }
    if (draws && draws->count) {
        xeen_status drawn = xeen_presentation_draw_sprites(p, draws->draws, draws->count);
        if (xeen_wizard_eye_plus_on) {
            xeen_wizard_eye_plus_minimap_end(r->m, p, drawn == XEEN_OK);
        }
    }
    if (draws && xeen_town_icons_on) {
        xeen_town_icons_minimap(r->m, p->framebuffer, p->framebuffer_size); /* add-on town_icons */
    }
    (void)xeen_presentation_darken(p,
                                   xeen_read16(r->m, XEEN_DS_VIEW_LIGHT_LEVEL));
    if (xeen_interface_border(r->m, &ops) == XEEN_OK) {
        (void)xeen_presentation_draw_sprites(p, ops.draws, ops.count);
        if (ops.has_text) {
            (void)xeen_presentation_text(p, ops.text_window, ops.text);
            if (r->c->windows.text) {
                r->c->windows.text(r->c->windows.user, ops.text_window, ops.text);
            }
        }
        if (ops.has_frame) {
            (void)xeen_presentation_frame(p, ops.frame_window);
        }
    }
    if (r->c->quiet_scene) {
        return; /* a quiet pass presents nothing (Original: 1C112 with [BP+6] = 0) */
    }
    (void)xeen_presentation_update_window(p, XEEN_WINDOW_MINIMAP);
    (void)xeen_presentation_update_window(p, XEEN_WINDOW_VIEW_FRAME);
    (void)xeen_presentation_present(p);
    if (r->m->ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH)]) {
        (void)xeen_presentation_update_window(p, XEEN_WINDOW_SPEECH);
    }
}
/* The view's draw list of a pass (indoors and outdoors each have their own), rastered into window 3 as the
 * exploration scene does; a rejected list draws nothing. Original: 2469A (list 0962), 2538B (list 1166). */
static void present_view(run *r, uint16_t list)
{
    xeen_presentation *p = r->c->presentation;
    xeen_draw_list *l;
    if (!p || !(l = malloc(sizeof *l))) {
        return;
    }
    if (xeen_scene_collect_draw_list(r->m, 3, list, l) == XEEN_OK) {
        (void)xeen_presentation_submit(p, l);
    }
    free(l);
}
/* The party's state at every combat start: the combat party emptied, no turn taken and no block chosen, no hit
 * animation pending, nobody ran; the target is the first monster in view; then every member joins the combat party
 * in party order. Original: after 191DE(8C50) at both starts (9720 FFh, 9707 and 96FB, 96EF..96F1, 9804, FAFE; 306A;
 * 9720[i] = i, 971F = the party count). */
static void combat_party_reset(xeen_movement *m)
{
    unsigned i;
    memset(m->ds + XEEN_DS_COMBAT_PARTY, 0xff, XEEN_COMBAT_PARTY_BYTES);
    memset(m->ds + XEEN_DS_TURN_TAKEN, 0, XEEN_COMBATANT_BYTES);
    memset(m->ds + XEEN_DS_COMBAT_BLOCKING, 0, XEEN_COMBATANT_BYTES);
    m->ds[XEEN_DS_HIT_FRAMES] = m->ds[XEEN_DS_HIT_FRAMES + 1u] = m->ds[XEEN_DS_HIT_FRAMES + 2u] = 0;
    m->ds[XEEN_DS_HIT_SLOT] = 0;
    m->ds[XEEN_DS_COMBAT_RAN] = 0;
    m->ds[XEEN_DS_TARGET_MONSTER] = m->ds[XEEN_DS_VIEW_MONSTER(0)];
    for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
        m->ds[(uint16_t)(XEEN_DS_COMBAT_PARTY + i)] = (uint8_t)i;
    }
    m->ds[XEEN_DS_COMBAT_PARTY_COUNT] = m->ds[XEEN_DS_PARTY_COUNT];
}
/* The interface and party both fight starts set up: the combat buttons, the combat icons over the restored panel,
 * the map's monster archive (on a Darkside special map the one the map load chose, XEEN.CC), and a fresh combat
 * party. Original: 38FC6 43244, 38FCB 28D80, 38FD0 191DE(8C50) (19F31, 19FC4; 39505 restores the entry's archive). */
static void combat_setup(xeen_combat *c, xeen_movement *m)
{
    xeen_combat_buttons(m, 1);
    xeen_combat_main_icons(c, m, c->presentation);
    xeen_select_archive(m, xeen_read16(m, XEEN_DS_MAP_MONSTER_ARCHIVE));
    combat_party_reset(m);
}
/* The combat scene: the exploration scene's stages (game_scene.c), whose only combat-specific parts are the
 * monsters' countdown (modes 1 and 2) and the contact test (modes 1 and 5). The view lists, the object animation
 * and the darkening run in combat too (the map's sprites stay resident), so the 3D view follows the monsters: a
 * monster killed is gone from the next pass. A pass with the scene disabled draws nothing.
 * Original: 1B503(1). 9877 skips the pass (1B517 -> 1C1BD); 1B51A..1B70A (the prefix, the E003 object animation),
 * 1B70F, then indoors 1C9D4, 1BBDF, 1ED46, 20011, 208FB, 1BDE4, the 4E33F present, 22034/22B25, 2469A and 1BED9;
 * outdoors 1B7F1, 2126C, 219E4, 1B9B6, 4E33F, 246CD, 2538B and 1BAA2; then 1B0F6, 1E467, 1BF62 (the F60C darkening)
 * and the suffix. */
static uint32_t scene_site; /* Evidence: the traced 1B503's original return (0: unlabelled) */
static int build_order(run *r);
static int next_actor(run *r);
/* A fight that begins inside a combat scene: the scene's contact test (1C17E) starts a nested fight. With a
 * defeated party the original enters that fight and leaves it at once: the combat icons and buttons are pushed, the
 * combat party is reset and the turn order built, the next actor's turn finds the party defeated, and the icons are
 * popped again; the outer fight goes on with that state. With a live party the nested fight runs up to its own
 * command poll (nested_entry); a live party without a recorded contact site, or nested deeper than
 * XEEN_COMBAT_NEST_MAX, stops.
 * Original: the defeated form in order (phase158 orig158 probe, listings f38f5f.asm, f394f0.asm):
 * 38F65 [bp-6] = 94, 38F6D [bp-4] = 3066, 3066 = 0, FAF8 = 2, 9A1C = 2,
 * 199C = 0; 38F8E 190A3("combat.icn"), 98D4 into the 2A68 records; 43244,
 * 28D80, 191DE(8C50); 9720 FFh x9, 9707 and 96FB 0 x12, 96EF..96F1, 9804,
 * FAFE 0; 306A = 98B4; 9720[i] = i, 971F = ED8C; 38D25; 97FE/97FF/96C3 FFh;
 * 38633 (its 2F869: F60A, 9A1C = 1); 39059 F60A -> 39505: 191DE([bp-6]),
 * 199C = ECh, 39514 190CF, 98D4 into the 2A68 records, 43287, 28D80,
 * 306A = FFh; 39558 [bp-4] 0 -> 3957C DI 0 -> 395C3 FAF8 = 1, RETF 1C183.
 * The outer combat goes on with that DS (its 97FF FFh, 9A1C 1, FAF8 1). The live form: 39063 onward (a whole nested
 * fight, nested_entry). */
static void combat_window(run *, unsigned, int);
static int loop_top(run *);
static int monster_update(run *);
static int scene(run *);
/* The command icons after the first: their records take the current sprite handle. */
#define COMMAND_ICONS_HANDLED 15u
/* The 15 command icons after the first take the sprite handle just pushed or popped (a nested fight's icons) or set
 * (initialize: combat.icn's). */
static void command_icons_from_handle(xeen_movement *m)
{
    unsigned i;
    for (i = 0; i < COMMAND_ICONS_HANDLED; ++i) {
        xeen_write16(m, XEEN_VIEW_FIELD(XEEN_DS_COMMAND_ICONS + XEEN_VIEW_RECORD_BYTES, i, HANDLE),
                     xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
    }
}
/*
 * A live party's nested fight after its first actor, up to its own command poll: a party its own entry defeated
 * stops; otherwise the turn order again, the monsters' update and the scene while the countdown runs, the combat
 * window and the round loop's top. The attackers in view are kept, the nesting depth grows, and the fight awaits its
 * first command. Returns 0 when a step stopped (its boundary in r).
 * Original: 39059 F60A clear -> 39063, as initialize's 38633..3912C (38D25, the 96C0 branch 2A701 and 1B503(1),
 * 3907D window 2, the 39083 loop top).
 */
static int nested_live_fight(run *r)
{
    xeen_movement *m = r->m;
    xeen_combat *c = r->c;
    if (m->ds[XEEN_DS_PARTY_DEFEATED]) {
        return fail(r, NEST_STOP_DEFEATED_AT_ENTRY); /* defeated by its own entry (Original: 38633) */
    }
    if (!build_order(r)) {
        return 0;
    }
    if (m->ds[XEEN_DS_MONSTER_COUNTDOWN] && (!monster_update(r) || !scene(r))) {
        return 0;
    }
    combat_window(r, 2u, 1);
    if (!loop_top(r)) {
        return 0;
    }
    memcpy(c->attackers, m->ds + XEEN_DS_VIEW_MONSTERS, 3);
    ++c->outer_active;
    c->live_entered = 1;
    ++c->live_nested;
    c->phase = XC_AWAITING_COMMAND;
    xeen_presentation_trace_extra("mark combat-nested-live");
    return 1;
}
/*
 * A defeated party's nested fight ends at once (a party its first actor's turn found alive stops): the suspended
 * fight's WORLD archive back, the minimap button home, the icons popped, combat's buttons and main icons, no target;
 * the suspended fight's SIGN up would stop (inside combat it is always down); combat's mode back to exploring.
 * Returns 0 when a step stopped (its boundary in r).
 * Original: 39505 191DE([bp-6]), 39514 190CF, 39549 43287, 3954E 28D80, 39558 [bp-4], 395C3 FAF8 = 1.
 */
static int nested_defeated_exit(run *r, uint8_t world, uint8_t sign)
{
    xeen_movement *m = r->m;
    xeen_combat *c = r->c;
    if (!m->ds[XEEN_DS_PARTY_DEFEATED]) {
        return fail(r, NEST_STOP_NOT_DEFEATED);
    }
    xeen_select_archive(m, world); /* the fight's world (Original: 39505 191DE([bp-6])) */
    xeen_write16(m, MINIMAP_BUTTON_X, MINIMAP_BUTTON_HOME_X);
    if (c->nested_icons(c->ranged_user, m, 0) != XEEN_OK) {
        return fail(r, NEST_STOP_ICONS_POP); /* Original: 39514 */
    }
    command_icons_from_handle(m);
    xeen_combat_buttons(m, 0);                     /* Original: 39549 43287 */
    xeen_combat_main_icons(c, m, c->presentation); /* Original: 3954E 28D80 */
    m->ds[XEEN_DS_TARGET_MONSTER] = XEEN_NO_TARGET;
    if (sign) {
        return fail(r, NEST_STOP_SIGN); /* never: the sign is down inside combat (Original: 39558 [bp-4], 3066) */
    }
    m->ds[XEEN_DS_COMBAT_MODE] = XEEN_COMBAT_MODE_EXPLORING;
    ++c->nested_entries;
    return 1;
}
/**
 * Enters a fight from inside another (a contact during a fight's scene, or a defeated party's fight's tail): the
 * sign is taken down, combat's modes set, the nested icons pushed, the interface and party set up, the turn order
 * built and its first actor found. A live party then fights it as a whole fight up to its own command poll; the
 * suspended fight's world and sign are kept for its resume (at most XEEN_COMBAT_NEST_MAX deep). A defeated party's
 * nested fight ends at once: the world's archive back, the icons popped, combat's buttons and main icons, no target;
 * a sign up would stop (inside combat it is always down).
 * Original: the nested 38F5F (its frame's [bp-6] world and [bp-4] sign).
 */
static int nested_entry(run *r)
{
    xeen_movement *m = r->m;
    xeen_combat *c = r->c;
    uint8_t world = m->ds[XEEN_DS_WORLD], sign = m->ds[XEEN_DS_SIGN_SHOWN];
    /* A live party gets here from the round's tail scene, or from inside another nested fight; the suspended
     * fight's world and sign are kept to be restored when it resumes.
     * Original: phase164 orig164 (393F0 -> 393F5 -> 1C17E 38F5F, the nested fight to its own 3912C poll); phase217
     * orig217 r-kf4 (the depth-2 fight's own 393F8 opens a third 38F5F); the frame's [bp-6]/[bp-4] pushed. */
    int live = !m->ds[XEEN_DS_PARTY_DEFEATED];
    if (!c->nested_icons ||
        (live && (!c->live_contact_site || c->outer_active >= XEEN_COMBAT_NEST_MAX))) {
        return fail(r, NEST_STOP_ENTRY);
    }
    if (live) {
        c->outer_world[c->outer_active] = c->entry_world;
        c->outer_3066[c->outer_active] = c->entry_3066;
        c->entry_world = world;
        c->entry_3066 = sign;
    }
    xeen_presentation_trace_extra("mark combat-nested-entry");
    m->ds[XEEN_DS_SIGN_SHOWN] = 0;
    m->ds[XEEN_DS_COMBAT_MODE] = XEEN_COMBAT_MODE_FIGHTING;
    m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_COMBAT;
    xeen_write16(m, MINIMAP_BUTTON_X, 0);
    if (c->nested_icons(c->ranged_user, m, 1) != XEEN_OK) {
        return fail(r, NEST_STOP_ICONS_PUSH);
    }
    command_icons_from_handle(m);
    /* The interface and party, the turn order and the first actor. Original: 39047 38D25, 39056 38633. */
    combat_setup(c, m);
    if (!build_order(r)) {
        return 0;
    }
    m->ds[XEEN_DS_TURN_INDEX] = m->ds[XEEN_DS_TURN_ACTOR] = m->ds[XEEN_DS_HIGHLIGHTED_PORTRAIT] = 0xffu;
    if (!next_actor(r)) {
        return 0;
    }
    return live ? nested_live_fight(r) : nested_defeated_exit(r, world, sign);
}
/* The pass's opening: its entry in the evidence trace, with where it returns when that is known (no presentation:
 * it draws nothing), and the pass's frame mark. */
static void open_scene(xeen_presentation *p)
{
    if (scene_site) {
        xeen_presentation_trace_extra(p ? "scenecall %05X" : "scenecall %05X semantic", (unsigned)scene_site);
    } else {
        xeen_presentation_trace_extra(p ? "scenecall" : "scenecall semantic");
    }
    scene_site = 0;
    xeen_presentation_display(p, XEEN_DISPLAY_SCENE_START);
}
/* The page presented after the records' shuffle, before the view's construction (Original: 4E33F). */
static void present_shuffled(xeen_presentation *p)
{
    if (p) {
        (void)xeen_presentation_present(p);
    }
}
/* The indoor view's construction: the terrain, then the geometry when the terrain hands over to it (22034, 22B25). */
static xeen_caller_result construct_indoor(xeen_movement *m)
{
    xeen_caller_result view = xeen_scene_construct_terrain(m);
    return view.boundary == XEEN_SCENE_INDOOR_GEOMETRY ? xeen_scene_construct_geometry(m) : view;
}
/* The constructed view's tail, the same stages indoors and outdoors: its draw list presented, the construction
 * finished and the shuffled GROUP restored when the finish hands over to the restore. Returns the boundary the
 * animation follows. Original: indoors 2469A, 1BED9; outdoors 2538B, 1BAA2. */
static xeen_caller_result finish_view(run *r, int outdoor, uint16_t group)
{
    xeen_caller_result view;
    if (outdoor) {
        present_view(r, XEEN_DS_OUTDOOR_VIEW_LIST);
        view = xeen_scene_construct_outdoor_finish(r->m);
        return view.boundary == XEEN_SCENE_OUTDOOR_RESTORE ? xeen_scene_outdoor_restore(r->m, group) : view;
    }
    present_view(r, XEEN_DS_INDOOR_VIEW_LIST);
    view = xeen_scene_construct_finish(r->m);
    return view.boundary == XEEN_SCENE_INDOOR_RESTORE ? xeen_scene_restore_group(r->m, group) : view;
}
/* The minimap's draws into DRAWS (the caller's scratch) and, when they were made, the pass's presentation
 * (present_suffix); returns the minimap's status. Original: 1E467, then 1BF62 and the suffix. */
static xeen_status draw_minimap(run *r, xeen_minimap_draws *draws)
{
    xeen_status status = xeen_scene_minimap(r->m, draws);
    if (status == XEEN_OK) {
        present_suffix(r, draws);
    }
    return status;
}
/* The scene's tail (Original: 1C18F..1C1B3): the party has not acted, the pass is counted, the attackers it chose
 * kept for the round, and the pass's frame wait on P, the presentation the pass opened with (the window paces it). */
static void close_scene(run *r, xeen_presentation *p)
{
    xeen_movement *m = r->m;
    m->ds[XEEN_DS_PARTY_ACTED] = 0;
    m->status = XEEN_OK;
    ++r->c->scenes;
    memcpy(r->c->attackers, m->ds + XEEN_DS_VIEW_MONSTERS, 3);
    xeen_presentation_display(p, XEEN_DISPLAY_SCENE_END);
}
/* Runs one combat scene pass (described above present_suffix): the exploration scene's stages in their order,
 * indoors or outdoors, the view's list drawn after its construction, then the animation, the minimap and the
 * presentation. Each stage reports the boundary of the stage after it; any other stops the fight at that boundary.
 * Every stop is this function's (the stage helpers above cannot stop). The presentation is the one the pass opens
 * with (a nested fight's entry or the cell's event may rebuild the combat record). */
static int scene(run *r)
{
    xeen_movement *m = r->m;
    xeen_presentation *p = r->c->presentation;
    const int outdoor = xeen_read16(m, XEEN_DS_OUTDOORS) != 0u;
    xeen_caller_result view;
    uint16_t group = 0;
    xeen_minimap_draws *draws;
    xeen_status status;
    open_scene(p);
    view = xeen_scene_after_timer(m);
    if (view.boundary == XEEN_LOOP_COMMAND_POLL) {
        m->status = XEEN_OK;
        return 1; /* the scene is disabled: the pass draws nothing (Original: 9877, 1C1BD) */
    }
    if (view.boundary != XEEN_SCENE_PREPARE_RENDER) {
        return fail(r, view.boundary);
    }
    view = xeen_scene_prepare_render(m);
    /* The view up to its construction: outdoors 1B7F1, 2126C, 219E4, 1B9B6, 4E33F, 246CD; indoors 1C9D4, 1BBDF,
     * 1ED46, 20011, 208FB, 1BDE4, 4E33F, 22034/22B25. */
    if (outdoor) {
        if (view.boundary != XEEN_SCENE_OUTDOOR_EFFECTS) {
            return fail(r, view.boundary);
        }
        (void)xeen_scene_outdoor_effects(m);
        if (xeen_scene_outdoor_monsters(m, r->db, r->n) != XEEN_OK) {
            return fail(r, SCENE_STOP_OUTDOOR_MONSTERS);
        }
        if (xeen_scene_outdoor_objects(m) != XEEN_OK) {
            return fail(r, SCENE_STOP_OUTDOOR_OBJECTS);
        }
        view = xeen_scene_outdoor_shuffle(m, &group);
        if (view.boundary != XEEN_SCENE_PRESENT) {
            return fail(r, view.boundary);
        }
        present_shuffled(p);
        view = xeen_scene_construct_outdoor(m);
        if (view.boundary != XEEN_SCENE_OUTDOOR_RENDER) {
            return fail(r, view.boundary);
        }
    } else {
        if (view.boundary != XEEN_SCENE_VIEW_WALL_SCAN) {
            return fail(r, view.boundary);
        }
        view = xeen_scene_scan_view_walls(m);
        if (view.boundary != XEEN_SCENE_INDOOR_EFFECTS) {
            return fail(r, view.boundary);
        }
        (void)xeen_scene_prepare_effects(m);
        if (xeen_scene_monsters(m, r->db, r->n) != XEEN_OK) {
            return fail(r, SCENE_STOP_MONSTERS);
        }
        if (xeen_scene_objects(m) != XEEN_OK) {
            return fail(r, SCENE_STOP_OBJECTS);
        }
        if (xeen_scene_walls(m) != XEEN_OK) {
            return fail(r, SCENE_STOP_WALLS);
        }
        view = xeen_scene_shuffle(m, &group);
        if (view.boundary != XEEN_SCENE_PRESENT) {
            return fail(r, view.boundary);
        }
        present_shuffled(p);
        view = construct_indoor(m);
        if (view.boundary != XEEN_SCENE_INDOOR_RENDER) {
            return fail(r, view.boundary);
        }
    }
    view = finish_view(r, outdoor, group);
    if (view.boundary != XEEN_SCENE_ANIMATE) {
        return fail(r, view.boundary);
    }
    view = xeen_scene_animate(m, r->db, r->n);
    if (view.boundary != XEEN_SCENE_DRAW) {
        return fail(r, view.boundary);
    }
    draws = malloc(sizeof *draws);
    if (!draws) {
        return fail(r, SCENE_STOP_DRAW);
    }
    status = draw_minimap(r, draws);
    free(draws);
    if (status != XEEN_OK) {
        return fail(r, SCENE_STOP_DRAW);
    }
    /* The scene's contact test: a monster on the party's cell, in exploration or Rest mode, with no shot queued or
     * flying and the monsters enabled, starts a fight. Combat's own mode never passes it; but a monster attack that
     * found no member able to be its target sets the defeated party and exploration mode, so the next combat scene
     * enters a nested fight (nested_entry).
     * Original: 1C142..1C17C; 383D6 sets F60A = 1 and 9A1C = 1; 1C17E 38F5F (xeen_dat_evidence/phase158 orig158:
     * 39075 and 390D9 after the entry's 2A701). */
    if (xeen_scene_contact_due(m->ds)) {
        if (!nested_entry(r)) {
            return 0;
        }
        /* A live nested fight waits at its own command poll; this scene's tail runs after that fight's cleanup
         * (the game's owner). Original: the 3912C poll; the tail 1C183..1C1B3. */
        if (r->c->live_entered) {
            m->status = XEEN_OK;
            return 1;
        }
        /* A step's event that was deferred while a monster was in range runs now, inside the combat scene: the
         * owner runs the party cell's dispatcher to its return (cell_event); without one the scene stops.
         * Original: 1C183 tests 9756 (deferred by 313FD) -> 1C18A 310B4 (phase159 orig159 run orig-1c18a, 7Ch). */
        if (m->ds[XEEN_DS_EVENT_DEFERRED] &&
            (!r->c->cell_event || r->c->cell_event(r->c->ranged_user, m) != XEEN_OK)) {
            return fail(r, SCENE_STOP_CELL_EVENT);
        }
    }
    close_scene(r, p);
    return 1;
}

/* The monster update's random draws inside combat (a monster's saving throw, 1..id + 50), made with the bound as
 * the generator's argument and recorded in the combat trace as random_word's are.
 * Original: 2B5E9's 38ADB in 2A701: 504DA(1, id + 32h), entered with AX = that bound. */
static uint16_t update_random(void *u, uint16_t ax)
{
    uint16_t converted;
    /* The monster update's generator returns the raw word (it ranges the draw itself); the trace records the 1..AX
     * value that range gives. */
    return traced_draw(u, 1u, ax, ax, &converted);
}
/* The monster update run from inside combat: when it ends at the monsters' ranged attack, the owner delivers the
 * queued attacks before combat goes on; any other unfinished update stops. Original: 2A701 called at 3906E and
 * 393C7; its final 2AAB1. */
static int monster_update(run *r)
{
    uint32_t boundary = 0;
    xeen_status status = xeen_input_release_96c0_database_rng(
            r->m, r->db, r->n, r->random ? update_random : NULL, r, &boundary);
    if (status == XEEN_SERVICE_REQUIRED && boundary == XEEN_MONSTER_UPDATE_RANGED && r->c->ranged_delivery) {
        status = r->c->ranged_delivery(r->c->ranged_user, r->m, &boundary);
    }
    if (status != XEEN_OK) {
        return fail(r, boundary ? boundary : MONSTER_UPDATE_STOP_ROUND);
    }
    return 1;
}

/* The turn order from SPEEDS (slot i: the party's members, then the attackers): from the fastest speed MAXIMUM
 * down to 0, every combatant of that speed in slot order; one slower than 0 (a member's signed speed) gets no turn. */
static void order_by_speed(xeen_movement *m, const uint16_t speeds[XEEN_COMBATANT_BYTES], int maximum,
                           unsigned party)
{
    unsigned i, count = 0;
    int speed;
    for (speed = maximum; speed >= 0; --speed) {
        for (i = 0; i < party + 3u && i < XEEN_COMBATANT_BYTES; ++i) {
            if (speeds[i] == (uint16_t)speed && count < XEEN_COMBATANT_BYTES) {
                m->ds[XEEN_DS_TURN_ORDER + count++] = (uint8_t)i;
            }
        }
    }
}

/* After a rebuild in the middle of a round: when the turn index no longer points at PREVIOUS, the combatant whose
 * turn it was, the index moves to PREVIOUS's new place among the first party + live attackers entries (and stays
 * when it is not among them). */
static void follow_previous(xeen_movement *m, int previous, unsigned party)
{
    unsigned i, limit;
    if ((int8_t)m->ds[(uint16_t)(XEEN_DS_TURN_ORDER + (unsigned)(int8_t)m->ds[XEEN_DS_TURN_INDEX])] == previous) {
        return;
    }
    limit = party + attacker_count(m);
    for (i = 0; i < limit && i < XEEN_COMBATANT_BYTES; ++i) {
        if ((int8_t)m->ds[XEEN_DS_TURN_ORDER + i] == previous) {
            m->ds[XEEN_DS_TURN_INDEX] = (uint8_t)i;
            break;
        }
    }
}

/* Evidence (XEEN_DRAW_TRACE_EXTRA only): the order, the turn index and the speeds as 38D25 leaves them (38F5B). */
static void trace_order(const xeen_movement *m, const uint16_t speeds[XEEN_COMBATANT_BYTES])
{
    char text[256];
    size_t k = 0; /* at most 12 x 4 + 19 + 12 x 7 = 151 */
    unsigned i;
    if (!xeen_presentation_tracing_extra()) {
        return;
    }
    for (i = 0; i < XEEN_COMBATANT_BYTES; ++i) {
        k += (size_t)snprintf(text + k, sizeof text - k, "%s%u", i ? "," : "", m->ds[XEEN_DS_TURN_ORDER + i]);
    }
    k += (size_t)snprintf(text + k, sizeof text - k, " active %u speeds ", m->ds[XEEN_DS_TURN_INDEX]);
    for (i = 0; i < XEEN_COMBATANT_BYTES; ++i) {
        k += (size_t)snprintf(text + k, sizeof text - k, "%s%d", i ? "," : "", (int)(int16_t)speeds[i]);
    }
    xeen_presentation_trace_extra("ai-order %s", text);
}

/* The round's turn order (XEEN_DS_TURN_ORDER): every member's speed (combat stat, signed) and the speed of each
 * attacker in the party cell's three view-monster slots, then the order by speed (order_by_speed). Only the first
 * two present slots advance the entry index (the original's loop; the third is the last, so nothing follows it). A
 * rebuild while a turn index is set keeps the combatant whose turn it was (follow_previous). A combat-party entry
 * past member 5, or more entries than XEEN_COMBATANT_BYTES, stops. Original: 38D25. */
static int build_order(run *r)
{
    xeen_movement *m = r->m;
    uint16_t speeds[XEEN_COMBATANT_BYTES];
    int had_active = m->ds[XEEN_DS_TURN_INDEX] != XEEN_NO_TURN, previous, maximum = 0, speed;
    unsigned i, party = m->ds[XEEN_DS_COMBAT_PARTY_COUNT], slot;
    /* 38D25: CBW of the previous entry; an FFh index reads 9712, TURN_TAKEN[11]. */
    previous = (int8_t)m->ds[(uint16_t)(XEEN_DS_TURN_ORDER +
                                        (unsigned)(int8_t)m->ds[XEEN_DS_TURN_INDEX])];
    memset(m->ds + XEEN_DS_TURN_ORDER, 0xff, XEEN_COMBATANT_BYTES);
    for (i = 0; i < XEEN_COMBATANT_BYTES; ++i) {
        speeds[i] = 0xffffu;
    }
    if (party > XEEN_COMBATANT_BYTES) {
        return fail(r, ORDER_STOP_COMBATANTS);
    }
    for (i = 0; i < party; ++i) {
        if (m->ds[XEEN_DS_COMBAT_PARTY + i] > 5u) {
            return fail(r, ORDER_STOP_COMBATANTS);
        }
        speeds[i] = xeen_combat_stat(m, MEMBER(m, i), XEEN_STAT_SPEED, 0);
        if (xeen_sign16(speeds[i]) > maximum) {
            maximum = xeen_sign16(speeds[i]);
        }
    }
    for (slot = 0; slot < 3u; ++slot) {
        const uint8_t *row;
        if (m->ds[XEEN_DS_VIEW_MONSTERS + slot] == XEEN_VIEW_NO_MONSTER) {
            continue;
        }
        if (!(row = monster_row(r, m->ds[XEEN_DS_VIEW_MONSTERS + slot])) ||
            i >= XEEN_COMBATANT_BYTES) {
            return fail(r, ORDER_STOP_COMBATANTS);
        }
        speeds[i] = row[XEEN_MONSTER_SPEED];
        speed = row[XEEN_MONSTER_SPEED];
        if (slot < 2u) {
            ++i;
        }
        if (speed > maximum) {
            maximum = speed;
        }
    }
    order_by_speed(m, speeds, maximum, party);
    if (had_active) {
        follow_previous(m, previous, party);
    }
    trace_order(m, speeds);
    return 1;
}

xeen_status xeen_combat_build_order(xeen_movement *m, const uint8_t *db, size_t n)
{
    xeen_combat scratch;
    run r;
    memset(&scratch, 0, sizeof scratch);
    r = (run){&scratch, m, db, n, NULL, NULL, NULL, 0};
    if (!m || !db) {
        return XEEN_BAD_INPUT;
    }
    return build_order(&r) ? XEEN_OK : XEEN_SERVICE_REQUIRED;
}

/* Whether everyone has had a turn this round: each of the first party + live attackers combatants has its turn
 * taken (XEEN_DS_TURN_TAKEN), a member who cannot fight (worst condition 11..15) counting as done. Original: 37923. */
static int all_gone(const xeen_movement *m)
{
    unsigned i, party = m->ds[XEEN_DS_COMBAT_PARTY_COUNT], limit = party + attacker_count(m);
    for (i = 0; i < limit && i < XEEN_COMBATANT_BYTES; ++i) {
        if (m->ds[XEEN_DS_TURN_TAKEN + i]) {
            continue;
        }
        if (i >= party) {
            return 0;
        }
        if (!incapacitated(xeen_character_worst_condition(MEMBER(m, i)))) {
            return 0;
        }
    }
    return 1;
}
/* Whether no member of the combat party can act (each one's worst condition). Original: 385E0. */
static int party_cannot_act(const xeen_movement *m)
{
    unsigned i;
    for (i = 0; i < m->ds[XEEN_DS_COMBAT_PARTY_COUNT]; ++i) {
        if (!xeen_condition_cannot_act(xeen_character_worst_condition(MEMBER(m, i)))) {
            return 0;
        }
    }
    return 1;
}
/* The party's defeat (XEEN_DS_PARTY_DEFEATED) from the active party: 1 when no member can fight, else 0.
 * Original: 2F869 (F60A). */
static void party_check(xeen_movement *m)
{
    unsigned i, count = xeen_active_party_count(m->ds);
    for (i = 0; i < count; ++i) {
        const uint8_t *ch = m->ds + XEEN_PARTY_RECORDS +
                            xeen_active_party_slot(m->ds, i) * XEEN_CHARACTER_BYTES;
        if (!incapacitated(xeen_character_worst_condition(ch))) {
            m->ds[XEEN_DS_PARTY_DEFEATED] = 0;
            return;
        }
    }
    m->ds[XEEN_DS_PARTY_DEFEATED] = 1;
}

/* ----------------------------------------------------- party damage path */

/* 3891F: the attack's weapon for member CH. Range zero selects the melee hand slots (a9 1 or 13), any other range
 * the missile slot (a9 4). Each selected slot, in row order: an intact one (not cursed or broken) sets its element (a
 * non-metal material) or, for a metal, ASSIGNS the to-hit and the damage from its metal (replacing what earlier slots
 * gave), and its power bits into 96EC (written only: deliberately numeric); then every selected slot adds Heroism to
 * the to-hit and Holy Bonus to the damage, sets the weapon, its dice and sides, and rolls the dice into the damage.
 * The damage is at least 0; the Adventurer preference adds 5 to-hit and triples it. Writes
 * ATTACK_DICE/SIDES/WEAPON/ELEMENT, 96EC, ATTACK_DAMAGE and ATTACK_HIT_BONUS; returns 1, or 0 through fail() for a
 * weapon with dice but no sides. */
static int prepare_weapon(run *r, const uint8_t *ch, unsigned range)
{
    xeen_movement *m = r->m;
    unsigned i, d;
    int16_t prepared = 0;
    int32_t hit = 0;
    m->ds[XEEN_DS_ATTACK_SIDES] = m->ds[XEEN_DS_ATTACK_DICE] = 0;
    m->ds[0x96ec] = m->ds[XEEN_DS_ATTACK_ELEMENT] = m->ds[XEEN_DS_ATTACK_WEAPON] = 0; /* 96EC: the power */
    for (i = 0; i < XEEN_ITEM_ROWS; ++i) {
        const uint8_t *item = ch + XEEN_CHARACTER_ITEM(XEEN_ITEMS_WEAPONS, i);
        if (range ? item[XEEN_ITEM_ROW_EQUIPPED] != XEEN_SLOT_MISSILE
                  : (item[XEEN_ITEM_ROW_EQUIPPED] != XEEN_SLOT_WEAPON &&
                     item[XEEN_ITEM_ROW_EQUIPPED] != XEEN_SLOT_TWO_HANDED)) {
            continue;
        }
        if (!(item[XEEN_ITEM_ROW_STATE] & (XEEN_ITEM_CURSED | XEEN_ITEM_BROKEN))) {
            if (item[XEEN_ITEM_ROW_MATERIAL] < XEEN_ITEM_FIRST_METAL) {
                m->ds[XEEN_DS_ATTACK_ELEMENT] = item[XEEN_ITEM_ROW_MATERIAL];
            } else if (item[XEEN_ITEM_ROW_MATERIAL] < XEEN_ITEM_FIRST_ATTRIBUTE) {
                hit = (int8_t)m->ds[XEEN_DS_METAL_TO_HIT_BY_MATERIAL + item[XEEN_ITEM_ROW_MATERIAL]];
                prepared = (int8_t)m->ds[XEEN_DS_METAL_DAMAGE_BY_MATERIAL + item[XEEN_ITEM_ROW_MATERIAL]];
            }
            m->ds[0x96ec] = (uint8_t)(item[XEEN_ITEM_ROW_STATE] & XEEN_ITEM_POWER);
        }
        hit += m->ds[XEEN_DS_EFFECT_HEROISM];
        prepared = (int16_t)(prepared + m->ds[XEEN_DS_EFFECT_HOLY_BONUS]);
        m->ds[XEEN_DS_ATTACK_WEAPON] = item[XEEN_ITEM_ROW_ID];
        m->ds[XEEN_DS_ATTACK_DICE] = m->ds[XEEN_DS_WEAPON_DICE + item[XEEN_ITEM_ROW_ID]];
        m->ds[XEEN_DS_ATTACK_SIDES] = m->ds[XEEN_DS_WEAPON_SIDES + item[XEEN_ITEM_ROW_ID]];
        if (!m->ds[XEEN_DS_ATTACK_SIDES] && m->ds[XEEN_DS_ATTACK_DICE]) {
            return fail(r, WEAPON_STOP_DICE);
        }
        for (d = 0; d < m->ds[XEEN_DS_ATTACK_DICE]; ++d) {
            prepared = (int16_t)(prepared +
                                 random_range(r, 1u, m->ds[XEEN_DS_ATTACK_SIDES], m->ds[XEEN_DS_ATTACK_SIDES]));
        }
    }
    if (prepared < 1) {
        prepared = 0;
    }
    if (m->ds[XEEN_DS_GAME_PREFERENCE] == XEEN_PREFERENCE_ADVENTURER) {
        hit += 5;
        prepared = (int16_t)(prepared * 3);
    }
    xeen_put_le16(m->ds + XEEN_DS_ATTACK_DAMAGE, (uint16_t)prepared);
    xeen_put_le32(m->ds + XEEN_DS_ATTACK_HIT_BONUS, (uint32_t)hit);
    return 1;
}

/*
 * One hit test of member CH against the targeted monster (306A; its row TARGET_ROW in the monster database), with the
 * weapon slots RANGE selects prepared first (3891F): the accuracy bonus plus the attack's hit bonus, the level over
 * the class's divisor, a d20 that explodes on every 20 (each further d20 added), the Cursed level taken off and 20 more
 * against a monster under a condition; *HIT when that score reaches 10 + the monster's armour (compared unsigned). The
 * score and the threshold are kept (last_attack_score, last_attack_threshold). Returns 0 when a step stopped (a class
 * past the ten, or the weapon's preparation), else 1. Original: 38818.
 */
static int player_hit(run *r, const uint8_t *ch, const uint8_t *target_row, unsigned range,
                      int *hit)
{
    /* the class's level divisor for the attack score (level / it) */
    static const uint8_t divisors[10] = {[XEEN_CLASS_KNIGHT] = 1, [XEEN_CLASS_PALADIN] = 2, [XEEN_CLASS_ARCHER] = 2,
                                         [XEEN_CLASS_CLERIC] = 3, [XEEN_CLASS_SORCERER] = 4, [XEEN_CLASS_ROBBER] = 2,
                                         [XEEN_CLASS_NINJA] = 2,  [XEEN_CLASS_BARBARIAN] = 1, [XEEN_CLASS_DRUID] = 3,
                                         [XEEN_CLASS_RANGER] = 2};
    xeen_movement *m = r->m;
    uint16_t score, threshold, quotient;
    uint8_t roll;
    if (ch[XEEN_CHARACTER_CLASS] > 9u) {
        return fail(r, HIT_STOP_CLASS);
    }
    if (!prepare_weapon(r, ch, range)) {
        return 0;
    }
    score = (uint16_t)(xeen_combat_stat_bonus(m, xeen_combat_stat(m, ch, XEEN_STAT_ACCURACY, 0)) +
                       xeen_read16(m, XEEN_DS_ATTACK_HIT_BONUS));
    quotient = (uint16_t)(xeen_character_current_level(ch) / divisors[ch[XEEN_CHARACTER_CLASS]]);
    score = (uint16_t)(score + quotient);
    roll = random_range(r, 1u, 20u, quotient);
    score = (uint16_t)(score + roll);
    while (roll == 20u) {
        roll = random_range(r, 1u, 20u, 20u);
        score = (uint16_t)(score + roll);
    }
    score = (uint16_t)(score - ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_CURSED)]);
    if (m->ds[(uint16_t)(MONSTER_RECORD(m->ds[XEEN_DS_TARGET_MONSTER]) + XEEN_MAP_MONSTER_CONDITION)]) {
        score = (uint16_t)(score + 20u);
    }
    threshold = (uint16_t)(10u + target_row[XEEN_MONSTER_ARMOR]);
    r->c->last_attack_score = score;
    r->c->last_attack_threshold = threshold;
    *hit = score >= threshold; /* compared unsigned (Original: JB at 388FD) */
    return 1;
}

/* Splits EXPERIENCE over the able combat members, doubled for a member below level 15. Original: 36EF2. */
static int award_experience(run *r, uint32_t experience)
{
    xeen_movement *m = r->m;
    unsigned i, count = xeen_active_party_count(m->ds), eligible = 0;
    for (i = 0; i < count; ++i) {
        uint8_t *ch = m->ds + XEEN_PARTY_RECORDS +
                      xeen_active_party_slot(m->ds, i) * XEEN_CHARACTER_BYTES;
        uint8_t worst = xeen_character_worst_condition(ch);
        if (!xeen_condition_is_deathlike(worst)) {
            ++eligible;
        }
    }
    if (!eligible) {
        return fail(r, EXPERIENCE_STOP_NO_ELIGIBLE);
    }
    for (i = 0; i < count; ++i) {
        uint8_t *ch = m->ds + XEEN_PARTY_RECORDS +
                      xeen_active_party_slot(m->ds, i) * XEEN_CHARACTER_BYTES;
        uint8_t worst = xeen_character_worst_condition(ch);
        uint32_t share = experience / eligible;
        if (xeen_condition_is_deathlike(worst)) {
            continue;
        }
        if (ch[XEEN_CHARACTER_LEVEL] < 15u) {
            share <<= 1;
        }
        xeen_put_le32(ch + XEEN_CHARACTER_EXPERIENCE,
                      xeen_get_le32(ch + XEEN_CHARACTER_EXPERIENCE) + share);
    }
    ++r->c->rewards_applied;
    return 1;
}

/* The Clouds' quest kills, by the killed monster's record TARGET on the party's map: record 28 on map 41 sets game
 * flag 0Bh; record 8 on map 78 sets game flag 3Ch, clears quest flag 17h and gives every party member award 2Ah
 * (the award byte's bit 0); record 27 on map 29 sets game flag 68h. Original: 36ADD..36B7A (463DE, 2F8F3). */
static void clouds_quest_kill(xeen_movement *m, uint8_t target)
{
    if (target == 28u && m->ds[XEEN_DS_PARTY_MAP] == 41u) {
        xeen_ds_set_bit(m->ds, XEEN_DS_GAME_FLAGS, 0x0bu, 1);
    }
    if (target == 8u && m->ds[XEEN_DS_PARTY_MAP] == 78u) {
        xeen_ds_set_bit(m->ds, XEEN_DS_GAME_FLAGS, 0x3cu, 1);
        xeen_ds_set_bit(m->ds, XEEN_DS_QUEST_FLAGS, 0x17u, 0);
        for (unsigned k = 0; k < m->ds[XEEN_DS_PARTY_COUNT]; ++k) {
            m->ds[(uint16_t)(XEEN_PARTY_RECORDS + k * XEEN_CHARACTER_BYTES + XEEN_CHARACTER_AWARDS + 0x2au)] |= 1u;
        }
    }
    if (target == 27u && m->ds[XEEN_DS_PARTY_MAP] == 29u) {
        xeen_ds_set_bit(m->ds, XEEN_DS_GAME_FLAGS, 0x68u, 1);
    }
}
/* Lord Xeen's weapon on the Clouds: weapon 5Ah, material 0, into the first pending weapon row (overwriting whatever
 * was there), the pending items flagged and quest item 5Ah counted. Original: 36C4B..36C77 (9A10, F07F). */
static void lord_xeen_weapon(xeen_movement *m)
{
    m->ds[XEEN_DS_TREASURE_WEAPONS + XEEN_ITEM_ROW_MATERIAL] = 0;
    m->ds[XEEN_DS_TREASURE_WEAPONS + XEEN_ITEM_ROW_ID] = 0x5au;
    m->ds[XEEN_DS_TREASURE_WEAPONS + XEEN_ITEM_ROW_STATE] = 0;
    m->ds[XEEN_DS_TREASURE_ITEMS_PENDING] = 1;
    ++m->ds[XEEN_DS_QUEST_ITEMS + 0x5au];
}
/*
 * A killed monster: on Clouds its quest kills first (clouds_quest_kill); then sound effect 4 and the experience;
 * outside map 85 the monster's gold and gems go to the treasure, Clouds' Lord Xeen leaves his weapon
 * (lord_xeen_weapon) and the monster's drop is rolled; then the monster leaves its map record, its hit frame and
 * the target are cleared, one scene runs, and the party cell's first monster, when one is left, becomes the target.
 * Original: 36AD1..36ECE (Darkside, DS 94 nonzero, jumps from 36AD8 to the common 36B7A); the quest kills
 * 36ADD..36B7A by the record 306A (36AEB the first); the award 463DE(record, 2Ah, 1), i.e. +39h + 2Ah |= 1 for each
 * of the ED8C members (B41E + k x 162h); the flags F01F's and F06F's bits (2F8F3); Lord Xeen's weapon 36C4B..36C77.
 */
static int monster_death(run *r, const uint8_t *row)
{
    xeen_movement *m = r->m;
    uint8_t target = m->ds[XEEN_DS_TARGET_MONSTER];
    uint16_t record = MONSTER_RECORD(target);
    if (!m->ds[XEEN_DS_WORLD]) {
        clouds_quest_kill(m, target);
    }
    if (!play_effect(r, 4u) || !award_experience(r, xeen_get_le32(row + XEEN_MONSTER_EXPERIENCE))) {
        return 0;
    }
    ++r->c->monster_deaths;
    if (m->ds[XEEN_DS_PARTY_MAP] != 85u) {
        xeen_put_le32(m->ds + XEEN_DS_TREASURE_GOLD,
                      xeen_get_le32(m->ds + XEEN_DS_TREASURE_GOLD) + xeen_get_le16(row + XEEN_MONSTER_GOLD));
        xeen_put_le32(m->ds + XEEN_DS_TREASURE_GEMS,
                      xeen_get_le32(m->ds + XEEN_DS_TREASURE_GEMS) + row[XEEN_MONSTER_GEMS]);
        if (!m->ds[XEEN_DS_WORLD] && m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_ID)] == XEEN_CLOUDS_LORD_XEEN) {
            lord_xeen_weapon(m);
        }
        if (row[XEEN_MONSTER_DROP] && !monster_drop(r, row[XEEN_MONSTER_DROP])) {
            return 0;
        }
    }
    m->ds[record] = m->ds[(uint16_t)(record + 1u)] = XEEN_MAP_MONSTER_REMOVED;
    m->ds[XEEN_DS_HIT_FRAMES + m->ds[XEEN_DS_HIT_SLOT]] = 0;
    m->ds[XEEN_DS_TARGET_MONSTER] = XEEN_NO_TARGET;
    if (!scene(r)) {
        return 0;
    }
    if (m->ds[XEEN_DS_VIEW_MONSTER(0)] != XEEN_VIEW_NO_MONSTER) {
        m->ds[XEEN_DS_TARGET_MONSTER] = m->ds[XEEN_DS_VIEW_MONSTER(0)];
        m->ds[XEEN_DS_HIT_SLOT] = 0;
    }
    return 1;
}

/* A monster's saving throw: a roll of 1..ID + 50 that is at most ID (signed) saves. Original: 38ADB(id). */
static int monster_save(run *r, uint8_t id)
{
    return (int16_t)random_word(r, 1u, id + 50u, id + 50u) <= (int16_t)id;
}
/* The saving throw of the monster at RECORD (monster_save by its id); a saved throw counts in volley_saves. */
static int monster_saves(run *r, uint16_t record)
{
    if (!monster_save(r, r->m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_ID)])) {
        return 0;
    }
    ++r->c->volley_saves;
    return 1;
}

/* The kinds of hit on a monster (monster_damage's KIND): the party's melee, a spell, a spell whose damage its own rule
 * took from the monster (typed_spell_damage: Finger of Death, Mass Distortion, Holy Word and Turn Undead on any other
 * monster; every reader treats it as a spell) and one missile hit of a volley. Melee and missiles are the physical
 * hits. Original: 36738's argument. */
enum hit_kind { HIT_MELEE = 0, HIT_SPELL = 1, HIT_SPELL_BY_RULE = 2, HIT_MISSILE = 3 };
/* The extra damage the attack's element (the weapon's material) adds to a melee hit or a volley missile (TYPE 0 or
 * 3) on the monster of database ROW: the material's element damage, scaled down by the monster's resistance to the
 * element's category (100: immune), in the original's 16-bit arithmetic. Spells (types 1 and 2) get no extra: the
 * original reads the element's resistance for them too but scales a zero by it, so a monster's element resistance
 * never changes spell damage.
 * Original: 36FB5: 20D9[96ED], the category 2A26D (row 22h..27h), the IMUL low word, CWD, IDIV 100; for spells 3704C
 * sets SI 0, 972E 0..6 by the CS:0B64 table at 3705C (+28h, +27h, +22h..+26h), then 371B3. */
static int16_t element_extra(const xeen_movement *m, const uint8_t *row, unsigned kind)
{
    uint8_t material = m->ds[XEEN_DS_ATTACK_ELEMENT];
    int16_t extra = 0;
    if (kind == HIT_MELEE || kind == HIT_MISSILE) {
        extra = m->ds[(uint16_t)((int)XEEN_DS_ELEMENT_DAMAGE + (int8_t)material)];
        if (material) {
            uint8_t resist = row[XEEN_MONSTER_RESISTANCES + element_category(material)];
            if (resist == 100u) {
                extra = 0;
            } else if (resist) {
                extra = (int16_t)((int16_t)((100 - (int)resist) * extra) / 100);
            }
        }
    }
    return extra;
}
/* The defences against a nonzero hit of KIND (enum hit_kind) on the monster at RECORD (database ROW), in the original's
 * order:
 *   Lord Xeen (Clouds) takes 100 more. The original's test meant for the Xeen Slayer Sword compares a boolean with the
 *     sword's id, which is never equal, so its zeroing branch never runs (an original quirk, kept);
 *   a sleeping monster (Sleep or Dragon Sleep) wakes;
 *   physical resistance (100: immune) scales melee and physical spells and missiles (any kind while the damage type is
 *     0), never the Xeen Slayer Sword;
 *   a hit that still does damage animates in hit SLOT, marked physical only for physical melee or missile damage;
 *   the weapon's element adds its extra (element_extra); a positive extra marks the slot's element and scale, and
 *     anything else clears the slot's element except a volley missile's.
 * Returns the damage after them, which may be zero; the caller reads it as signed (below 1: no damage).
 * Original: 36787 compares !96EE with 22h (CBW, NEG, SBB, INC), the dead branch 367A6; the resistance 367EB (a nonzero
 * type only with 972E zero); the animation 36882; the element 36FB5, 97A6 and 3667D. */
static uint32_t defended_damage(xeen_movement *m, uint16_t record, const uint8_t *row, uint8_t slot, unsigned kind,
                                uint16_t spell, uint32_t damage)
{
    uint8_t resist, material;
    int16_t extra;
    if (!m->ds[XEEN_DS_WORLD] && m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_ID)] == XEEN_CLOUDS_LORD_XEEN) {
        damage += 100u;
    }
    if (m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_CONDITION)] == XEEN_DAMAGE_SLEEP ||
        m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_CONDITION)] == XEEN_DAMAGE_DRAGON_SLEEP) {
        m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_CONDITION)] = 0;
    }
    resist = row[XEEN_MONSTER_PHYSICAL_RESISTANCE];
    if ((kind == HIT_MELEE || !xeen_read16(m, XEEN_DS_DAMAGE_TYPE)) &&
        m->ds[XEEN_DS_ATTACK_WEAPON] != XEEN_WEAPON_XEEN_SLAYER_SWORD && resist) {
        damage = resist == 100u
                         ? 0u
                         : (uint32_t)((int32_t)(damage * (uint32_t)(100u - resist)) / 100);
    }
    if (damage) {
        m->ds[XEEN_DS_HIT_FRAMES + slot] = 3;
        m->ds[XEEN_DS_HIT_PHYSICAL + slot] = (uint8_t)(!spell && (kind == HIT_MELEE || kind == HIT_MISSILE));
        m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_FRAME)] = 11u;
        m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_FRAME_HOLD)] = 5u;
    }
    material = m->ds[XEEN_DS_ATTACK_ELEMENT];
    extra = element_extra(m, row, kind);
    damage = (uint32_t)((int32_t)damage + extra);
    if (extra > 0) {
        m->ds[XEEN_DS_HIT_ELEMENT + slot] = (uint8_t)(element_category(material) + 1u);
        xeen_write16(m, (uint16_t)(XEEN_DS_HIT_ELEMENT_SCALE + 2u * slot), XEEN_HIT_SCALE(extra));
    } else if (kind != HIT_MISSILE) {
        m->ds[XEEN_DS_HIT_ELEMENT + slot] = 0;
    }
    return damage;
}
/* A spell's lasting effect on the monster at RECORD, by damage type SPELL (at most Magic Arrow): the effect in its
 * +0Eh word and a cleared +0Ch word. Original: 36931, the 57FD[972E] bytes. */
static void spell_effect(xeen_movement *m, uint16_t record, uint16_t spell)
{
    static const uint8_t effect_table[19] = {3, 10, 4, 11, 1, 2, 5, 9, 5, 14, 5, 14, 10, 8, 3, 9, 2, 2, 3};
    xeen_write16(m, (uint16_t)(record + XEEN_MAP_MONSTER_EFFECT), effect_table[spell]);
    xeen_write16(m, (uint16_t)(record + XEEN_MAP_MONSTER_EFFECT_CYCLE), 0);
}
/* The damage after the monster saved against a spell or missile of damage type SPELL: Finger of Death and Mass
 * Distortion do 5; the other spells Sleep..Magic Arrow keep their damage, except Poison Volley; everything else
 * (the elements, Poison Volley) is halved by an arithmetic shift. Original: 36964 (the 3698D table, 972E 7..18). */
static uint32_t saved_damage(uint16_t spell, uint32_t damage)
{
    if (spell == XEEN_DAMAGE_FINGER_OF_DEATH || spell == XEEN_DAMAGE_MASS_DISTORTION) {
        return 5u;
    }
    if (spell < XEEN_DAMAGE_SLEEP || spell > XEEN_DAMAGE_MAGIC_ARROW || spell == XEEN_DAMAGE_POISON_VOLLEY) {
        return (uint32_t)((int32_t)damage >> 1);
    }
    return damage;
}
/* The sound of a damaging hit with WEAPON (an index below 40): its power (0..5, by weapon) names the voice
 * "pow<power>.voc" and the effect 60 + power. Returns 0 when the effect cannot play.
 * Original: 36A15 "pow%d.voc" (DS:6760) of the power byte at 57FA:[96EE] (36A05: another segment's table, not the
 * program data), 36A51 187E9. */
static int hit_sound(run *r, uint8_t weapon)
{
    static const uint8_t power[40] = {0, 5, 4, 5, 5, 5, 5, 2, 4, 5, 3, 5, 4, 2,
                                      3, 2, 2, 4, 5, 5, 5, 5, 5, 1, 3, 2, 5, 1,
                                      1, 1, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0};
    (void)snprintf(sample_name(r->m), SAMPLE_NAME_BYTES, (const char *)r->m->ds + XEEN_TEXT_HIT_VOICE_FORMAT,
                   (int)power[weapon]);
    if (!play_effect(r, (uint16_t)(60u + power[weapon]))) {
        return 0;
    }
    xeen_native_audio_voice_name(r->audio, r->m);
    return 1;
}
/* Subtracts DAMAGE from the HP of the monster at RECORD, at least to zero; returns 1 when it killed the monster. */
static int take_monster_hp(xeen_movement *m, uint16_t record, uint32_t damage)
{
    uint16_t hp = xeen_read16(m, (uint16_t)(record + XEEN_MAP_MONSTER_HP));
    if (hp <= damage) {
        xeen_write16(m, (uint16_t)(record + XEEN_MAP_MONSTER_HP), 0);
        return 1;
    }
    xeen_write16(m, (uint16_t)(record + XEEN_MAP_MONSTER_HP), (uint16_t)(hp - damage));
    return 0;
}
/* A hit of no damage (below 1, signed) on the monster TARGET: the add-on's miss, the miss sound (6) and a scene; the
 * last hit is marked missed. Returns 0 when the sound or the scene stops. Original: 36738's branch at 36A9E. */
static int monster_missed(run *r, uint8_t target)
{
    XEEN_DAMAGE_NUMBERS(monster(r->m->ds, target, 0)); /* add-on hook */
    if (!play_effect(r, 6u) || !scene(r)) {
        return 0;
    }
    r->c->last_hit = 0;
    return 1;
}
/* A damaging hit after its scene and sound: the add-on's number, the HP drop (take_monster_hp), the last hit marked,
 * a scene, and a lethal hit's death (monster_death). Returns 0 when the scene or the death stops. */
static int monster_wounded(run *r, uint8_t target, uint16_t record, const uint8_t *row, uint32_t damage)
{
    int lethal;
    XEEN_DAMAGE_NUMBERS(
            monster(r->m->ds, target,
                    damage > 0x7fffffffu ? -0x7fffffff : -(int32_t)damage)); /* add-on hook */
    lethal = take_monster_hp(r->m, record, damage);
    r->c->last_hit = 1;
    if (!scene(r)) {
        return 0;
    }
    return lethal ? monster_death(r, row) : 1;
}
/* Damage of KIND (enum hit_kind) to the targeted monster. The
 * hit slot indexes the twelve hit-animation arrays (melee 0..2; a volley's tiers advance it up to 11).
 * The steps: on Clouds spells and missiles do nothing to Lord Xeen; a nonzero hit meets the monster's defences
 * (defended_damage), a spell leaves its effect (spell_effect), and a spell or missile allows a saving throw
 * (monster_saves, saved_damage). Less than 1 is a miss (monster_missed). Otherwise the damage is shown (the dword
 * FAFA, the slot's scale, a scene), the hit sounds by the weapon's power (hit_sound) and wounds the monster
 * (monster_wounded).
 * Original: 36738 against the target 306A; the slot 9804; the volley 37452; Lord Xeen's immunity 36743, to 36AAC. */
static int monster_damage(run *r, uint32_t damage, unsigned kind)
{
    xeen_movement *m = r->m;
    uint8_t target = m->ds[XEEN_DS_TARGET_MONSTER], slot = m->ds[XEEN_DS_HIT_SLOT];
    uint16_t record = MONSTER_RECORD(target), spell = xeen_read16(m, XEEN_DS_DAMAGE_TYPE);
    const uint8_t *row = monster_row(r, target);
    if (!row || slot > 11u || kind > HIT_MISSILE) {
        return fail(r, MONSTER_DAMAGE_STOP_SLOT);
    }
    if (!m->ds[XEEN_DS_WORLD] && damage && kind != HIT_MELEE &&
        m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_ID)] == XEEN_CLOUDS_LORD_XEEN) {
        damage = 0;
    }
    if (damage) {
        damage = defended_damage(m, record, row, slot, kind, spell, damage);
        if (kind == HIT_SPELL || kind == HIT_SPELL_BY_RULE) {
            if (spell > XEEN_DAMAGE_MAGIC_ARROW) {
                return fail(r, MONSTER_DAMAGE_STOP_SPELL);
            }
            spell_effect(m, record, spell);
        }
        if (kind != HIT_MELEE && monster_saves(r, record)) {
            damage = saved_damage(spell, damage);
        }
    }
    if ((int32_t)damage < 1) {
        return monster_missed(r, target);
    }
    xeen_put_le32(m->ds + 0xfafa, damage);
    xeen_write16(m, (uint16_t)(XEEN_DS_HIT_DAMAGE_SCALE + 2u * slot), XEEN_HIT_SCALE(damage));
    if (!scene(r)) {
        return 0;
    }
    if (m->ds[XEEN_DS_ATTACK_WEAPON] >= 40u) {
        return fail(r, MONSTER_DAMAGE_STOP_WEAPON);
    }
    if (!hit_sound(r, m->ds[XEEN_DS_ATTACK_WEAPON])) {
        return 0;
    }
    return monster_wounded(r, target, record, row, damage);
}

/* Member CH's melee attack on the target monster: level / the class's divisor + 1 swings, each hit adding the
 * Might bonus plus the attack's damage (at least 0); each equipped slayer weapon of the monster's type triples the
 * total; then the monster's damage and the turn order rebuilt. A missing target or a class past 9 stops.
 * Original: 371E2 with range argument zero. */
static int party_attack(run *r, const uint8_t *ch)
{
    /* attacks a round = level / divisors[class] + 1: 371E2's per-class switch (CS:129F, DI 4..8) gives the values
     * of DS:2E01 (XEEN_DS_CLASS_ATTACK_DIVISOR, which only the Level details read, 42A35) */
    static const uint8_t divisors[10] = {5, 6, 6, 7, 8, 6, 5, 4, 7, 6};
    /* the slayer kinds 1..6 (DS:2620's "Dragons", "Undead", "Golems", "Bugs", "Monsters", "Beasts") as types */
    static const uint8_t slayer[6] = {XEEN_MONSTER_TYPE_DRAGON, XEEN_MONSTER_TYPE_UNDEAD, XEEN_MONSTER_TYPE_GOLEM,
                                      XEEN_MONSTER_TYPE_BUG,    XEEN_MONSTER_TYPE_MONSTER, XEEN_MONSTER_TYPE_BEAST};
    xeen_movement *m = r->m;
    const uint8_t *row;
    uint32_t total = 0;
    unsigned attacks, i;
    int hit;
    if (m->ds[XEEN_DS_TARGET_MONSTER] == XEEN_NO_TARGET || !(row = monster_row(r, m->ds[XEEN_DS_TARGET_MONSTER]))) {
        return fail(r, ATTACK_STOP_TARGET);
    }
    if (ch[XEEN_CHARACTER_CLASS] > 9u) {
        return fail(r, ATTACK_STOP_CLASS);
    }
    xeen_write16(m, XEEN_DS_DAMAGE_TYPE, XEEN_ATTACK_PHYSICAL);
    attacks =
            (unsigned)(xeen_character_current_level(ch) / divisors[ch[XEEN_CHARACTER_CLASS]]) + 1u;
    r->c->player_attacks = (uint8_t)attacks;
    int dn_hits = 0; /* add-on hook */
    for (i = 0; i < attacks; ++i) {
        if (!player_hit(r, ch, row, 0u, &hit)) {
            return 0;
        }
        dn_hits += hit; /* add-on hook */
        if (hit) {
            int damage = xeen_combat_stat_bonus(m, xeen_combat_stat(m, ch, XEEN_STAT_MIGHT, 0)) +
                         xeen_sign16(xeen_read16(m, XEEN_DS_ATTACK_DAMAGE));
            total += (uint32_t)(damage < 1 ? 0 : damage);
        }
    }
    for (i = 0; i < XEEN_ITEM_ROWS; ++i) {
        const uint8_t *item = ch + XEEN_CHARACTER_ITEM(XEEN_ITEMS_WEAPONS, i);
        unsigned kind = (unsigned)(item[XEEN_ITEM_ROW_STATE] & 0xfu);
        if (item[XEEN_ITEM_ROW_EQUIPPED] && kind >= 1u && kind <= 6u && row[XEEN_MONSTER_TYPE] == slayer[kind - 1u]) {
            total *= 3u;
        }
    }
    r->c->last_damage = (uint16_t)total;
    if (!dn_hits) {
        XEEN_DAMAGE_NUMBERS(monster_miss(m->ds, m->ds[XEEN_DS_TARGET_MONSTER])); /* add-on hook */
    }
    if (!monster_damage(r, total, HIT_MELEE)) {
        return 0;
    }
    return build_order(r);
}

/* ------------------------------------------------- party ranged (Shoot) */

int xeen_combat_has_missile(const uint8_t *ch)
{
    unsigned i;
    /* 39721: the first slot whose location byte is 4 (XEEN_SLOT_MISSILE) decides; worst
     * condition 8 and 11..15 (the 3976A table) cannot shoot. */
    if (!ch) {
        return 0;
    }
    for (i = 0; i < XEEN_ITEM_ROWS; ++i) {
        if (ch[XEEN_CHARACTER_ITEM(XEEN_ITEMS_WEAPONS, i) + XEEN_ITEM_ROW_EQUIPPED] == XEEN_SLOT_MISSILE) {
            return !xeen_condition_cannot_act(xeen_character_worst_condition(ch));
        }
    }
    return 0;
}

/* The party's missile volley at the target: in party order, every shooting row that has not missed this target
 * rolls one hit test; a hit uses up the row, deals the volley's damage (or the row's own, sign-extended) as a
 * missile hit and clears that row's projectile. The volley ends after a kill that leaves no target and, unlike
 * melee, does not rebuild the turn order.
 * Original: 37452, from 371E2 with a nonzero range for 972A = 1 and 972E other than 12h; the test 38818; the
 * damage 9730 or 96E6 through 36738 type 3; the target 306A; the order 38D25. */
static int party_volley(run *r)
{
    xeen_movement *m = r->m;
    unsigned i;
    if (m->ds[XEEN_DS_PARTY_COUNT] > 8u) {
        return fail(r, VOLLEY_STOP_PARTY);
    }
    memset(m->ds + XEEN_DS_HIT_ELEMENT, 0, XEEN_HIT_SLOTS);
    for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
        const uint8_t *row;
        uint8_t *ch = m->ds + XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES;
        int hit;
        int32_t damage;
        if (!m->ds[XEEN_DS_MISSILE_SLOTS + i] || m->ds[XEEN_DS_VOLLEY_MISSED + i]) {
            continue;
        }
        if (!(row = monster_row(r, m->ds[XEEN_DS_TARGET_MONSTER]))) {
            return fail(r, VOLLEY_STOP_TARGET);
        }
        if (!player_hit(r, ch, row, 1u, &hit)) {
            return 0;
        }
        if (!hit) {
            XEEN_DAMAGE_NUMBERS(monster_miss(m->ds, m->ds[XEEN_DS_TARGET_MONSTER])); /* add-on hook */
        }
        if (!hit) {
            ++m->ds[XEEN_DS_VOLLEY_MISSED + i];
            ++r->c->volley_misses;
            continue;
        }
        damage = xeen_read16(m, XEEN_DS_VOLLEY_DAMAGE) ? (int32_t)xeen_read16(m, XEEN_DS_VOLLEY_DAMAGE)
                                        : (int32_t)xeen_sign16(xeen_read16(m, XEEN_DS_ATTACK_DAMAGE));
        m->ds[XEEN_DS_MISSILE_SLOTS + i] = 0;
        ++r->c->volley_hits;
        r->c->last_player_actor = (uint8_t)i;
        r->c->last_damage = (uint16_t)damage;
        if (!monster_damage(r, (uint32_t)damage, HIT_MISSILE)) {
            return 0;
        }
        xeen_projectile_clear_member(m, i); /* 37506/37581 */
        if (m->ds[XEEN_DS_TARGET_MONSTER] == XEEN_NO_TARGET) {
            return 1;
        }
    }
    return 1;
}

/* Whether a spell of damage type SPELL affects a monster of TYPE: Sleep, Beast Master, Hypnotize and Finger of Death
 * beasts and type 3; Dragon Sleep dragons; Holy Word and Turn Undead the undead; Golem Stopper golems; Insect Spray
 * bugs; every other damage type any monster. What an unaffected monster gets is the spell's own rule
 * (typed_spell_damage). Original: the type tests of 376C1's handlers (the type row +21h). */
static int spell_affects(unsigned spell, uint8_t type)
{
    switch (spell) {
        case XEEN_DAMAGE_SLEEP:
        case XEEN_DAMAGE_BEAST_MASTER:
        case XEEN_DAMAGE_HYPNOTIZE:
        case XEEN_DAMAGE_FINGER_OF_DEATH:
            return type == XEEN_MONSTER_TYPE_BEAST || type == 3u;
        case XEEN_DAMAGE_DRAGON_SLEEP:
            return type == XEEN_MONSTER_TYPE_DRAGON;
        case XEEN_DAMAGE_HOLY_WORD:
        case XEEN_DAMAGE_TURN_UNDEAD:
            return type == XEEN_MONSTER_TYPE_UNDEAD;
        case XEEN_DAMAGE_GOLEM_STOPPER:
            return type == XEEN_MONSTER_TYPE_GOLEM;
        case XEEN_DAMAGE_INSECT_SPRAY:
            return type == XEEN_MONSTER_TYPE_BUG;
        default:
            return 1;
    }
}
/*
 * The damage of a spell without a precomputed amount, by its damage type SPELL against the monster at RECORD (of
 * TYPE; spell_affects): 1 with *DAMAGE and *HIT_TYPE (the damage kind) set, or 0 when the hit ends here with no
 * damage and no after-hit update:
 *   Sleep, Beast Master, Hypnotize and Dragon Sleep end the hit; an affected monster that fails its saving throw
 *     (monster_saves) takes the spell as its condition (counted in volley_hits);
 *   Golem Stopper: golems 100, any other monster ends the hit;
 *   Finger of Death: an affected monster that fails its saving throw loses its HP up to 50, as HIT_SPELL_BY_RULE;
 *     otherwise the hit ends;
 *   Mass Distortion: half the HP, at least 1, as HIT_SPELL_BY_RULE;
 *   Holy Word: the undead's whole HP; Turn Undead: the undead 25; to any other monster both do 0 as
 *     HIT_SPELL_BY_RULE;
 *   Insect Spray: bugs 25, others 0; Magic Arrow: 8;
 *   any other type (damage type 17, an element with no precomputed amount): 0.
 * *DAMAGE and *HIT_TYPE come in as 0 and HIT_SPELL. The only DS write is the condition; the only draws are the
 * saving throws.
 * Original: the damage at 376C1 (972E 7..18 by CS:127B, the type row +21h); the save 38ADB; the condition store 377FC.
 */
static int typed_spell_damage(run *r, unsigned spell, uint8_t type, uint16_t record, uint32_t *damage,
                              unsigned *hit_type)
{
    xeen_movement *m = r->m;
    uint16_t hp = xeen_read16(m, (uint16_t)(record + XEEN_MAP_MONSTER_HP));
    int affected = spell_affects(spell, type);
    switch (spell) {
        case XEEN_DAMAGE_SLEEP:
        case XEEN_DAMAGE_BEAST_MASTER:
        case XEEN_DAMAGE_HYPNOTIZE:
        case XEEN_DAMAGE_DRAGON_SLEEP:
            if (affected && !monster_saves(r, record)) {
                m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_CONDITION)] = (uint8_t)spell;
                ++r->c->volley_hits;
            }
            return 0;
        case XEEN_DAMAGE_FINGER_OF_DEATH:
            if (!affected || monster_saves(r, record)) {
                return 0;
            }
            *damage = hp > 50u ? 50u : hp;
            *hit_type = HIT_SPELL_BY_RULE;
            return 1;
        case XEEN_DAMAGE_HOLY_WORD:
        case XEEN_DAMAGE_TURN_UNDEAD:
            if (affected) {
                *damage = spell == XEEN_DAMAGE_HOLY_WORD ? hp : 25u;
            } else {
                *hit_type = HIT_SPELL_BY_RULE;
            }
            return 1;
        case XEEN_DAMAGE_MASS_DISTORTION:
            *damage = hp >> 1 ? (uint32_t)(hp >> 1) : 1u;
            *hit_type = HIT_SPELL_BY_RULE;
            return 1;
        case XEEN_DAMAGE_GOLEM_STOPPER:
            if (!affected) {
                return 0;
            }
            *damage = 100u;
            return 1;
        case XEEN_DAMAGE_INSECT_SPRAY:
            if (affected) {
                *damage = 25u;
            }
            return 1;
        case XEEN_DAMAGE_MAGIC_ARROW:
            *damage = 8u;
            return 1;
        default:
            return 1;
    }
}
/*
 * A spell's single hit on the targeted monster. A monster with magic resistance resists when its resistance exceeds a
 * random 1..(the caster's level + 100): no damage and no after-hit update. Otherwise the damage is the spell's
 * precomputed amount (XEEN_DS_VOLLEY_DAMAGE) when nonzero, else the damage type's rule for the monster's type
 * (typed_spell_damage), which can also end the hit (immune by type, saved, a condition). Then the damage (as
 * HIT_SPELL unless the rule says HIT_SPELL_BY_RULE) and the after-hit update.
 * Original: 37621 (371E2 with a nonzero range; the caster ES:BX = 9802:9800, the target 306A); the resistance (row
 * +27h) at 376C1; the immune return 378E7; then 36738(damage, SI) and 38D25, SI the 371E2 range (1) unless 2.
 */
static int spell_hit(run *r)
{
    xeen_movement *m = r->m;
    const uint8_t *row;
    uint16_t level, roll, caster = xeen_read16(m, XEEN_DS_ACTING_CHARACTER), record;
    uint32_t damage;
    unsigned hit_type = HIT_SPELL, spell;
    uint8_t type;
    if (m->ds[XEEN_DS_TARGET_MONSTER] == XEEN_NO_TARGET || !(row = monster_row(r, m->ds[XEEN_DS_TARGET_MONSTER]))) {
        return fail(r, SPELL_HIT_STOP_TARGET);
    }
    if (row[XEEN_MONSTER_MAGIC_RESISTANCE]) {
        if (caster > 0x10000u - XEEN_CHARACTER_BYTES) {
            return fail(r, SPELL_HIT_STOP_CASTER);
        }
        level = xeen_character_current_level(m->ds + caster);
        roll = random_word(r, 1u, level + 100u, level + 100u);
        if ((int16_t)row[XEEN_MONSTER_MAGIC_RESISTANCE] > (int16_t)roll) {
            ++r->c->volley_saves;
            return 1;
        }
    }
    record = MONSTER_RECORD(m->ds[XEEN_DS_TARGET_MONSTER]);
    type = row[XEEN_MONSTER_TYPE];
    damage = xeen_read16(m, XEEN_DS_VOLLEY_DAMAGE);
    spell = xeen_read16(m, XEEN_DS_DAMAGE_TYPE);
    if (!damage && !typed_spell_damage(r, spell, type, record, &damage, &hit_type)) {
        return 1;
    }
    r->c->last_damage = (uint16_t)damage;
    ++r->c->volley_hits;
    if (!monster_damage(r, damage, hit_type)) {
        return 0;
    }
    return build_order(r);
}
/* One ranged hit of the party's volley: the shooting rows' missiles when the volley shoots by member (except Magic
 * Arrow), else a spell's hit on the target. Original: 371E2(ES:BX, range) as 281A7 calls it: 37452 for 972A 1 and
 * 972E other than 12h, else 37621. */
static int party_ranged(run *r)
{
    xeen_movement *m = r->m;
    if (xeen_read16(m, XEEN_DS_VOLLEY_BY_MEMBER) == 1u && xeen_read16(m, XEEN_DS_DAMAGE_TYPE) != XEEN_DAMAGE_MAGIC_ARROW) {
        return party_volley(r);
    }
    return spell_hit(r);
}

/* Whether the volley's line of fire ends before TIER: outdoors, the view cell at that distance (7, 14, 27) holds a
 * mountain kind or a dense forest (terrain 1, 3, 6, 7, 9, 10, 12: what a shot does not cross, as
 * xeen_ranged_distance), indoors the cell's (2, 7, 14) wall value is at or above the map's threshold (signed).
 * Original: 281A7's tier test: 1C1C1, the terrain F48E, the threshold C0FC. */
static int tier_blocked(run *r, unsigned tier, int *blocked)
{
    static const uint16_t outdoor[3] = {7, 14, 27}, indoor[3] = {2, 7, 14};
    xeen_movement *m = r->m;
    uint16_t ax = 0;
    if (xeen_navigation_query(m, xeen_read16(m, XEEN_DS_OUTDOORS) ? outdoor[tier] : indoor[tier], &ax) !=
        XEEN_OK) {
        return fail(r, TIER_STOP_QUERY);
    }
    if (xeen_read16(m, XEEN_DS_OUTDOORS)) {
        uint16_t terrain = xeen_read16(m, XEEN_DS_CELL_WALLS);
        *blocked = terrain == 1u || terrain == 3u || terrain == 6u || terrain == 7u ||
                   terrain == 9u || terrain == 10u || terrain == 12u;
    } else {
        *blocked = (int16_t)ax >= (int16_t)m->ds[XEEN_DS_MAP_FIELD(THRESHOLD)];
    }
    return 1;
}

/* Before the treasure dialog: the shooting rows cleared, the missiles landed, one scene, the treasure's sound when
 * gold or gems are pending, and the input code cleared. Original: 2EB9C, 1B503(1), FX 36h, 189D7. */
static int treasure_prefix(run *r)
{
    xeen_movement *m = r->m;
    memset(m->ds + XEEN_DS_MISSILE_SLOTS, 0, 8);
    m->ds[XEEN_DS_MISSILES_FLYING] = 0;
    scene_site = 0x2ebb8u;
    if (!scene(r)) {
        return 0;
    }
    if ((xeen_get_le32(m->ds + XEEN_DS_TREASURE_GEMS) || xeen_get_le32(m->ds + XEEN_DS_TREASURE_GOLD)) &&
        !play_effect(r, 0x36u)) {
        return 0;
    }
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    return 1;
}
xeen_status xeen_combat_treasure_prefix(xeen_combat *c, xeen_movement *m, const uint8_t *db,
                                        size_t n, xeen_native_audio *audio, uint32_t *boundary)
{
    run r;
    if (!c || !m || !db || !boundary || (m->ds[XEEN_DS_SOUND_ON] && !audio)) {
        return XEEN_BAD_INPUT;
    }
    r = (run){c, m, db, n, NULL, NULL, audio, 0};
    *boundary = 0;
    if (xeen_treasure_check(m) != XEEN_TREASURE_DIALOG) {
        return XEEN_BAD_INPUT;
    }
    if (treasure_prefix(&r)) {
        return XEEN_OK;
    }
    *boundary = r.boundary;
    return XEEN_SERVICE_REQUIRED;
}
/* How 281A7's stages hand over to the next. */
typedef enum volley_flow {
    VOLLEY_UNSUPPORTED, /* an unsupported boundary is recorded in the run */
    VOLLEY_CONTINUE,    /* on to the next stage */
    VOLLEY_NO_SHOOTER,  /* 28268: no row shoots; 281A7 returns at once (no close-out) */
    VOLLEY_FINISHED,    /* the attack ended early: 39654 (xeen_projectile_end_attack), then the close-out */
    VOLLEY_DONE         /* the tiers ran out: the close-out */
} volley_flow;

/* The volley's first stage: which party rows shoot (XEEN_DS_MISSILE_SLOTS) and whether by member.
 * Byte 97C7 is set when the party stands outdoors on surface 0 (cleared indoors); the original writes it here only
 * and no direct reader of it is known, so it stays numeric.
 *   Poison Volley: becomes poison damage shot by member from all six rows, without a sound;
 *   a volley icon other than 11 (a spell's): row 0 shoots, not by member;
 *   icon 11, the party's Shoot, by member: with physical damage every member with a missile weapon shoots (the
 *   precomputed damage cleared), with a damage type only row 0. Then effect 31h, or, when no member can shoot, the
 *   refusal effect and the volley ends at once (no close-out).
 * Original: 281A7's prologue (the surface 1C680; 39721 the missile rows; the no-shooter return 28268). */
static volley_flow volley_rows(run *r, unsigned icon)
{
    xeen_movement *m = r->m;
    unsigned i;
    int shoots = 0;
    if (xeen_read16(m, XEEN_DS_OUTDOORS)) {
        uint16_t surface = 0;
        if (xeen_maze_lookup(m, (int16_t)xeen_signed_byte(m->ds[XEEN_DS_PARTY_X]),
                             (int16_t)xeen_signed_byte(m->ds[XEEN_DS_PARTY_Y]), 0x70u, 0u,
                             &surface) != XEEN_OK) {
            fail(r, VOLLEY_STOP_ROWS);
            return VOLLEY_UNSUPPORTED;
        }
        m->ds[0x97c7] = (uint8_t)(surface == 0u);
    } else {
        m->ds[0x97c7] = 0;
    }
    if (xeen_read16(m, XEEN_DS_DAMAGE_TYPE) == XEEN_DAMAGE_POISON_VOLLEY) {
        xeen_write16(m, XEEN_DS_DAMAGE_TYPE, XEEN_ATTACK_POISON);
        xeen_write16(m, XEEN_DS_VOLLEY_BY_MEMBER, 1);
        memset(m->ds + XEEN_DS_MISSILE_SLOTS, 1, 6);
        return VOLLEY_CONTINUE;
    }
    if (icon != 11u) {
        m->ds[XEEN_DS_MISSILE_SLOTS] = 1;
        xeen_write16(m, XEEN_DS_VOLLEY_BY_MEMBER, 0);
        return VOLLEY_CONTINUE;
    }
    /* 28200: 972A=1; with 972E zero, 9730=0 and 39721 picks the rows. */
    xeen_write16(m, XEEN_DS_VOLLEY_BY_MEMBER, 1);
    if (!xeen_read16(m, XEEN_DS_DAMAGE_TYPE)) {
        xeen_write16(m, XEEN_DS_VOLLEY_DAMAGE, 0);
        for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
            if (xeen_combat_has_missile(m->ds + XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES)) {
                m->ds[XEEN_DS_MISSILE_SLOTS + i] = 1;
                shoots = 1;
            }
        }
    } else {
        m->ds[XEEN_DS_MISSILE_SLOTS] = 1;
        shoots = 1;
    }
    if (!shoots) {
        if (!play_effect(r, XEEN_CUE_REFUSED)) {
            return VOLLEY_UNSUPPORTED;
        }
        r->c->volley_end = 0x28268u;
        return VOLLEY_NO_SHOOTER;
    }
    return play_effect(r, 0x31u) ? VOLLEY_CONTINUE : VOLLEY_UNSUPPORTED;
}

/* The missiles take flight (XEEN_DS_MISSILES_FLYING) and their sprite "pow<ICON>.icn" is loaded into the resident
 * slot; its handle goes to *HANDLE and XEEN_DS_PROJECTILE_SPRITE. 0 (a stop) when the load fails.
 * Original: 14BBA "pow%d.icn" into F5E8, 18931 with the slot 8C86. */
static int volley_projectile(run *r, xeen_sprite_cache *cache, const xeen_sprite_archives *archives,
                             unsigned icon, uint16_t *handle)
{
    xeen_movement *m = r->m;
    char name[16];
    m->ds[XEEN_DS_MISSILES_FLYING] = 1;
    snprintf(name, sizeof name, "pow%u.icn", icon);
    memcpy(m->ds + XEEN_DS_FILE_NAME, name, strlen(name) + 1u);
    if (xeen_sprite_load(cache, archives, m->ds[XEEN_DS_WORLD], name,
                         xeen_read16(m, XEEN_DS_RESIDENT_SPRITE_SLOT), handle) != XR_OK) {
        return fail(r, PROJECTILE_STOP_SPRITES);
    }
    xeen_write16(m, XEEN_DS_PROJECTILE_SPRITE, *handle);
    return 1;
}

/* Tier TIER's targets: the three view-monster slots of the party's column at depth TIER (the view cells 0, 5, 12, 25:
 * slots 3 x TIER ..), the occupied ones first, the rest FFh. */
static void tier_targets(const xeen_movement *m, unsigned tier, uint8_t list[3])
{
    unsigned i, k;
    memset(list, XEEN_VIEW_NO_MONSTER, 3);
    for (i = 0, k = 0; i < 3u; ++i) {
        if (m->ds[XEEN_DS_VIEW_MONSTERS + 3u * tier + i] != XEEN_VIEW_NO_MONSTER) {
            list[k++] = m->ds[XEEN_DS_VIEW_MONSTERS + 3u * tier + i];
        }
    }
}

/* Every shooting row's projectile records (farthest level first): the scales 12, 8, 4, 0, the sprite HANDLE in
 * level 0's record (nearest the party, where a missile starts), FFFFh in the other three. */
static void arm_projectiles(xeen_movement *m, uint16_t handle)
{
    /* the projectile levels, farthest first */
    static const uint16_t outdoor[4] = {XEEN_DS_OUTDOOR_PROJECTILES(3u), XEEN_DS_OUTDOOR_PROJECTILES(2u),
                                        XEEN_DS_OUTDOOR_PROJECTILES(1u), XEEN_DS_OUTDOOR_PROJECTILES(0u)},
                          indoor[4] = {XEEN_DS_INDOOR_PROJECTILES(3u), XEEN_DS_INDOOR_PROJECTILES(2u),
                                       XEEN_DS_INDOOR_PROJECTILES(1u), XEEN_DS_INDOOR_PROJECTILES(0u)};
    const uint16_t *at = xeen_read16(m, XEEN_DS_OUTDOORS) ? outdoor : indoor;
    unsigned i, k;
    for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
        if (!m->ds[XEEN_DS_MISSILE_SLOTS + i]) {
            continue;
        }
        for (k = 0; k < 4u; ++k) {
            xeen_write16(m, (uint16_t)(at[k] + XEEN_VIEW_RECORD_BYTES * i + XEEN_VIEW_RECORD_SCALE),
                         (uint16_t)(12u - 4u * k));
        }
        xeen_write16(m, (uint16_t)(at[3] + XEEN_VIEW_RECORD_BYTES * i), handle);
        for (k = 0; k < 3u; ++k) {
            xeen_write16(m, (uint16_t)(at[k] + XEEN_VIEW_RECORD_BYTES * i), 0xffffu);
        }
    }
}

/* One tier's shots at LIST, slot by slot: every slot advances the hit slot (XEEN_DS_HIT_SLOT), an occupied one
 * becomes the target of one ranged hit (party_ranged) with its misses cleared. A volley that hits one target
 * (XEEN_VOLLEY_ONE) ends after its first. *ATTACKED is set once any target was shot.
 * Original: 281A7's target loop (9804, 973C, 306A, 371E2; the single-target end 2845D). */
static volley_flow shoot_tier(run *r, const uint8_t list[3], int *attacked)
{
    xeen_combat *c = r->c;
    xeen_movement *m = r->m;
    unsigned i;
    for (i = 0; i < 3u; ++i) {
        ++m->ds[XEEN_DS_HIT_SLOT];
        if (list[i] == XEEN_VIEW_NO_MONSTER) {
            continue;
        }
        memset(m->ds + XEEN_DS_VOLLEY_MISSED, 0, 8);
        m->ds[XEEN_DS_TARGET_MONSTER] = list[i];
        ++c->volley_targets;
        if (!party_ranged(r)) {
            return VOLLEY_UNSUPPORTED;
        }
        *attacked = 1;
        if (xeen_read16(m, XEEN_DS_VOLLEY_MODE) == XEEN_VOLLEY_ONE) {
            c->volley_end = 0x2845du;
            return VOLLEY_FINISHED;
        }
    }
    return VOLLEY_CONTINUE;
}

/* Whether the missiles fly on from tier TIER - 1 to TIER (1..3): not through a cell that blocks the line of fire
 * (tier_blocked: effect 2Eh and the volley ends), nor when no shooting row still holds a missile (DS 97F7, the
 * scan's "a missile slot in use": the volley ends). Otherwise one scene moves the missiles and LIST becomes TIER's
 * targets. Original: 281A7's tier step (1C1C1, FX 2Eh at 2849E; 97F7 at 284BD, the end 284C4). */
static volley_flow advance_tier(run *r, unsigned tier, uint8_t list[3])
{
    xeen_combat *c = r->c;
    xeen_movement *m = r->m;
    int blocked;
    if (!tier_blocked(r, tier - 1u, &blocked)) {
        return VOLLEY_UNSUPPORTED;
    }
    if (blocked) {
        if (!play_effect(r, 0x2eu)) {
            return VOLLEY_UNSUPPORTED;
        }
        c->volley_end = 0x2849eu;
        return VOLLEY_FINISHED;
    }
    if (!m->ds[0x97f7]) {
        c->volley_end = 0x284c4u;
        return VOLLEY_FINISHED;
    }
    if (!scene(r)) {
        return VOLLEY_UNSUPPORTED;
    }
    tier_targets(m, tier, list);
    return VOLLEY_CONTINUE;
}

/* The volley's flight down the party's column, tier 0 (LIST, the party's own cell) to tier 3: each tier's shots,
 * then the column volley's end at the first tier with a target (XEEN_VOLLEY_CELL), then the step to the next tier.
 * VOLLEY_DONE when the four tiers ran out (its end recorded unless a shot recorded one). */
static volley_flow volley_tiers(run *r, uint8_t list[3])
{
    xeen_movement *m = r->m;
    unsigned tier = 0;
    int attacked = 0;
    volley_flow flow;
    for (;;) {
        if ((flow = shoot_tier(r, list, &attacked)) != VOLLEY_CONTINUE) {
            return flow;
        }
        if (attacked && xeen_read16(m, XEEN_DS_VOLLEY_MODE) == XEEN_VOLLEY_CELL) {
            return VOLLEY_FINISHED;
        }
        if (++tier == 4u) {
            break;
        }
        if ((flow = advance_tier(r, tier, list)) != VOLLEY_CONTINUE) {
            return flow;
        }
    }
    if (!r->c->volley_end) {
        r->c->volley_end = 0x287e3u;
    }
    return VOLLEY_DONE;
}

/* The volley's unsupported exit: the run's boundary into *BOUNDARY and the fight's last boundary. */
static xeen_status volley_unsupported(run *r, uint32_t *boundary)
{
    *boundary = r->boundary;
    r->c->last_boundary = r->boundary;
    return XEEN_SERVICE_REQUIRED;
}

/* The volley's close-out: the shooting rows cleared and the target and hit slot restored to the ones before the
 * volley. A treasure the fight left is shown only when no monster is visible (xeen_treasure_check): its prefix
 * (treasure_prefix) runs here and the "The Party Found" dialog with its settlement belongs to the caller
 * (XEEN_TREASURE_CALL), which then resumes the volley's caller.
 * Original: 281A7's tail, 287FF 2EA68; the deferral 2EA98; the prefix 2EB9C; the settlement 2F1AA. */
static xeen_status volley_close(run *r, uint8_t saved_slot, uint8_t saved_target,
                                uint32_t *boundary)
{
    xeen_movement *m = r->m;
    memset(m->ds + XEEN_DS_MISSILE_SLOTS, 0, 8);
    m->ds[XEEN_DS_TARGET_MONSTER] = saved_target;
    m->ds[XEEN_DS_HIT_SLOT] = saved_slot;
    if (xeen_treasure_check(m) != XEEN_TREASURE_DIALOG) {
        return XEEN_OK;
    }
    if (!treasure_prefix(r)) {
        return volley_unsupported(r, boundary);
    }
    *boundary = XEEN_TREASURE_CALL;
    r->c->last_boundary = XEEN_TREASURE_CALL;
    return XEEN_SERVICE_REQUIRED;
}

/* The party's projectile volley with sprite ICON: the counters reset, the shooting rows (volley_rows), the sprite
 * (volley_projectile), the first tier's targets (the selected target first, its hit slot one before so that the
 * first hit lands in it), the rows' projectiles armed, one scene, the tiers (volley_tiers), the attack's end when the
 * tiers stopped early, and the close-out (volley_close). An unsupported step leaves the target and hit slot as they
 * stand. Original: 281A7(icon); the scene 1B503(1); the early end 39654. */
static xeen_status volley(run *r, xeen_sprite_cache *cache, const xeen_sprite_archives *archives,
                          unsigned icon, uint32_t *boundary)
{
    xeen_combat *c = r->c;
    xeen_movement *m = r->m;
    uint8_t list[3], saved_slot, saved_target;
    uint16_t handle;
    volley_flow flow;
    c->volley_hits = c->volley_misses = c->volley_saves = c->volley_targets = 0;
    c->volley_end = 0;
    if (!c->rng_draws) {
        c->rng_trace_complete = 1u;
    }
    flow = volley_rows(r, icon);
    if (flow == VOLLEY_NO_SHOOTER) {
        return XEEN_OK;
    }
    if (flow == VOLLEY_UNSUPPORTED || !volley_projectile(r, cache, archives, icon, &handle)) {
        return volley_unsupported(r, boundary);
    }
    saved_slot = m->ds[XEEN_DS_HIT_SLOT];
    saved_target = m->ds[XEEN_DS_TARGET_MONSTER];
    tier_targets(m, 0, list);
    m->ds[XEEN_DS_HIT_SLOT] = 0xffu;
    if (saved_target != XEEN_NO_TARGET) {
        m->ds[XEEN_DS_HIT_SLOT] = (uint8_t)(saved_slot - 1u);
        list[0] = saved_target;
    }
    arm_projectiles(m, handle);
    if (!scene(r)) {
        return volley_unsupported(r, boundary);
    }
    flow = volley_tiers(r, list);
    if (flow == VOLLEY_UNSUPPORTED) {
        return volley_unsupported(r, boundary);
    }
    if (flow == VOLLEY_FINISHED) {
        xeen_projectile_end_attack(m);
    }
    return volley_close(r, saved_slot, saved_target, boundary);
}
xeen_status xeen_combat_shoot(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                              xeen_sprite_cache *cache, const xeen_sprite_archives *archives,
                              xeen_entity_random_fn random, void *user, xeen_native_audio *audio,
                              uint32_t *boundary)
{
    run r;
    if (!c || !m || !db || !cache || !archives || !random || !boundary ||
        m->ds[XEEN_DS_PARTY_COUNT] > 8u || (m->ds[XEEN_DS_SOUND_ON] && !audio)) {
        return XEEN_BAD_INPUT;
    }
    *boundary = 0;
    r = (run){c, m, db, n, random, user, audio, 0};
    /* 2A2D4: physical volley over every tier (972C=2, 972E=0), pow 11. */
    xeen_write16(m, XEEN_DS_VOLLEY_MODE, XEEN_VOLLEY_COLUMN);
    xeen_write16(m, XEEN_DS_DAMAGE_TYPE, XEEN_ATTACK_PHYSICAL);
    return volley(&r, cache, archives, 11u, boundary);
}
xeen_status xeen_combat_spell_volley(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                                     xeen_sprite_cache *cache, const xeen_sprite_archives *archives,
                                     xeen_entity_random_fn random, void *user,
                                     xeen_native_audio *audio, unsigned icon, uint32_t *boundary)
{
    run r;
    if (!c || !m || !db || !cache || !archives || !random || !boundary || icon > 99u ||
        m->ds[XEEN_DS_PARTY_COUNT] > 8u || (m->ds[XEEN_DS_SOUND_ON] && !audio)) {
        return XEEN_BAD_INPUT;
    }
    *boundary = 0;
    r = (run){c, m, db, n, random, user, audio, 0};
    return volley(&r, cache, archives, icon, boundary);
}
xeen_status xeen_combat_scene(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                              uint32_t *boundary)
{
    run r;
    if (!c || !m || !db || !boundary) {
        return XEEN_BAD_INPUT;
    }
    r = (run){c, m, db, n, NULL, NULL, NULL, 0};
    *boundary = 0;
    if (scene(&r)) {
        return XEEN_OK;
    }
    *boundary = r.boundary;
    return XEEN_SERVICE_REQUIRED;
}

xeen_status xeen_combat_idle(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                             xeen_entity_random_fn random, void *user, xeen_native_audio *audio,
                             unsigned idle_count, uint32_t *boundary)
{
    run r;
    if (!c || !m || !db || !boundary || c->phase != XC_AWAITING_COMMAND) {
        return XEEN_BAD_INPUT;
    }
    r = (run){c, m, db, n, random, user, audio, 0};
    *boundary = 0;
    if (!scene(&r)) {
        *boundary = r.boundary;
        return XEEN_SERVICE_REQUIRED;
    }
    if (idle_count == 5u && m->ds[XEEN_DS_VIEW_MONSTER(0)] != XEEN_VIEW_NO_MONSTER) {
        unsigned id = m->ds[(uint16_t)(XEEN_MAP_MONSTER(m->ds[XEEN_DS_TARGET_MONSTER]) + XEEN_MAP_MONSTER_ID)];
        if ((size_t)id * XEEN_MONSTER_RECORD_BYTES + XEEN_MONSTER_ATTACK_VOICE > n) {
            *boundary = IDLE_VOICE_PAST_DATABASE;
            return XEEN_SERVICE_REQUIRED;
        }
        if (!play_effect(&r, db[(size_t)id * XEEN_MONSTER_RECORD_BYTES + XEEN_MONSTER_SOUND])) {
            *boundary = r.boundary;
            return XEEN_SERVICE_REQUIRED;
        }
    }
    return XEEN_OK;
}

/* ---------------------------------------------------- monster turn path */

/* 2AAC2: maximum HP. (class 18B8 + Endurance bonus + race 299A + (ch+2A
 * nonzero)), at least 1, times the level, plus the stat-7 item bonus added
 * as an unsigned word; negative totals become 0. Returns the low word. */
uint16_t xeen_combat_max_hp(const xeen_movement *m, const uint8_t *ch)
{
    int32_t base = (int32_t)m->ds[XEEN_DS_CLASS_BASE_HP + ch[XEEN_CHARACTER_CLASS]] +
                   xeen_combat_stat_bonus(m, xeen_combat_stat(m, ch, XEEN_STAT_ENDURANCE, 0)) +
                   (int8_t)m->ds[XEEN_DS_RACE_HP_BONUS + ch[XEEN_CHARACTER_RACE]] +
                   (ch[XEEN_CHARACTER_SKILLS + XEEN_SKILL_BODY_BUILDER] ? 1 : 0);
    if (base < 1) {
        base = 1;
    }
    base = (int32_t)((uint32_t)base * xeen_character_current_level(ch)) +
           (int32_t)(uint16_t)item_scan(m, ch, XEEN_ITEM_BONUS_HIT_POINTS);
    return base < 0 ? 0u : (uint16_t)base;
}

/* 46470: maximum SP, 0 without ch+A3. Sorcerers and archers use Intellect
 * and Prestidigitator (ch+34h), the others Personality and Prayer Master
 * (ch+33h; druids and rangers: Astrologer, ch+29h). (stat bonus + 3 + race
 * 298F[race*2+stat] + 2 with the skill), at least 1, times the level;
 * halved (SAR) except for clerics, sorcerers and druids. Druids and rangers
 * repeat it with Intellect and halve the sum of both. The stat-8 item bonus
 * is added as an unsigned word; negative becomes 0. */
uint16_t xeen_combat_max_sp(const xeen_movement *m, const uint8_t *ch)
{
    unsigned klass = ch[XEEN_CHARACTER_CLASS],
             stat = (klass == XEEN_CLASS_SORCERER || klass == XEEN_CLASS_ARCHER) ? (unsigned)XEEN_STAT_INTELLECT
                                                                                 : (unsigned)XEEN_STAT_PERSONALITY,
             skill = (klass == XEEN_CLASS_SORCERER || klass == XEEN_CLASS_ARCHER) ? (unsigned)XEEN_SKILL_PRESTIDIGITATOR
                                                                                  : (unsigned)XEEN_SKILL_PRAYER_MASTER;
    int32_t value, first = 0;
    int second = 0;
    if (!ch[XEEN_CHARACTER_CASTER]) {
        return 0;
    }
    if (klass == XEEN_CLASS_DRUID || klass == XEEN_CLASS_RANGER) {
        skill = XEEN_SKILL_ASTROLOGER;
    }
    for (;;) {
        value = (int16_t)(xeen_combat_stat_bonus(m, xeen_combat_stat(m, ch, stat, 0)) + 3);
        value += (int8_t)m->ds[(uint16_t)(XEEN_DS_RACE_SP_BONUS_BASE + ch[XEEN_CHARACTER_RACE] * 2u + stat)];
        if (ch[XEEN_CHARACTER_SKILLS + skill]) {
            value += 2;
        }
        if (value < 1) {
            value = 1;
        }
        value = (int32_t)((uint32_t)value * xeen_character_current_level(ch));
        if (klass != XEEN_CLASS_SORCERER && klass != XEEN_CLASS_CLERIC && klass != XEEN_CLASS_DRUID) {
            value /= 2;
        }
        if (second || (klass != XEEN_CLASS_DRUID && klass != XEEN_CLASS_RANGER)) {
            break;
        }
        first = value;
        stat = XEEN_STAT_INTELLECT;
        second = 1;
    }
    if (second) {
        value = (value + first) / 2;
    }
    value += (uint16_t)item_scan(m, ch, XEEN_ITEM_BONUS_SPELL_POINTS);
    return value < 0 ? 0u : (uint16_t)value;
}

/* Subtracts DAMAGE from the member's HP (a 16-bit word). Below 1 the member falls: dead (condition 13) when HP plus
 * the maximum HP is below 1, else unconscious (12, with its sound). At death or at -10 HP or below, every equipped
 * armor piece (a nonzero id) breaks (bit 80h). Original: 2A152 (its SI marks -10 or below); 2A17D; the sound 26h;
 * 2A1CB. */
xeen_status xeen_character_subtract_hp(xeen_movement *m, uint8_t *ch, uint16_t damage,
                                       xeen_native_audio *audio, int *downed)
{
    int16_t hp;
    int broken;
    unsigned i;
    if (downed) {
        *downed = 0;
    }
    if (!m || !ch) {
        return XEEN_BAD_INPUT;
    }
    XEEN_DAMAGE_NUMBERS(hp_ptr(m->ds, ch, -(int32_t)(int16_t)damage)); /* add-on hook */
    hp = (int16_t)(xeen_sign16(xeen_get_le16(ch + XEEN_CHARACTER_HP)) - (int16_t)damage);
    broken = hp <= -10;
    xeen_put_le16(ch + XEEN_CHARACTER_HP, (uint16_t)hp);
    if (hp >= 1) {
        return XEEN_OK;
    }
    if ((int16_t)(hp + (int16_t)xeen_combat_max_hp(m, ch)) < 1) {
        ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_DEAD)] = 1;
        broken = 1;
        if (hp > 0) {
            xeen_put_le16(ch + XEEN_CHARACTER_HP, 0);
        }
    } else {
        ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_UNCONSCIOUS)] = 1;
        if (xeen_native_audio_effect(audio, m, 0x26u) != XEEN_OK) {
            return XEEN_SERVICE_REQUIRED;
        }
    }
    if (broken) {
        for (i = 0; i < XEEN_ITEM_ROWS; ++i) {
            uint8_t *armor = ch + XEEN_CHARACTER_ITEM(XEEN_ITEMS_ARMOR, i);
            if (armor[XEEN_ITEM_ROW_ID] && armor[XEEN_ITEM_ROW_EQUIPPED]) {
                armor[XEEN_ITEM_ROW_STATE] |= XEEN_ITEM_BROKEN;
            }
        }
    }
    if (downed) {
        *downed = 1;
    }
    return XEEN_OK;
}
/* The shared 2A152 above (Bash's 3D6FD/3D75D call it too). */
static int subtract_hp(run *r, uint8_t *ch, uint16_t damage)
{
    int downed = 0;
    xeen_status status = xeen_character_subtract_hp(r->m, ch, damage, r->audio, &downed);
    if (status == XEEN_SERVICE_REQUIRED) {
        return fail(r, COMBAT_STOP_HIT_SOUND);
    }
    if (status != XEEN_OK) {
        return 0;
    }
    if (downed) {
        ++r->c->party_downed;
    }
    return 1;
}

/* The threshold of member CH's saving throw against attack TYPE, and in *HIGH the roll's upper bound. Type 0 (the
 * general save): the member's current level plus the stat bonus of twice their full Luck, rolled 1..threshold + 20.
 * Types 1..6 (an attack type: magic or an element): the member's base and temporary resistance bytes plus the
 * items' bonus against it, rolled 1..threshold + 40. A type above 6 is its own threshold (an original quirk: the
 * routine never loads its register then, and both callers pass the type in it). The member saves when the roll,
 * signed, is at most the threshold; the caller uses the bound's low byte as the roll's second argument.
 * Original: 37A02(ch, type): 2C909 the level, 2C92E the bonus of 2BD94 stat 6; 2BAFA the items' bonus, the 37B61
 * table; SI kept above 6 (the callers 37C9E); AL the pushed bound's low byte. */
uint16_t xeen_combat_save_threshold(const xeen_movement *m, const uint8_t *ch, unsigned type,
                                    uint16_t *high)
{
    /* by attack type - 1: the member's resistance pair and its item bonus kind (11 + the element's category) */
    static const uint16_t resist[6] = {
            [XEEN_ATTACK_MAGIC - 1] = XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_MAGIC),
            [XEEN_ATTACK_FIRE - 1] = XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_FIRE),
            [XEEN_ATTACK_ELECTRICITY - 1] = XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_ELECTRICITY),
            [XEEN_ATTACK_COLD - 1] = XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_COLD),
            [XEEN_ATTACK_POISON - 1] = XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_POISON),
            [XEEN_ATTACK_ENERGY - 1] = XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_ENERGY)};
    static const uint8_t stat[6] = {[XEEN_ATTACK_MAGIC - 1] = XEEN_ITEM_BONUS_RESISTANCES + 5u,
                                    [XEEN_ATTACK_FIRE - 1] = XEEN_ITEM_BONUS_RESISTANCES + 0u,
                                    [XEEN_ATTACK_ELECTRICITY - 1] = XEEN_ITEM_BONUS_RESISTANCES + 1u,
                                    [XEEN_ATTACK_COLD - 1] = XEEN_ITEM_BONUS_RESISTANCES + 2u,
                                    [XEEN_ATTACK_POISON - 1] = XEEN_ITEM_BONUS_RESISTANCES + 3u,
                                    [XEEN_ATTACK_ENERGY - 1] = XEEN_ITEM_BONUS_RESISTANCES + 4u};
    uint16_t threshold;
    if (!type) {
        uint16_t luck = xeen_combat_stat(m, ch, XEEN_STAT_LUCK, 0);
        threshold = (uint16_t)(xeen_character_current_level(ch) +
                               (uint16_t)xeen_combat_stat_bonus(m, (uint16_t)(luck * 2u)));
        *high = (uint16_t)(threshold + 20u);
    } else {
        threshold = type > XEEN_ATTACK_ENERGY ? (uint16_t)type
                              : (uint16_t)(ch[resist[type - 1u]] +
                                           (uint16_t)item_scan(m, ch, stat[type - 1u]) +
                                           ch[resist[type - 1u] + 1u]);
        *high = (uint16_t)(threshold + 40u);
    }
    return threshold;
}
/**
 * Rolls MEMBER's saving throw against attack TYPE (one draw of 1..HIGH from xeen_combat_save_threshold, the
 * original's signed comparison of the roll with the threshold) and counts a save. Returns 1 when the member saves.
 */
static int saving_throw(run *r, const uint8_t *ch, unsigned type)
{
    uint16_t high, threshold = xeen_combat_save_threshold(r->m, ch, type, &high), roll;
    roll = random_word(r, 1u, high, high & 255u);
    if (xeen_sign16(roll) > xeen_sign16(threshold)) {
        return 0;
    }
    ++r->c->party_saves;
    return 1;
}

/* A character's condition COUNT rises by one and saturates at FFh (INC, then FFh when the byte wrapped). */
static void raise_condition(uint8_t *count)
{
    if (!++*count) {
        *count = 0xffu;
    }
}
/* Marks every item the member carries with STATE (cursed or broken): the weapons in every row, empty rows included,
 * except a Xeen Slayer Sword, and every armor, accessory and misc row. UNEQUIP also unequips the marked weapons and
 * every armor and accessory row; a misc item keeps its equipped byte.
 * Original: 37FB1 (the curse, without unequipping) and 366AA (eradication's breakage). */
static void mark_carried_items(uint8_t *ch, uint8_t state, int unequip)
{
    static const uint8_t others[3] = {XEEN_ITEMS_ARMOR, XEEN_ITEMS_ACCESSORIES, XEEN_ITEMS_MISC};
    unsigned i, k;
    for (i = 0; i < XEEN_ITEM_ROWS; ++i) {
        uint8_t *weapon = ch + XEEN_CHARACTER_ITEM(XEEN_ITEMS_WEAPONS, i);
        if (weapon[XEEN_ITEM_ROW_ID] != XEEN_WEAPON_XEEN_SLAYER_SWORD) {
            weapon[XEEN_ITEM_ROW_STATE] |= state;
            if (unequip) {
                weapon[XEEN_ITEM_ROW_EQUIPPED] = 0;
            }
        }
        for (k = 0; k < sizeof others; ++k) {
            uint8_t *item = ch + XEEN_CHARACTER_ITEM(others[k], i);
            item[XEEN_ITEM_ROW_STATE] |= state;
            if (unequip && others[k] != XEEN_ITEMS_MISC) {
                item[XEEN_ITEM_ROW_EQUIPPED] = 0;
            }
        }
    }
}
/* The end of a fatal special (eradication, death, stone): it counts, sounds FX, and a member with HP left drops to
 * zero (negative HP stays). Original: the common tail of 37F06, 37F51 and 37F81. */
static int fatal_special(run *r, uint8_t *ch, uint8_t fx)
{
    ++r->c->specials_applied;
    if (!play_effect(r, fx)) {
        return 0;
    }
    if (xeen_sign16(xeen_get_le16(ch + XEEN_CHARACTER_HP)) > 0) {
        xeen_put_le16(ch + XEEN_CHARACTER_HP, 0);
    }
    return 1;
}
/* A monster's special attack (its database row's byte +1Eh) after a damaging hit the member did not save against.
 * A condition special raises its condition by one (raise_condition) with its sound; the others: the curse of every
 * carried item, the loss of all spell points, the breakage of the equipped weapons, one year of age (no saturation),
 * and the fatal three (eradication also breaks and unequips everything carried; fatal_special). Any other special
 * (1..4, 6, 11, 23 and above) has no effect and no sound.
 * Original: 37D6E; the table CS:19A9; the others fall through to 37FF5. */
static int special_attack(run *r, uint8_t *ch, uint8_t special)
{
    static const struct {
        uint8_t special;
        uint16_t offset;
        uint8_t fx;
    } raise[] = {
            {XEEN_SPECIAL_POISON, XEEN_CHARACTER_CONDITION(XEEN_CONDITION_POISONED), 0x1a},
            {XEEN_SPECIAL_DISEASE, XEEN_CHARACTER_CONDITION(XEEN_CONDITION_DISEASED), 0x1a},
            {XEEN_SPECIAL_INSANE, XEEN_CHARACTER_CONDITION(XEEN_CONDITION_INSANE), 0x1c},
            {XEEN_SPECIAL_ASLEEP, XEEN_CHARACTER_CONDITION(XEEN_CONDITION_ASLEEP), 0x24},
            {XEEN_SPECIAL_CURSE, XEEN_CHARACTER_CONDITION(XEEN_CONDITION_CURSED), 0x25},
            {XEEN_SPECIAL_PARALYSIS, XEEN_CHARACTER_CONDITION(XEEN_CONDITION_PARALYZED), 0x25},
            {XEEN_SPECIAL_UNCONSCIOUS, XEEN_CHARACTER_CONDITION(XEEN_CONDITION_UNCONSCIOUS), 0x25},
            {XEEN_SPECIAL_CONFUSE, XEEN_CHARACTER_CONDITION(XEEN_CONDITION_CONFUSED), 0x1c},
            {XEEN_SPECIAL_WEAK, XEEN_CHARACTER_CONDITION(XEEN_CONDITION_WEAK), 0x24},
    };
    unsigned i;
    for (i = 0; i < sizeof raise / sizeof raise[0]; ++i) {
        if (raise[i].special == special) {
            raise_condition(&ch[raise[i].offset]);
            ++r->c->specials_applied;
            return play_effect(r, raise[i].fx);
        }
    }
    switch (special) {
        case XEEN_SPECIAL_CURSE_ITEMS: /* Original: 37FB1 */
            mark_carried_items(ch, XEEN_ITEM_CURSED, 0);
            break;
        case XEEN_SPECIAL_DRAIN_SP: /* Original: 37EA1 */
            xeen_put_le16(ch + XEEN_CHARACTER_SP, 0);
            break;
        case XEEN_SPECIAL_BREAK_WEAPONS:
            /* Only equipped weapons in a row, and never the Xeen Slayer Sword. Original: 37E48 */
            for (i = 0; i < XEEN_ITEM_ROWS; ++i) {
                uint8_t *weapon = ch + XEEN_CHARACTER_ITEM(XEEN_ITEMS_WEAPONS, i);
                if (weapon[XEEN_ITEM_ROW_ID] != XEEN_WEAPON_XEEN_SLAYER_SWORD && weapon[XEEN_ITEM_ROW_ID] &&
                    weapon[XEEN_ITEM_ROW_EQUIPPED]) {
                    weapon[XEEN_ITEM_ROW_STATE] |= XEEN_ITEM_BROKEN;
                    weapon[XEEN_ITEM_ROW_EQUIPPED] = 0;
                }
            }
            break;
        case XEEN_SPECIAL_AGE: /* Original: 37ED2 */
            ++ch[XEEN_CHARACTER_AGE_BONUS];
            break;
        case XEEN_SPECIAL_ERADICATE: /* no RNG, no draw. Original: 37F06, 37F28 366AA */
            raise_condition(&ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_ERADICATED)]);
            mark_carried_items(ch, XEEN_ITEM_BROKEN, 1);
            return fatal_special(r, ch, 0x25u);
        case XEEN_SPECIAL_DEAD: /* Original: 37F81 */
            raise_condition(&ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_DEAD)]);
            return fatal_special(r, ch, 0x26u);
        case XEEN_SPECIAL_STONE: /* Original: 37F51 */
            raise_condition(&ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_STONE)]);
            return fatal_special(r, ch, 0x25u);
        default:
            return 1;
    }
    ++r->c->specials_applied;
    return play_effect(r, 0x25u);
}

static void party_bar(run *);
/* By damage type, as both the monsters' hits (37B6D: XEEN_ATTACK_* 0..6) and the party damage of 2E68D (types 0..7)
 * number them: the party's protection word against it (fire, electricity, cold, poison; 0 for none), the hit's sound
 * and its "charpow" frame over the portrait. Type 7 is only 2E68D's: the member dies (DAMAGE_TYPE_DEATH).
 * Original: 2E753's table CS:2F4D (2E758..2E7C1); 37C24's +1Dh row and 37CBF's frames for 1..6. */
enum { DAMAGE_TYPE_DEATH = 7u, DAMAGE_TYPES = 8u };
static const struct {
    uint16_t protection;
    uint8_t sound, frame;
} damage_types[DAMAGE_TYPES] = {
        [XEEN_ATTACK_PHYSICAL] = {0, 0x1d, 0},
        [XEEN_ATTACK_MAGIC] = {0, 0x1b, 6},
        [XEEN_ATTACK_FIRE] = {XEEN_DS_PROTECTION_FIRE, 0x16, 1},
        [XEEN_ATTACK_ELECTRICITY] = {XEEN_DS_PROTECTION_ELECTRICITY, 0x17, 2},
        [XEEN_ATTACK_COLD] = {XEEN_DS_PROTECTION_COLD, 0x18, 3},
        [XEEN_ATTACK_POISON] = {XEEN_DS_PROTECTION_POISON, 0x1a, 4},
        [XEEN_ATTACK_ENERGY] = {0, 0x19, 5},
        [DAMAGE_TYPE_DEATH] = {0, 0x26, 0},
};
/* A hit's frame FRAME of the "charpow" sprites over the portrait of party slot SLOT at y 150, and the party bar
 * shown. Original: 4AC92 of 98CE at (17B2[slot], 96h), 50BF6(21h). */
static void portrait_hit(xeen_presentation *p, const xeen_movement *m, unsigned slot, uint16_t frame)
{
    xeen_sprite_draw d = {xeen_read16(m, XEEN_DS_CHARPOW_SPRITES), frame, m->ds[(uint16_t)(XEEN_DS_PORTRAIT_X + slot)],
                          150, 0, 0, 0};
    (void)xeen_presentation_draw_sprites(p, &d, 1);
    (void)xeen_presentation_update_window(p, XEEN_WINDOW_PARTY_BAR);
}
/* The damage of a monster hit on member CH by the monster of database ROW, into *DAMAGE (16-bit, signed) with its
 * sound *FX: the sum of the dice; for a typed attack a general saving throw halves it, the party's protection word
 * against the type (fire, electricity, cold, poison) is taken off, and each successful save against the type halves
 * it again while any is left. An untyped attack keeps effect 1Dh. */
static void monster_hit_damage(run *r, const uint8_t *ch, const uint8_t *row, int16_t *damage, uint8_t *fx)
{
    unsigned i, type = row[XEEN_MONSTER_ATTACK_TYPE];
    *damage = 0;
    *fx = damage_types[XEEN_ATTACK_PHYSICAL].sound;
    for (i = 0; i < xeen_get_le16(row + XEEN_MONSTER_DICE); ++i) {
        *damage = (int16_t)(*damage + random_range(r, 1u, row[XEEN_MONSTER_DIE], row[XEEN_MONSTER_DIE]));
    }
    if (!type) {
        return;
    }
    if (saving_throw(r, ch, 0u)) {
        *damage = (int16_t)(*damage >> 1);
    }
    if (type <= XEEN_ATTACK_ENERGY) { /* a type past energy keeps the physical sound and no protection */
        *fx = damage_types[type].sound;
        if (damage_types[type].protection) { /* the word signed here (2E68D takes it unsigned) */
            *damage = (int16_t)(*damage - xeen_sign16(xeen_read16(r->m, damage_types[type].protection)));
        }
    }
    do {
        if (!saving_throw(r, ch, type)) {
            break;
        }
        *damage = (int16_t)(*damage >> 1);
    } while (*damage);
}
/**
 * One monster hit on member CH (party slot SLOT) by the monster of database ROW. The member wakes; the damage is the
 * sum of the monster's dice (16-bit). A nonzero attack type then allows a general saving throw (half damage), takes
 * the party's protection against fire, electricity, cold or poison off, and halves the damage again for each
 * successful save against that type while any damage is left. The type's sound and hit frame (portrait_hit), Power
 * Shield's reduction (at least 0), then the monster's special when the hit did damage and the member fails a general
 * save; the HP drop, one scene and the party bar.
 * Original: 37B6D (the asleep byte +14Bh, the dice in DI; the type row +1Dh at 37C24; the sound 37CB6, the frame
 * 37CBF; Power Shield EDA4; 2A152; 30E2D(2); 2ABB8).
 */
static int party_damage(run *r, uint8_t *ch, const uint8_t *row, uint16_t slot)
{
    xeen_movement *m = r->m;
    unsigned type = row[XEEN_MONSTER_ATTACK_TYPE];
    int16_t damage;
    uint8_t fx;
    ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_ASLEEP)] = 0;
    if (!row[XEEN_MONSTER_DIE] && xeen_get_le16(row + XEEN_MONSTER_DICE)) {
        return fail(r, PARTY_DAMAGE_STOP_DICE);
    }
    monster_hit_damage(r, ch, row, &damage, &fx);
    if (!play_effect(r, fx)) {
        return 0;
    }
    if (r->c->presentation) { /* the type's frame, 0 for a type past energy */
        portrait_hit(r->c->presentation, m, slot, type <= XEEN_ATTACK_ENERGY ? damage_types[type].frame : 0u);
    }
    damage = (int16_t)(damage - m->ds[XEEN_DS_EFFECT_POWER_SHIELD]);
    if (damage < 1) {
        damage = 0;
    }
    if (damage && row[XEEN_MONSTER_SPECIAL]) {
        if (!saving_throw(r, ch, 0u) && !special_attack(r, ch, row[XEEN_MONSTER_SPECIAL])) {
            return 0;
        }
    }
    if (!subtract_hp(r, ch, (uint16_t)damage)) {
        return 0;
    }
    r->c->last_damage = (uint16_t)damage;
    /* 30E2D(2): the timed wait redraws the scene until two ticks elapse;
     * the deterministic native policy is one completed frame. */
    scene_site = 0x30e3cu;
    if (!scene(r)) {
        return 0;
    }
    party_bar(r); /* 3800E 2ABB8(1) */
    return 1;
}

/** Picks a random member of the PARTY (by its count only; retarget_member handles a member who cannot be hit) as a
 * monster's target into *TARGET. */
static int pick_random_target(run *r, unsigned party, uint16_t *target)
{
    unsigned first;
    if (party == 1u) {
        *target = 0;
        return 1;
    }
    if (party >= 2u && party <= 5u) {
        *target = random_range(r, 0u, party - 1u, party - 1u);
        return 1;
    }
    if (party == 6u) {
        first = random_range(r, 1u, 6u, 5u);
        *target = first == 6u ? 5u : random_range(r, 0u, 4u, first);
        return 1;
    }
    return fail(r, TARGET_STOP_RANDOM);
}

/*
 * The monster that acts this turn: its database row into *ROW and its id into *ID; *IDLE when its map record's
 * condition keeps it from acting (the turn ends with no attack). A melee turn selects the actor from the turn order;
 * a ranged turn (RANGED_ID) names it. The turn's actor (XEEN_DS_TURN_ACTOR) minus the party's count is the view slot
 * 0..2 of the acting monster.
 *
 * An original quirk is kept: when the turn order's entry is FFh (no actor), the slot is negative and matches none of
 * 0..2, so nothing selects the actor and the caller's loop counter (CALLER_DI) is used as the map monster's record:
 * that record takes the turn (its condition test, the writes to its record, its voice and its attacks). A slot
 * 0..2 whose view entry is empty would use record FFh, past the native map monsters (in DOS, far pointers at F91C..
 * the native does not model): it stays an explicit stop. Returns 0 at a stop (or a failed scene), else 1.
 * Original: 38049 (melee), 38058 (ranged: the DB argument); CALLER_DI is 38633's DI at the 38769 call; the switch
 * 3805B..38077 on SI = (signed)97FF - 971F for 0..2 (380A4 below the three DI loads), no 10BA/1072/1096 write
 * otherwise; the record's +7 test and +2/+10/+9 writes; the stop 3805B.
 */
static int monster_actor(run *r, unsigned ranged_id, unsigned caller_di, const uint8_t **row,
                         uint8_t *id, int *idle)
{
    xeen_movement *m = r->m;
    unsigned party = m->ds[XEEN_DS_COMBAT_PARTY_COUNT], slot;
    uint8_t record_index;
    uint16_t record;
    *idle = 0;
    if (m->ds[XEEN_DS_MONSTER_SHOTS_QUEUED]) {
        if (ranged_id > 255u || (size_t)ranged_id * XEEN_MONSTER_RECORD_BYTES + XEEN_MONSTER_RECORD_BYTES > r->n) {
            return fail(r, MONSTER_ACTOR_STOP_RANGED_ID);
        }
        *id = (uint8_t)ranged_id;
        *row = ROW(r->db, *id);
        return 1;
    }
    slot = (unsigned)((int)(int8_t)m->ds[XEEN_DS_TURN_ACTOR] - (int)party);
    if (slot <= 2u) {
        if ((record_index = m->ds[XEEN_DS_VIEW_MONSTERS + slot]) == XEEN_VIEW_NO_MONSTER) {
            return fail(r, MONSTER_ACTOR_STOP_SLOT);
        }
        /* its record's scale in the cell ahead: 0 */
        xeen_write16(m, (uint16_t)(XEEN_DS_PARTY_CELL_MONSTER_RECORDS(slot) + XEEN_VIEW_RECORD_SCALE), 0);
    } else {
        /* DI is below 38633's limit 971F + live (at most 11): an in-table
         * record index; MONSTER_RECORD() stays inside the 64 KiB DS either way. */
        record_index = (uint8_t)caller_di;
    }
    record = MONSTER_RECORD(record_index);
    if (m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_CONDITION)]) {
        *idle = 1;
        return 1;
    }
    if (!(*row = monster_row(r, record_index))) {
        return fail(r, MONSTER_ACTOR_STOP_RECORD);
    }
    m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_FRAME)] = 8u;
    m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_FRAME_HOLD)] = 3u;
    m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_BOUNCE_BACK)] = 0;
    if (!scene(r) || !scene(r)) {
        return 0;
    }
    { /* 38122: "%s.voc" of the record's voice name (the native reads it within the record) */
        char voice[XEEN_MONSTER_RECORD_BYTES - XEEN_MONSTER_ATTACK_VOICE + 1u];
        size_t i;
        for (i = 0; i + 1u < sizeof voice && (*row)[XEEN_MONSTER_ATTACK_VOICE + i]; ++i) {
            voice[i] = (char)(*row)[XEEN_MONSTER_ATTACK_VOICE + i];
        }
        voice[i] = 0;
        (void)snprintf(sample_name(m), SAMPLE_NAME_BYTES, (const char *)m->ds + XEEN_TEXT_MONSTER_VOICE_FORMAT, voice);
    }
    xeen_native_audio_voice_name(r->audio, m);            /* Sink: 38142 187E9 */
    *id = m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_ID)];
    return 1;
}
/* One attack's first target by the row's hate class (+19h): 15 attacks every member from member 0 (*ATTACK_ALL
 * set); a class 0..9 the first able member of that class, 12 the first able member of +11h 2; else (and when no
 * member matches) a random target (pick_random_target). Returns 0 at a failed draw, 2 when an attack on every
 * member meets an empty party (the attack is skipped), else 1. */
static int attack_target(run *r, const uint8_t *row, unsigned party, uint16_t *target,
                         int *attack_all)
{
    xeen_movement *m = r->m;
    *target = 0xffffu;
    if (row[XEEN_MONSTER_HATES] == XEEN_MONSTER_HATES_EVERYONE) {
        *attack_all = 1;
        *target = 0;
        if (party <= *target) {
            return 2;
        }
    } else if (row[XEEN_MONSTER_HATES] != 1u) {
        unsigned i;
        for (i = 0; i < party && *target == 0xffffu; ++i) {
            const uint8_t *member = MEMBER(m, i);
            if (incapacitated(xeen_character_worst_condition(member))) {
                continue;
            }
            if ((row[XEEN_MONSTER_HATES] <= XEEN_CLASS_RANGER &&
                 member[XEEN_CHARACTER_CLASS] == row[XEEN_MONSTER_HATES]) ||
                (row[XEEN_MONSTER_HATES] == XEEN_MONSTER_HATES_DWARVES &&
                 member[XEEN_CHARACTER_RACE] == XEEN_RACE_DWARF)) {
                *target = (uint16_t)i;
            }
        }
    }
    if (*target == 0xffffu && !pick_random_target(r, party, target)) {
        return 0;
    }
    return 1;
}
/* A single-target attack on an incapacitated member draws again among the able members (504DA over their
 * 9720 entries) into *TARGET and returns 1; with no able member the party is defeated (9A1C back to
 * exploration) and it returns 0. */
static int retarget_member(run *r, unsigned party, uint16_t *target)
{
    xeen_movement *m = r->m;
    uint8_t eligible[8];
    unsigned i, found = 0;
    uint8_t chosen;
    for (i = 0; i < party; ++i) {
        if (!incapacitated(xeen_character_worst_condition(MEMBER(m, i)))) {
            eligible[found++] = m->ds[XEEN_DS_COMBAT_PARTY + i];
        }
    }
    if (!found) {
        m->ds[XEEN_DS_PARTY_DEFEATED] = 1;
        m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_EXPLORATION;
        return 0;
    }
    chosen = eligible[random_range(r, 0u, found - 1u, found - 1u)];
    for (i = 0; i < party; ++i) {
        if (m->ds[XEEN_DS_COMBAT_PARTY + i] == chosen) {
            *target = (uint16_t)i;
            break;
        }
    }
    return 1;
}
/* Attack ATTACK of monster ID (row ROW) on member TARGET: an asleep member or a row with +1Dh is hit at once;
 * else 504DA(1, 20): a 20 hits (and goes on to the score), a 1 misses (effect 6), otherwise the score (roll +
 * +1Fh/4 + 504DA(1, +1Fh)) against the defense (armor class + 15 + level/2 when blocking, else + 10). Sets *HIT.
 * Returns 0 at a stop or a failed effect or damage, else 1. */
static int attack_member(run *r, const uint8_t *row, uint8_t id, unsigned attack, uint16_t target,
                         int *hit)
{
    xeen_movement *m = r->m;
    uint8_t *ch = MEMBER(m, target);
    uint8_t roll;
    if (row[XEEN_MONSTER_ATTACK_TYPE] || ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_ASLEEP)]) {
        if (!party_damage(r, ch, row, target)) {
            return 0;
        }
        *hit = 1;
        return 1;
    }
    /* AL is the normalized offset of the 60-byte row returned by 083B. */
    roll = random_range(r, 1u, 20u, ((unsigned)id * XEEN_MONSTER_RECORD_BYTES) & 15u);
    if (roll == 20u) {
        if (!party_damage(r, ch, row, target)) {
            return 0;
        }
        *hit = 1;
    }
    if (roll == 1u) {
        return play_effect(r, 6u) ? 1 : 0;
    }
    {
        uint16_t score, defense;
        if (!row[XEEN_MONSTER_TO_HIT]) {
            return fail(r, MEMBER_ATTACK_STOP_TO_HIT);
        }
        score = (uint16_t)(roll + (row[XEEN_MONSTER_TO_HIT] >> 2) +
                           random_range(r, 1u, row[XEEN_MONSTER_TO_HIT], row[XEEN_MONSTER_TO_HIT]));
        defense = (uint16_t)(xeen_combat_armor_class(m, ch, 0) +
                             (m->ds[XEEN_DS_COMBAT_BLOCKING + target]
                                      ? xeen_character_current_level(ch) / 2u + 15u
                                      : 10u));
        if (attack < 2u) {
            r->c->monster_attack_scores[attack] = score;
            r->c->monster_defenses[attack] = defense;
        }
        if (defense <= score) {
            if (!party_damage(r, ch, row, target)) {
                return 0;
            }
            *hit = 1;
            return 1;
        }
        return play_effect(r, 6u) ? 1 : 0;
    }
}
/* One attack of monster ID (row ROW) on member TARGET, the ATTACK-th of its turn: the trace of the target, the
 * attack itself (attack_member, which only ever sets *HIT) and, for the first two attacks, the evidence (target,
 * hit, damage); a miss shows the damage-numbers add-on's miss. *HIT belongs to the attack, not the member: an attack
 * on every member that hit one counts as a hit on the members after it (as the original's flag does). Returns 0 at a
 * stop. */
static int strike_member(run *r, const uint8_t *row, uint8_t id, unsigned attack, int attack_all, uint16_t target,
                         int *hit)
{
    xeen_combat *c = r->c;
    /* evidence (XEEN_DRAW_TRACE_EXTRA only): 38419's target. */
    xeen_presentation_trace_extra("ai-target monster %u attack %u all %u target %u hate %u", id, attack, attack_all,
                                  target, row[XEEN_MONSTER_HATES]);
    if (attack < 2u) {
        c->monster_targets[attack] = (uint8_t)target;
    }
    if (!attack_member(r, row, id, attack, target, hit)) {
        return 0;
    }
    if (!*hit) {
        XEEN_DAMAGE_NUMBERS(party_miss(r->m->ds[XEEN_DS_COMBAT_PARTY + target])); /* add-on hook */
    }
    if (attack < 2u) {
        c->monster_hits[attack] = (uint8_t)*hit;
        c->monster_damage[attack] = *hit ? c->last_damage : 0u;
    }
    return 1;
}
/* The acting monster's turn: the monster that acts (monster_actor), then each of its attacks: the first target
 * (attack_target; an attack without one is skipped), then every member it attacks (from the first to the last for
 * an attack on all, else the one target). A single-target attack on a member who cannot fight turns to a random able
 * member (retarget_member); when none is left the party is defeated and the turn ends at once, without the party
 * bar's redraw. After the attacks the party bar is redrawn.
 * Original: 38049..38590 (the row's +18h attacks; the check 38419; 2ABB8(1)). */
static int monster_turn(run *r, unsigned ranged_id, unsigned caller_di)
{
    xeen_movement *m = r->m;
    unsigned party = m->ds[XEEN_DS_COMBAT_PARTY_COUNT], attack, count;
    uint8_t id;
    const uint8_t *row;
    int attack_all = 0, idle;
    if (!monster_actor(r, ranged_id, caller_di, &row, &id, &idle)) {
        return 0;
    }
    if (idle) {
        return 1;
    }
    ++r->c->monster_actions;
    r->c->monster_id = id;
    count = row[XEEN_MONSTER_ATTACKS];
    for (attack = 0; attack < count; ++attack) {
        uint16_t target;
        int hit = 0; /* the attack's, carried from member to member (strike_member) */
        int found = attack_target(r, row, party, &target, &attack_all);
        if (!found) {
            return 0;
        }
        if (found == 2) {
            continue;
        }
        do {
            if (!attack_all && incapacitated(xeen_character_worst_condition(MEMBER(m, target))) &&
                !retarget_member(r, party, &target)) {
                return 1; /* the party is defeated */
            }
            if (target >= party) {
                return fail(r, MONSTER_TURN_STOP_TARGET);
            }
            if (!strike_member(r, row, id, attack, attack_all, target, &hit)) {
                return 0;
            }
        } while (attack_all && ++target < party);
    }
    party_bar(r); /* 38590 2ABB8(1) after the attacks */
    return 1;
}

/* ------------------------------------------------------- control flow */

/* The global sprites' frames under the party bar's portraits (y 149): the highlight, and the plain frame of party
 * slot k at PORTRAIT_PLAIN_FRAME + k. */
enum { PORTRAIT_HIGHLIGHT_FRAME = 8, PORTRAIT_PLAIN_FRAME = 9, PORTRAIT_FRAME_Y = 0x95 };
/* The draw of global sprite frame FRAME under the portrait of party slot SLOT (a signed byte, as the original's
 * cbw): x one left of the portrait's (XEEN_DS_PORTRAIT_X), y PORTRAIT_FRAME_Y. Original: 2A1F6's and 42298's
 * 4AC92(F458, frame, 17B2[slot] - 1, 95h). */
static xeen_sprite_draw portrait_frame(const xeen_movement *m, uint16_t sprites, int frame, int8_t slot)
{
    return (xeen_sprite_draw){sprites,
                              (uint16_t)frame,
                              (uint16_t)(m->ds[(uint16_t)(XEEN_DS_PORTRAIT_X + (unsigned)(int)slot)] - 1u),
                              PORTRAIT_FRAME_Y,
                              0,
                              0,
                              0};
}
/* The portrait highlight moves to member ACTOR: nothing when ACTOR already has it or the byte is FEh (a value
 * whose writer and role are not established); otherwise the old portrait's plain frame is drawn back unless none
 * was highlighted, the highlight frame goes under ACTOR's portrait, XEEN_DS_HIGHLIGHTED_PORTRAIT becomes ACTOR and
 * the party bar is shown. Without a presentation only the byte changes. The combat callers pass the actor byte
 * sign-extended (cbw 97FF at 38794, 39088, 392DE, 39344), so the compare and the index are the bytes'.
 * Original: 2A1F6(actor): the F458 sprites at (17B2[slot] - 1, 95h), 96C3, 50BF6(21h). */
static void highlight(run *r, uint8_t actor)
{
    xeen_movement *m = r->m;
    xeen_presentation *p = r->c->presentation;
    xeen_sprite_draw d[2];
    size_t n = 0;
    uint8_t old = m->ds[XEEN_DS_HIGHLIGHTED_PORTRAIT];
    if (old == actor || old == 0xfeu) {
        return;
    }
    if (p) {
        uint16_t h = xeen_read16(m, XEEN_DS_GLOBAL_SPRITES);
        if (old != XEEN_NO_PORTRAIT) {
            d[n++] = portrait_frame(m, h, (int)(int8_t)old + PORTRAIT_PLAIN_FRAME, (int8_t)old);
        }
        d[n++] = portrait_frame(m, h, PORTRAIT_HIGHLIGHT_FRAME, (int8_t)actor);
        (void)xeen_presentation_draw_sprites(p, d, n);
    }
    m->ds[XEEN_DS_HIGHLIGHTED_PORTRAIT] = actor;
    if (p) {
        (void)xeen_presentation_update_window(p, XEEN_WINDOW_PARTY_BAR);
    }
}
/* The portrait highlight removed: when a portrait is highlighted (the byte non-negative, signed), its plain frame
 * is drawn back under it, XEEN_DS_HIGHLIGHTED_PORTRAIT becomes FFh and the party bar is shown.
 * Original: 42298 (96C3 > -1; F458 at (17B2[96C3] - 1, 95h); 50BF6(21h)). */
static void unhighlight(run *r)
{
    xeen_movement *m = r->m;
    xeen_presentation *p = r->c->presentation;
    int8_t old = (int8_t)m->ds[XEEN_DS_HIGHLIGHTED_PORTRAIT];
    if (old <= -1) {
        return;
    }
    if (p) {
        xeen_sprite_draw d = portrait_frame(m, xeen_read16(m, XEEN_DS_GLOBAL_SPRITES), old + PORTRAIT_PLAIN_FRAME, old);
        (void)xeen_presentation_draw_sprites(p, &d, 1);
    }
    m->ds[XEEN_DS_HIGHLIGHTED_PORTRAIT] = XEEN_NO_PORTRAIT;
    if (p) {
        (void)xeen_presentation_update_window(p, XEEN_WINDOW_PARTY_BAR);
    }
}
/* Opens (OPEN) or closes window N through the owner's window service when it has one (the session's, which also
 * draws and restores the pixels under it), else only in the DS's window table. Original: 1871B / 18765. */
static void combat_window(run *r, unsigned n, int open)
{
    const xeen_combat_windows *w = &r->c->windows;
    if (w->window) {
        w->window(w->user, n, open);
    } else if (open) {
        xeen_window_open(r->m, n);
    } else {
        xeen_window_close(r->m, n);
    }
}
/* The party bar drawn again and shown (nothing without a presentation). Original: 2ABB8(1), 50BF6(21h). */
static void party_bar(run *r)
{
    if (r->c->presentation) {
        (void)xeen_world_entry_party_bar(r->m, r->c->presentation, 1);
    }
}
void xeen_combat_buttons(xeen_movement *m, int combat)
{
    static const uint16_t live[5] = {XEEN_DS_BUTTON_X_TABLE, XEEN_DS_BUTTON_Y_TABLE,
                                     XEEN_DS_BUTTON_WIDTH_TABLE, XEEN_DS_BUTTON_HEIGHT_TABLE,
                                     XEEN_DS_BUTTON_ENABLED_TABLE},
                          table[5] = {XEEN_DS_EXPLORATION_BUTTON_X, XEEN_DS_EXPLORATION_BUTTON_Y,
                                      XEEN_DS_EXPLORATION_BUTTON_WIDTH, XEEN_DS_EXPLORATION_BUTTON_HEIGHT,
                                      XEEN_DS_EXPLORATION_BUTTON_ENABLED};
    unsigned i;
    if (!m) {
        return;
    }
    for (i = 0; i < 5u; ++i) {
        xeen_write16(m, live[i], table[i]);
        xeen_write16(m, (uint16_t)(live[i] + 2u), XEEN_DS_SEGMENT);
    }
    xeen_write16(m, XEEN_DS_BUTTON_KEY_TABLE, combat ? XEEN_DS_COMBAT_BUTTON_KEYS : XEEN_DS_EXPLORATION_BUTTON_KEYS);
    xeen_write16(m, XEEN_DS_BUTTON_KEY_TABLE + 2u, XEEN_DS_SEGMENT);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
}
void xeen_combat_main_icons(const xeen_combat *c, xeen_movement *m, xeen_presentation *p)
{
    xeen_draw_list list;
    if (!c || !m) {
        return;
    }
    if (c->windows.window) {
        c->windows.window(c->windows.user, 0x26, 0);
        c->windows.window(c->windows.user, 0xc, 0);
    } else {
        xeen_window_close(m, XEEN_WINDOW_EVENT_TEXT);
        xeen_window_close(m, XEEN_WINDOW_BOTTOM);
    }
    if (!p) {
        return;
    }
    if (xeen_scene_collect_draw_list(m, 0, XEEN_DS_COMMAND_ICONS, &list) == XEEN_OK) {
        (void)xeen_presentation_submit(p, &list);
    }
    (void)xeen_presentation_update_window(p, XEEN_WINDOW_ICONS);
}

/* Moves the turn on to the next actor who can act, at most LIMIT steps through the turn order: the actor whose turn
 * it was is marked as having taken it; the index advances (an 8-bit increment, sign-extended so that 7Fh becomes
 * -128, then the signed remainder by LIMIT); a member who cannot act is passed over; the walk stops at once when
 * every combatant has had a turn (all_gone). Returns the steps taken before the stop, LIMIT when every one of them
 * passed over a member. Original: 38633's inner loop. */
static unsigned advance_turn(xeen_movement *m, unsigned limit)
{
    unsigned step;
    for (step = 0; step < limit; ++step) {
        if (m->ds[XEEN_DS_TURN_ACTOR] != XEEN_NO_TURN) {
            m->ds[(uint16_t)(XEEN_DS_TURN_TAKEN + m->ds[XEEN_DS_TURN_ACTOR])] = 1;
        }
        m->ds[XEEN_DS_TURN_INDEX] = (uint8_t)((int)(int8_t)(uint8_t)(m->ds[XEEN_DS_TURN_INDEX] + 1u) % (int)limit);
        m->ds[XEEN_DS_TURN_ACTOR] = m->ds[XEEN_DS_TURN_ORDER + m->ds[XEEN_DS_TURN_INDEX]];
        if (all_gone(m)) {
            break;
        }
        if (m->ds[XEEN_DS_TURN_ACTOR] < m->ds[XEEN_DS_COMBAT_PARTY_COUNT] &&
            xeen_condition_cannot_act(xeen_character_worst_condition(MEMBER(m, m->ds[XEEN_DS_TURN_ACTOR])))) {
            continue;
        }
        break;
    }
    return step;
}

/* The next actor of the round. With every monster gone or no party left, combat returns to exploration; otherwise
 * the party is checked (a defeated party ends here), the turn moves on (advance_turn). When every combatant has had
 * a turn, a new round starts if some member can act: the turn order is rebuilt and the turns cleared. A monster's
 * turn is run at once (then the party's check again, and on to the next actor); a member's turn is highlighted and
 * left to the command poll. The count of live attackers is taken once, on entry. Original: 38633 (the count in SI). */
static int next_actor(run *r)
{
    xeen_movement *m = r->m;
    unsigned live = attacker_count(m), limit, step;
    if (all_gone(m)) {
        return 1;
    }
    if (live == 0u || !m->ds[XEEN_DS_COMBAT_PARTY_COUNT]) {
        m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_EXPLORATION;
        return 1;
    }
    limit = m->ds[XEEN_DS_COMBAT_PARTY_COUNT] + live;
    for (;;) {
        party_check(m);
        if (m->ds[XEEN_DS_PARTY_DEFEATED]) {
            m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_EXPLORATION;
            return 1;
        }
        step = advance_turn(m, limit);
        if (step < limit && all_gone(m)) {
            if (!party_cannot_act(m)) {
                return 1;
            }
            if (!build_order(r)) {
                return 0;
            }
            m->ds[XEEN_DS_TURN_ACTOR] = m->ds[XEEN_DS_TURN_INDEX] = XEEN_NO_TURN;
            memset(m->ds + XEEN_DS_TURN_TAKEN, 0, XEEN_COMBATANT_BYTES);
            continue;
        }
        if (m->ds[XEEN_DS_TURN_ACTOR] >= m->ds[XEEN_DS_COMBAT_PARTY_COUNT]) {
            if (!monster_turn(r, 0u, step)) {
                return 0;
            }
            ++r->c->actions;
            if (!m->ds[XEEN_DS_PARTY_DEFEATED]) {
                party_check(m);
            }
            if (m->ds[XEEN_DS_PARTY_DEFEATED]) {
                return 1;
            }
            continue;
        }
        if (!all_gone(m)) {
            highlight(r, m->ds[XEEN_DS_TURN_ACTOR]);
        }
        return 1;
    }
}

/* 2125F, the party to the map's run position (C0FB, C104); the
 * facing is kept. */
static void run_position(xeen_movement *m)
{
    m->ds[XEEN_DS_PARTY_X] = m->ds[XEEN_DS_MAP_FIELD(RUN_X)];
    m->ds[XEEN_DS_PARTY_Y] = m->ds[XEEN_DS_MAP_FIELD(RUN_Y)];
}
/* The combat party closed up after a member left it (Run): each empty one of the eight combat slots takes the next
 * listed member after it with that member's turn-taken flag (cleared where it was); then the party bar.
 * Original: 379B8 (9720, 9707), 2ABB8(1). */
static void compact_party(run *r)
{
    uint8_t *ds = r->m->ds;
    unsigned to, from;
    for (to = 0; to < 8u; ++to) {
        if (ds[XEEN_DS_COMBAT_PARTY + to] != XEEN_COMBAT_PARTY_EMPTY) {
            continue;
        }
        for (from = to + 1u; from < 8u; ++from) {
            if (ds[XEEN_DS_COMBAT_PARTY + from] == XEEN_COMBAT_PARTY_EMPTY) {
                continue;
            }
            ds[XEEN_DS_COMBAT_PARTY + to] = ds[XEEN_DS_COMBAT_PARTY + from];
            ds[XEEN_DS_TURN_TAKEN + to] = ds[XEEN_DS_TURN_TAKEN + from];
            ds[XEEN_DS_TURN_TAKEN + from] = 0;
            ds[XEEN_DS_COMBAT_PARTY + from] = XEEN_COMBAT_PARTY_EMPTY;
            break;
        }
    }
    party_bar(r);
}
/* Run: a roll of 1..100 below the map's run chance takes the actor alone out of the fight: their combat slot
 * emptied, the combat party one smaller and compacted, the turn order rebuilt and moved back one, nobody's turn, the
 * "ran" flag, the sound. A failed roll changes nothing; the turn is spent either way (the next actor follows). AL is
 * the caller's (the random draw receives it). Original: 38B00: 504DA(1, 100) against C103; 9720[97FF] = FFh, 971F
 * - 1, 379B8, 38D25, 97FE - 1, 97FF = FFh, FAFE = 1, 1880E(33h); then 38633. */
static int party_run(run *r, unsigned al)
{
    xeen_movement *m = r->m;
    ++r->c->runs;
    if ((int16_t)random_word(r, 1u, 100u, al) >= (int16_t)m->ds[XEEN_DS_MAP_FIELD(RUN_CHANCE)]) {
        return 1;
    }
    m->ds[(uint16_t)(XEEN_DS_COMBAT_PARTY + (unsigned)(int)(int8_t)m->ds[XEEN_DS_TURN_ACTOR])] =
            0xffu;
    m->ds[XEEN_DS_COMBAT_PARTY_COUNT] = (uint8_t)(m->ds[XEEN_DS_COMBAT_PARTY_COUNT] - 1u);
    compact_party(r);
    if (!build_order(r)) {
        return 0;
    }
    m->ds[XEEN_DS_TURN_INDEX] = (uint8_t)(m->ds[XEEN_DS_TURN_INDEX] - 1u);
    m->ds[XEEN_DS_TURN_ACTOR] = XEEN_NO_TURN;
    m->ds[XEEN_DS_COMBAT_RAN] = 1;
    ++r->c->escapes;
    return play_effect(r, XEEN_CUE_TRANSPORTED);
}
/* Block: the actor blocks for the rest of the round. Original: 38B45 96FB[CBW 97FF] = 1. */
static void party_block(run *r)
{
    xeen_movement *m = r->m;
    m->ds[(uint16_t)(XEEN_DS_COMBAT_BLOCKING + (unsigned)(int)(int8_t)m->ds[XEEN_DS_TURN_ACTOR])] =
            1;
    ++r->c->blocks;
}

/* Bounded original sprintf subset used by 38BB3: %2u and %s only. */
static int format(char *out, size_t size, const uint8_t *fmt, size_t fmt_limit,
                  const unsigned *numbers, const char *const *strings)
{
    size_t at = 0, i;
    unsigned ni = 0, si = 0;
    for (i = 0; i < fmt_limit && fmt[i]; ++i) {
        if (fmt[i] != '%') {
            if (at + 1u >= size) {
                return -1;
            }
            out[at++] = (char)fmt[i];
            continue;
        }
        if (i + 2u < fmt_limit && fmt[i + 1] == '2' && fmt[i + 2] == 'u') {
            char number[8];
            int w = snprintf(number, sizeof number, "%2u", numbers[ni++]);
            if (w < 0 || at + (size_t)w >= size) {
                return -1;
            }
            memcpy(out + at, number, (size_t)w);
            at += (size_t)w;
            i += 2u;
        } else if (i + 1u < fmt_limit && fmt[i + 1] == 's') {
            size_t w = strlen(strings[si++]);
            if (at + w >= size) {
                return -1;
            }
            memcpy(out + at, strings[si - 1u], w);
            at += w;
            i += 1u;
        } else {
            return -1;
        }
    }
    if (i >= fmt_limit) {
        return -1;
    }
    out[at] = 0;
    return (int)at;
}

/* The colour of an attacker's line by its HP against the database's: unhurt 15, below a quarter 32, else 9. */
static unsigned monster_hp_colour(uint16_t hp, uint16_t full)
{
    return hp == full ? 15u : hp < (uint16_t)(full >> 2) ? 32u : 9u;
}
/* The target follows the hit slot (XEEN_DS_HIT_SLOT) when that slot of the party's cell holds a monster (slots 2 and
 * 1); otherwise the target is slot 0's monster and the hit slot goes back to 0. */
static void target_hit_slot(xeen_movement *m)
{
    if (m->ds[XEEN_DS_HIT_SLOT] == 2u && m->ds[XEEN_DS_VIEW_MONSTER(2)] != XEEN_VIEW_NO_MONSTER) {
        m->ds[XEEN_DS_TARGET_MONSTER] = m->ds[XEEN_DS_VIEW_MONSTER(2)];
    } else if (m->ds[XEEN_DS_HIT_SLOT] == 1u && m->ds[XEEN_DS_VIEW_MONSTER(1)] != XEEN_VIEW_NO_MONSTER) {
        m->ds[XEEN_DS_TARGET_MONSTER] = m->ds[XEEN_DS_VIEW_MONSTER(1)];
    } else {
        m->ds[XEEN_DS_TARGET_MONSTER] = m->ds[XEEN_DS_VIEW_MONSTER(0)];
        m->ds[XEEN_DS_HIT_SLOT] = 0;
    }
}
/* The combat window's text: one line per attacker in the party's cell (its name in the colour of its health,
 * monster_hp_colour) into the window's format, written to text buffer 2; the target follows the hit slot
 * (target_hit_slot) between the lines and the window. A monster past the database, a name without its end or a
 * text past the buffer stops. Original: 38BB3 (the DS formats 6771/677F/678D/679B; 9804/306A). */
static int combat_text(run *r)
{
    static const uint16_t formats[3] = {XEEN_TEXT_COMBAT_MONSTER_LINE_1, XEEN_TEXT_COMBAT_MONSTER_LINE_2, XEEN_TEXT_COMBAT_MONSTER_LINE_3};
    xeen_movement *m = r->m;
    char lines[3][48], names[3][61];
    unsigned slot;
    char text[200];
    int written;
    for (slot = 0; slot < 3u; ++slot) {
        uint8_t record_index = m->ds[XEEN_DS_VIEW_MONSTERS + slot];
        const uint8_t *row;
        uint16_t hp;
        unsigned colour;
        const char *strings[1];
        size_t k;
        lines[slot][0] = 0;
        if (record_index == XEEN_VIEW_NO_MONSTER) {
            continue;
        }
        if (!(row = monster_row(r, record_index))) {
            return fail(r, TEXT_STOP_MONSTER);
        }
        hp = xeen_read16(m, (uint16_t)(MONSTER_RECORD(record_index) + XEEN_MAP_MONSTER_HP));
        colour = monster_hp_colour(hp, xeen_get_le16(row + XEEN_MONSTER_HP));
        for (k = 0; k < XEEN_MONSTER_RECORD_BYTES && row[k]; ++k) {
            names[slot][k] = (char)row[k];
        }
        if (k == XEEN_MONSTER_RECORD_BYTES) {
            return fail(r, TEXT_STOP_NAME);
        }
        names[slot][k] = 0;
        strings[0] = names[slot];
        if (format(lines[slot], sizeof lines[slot], m->ds + formats[slot], 0x40u, &colour,
                   strings) < 0) {
            return fail(r, TEXT_STOP_NAME);
        }
    }
    target_hit_slot(m);
    {
        const char *strings[3] = {lines[0], lines[1], lines[2]};
        written = format(text, sizeof text, m->ds + XEEN_TEXT_COMBAT_WINDOW, 0x40u, NULL, strings);
    }
    if (written < 0 || XEEN_DS_TEXT_BUFFER_2 + (unsigned)written + 1u > XEEN_PARTY_RECORDS) {
        return fail(r, TEXT_STOP_SPACE);
    }
    memcpy(m->ds + XEEN_DS_TEXT_BUFFER_2, text, (size_t)written + 1u);
    return 1;
}
/**
 * Presents the combat loop's top frame (the monsters' scene and the party bar) when presentation is
 * attached.
 */
static int present_loop_top(run *r)
{
    xeen_presentation *present = r->c->presentation;
    xeen_sprite_draw draw;
    if (!present) {
        return 1;
    }
    if (xeen_presentation_text(present, XEEN_WINDOW_COMBAT, (const char *)r->m->ds + XEEN_DS_TEXT_BUFFER_2) != XEEN_OK) {
        return fail(r, LOOP_TOP_STOP_TEXT);
    }
    /* 4E38C(2, AC0F) drawn by the owner's text writer. */
    if (r->c->windows.text) {
        r->c->windows.text(r->c->windows.user, XEEN_WINDOW_COMBAT, (const char *)r->m->ds + XEEN_DS_TEXT_BUFFER_2);
    }
    /* 390A4: combat.icn frame 32 at (233,27+10*9804), driver mode 0801 (the
     * 4DAE4 window-clipped draw, decoded natively for window 0). */
    draw = (xeen_sprite_draw){
            r->c->icon_handle, 32, 233, (uint16_t)(27u + 10u * r->m->ds[XEEN_DS_HIT_SLOT]), 0, 0x801, 0};
    if (xeen_presentation_draw_sprites(present, &draw, 1) != XEEN_OK) {
        return fail(r, LOOP_TOP_STOP_DRAW);
    }
    if (xeen_presentation_update_window(present, XEEN_WINDOW_COMBAT) != XEEN_OK) {
        return fail(r, LOOP_TOP_STOP_FRAME);
    }
    return 1;
}

/* 39083..390D9: loop top before the 3912C poll. */
static int loop_top(run *r)
{
    xeen_presentation_trace_extra("mark combat-top"); /* Evidence: 39083 */
    ++r->c->loop_tops;
    highlight(r, r->m->ds[XEEN_DS_TURN_ACTOR]);
    if (!build_order(r) || !combat_text(r) || !present_loop_top(r) || !scene(r)) {
        return 0;
    }
    r->c->active_actor = r->m->ds[XEEN_DS_TURN_ACTOR];
    return 1;
}

/* The fight's last step (also for a party defeated at the entry): the entry's world's archives back, the minimap
 * button home, no target, and, unless the fight began under a sign (the after-combat event then sets the mode), the
 * combat mode back to 1. The combat ends (no actor, inactive) and keeps the view's three attackers for the caller.
 * Original: 39505..395C3: 191DE([bp-6]), 199C = ECh, 306A = FFh; with [bp-4] (the entry's 3066: a sign was up)
 * 3955E 1C844(ED97, ED98) and, with F482, 310B4 before 395C3's FAF8 = 1 (the game's cleanup_event). */
static void cleanup_tail(run *r)
{
    xeen_movement *m = r->m;
    xeen_select_archive(m, r->c->entry_world);
    xeen_write16(m, MINIMAP_BUTTON_X, MINIMAP_BUTTON_HOME_X);
    m->ds[XEEN_DS_TARGET_MONSTER] = XEEN_NO_TARGET;
    if (!r->c->entry_3066) {
        m->ds[XEEN_DS_COMBAT_MODE] = XEEN_COMBAT_MODE_EXPLORING;
    }
    r->c->active_actor = 0xffu;
    r->c->phase = XC_INACTIVE;
    ++r->c->cleanup_complete;
    memcpy(r->c->attackers, m->ds + XEEN_DS_VIEW_MONSTERS, 3);
}
/* Back to exploration after the treasure gate: the party has acted, no member is highlighted, the combat party is
 * the whole party in order again, the party bar is redrawn, and the fight ends (cleanup_tail).
 * Original: 394C5..39505 (42298 the highlight, 2ABB8(1) the bar), then 39505. */
static void leave_combat(run *r)
{
    xeen_movement *m = r->m;
    unsigned i;
    m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_EXPLORATION;
    m->ds[XEEN_DS_PARTY_ACTED] = 1;
    unhighlight(r);
    for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
        m->ds[XEEN_DS_COMBAT_PARTY + i] = (uint8_t)i;
    }
    m->ds[XEEN_DS_COMBAT_PARTY_COUNT] = m->ds[XEEN_DS_PARTY_COUNT];
    party_bar(r);
    cleanup_tail(r);
}
/* A member a party that ran leaves behind: one whose worst condition keeps them from moving (asleep, paralyzed,
 * unconscious, stone or eradicated; not dead). Original: the CS:2F5D table (8, 11, 12, 14, 15). */
static int left_behind(uint8_t worst)
{
    return worst == XEEN_CONDITION_ASLEEP || worst == XEEN_CONDITION_PARALYZED ||
           worst == XEEN_CONDITION_UNCONSCIOUS || worst == XEEN_CONDITION_STONE || worst == XEEN_CONDITION_ERADICATED;
}
/* The combat side of the fight's end. A party that ran while an attacker slot is still filled is checked (every
 * member counts) and, unless that defeated it, moves to the run's position, and each member it left behind
 * (left_behind) is dead. Then the combat window closes, the input is cleared and a scene runs with the minimap
 * shown again. A treasure waits for its dialog while no monster is visible (XC_AWAITING_TREASURE: the caller shows
 * it, then xeen_combat_finish_treasure); otherwise the fight ends here (leave_combat). The turn-taken flags and the
 * turn state are not written: the next fight's entry resets them. Restoring the resources is the game owner's
 * service at the boundary 39430.
 * Original: 39430..395CB; the run FAFE, 2F869 (9A1C 1, F60A), 2125F, 2AB96; 394B3 189D7; 394C4 1B503 with window 2
 * closed (394AB); the treasure gate 394C5 2EA68. */
static int cleanup(run *r)
{
    xeen_movement *m = r->m;
    unsigned i;
    m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_EXPLORATION;
    if (m->ds[XEEN_DS_COMBAT_RAN] && attacker_count(m)) {
        party_check(m);
        if (!m->ds[XEEN_DS_PARTY_DEFEATED]) {
            run_position(m);
            for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
                uint8_t *ch = m->ds + XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES;
                if (left_behind(xeen_character_worst_condition(ch))) {
                    ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_DEAD)] = 1;
                }
            }
        }
    }
    combat_window(r, 2u, 0);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_COMBAT;
    if (!scene(r)) {
        return 0;
    }
    if (xeen_treasure_check(m) == XEEN_TREASURE_DIALOG) {
        r->c->phase = XC_AWAITING_TREASURE;
        r->c->last_boundary = XEEN_TREASURE_FOUND;
        return 0;
    }
    leave_combat(r);
    return 1;
}

xeen_status xeen_combat_finish_treasure(xeen_combat *c, xeen_movement *m, uint32_t *boundary)
{
    if (!c || !m || !boundary || c->phase != XC_AWAITING_TREASURE) {
        return XEEN_BAD_INPUT;
    }
    if (m->ds[XEEN_DS_TREASURE_ITEMS_PENDING] || xeen_get_le32(m->ds + XEEN_DS_TREASURE_GEMS) ||
        xeen_get_le32(m->ds + XEEN_DS_TREASURE_GOLD)) {
        return XEEN_BAD_INPUT;
    }
    /* The scene and treasure gate at the head of cleanup already ran. */
    run r = (run){c, m, NULL, 0, NULL, NULL, NULL, 0};
    leave_combat(&r);
    *boundary = XEEN_COMBAT_OVER;
    return XEEN_SERVICE_REQUIRED;
}

/* 2FCAB draws during a combat time charge: the bounds stay inside the tick,
 * so the diagnostic trace keeps the raw value only and is marked partial. */
static uint16_t tick_draw(void *user, uint16_t incoming)
{
    run *r = user;
    uint16_t raw = r->random(r->user, incoming);
    if (r->c->rng_draws < XEEN_COMBAT_RNG_CAPACITY) {
        r->c->rng_raw[r->c->rng_draws] = raw;
    }
    ++r->c->rng_draws;
    r->c->rng_trace_complete = 0u;
    return raw;
}
/**
 * Advances the clock by one minute inside combat (with its condition loop) and redraws the party
 * bar when a member died of it.
 */
static int time_minute(run *r)
{
    xeen_gameplay_tick t =
            xeen_gameplay_time_conditions_rng(r->m, 1u, r->random ? tick_draw : NULL, r);
    if (t.dead == 2u) {
        party_bar(r); /* 2FFE1 2ABB8(1), before 30004 */
    }
    if (t.boundary == XEEN_TICK_UNFED_DAWN) {
        r->c->rest_tick = t; /* The owner's 18F2F scroll */
    }
    if (t.boundary) {
        return fail(r, t.boundary);
    }
    return 1;
}

static uint32_t continuation_tail(run *);
static uint32_t finish_round(run *);
/* The fight is over: the cleanup (cleanup, which may leave a treasure waiting) and XEEN_COMBAT_OVER, or the stop
 * boundary of a failed cleanup step. */
static uint32_t combat_over(run *r)
{
    return cleanup(r) ? XEEN_COMBAT_OVER : r->boundary;
}
/* Clouds' Ice Trolls regenerate: every map monster of that type is restored to its database HP. Returns 0, before
 * any write, when the monster database is too short to hold the troll's row. */
static int heal_ice_trolls(run *r)
{
    xeen_movement *m = r->m;
    unsigned i;
    for (i = 0; i < m->ds[XEEN_DS_MAP_MONSTER_COUNT]; ++i) {
        if (m->ds[(uint16_t)(MONSTER_RECORD(i) + XEEN_MAP_MONSTER_ID)] != XEEN_CLOUDS_ICE_TROLL) {
            continue;
        }
        if (r->n < (XEEN_CLOUDS_ICE_TROLL + 1u) * XEEN_MONSTER_RECORD_BYTES) {
            return 0;
        }
        xeen_write16(m, (uint16_t)(MONSTER_RECORD(i) + XEEN_MAP_MONSTER_HP),
                     xeen_get_le16(ROW(r->db, XEEN_CLOUDS_ICE_TROLL) + XEEN_MONSTER_HP));
    }
    return 1;
}
/*
 * The round's continuation once every combatant has acted (or the round was cut short): when nobody is left to act,
 * a new round begins: the turn-taken and blocking flags cleared, the turn order rebuilt and its first actor found;
 * on Clouds every Ice Troll is healed to its database HP (the trolls regenerate fully each round); the
 * monsters move and the view's monsters are chosen again (the indoor selector even outdoors, as the original does;
 * the next scene reselects); one minute passes (with its condition loop) and the round is counted. Then
 * continuation_tail. Returns the next original boundary.
 * Original: 3934F..3942D; 393CC calls 1ED46 even outdoors, the next 1B503 reselects through 2126C.
 */
static uint32_t continuation(run *r)
{
    xeen_movement *m = r->m;
    if (all_gone(m)) {
        memset(m->ds + XEEN_DS_TURN_TAKEN, 0, XEEN_COMBATANT_BYTES);
        memset(m->ds + XEEN_DS_COMBAT_BLOCKING, 0, XEEN_COMBATANT_BYTES);
        if (!build_order(r)) {
            return r->boundary;
        }
        m->ds[XEEN_DS_TURN_ACTOR] = m->ds[XEEN_DS_TURN_INDEX] = XEEN_NO_TURN;
        if (!next_actor(r)) {
            return r->boundary;
        }
        if (!m->ds[XEEN_DS_WORLD] && !heal_ice_trolls(r)) {
            return fail(r, CONTINUATION_STOP_DATABASE), r->boundary;
        }
        if (!monster_update(r)) {
            return r->boundary;
        }
        if (xeen_scene_monsters(m, r->db, r->n) != XEEN_OK) {
            return fail(r, SCENE_STOP_MONSTERS), r->boundary;
        }
        memcpy(r->c->attackers, m->ds + XEEN_DS_VIEW_MONSTERS, 3);
        r->c->sky_site = TIME_CHARGE_ROUND_RESET;
        if (!time_minute(r)) {
            return r->boundary;
        }
        ++r->c->rounds;
    }
    return continuation_tail(r);
}
/* With no monster left attacking, a minute passes and a scene runs (a live nested fight that starts there takes over
 * the command poll); still none, the fight is over. Otherwise the round ends (finish_round).
 * Original: 393D9..3942D: 393F0's 2FC7E and 1B503, the attacker test again; the defeat and mode tests; the loop top
 * 39083. */
static uint32_t continuation_tail(run *r)
{
    xeen_movement *m = r->m;
    if (!attacker_count(m)) {
        int ok;
        r->c->sky_site = TIME_CHARGE_NO_ATTACKERS;
        if (!time_minute(r)) {
            return r->boundary;
        }
        r->c->live_contact_site = 1;
        ok = scene(r);
        r->c->live_contact_site = 0;
        if (!ok) {
            return r->boundary;
        }
        if (r->c->live_entered) {
            r->c->live_entered = 0;
            return XEEN_COMBAT_COMMAND_POLL;
        } /* The nested fight's poll */
        if (!attacker_count(m)) {
            return combat_over(r);
        }
    }
    return finish_round(r);
}
/* Whether the round's end also ends the fight: the party was already defeated, or is now (the able-member check runs
 * only when it was not), or the UI mode is no longer combat's. Original: 39413 (F60A), 2F869, 3941F; 9A1C != 2. */
static int round_ends_combat(xeen_movement *m)
{
    if (m->ds[XEEN_DS_PARTY_DEFEATED]) {
        return 1;
    }
    party_check(m);
    return m->ds[XEEN_DS_PARTY_DEFEATED] || m->ds[XEEN_DS_UI_MODE] != XEEN_UI_MODE_COMBAT;
}
/*
 * Ends a combat round (also xeen_combat_resume_outer's tail): when the round ends the fight (round_ends_combat) it
 * leaves through the cleanup (a defeated party's game tail then leaves the exploration loop); otherwise the next
 * round's loop top runs and the combat waits for a command. Returns that next original boundary, or the stop
 * boundary of a failed step.
 * Original: 39413..3942D; the cleanup 39430, the defeated party's 17F07 tail exits at 17F28; the poll 3912C.
 */
static uint32_t finish_round(run *r)
{
    xeen_movement *m = r->m;
    if (round_ends_combat(m)) {
        return combat_over(r);
    }
    if (!loop_top(r)) {
        return r->boundary;
    }
    r->c->phase = XC_AWAITING_COMMAND;
    return XEEN_COMBAT_COMMAND_POLL;
}

/**
 * Returns nonzero when a contact fight can start (38F5F): a party of 1..6 and a monster in contact
 * range in the world's own database.
 */
static int contact_ready(const xeen_movement *m, const uint8_t *db, size_t n)
{
    unsigned slot, present = 0;
    /* No world refuses a fight (the world is kept for the cleanup's archive); the database is the world's own
     * (dark.mon on Darkside). Original: 38F5F keeps DS 94 for 39505's 191DE. */
    if (!m || !db || !m->ds[XEEN_DS_PARTY_COUNT] || m->ds[XEEN_DS_PARTY_COUNT] > 6u) {
        return 0;
    }
    for (slot = 0; slot < 3u; ++slot) {
        uint8_t index = m->ds[XEEN_DS_VIEW_MONSTERS + slot];
        if (index == XEEN_VIEW_NO_MONSTER) {
            continue;
        }
        if (index >= m->ds[XEEN_DS_MAP_MONSTER_COUNT] ||
            (size_t)m->ds[(uint16_t)(MONSTER_RECORD(index) + XEEN_MAP_MONSTER_ID)] * XEEN_MONSTER_RECORD_BYTES +
                    XEEN_MONSTER_RECORD_BYTES >
                    n) {
            return 0;
        }
        ++present;
    }
    return present != 0u;
}

/* A party already defeated before the first round (by the first actor's check, e.g. after the ranged shot that came
 * before the contact) skips the round and the cleanup's head: the rounds' window was never opened, so no scene, no
 * treasure and no party map; only the cleanup's tail (the entry's archive, the interface, the sign) runs, the rest in
 * after_combat.
 * Original: 39059 -> 39505 (F60A set by 38633's 2F869): 191DE back, 190CF/43287/28D80, 199C = ECh, 306A = FFh, the
 * entry 3066 test, FAF8 = 1. */
static xeen_status defeated_at_entry(run *r)
{
    cleanup_tail(r);
    r->c->last_boundary = XEEN_COMBAT_DEFEATED_AT_ENTRY;
    return XEEN_SERVICE_REQUIRED;
}

/* A fight's start stopped at a continuation the native run does not take: the combat is
 * XC_UNSUPPORTED_CONTINUATION at R's boundary (last_boundary), which the caller reports. */
static xeen_status entry_unsupported(xeen_combat *c, const run *r)
{
    c->phase = XC_UNSUPPORTED_CONTINUATION;
    c->last_boundary = r->boundary;
    return XEEN_SERVICE_REQUIRED;
}
/* A contact fight's start, once the combat sheet is loaded: the sign and world kept for the cleanup, the combat
 * modes set, the combat icons' handle in the icon list, the interface and party (combat_setup), the attackers in
 * view, the turn order and the first actor (from here on gameplay may draw and change state; a failure keeps its
 * writes and reports the original's boundary); a party defeated already ends at once (defeated_at_entry); else the
 * turn order again, the monsters' move and scene while their countdown runs, the rounds' window, and the round loop's
 * top up to the command poll. Original: 38F6D..3912C. */
static xeen_status initialize(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                              xeen_entity_random_fn random, void *user, xeen_native_audio *audio)
{
    run r;
    c->entry_3066 = m->ds[XEEN_DS_SIGN_SHOWN];
    c->entry_world = m->ds[XEEN_DS_WORLD];
    m->ds[XEEN_DS_SIGN_SHOWN] = 0;
    m->ds[XEEN_DS_COMBAT_MODE] = XEEN_COMBAT_MODE_FIGHTING;
    m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_COMBAT;
    xeen_write16(m, MINIMAP_BUTTON_X, 0);
    if (c->icon_handle) {
        xeen_write16(m, XEEN_DS_SPRITE_HANDLE, c->icon_handle);
        command_icons_from_handle(m);
    }
    combat_setup(c, m);
    memcpy(c->attackers, m->ds + XEEN_DS_VIEW_MONSTERS, 3);
    r = (run){c, m, db, n, random, user, audio, 0};
    if (!build_order(&r)) {
        return XEEN_BAD_INPUT;
    }
    m->ds[XEEN_DS_TURN_INDEX] = m->ds[XEEN_DS_TURN_ACTOR] = m->ds[XEEN_DS_HIGHLIGHTED_PORTRAIT] = 0xffu;
    if (!next_actor(&r)) {
        return entry_unsupported(c, &r);
    }
    if (m->ds[XEEN_DS_PARTY_DEFEATED]) {
        return defeated_at_entry(&r);
    }
    if (!build_order(&r)) {
        return entry_unsupported(c, &r);
    }
    if (m->ds[XEEN_DS_MONSTER_COUNTDOWN] && (!monster_update(&r) || !scene(&r))) {
        return entry_unsupported(c, &r);
    }
    combat_window(&r, 2u, 1);
    /* No UI-mode test here: the original goes on to the loop's top whatever the monsters' move left (after a defeat
     * there the mode is exploration's, and its scenes' contact runs the nested entry). Original: 39083 after the 96C0
     * branch (2A701, 9A1C 1). */
    if (!loop_top(&r)) {
        return entry_unsupported(c, &r);
    }
    c->phase = XC_AWAITING_COMMAND;
    return XEEN_SERVICE_REQUIRED;
}

/* A new fight's state: everything zero but the services the host attached, which every fight keeps (the ranged
 * delivery and its user, the windows, the nested fight's icons, the deferred cell event). */
static xeen_combat fresh_combat(const xeen_combat *c)
{
    xeen_combat next;
    memset(&next, 0, sizeof next);
    next.ranged_delivery = c->ranged_delivery;
    next.ranged_user = c->ranged_user;
    next.windows = c->windows;
    next.nested_icons = c->nested_icons;
    next.cell_event = c->cell_event;
    return next;
}

xeen_status xeen_combat_initialize(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                                   xeen_entity_random_fn random, void *user,
                                   xeen_native_audio *audio)
{
    xeen_combat next;
    xeen_status status;
    xeen_movement *before;
    if (!c || c->phase != XC_INACTIVE || !m || !random || !contact_ready(m, db, n) ||
        (m->ds[XEEN_DS_SOUND_ON] && !audio)) {
        return XEEN_BAD_INPUT;
    }
    before = malloc(sizeof *before);
    if (!before) {
        return XEEN_BAD_INPUT;
    }
    *before = *m;
    next = fresh_combat(c);
    next.rng_trace_complete = 1u;
    next.presentation = c->presentation;
    next.icon_handle = c->icon_handle;
    *c = next;
    status = initialize(c, m, db, n, random, user, audio);
    if (status == XEEN_BAD_INPUT) {
        *m = *before;
        *c = (xeen_combat){0};
    }
    free(before);
    return status;
}

/* What a fight's start puts back when it fails before the fight begins: the movement, the sprite cache and the
 * presentation as they were, and the framebuffer's pixels. */
typedef struct {
    xeen_movement *movement;
    xeen_sprite_cache *cache;
    xeen_presentation *presentation;
    uint8_t *pixels;
} begin_backup;
/* Takes B's copies of M, CACHE and PRESENT (and its framebuffer). Returns 0, with nothing kept, without memory. */
static int begin_backup_take(begin_backup *b, const xeen_movement *m, const xeen_sprite_cache *cache,
                             const xeen_presentation *present)
{
    b->movement = malloc(sizeof *b->movement);
    b->cache = malloc(sizeof *b->cache);
    b->presentation = malloc(sizeof *b->presentation);
    b->pixels = malloc(XEEN_PRESENT_PIXELS);
    if (!b->movement || !b->cache || !b->presentation || !b->pixels) {
        free(b->movement);
        free(b->cache);
        free(b->presentation);
        free(b->pixels);
        return 0;
    }
    *b->movement = *m;
    *b->cache = *cache;
    *b->presentation = *present;
    memcpy(b->pixels, present->framebuffer, XEEN_PRESENT_PIXELS);
    return 1;
}
/* Puts B's copies back into M, CACHE and PRESENT (its framebuffer's pixels too). */
static void begin_backup_restore(const begin_backup *b, xeen_movement *m, xeen_sprite_cache *cache,
                                 xeen_presentation *present)
{
    *m = *b->movement;
    *cache = *b->cache;
    *present = *b->presentation;
    memcpy(present->framebuffer, b->pixels, XEEN_PRESENT_PIXELS);
}
static void begin_backup_free(begin_backup *b)
{
    free(b->movement);
    free(b->cache);
    free(b->presentation);
    free(b->pixels);
}
/* Pushes the combat icons (combat.icn) on the handle stack over the exploration icons' handle (HANDLE, the first
 * free one after the map's sprites, which stay resident: the combat scene draws them), *HANDLE then the icons'.
 * Returns 0 for no exploration handle or when combat.icn cannot be loaded.
 * Original: 38F8E 190A3("combat.icn"): 96C1 up, 95A8[96C1] = 98D4, 18931 with reload 0. */
static int push_combat_icons(xeen_movement *m, xeen_sprite_cache *cache, const xeen_sprite_archives *archives,
                             uint16_t *handle)
{
    if (!*handle) {
        return 0;
    }
    m->ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(m->ds[XEEN_DS_HANDLE_DEPTH] + 1u);
    xeen_write16(m, XEEN_DS_HANDLE_SLOT(m->ds), *handle);
    return xeen_sprite_load(cache, archives, m->ds[XEEN_DS_WORLD], (const char *)m->ds + XEEN_TEXT_COMBAT_SPRITES, 0,
                            handle) == XR_OK;
}
/*
 * Begins a contact fight: a fresh combat state bound to the presentation, the combat icons pushed (their handle the
 * fight's: initialize writes it into the icon list), then the fight's start (xeen_combat_initialize). Refused
 * (XEEN_BAD_INPUT) for missing inputs, a combat already running, a contact not ready, sound on without audio, or no
 * memory. A start that fails before any random draw or monster update (no exploration handle, combat.icn missing,
 * or the start's own refusal) puts the movement, the sprite cache, the presentation and its pixels back as they were,
 * clears the combat state and answers XEEN_BAD_INPUT.
 */
xeen_status xeen_combat_begin(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                              xeen_sprite_cache *cache, const xeen_sprite_archives *archives,
                              xeen_presentation *present, xeen_entity_random_fn random, void *user,
                              xeen_native_audio *audio)
{
    begin_backup b;
    uint16_t handle;
    xeen_status status = XEEN_BAD_INPUT;
    if (!c || c->phase != XC_INACTIVE || !m || !cache || !archives || !present || !random ||
        !present->framebuffer || present->framebuffer_size < XEEN_PRESENT_PIXELS ||
        !contact_ready(m, db, n) || (m->ds[XEEN_DS_SOUND_ON] && !audio)) {
        return XEEN_BAD_INPUT;
    }
    if (!begin_backup_take(&b, m, cache, present)) {
        return XEEN_BAD_INPUT;
    }
    handle = xeen_read16(m, XEEN_DS_SPRITE_HANDLE);
    *c = fresh_combat(c);
    c->presentation = present;
    if (push_combat_icons(m, cache, archives, &handle)) {
        c->icon_handle = handle;
        status = xeen_combat_initialize(c, m, db, n, random, user, audio);
    }
    if (status == XEEN_BAD_INPUT) {
        begin_backup_restore(&b, m, cache, present);
        *c = (xeen_combat){0};
    }
    begin_backup_free(&b);
    return status;
}

/*
 * Combat's command keys. The host passes the key in the low byte of a command word: the attacker slots, Attack and
 * Block match that byte, every other key the whole word. Original: 3914E's table CS:2F7D (Phase 100); each key's
 * handler follows its name.
 */
enum {
    COMMAND_KEY_CONTROL_PANEL = 0x09,  /* Tab: the control panel. Original: 392C9, 297D9 */
    COMMAND_KEY_ATTACKER_1 = '1',      /* '1'..'3': target the view's attacker n. Original: 392E6.., 98B4 + n */
    COMMAND_KEY_ATTACKER_3 = '3',
    COMMAND_KEY_ATTACK = 'A',          /* Original: 39203 */
    COMMAND_KEY_BLOCK = 'B',           /* Original: 39291 */
    COMMAND_KEY_CAST = 'C',            /* Original: 391E5 */
    COMMAND_KEY_QUICK_FIGHT = 'F',     /* Original: 391DE */
    COMMAND_KEY_INFO = 'I',            /* the Info dialog. Original: 392BB, 29EF0 */
    COMMAND_KEY_OPTIONS = 'O',         /* Quick Fight Options. Original: 3929C, 3E9B6 */
    COMMAND_KEY_QUICK_REFERENCE = 'Q', /* Original: 392C2, 46BA0 */
    COMMAND_KEY_RUN = 'R',             /* Original: 39260 */
    COMMAND_KEY_USE = 'U',             /* Original: 39223 */
    COMMAND_KEY_VIEW_FIRST = 0xc9,     /* F1..F6 (the key codes of input_poll.h): the member's character view.
                                        * Original: 39322, 44AE6(F5FE - C9h) */
    COMMAND_KEY_VIEW_LAST = 0xce,
    COMMAND_KEY_TURN_LEFT = 0xf0,      /* the arrow keys. Original: 39168 */
    COMMAND_KEY_TURN_RIGHT = 0xf1      /* Original: 39188 */
};
/* How dispatch_command left a command. */
typedef enum {
    COMMAND_CONTINUES,   /* the common 3934F continuation follows */
    COMMAND_UNSUPPORTED, /* stopped at r->boundary; the caller restores DS if nothing was drawn */
    COMMAND_RETURNS      /* complete: the phase and *boundary are set, DS stays as it is */
} command_outcome;

/* 1 when combatant code actor is a listed party member (a combat position below 971F). */
static int is_party_actor(const xeen_movement *m, uint8_t actor)
{
    return actor < m->ds[XEEN_DS_COMBAT_PARTY_COUNT];
}

/* The command continues in the game owner's shared dialog: the phase waits for its result, which
 * the owner hands back at site (xeen_combat_after_cast / _after_use / _after_modal). */
static command_outcome await_owner(xeen_combat *c, uint32_t site, uint32_t *boundary)
{
    c->phase = XC_AWAITING_CAST;
    *boundary = site;
    c->last_boundary = 0;
    return COMMAND_RETURNS;
}

/* 39203 Attack: the actor's attack (371E2(record, 0)), then 39295's next actor (38633). */
static int command_attack(run *r)
{
    xeen_combat *c = r->c;
    uint8_t actor = r->m->ds[XEEN_DS_TURN_ACTOR];
    if (!is_party_actor(r->m, actor)) {
        return fail(r, COMMAND_STOP_ATTACK_ACTOR);
    }
    c->last_player_actor = actor;
    if (!party_attack(r, MEMBER(r->m, actor))) {
        return 0;
    }
    ++c->commands;
    ++c->actions;
    return next_actor(r);
}

/* Whether ACTOR is no actor at all because the party fell inside a nested fight (the scene's contact tail): the turn
 * actor is FFh, the party defeated and a nested entry made. Block, Run and Use then take their quirk paths. */
static int nested_defeat_actor(const xeen_combat *c, const xeen_movement *m, uint8_t actor)
{
    return actor == XEEN_NO_TURN && m->ds[XEEN_DS_PARTY_DEFEATED] && c->nested_entries;
}
/*
 * Block: the acting member blocks for the rest of the round (XEEN_DS_COMBAT_BLOCKING: a monster's attack then reads
 * the member's defense as level / 2 + 15 instead of 10; the end of combat and the next round clear it), then the
 * turn advances to the common continuation. Nothing is drawn. A preserved quirk: after a nested defeat (the party
 * fell in the scene's contact tail) there is no turn actor (FFh), the index sign-extends to -1 and the store hits
 * the byte just below the array; the advance then finds the party defeated.
 * Original: 39291; 38B45 sets COMBAT_BLOCKING[CBW 97FF] = 1 (read by 38049's defense; cleared by 38F5F and the 3936A
 * round reset; 96FA for 97FF FFh), 38633, 3934F; F60A.
 */
static int command_block(run *r)
{
    xeen_movement *m = r->m;
    xeen_combat *c = r->c;
    uint8_t actor = m->ds[XEEN_DS_TURN_ACTOR];
    if (nested_defeat_actor(c, m, actor)) {
        m->ds[XEEN_DS_COMBAT_BLOCKING - 1u] = 1;
        ++c->commands;
        ++c->blocks;
        return next_actor(r);
    }
    if (!is_party_actor(m, actor)) {
        return fail(r, COMMAND_STOP_BLOCK_ACTOR);
    }
    m->ds[(uint16_t)(XEEN_DS_COMBAT_BLOCKING + actor)] = 1;
    c->last_player_actor = actor;
    ++c->commands;
    ++c->actions;
    ++c->blocks;
    return next_actor(r);
}

/* Reports where the native run R stopped: *BOUNDARY and the combat's last_boundary are R's boundary, and the answer
 * is XEEN_SERVICE_REQUIRED (the host takes the boundary). */
static xeen_status report_stop(xeen_combat *c, const run *r, uint32_t *boundary)
{
    *boundary = r->boundary;
    c->last_boundary = r->boundary;
    return XEEN_SERVICE_REQUIRED;
}
/* A continuation the native run stopped at (R's boundary): the combat is XC_UNSUPPORTED_CONTINUATION there, and the
 * caller reports that boundary. */
static xeen_status round_unsupported(xeen_combat *c, const run *r, uint32_t *boundary)
{
    c->phase = XC_UNSUPPORTED_CONTINUATION;
    return report_stop(c, r, boundary);
}
/*
 * The answer of a round's continuation, once *BOUNDARY holds what the continuation returned: 2EA8E
 * (XEEN_TREASURE_FOUND) when the round now awaits its treasure dialog, else the unsupported stop at R's boundary
 * when the native run stopped at one (round_unsupported), else *BOUNDARY as it is. Always XEEN_SERVICE_REQUIRED.
 */
static xeen_status round_outcome(xeen_combat *c, run *r, uint32_t *boundary)
{
    if (c->phase == XC_AWAITING_TREASURE) {
        *boundary = XEEN_TREASURE_FOUND;
        return XEEN_SERVICE_REQUIRED;
    }
    if (r->boundary) {
        return round_unsupported(c, r, boundary);
    }
    return XEEN_SERVICE_REQUIRED;
}
/*
 * The fight's end through the combat cleanup (39430): XEEN_COMBAT_OVER when the cleanup ended the combat, 2EA8E
 * (XEEN_TREASURE_FOUND) when a pending treasure is to be shown first, else the unsupported stop at R's boundary
 * (round_unsupported; also when it is 0). Always XEEN_SERVICE_REQUIRED.
 */
static xeen_status fight_end(xeen_combat *c, run *r, uint32_t *boundary)
{
    if (cleanup(r)) {
        *boundary = XEEN_COMBAT_OVER;
        return XEEN_SERVICE_REQUIRED;
    }
    if (c->phase == XC_AWAITING_TREASURE) {
        *boundary = XEEN_TREASURE_FOUND;
        return XEEN_SERVICE_REQUIRED;
    }
    return round_unsupported(c, r, boundary);
}

/* Everyone ran: after a Run, with the UI mode back to exploration's, nobody is left to act. The pending treasure is
 * lost (its counts and gold zeroed), then the party map and the combat cleanup. Its stops keep their writes (no
 * rollback). Original: 39272: 9A10, 9A12, 9A14 and the dword 9A16 = 0; 2125F; the cleanup 39430. */
static command_outcome everyone_ran(run *r, uint32_t *boundary)
{
    xeen_movement *m = r->m;
    xeen_combat *c = r->c;
    m->ds[XEEN_DS_TREASURE_ITEMS_PENDING] = 0;
    xeen_write16(m, XEEN_DS_TREASURE_GEMS, 0);
    xeen_write16(m, XEEN_DS_TREASURE_GEMS + 2u, 0);
    xeen_put_le32(m->ds + XEEN_DS_TREASURE_GOLD, 0);
    run_position(m);
    (void)fight_end(c, r, boundary);
    return COMMAND_RETURNS;
}

/*
 * Run: the acting member tries to flee (a roll 1..100 against the map's run chance), then the turn advances; when
 * everyone has run (UI mode 1) the fight ends through everyone_ran, otherwise the common continuation follows.
 * The dead party's command poll (after a nested defeat: no turn actor, the party defeated) still draws the roll as
 * for a member. A roll below the run chance would store the combat party's count at index -1 and reorder 254
 * members; no case was witnessed (the run chance is 0 wherever the entry defeat occurs), so that path stops.
 * Otherwise nothing is written, the advance finds the party defeated and the treasure is dropped.
 * Original: 39260; 38B00 (AL 52h, the key; its 504DA(1, 100) against C103, the map's run chance), 38633, then
 * everyone_ran (UI mode 1) or 3934F. The dead party's poll after a nested 38F5F (97FF FFh, F60A; phase159 orig159
 * runs orig-52): 38B1C's 9720[-1] = 971F (FFh, then FEh) and 38D25's 254 members, the stop 38B16; 38633 finds F60A
 * (9A1C 1) and 39272 drops the treasure.
 */
static command_outcome command_run(run *r, uint32_t *boundary)
{
    xeen_movement *m = r->m;
    xeen_combat *c = r->c;
    uint8_t actor = m->ds[XEEN_DS_TURN_ACTOR];
    if (nested_defeat_actor(c, m, actor)) {
        ++c->runs;
        if ((int16_t)random_word(r, 1u, 100u, 0x52u) < (int16_t)m->ds[XEEN_DS_MAP_FIELD(RUN_CHANCE)]) {
            fail(r, RUN_STOP_DEFEATED_ESCAPE);
            return COMMAND_UNSUPPORTED;
        }
        ++c->commands;
    } else {
        if (!is_party_actor(m, actor)) {
            fail(r, COMMAND_STOP_RUN_ACTOR);
            return COMMAND_UNSUPPORTED;
        }
        c->last_player_actor = actor;
        if (!party_run(r, 0x52u)) {
            return COMMAND_UNSUPPORTED;
        }
        ++c->commands;
        ++c->actions;
    }
    if (!next_actor(r)) {
        return COMMAND_UNSUPPORTED;
    }
    if (m->ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_EXPLORATION) {
        return everyone_ran(r, boundary);
    }
    return COMMAND_CONTINUES;
}

/* A member's quick-fight choice (the record's XEEN_CHARACTER_QUICK_FIGHT byte, as the Quick Fight Options set it). */
enum { QUICK_FIGHT_ATTACK = 0, QUICK_FIGHT_CAST = 1, QUICK_FIGHT_BLOCK = 2, QUICK_FIGHT_RUN = 3 };
/*
 * Quick Fight (F): the acting member does their quick-fight choice: attack, cast the ready spell without the Cast
 * dialog (the game owner casts it and hands back through xeen_combat_after_modal), block, or run; any other value
 * does nothing. The turn is then spent whatever happened (the cast's when the owner hands back: no spell cast
 * means an attack instead).
 * Original: 391DE: 387AE by the choice (CS:21A0): 371E2(record, 0), 3D8C1(1) (back at 387F4), 38B45, 38B00 (AL 3);
 * then 39295's 38633.
 */
static command_outcome command_quick_fight(run *r, uint32_t *boundary)
{
    xeen_combat *c = r->c;
    const uint8_t *ch;
    uint8_t actor = r->m->ds[XEEN_DS_TURN_ACTOR];
    if (!is_party_actor(r->m, actor)) {
        fail(r, COMMAND_STOP_QUICK_FIGHT_ACTOR);
        return COMMAND_UNSUPPORTED;
    }
    ch = MEMBER(r->m, actor);
    c->last_player_actor = actor;
    ++c->commands;
    ++c->quick_fights;
    switch (ch[XEEN_CHARACTER_QUICK_FIGHT]) {
        case QUICK_FIGHT_ATTACK:
            if (!party_attack(r, ch)) {
                return COMMAND_UNSUPPORTED;
            }
            break;
        case QUICK_FIGHT_CAST:
            c->modal_site = XEEN_COMBAT_MODAL_QUICK_FIGHT;
            return await_owner(c, XEEN_COMBAT_MODAL_QUICK_FIGHT, boundary);
        case QUICK_FIGHT_BLOCK:
            party_block(r);
            break;
        case QUICK_FIGHT_RUN:
            if (!party_run(r, 3u)) {
                return COMMAND_UNSUPPORTED;
            }
            break;
        default:
            break;
    }
    ++c->actions;
    return next_actor(r) ? COMMAND_CONTINUES : COMMAND_UNSUPPORTED;
}

/* The handler of a command whose dialog the game owner runs (Tab, I, O, Q, and F1..F6 for a listed
 * member: 39322 needs a position below 971F), or 0. */
static uint32_t owner_dialog_site(const xeen_movement *m, uint16_t command)
{
    switch (command) {
        case COMMAND_KEY_CONTROL_PANEL:
            return XEEN_COMBAT_MODAL_CONTROL_PANEL;
        case COMMAND_KEY_INFO:
            return XEEN_COMBAT_MODAL_INFO;
        case COMMAND_KEY_OPTIONS:
            return XEEN_COMBAT_MODAL_OPTIONS;
        case COMMAND_KEY_QUICK_REFERENCE:
            return XEEN_COMBAT_MODAL_QUICK_REFERENCE;
        default:
            break;
    }
    if (command >= COMMAND_KEY_VIEW_FIRST && command <= COMMAND_KEY_VIEW_LAST &&
        m->ds[XEEN_DS_COMBAT_PARTY_COUNT] > command - (unsigned)COMMAND_KEY_VIEW_FIRST) {
        return XEEN_COMBAT_MODAL_VIEW_MEMBER;
    }
    return 0;
}

/*
 * Turning in combat (the left and right arrows): the party faces one step left or right (a facing above 3 is kept as
 * it is); the step flip toggles indoors when 9732 is 0 or 3, and always outdoors; with the monsters' countdown
 * running the monsters move; the turn counts as the party's action for the next scene. No action is spent: the same
 * actor keeps the turn. Original: 39168/39188 (ED96 - 1 / + 1 through CS:2F75 / CS:2F6D); 391B0 (F5E6 ^= 1 when 9732
 * is 0 or 3 or F608 is set); 96C0: 2A701; 3067 = 1; then 3934F.
 */
static int command_turn(run *r, uint16_t command)
{
    xeen_movement *m = r->m;
    uint8_t facing = m->ds[XEEN_DS_PARTY_DIRECTION];
    if (facing <= 3u) {
        m->ds[XEEN_DS_PARTY_DIRECTION] =
                (uint8_t)(command == COMMAND_KEY_TURN_LEFT ? (facing + 3u) & 3u
                                                           : (facing + 1u) & 3u);
    }
    if (!m->ds[0x9732] || xeen_read16(m, XEEN_DS_OUTDOORS) || m->ds[0x9732] == 3u) {
        xeen_write16(m, XEEN_DS_TURN_FLIP, (uint16_t)(xeen_read16(m, XEEN_DS_TURN_FLIP) ^ 1u));
    }
    if (m->ds[XEEN_DS_MONSTER_COUNTDOWN] && !monster_update(r)) {
        return 0;
    }
    m->ds[XEEN_DS_PARTY_ACTED] = 1;
    ++r->c->commands;
    ++r->c->turns;
    return 1;
}

/* One command of the acting member's poll, tested in the original's table order: choosing an attacker slot, Attack,
 * Block, Cast and Use (their shared dialogs, which the host owns), Run, Quick Fight, the dialogs the combat owner runs
 * (a member's view among them), a member position past the combat party, and the turns. Any other key goes on to the
 * common continuation without spending an action. Original: 3914E (the table CS:2F7D, from the poll 3912C), 39161. */
static command_outcome dispatch_command(run *r, uint16_t command, uint32_t *boundary)
{
    xeen_movement *m = r->m;
    xeen_combat *c = r->c;
    uint8_t key = (uint8_t)command;
    uint32_t site;
    if (key >= COMMAND_KEY_ATTACKER_1 && key <= COMMAND_KEY_ATTACKER_3) {
        /* An attacker slot with a monster in it becomes the target (and the hit slot). Original: 392E6, 392FA, 3930E
         * (306A, 9804). */
        unsigned slot = (unsigned)(key - COMMAND_KEY_ATTACKER_1);
        if (m->ds[XEEN_DS_VIEW_MONSTERS + slot] != XEEN_VIEW_NO_MONSTER) {
            m->ds[XEEN_DS_TARGET_MONSTER] = m->ds[XEEN_DS_VIEW_MONSTERS + slot];
            m->ds[XEEN_DS_HIT_SLOT] = (uint8_t)slot;
        }
        ++c->commands;
        return COMMAND_CONTINUES;
    }
    if (key == COMMAND_KEY_ATTACK) {
        return command_attack(r) ? COMMAND_CONTINUES : COMMAND_UNSUPPORTED;
    }
    if (key == COMMAND_KEY_BLOCK) {
        return command_block(r) ? COMMAND_CONTINUES : COMMAND_UNSUPPORTED;
    }
    if (command == COMMAND_KEY_CAST) {
        /* The Cast dialog the exploration shares; xeen_combat_after_cast continues after it. Original: 3D8C1(0), then
         * 391ED. */
        ++c->commands;
        return await_owner(c, XEEN_COMBAT_CAST_DIALOG, boundary);
    }
    if (command == COMMAND_KEY_USE) {
        /* The items dialog on the acting member (kept: the dialog's tail clears the turn actor); xeen_combat_after_use
         * continues after it. Original: [BP-2] = 97FF, 44F92(9720[97FF]'s record, 7), then 39249. */
        ++c->commands;
        c->use_actor = m->ds[XEEN_DS_TURN_ACTOR];
        return await_owner(c, XEEN_COMBAT_USE_DIALOG, boundary);
    }
    if (command == COMMAND_KEY_RUN) {
        return command_run(r, boundary);
    }
    if (command == COMMAND_KEY_QUICK_FIGHT) {
        return command_quick_fight(r, boundary);
    }
    if ((site = owner_dialog_site(m, command)) != 0) {
        /* A dialog the combat's owner runs; xeen_combat_after_modal continues after it. A member's view first makes the
         * key the member's position. Original: 39322 (F5FE - C9h). */
        if (site == XEEN_COMBAT_MODAL_VIEW_MEMBER) {
            xeen_write16(m, XEEN_DS_INPUT_CODE,
                         (uint16_t)(command - (unsigned)COMMAND_KEY_VIEW_FIRST));
        }
        ++c->commands;
        c->modal_site = site;
        return await_owner(c, site, boundary);
    }
    if (command >= COMMAND_KEY_VIEW_FIRST && command <= COMMAND_KEY_VIEW_LAST) {
        /* A member position past the combat party: the position is kept and the continuation follows, no dialog.
         * Original: 39322 (F5FE - C9h kept), 3934F. */
        xeen_write16(m, XEEN_DS_INPUT_CODE, (uint16_t)(command - (unsigned)COMMAND_KEY_VIEW_FIRST));
        ++c->commands;
        return COMMAND_CONTINUES;
    }
    if (command == COMMAND_KEY_TURN_LEFT || command == COMMAND_KEY_TURN_RIGHT) {
        return command_turn(r, command) ? COMMAND_CONTINUES : COMMAND_UNSUPPORTED;
    }
    return COMMAND_CONTINUES;
}

/* Whether a command that stopped at R's boundary undoes its DS writes: a rejection before any original random draw
 * (the fight's draw count still DRAWS) leaves DS unchanged. The round charge's continuations (30DB3, the owner's
 * scroll; 30004, the sky reload) are not rejections: their writes stay. */
static int rejected_before_draws(const xeen_combat *c, const run *r, uint16_t draws)
{
    return c->rng_draws == draws && r->boundary != XEEN_TICK_UNFED_DAWN && r->boundary != XEEN_TICK_SKY_RELOAD;
}
xeen_status xeen_combat_command(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                                xeen_entity_random_fn random, void *user, xeen_native_audio *audio,
                                uint16_t command, uint32_t *boundary)
{
    run r;
    xeen_movement *before;
    uint16_t draws;
    if (!c || !m || !boundary || c->phase != XC_AWAITING_COMMAND) {
        return XEEN_BAD_INPUT;
    }
    *boundary = XEEN_COMBAT_COMMAND_POLL;
    if (!random || !db || (m->ds[XEEN_DS_SOUND_ON] && !audio)) {
        c->phase = XC_UNSUPPORTED_CONTINUATION;
        *boundary = COMMAND_STOP_NO_SERVICES;
        return XEEN_SERVICE_REQUIRED;
    }
    before = malloc(sizeof *before);
    if (!before) {
        return XEEN_BAD_INPUT;
    }
    *before = *m;
    draws = c->rng_draws;
    r = (run){c, m, db, n, random, user, audio, 0};
    switch (dispatch_command(&r, command, boundary)) {
        case COMMAND_RETURNS:
            free(before);
            return XEEN_SERVICE_REQUIRED;
        case COMMAND_CONTINUES:
            *boundary = continuation(&r);
            if (c->phase == XC_AWAITING_TREASURE || !r.boundary) {
                free(before);
                return round_outcome(c, &r, boundary); /* the treasure, or the next poll */
            }
            break;
        case COMMAND_UNSUPPORTED:
            break;
    }
    if (rejected_before_draws(c, &r, draws)) {
        *m = *before;
    }
    free(before);
    return round_unsupported(c, &r, boundary);
}
/* The end of a round resumed after the combat Cast, Use or a modal (xeen_combat_after_*): the continuation's
 * boundary, 2EA8E when the round now awaits its treasure, or the unsupported stop when the run ended at a
 * boundary. */
static xeen_status round_end(xeen_combat *c, run *r, uint32_t *boundary)
{
    *boundary = continuation(r);
    return round_outcome(c, r, boundary);
}

/* After the Cast dialog in combat: a paid spell spends the turn (the next actor); none highlights the actor again,
 * by the member's party index (unlike the loop top, which uses the combat index); both go on to the round's next
 * step. Original: 391ED after 3D8C1: AX to 39295 (38633); 2A1F6(9720[97FF]) at 392DD; 3934F. */
xeen_status xeen_combat_after_cast(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                                   xeen_entity_random_fn random, void *user,
                                   xeen_native_audio *audio, int paid, uint32_t *boundary)
{
    run r;
    if (!c || !m || !db || !boundary || c->phase != XC_AWAITING_CAST) {
        return XEEN_BAD_INPUT;
    }
    r = (run){c, m, db, n, random, user, audio, 0};
    c->phase = XC_AWAITING_COMMAND;
    *boundary = XEEN_COMBAT_COMMAND_POLL;
    if (paid) {
        ++c->actions;
        c->last_player_actor = m->ds[XEEN_DS_TURN_ACTOR];
        if (!next_actor(&r)) {
            return round_unsupported(c, &r, boundary);
        }
    } else {
        highlight(&r, m->ds[(uint16_t)(XEEN_DS_COMBAT_PARTY +
                                       (unsigned)(int)(int8_t)m->ds[XEEN_DS_TURN_ACTOR])]);
    }
    return round_end(c, &r, boundary);
}

/*
 * After a member used an item in combat: Use's own tail runs first (the monsters' update, with its ranged delivery
 * and its draws as the round's, then no turn actor); the combat loop then sees the actor changed, restores the member
 * as the actor and advances the turn, and the common continuation follows. The loop top's combat-index update and the
 * panel redraw the original has there are not reached on this path.
 * Original: 39249 after 44F92(record, 7); Use's tail 3BE09..3BE13 (97F9 = 0, 2A701: monster_update, its 2AAB1
 * delivery and 2B5E9 draws; 97FF = FFh); then 97FF = [BP-2] and 39295 (38633); unused 3933F 2A1F6(97FF) and 43244;
 * 3934F.
 */
xeen_status xeen_combat_use_tail(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                                 xeen_entity_random_fn random, void *user, xeen_native_audio *audio,
                                 uint32_t *boundary)
{
    run r;
    if (!c || !m || !db || !boundary || c->phase != XC_AWAITING_CAST) {
        return XEEN_BAD_INPUT;
    }
    r = (run){c, m, db, n, random, user, audio, 0};
    m->ds[XEEN_DS_MISSILES_FLYING] = 0;
    if (!monster_update(&r)) {
        *boundary = r.boundary;
        return XEEN_SERVICE_REQUIRED;
    } /* 3BE0E 2A701 */
    m->ds[XEEN_DS_TURN_ACTOR] = XEEN_NO_TURN;
    c->use_tail = 1;
    *boundary = 0; /* 3BE13 */
    return XEEN_OK;
}
/* Combat's Use has handed back (USED: an item was used). A used item spends the actor's turn: the monsters move
 * (unless the item's own tail moved them already), the action is counted and the next actor found. Without a use
 * the actor's highlight and the combat buttons come back. Then the round's end. */
xeen_status xeen_combat_after_use(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                                  xeen_entity_random_fn random, void *user,
                                  xeen_native_audio *audio, int used, uint32_t *boundary)
{
    run r;
    if (!c || !m || !db || !boundary || c->phase != XC_AWAITING_CAST) {
        return XEEN_BAD_INPUT;
    }
    r = (run){c, m, db, n, random, user, audio, 0};
    c->phase = XC_AWAITING_COMMAND;
    *boundary = XEEN_COMBAT_COMMAND_POLL;
    /* A dead party's Use after a nested fight (no actor): an item used there would take the no-actor path too, which
     * no original run has shown: an explicit stop. Without a use that path only clears the highlight.
     * Original: 97FF FFh at 39223; 39249 compares CBW 97FF with [BP-2] = -1 (Use's tail writes 97FF FFh); 3933F, not
     * 39295; 3933F's 2A1F6(-1) (96C3 FFh), 43244. */
    if (used && nested_defeat_actor(c, m, c->use_actor)) {
        fail(&r, USE_STOP_DEFEATED_ITEM);
        return round_unsupported(c, &r, boundary);
    }
    if (used) {
        if (!c->use_tail) {
            m->ds[XEEN_DS_MISSILES_FLYING] = 0;
            if (!monster_update(&r)) {
                return round_unsupported(c, &r, boundary);
            }
        }
        c->use_tail = 0;
        m->ds[XEEN_DS_TURN_ACTOR] = c->use_actor;
        ++c->actions;
        c->last_player_actor = c->use_actor;
        if (!next_actor(&r)) {
            return round_unsupported(c, &r, boundary);
        }
    } else {
        highlight(&r, m->ds[XEEN_DS_TURN_ACTOR]);
        xeen_combat_buttons(m, 1);
    }
    return round_end(c, &r, boundary);
}

/* The modal commands' return (contract and original sites: combat.h): Quick Fight's attack, Mr Wizard's end of the
 * fight (fight_end), the member highlighted again after Info, Options, Quick Reference or a member view; then the
 * round goes on (round_end). */
xeen_status xeen_combat_after_modal(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                                    xeen_entity_random_fn random, void *user,
                                    xeen_native_audio *audio, uint32_t site, uint16_t ax,
                                    uint32_t *boundary)
{
    run r;
    if (!c || !m || !db || !boundary || c->phase != XC_AWAITING_CAST || !site ||
        site != c->modal_site) {
        return XEEN_BAD_INPUT;
    }
    r = (run){c, m, db, n, random, user, audio, 0};
    c->phase = XC_AWAITING_COMMAND;
    c->modal_site = 0;
    *boundary = XEEN_COMBAT_COMMAND_POLL;
    switch (site) {
        case XEEN_COMBAT_MODAL_QUICK_FIGHT: /* no spell cast (AX 0): the member attacks instead. Original: 387FC, 387E3 */
            if (!ax && !party_attack(&r, MEMBER(m, m->ds[XEEN_DS_TURN_ACTOR]))) {
                return round_unsupported(c, &r, boundary);
            }
            ++c->actions;
            if (!next_actor(&r)) {
                return round_unsupported(c, &r, boundary);
            }
            break;
        case XEEN_COMBAT_MODAL_CONTROL_PANEL:
            if (ax == 2u) {
                /* Mr Wizard's help (the panel's answer 2) ends the fight: the ordinary cleanup, so a pending treasure
                 * still shows (394C5), whose tail then moves the party for the game owner (the wizard flag).
                 * Original: 392CE..392D3 (DI = 1), 39430, 3957C */
                c->wizard = 1;
                return fight_end(c, &r, boundary);
            }
            highlight(&r, m->ds[XEEN_DS_TURN_ACTOR]);
            break;
        case XEEN_COMBAT_MODAL_INFO:
        case XEEN_COMBAT_MODAL_QUICK_REFERENCE:
        case XEEN_COMBAT_MODAL_OPTIONS: /* back to the same member: re-highlighted. Original: 392D9 */
            highlight(&r, m->ds[XEEN_DS_TURN_ACTOR]);
            break;
        case XEEN_COMBAT_MODAL_VIEW_MEMBER: /* the same, and the combat buttons back. Original: 3933F */
            highlight(&r, m->ds[XEEN_DS_TURN_ACTOR]);
            xeen_combat_buttons(m, 1);
            break;
        default:
            return XEEN_BAD_INPUT;
    }
    return round_end(c, &r, boundary);
}

/*
 * The round charge's 2FC7E returned to its caller, at 393D8 or 393F5 (after a sky reload or a rest's clock stop;
 * last_boundary cleared first). From the round reset (393D3, sky_site TIME_CHARGE_ROUND_RESET) the round goes on at
 * 393D9 with the attackers still there: without the random or audio service the round's resume boundary is reported,
 * else the next round and its continuation (round_outcome). From the no-attackers time charge (393F5) the scene pass
 * runs (a live contact may enter, reported as the command poll; its stop is reported as it is); attackers that
 * appeared again stop at 3912C (last_boundary, with the command poll in *BOUNDARY); none left end the fight
 * (fight_end).
 */
static xeen_status charge_returned(run *r, uint32_t *boundary)
{
    xeen_combat *c = r->c;
    xeen_movement *m = r->m;
    c->last_boundary = 0;
    if (c->sky_site == TIME_CHARGE_ROUND_RESET) {
        /* The round reset's 2FC7E (393D3) reached the sky reload;
         * its caller continues at 393D9 with the attackers still there. */
        if (!r->random || (m->ds[XEEN_DS_SOUND_ON] && !r->audio)) {
            c->last_boundary = XEEN_COMBAT_ROUND_RESUME;
            *boundary = XEEN_COMBAT_ROUND_RESUME;
            return XEEN_SERVICE_REQUIRED;
        }
        ++c->rounds;
        c->phase = XC_AWAITING_COMMAND;
        *boundary = continuation_tail(r);
        return round_outcome(c, r, boundary);
    }
    {
        int ok;
        c->live_contact_site = 1;
        ok = scene(r);
        c->live_contact_site = 0;
        if (!ok) {
            return report_stop(c, r, boundary);
        }
    }
    if (c->live_entered) {
        c->live_entered = 0;
        *boundary = XEEN_COMBAT_COMMAND_POLL;
        return XEEN_SERVICE_REQUIRED;
    }
    if (attacker_count(m)) {
        c->phase = XC_UNSUPPORTED_CONTINUATION;
        c->last_boundary = CHARGE_STOP_ATTACKERS_AGAIN;
        *boundary = XEEN_COMBAT_COMMAND_POLL;
        return XEEN_SERVICE_REQUIRED;
    }
    return fight_end(c, r, boundary);
}
xeen_status xeen_combat_resume_outer(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                                     xeen_entity_random_fn random, void *user,
                                     xeen_native_audio *audio, uint32_t *boundary)
{
    run r;
    if (!c || !m || !db || !random || !boundary || !c->outer_active || c->phase != XC_INACTIVE) {
        return XEEN_BAD_INPUT;
    }
    r = (run){c, m, db, n, random, user, audio, 0};
    /* RETF 393F5 (393FD): the innermost suspended 38F5F frame again
     * ([bp-6], [bp-4]); one level per return. */
    --c->outer_active;
    c->entry_world = c->outer_world[c->outer_active];
    c->entry_3066 = c->outer_3066[c->outer_active];
    c->phase = XC_AWAITING_COMMAND;
    c->last_boundary = 0;
    xeen_presentation_trace_extra("mark combat-outer-resume");
    *boundary = attacker_count(m) ? finish_round(&r) : combat_over(&r);
    return round_outcome(c, &r, boundary);
}
xeen_status xeen_combat_after_sky(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                                  uint32_t *boundary)
{
    return xeen_combat_after_sky_rng(c, m, db, n, NULL, NULL, NULL, boundary);
}
xeen_status xeen_combat_after_sky_rng(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                                      xeen_entity_random_fn random, void *user,
                                      xeen_native_audio *audio, uint32_t *boundary)
{
    run r;
    uint16_t minutes, night;
    if (!c || !m || !db || !boundary || c->phase != XC_UNSUPPORTED_CONTINUATION ||
        c->last_boundary != XEEN_TICK_SKY_RELOAD) {
        return XEEN_BAD_INPUT;
    }
    minutes = xeen_read16(m, XEEN_DS_MINUTE_OF_DAY);
    night = (minutes < XEEN_NIGHT_ENDS || minutes >= XEEN_NIGHT_BEGINS) ? 1u : 0u;
    if (xeen_read16(m, XEEN_DS_NIGHT) != night) {
        *boundary = COMBAT_STOP_SKY_RELOAD;
        return XEEN_SERVICE_REQUIRED;
    }
    r = (run){c, m, db, n, random, user, audio, 0};
    return charge_returned(&r, boundary);
}
/* 30DB3's 18F2F dismissed: 30DC2.. the rest of the round charge,
 * then its caller as after the sky reload. */
xeen_status xeen_combat_after_rest_rng(xeen_combat *c, xeen_movement *m, const uint8_t *db,
                                       size_t n, xeen_entity_random_fn random, void *user,
                                       xeen_native_audio *audio, uint32_t *boundary)
{
    run r;
    xeen_gameplay_tick t;
    if (!c || !m || !db || !boundary || c->phase != XC_UNSUPPORTED_CONTINUATION ||
        c->last_boundary != XEEN_TICK_UNFED_DAWN || c->rest_tick.boundary != XEEN_TICK_UNFED_DAWN) {
        return XEEN_BAD_INPUT;
    }
    r = (run){c, m, db, n, random, user, audio, 0};
    t = xeen_gameplay_time_conditions_resume_rest(m, c->rest_tick, random ? tick_draw : NULL, &r);
    if (t.dead == 2u) {
        party_bar(&r); /* 2FFE1 2ABB8(1) */
    }
    c->rest_tick.boundary = 0;
    if (t.boundary) {
        c->last_boundary = t.boundary;
        *boundary = t.boundary;
        return XEEN_SERVICE_REQUIRED;
    } /* 30004: the sky */
    return charge_returned(&r, boundary);
}

/* 4128F calls 38049 with a DATABASE ID while 97F6 is set. The original 38058
 * branch skips melee runtime selection and animation, then uses exactly the
 * shared 38158 target/hit/damage loop; it does not consume initiative. */
xeen_status xeen_combat_ranged_action(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                                      unsigned id, xeen_entity_random_fn random, void *user,
                                      xeen_native_audio *audio, uint32_t *boundary)
{
    run r = {c, m, db, n, random, user, audio, 0};
    unsigned i;
    if (!c || !m || !db || !boundary || !random || !m->ds[XEEN_DS_MONSTER_SHOTS_QUEUED]) {
        return XEEN_BAD_INPUT;
    }
    *boundary = 0;
    if (!m->ds[XEEN_DS_COMBAT_PARTY_COUNT] || m->ds[XEEN_DS_COMBAT_PARTY_COUNT] > 8u) {
        return XEEN_BAD_INPUT;
    }
    for (i = 0; i < m->ds[XEEN_DS_COMBAT_PARTY_COUNT]; ++i) {
        if (m->ds[XEEN_DS_COMBAT_PARTY + i] > 5u) {
            return XEEN_BAD_INPUT;
        }
    }
    if (!c->rng_draws) {
        c->rng_trace_complete = 1;
    }
    if (!monster_turn(&r, id, 0u)) {
        return report_stop(c, &r, boundary);
    }
    return XEEN_OK;
}

xeen_status xeen_combat_ranged_complete(xeen_movement *m, uint32_t *boundary)
{
    unsigned i;
    if (!m || !boundary) {
        return XEEN_BAD_INPUT;
    }
    *boundary = 0;
    m->ds[XEEN_DS_MONSTER_SHOTS_QUEUED] = 0;
    m->ds[0x97f8] = 0;
    party_check(m);
    /* 4152D: a sleeping party (mode 5) wakes (mode 1) at the first member
     * whose 2AB96 worst condition is not asleep or 11..15; depressed,
     * confused, conditions 0..7 and none all wake it. */
    if (m->ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_REST) {
        for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
            if (!xeen_condition_cannot_act(
                        xeen_character_worst_condition(m->ds + XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES))) {
                m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_EXPLORATION;
                break;
            }
        }
    }
    return XEEN_OK;
}
void xeen_combat_party_check(xeen_movement *m)
{
    if (m) {
        party_check(m);
    }
}

/* The members a party damage strikes, into MEMBERS (*COUNT of them), by the member selector: 2 strikes the first
 * two able members (worst condition not unconscious, dead, stone or eradicated) in party order; any other nonzero
 * value strikes TARGET only; zero strikes every member. With fewer than two able members the two-member form keeps
 * an original quirk: it strikes from the member after the first able one (from the first member when none is able)
 * up to TARGET, so the able member itself is skipped. Returns XEEN_OK, or XEEN_BAD_INPUT with *BOUNDARY when a
 * member would lie past the party or the six slots.
 * Original: 2E68D (98D7 the selector); the scan 2E6B6 (2AB96; 2E6F5: BP-6 = the second + 1, SI the first), the
 * damage order 2E89D; with fewer than two, 2E883 strikes SI, SI + 1, .. while target + 1 is greater (signed); the
 * bound 2E72B. */
static xeen_status damage_targets(const xeen_movement *m, unsigned target, unsigned members[6],
                                  unsigned *count, uint32_t *boundary)
{
    unsigned k;
    if (m->ds[XEEN_DS_MEMBER_SELECTOR] == 2u) {
        /* after_first: the member after the first able one (SI: the first + 1; 0 while none is able); second: the
         * second able member + 1 (BP-6; 0 while there is none) */
        unsigned after_first = 0, second = 0;
        int limit = (int)(int16_t)(uint16_t)(target + 1u);
        for (k = 0; k < m->ds[XEEN_DS_PARTY_COUNT]; ++k) {
            uint8_t w = xeen_character_worst_condition(m->ds + XEEN_PARTY_RECORDS +
                                                       k * XEEN_CHARACTER_BYTES);
            if (w >= XEEN_CONDITION_UNCONSCIOUS && w <= XEEN_CONDITION_ERADICATED) {
                continue;
            }
            if (!after_first) {
                after_first = k + 1u;
                continue;
            }
            second = k + 1u;
            break;
        }
        if (second) {
            members[0] = after_first - 1u;
            members[1] = second - 1u;
            *count = 2;
            return XEEN_OK;
        }
        /* the quirk: from the member after the first able one (or the first member) while target + 1 is greater */
        for (k = after_first; limit > (int)k; ++k) {
            if (*count == 6u || k >= m->ds[XEEN_DS_PARTY_COUNT]) {
                *boundary = PARTY_DAMAGE_TARGETS_PAST_PARTY;
                return XEEN_BAD_INPUT;
            }
            members[(*count)++] = k;
        }
        return XEEN_OK;
    }
    {
        unsigned last = m->ds[XEEN_DS_MEMBER_SELECTOR] ? target + 1u : m->ds[XEEN_DS_PARTY_COUNT];
        if (last > 6u || last > m->ds[XEEN_DS_PARTY_COUNT]) {
            *boundary = PARTY_DAMAGE_TARGETS_PAST_PARTY;
            return XEEN_BAD_INPUT;
        }
        for (k = m->ds[XEEN_DS_MEMBER_SELECTOR] ? target : 0u; k < last; ++k) {
            members[(*count)++] = k;
        }
    }
    return XEEN_OK;
}
/* Damages member I: sleep cleared, the party's protection against the type subtracted, the halving saves, the type's
 * effect; with a presentation the hit's frame over the portrait and the party bar shown; Power Shield; type 7 sets
 * the member's HP and death; then the member's HP check. Returns 0 at a stop (r->boundary), else 1.
 * Original: 2E720..2E8B0; the frame 2E7FE (98CE, the type's frame, 17B2[member], 96h), 50BF6(21h); 2A152. */
static int damage_member(run *r, unsigned i, uint32_t damage, unsigned type, xeen_presentation *p)
{
    xeen_movement *m = r->m;
    /* Each member's damage starts from the amount's low word (an original quirk: a negative amount, which event
     * operation 52 can pass, becomes a large one, and the 16-bit HP subtraction then adds that much: -48 heals 48).
     * Original: 2E6A3/2E720; op 52 passes CBW(8C81); 2A152. */
    uint8_t *ch = m->ds + XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES;
    int32_t value = (int32_t)(uint16_t)damage;
    ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_ASLEEP)] = 0;
    if (damage_types[type].protection) { /* the word unsigned here (37B6D takes it signed) */
        value -= (int32_t)xeen_read16(m, damage_types[type].protection);
    }
    if (type) {
        do { /* halve while the save of this type succeeds. Original: 2E7CA */
            if (!saving_throw(r, ch, type)) {
                break;
            }
            value >>= 1;
        } while (value);
    }
    if (!play_effect(r, damage_types[type].sound)) {
        return 0;
    }
    if (p) {
        portrait_hit(p, m, i, damage_types[type].frame);
    }
    if (m->ds[XEEN_DS_EFFECT_POWER_SHIELD]) {
        value -= m->ds[XEEN_DS_EFFECT_POWER_SHIELD];
    }
    if (value < 0) {
        value = 0;
    }
    if (type == DAMAGE_TYPE_DEATH) {
        value = xeen_sign16(xeen_get_le16(ch + XEEN_CHARACTER_HP));
        ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_DEAD)] = 1;
    }
    if (!subtract_hp(r, ch, (uint16_t)value)) {
        return 0;
    }
    r->c->last_damage = (uint16_t)value;
    return 1;
}
/* 2E68D up to its 2E8B3 30E2D(2): the members (damage_targets), 186C0, then each member's damage
 * (damage_member) in order. A stop inside a member's damage is reported through *BOUNDARY and the combat's
 * last boundary. */
static xeen_status party_damage_members(run *r, uint32_t damage, unsigned type, unsigned target,
                                        xeen_presentation *p, uint32_t *boundary)
{
    xeen_combat *c = r->c;
    xeen_movement *m = r->m;
    unsigned k, members[6], count = 0;
    xeen_status status;
    *boundary = 0;
    if (type >= DAMAGE_TYPES) {
        *boundary = PARTY_DAMAGE_TYPE_UNKNOWN;
        return XEEN_SERVICE_REQUIRED;
    }
    if ((status = damage_targets(m, target, members, &count, boundary)) != XEEN_OK) {
        return status;
    }
    xeen_window_close_all(m);
    if (!c->rng_draws) {
        c->rng_trace_complete = 1;
    }
    for (k = 0; k < count; ++k) {
        if (!damage_member(r, members[k], damage, type, p)) {
            return report_stop(c, r, boundary);
        }
    }
    return XEEN_OK;
}
xeen_status xeen_combat_party_damage(xeen_combat *c, xeen_movement *m, const uint8_t *db, size_t n,
                                     uint32_t damage, unsigned type, unsigned target,
                                     xeen_entity_random_fn random, void *user,
                                     xeen_native_audio *audio, uint32_t *boundary)
{
    run r = {c, m, db, n, random, user, audio, 0};
    xeen_status status;
    if (!c || !m || !db || !random || !boundary) {
        return XEEN_BAD_INPUT;
    }
    if ((status = party_damage_members(&r, damage, type, target, NULL, boundary)) != XEEN_OK) {
        return status;
    }
    /* 30E2D(2): the timed wait redraws the scene until two ticks elapse;
     * the deterministic native policy is one completed frame. Its mode-1
     * contact test is not part of the semantic scene: stop if it would fire. */
    scene_site = 0x30e3cu;
    if (!scene(&r)) {
        return report_stop(c, &r, boundary);
    }
    if (xeen_scene_contact_due(m->ds)) {
        *boundary = PARTY_DAMAGE_CONTACT_FIGHT;
        c->last_boundary = *boundary;
        return XEEN_SERVICE_REQUIRED;
    }
    /* 2ABB8(1): portraits, presentation. */
    party_check(m);
    return XEEN_OK;
}
xeen_status xeen_combat_party_damage_hits(xeen_combat *c, xeen_movement *m, const uint8_t *db,
                                          size_t n, uint32_t damage, unsigned type, unsigned target,
                                          xeen_entity_random_fn random, void *user,
                                          xeen_native_audio *audio, xeen_presentation *presentation,
                                          uint32_t *boundary)
{
    run r = {c, m, db, n, random, user, audio, 0};
    if (!c || !m || !db || !random || !boundary) {
        return XEEN_BAD_INPUT;
    }
    return party_damage_members(&r, damage, type, target, presentation, boundary);
}
