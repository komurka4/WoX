/*
 * A statistic's details and the awards list (Phase 248, from game_view.c): the character view's 428F1 window of a
 * statistic (its lines, the skills and conditions lists, the age), with the character_details add-on's hooks, and
 * the 41581 awards list the award row opens (its entries, its text, its keys). The view itself (44AE6) and its
 * Exchange are game_view.c's.
 */
#include "game_items.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when the character details or the awards cannot be shown; each value is the original
 * address of the step (tools/xeen_replay/stops.json). The line and table stops guard writes the original makes past
 * its buffers.
 */
enum {
    AWARDS_STOP_WINS_TEXT = 0x4171f,         /* the Warzone wins award's text has no end within 28 bytes */
    AWARDS_STOP_RACE_TEXT = 0x4178e,         /* the race award's text has no end within 28 bytes */
    AWARDS_STOP_WINS_LINE = 0x4173f,         /* the Warzone wins line would not fit its 90 bytes */
    AWARDS_STOP_RACE_LINE = 0x417b8,         /* the race award's line would not fit its 90 bytes */
    DETAILS_STOP_SKILL_LINE = 0x42cb2,       /* a 30-byte skill line overflows */
    DETAILS_STOP_CONDITION_LINE = 0x42f4f,
    DETAILS_STOP_WINDOW = 0x43157,           /* window 28 at the stat's size */
    DETAILS_STOP_STAT_TABLE = 0x4296f,       /* past the last word of the stat table (DS:2DA8) */
    DETAILS_STOP_FOOD_NO_PARTY = 0x42ec4,    /* the days of food divide by a party count of 0 */
    DETAILS_STOP_TEXT = 0x4314b,             /* the window's text does not fit */
    AWARDS_STOP_SPRITES = 0x41590,           /* award.icn */
    AWARDS_STOP_NO_MEMORY = 0x4160c,
    AWARDS_STOP_ENTRY = 0x416cd,             /* an award offset the text block never wrote */
    AWARDS_STOP_TOP_LINE = 0x41827,          /* the first line past the list */
    AWARDS_STOP_TEXT = 0x418f6
};
static xeen_status view_after_details(xeen_game *);
/* 428F1's statistic (the view's F5FE - 61h, CS:08C6's cases, 2F04's titles and the 2D80/2D94 window
 * tables): 0..6 are the attributes (XEEN_STAT_*), the rest as each case's own reads show. */
enum details_stat {
    DETAILS_AGE = 7,
    DETAILS_LEVEL = 8,
    DETAILS_ARMOR_CLASS = 9,
    DETAILS_HP = 10,
    DETAILS_SP = 11,
    DETAILS_RESISTANCES = 12,
    DETAILS_SKILLS = 13,
    DETAILS_AWARDS = 14,
    DETAILS_EXPERIENCE = 15,
    DETAILS_GOLD = 16,
    DETAILS_GEMS = 17,
    DETAILS_FOOD = 18,
    DETAILS_CONDITIONS = 19
};
/* Formats one 30-byte line of a list detail (the skills, the conditions) into SLOT; 0 when it would not fit. */
static int details_line(char *slot, const char *format, ...)
{
    va_list a;
    int n;
    va_start(a, format);
    n = vsnprintf(slot, 30, format, a);
    va_end(a);
    return n >= 0 && n < 30;
}
static xeen_status awards_begin(xeen_game *, uint16_t);
/*
 * The skills detail: a line for each skill CH has (Thievery's with its value, xeen_combat_thievery), shown in
 * the page's alphabetical order (skills 0..5, 17, 6..13, 16, 14, 15); without any, "None" on the first line.
 * *LINES is the number of lines (at least 1), *N the page text's snprintf result in OUT.
 * Stops: a skill line that does not fit its 30 bytes (the skill line stop).
 * Original: 42C56 (the order DS 7918, the line "\n\t020%s", Thievery's value from 3CF5B).
 */
static xeen_status details_skills(xeen_game *g, const uint8_t *ch, const char *title, char *out,
                                  size_t size, int *n, unsigned *lines)
{
    static const uint8_t order[18] = {0, 1, 2, 3, 4, 5, 17, 6, 7, 8, 9, 10, 11, 12, 13, 16, 14, 15};
    char slot[18][30];
    const char *l[18];
    unsigned i;
    for (i = 0; i < 18u; ++i) {
        slot[i][0] = 0;
    }
    *lines = 0;
    for (i = 0; i < 18u; ++i) {
        if (ch[XEEN_CHARACTER_SKILLS + i]) {
            ++*lines;
        }
    }
    if (!*lines) {
        (void)details_line(slot[0], "%s", ds_text(g, XEEN_TEXT_DETAILS_NO_SKILLS));
        *lines = 1;
    } else {
        for (i = 0; i < 18u; ++i) {
            if (!ch[XEEN_CHARACTER_SKILLS + i]) {
                continue;
            }
            if (!(i ? details_line(slot[i], ds_text(g, XEEN_TEXT_DETAILS_LIST_LINE), far_text(g, XEEN_DS_SKILL_NAMES, i))
                    : details_line(slot[i], ds_text(g, XEEN_TEXT_DETAILS_SKILL_VALUE), far_text(g, XEEN_DS_SKILL_NAMES, i),
                                   (unsigned)(uint16_t)xeen_combat_thievery(&g->movement, ch)))) {
                return shop_stop(g, DETAILS_STOP_SKILL_LINE);
            }
        }
    }
    for (i = 0; i < 18u; ++i) {
        l[i] = slot[order[i]];
    }
    *n = snprintf(out, size, ds_text(g, XEEN_TEXT_DETAILS_SKILLS_PAGE), title, l[0], l[1], l[2], l[3], l[4], l[5], l[6],
                  l[7], l[8], l[9], l[10], l[11], l[12], l[13], l[14], l[15], l[16], l[17]);
    return XEEN_OK;
}
/*
 * The conditions detail: each condition CH has on its own line (0..15; the first twelve with their value), "Good"
 * on line 0 when the character has none (the worst condition is 16), then the party's Bless, Power Shield, Holy
 * Bonus and Heroism with their "%+d" values on lines 16..19 when active. *LINES counts the filled lines (at
 * least 1), *N is the page text's snprintf result in OUT.
 * Stops: a condition line that does not fit its 30 bytes (the condition line stop).
 * Original: 42F0B (the worst condition 2AB96; the effects EDA3..EDA6; the page DS 7A41).
 */
static xeen_status details_conditions(xeen_game *g, const uint8_t *ch, const char *title, char *out,
                                      size_t size, int *n, unsigned *lines)
{
    static const uint16_t effect[4] = {XEEN_TEXT_DETAILS_BLESSED, XEEN_TEXT_DETAILS_POWER_SHIELD, XEEN_TEXT_DETAILS_HOLY_BONUS, XEEN_TEXT_DETAILS_HEROISM};
    const uint8_t *ds = g->movement.ds;
    char slot[20][30];
    const char *l[20];
    unsigned i;
    for (i = 0; i < 20u; ++i) {
        slot[i][0] = 0;
    }
    *lines = 0;
    for (i = 0; i < 16u; ++i) {
        uint8_t v = ch[XEEN_CHARACTER_CONDITIONS + i];
        if (!v) {
            continue;
        }
        if (!(i < 12u ? details_line(slot[i], ds_text(g, XEEN_TEXT_DETAILS_CONDITION_VALUE), far_text(g, XEEN_DS_CONDITION_NAMES, i), (int)v)
                      : details_line(slot[i], ds_text(g, XEEN_TEXT_DETAILS_LIST_LINE), far_text(g, XEEN_DS_CONDITION_NAMES, i)))) {
            return shop_stop(g, DETAILS_STOP_CONDITION_LINE);
        }
        ++*lines;
    }
    if (xeen_character_worst_condition(ch) == 16u) {
        (void)details_line(slot[0], ds_text(g, XEEN_TEXT_DETAILS_LIST_LINE), far_text(g, XEEN_DS_CONDITION_NAMES, 16));
        ++*lines;
    }
    for (i = 0; i < 4u; ++i) {
        slot[16u + i][0] = 0;
        if (ds[XEEN_DS_EFFECT_BLESS + i]) {
            (void)details_line(slot[16u + i], ds_text(g, effect[i]),
                               (int)ds[XEEN_DS_EFFECT_BLESS + i]);
            ++*lines;
        }
    }
    for (i = 0; i < 20u; ++i) {
        l[i] = slot[i];
    }
    *n = snprintf(out, size, ds_text(g, XEEN_TEXT_DETAILS_CONDITIONS_PAGE), title, l[0], l[1], l[2], l[3], l[4], l[5], l[6],
                  l[7], l[8], l[9], l[10], l[11], l[12], l[13], l[14], l[15], l[16], l[17], l[18],
                  l[19]);
    return XEEN_OK;
}
/*
 * Shows the formatted detail (DS text buffer 2) in the defined window at the statistic's place (the DS 2D80 x and
 * 2D94 y tables by STAT), 143 pixels wide and HEIGHT high. A list (skills, conditions) is instead 9 per line +
 * 26 high, raised by 8 for every two lines past the first, and raised further so it ends by row 199. Then the
 * default buttons and the details poll (a key closes it, details_end; the character_details add-on may take a
 * click first).
 * Stops: the window cannot be defined at that size (the window stop).
 * Original: 50B45, 1871B, 4E38C, 50BF6 (the list heights 42DCC, 43126); the poll 43183.
 */
static xeen_status details_window(xeen_game *g, unsigned stat, unsigned height, unsigned lines)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    int y = ds[(uint16_t)(XEEN_DS_STAT_WINDOW_Y + stat)];
    if (stat == DETAILS_SKILLS || stat == DETAILS_CONDITIONS) {
        height = lines * 9u + 26u;
        y -= (((int)lines - 1) >> 1) * 8;
        if (y + (int)height > 199) { /* kept above the screen's last row */
            y = 199 - (int)height;
        }
    }
    if (xeen_presentation_define_window(g->presentation, XEEN_WINDOW_DEFINED, ds[(uint16_t)(XEEN_DS_STAT_WINDOW_X + stat)],
                                        (uint16_t)y, (uint16_t)height, 143) != XEEN_OK) {
        return shop_stop(g, DETAILS_STOP_WINDOW);
    }
    panel_window(g, XEEN_WINDOW_DEFINED, 1);
    menu_window_text(g, XEEN_WINDOW_DEFINED, WINDOW_FRAMED, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_DEFINED);
    button_defaults(m);
    return panel_set(g, DETAILS_POLL, DETAILS_POLL_BOUNDARY);
}
/*
 * The texts of 428F1's statistics. Each formats the detail of member CH, titled TITLE (the statistic's name), into
 * OUT (SIZE bytes) from its DS format and returns snprintf's result; details_begin checks that it fits. Every value
 * is read from the member's record and the party's DS words; none of them writes the member or the party.
 * Original: 428F1's cases (CS:08C6), each a sprintf into AC0F's buffer.
 */
/* The 24 words of the attribute thresholds (DS 2DA8): an attribute's rating is the first threshold above it. */
#define ATTRIBUTE_RATINGS 24u
/* The rating of attribute value VALUE: the index of the first threshold above it, or ATTRIBUTE_RATINGS when the
 * value reaches every threshold (the original would then read past the table: details_begin stops). The thresholds
 * are read in order, up to that first one. Original: 428F1's rating loop (its stop 4296F). */
static unsigned attribute_rating(const xeen_movement *m, uint16_t value)
{
    unsigned k = 0;
    while (k < ATTRIBUTE_RATINGS && xeen_read16(m, (uint16_t)(XEEN_DS_ATTRIBUTE_BONUS_THRESHOLD + 2u * k)) <= value) {
        ++k;
    }
    return k;
}
/* An attribute (0..6): its current value CURRENT, its maximum MAXIMUM and the name of the current value's rating
 * RATING (attribute_rating). */
static int details_attribute(xeen_game *g, uint16_t current, uint16_t maximum, unsigned rating, const char *title,
                             char *out, size_t size)
{
    return snprintf(out, size, ds_text(g, XEEN_TEXT_DETAILS_STAT), title, (unsigned long)current,
                    (unsigned long)maximum, far_text(g, XEEN_DS_RATING_NAMES, rating));
}
/* The age: current and natural, the birth day and year. */
static int details_age(xeen_game *g, const uint8_t *ch, const char *title, char *out, size_t size)
{
    xeen_movement *m = &g->movement;
    return snprintf(out, size, ds_text(g, XEEN_TEXT_DETAILS_AGE), title, (unsigned)view_age(m, ch, 0),
                    (unsigned)view_age(m, ch, 1), (unsigned)ch[XEEN_CHARACTER_BIRTH_DAY],
                    (unsigned)(uint16_t)xeen_get_le16(ch + XEEN_CHARACTER_BIRTH_YEAR));
}
/* The level: current and the base level byte, and the attacks a round: the level divided by the class's divisor
 * (XEEN_DS_CLASS_ATTACK_DIVISOR by class), plus 1; the plural suffix unless it is one. */
static int details_level(xeen_game *g, const uint8_t *ch, const char *title, char *out, size_t size)
{
    uint16_t level = xeen_character_current_level(ch),
             attacks = (uint16_t)(level / g->movement.ds[(uint16_t)(XEEN_DS_CLASS_ATTACK_DIVISOR + ch[XEEN_CHARACTER_CLASS])] +
                                  1u);
    return snprintf(out, size, ds_text(g, XEEN_TEXT_DETAILS_LEVEL), title, (unsigned)level,
                    (unsigned)ch[XEEN_CHARACTER_LEVEL], (unsigned)attacks,
                    attacks == 1u ? ds_text(g, XEEN_TEXT_DETAILS_EMPTY) : ds_text(g, XEEN_TEXT_DETAILS_PLURAL));
}
/* The armor class: current and maximum. */
static int details_armor_class(xeen_game *g, const uint8_t *ch, const char *title, char *out, size_t size)
{
    xeen_movement *m = &g->movement;
    return snprintf(out, size, ds_text(g, XEEN_TEXT_DETAILS_CURRENT_MAXIMUM), title,
                    (unsigned long)xeen_combat_armor_class(m, ch, 0), (unsigned long)xeen_combat_armor_class(m, ch, 1));
}
/* The hit points: the signed current and the maximum. */
static int details_hit_points(xeen_game *g, const uint8_t *ch, const char *title, char *out, size_t size)
{
    return snprintf(out, size, ds_text(g, XEEN_TEXT_DETAILS_HIT_POINTS), title,
                    (int)(int16_t)xeen_get_le16(ch + XEEN_CHARACTER_HP),
                    (unsigned long)(uint16_t)xeen_combat_max_hp(&g->movement, ch));
}
/* The spell points: current and maximum. A negative current SP prints as its 32-bit unsigned value: the original
 * sign-extends the word for the "%lu" format (42B00). */
static int details_spell_points(xeen_game *g, const uint8_t *ch, const char *title, char *out, size_t size)
{
    return snprintf(out, size, ds_text(g, XEEN_TEXT_DETAILS_CURRENT_MAXIMUM), title,
                    (unsigned long)(uint32_t)(int32_t)(int16_t)xeen_get_le16(ch + XEEN_CHARACTER_SP),
                    (unsigned long)xeen_combat_max_sp(&g->movement, ch));
}
/* The resistances: for each of the six elements its base + item bonus + temporary value, each in a word. */
static int details_resistances(xeen_game *g, const uint8_t *ch, const char *title, char *out, size_t size)
{
    /* each resistance's item bonus kind: XEEN_ITEM_BONUS_RESISTANCES + its element's category (fire 0, electricity 1,
     * cold 2, poison 3, energy 4, magic 5: the materials' order, not the record's) */
    static const uint8_t resist_bonus[6] = {
            [XEEN_RESISTANCE_FIRE] = XEEN_ITEM_BONUS_RESISTANCES + 0u,
            [XEEN_RESISTANCE_COLD] = XEEN_ITEM_BONUS_RESISTANCES + 2u,
            [XEEN_RESISTANCE_ELECTRICITY] = XEEN_ITEM_BONUS_RESISTANCES + 1u,
            [XEEN_RESISTANCE_POISON] = XEEN_ITEM_BONUS_RESISTANCES + 3u,
            [XEEN_RESISTANCE_ENERGY] = XEEN_ITEM_BONUS_RESISTANCES + 4u,
            [XEEN_RESISTANCE_MAGIC] = XEEN_ITEM_BONUS_RESISTANCES + 5u};
    unsigned v[6], i;
    for (i = 0; i < 6u; ++i) {
        v[i] = (uint16_t)(ch[XEEN_CHARACTER_RESISTANCE(i)] + xeen_combat_item_bonus(&g->movement, ch, resist_bonus[i]) +
                          ch[XEEN_CHARACTER_RESISTANCE_TEMPORARY(i)]);
    }
    return snprintf(out, size, ds_text(g, XEEN_TEXT_DETAILS_RESISTANCES), title, v[0], v[1], v[2], v[3], v[4], v[5]);
}
/* The experience: its total and what the next level needs, "Eligible" when nothing. The need is first written as
 * text into DS text buffer 0 (a side effect the detail leaves behind), which the format then reads. */
static int details_experience(xeen_game *g, const uint8_t *ch, const char *title, char *out, size_t size)
{
    xeen_movement *m = &g->movement;
    uint32_t need = xeen_experience_needed(m, ch);
    char text[16];
    if (!need) {
        (void)snprintf(text, sizeof text, "%s", ds_text(g, XEEN_TEXT_DETAILS_ELIGIBLE));
    } else {
        (void)snprintf(text, sizeof text, "%ld", (long)(int32_t)need); /* 13F5A, radix 10 */
    }
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_0, text);
    return snprintf(out, size, ds_text(g, XEEN_TEXT_DETAILS_EXPERIENCE), title,
                    (unsigned long)xeen_level_experience(m, ch), ds_text(g, XEEN_DS_TEXT_BUFFER_0));
}
/* The gold or the gems (STAT): on hand and in the bank, the party's. */
static int details_funds(xeen_game *g, unsigned stat, const char *title, char *out, size_t size)
{
    xeen_movement *m = &g->movement;
    return snprintf(out, size, ds_text(g, XEEN_TEXT_DETAILS_PARTY_FUNDS), title,
                    (unsigned long)xeen_ds_read32(m, stat == DETAILS_GOLD ? XEEN_DS_PARTY_GOLD : XEEN_DS_PARTY_GEMS),
                    (unsigned long)xeen_ds_read32(m, stat == DETAILS_GOLD ? XEEN_DS_BANK_GOLD : XEEN_DS_BANK_GEMS));
}
/* The food: on hand and the days it lasts, food / members / 3 (the plural suffix unless one). The party must not be empty (the
 * original divides by its count: details_begin stops first). */
static int details_food(xeen_game *g, const char *title, char *out, size_t size)
{
    xeen_movement *m = &g->movement;
    unsigned food = xeen_read16(m, XEEN_DS_PARTY_FOOD), days = food / m->ds[XEEN_DS_PARTY_COUNT] / 3u;
    return snprintf(out, size, ds_text(g, XEEN_TEXT_DETAILS_FOOD), title, food, days,
                    days == 1u ? ds_text(g, XEEN_TEXT_DETAILS_EMPTY) : ds_text(g, XEEN_TEXT_DETAILS_PLURAL));
}
/* The detail window's height for statistic STAT, by its number of text lines. The lists' (skills, conditions)
 * come from their line count instead (details_window), and the awards have their own window. */
static unsigned details_height(unsigned stat)
{
    switch (stat) {
        case DETAILS_AGE:
        case DETAILS_LEVEL:
            return 53;
        case DETAILS_ARMOR_CLASS:
        case DETAILS_HP:
        case DETAILS_SP:
            return 42;
        case DETAILS_RESISTANCES:
            return 80;
        case DETAILS_EXPERIENCE:
        case DETAILS_GOLD:
        case DETAILS_GEMS:
        case DETAILS_FOOD:
            return 43;
        default: /* the attributes */
            return 52;
    }
}
/*
 * Opens the character view's detail of statistic STAT (0..19, enum details_stat; the view's cursor or a statistic
 * button) for the member record RECORD: its text (details_attribute .. details_food, details_skills,
 * details_conditions), titled by the statistic's name, into DS text buffer 2, then the detail window
 * (details_window) at the statistic's height. The awards open the awards list instead (awards_begin). The
 * character_details add-on's state is reset first when it is on.
 * Stops: an attribute above every threshold (the stat table stop), the food of an empty party (the original
 * divides by 0), a text that does not fit its buffer, and the lists' and window's own stops.
 * Original: 428F1 (the cases CS:08C6; the titles DS 2F04; the ratings DS 2F9C).
 */
xeen_status details_begin(xeen_game *g, uint16_t record, unsigned stat)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    const uint8_t *ch = ds + record;
    char out[2000];
    const char *title = far_text(g, XEEN_DS_STAT_NAMES, stat);
    unsigned lines = 0;
    /* Every case that reaches the text writes n first (details_skills/details_conditions set it whenever they
     * return XEEN_OK; their stops return XEEN_SERVICE_REQUIRED); -1 would only reach the text stop below. */
    int n = -1;
    xeen_status status;
    xeen_presentation_trace_extra("mark dialog-details"); /* Evidence: 428F1 */
    if (xeen_character_details_on) {
        xeen_character_details_reset(); /* add-on character_details */
    }
    switch (stat) {
        case XEEN_STAT_MIGHT:
        case XEEN_STAT_INTELLECT:
        case XEEN_STAT_PERSONALITY:
        case XEEN_STAT_ENDURANCE:
        case XEEN_STAT_SPEED:
        case XEEN_STAT_ACCURACY:
        case XEEN_STAT_LUCK: {
            uint16_t current = xeen_combat_stat(m, ch, stat, 0), maximum = xeen_combat_stat(m, ch, stat, 1);
            unsigned rating = attribute_rating(m, current);
            if (rating == ATTRIBUTE_RATINGS) {
                return shop_stop(g, DETAILS_STOP_STAT_TABLE);
            }
            n = details_attribute(g, current, maximum, rating, title, out, sizeof out);
            break;
        }
        case DETAILS_AGE:
            n = details_age(g, ch, title, out, sizeof out);
            break;
        case DETAILS_LEVEL:
            n = details_level(g, ch, title, out, sizeof out);
            break;
        case DETAILS_ARMOR_CLASS:
            n = details_armor_class(g, ch, title, out, sizeof out);
            break;
        case DETAILS_HP:
            n = details_hit_points(g, ch, title, out, sizeof out);
            break;
        case DETAILS_SP:
            n = details_spell_points(g, ch, title, out, sizeof out);
            break;
        case DETAILS_RESISTANCES:
            n = details_resistances(g, ch, title, out, sizeof out);
            break;
        case DETAILS_SKILLS:
            if ((status = details_skills(g, ch, title, out, sizeof out, &n, &lines)) != XEEN_OK) {
                return status;
            }
            break;
        case DETAILS_AWARDS:
            return awards_begin(g, record);
        case DETAILS_EXPERIENCE:
            n = details_experience(g, ch, title, out, sizeof out);
            break;
        case DETAILS_GOLD:
        case DETAILS_GEMS:
            n = details_funds(g, stat, title, out, sizeof out);
            break;
        case DETAILS_FOOD:
            if (!ds[XEEN_DS_PARTY_COUNT]) {
                return shop_stop(g, DETAILS_STOP_FOOD_NO_PARTY);
            }
            n = details_food(g, title, out, sizeof out);
            break;
        default: /* DETAILS_CONDITIONS */
            if ((status = details_conditions(g, ch, title, out, sizeof out, &n, &lines)) !=
                XEEN_OK) {
                return status;
            }
            break;
    }
    if (n < 0 || (size_t)n >= sizeof out || XEEN_DS_TEXT_BUFFER_2 + (unsigned)n + 1u > XEEN_PARTY_RECORDS) {
        return shop_stop(g, DETAILS_STOP_TEXT);
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    return details_window(g, stat, details_height(stat), lines);
}
/*
 * A key in the details poll closes the detail: the view's buttons redrawn, the defined window's font reset and
 * the window closed, the key word F5FE cleared, then back to the character view (view_after_details).
 * Original: 4318C (46321, 4E38C(1Ch, "\x01"), 18765(1Ch), 189D7); 428F1 answers 0.
 */
xeen_status details_end(xeen_game *g)
{
    view_buttons(g);
    menu_window_text(g, XEEN_WINDOW_DEFINED, WINDOW_TEXT_ONLY, ds_text(g, XEEN_TEXT_DETAILS_FONT_RESET));
    panel_window(g, XEEN_WINDOW_DEFINED, 0);
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
    return view_after_details(g);
}
/*
 * Back to the character view after a detail or the awards: UI mode 0Ah (the view) and the view's tick, which
 * neither redraws the view nor waits for the frame timer first.
 * Original: 44D8E (428F1's AX, always 0), 44E89 (F5FE 0) to the loop at 44C09.
 */
static xeen_status view_after_details(xeen_game *g)
{
    g->movement.ds[XEEN_DS_UI_MODE] = 0x0a;
    return panel_set(g, VIEW_TICK, VIEW_TICK_BOUNDARY);
}
/* ------------------------------------ the awards list */
static xeen_status awards_text(xeen_game *);
/*
 * Opens the awards list of the member record RECORD (the details' statistic 14): the button stack pushed, the
 * award.icn buttons (Up, Down, Esc), the UI mode saved and set to FFh, the text block pointer and the file buffer's
 * segment saved (text_block_saved); award.bin loaded from archive 1 (the world's archive selected back after) as the
 * text block, as qnotes.bin is; the
 * list windows (29, 30) opened unless window 29 is open already (its flag, DS 9889: then they stay as they are and
 * are not closed at the end);
 * then the list from its first line (awards_text).
 * Stops: award.icn cannot be loaded, nor award.bin.
 * Original: 41581 (190ED, 190A3, the buttons 1E62..1E76, 191DE(1), 2B6C9, 191DE(DS 94), 1871B(1Dh/1Eh) when 9889
 * is clear, 4E38C(1Dh, DS 76A6)), then 416A2.
 */
static xeen_status awards_begin(xeen_game *g, uint16_t record)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, world;
    size_t n = 0;
    uint8_t *b;
    xeen_presentation_trace_extra("mark dialog-awards");   /* Evidence: 41581 */
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (!handle_push_or_undo(g, XEEN_TEXT_AWARDS_SPRITES)) {
        return shop_stop(g, AWARDS_STOP_SPRITES);
    }
    button_tables(m, XEEN_BUTTONS_AWARDS);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
    g->awards_ui_mode = ds[XEEN_DS_UI_MODE];
    ds[XEEN_DS_UI_MODE] = 0xff;
    text_block_save(m, &g->awards_text);
    world = ds[XEEN_DS_WORLD];
    xeen_select_archive(m, 1);
    b = menu_load(g, ds_text(g, XEEN_TEXT_AWARDS_FILE), &n);
    xeen_select_archive(m, world);
    if (!b) {
        return shop_stop(g, AWARDS_STOP_NO_MEMORY);
    }
    free(g->awards_bin);
    g->awards_bin = b;
    g->awards_bin_size = n;
    text_block_in_file_buffer(m, &g->awards_text);
    g->awards_record = record;
    g->awards_top = 0;
    g->awards_opened = !ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_LIST_BUTTONS)];
    if (g->awards_opened) {
        panel_window(g, XEEN_WINDOW_LIST_BUTTONS, 1);
        panel_window(g, XEEN_WINDOW_LIST, 1);
    }
    menu_window_text(g, XEEN_WINDOW_LIST_BUTTONS, WINDOW_FRAMED, ds_text(g, XEEN_TEXT_AWARDS_BUTTONS));
    location_buttons(g);
    return awards_text(g);
}
/* award.bin's text of award K: after the file's K-th NUL (entry 0 at its start, as qnotes.bin); NULL past the
 * file. Original: the offsets 2B6C9 keeps at 50D4:165C. */
static const char *awards_entry(const xeen_game *g, unsigned k)
{
    return nul_entry(g->awards_bin, g->awards_bin_size, k);
}
/*
 * Builds and shows the awards page from line g->awards_top: every award the member holds takes the next of the 88
 * slots in award order (the others show the empty first name of the DS 263C table); the Warzone wins award is
 * its text used as a format with the member's count, the race award with the member's race name; without any
 * award, slot 1 says "None". The page: the member's name and class and the nine slots from the top line; then
 * the awards poll (awards_key).
 * Stops: an award past award.bin's texts; a wins or race text of 28 characters or more (the original's 28-byte
 * copy keeps no terminator) or its line past 90 bytes; a top line outside the slots; a page that does not fit the
 * text buffer.
 * Original: 416A2 (46384's award test; the copy 15616; the page DS 76DB, "None" DS 76CF), then the poll 41914.
 */
static xeen_status awards_text(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    const uint8_t *ch = ds + g->awards_record;
    char out[0x7d0];
    unsigned di, count = 0;
    int n, top = g->awards_top;
    const char **sl = g->awards_slot;
    for (di = 0; di < XEEN_AWARD_COUNT; ++di) {
        sl[di] = far_text(g, XEEN_DS_WEAPON_POWER_NAMES, 0);
    }
    for (di = 0; di < XEEN_AWARD_COUNT; ++di) {
        const char *e;
        if (!has_award(ch, (uint16_t)di)) {
            continue;
        }
        if (!(e = awards_entry(g, di))) {
            return shop_stop(g, AWARDS_STOP_ENTRY);
        }
        if (di == XEEN_AWARD_WARZONE_WINS || di == XEEN_AWARD_RACE) {
            char *to = di == XEEN_AWARD_WARZONE_WINS ? g->awards_counted : g->awards_race;
            char form[29];
            if (strlen(e) >= 28u) {
                /* 9 is XEEN_AWARD_WARZONE_WINS, spelt as the literal the stop registry keys these selectors by */
                return shop_stop(g, di == 9u ? AWARDS_STOP_WINS_TEXT : AWARDS_STOP_RACE_TEXT); /* 15616 leaves no NUL */
            }
            memcpy(form, e, strlen(e) + 1u);
            n = di == XEEN_AWARD_WARZONE_WINS ? snprintf(to, 90, form, (unsigned)ch[XEEN_CHARACTER_AWARDS + di])
                         : snprintf(to, 90, form, far_text(g, XEEN_DS_RACE_NAMES, ch[XEEN_CHARACTER_RACE]));
            if (n < 0 || n >= 90) {
                return shop_stop(g, di == 9u ? AWARDS_STOP_WINS_LINE : AWARDS_STOP_RACE_LINE);
            }
            e = to;
        }
        sl[count++] = e;
    }
    g->awards_count = (int)count;
    if (!count) {
        sl[1] = ds_text(g, XEEN_TEXT_AWARDS_NONE);
    }
    if (top < 0 || top + 9 > (int)XEEN_AWARD_COUNT) {
        return shop_stop(g, AWARDS_STOP_TOP_LINE);
    }
    n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_AWARDS_PAGE), (const char *)ch,
                 far_text(g, XEEN_DS_CLASS_NAMES, ch[XEEN_CHARACTER_CLASS]), sl[top], sl[top + 1], sl[top + 2],
                 sl[top + 3], sl[top + 4], sl[top + 5], sl[top + 6], sl[top + 7], sl[top + 8]);
    if (n < 0 || (size_t)n >= sizeof out || XEEN_DS_TEXT_BUFFER_2 + (unsigned)n + 1u > XEEN_PARTY_RECORDS) {
        return shop_stop(g, AWARDS_STOP_TEXT);
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    menu_window_text(g, XEEN_WINDOW_LIST, WINDOW_FRAMED, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_UPPER_SCREEN);
    return panel_set(g, AWARDS_POLL, AWARDS_POLL_BOUNDARY);
}
/*
 * A key in the awards list. Esc closes it: the list windows closed when awards_begin opened them, award.bin freed,
 * the text block pointer and file buffer restored, the key word F5FE cleared, the button stack and sprites popped, the UI mode
 * restored, then back to the character view. Up scrolls one line (not above the first), Down one line (not when
 * the page's last line would pass the member's award count); any key but Esc redraws the page.
 * Original: 4191D (Esc 4195C: 18765(1Eh/1Dh), 18693, F5FE = 0, 18FFF, 190CF; 428F1 answers 0 at 431AF).
 */
xeen_status awards_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    if (key == XEEN_KEY_ESCAPE) {
        if (g->awards_opened) {
            panel_window(g, XEEN_WINDOW_LIST, 0);
            panel_window(g, XEEN_WINDOW_LIST_BUTTONS, 0);
        }
        free(g->awards_bin);
        g->awards_bin = NULL;
        g->awards_bin_size = 0;
        text_block_restore(m, &g->awards_text);
        xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
        xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
        handle_pop(g);
        ds[XEEN_DS_UI_MODE] = g->awards_ui_mode;
        return view_after_details(g);
    }
    if (key == XEEN_KEY_UP) {
        if (--g->awards_top < 1) {
            g->awards_top = 0;
        }
    } else if (key == XEEN_KEY_DOWN) {
        if (++g->awards_top + 9 > g->awards_count) {
            --g->awards_top;
        }
    }
    return awards_text(g);
}
