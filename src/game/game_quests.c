/*
 * The Quests viewer: three lists (the quest items the party carries, the current quests and the auto notes) shown a
 * page at a time in the list window, opened from the character view's items dialog (Q) and from exploration (V).
 * Its begin, the note texts, the lists' lines and page text, the keys and the end. Original: 3E24F (Phase 246 moved
 * it from game_shop.c).
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Each list's first Darkside entry (above the add-on's include: quest_journal_ids mirrors the lists): the Darkside heading comes before it (quest item 35 is "Pass to Castleview",
 * the first of DS:24CC's Darkside names; the quests and the auto notes by their own boundaries). */
enum { QUESTS_DARKSIDE_ITEM = 35, QUESTS_DARKSIDE_QUEST = 29, QUESTS_DARKSIDE_NOTE = 72 };
/* The quest flags (both worlds' quests) and the auto-notes; the note texts hold the quests' texts first, then the
 * notes'. */
enum { QUESTS_QUEST_COUNT = 56, QUESTS_NOTE_COUNT = 128 };
#include "../addon/quest_journal/quests_modal.inc" /* add-on quest_journal: quest_journal_* */
/*
 * Where native play stops when the Quests screen cannot be shown; each value is the original address of the step
 * (tools/xeen_replay/stops.json).
 */
enum {
    QUESTS_STOP_NO_PRESENTATION = 0x3e24f,
    QUESTS_STOP_SPRITES = 0x3e25e,           /* quest.icn */
    QUESTS_STOP_TEXTS = 0x2b6ec,             /* qnotes.bin */
    QUESTS_STOP_LINE_SPACE = 0x3e46f,        /* a line would reach past the text buffer */
    QUESTS_STOP_ITEM_LINE = 0x3e4b5,
    QUESTS_STOP_QUEST_LINE = 0x3e663,
    QUESTS_STOP_NOTE_LINE = 0x3e7a6,
    QUESTS_STOP_TEXT = 0x3e877,               /* the text would reach the roster (B41E) */
    /* variable stop sites (stops_variable.json) */
    QUESTS_STOP_ITEM_SLOT = 0x3e4db,         /* a shown Quest Items line with no item behind it */
    QUESTS_STOP_QUEST_SLOT = 0x3e680,        /* a shown Current Quests line with no quest behind it */
    QUESTS_STOP_NOTE_SLOT = 0x3e7c4          /* a shown Auto Notes line with no note behind it */
};
static xeen_status quests_text(xeen_game *g);
/**
 * Opens the Quests viewer for OWNER (1 the items dialog, else exploration; quests_end returns there): pushes the
 * button set and quest.icn, installs the viewer's buttons (I, Q, A, the two scroll arrows, Esc), sets the UI mode
 * FFh (its old value kept), saves the text block and loads qnotes.bin from the Darkside archive (the note texts:
 * entry k follows the file's k-th NUL), opens the two list windows unless the button row's window is open already, writes
 * the button labels and shows the quest items from the top. Stops without a presentation, without quest.icn or
 * without qnotes.bin.
 * Original: 3E24F, from 45D4A (items, then 45152) and 17E18 (exploration, then 17F07): 190ED, 190A3(quest.icn), the
 * tables 98AC..9898 = DS:1E36..1E5C, 9A1C = FFh, 97C2/97C0 saved, 191DE(1), 2B6C9(qnotes.bin) (entry 0 is never
 * written: the native reads the file's start, as for tavern.bin), 1871B(1Dh/1Eh), 4E38C(1Dh, DS:6FAA), 4AC92.
 */
xeen_status quests_begin(xeen_game *g, enum quests_owner owner)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, world;
    uint16_t h = 0;
    size_t n = 0;
    uint8_t *b;
    if (!g->presentation) {
        return shop_stop(g, QUESTS_STOP_NO_PRESENTATION);
    }
    xeen_presentation_trace_extra("mark dialog-quests"); /* Evidence: 3E24F */
    xeen_presentation_trace_extra(
            "locmark %x",
            (unsigned)xeen_read16(m, XEEN_DS_INPUT_CODE)); /* the location observer's entry form */
    g->quests_trace = 1;
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (!handle_push(g, ds_text(g, XEEN_TEXT_QUESTS_SPRITES), &h)) {
        return shop_stop(g, QUESTS_STOP_SPRITES);
    }
    button_tables(m, XEEN_BUTTONS_QUESTS);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
    g->quests_ui_mode = ds[XEEN_DS_UI_MODE];
    ds[XEEN_DS_UI_MODE] = 0xff;
    text_block_save(m, &g->quests_text);
    world = ds[XEEN_DS_WORLD];
    xeen_select_archive(m, 1);
    b = menu_load(g, ds_text(g, XEEN_TEXT_QUESTS_NOTES_FILE), &n);
    xeen_select_archive(m, world);
    if (!b) {
        return shop_stop(g, QUESTS_STOP_TEXTS);
    }
    free(g->quests_notes);
    g->quests_notes = b;
    g->quests_notes_size = n;
    /* 2B6C9: into the F42C buffer when it is set (2B6D5), else 18975
     * allocates it (the native keeps no DOS memory: 0); 97C0 = 0. */
    text_block_in_file_buffer(m, &g->quests_text);
    g->quests_owner = owner;
    g->quests_mode = QUESTS_ITEMS;
    g->quests_top = 0;
    g->quests_lines = 0;
    memset(g->quests_slot, 0, sizeof g->quests_slot);
    g->quests_opened = !ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_LIST_BUTTONS)];
    if (g->quests_opened) {
        panel_window(g, XEEN_WINDOW_LIST_BUTTONS, 1);
        panel_window(g, XEEN_WINDOW_LIST, 1);
    }
    menu_window_text(g, XEEN_WINDOW_LIST_BUTTONS, WINDOW_FRAMED, ds_text(g, XEEN_TEXT_QUESTS_BUTTONS));
    location_buttons(g);
    return quests_text(g);
}
/* qnotes.bin's text K: the quests' texts first (0..55), then the auto notes'. Original: the 50D4:165C offsets. */
static const char *quests_note(xeen_game *g, unsigned k)
{
    return nul_entry(g->quests_notes, g->quests_notes_size, k);
}
/* The headings quests_line puts before entry INDEX at line K: the Clouds heading before the list's first line when
 * it is a Clouds entry, the Darkside heading before the first Darkside entry (DARK: it was put). */
static unsigned quests_headings(unsigned k, int dark, unsigned index, unsigned darkside_first)
{
    return (unsigned)(!k && !dark && index < darkside_first) + (unsigned)(index >= darkside_first && !dark);
}
/* Appends list entry INDEX (its text ENTRY) at line *K: first the "Clouds of Xeen" heading when it is the list's first
 * line and a Clouds entry, the "Darkside of Xeen" heading before the first Darkside entry (*DARK records that it was
 * put). Returns 0 when a line would pass the 135 slots or ENTRY is missing (the caller's stop). Original: in 3E375,
 * the headings' far texts DS:6E2A and 6E2E, [bp-4] the line, [bp-2] the Darkside heading. */
static int quests_line(xeen_game *g, unsigned *k, int *dark, unsigned index, unsigned darkside_first,
                       const char *entry)
{
    if (*k >= 135u) {
        return 0;
    }
    if (!*k && !*dark && index < darkside_first) {
        g->quests_slot[(*k)++] = far_text(g, XEEN_DS_QUESTS_CLOUDS, 0);
    }
    if (index >= darkside_first && !*dark) {
        if (*k >= 135u) {
            return 0;
        }
        g->quests_slot[(*k)++] = far_text(g, XEEN_DS_QUESTS_DARKSIDE, 0);
        ++*dark;
    }
    if (*k >= 135u || !entry) {
        return 0;
    }
    g->quests_slot[(*k)++] = entry;
    return 1;
}
/* How building a list's lines ended (the caller turns a failure into its stop). */
enum quests_build {
    QUESTS_BUILT,
    QUESTS_LINE_MISSING,       /* quests_line refused a line: past the 135 slots, or a note text the file lacks */
    QUESTS_COUNT_LINE_NO_ROOM  /* a quest item's count line would not fit its line buffer */
};
/* The page's rows: three for the current quests, nine for the other lists. */
static unsigned quests_page_rows(const xeen_game *g)
{
    return g->quests_mode == QUESTS_CURRENT ? 3u : 9u;
}
/* Sets the first COUNT slots to FILLER, the blank line a page shows past its list's end. The slots after them keep
 * whatever an earlier list put there (the original's lines are its stack, which the lists share). */
static void quests_fill(xeen_game *g, unsigned count, const char *filler)
{
    for (unsigned i = 0; i < count; ++i) {
        g->quests_slot[i] = filler;
    }
}
/* The quest items the party can hold several of (their counter is a quantity, not a flag): Phirna Root, King's Mega
 * Credit, the Ruby, Emerald, Sapphire and Diamond Rocks, Monga Melon and Energy Disk. Original: the CS:0756 table. */
static int quests_item_is_counted(unsigned index)
{
    static const uint8_t counted[8] = {17, 26, 79, 80, 81, 82, 83, 84};
    return memchr(counted, (int)index, sizeof counted) != NULL;
}
/* Writes quest item NAME's count line ("N name", a plural 's' or a space after it) into the 32-byte line buffer of
 * slot SLOT, the slot the line will take, and returns it there; NULL when the line passes 63 characters or would
 * reach the page buffer that follows the line buffers. Original: "%d %s%c" at A43F + 32 x the line, in 3E38F. */
static const char *quests_count_line(xeen_game *g, unsigned slot, uint8_t count, const char *name)
{
    uint8_t *ds = g->movement.ds;
    char line[64];
    uint16_t at = (uint16_t)(XEEN_DS_TEXT_BUFFER_0 + 32u * slot);
    int w = snprintf(line, sizeof line, ds_text(g, XEEN_TEXT_QUEST_ITEM_COUNT), (int)count, name,
                     count > 1u ? 's' : ' ');
    if (w < 0 || (size_t)w >= sizeof line || (unsigned)at + (unsigned)w + 1u > XEEN_DS_TEXT_BUFFER_2) {
        return NULL;
    }
    memcpy(ds + at, line, (size_t)w + 1u);
    return (const char *)ds + at;
}
/* The quest items list into the slots from *K: each item the party holds (its counter nonzero) by name, the counted
 * ones by their count line, the Clouds and Darkside parts under their headings. A failure leaves the lines built so
 * far. Original: 3E38F (the counters F077 + n, the names DS:24CC). */
static enum quests_build quests_item_lines(xeen_game *g, unsigned *k)
{
    const uint8_t *ds = g->movement.ds;
    int dark = 0;
    for (unsigned index = 0; index < XEEN_QUEST_ITEM_COUNT; ++index) {
        uint8_t count = ds[(uint16_t)(XEEN_DS_QUEST_ITEMS + XEEN_QUEST_ITEM_FIRST + index)];
        const char *entry = far_text(g, XEEN_DS_QUEST_ITEM_NAMES, index);
        if (!count) {
            continue;
        }
        if (quests_item_is_counted(index)) {
            /* the slot the line will take: after the headings quests_line is about to put before it */
            unsigned slot = *k + quests_headings(*k, dark, index, QUESTS_DARKSIDE_ITEM);
            if (!(entry = quests_count_line(g, slot, count, entry))) {
                return QUESTS_COUNT_LINE_NO_ROOM;
            }
        }
        if (!quests_line(g, k, &dark, index, QUESTS_DARKSIDE_ITEM, entry)) {
            return QUESTS_LINE_MISSING;
        }
    }
    return QUESTS_BUILT;
}
/* A list of flagged entries into the slots from *K: entry n (0 <= n < COUNT) when bit n + 1 of the bit array at FLAGS
 * is set, its text qnotes.bin's entry FIRST_TEXT + n, the Clouds and Darkside parts (from DARKSIDE_FIRST) under their
 * headings. Returns 0 when quests_line refuses a line (the lines built so far stay). Original: the quests 3E58D
 * (2F847(F06F, n + 1)) and the auto notes 3E6CC (2F847(F05F, n + 1), entry n + 38h). */
static int quests_flagged_lines(xeen_game *g, unsigned *k, uint16_t flags, unsigned count, unsigned darkside_first,
                                unsigned first_text)
{
    int dark = 0;
    for (unsigned index = 0; index < count; ++index) {
        if (!xeen_ds_get_bit(g->movement.ds, flags, index + 1u)) {
            continue;
        }
        if (!quests_line(g, k, &dark, index, darkside_first, quests_note(g, first_text + index))) {
            return 0;
        }
    }
    return 1;
}
/* The page's rows L from line quests_top on; 0 when one would be outside the 135 slots or a slot no list wrote. */
static int quests_page_lines(const xeen_game *g, const char *l[9])
{
    for (unsigned i = 0; i < quests_page_rows(g); ++i) {
        int at = g->quests_top + (int)i;
        if (at < 0 || at >= 135 || !g->quests_slot[at]) {
            return 0;
        }
        l[i] = g->quests_slot[at];
    }
    return 1;
}
/* The page text of rows L in the list's format into OUT; snprintf's result. Original: DS:703A / 70D8 / 7113. */
static int quests_page_text(xeen_game *g, const char *const l[9], char *out, size_t size)
{
    if (g->quests_mode == QUESTS_ITEMS) {
        return snprintf(out, size, ds_text(g, XEEN_TEXT_QUESTS_ITEMS_PAGE), l[0], l[1], l[2], l[3], l[4], l[5], l[6],
                        l[7], l[8]);
    }
    if (g->quests_mode == QUESTS_CURRENT) {
        return snprintf(out, size, ds_text(g, XEEN_TEXT_QUESTS_CURRENT_PAGE), l[0], l[1], l[2]);
    }
    return snprintf(out, size, ds_text(g, XEEN_TEXT_QUESTS_NOTES_PAGE), l[0], l[1], l[2], l[3], l[4], l[5], l[6], l[7],
                    l[8]);
}
/* Builds and shows the current list's page from line quests_top. The lists:
 * - the quest items: each item the party holds, by name; the counted ones as "N name" with a plural 's'
 *   (quests_count_line); none: the "no items" text alone;
 * - the current quests: each set quest flag's text (from qnotes.bin); none: line 1 says so; three rows a page;
 * - the auto notes: each set note's text; none: line 1 says so.
 * Each list first blanks its own number of slots (85 items, 84 quests, 128 notes: the original's counts), then puts
 * its lines from slot 0, its Clouds and Darkside parts under their headings (quests_line). A mode past the three is
 * shown as the auto notes. The page (nine rows but for the quests) goes into the page buffer, the list window and
 * its home line are drawn and presented, and the viewer waits for its key, which is kept (no key flush).
 * Stops:
 * - a count line too long or past its line buffer;
 * - a list past its 135 lines (unreachable for the quest items, 85 at most: names are never missing) or a note text
 *   past the end of qnotes.bin;
 * - a row outside the slots or one no list wrote (the original shows stack content there);
 * - a page past its buffer (it would reach the roster).
 * Original: 3E375 (14BBA into AC0F, 4E38C(1Eh), 4E38C(1Eh, DS:715C), 50BF6(18h), the 3E8A3 poll with no 189BC);
 * the blank slots DS:6E26 (the items) and DS:263C (the other lists: the first weapon power name, an empty text); the
 * "none" texts DS:700B / 70BA / 7103. */
static xeen_status quests_text(xeen_game *g)
{
    const char *l[9];
    char out[0x80f];
    unsigned k = 0;
    int n;
    if (g->quests_mode == QUESTS_ITEMS) {
        quests_fill(g, XEEN_QUEST_ITEM_COUNT, far_text(g, XEEN_DS_QUEST_ITEMS_FILLER, 0));
        switch (quests_item_lines(g, &k)) {
            case QUESTS_COUNT_LINE_NO_ROOM:
                return shop_stop(g, QUESTS_STOP_LINE_SPACE);
            case QUESTS_LINE_MISSING:
                return shop_stop(g, QUESTS_STOP_ITEM_LINE);
            case QUESTS_BUILT:
                break;
        }
    } else if (g->quests_mode == QUESTS_CURRENT) {
        quests_fill(g, 84u, far_text(g, XEEN_DS_WEAPON_POWER_NAMES, 0));
        if (!quests_flagged_lines(g, &k, XEEN_DS_QUEST_FLAGS, QUESTS_QUEST_COUNT, QUESTS_DARKSIDE_QUEST, 0)) {
            return shop_stop(g, QUESTS_STOP_QUEST_LINE);
        }
    } else {
        quests_fill(g, 128u, far_text(g, XEEN_DS_WEAPON_POWER_NAMES, 0));
        if (!quests_flagged_lines(g, &k, XEEN_DS_AUTO_NOTES, QUESTS_NOTE_COUNT, QUESTS_DARKSIDE_NOTE,
                                  QUESTS_QUEST_COUNT)) {
            return shop_stop(g, QUESTS_STOP_NOTE_LINE);
        }
    }
    g->quests_lines = (int)k;
    if (!k && g->quests_mode == QUESTS_ITEMS) {
        n = snprintf(out, sizeof out, "%s", ds_text(g, XEEN_TEXT_QUESTS_NO_ITEMS));
    } else {
        if (!k) {
            g->quests_slot[1] = ds_text(g, g->quests_mode == QUESTS_CURRENT ? XEEN_TEXT_QUESTS_NONE_CURRENT
                                                                            : XEEN_TEXT_QUESTS_NO_NOTES);
        }
        if (!quests_page_lines(g, l)) {
            /* a variable stop site: the mode literals stay (stops_variable.json keys it by its expression) */
            return shop_stop(g, g->quests_mode == 0   ? QUESTS_STOP_ITEM_SLOT
                                : g->quests_mode == 1 ? QUESTS_STOP_QUEST_SLOT
                                                      : QUESTS_STOP_NOTE_SLOT);
        }
        n = quests_page_text(g, l, out, sizeof out);
    }
    if (n < 0 || (size_t)n >= sizeof out) {
        return shop_stop(g, QUESTS_STOP_TEXT);
    }
    memcpy(g->movement.ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    quest_journal_listed(g, !k && g->quests_mode == QUESTS_ITEMS ? NULL : l); /* add-on quest_journal */
    menu_window_text(g, XEEN_WINDOW_LIST, WINDOW_FRAMED, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    menu_window_text_on(g, XEEN_WINDOW_LIST, ds_text(g, XEEN_TEXT_QUESTS_HOME));
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_UPPER_SCREEN);
    return panel_set(g, QUESTS_POLL, QUESTS_POLL_BOUNDARY);       /* F5FE keeps the key: no 189BC */
}
/* Esc: closes the list windows when the viewer opened them, frees the note texts, restores the text block, pops the
 * button set and quest.icn, restores the UI mode and returns to the owner: the items dialog redrawn in full, or
 * exploration's command tail. Original: 3E91C (18765(1Eh), 18765(1Dh), 18693, 97C2/97C0, 18FFF, 190CF, 9A1C), then
 * 45D4F (45152) or 17E1D (17F07). */
static xeen_status quests_end(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    quest_journal_end(g); /* add-on quest_journal */
    if (g->quests_opened) {
        panel_window(g, XEEN_WINDOW_LIST, 0);
        panel_window(g, XEEN_WINDOW_LIST_BUTTONS, 0);
    }
    free(g->quests_notes);
    g->quests_notes = NULL;
    g->quests_notes_size = 0;
    text_block_restore(m, &g->quests_text);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    handle_pop(g);
    ds[XEEN_DS_UI_MODE] = g->quests_ui_mode;
    g->panel_stage = 0;
    g->quests_trace = 0;
    xeen_presentation_trace_extra("locend"); /* Evidence: 3E96C (RETF) */
    if (g->quests_owner == QUESTS_FROM_ITEMS) {
        return items_full(g); /* 45D4F: 45152 */
    }
    return command_tail(g); /* 17E1D: 17F07 */
}
/* The viewer's keys: Esc ends it; A, I and Q show the auto notes, the quest items and the current quests from the top;
 * Home the top, End the last line; PgUp/PgDn three lines and the arrows Up/Down one, kept between the top and the
 * last line (with the bug fix quest_up_down on, U and D act as Up and Down). Any other key redraws the page. Original: 3E8AC by
 * CS:072E (3E8EA: below 1 the top; 3E8F8: past the lines the last). */
xeen_status quests_key(xeen_game *g, uint16_t key)
{
    int lines = g->quests_lines, top = g->quests_top;
    switch (xeen_bugfix_quest_key(key)) { /* bug fix quest_up_down (off: the key itself) */
        case XEEN_KEY_ESCAPE:
            return quests_end(g);
        case 'A':
            g->quests_mode = QUESTS_AUTO_NOTES;
            top = 0;
            break;
        case 'I':
            g->quests_mode = QUESTS_ITEMS;
            top = 0;
            break;
        case 'Q':
            g->quests_mode = QUESTS_CURRENT;
            top = 0;
            break;
        case XEEN_KEY_HOME:
            top = 0;
            break;
        case XEEN_KEY_END:
            top = lines - 1;
            break;
        case XEEN_KEY_PAGE_UP:
            top -= 3;
            if (top < 1) {
                top = 0;
            }
            break;
        case XEEN_KEY_PAGE_DOWN:
            top += 3;
            if (top >= lines) {
                top = lines - 1;
            }
            if (top < 1) {
                top = 0;
            }
            break;
        case XEEN_KEY_UP:
            --top;
            if (top < 1) {
                top = 0;
            }
            break;
        case XEEN_KEY_DOWN:
            ++top;
            if (top >= lines) {
                top = lines - 1;
            }
            if (top < 1) {
                top = 0;
            }
            break;
        default:
            break;
    }
    g->quests_top = top;
    return quests_text(g);
}
