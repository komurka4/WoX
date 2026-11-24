/*
 * The guild (Phase 248, from game_bank.c): location kind 2 in the 277E5 loop: the spells it sells, a member's
 * check, the 3F05D spell list (shared with the Cast dialog's list: cast_list_build) with its texts, costs and
 * categories, its page, buttons, keys and the purchase's confirmation, with the spell_list_sort add-on's hooks.
 * The bank (kind 0) and the 28ABA amounts are game_bank.c's.
 */
#include "game_location.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when the mage guild cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json).
 */
enum {
    GUILD_STOP_MAP = 0x3ed28,                /* a Clouds guild on a map without a guild table */
    GUILD_STOP_SPELL_SPRITES = 0x3f06c,      /* scroll.icn */
    GUILD_STOP_LIST_MEMBER = 0x3f847,        /* the F1..F6 member choice failed */
    GUILD_STOP_CATEGORY = 0x3f4ce,           /* no spell category chosen ([BP-0Ah] unset) */
    GUILD_STOP_CONFIRM = 0x3f604             /* the purchase's confirmation could not be shown */
};
/* ------------------------------------ the guild (location kind 2)
 * Entering loads the spell descriptions (spldesc.bin); the guild's text depends on the member's membership. Its keys:
 * B lists the spells the guild sells to the member, S (Spell Info) the same list with the spells the member knows,
 * a chosen spell showing its description instead of the purchase offer (guild_info); F1..F6 change the member.
 * Leaving charges the clock a day, as every location's exit does outside the Sign In (location_end).
 * Original: 277E5 kind 2: 27B96 (spldesc.bin through 2B632), 276F4 (the membership test 253C0), 26113 (B:
 * 3F05D(member, 0); S: 3F05D(member, 80h)), 28056 (the exit). */

/* 5802:0000 (the image's data before DS): the twenty spells of each Clouds
 * guild, Vertigo (map 28) to Winterkill (32); 3ED28 indexes it by map - 28.
 * Darkside's lists are in DS (3EE12, guild_list). */
static const uint8_t guild_spells[5][20] = {
        {1, 10, 20, 26, 27, 38, 40, 42, 45, 50, 55, 59, 60, 61, 62, 68, 72, 75, 77, 77},
        {3, 4, 5, 14, 15, 25, 30, 31, 34, 41, 49, 51, 53, 67, 73, 75, 255, 255, 255, 255},
        {4, 8, 9, 12, 13, 22, 23, 24, 28, 34, 41, 44, 52, 70, 73, 74, 255, 255, 255, 255},
        {6, 7, 9, 11, 12, 13, 17, 21, 22, 24, 29, 36, 56, 58, 64, 71, 255, 255, 255, 255},
        {6, 7, 9, 11, 12, 13, 18, 21, 29, 32, 36, 37, 46, 51, 56, 58, 69, 255, 255, 255}};
/* F868[spell]: spells.xen (archive 1) holds the names the resident table
 * addresses (offsets into the file; DOS memory in the original). */
const char *spell_name(xeen_game *g, unsigned spell)
{
    xeen_movement *m = &g->movement;
    uint16_t at = xeen_read16(m, (uint16_t)(XEEN_DS_SPELL_NAMES + 4u * spell));
    if (!g->spells) {
        uint8_t world = m->ds[XEEN_DS_WORLD], *b;
        size_t n = 0;
        xeen_select_archive(m, 1);
        b = menu_extract(g, ds_text(g, XEEN_TEXT_SPELL_NAMES_FILE), &n);
        xeen_select_archive(m, world);
        if (!b || !(g->spells = malloc(n + 1u))) {
            free(b);
            return "";
        }
        memcpy(g->spells, b, n);
        free(b);
        g->spells[n] = 0;
        g->spells_size = n;
    }
    return at < g->spells_size ? (const char *)g->spells + at : "";
}
/* Whether the member at RECORD belongs to this town's guild, by the guild's award: on Clouds map 31h award 5, any
 * other Clouds map award map - 1Ch; on Darkside by town (53h..56h for the towns' maps 1Dh, 1Fh, 21h, 23h, else 57h;
 * the map-31h test reads the world byte, so Darkside's map 31h takes the table). The award is held when its byte is
 * nonzero and its nibble set (has_award's rule; the DS index wraps at 64K for a negative award's slot).
 * Original: 253C0 (the 25404 table on map - 1Dh: 0 53h, 2 54h, 4 55h, 6 56h); 46384 (+39h + slot). */
static int guild_member(const xeen_game *g, uint16_t record)
{
    const uint8_t *ds = g->movement.ds;
    int award;
    unsigned slot;
    uint8_t v;
    if (ds[XEEN_DS_WORLD]) {
        static const uint8_t dark_award[7] = {0x53, 0x57, 0x54, 0x57, 0x55, 0x57, 0x56};
        unsigned map_index = (uint16_t)(ds[XEEN_DS_PARTY_MAP] - 0x1du);
        award = map_index < 7u ? dark_award[map_index] : 0x57;
    } else {
        award = ds[XEEN_DS_PARTY_MAP] == 49u ? 5 : (int)ds[XEEN_DS_PARTY_MAP] - 0x1c;
    }
    slot = xeen_award_slot((uint16_t)award);
    /* the DS index wraps at 64K, as the original's does, for a negative award's slot */
    v = ds[(uint16_t)(record + XEEN_CHARACTER_AWARDS + slot)];
    return v && (!xeen_award_high_nibble((uint16_t)award) ? (v & 0x0fu) : (v & 0xf0u));
}
/* The guild's window is two texts (the far format pointers of the program data): A827, the member's line (FORMAT
 * of the member's name: DS:[6E1C], "%s" over "Buy Spells" and "Spell Info"), then AC0F, the options window
 * (DS:[6E20]: "Guild Options for%s" of A827, and the party gold). 276F4, 3F0F6 and 3F747 each build both. */
static void guild_member_line(xeen_game *g, const char *format, uint16_t record)
{
    char line[256];
    (void)snprintf(line, sizeof line, format, ds_text(g, record));
    ds_put_text(g, XEEN_DS_TEXT_BUFFER, line);
}
/* The options window of A827 into OUT (N bytes). Returns 1, or 0 when it does not fit. */
static int guild_options_window(xeen_game *g, char *out, size_t n)
{
    char gold[24];
    int k;
    location_amount(g, gold, sizeof gold, xeen_ds_read32(&g->movement, XEEN_DS_PARTY_GOLD));
    k = snprintf(out, n, far_text(g, XEEN_DS_GUILD_OPTIONS_TEXT, 0), ds_text(g, XEEN_DS_TEXT_BUFFER), gold);
    return k >= 0 && (size_t)k < n;
}
/* The guild's text: the customer's member line, or "You have to be a member to shop here." for one who is not a
 * member, into the text buffer; then the options window (guild_options_window). Original: 276F4 (A827, DS:54E7),
 * 272DD. */
int guild_text(xeen_game *g, char *out, size_t n)
{
    if (guild_member(g, g->location_member)) {
        guild_member_line(g, far_text(g, XEEN_DS_GUILD_MEMBER_TEXT, 0), g->location_member);
    } else {
        char line[256];
        (void)snprintf(line, sizeof line, "%s", ds_text(g, XEEN_TEXT_GUILD_MEMBERS_ONLY));
        ds_put_text(g, XEEN_DS_TEXT_BUFFER, line);
    }
    return guild_options_window(g, out, n);
}
/* The guild's price of SPELL: its spell-point cost x 100, or for a negative cost the cost's magnitude x 500, doubled
 * for the hybrid classes (DOUBLED), all in 16 bits. Original: 3EBAB(spell, doubled): DS:1EC2[spell]. */
static uint16_t guild_cost(const xeen_game *g, unsigned spell, unsigned doubled)
{
    int16_t v = (int16_t)xeen_read16(&g->movement, (uint16_t)(XEEN_DS_SPELL_SP_COST + 2u * spell));
    uint16_t ax = v < 0 ? (uint16_t)(v * -500) : (uint16_t)(v * 100);
    return (uint16_t)(ax << doubled);
}
/* A class's spell list in the guild (0 the clerical list, 1 the sorcerous, 2 the druidic; -1 a class that casts no
 * spells), and *DOUBLED whether the class pays double (the hybrids: Paladin, Archer, Ranger).
 * Original: 3EC1F / 3F4A7 (DS:201E + 40 x the list). */
static int guild_category(uint8_t cls, unsigned *doubled)
{
    *doubled = cls == XEEN_CLASS_PALADIN || cls == XEEN_CLASS_ARCHER || cls == XEEN_CLASS_RANGER;
    switch (cls) {
        case XEEN_CLASS_PALADIN:
        case XEEN_CLASS_CLERIC:
            return 0;
        case XEEN_CLASS_ARCHER:
        case XEEN_CLASS_SORCERER:
            return 1;
        case XEEN_CLASS_DRUID:
        case XEEN_CLASS_RANGER:
            return 2;
        default:
            return -1;
    }
}
/* The spell list's lines: 50 bytes each at DS:9C6F, the target of the far pointer XEEN_DS_SPELL_LIST_LINES that
 * 3F05D's open sets (the list routines address them through it; the area is scratch other dialogs reuse). */
#define GUILD_LINES      XEEN_DS_SCRATCH
#define GUILD_LINE_BYTES 50u
#define GUILD_LINE_COUNT 40u /* the 7D0h bytes the page clears (XEEN_DS_SCRATCH_BYTES) */
/* A list without spells: its first line is the message TEXT ("You have learned all we can teach you.", "Not a
 * spell caster...", "You don't know any spells..."). */
static void guild_list_message(xeen_game *g, const char *text)
{
    memcpy(g->movement.ds + GUILD_LINES, text, strlen(text) + 1u);
}
/*
 * One spell of 3EBD6(member, 0)'s list for class list CAT: its first index in the class's list (DS:201E + 40 x
 * cat); one in it and not known (+79h + index), or known with 80h (Spell Info: the guild's list), gets the
 * line DS:71C6 (its name, its cost) at 9C6F + 50 x *COUNT (the original's DI) and the index in guild_index;
 * *COUNT counts the lines.
 */
static void guild_list_spell(xeen_game *g, unsigned cat, unsigned spell, unsigned doubled, unsigned *count)
{
    uint8_t *ds = g->movement.ds;
    uint16_t rec = g->guild_record;
    unsigned i;
    for (i = 0; i < XEEN_CLASS_SPELL_LIST && ds[XEEN_DS_CLASS_SPELL_LISTS + XEEN_CLASS_SPELL_LIST * cat + i] != spell;
         ++i) {
    }
    if (i >= XEEN_CLASS_SPELL_LIST || (ds[(uint16_t)(rec + XEEN_CHARACTER_SPELLS + i)] && !g->guild_info)) {
        return; /* 80h lists known ones */
    }
    {
        /* 3ECDF: sprintf straight into the line's 50-byte slot, unbounded: a line of 50 or more bytes runs on into
         * the next slot (the shipped names and prices fill at most 25); here bounded only by the DS's end. */
        size_t at = GUILD_LINES + GUILD_LINE_BYTES * *count;
        if (snprintf((char *)ds + at, sizeof g->movement.ds - at, ds_text(g, XEEN_TEXT_SPELL_LIST_PRICE_LINE),
                     spell_name(g, spell), (unsigned)guild_cost(g, spell, doubled)) < 0) {
            ds[at] = 0;
        }
    }
    g->guild_index[(*count)++] = (uint8_t)i;
}
/* Builds the guild's spell list for the customer: for the class's spell list, each spell the town's guild sells (in
 * a Clouds town by the town; Darkside by the town's range of its list; the two special guild maps 49 and 37 every
 * spell of the list, in id order) that the member does not know (or every one with Spell Info) as a line with its
 * name and price; "You have learned all we can teach you." when none; "Not a spell caster..." for a member without
 * spell points. Returns the lines' count.
 * Original: 3EBD6(member, 0): the line DS:71C6 (F868, the cost) at FB02 + 50 x n (DS:9C6F), the list at 580E:0047;
 * DS:71D3, DS:71FE (46470); maps 31h / 25h at 3EC50; Darkside 3EE12. */
static unsigned guild_list(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t rec = g->guild_record;
    unsigned count = 0, doubled, k;
    int cat = guild_category(ds[(uint16_t)(rec + XEEN_CHARACTER_CLASS)], &doubled);
    if (cat >= 0 && (ds[XEEN_DS_PARTY_MAP] == 49u || ds[XEEN_DS_PARTY_MAP] == 37u)) {
        /* 3EC50: maps 31h and 25h (either world) list every spell
         * id 0..4Bh that the class's list holds (its first index there), in
         * id order; with 80h (Spell Info) the known ones as well. */
        unsigned spell;
        for (spell = 0; spell < XEEN_SPELL_COUNT; ++spell) {
            guild_list_spell(g, (unsigned)cat, spell, doubled, &count);
        }
        if (!count) {
            guild_list_message(g, ds_text(g, XEEN_TEXT_SPELL_LIST_ALL_LEARNED));
        }
    } else if (cat >= 0) {
        unsigned town = (unsigned)ds[XEEN_DS_PARTY_MAP] - 28u, first = 0, end = 20u;
        if (ds[XEEN_DS_WORLD]) {
            /* 3EE12: Darkside town t = (map - 1Dh) SAR 1; the
             * class's guild indices DS:6E32 + 8 x cat + 2t (first) and
             * 6E33 (end) into DS:1FA9 + 27h x cat. */
            int t = (int)(int16_t)(uint16_t)(ds[XEEN_DS_PARTY_MAP] - 0x1du) >> 1;
            uint16_t at = (uint16_t)(XEEN_DS_DARKSIDE_GUILD_RANGES + 8u * (unsigned)cat + (uint16_t)(2 * t));
            first = ds[at];
            end = ds[(uint16_t)(at + 1u)];
        }
        for (k = first; k < end; ++k) {
            unsigned spell =
                    ds[XEEN_DS_WORLD]
                            ? ds[(uint16_t)(XEEN_DS_DARKSIDE_GUILD_SPELLS +
                                            XEEN_DARKSIDE_GUILD_LIST * (unsigned)cat + k)]
                            : guild_spells[town][k];
            guild_list_spell(g, (unsigned)cat, spell, doubled, &count);
        }
        if (!count) {
            guild_list_message(g, ds_text(g, XEEN_TEXT_SPELL_LIST_ALL_LEARNED));
        }
    }
    if (!xeen_combat_max_sp(m, ds + rec)) {
        guild_list_message(g, ds_text(g, XEEN_TEXT_SPELL_LIST_NOT_CASTER));
    }
    return count;
}
/* The list of the spells the current record knows (the spell list's caster mode, for the Cast dialog), each line
 * its name, its spell-point cost at the member's level and its gems, with the spell's slot kept per line; returns the
 * count. A
 * member without spell points gets "not a spellcaster", one who knows none "You don't know any spells". The class
 * chooses the slot list (clerics and paladins, sorcerers and archers, druids and rangers).
 * Original: 3EBD6(member, 1) (3EF4A): 46470, DS:71FE; per +79h + index (0..38) DS:7214 (3EB8B at 2C909's level);
 * DS:7224; 3EF5D the class's list. */
unsigned cast_list_build(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t rec = g->guild_record;
    unsigned count = 0, i, cat;
    uint16_t level = (uint16_t)(ds[(uint16_t)(rec + XEEN_CHARACTER_LEVEL)] +
                                ds[(uint16_t)(rec + XEEN_CHARACTER_LEVEL_BONUS)]);
    if (!xeen_combat_max_sp(m, ds + rec)) {
        guild_list_message(g, ds_text(g, XEEN_TEXT_SPELL_LIST_NOT_CASTER));
        return 0;
    }
    cat = cast_category(ds[(uint16_t)(rec + XEEN_CHARACTER_CLASS)]);
    for (i = 0; i < XEEN_CLASS_SPELL_LIST - 1u; ++i) { /* slot 39 is the list's "no spell" entry */
        unsigned spell;
        char line[50];
        int n;
        if (!ds[(uint16_t)(rec + XEEN_CHARACTER_SPELLS + i)]) {
            continue;
        }
        spell = ds[XEEN_DS_CLASS_SPELL_LISTS + XEEN_CLASS_SPELL_LIST * cat + i];
        n = snprintf(line, sizeof line, ds_text(g, XEEN_TEXT_SPELL_LIST_COST_LINE), spell_name(g, spell),
                     (unsigned)cast_sp(g, spell, level), (unsigned)(uint16_t)cast_gems(g, spell));
        if (n < 0) {
            n = 0;
        }
        if (n > 49) {
            n = 49;
        }
        memcpy(ds + GUILD_LINES + GUILD_LINE_BYTES * count, line, (size_t)n);
        ds[GUILD_LINES + GUILD_LINE_BYTES * count + (unsigned)n] = 0;
        g->guild_index[count++] = (uint8_t)i;
    }
    if (!count) {
        guild_list_message(g, ds_text(g, XEEN_TEXT_SPELL_LIST_NONE_KNOWN));
    }
    return count;
}
/* Draws the location's buttons, the set saved on the button stack at its current depth when the guild pushed its own:
 * each enabled button's frame 2i at its x and y, up to the FFFFh terminator (buttons_draw). Original: 3F862..3F8E2:
 * frame 2i of 9684[96C2] at (9698[96C2][i], 965C[96C2][i]) for each enabled 95BC[96C2][i]. */
static void guild_head_buttons(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    int l = (int)(int8_t)m->ds[XEEN_DS_BUTTON_DEPTH];
    buttons_draw(g, xeen_read16(m, XEEN_DS_BUTTON_TABLE_SLOT(XEEN_DS_BUTTON_X_STACK, l)),
                 xeen_read16(m, XEEN_DS_BUTTON_TABLE_SLOT(XEEN_DS_BUTTON_Y_STACK, l)),
                 xeen_read16(m, XEEN_DS_BUTTON_TABLE_SLOT(XEEN_DS_BUTTON_ENABLED_STACK, l)),
                 xeen_read16(m, XEEN_DS_BUTTON_SPRITES_SLOT(l)), 32u);
}
/* The member's head of a guild list (mode 0 only, 3F0ED..3F15B): A827 = DS:6ED0 of the member's name, the options
 * window's text in AC0F (window 0Ah, DS:6F0B), frame 0 of the saved button sprites (9896, 9684[96C2]) at (105h, 6Ch),
 * and the right column shown (50BF6(0Ah), presentation). */
static void guild_page_head(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    char out[1000];
    /* 3F10F pushes the member line's text DS:6ED0 itself, not DS:[6E1C] (which holds the same offset) */
    guild_member_line(g, ds_text(g, XEEN_TEXT_GUILD_OPTIONS), g->guild_record);
    (void)guild_options_window(g, out, sizeof out);
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, out);
    menu_window_text(g, XEEN_WINDOW_RIGHT_COLUMN, WINDOW_TEXT_ONLY, out);
    exit_draw(g, xeen_read16(m, XEEN_DS_BUTTON_SPRITES_SLOT((int)(int8_t)m->ds[XEEN_DS_BUTTON_DEPTH])), 0, 261, 108,
              0);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_RIGHT_COLUMN);
}
/* The list's forty lines cleared (13FE7(DS:9C6F, 0, 7D0h)) and built: the member's known spells with their costs
 * (Cast, cast_list_build) or the guild's (guild_list); then A43F = the line numbers (DS:7263), or DS:6ECF when the
 * list is empty, and the title (DS:7282 of A43F and the member's name) in the framed window 19h. */
static void guild_page_list(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    char out[1000];
    const char *digits;
    memset(ds + GUILD_LINES, 0, GUILD_LINE_COUNT * GUILD_LINE_BYTES);
    g->guild_count = g->list_caster_spells ? cast_list_build(g) : guild_list(g);
    digits = g->guild_count ? ds_text(g, XEEN_TEXT_SPELL_LIST_NUMBERS) : ds_text(g, XEEN_TEXT_SPELL_LIST_NO_NUMBERS);
    memmove(ds + XEEN_DS_TEXT_BUFFER_0, digits, strlen(digits) + 1u);
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_SPELL_LIST_TITLE), ds_text(g, XEEN_DS_TEXT_BUFFER_0),
                   ds_text(g, g->guild_record));
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, out);
    menu_window_text(g, XEEN_WINDOW_VIEW_DIALOG, WINDOW_FRAMED, out);
}
/* The ten lines from the list's top in their colours (9 unless chosen), then the heading and its value: DS:7357
 * ("Gold") and the party's gold, or in the Cast list the spell-point heading and the member's spell points (signed);
 * AC0F = DS:72A0 of them, written in window 25h. */
static void guild_page_lines(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    unsigned top = g->guild_top, k;
    const uint8_t *c = g->list_colour + top;
    const char *l[10];
    char out[1000];
    for (k = 0; k < 10u; ++k) {
        l[k] = ds_text(g, (uint16_t)(GUILD_LINES + GUILD_LINE_BYTES * (top + k)));
    }
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_SPELL_LIST_PAGE), c[0], l[0], c[1], l[1], c[2], l[2], c[3],
                   l[3], c[4], l[4], c[5], l[5], c[6], l[6], c[7], l[7], c[8], l[8], c[9], l[9],
                   ds_text(g, g->list_caster_spells ? XEEN_TEXT_SPELL_LIST_SP_HEADING : XEEN_TEXT_SPELL_LIST_GOLD_HEADING),
                   g->list_caster_spells
                           ? (unsigned long)(int32_t)(int16_t)xeen_read16(m, (uint16_t)(g->guild_record + XEEN_CHARACTER_SP))
                           : (unsigned long)xeen_ds_read32(m, XEEN_DS_PARTY_GOLD));
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, out);
    menu_window_text(g, XEEN_WINDOW_VIEW_DIALOG_TEXT, WINDOW_TEXT_ONLY, out);
}
/* The scroll's frames (3F387..3F3E8, 98D4 = scroll.icn): 4 at (27h, 1Ah), 0 at (BBh, 1Ah), 2 at (BBh, 6Fh) and, in
 * the Cast list, 5 at (84h, 7Bh) (presentation). */
static void guild_page_scroll(xeen_game *g)
{
    uint16_t h = xeen_read16(&g->movement, XEEN_DS_SPRITE_HANDLE);
    exit_draw(g, h, 4, 39, 26, 0);
    exit_draw(g, h, 0, 187, 26, 0);
    exit_draw(g, h, 2, 187, 111, 0);
    if (g->list_caster_spells) {
        exit_draw(g, h, 5, 132, 123, 0);
    }
}
/*
 * The guild's (or the Cast list's) page, 3F0ED..3F3F2, in its original order: the member's head (guild Buy and Spell
 * Info only: guild_page_head), the list built and titled (guild_page_list), its ten lines from the top
 * (guild_page_lines), the scroll's frames (guild_page_scroll), the spell_list_sort add-on's page (off: nothing),
 * and window 19h shown (50BF6(19h)).
 */
void guild_page(xeen_game *g)
{
    if (!g->list_caster_spells) {
        guild_page_head(g);
    }
    guild_page_list(g);
    guild_page_lines(g);
    guild_page_scroll(g);
    spell_list_sort_page(g); /* add-on spell_list_sort (off: nothing) */
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW_DIALOG);
}
/* Opens the guild's spell list: the guilds of maps 49 and 37 (either world) list every spell, Clouds' other
 * guilds (maps 28..32) their own twenty, Darkside's guilds their DS tables; a Clouds map without a guild stops.
 * The list's window opens with its buttons and sprites (scroll.icn), then the page and its poll.
 * Original: 3F05D(member, 0): 190ED, 190A3(scroll.icn), FB04:FB02 = DS:9C6F, the 190A.. buttons, 9896 = 98D4,
 * 1871B(19h), the page, 189E3 (3F415, a busy poll); 3EC3F (3EC50 every spell). */
static xeen_status guild_begin(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    /* 3EC3F: maps 31h and 25h (either world) list every spell (3EC50,
     * guild_list). Clouds: 3ED28 indexes
     * 5802:0000 by map - 1Ch, twenty bytes a town: the five rows of maps
     * 28..32. Darkside: 3EE12's DS tables. */
    if (!ds[XEEN_DS_WORLD] && ds[XEEN_DS_PARTY_MAP] != 49u && ds[XEEN_DS_PARTY_MAP] != 37u &&
        (ds[XEEN_DS_PARTY_MAP] < 28u || ds[XEEN_DS_PARTY_MAP] > 32u)) {
        return location_stop(g, GUILD_STOP_MAP);
    }
    g->list_caster_spells = 0;
    g->guild_record = g->location_member;
    memset(g->list_colour, 9, sizeof g->list_colour);
    return guild_open(g);
}
/* Opens the spell list dialog (the guild's, or the Cast dialog's N with list_caster_spells set): the button set
 * pushed, its sprites pushed as the buttons' with the ten lines' table, the list window opened, the page from its top
 * drawn; then its poll (the guild's or the Cast list's). Stops when the sprites cannot load.
 * Original: 3F05D's common head (mode 1: colour bytes [BP-46h] 9, DI = FFFFh). */
xeen_status guild_open(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t h = 0;
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (!handle_push(g, ds_text(g, XEEN_TEXT_GUILD_SPRITES), &h)) {
        return location_stop(g, GUILD_STOP_SPELL_SPRITES);
    }
    xeen_write16(m, XEEN_DS_SPELL_LIST_LINES, GUILD_LINES);
    xeen_write16(m, XEEN_DS_SPELL_LIST_LINES_SEGMENT, XEEN_DS_SEGMENT);
    button_tables(m, XEEN_BUTTONS_LIST10);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, h);
    panel_window(g, XEEN_WINDOW_VIEW_DIALOG, 1);
    g->guild_top = 0;
    g->guild_active = 1;
    guild_page(g);
    return panel_set(g, g->list_caster_spells ? CAST_LIST : GUILD_POLL, GUILD_POLL_BOUNDARY);
}
/* The spell list closes: its window and sprites released, the button set popped, the key cleared; the dialog's member
 * becomes the location's chosen member, Spell Info mode ends and the location redraws.
 * Original: 3F8F8 (18765(19h), 190CF, 18FFF, F5FE = 0); 3F93C (DX:AX the dialog's member); 26161 (256A6, 26193). */
static xeen_status guild_end(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    window_pop(g, XEEN_WINDOW_VIEW_DIALOG);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    g->guild_active = 0;
    g->location_member = g->guild_record;
    g->guild_info = 0; /* 3F93C: DX:AX, the dialog's member */
    return location_redraw(g);
}
/* The list built again (after a member change, say) with its top lowered to show the last ten (0 when ten or fewer);
 * a member who is not a guild member hears the refusal voice and the list closes; else the page is redrawn.
 * Original: 3F67A (3EBD6), 3F0ED; mode 0's head 3F0F6 asks 253C0 (guild11.voc / skull1.voc, F47C = 5; 3F8F8). */
static xeen_status guild_refresh(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    int top = (int)g->guild_top;
    g->guild_count = guild_list(g);
    if (top + 10 > (int)g->guild_count) {
        top = (int)g->guild_count - 10;
        if (top < 1) {
            top = 0;
        }
    }
    g->guild_top = (unsigned)top;
    if (!guild_member(g, g->guild_record)) {
        voice_cancel(g, 0);
        xeen_write16(m, 0xf47c, 5);
        location_voc(g, ds_text(g, m->ds[XEEN_DS_WORLD] ? XEEN_TEXT_GUILD_NOT_MEMBER_VOICE_DARKSIDE : XEEN_TEXT_GUILD_NOT_MEMBER_VOICE_CLOUDS));
        exit_event(g, "voice");
        return guild_end(g);
    }
    guild_page(g);
    return panel_set(g, GUILD_POLL, GUILD_POLL_BOUNDARY);
}
/*
 * The spell list's keys in the guild (mode 0): 1..9 and 0 choose a listed spell: its cost and its description; with
 * Spell Info (mode bit 80h) an information box (the name, the text, "Press a Key!"; any key, no purchase), else the
 * purchase's yes/no. Esc leaves; Enter and S do nothing in this mode (no line is chosen); Up and Down move the top
 * line by one (Down at most to the last ten), PgUp and PgDn by ten; F1..F6 make a present member the list's (the
 * colours reset, the highlight), in a location with the location's head again, the top back to 0; an absent member
 * only resets the top.
 * Original: the table CS:1728 in 3F05D's mode 0 ([BP+0Ah] = mode & 7Fh, [BP-10h] the whole mode): 3F46D, 3EBAB, A43F =
 * spldesc.bin (15616 from 97C2:97C0 by 2B632's offsets, 199 bytes), A827 = DS:735C and 2937A(A827, 81h) (AX 0), else
 * DS:7382 and 2937A(A827, 1); Esc 3F6BA; Enter/S 3F6E1 (DI FFFFh); Up 3F441, Down 3F44A, PgUp/PgDn 3F6BF/3F6C8
 * through 3F67A; F1..F6 3F6F1 (97BE, 2A1F6; 9A1C 11h: DS:[6E1C], [6E20], window 0Ah).
 */
/* The spell list's F2/F3 (one line up or down): the top line moves by one, down at most to the last ten lines, never
 * above the first. */
void guild_scroll(xeen_game *g, uint16_t key)
{
    int top = (int)g->guild_top + (key == XEEN_KEY_UP ? -1 : 1);
    if (key == XEEN_KEY_DOWN && top > (int)g->guild_count - 10) {
        top = (int)g->guild_count - 10;
    }
    g->guild_top = top < 1 ? 0u : (unsigned)top;
}
/*
 * F1..F6 in the spell list, in either mode (the guild and Spell Info in a location, the Cast dialog's N list). Ignored
 * in combat: 0. A present member: every line's colour reset, no line chosen, the member becomes the list's and is
 * highlighted; then the head: in a location the member's name and the party's gold, otherwise (the Cast list) the
 * name, the ready spell (list entry 39 for none), its cost at the member's level, its gems and the member's spell points;
 * both heads go to the right column with the saved buttons, and it is shown. Present or absent: the top back to 0 and
 * the list again: 1. -1 when the head would not fit (none of the shipped texts). Neither head depends on the dialog's
 * mode: only a location sets the location's UI mode, and the Cast dialog runs exploring or in combat, so the Cast
 * list always takes its own head and the guild the location's.
 * Original: 3F6F1 in 3F05D; combat 3F8EF; [BP-46h] colour 9 (13FE7), DI FFFFh, [BP+6], 97BE, 2A1F6; 9A1C 11h 3F747:
 * A827 = [6E1C], AC0F = [6E20] with the gold (2689A); else 3F793: AC0F = [6E18], +13h's list, +A4 (27h none), 3EB8B at
 * 2C909's level, +158h; 3F84F: 4E38C(0Ah), the buttons of level 96C2 (3F862), 50BF6(0Ah); SI = 0, 3F67A; 9A1C 11h
 * from 277E5, 27B14/27BA2/27D6E; 3D8C1's callers 17E46, 387F6, 391E7.
 */
int list_member_key(xeen_game *g, unsigned n)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char out[1000];
    if (ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_COMBAT) {
        return 0;
    }
    if (ds[XEEN_DS_PARTY_COUNT] <= n) {
        return 1;
    }
    memset(g->list_colour, 9, sizeof g->list_colour);
    g->list_choice = -1;
    g->guild_record = XEEN_PARTY_RECORD(n);
    ds[XEEN_DS_CAST_MEMBER] = (uint8_t)n;
    portrait_highlight(g, n);
    if (ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_LOCATION) {
        guild_member_line(g, far_text(g, XEEN_DS_GUILD_MEMBER_TEXT, 0), g->guild_record);
        (void)guild_options_window(g, out, sizeof out);
        ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, out);
    } else if (!cast_text_of(g, g->guild_record)) {
        return -1;
    }
    menu_window_text(g, XEEN_WINDOW_RIGHT_COLUMN, WINDOW_TEXT_ONLY, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    guild_head_buttons(g);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_RIGHT_COLUMN);
    return 1;
}
/* Copies SPELL's description into the text buffer A43F (cleared first; at most its 1,000 bytes, up to the entry's NUL
 * or the file's end): entry k of the descriptions file starts after its k-th NUL. Spell 0 gets the file's start (the
 * original reads the DOS memory there), and so does a spell past the file's entries. Original: 2B632. */
static void guild_description(xeen_game *g, unsigned spell)
{
    uint8_t *ds = g->movement.ds;
    size_t at = 0, k;
    uint16_t want = (uint16_t)spell;
    for (k = 0; k < g->guild_desc_size && want; ++k) {
        if (!g->guild_desc[k] && !--want) {
            at = k + 1u;
            break;
        }
    }
    memset(ds + XEEN_DS_TEXT_BUFFER_0, 0, XEEN_TEXT_BUFFER_BYTES);
    for (k = 0; k < XEEN_TEXT_BUFFER_BYTES && at + k < g->guild_desc_size && g->guild_desc[at + k]; ++k) {
        ds[XEEN_DS_TEXT_BUFFER_0 + k] = g->guild_desc[at + k];
    }
}
/*
 * The spell guild's keys (add-on spell_list_sort first). Esc ends the visit; Up and Down scroll the list a line,
 * PgUp and PgDn ten (the top not below 0); F1..F6 show that member's list (list_member_key; none: the poll again).
 * 1..9 and 0 (the tenth) choose a listed spell: its price (guild_cost), the chosen line and its description, then the
 * wide yes/no with the purchase text, or in the information mode an information box with the spell's text; its
 * answer is guild_confirmed's. Any other key, or a number past the list, polls again. Stops: the member's list, the
 * member's class without a spell list (a non-caster), and a yes/no that cannot open.
 */
xeen_status guild_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned n, doubled, index, spell;
    int cat;
    char out[1000];
    {
        xeen_status st;
        if (spell_list_sort_key(g, key, &st)) {
            return st;
        }
    } /* add-on spell_list_sort (off: 0) */
    if (key == XEEN_KEY_ESCAPE) {
        return guild_end(g);
    }
    if (key == XEEN_KEY_UP || key == XEEN_KEY_DOWN) {
        guild_scroll(g, key);
        return guild_refresh(g);
    }
    if (key == XEEN_KEY_PAGE_UP || key == XEEN_KEY_PAGE_DOWN) {
        int top = (int)g->guild_top + (key == XEEN_KEY_PAGE_UP ? -10 : 10);
        g->guild_top = top < 0 ? 0u : (unsigned)top;
        return guild_refresh(g);
    }
    if (key >= XEEN_KEY_F1 && key <= XEEN_KEY_F6) {
        int r = list_member_key(g, key - XEEN_KEY_F1); /* 3F6F1, shared */
        if (r < 0) {
            return location_stop(g, GUILD_STOP_LIST_MEMBER);
        }
        if (!r) {
            return panel_set(g, GUILD_POLL, GUILD_POLL_BOUNDARY);
        }
        g->guild_top = 0;
        return guild_refresh(g);
    }
    if (key < '0' || key > '9') {
        return panel_set(g, GUILD_POLL, GUILD_POLL_BOUNDARY);
    }
    n = key == '0' ? 10u : (unsigned)(key - '0');
    if (!g->guild_count || n > g->guild_count) {
        return panel_set(g, GUILD_POLL, GUILD_POLL_BOUNDARY);
    }
    cat = guild_category(ds[(uint16_t)(g->guild_record + XEEN_CHARACTER_CLASS)], &doubled);
    if (cat < 0) {
        return location_stop(g, GUILD_STOP_CATEGORY);
    }
    index = g->guild_index[n - 1u + g->guild_top];
    spell = ds[XEEN_DS_CLASS_SPELL_LISTS + XEEN_CLASS_SPELL_LIST * (unsigned)cat + index];
    g->guild_cost = guild_cost(g, spell, doubled);
    g->guild_choice = (uint8_t)index;
    guild_description(g, spell);
    XEEN_BUGFIX_SPELL_DESCRIPTION(ds + XEEN_DS_TEXT_BUFFER_0, 1000u, spell); /* bug-fix hook (off by default) */
    if (g->guild_info) {
        (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_GUILD_SPELL_INFO), spell_name(g, spell),
                       ds_text(g, XEEN_DS_TEXT_BUFFER_0));
    } else {
        (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_GUILD_PURCHASE), ds_text(g, XEEN_DS_TEXT_BUFFER_0),
                       spell_name(g, spell), (unsigned)g->guild_cost);
    }
    ds_put_text(g, XEEN_DS_TEXT_BUFFER, out);
    g->confirm_wide = 1;
    g->confirm_info = g->guild_info;
    if (open_confirm(g, CONFIRM_GUILD, XEEN_DS_TEXT_BUFFER)) {
        return XEEN_SERVICE_REQUIRED;
    }
    g->confirm_wide = 0;
    g->confirm_info = 0;
    return location_stop(g, GUILD_STOP_CONFIRM);
}
/* A spell purchase answered. No: back to the list's poll. Yes: the party pays (too little: the "not enough" message,
 * then the refusal's sound and the list's poll, nothing redrawn); the member learns the spell (its byte counts up),
 * the keeper's purchase voice plays, and the list is built again with its top lowered to show the last ten.
 * Original: 2937A; 29281(0, cost, 0, 0) (291FB's message in an 18F2F mode-0 scroll, 3F6AF 1880E(15h)); +79h +
 * index; 187E9(0, 0), F47C = 0, guild12.voc / parrot2.voc; 3F68D. */
xeen_status guild_confirmed(xeen_game *g, int answer)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    if (!answer) {
        return panel_set(g, GUILD_POLL, GUILD_POLL_BOUNDARY);
    }
    if (!party_pay(g, g->guild_cost)) {
        return scroll0_open(g, MESSAGE_GUILD_PAYMENT, ds_text(g, XEEN_DS_TEXT_BUFFER));
    }
    ++ds[(uint16_t)(g->guild_record + XEEN_CHARACTER_SPELLS + g->guild_choice)];
    voice_cancel(g, 0);
    xeen_write16(m, 0xf47c, 0);
    location_voc(g, ds_text(g, ds[XEEN_DS_WORLD] ? XEEN_TEXT_GUILD_PURCHASE_VOICE_DARKSIDE : XEEN_TEXT_GUILD_PURCHASE_VOICE_CLOUDS));
    exit_event(g, "voice");
    g->guild_count = guild_list(g);
    if (g->guild_top + 10u > g->guild_count) {
        int t = (int)g->guild_count - 10;
        g->guild_top = t < 1 ? 0u : (unsigned)t;
    }
    guild_page(g);
    return panel_set(g, GUILD_POLL, GUILD_POLL_BOUNDARY);
}
/* The guild's keys at its location loop. B (Buy): a member who cannot act is refused, and so (silently: the key is
 * dropped) is one who is not a guild member; else the guild's spell list. S (Spell Info): the membership test only,
 * then the list in Spell Info mode. F1..F6 choose a member; one who is not a guild member hears the keeper's
 * refusal voice. Any other key redraws the location.
 * Original: 26113 by CS:0E90: B 26133 (28CFC, 253C0; 26193 F5FE = 0; 3F05D(member, 0)); S 2616B (253C0,
 * 3F05D(member, 80h)); F1..F6 2619C (2A1F6; guild11.voc / skull1.voc, F47C = 5). */
xeen_status guild_loc_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    if (key == 'B' || key == 'S') {
        xeen_status status = XEEN_OK;
        if (key == 'B' && location_refused(g, &status)) {
            return status; /* 26139's 28CFC message */
        }
        if (!guild_member(g, g->location_member)) {
            xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
            return location_redraw(g);
        }
        g->guild_info = key == 'S';
        return guild_begin(g);
    }
    if (key >= XEEN_KEY_F1 && key <= XEEN_KEY_F6) {
        (void)location_member_key(g, key);
        if (!guild_member(g, g->location_member)) {
            voice_cancel(g, 0);
            xeen_write16(m, 0xf47c, 5);
            location_voc(g, ds_text(g, m->ds[XEEN_DS_WORLD] ? XEEN_TEXT_GUILD_MEMBER_KEY_VOICE_DARKSIDE : XEEN_TEXT_GUILD_MEMBER_KEY_VOICE_CLOUDS));
            exit_event(g, "voice");
        }
    }
    return location_redraw(g);
}
