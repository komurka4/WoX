#include "xeen/town.h"
#include "xeen/ds_globals.h"
#include "xeen/combat.h"
#include "xeen/character_record.h"
#include "xeen/addon/damage_numbers.h"
#include "xeen/byte_order.h"
#include <stdio.h>
#include <string.h>
/* CWD: a DS word sign-extended to 32 bits. */
static uint32_t cwd32(const xeen_movement *m, uint16_t a)
{
    uint16_t w = xeen_read16(m, a);
    return (w & 0x8000u) ? 0xffff0000u | w : (uint32_t)w;
}
/* 107F9 (a long shift left) of the sign-extended class base. */
static uint32_t base_shifted(const xeen_movement *m, const uint8_t *member, int shift)
{
    if (shift < 0) {
        return 0;
    }
    return cwd32(m, (uint16_t)(XEEN_DS_CLASS_EXPERIENCE_BASE + 2u * member[XEEN_CHARACTER_CLASS])) << shift;
}
/* The experience that LEVEL takes: the class base shifted by LEVEL - 1 below 12 (none below 1), from 12 on the
 * level-11 base and 1,024,000 more for each level past 11 (32-bit, wrapping as the original's). */
static uint32_t threshold_at(const xeen_movement *m, const uint8_t *member, int level)
{
    if (level < 12) {
        return base_shifted(m, member, level - 1);
    }
    return (uint32_t)(level - 11) * 1024000u + base_shifted(m, member, 10);
}
uint32_t xeen_level_threshold(const xeen_movement *m, const uint8_t *member)
{
    return threshold_at(m, member, member[XEEN_CHARACTER_LEVEL]);
}
uint32_t xeen_level_experience(const xeen_movement *m, const uint8_t *member)
{
    return threshold_at(m, member, (int)member[XEEN_CHARACTER_LEVEL] - 1) +
           xeen_get_le32(member + XEEN_CHARACTER_EXPERIENCE);
}
uint32_t xeen_experience_needed(const xeen_movement *m, const uint8_t *member)
{
    uint32_t d = xeen_level_threshold(m, member) - xeen_level_experience(m, member);
    return (d & 0x80000000u) || !d ? 0 : d;
}
uint8_t xeen_training_top_level(uint8_t world, uint8_t map)
{
    if (world) {
        return map == 29u ? 0x1e : map == 31u ? 0x32 : map == 37u ? 0xc8 : 0x64;
    }
    return map == 28u ? 0x0a : map == 30u ? 0x0f : 0x14;
}
uint32_t xeen_training_cost(const uint8_t *member)
{
    uint32_t l = member[XEEN_CHARACTER_LEVEL];
    return l * l * 10u;
}
int xeen_training_refused(const uint8_t *member)
{
    return xeen_condition_cannot_act(xeen_character_worst_condition(member));
}
void xeen_train_level(const xeen_movement *m, uint8_t *member)
{
    uint32_t exp = xeen_get_le32(member + XEEN_CHARACTER_EXPERIENCE);
    uint32_t step = xeen_level_threshold(m, member) - (xeen_level_experience(m, member) - exp);
    xeen_put_le32(member + XEEN_CHARACTER_EXPERIENCE, exp - step);
    member[XEEN_CHARACTER_LEVEL] = (uint8_t)(member[XEEN_CHARACTER_LEVEL] + 1u);
}
/* The temple price bases of the town at MAP (in BASES' order below), or NULL for a map without a temple set, where the
 * previous town's bases stay (a Clouds map only: every Darkside map has one, the high set by default).
 * Original: 273F3's four cases by map and world; 2738B the Clouds' no-match. */
static const uint16_t *temple_bases(unsigned on_darkside, uint8_t map)
{
    static const uint16_t none[5] = {0, 0, 0, 0, 10}, low[5] = {100, 500, 50, 10, 25},
                          mid[5] = {200, 1000, 100, 20, 50}, high[5] = {300, 5000, 500, 100, 100};
    static const struct {
        uint8_t map;
        const uint16_t *bases;
    } clouds_towns[4] = {{28, none}, {30, low}, {49, high}, {73, mid}},
      darkside_towns[3] = {{29, none}, {31, low}, {37, mid}};
    unsigned i;
    if (on_darkside) {
        for (i = 0; i < 3u; ++i) {
            if (darkside_towns[i].map == map) {
                return darkside_towns[i].bases;
            }
        }
        return high;
    }
    for (i = 0; i < 4u; ++i) {
        if (clouds_towns[i].map == map) {
            return clouds_towns[i].bases;
        }
    }
    return NULL;
}
/* The state byte of the member's item row K of category CAT (XEEN_ITEMS_WEAPONS..XEEN_ITEMS_MISC). */
static unsigned item_state_at(unsigned cat, unsigned k)
{
    return XEEN_CHARACTER_ITEM(cat, k) + XEEN_ITEM_ROW_STATE;
}
/* A cursed item (state bit XEEN_ITEM_CURSED) in any of the member's 36 rows (9 of each of the four categories). */
static int has_cursed_item(const uint8_t *member)
{
    unsigned k, cat;
    for (k = 0; k < XEEN_ITEM_ROWS; ++k) {
        for (cat = XEEN_ITEMS_WEAPONS; cat <= XEEN_ITEMS_MISC; ++cat) {
            if (member[item_state_at(cat, k)] & XEEN_ITEM_CURSED) {
                return 1;
            }
        }
    }
    return 0;
}
/**
 * Computes the temple's prices for MEMBER in the current town into OUT, as dwords in DS too (F632..F642, the five
 * prices; F62E the level). The town's bases (temple_bases) and the member's level L (with its temporary part) give:
 * - Heal: L x 10 + the heal base when the HP is below the maximum, plus L x 10 for each condition from Heart Broken to
 *   Unconscious, plus the two prices below (the total is OUT->heal and DS F642);
 * - raising the Dead and the Stoned: for each, L x 100 + its count x 50 + the resurrection base;
 * - the Eradicated: L x 1000 + its count x 500 + the eradication base;
 * - Uncurse: L x 20 + the uncurse base with a cursed item or the Cursed condition;
 * - the donation: the donation base, or 0 once the temple BLESSED the party this visit.
 * The bases are signed words (cwd32). Original: 273F3 (2730E's prices for the temple text).
 */
void xeen_temple_prices_set(xeen_movement *m, const uint8_t *member, int blessed,
                            xeen_temple_prices *out)
{
    static const uint16_t bases[5] = {XEEN_DS_TEMPLE_UNCURSE_BASE, XEEN_DS_TEMPLE_ERADICATION_BASE,
                                      XEEN_DS_TEMPLE_RESURRECTION_BASE, XEEN_DS_TEMPLE_HEAL_BASE,
                                      XEEN_DS_TEMPLE_DONATION_BASE};
    const uint16_t *set = temple_bases(m->ds[XEEN_DS_WORLD] != 0, m->ds[XEEN_DS_PARTY_MAP]);
    uint32_t level, heal = 0, dead = 0, erad = 0, curse = 0;
    unsigned k;
    xeen_ds_write32(m, XEEN_DS_TEMPLE_DONATION_PRICE, 0);
    xeen_ds_write32(m, XEEN_DS_TEMPLE_UNCURSE_PRICE, 0);
    xeen_ds_write32(m, XEEN_DS_TEMPLE_ERADICATION_PRICE, 0);
    xeen_ds_write32(m, XEEN_DS_TEMPLE_RESURRECTION_PRICE, 0);
    xeen_ds_write32(m, 0xf642, 0);
    if (set) {
        for (k = 0; k < 5u; ++k) {
            xeen_write16(m, bases[k], set[k]);
        }
    }
    level = xeen_character_current_level(member);
    xeen_ds_write32(m, XEEN_DS_TEMPLE_LEVEL, level);
    if (xeen_combat_max_hp(m, member) > (uint16_t)xeen_get_le16(member + XEEN_CHARACTER_HP)) {
        heal = level * 10u + cwd32(m, XEEN_DS_TEMPLE_HEAL_BASE);
    }
    for (k = XEEN_CONDITION_HEART_BROKEN; k <= XEEN_CONDITION_UNCONSCIOUS; ++k) {
        if (member[XEEN_CHARACTER_CONDITIONS + k]) {
            heal += level * 10u;
        }
    }
    for (k = 0; k < 2u; ++k) { /* Dead, then Stone */
        if (member[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_DEAD) + k]) {
            dead += level * 100u + (uint32_t)member[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_DEAD) + k] * 50u +
                    cwd32(m, XEEN_DS_TEMPLE_RESURRECTION_BASE);
        }
    }
    xeen_ds_write32(m, XEEN_DS_TEMPLE_RESURRECTION_PRICE, dead);
    if (member[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_ERADICATED)]) {
        erad = level * 1000u + (uint32_t)member[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_ERADICATED)] * 500u +
               cwd32(m, XEEN_DS_TEMPLE_ERADICATION_BASE);
    }
    xeen_ds_write32(m, XEEN_DS_TEMPLE_ERADICATION_PRICE, erad);
    if (has_cursed_item(member) || member[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_CURSED)]) {
        curse = level * 20u + cwd32(m, XEEN_DS_TEMPLE_UNCURSE_BASE);
    }
    xeen_ds_write32(m, XEEN_DS_TEMPLE_UNCURSE_PRICE, curse);
    out->donation = blessed ? 0 : cwd32(m, XEEN_DS_TEMPLE_DONATION_BASE);
    xeen_ds_write32(m, XEEN_DS_TEMPLE_DONATION_PRICE, out->donation);
    out->dead_stoned = dead;
    out->eradicated = erad;
    out->uncurse = curse;
    out->heal = heal + dead + erad;
    xeen_ds_write32(m, 0xf642, out->heal);
}
void xeen_temple_amount(char *out, unsigned long size, uint32_t value)
{
    if (value > 999u) {
        (void)snprintf(out, size, "%uk", (unsigned)(uint16_t)(value / 1000u));
    } else {
        (void)snprintf(out, size, "%u", (unsigned)(uint16_t)value);
    }
}
void xeen_temple_heal(const xeen_movement *m, uint8_t *member)
{
    unsigned k;
    uint16_t hp;
    uint8_t cursed;
    for (k = 0; k < XEEN_RESISTANCE_COUNT; ++k) {
        member[XEEN_CHARACTER_RESISTANCE_TEMPORARY(k)] = 0;
    }
    member[XEEN_CHARACTER_AC_BONUS] = member[XEEN_CHARACTER_LEVEL_BONUS] = 0;
    for (k = 0; k < XEEN_STAT_COUNT; ++k) {
        member[XEEN_CHARACTER_STAT_TEMPORARY(k)] = 0;
    }
    XEEN_DAMAGE_NUMBERS(
            hp_ptr_restore(m->ds, member, xeen_combat_max_hp(m, member))); /* add-on hook */
    hp = xeen_combat_max_hp(m, member);
    xeen_put_le16(member + XEEN_CHARACTER_HP, hp);
    cursed = member[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_CURSED)];
    memset(member + XEEN_CHARACTER_CONDITIONS, 0, XEEN_CONDITION_COUNT); /* all but the curse, kept */
    member[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_CURSED)] = cursed;
}
void xeen_temple_uncurse(uint8_t *member)
{
    unsigned k;
    member[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_CURSED)] = 0;
    for (k = 0; k < XEEN_ITEM_ROWS; ++k) { /* every item's curse bit */
        unsigned cat;
        for (cat = XEEN_ITEMS_WEAPONS; cat <= XEEN_ITEMS_MISC; ++cat) {
            member[item_state_at(cat, k)] &= (uint8_t)~XEEN_ITEM_CURSED;
        }
    }
}
