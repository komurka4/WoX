/*
 * Load Saved Game (Phase 248, from game_menu.c): the main menu's slot dialog (2AD6D: its ten slot lines, its
 * window and its keys), the no-saves message, and the path of a chosen slot to the 19E82 map load: 1978C's CUR
 * copies of both worlds, the party, the 16FAB tail, and the outer loop (its restart and its world). The menu's
 * own stages, its exit 16FAB and its dispatch are game_menu.c's; the hand-off at 19E82 is game_startup.c's.
 */
#include "game_menu.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when loading a game, its dialog, the inn's restart or the world entry cannot go on; each
 * value is the original address of the step (tools/xeen_replay/stops.json). Most guard a file that did not load.
 */
enum {
    DIALOG_STOP_SPRITES = 0x2ad91,            /* scroll.icn */
    DIALOG_STOP_SLOTS = 0x2ae6b,              /* the save slots' names */
    LOAD_STOP_NO_SAVES_FRAME = 0x18f7f,
    LOAD_STOP_PARTY_FILES = 0x19987,          /* maze.nam, maze.chr, maze.pty from the Darkside state archive */
    LOAD_STOP_PARTY_RESTORE = 0x43dac,        /* the members from the roster */
    LOAD_STOP_PAIR_COPY = 0x197f5,            /* the loaded game's copy of both worlds' state */
    LOAD_STOP_SPELL_NAMES = 0x15ff1,          /* spells.xen */
    LOAD_STOP_MATERIAL_NAMES = 0x16072,       /* MAE.xen */
    LOAD_STOP_POW_VOICE = 0x160f4,            /* pow1.voc */
    LOAD_STOP_LAUGH_VOICE = 0x1611a,          /* laff1.voc */
    LOAD_STOP_MAP_TEXTS = 0x16136,            /* aaze0029.txt */
    RESTART_STOP_RESIDENT_SPRITES = 0x43c4e,  /* one of the eight resident sprite files */
    RESTART_STOP_INN_SPRITES = 0x43ce6,       /* inn.icn */
    RESTART_STOP_MATERIAL_NAMES = 0x43d24,    /* MAE.xen */
    ENTRY_STOP_FACE_SPRITES = 0x2a084,        /* a member's face sprites */
    ENTRY_STOP_DARKSIDE_FACES = 0x2a0a4,      /* dse.fac */
    ENTRY_STOP_MAIN_SPRITES = 0x28d74,        /* main.icn (28D6E) */
    ENTRY_STOP_CLOUDS_RELOCATION = 0x179a6    /* the Clouds-only relocation is not reconstructed */
};
/* The conversions of FORMAT in order, into KINDS (at most CAP - 1, NUL-terminated): 'u' for %u, 's' for %s, '?' for
 * any other; a width's digits after the '%' are skipped. */
static void format_kinds(const char *format, char *kinds, size_t cap)
{
    size_t k = 0, i;
    for (i = 0; format[i] && k < cap - 1u; ++i) {
        if (format[i] == '%') {
            while (format[i + 1] >= '0' && format[i + 1] <= '9') {
                ++i;
            }
            kinds[k++] = format[i + 1] == 'u' ? 'u' : format[i + 1] == 's' ? 's' : '?';
            ++i;
        }
    }
    kinds[k] = 0;
}
/* The arguments dialog_text passes to the menu text: the title; each of the ten slots' marker, name and line; the
 * selected slot's file name. */
#define DIALOG_MENU_KINDS "s" "uss" "uss" "uss" "uss" "uss" "uss" "uss" "uss" "uss" "uss" "s"
/*
 * Builds the save and load dialog's text in the second text buffer: the menu text with the dialog's title (Load
 * unless dialog_title names another), each slot's marker, name and line (an empty slot's line is the empty-slot
 * text), and the selected slot's file name ("DARK%02d.WOX", also left in the file-name buffer; empty when no slot
 * is selected or the selected slot has no name). The names are copied contiguously, so a name without a NUL runs on
 * into the next slot's (only the last is terminated). Returns 1, or 0 when the menu text does not take exactly these
 * arguments or the text is 1,000 bytes or more.
 * Original: 2AD6D(1): F5E8 = "DARK%02d.WOX" of the selection, AC0F = the 5E44 menu with the ten slot lines.
 */
int dialog_text(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    const char *f = ds_text(g, XEEN_TEXT_DIALOG_MENU), *empty = ds_text(g, XEEN_TEXT_DIALOG_EMPTY_SLOT);
    const char *slot_name[10], *slot_line[10];
    char out[1024], names[10 * XEEN_GAME_NAME_BYTES + 1], kinds[40];
    unsigned i;
    int n;
    format_kinds(f, kinds, sizeof kinds);
    if (strcmp(kinds, DIALOG_MENU_KINDS)) {
        return 0;
    }
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_DARK_SLOT_FORMAT_UPPER), (int)g->menu_selected);
    ds_put_text(g, XEEN_DS_FILE_NAME, out);
    for (i = 0; i < 10u; ++i) {
        memcpy(names + XEEN_GAME_NAME_BYTES * i, g->save_slots[i].header.name, XEEN_GAME_NAME_BYTES);
    }
    names[10 * XEEN_GAME_NAME_BYTES] = 0;
    for (i = 0; i < 10u; ++i) {
        slot_name[i] = names + XEEN_GAME_NAME_BYTES * i;
        slot_line[i] = names[XEEN_GAME_NAME_BYTES * i] ? g->save_slots[i].line : empty;
    }
    n = snprintf(out, sizeof out, f, ds_text(g, g->dialog_title ? g->dialog_title : XEEN_TEXT_DIALOG_LOAD),
                 (unsigned)g->save_slots[0].colour, slot_name[0], slot_line[0],
                 (unsigned)g->save_slots[1].colour, slot_name[1], slot_line[1],
                 (unsigned)g->save_slots[2].colour, slot_name[2], slot_line[2],
                 (unsigned)g->save_slots[3].colour, slot_name[3], slot_line[3],
                 (unsigned)g->save_slots[4].colour, slot_name[4], slot_line[4],
                 (unsigned)g->save_slots[5].colour, slot_name[5], slot_line[5],
                 (unsigned)g->save_slots[6].colour, slot_name[6], slot_line[6],
                 (unsigned)g->save_slots[7].colour, slot_name[7], slot_line[7],
                 (unsigned)g->save_slots[8].colour, slot_name[8], slot_line[8],
                 (unsigned)g->save_slots[9].colour, slot_name[9], slot_line[9],
                 g->menu_selected && names[XEEN_GAME_NAME_BYTES * (g->menu_selected - 1u)] ? ds_text(g, XEEN_DS_FILE_NAME)
                                                                          : ds_text(g, XEEN_TEXT_EMPTY_STRING));
    if (n < 0 || (size_t)n >= sizeof out || (size_t)n >= 1000u) {
        return 0;
    }
    memcpy(m->ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    return 1;
}
/* Draws the save and load dialog: its window cleared (framed or not), the slot text (dialog_text) in its text window
 * and the four frame pieces of the dialog's sprites (frames 4, 0, 2, 5).
 * Original: 2B06D/2B12F: window 19h cleared, the AC0F text in window 25h, scroll.icn (98D4). */
void dialog_draw(xeen_game *g, int framed)
{
    uint16_t h = xeen_read16(&g->movement, XEEN_DS_SPRITE_HANDLE);
    menu_window_text(g, XEEN_WINDOW_VIEW_DIALOG, framed, "\r");
    menu_window_text(g, XEEN_WINDOW_VIEW_DIALOG_TEXT, WINDOW_TEXT_ONLY, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    exit_draw(g, h, 4, 39, 26, 0);
    exit_draw(g, h, 0, 187, 26, 0);
    exit_draw(g, h, 2, 187, 111, 0);
    exit_draw(g, h, 5, 132, 123, 0);
}
/*
 * Opens the save and load dialog (titled by dialog_title): the portrait clicks saved and turned off, the new-game
 * save flag cleared, the button set pushed and the dialog's sprites (scroll.icn) pushed as its buttons' sprites with
 * the ten slots' button table; then the slots are listed and drawn (dialog_slots). Returns 0, or the stop of a step
 * that failed.
 * Original: 2AD6D entry: 98D6 saved and cleared, FA90 = 0, 190ED, 190A3(scroll.icn) (96C1 up, 95A8[96C1] = 98D4),
 * the 190A..196C tables, then 2ADDB.
 */
uint32_t dialog_open(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint16_t h = 0;
    g->menu_faces = m->ds[XEEN_DS_PORTRAIT_CLICKS];
    m->ds[XEEN_DS_PORTRAIT_CLICKS] = 0;
    xeen_write16(m, XEEN_DS_NEW_GAME_SAVE, 0);
    xeen_button_stack(m->ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (!handle_push(g, ds_text(g, XEEN_TEXT_DIALOG_SPRITES), &h)) {
        return DIALOG_STOP_SPRITES;
    }
    button_tables(m, XEEN_BUTTONS_LIST10);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, h);
    return dialog_slots(g);
}
/* Lists the dialog's ten slots: no selection, every slot's table cleared, then each Darkside slot file's header read
 * (a missing file leaves its slot empty; one shorter than the header stops) with its line ("A" or "W" for the
 * preference, and the level); the dialog's window opened, its text built, drawn and presented. Also the way back
 * after a cancelled game preference. Returns 0, or the stop of the failed step.
 * Original: 2ADDB (199B7 and the 5E1F lines, 1871B(19h), the first draw); from 2B4AB. */
uint32_t dialog_slots(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    unsigned i;
    g->menu_redraw = 0;
    g->menu_selected = 0;
    memset(g->save_slots, 0, sizeof g->save_slots);
    for (i = 0; i < 10u; ++i) {
        int r;
        {
            char out[16];
            (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_DARK_SLOT_FORMAT), (int)(i + 1u));
            ds_put_text(g, XEEN_DS_FILE_NAME, out);
        }
        r = menu_header(g, ds_text(g, XEEN_DS_FILE_NAME), 1, &g->save_slots[i].header);
        if (r < 0) {
            continue;
        }
        if (!r) {
            return DIALOG_STOP_SLOTS;
        }
        (void)snprintf(g->save_slots[i].line, sizeof g->save_slots[i].line, ds_text(g, XEEN_TEXT_DIALOG_SLOT_MARK),
                       g->save_slots[i].header.preference ? 'W' : 'A', (unsigned)g->save_slots[i].header.level);
    }
    xeen_window_open(m, XEEN_WINDOW_VIEW_DIALOG);
    if (!dialog_text(g)) {
        return DIALOG_STOP_TEXT;
    }
    dialog_draw(g, 1);
    if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW_DIALOG) != XEEN_OK) {
        return DIALOG_STOP_FRAME;
    }
    ++g->scene_returns;
    return 0;
}
/** Opens the menu's Load dialog. */
xeen_status dialog_begin(xeen_game *g)
{
    uint32_t stop;
    g->dialog_title = XEEN_TEXT_DIALOG_LOAD;
    if ((stop = dialog_open(g)) != 0) {
        return menu_stop(g, stop);
    }
    return menu_set(g, XEEN_MENU_DIALOG, MENU_DIALOG_BOUNDARY);
}
/* Closes the save and load dialog: its window (25) closed, its sprites and button set popped, the portrait clicks put
 * back as they were before it, the key code cleared. The selection stays in menu_selected (the original's AL).
 * Original: 2B58E: 18765(19h), 190CF (4AA3E(98D4); 98D4 = 95A8[96C1]; 96C1 down), 18FFF, 98D6, F5FE = 0. */
void dialog_end(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_window_close(m, XEEN_WINDOW_VIEW_DIALOG);
    handle_pop(g);
    xeen_button_stack(m->ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    m->ds[XEEN_DS_PORTRAIT_CLICKS] = g->menu_faces;
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
}
/* The menu's Load without any save: "There are no saved games to load!" in the scroll window, with the button set
 * pushed, the key cleared and the UI mode (saved for the close) FFh; then a key poll with no scene. Stops when the
 * window cannot be presented.
 * Original: 18F2F(0, text) mode 0 (DS 48A5): 190ED + 1919B, F5FE = 0, 9A1C saved and FFh, window 6, 189E3. */
xeen_status menu_no_saves(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    char text[160];
    xeen_button_stack(m->ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    g->scroll_mode = m->ds[XEEN_DS_UI_MODE];
    m->ds[XEEN_DS_UI_MODE] = 0xff;
    (void)snprintf(text, sizeof text, ds_text(g, XEEN_DS_MESSAGE_FORMAT), ds_text(g, XEEN_TEXT_NO_SAVED_GAMES));
    xeen_window_open(m, XEEN_WINDOW_SCROLL);
    menu_window_text(g, XEEN_WINDOW_SCROLL, WINDOW_FRAMED, text);
    if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCROLL) != XEEN_OK) {
        return menu_stop(g, LOAD_STOP_NO_SAVES_FRAME);
    }
    ++g->scene_returns;
    return menu_set(g, XEEN_MENU_SCROLL_POLL, SCROLL0_BOUNDARY);
}
/* Makes the current slot's file of the selected world (DS 94) that world's state file: "%s%02d.wox" read through the
 * host's save files and kept in memory as its "%s.cur" (the host's files are never written); the file-name buffer
 * ends with the CUR's name, and the world's archive is selected. Returns 0 when the file is missing (the original's
 * fatal exit). Of the original's memory metrics only the free-memory word is written (dos_free_memory).
 * Original: 1978C (the slot 95A6, F5E8; 2C80D from 197F5; 18886's B410..B415, of which B414). */
int menu_copy(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    unsigned w = m->ds[XEEN_DS_WORLD] ? 1u : 0u;
    size_t n = 0;
    uint8_t *f;
    char out[32];
    const char *prefix = ds_text(g, w ? XEEN_TEXT_DARKSIDE_PREFIX : XEEN_TEXT_CLOUDS_PREFIX);
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_SLOT_FILE_FORMAT), prefix, (int)m->ds[XEEN_DS_GAME_SLOT]);
    ds_put_text(g, XEEN_DS_FILE_NAME, out);
    f = g->save_files->read(g->save_files->user, out, &n);
    if (!f) {
        return 0;
    }
    dos_free_memory(m);
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_CUR_FILE_FORMAT), prefix);
    ds_put_text(g, XEEN_DS_FILE_NAME, out);
    cur_replace(g, w, f, n);
    xeen_select_archive(m, m->ds[XEEN_DS_WORLD]);
    return 1;
}
/*
 * Restores the party of the game being entered from Darkside's state file (the roster buffer bound first): its name
 * record into DS, its party file (either world's size) and the roster through the party library (which copies the
 * active members); the roster becomes the game's (for the inn) and the loaded file's size the party file's; then the
 * map side's archive is selected. Stops when a file is missing or of another size, or the party library refuses.
 * Original: 19953 (maze.nam -> 96C5, maze.chr, maze.pty -> ED8C from archive 1's CUR), 191DE(96E2); 43DA5.
 */
xeen_status menu_party(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_archive cur;
    size_t nn = 0, rn = 0, pn = 0;
    uint8_t *roster = malloc(XEEN_ROSTER_BYTES), party[XEEN_DARK_PARTY_BYTES], name[XEEN_GAME_NAME_BYTES];
    xeen_select_archive(m, 1);
    roster_buffer(m);
    if (!roster || !g->cur[1].bytes ||
        xeen_archive_open(&cur, g->cur[1].bytes, g->cur[1].size) != XR_OK ||
        !xeen_archive_extract_state_member(&cur, ds_text(g, XEEN_TEXT_STATE_NAME_FILE), name, sizeof name, &nn) ||
        nn != sizeof name ||
        !xeen_archive_extract_state_member(&cur, ds_text(g, XEEN_TEXT_STATE_ROSTER_FILE),
                                           roster, XEEN_ROSTER_BYTES, &rn) ||
        rn != XEEN_ROSTER_BYTES ||
        !xeen_archive_extract_state_member(&cur, ds_text(g, XEEN_TEXT_STATE_PARTY_FILE), party, sizeof party, &pn) ||
        (pn != XEEN_DARK_PARTY_BYTES && pn != XEEN_CLOUDS_PARTY_BYTES)) {
        free(roster);
        return menu_stop(g, LOAD_STOP_PARTY_FILES);
    }
    memcpy(m->ds + XEEN_DS_GAME_NAME, name, sizeof name);
    /* maze.pty to ED8C, then 43DA5 copies the members ED8E.. from the roster. */
    if (xeen_party_restore(roster, rn, party, pn, pn == XEEN_DARK_PARTY_BYTES, m->ds,
                           sizeof m->ds) != XP_OK) {
        free(roster);
        return menu_stop(g, LOAD_STOP_PARTY_RESTORE);
    }
    free(g->roster);
    g->roster = roster;                    /* B41A for the inn */
    xeen_write16(m, XEEN_DS_LOADED_FILE_SIZE, (uint16_t)pn); /* the last 4A877: maze.pty */
    xeen_select_archive(m, m->ds[XEEN_DS_MAP_SIDE]);
    return XEEN_OK;
}
/* After the slot: 1978C for both worlds, the 16FAB tail, 15FE9..16159,
 * 43C37, 16169 -> 1791F's prologue, 2A040, 28D6E and 98D0; the session
 * then waits at the 19E82 map load. */
/**
 * Continues after a save was loaded from the menu: copies the state and starts the game at the
 * loaded map.
 */
static xeen_status menu_loaded(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    if (!menu_copy(g)) {
        return menu_stop(g, LOAD_STOP_PAIR_COPY);
    }
    xeen_select_archive(m, !m->ds[XEEN_DS_WORLD]);
    if (!menu_copy(g)) {
        return menu_stop(g, LOAD_STOP_PAIR_COPY);
    }
    xeen_select_archive(m, !m->ds[XEEN_DS_WORLD]);
    addons_slot_load(g, (int)(int8_t)m->ds[XEEN_DS_GAME_SLOT]); /* add-on identify */
    return menu_tail(g);
}
/* The main menu's way into a game (after a load or a new game): the menu exits (menu_exit), then the game's resident
 * files are loaded: the spell names (77) and material names (131) as resident tables, the miss and pow voices, the
 * laugh voice into the file block (which the mirror's buffer then shares), and the map texts (aaze0029.txt) as the
 * event texts; then the game restarts from its party (outer_restart). Stops when a file cannot be loaded.
 * Original: 16FAB's exit with [BP-2] 0; 15FE9; 16159. */
xeen_status menu_tail(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *r;
    size_t n = 0;
    uint32_t stop;
    if ((stop = menu_exit(g)) != 0) {
        return menu_stop(g, stop);
    }
    /* 15FE9: spells.xen (F868, 77 entries) and MAE.xen (F65C, 131), the VOCs,
     * aaze0029.txt and the event script's 5056A buffer, all DOS memory (segment 0). F606 is the tables' loop counter there (1600A
     * F606 = 1, 16038 against 4Dh, 1605A INC), so each table leaves its entry count in it (xeen_resident_table's
     * answer); the word is the game's exit code later (game_exit.c). */
    if (!(r = menu_load(g, ds_text(g, XEEN_TEXT_LOAD_SPELL_NAMES), &n))) {
        return menu_stop(g, LOAD_STOP_SPELL_NAMES);
    }
    xeen_write16(m, XEEN_DS_SPELLS_FILE_SEGMENT, 0);
    xeen_write16(m, XEEN_DS_SPELLS_FILE, 0);
    xeen_write16(m, 0xf606, (uint16_t)xeen_resident_table(m, XEEN_DS_SPELL_NAMES, 0, r, n, XEEN_SPELL_NAMES_LIMIT));
    free(r);
    if (!(r = menu_load(g, ds_text(g, XEEN_TEXT_LOAD_MATERIAL_NAMES), &n))) {
        return menu_stop(g, LOAD_STOP_MATERIAL_NAMES);
    }
    xeen_write16(m, XEEN_DS_MAE_FILE_SEGMENT, 0);
    xeen_write16(m, XEEN_DS_MAE_FILE, 0);
    xeen_write16(m, 0xf606, (uint16_t)xeen_resident_table(m, XEEN_DS_MATERIAL_NAMES, 0, r, n, XEEN_MATERIAL_NAMES_LIMIT));
    free(r);
    if (!menu_load_word(g, XEEN_DS_MISS_VOICE_SEGMENT, ds_text(g, XEEN_TEXT_MISS_VOICE)) ||
        !menu_load_word(g, XEEN_DS_POW_VOICE_SEGMENT, ds_text(g, XEEN_TEXT_POW_VOICE))) {
        return menu_stop(g, LOAD_STOP_POW_VOICE);
    }
    /* 16117: F42C = 18975(laff1.voc), a new block (its bytes are
     * the f42c model); 16125/16128: B416:B418 = F42C:0, the mirror's buffer. */
    if (!(r = menu_load(g, ds_text(g, XEEN_TEXT_LAUGH_VOICE), &n))) {
        return menu_stop(g, LOAD_STOP_LAUGH_VOICE);
    }
    g->f42c_known = 0;
    f42c_load(g, r, n);
    free(r);
    xeen_write16(m, XEEN_DS_FILE_BUFFER_SEGMENT, 0);
    xeen_write16(m, 0xb418, xeen_read16(m, XEEN_DS_FILE_BUFFER_SEGMENT));
    xeen_write16(m, 0xb416, 0);
    if (!menu_load_word(g, XEEN_DS_MAP_TEXTS_SEGMENT, ds_text(g, XEEN_TEXT_LOAD_MAP_TEXTS))) {
        return menu_stop(g, LOAD_STOP_MAP_TEXTS);
    }
    xeen_write16(m, XEEN_DS_EVENT_SCRIPT_SEGMENT, 0); /* 16146/1614A: the event script's buffer (5056A) */
    xeen_write16(m, XEEN_DS_EVENT_SCRIPT_OFFSET, 0);
    xeen_write16(m, XEEN_DS_EVENT_TEXTS_SEGMENT, xeen_read16(m, XEEN_DS_MAP_TEXTS_SEGMENT));
    xeen_write16(m, XEEN_DS_EVENT_TEXTS_OFFSET, 0);
    return outer_restart(g);
}
/* The game's restart from its party, after the menu or a defeat: the map side's archive selected (after a defeat,
 * the defeated session's name record names it); every sprite above the mouse's freed and the eight resident sprite
 * files and inn.icn loaded again; the nine map records cleared; the material names reloaded from Darkside's
 * archive; the party restored (menu_party); the dawn flag set when it is night. A party at the inn (map 0) goes to
 * the inn; any other enters its world (outer_world) after the UI mode, the portrait clicks, the roster's face
 * sprites and the view window are reset. Stops when a resident file cannot be loaded.
 * Original: 16159 (the 15FE9 outer loop, or 16179 after 1791F returned 2): 191DE(96E2), 43C37, the inn 43E0B;
 * 16169. */
xeen_status outer_restart(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *r;
    size_t n = 0;
    unsigned i;
    uint8_t world;
    xeen_select_archive(m, m->ds[XEEN_DS_MAP_SIDE]);
    (void)xeen_sprite_free_from(&g->sprites, (uint16_t)(xeen_read16(m, XEEN_DS_MOUSE_SPRITES) + 1u));
    {
        static const uint16_t words[8] = {XEEN_DS_GLOBAL_SPRITES, XEEN_DS_BORDER_SPRITES, XEEN_DS_SPELLFX_SPRITES,
                                          XEEN_DS_FECP_SPRITES,   XEEN_DS_BLESS_SPRITES,  XEEN_DS_HPBARS_SPRITES,
                                          XEEN_DS_RESTOREX_SPRITES, XEEN_DS_CHARPOW_SPRITES};
        static const uint16_t names[8] = {XEEN_TEXT_RESIDENT_GLOBAL, XEEN_TEXT_RESIDENT_BORDER, XEEN_TEXT_RESIDENT_SPELL_EFFECTS, XEEN_TEXT_RESIDENT_FECP,
                                          XEEN_TEXT_RESIDENT_BLESS, XEEN_TEXT_RESIDENT_HP_BARS, XEEN_TEXT_RESIDENT_RESTORE, XEEN_TEXT_RESIDENT_CHARACTER_POWER};
        for (i = 0; i < 8u; ++i) {
            if (!menu_sprite(g, words[i], ds_text(g, names[i]))) {
                return menu_stop(g, RESTART_STOP_RESIDENT_SPRITES);
            }
        }
    }
    memset(m->ds + XEEN_DS_ROSTER_FACE_SPRITES, 0, XEEN_ROSTER_FACE_BYTES);
    if (!menu_sprite(g, XEEN_DS_INN_SPRITES, ds_text(g, XEEN_TEXT_INN_SPRITES))) {
        return menu_stop(g, RESTART_STOP_INN_SPRITES);
    }
    memset(m->ds + XEEN_DS_MAP_DATA, 0, 9u * XEEN_MAP_RECORD_BYTES);
    world = m->ds[XEEN_DS_WORLD];
    xeen_select_archive(m, 1); /* 33CD8 is DOS memory */
    if (!(r = menu_load(g, ds_text(g, XEEN_TEXT_RESTART_MATERIAL_NAMES), &n))) {
        return menu_stop(g, RESTART_STOP_MATERIAL_NAMES);
    }
    xeen_write16(m, XEEN_DS_MAE_FILE_SEGMENT, 0);
    xeen_write16(m, XEEN_DS_MAE_FILE, 0);
    xeen_select_archive(m, world);
    (void)xeen_resident_table(m, XEEN_DS_MATERIAL_NAMES, 0, r, n, XEEN_MATERIAL_NAMES_LIMIT);
    free(r);
    {
        xeen_status s = menu_party(g);
        if (s != XEEN_OK) {
            return s;
        }
    }
    m->ds[XEEN_DS_DAWN_PENDING] = (uint8_t)(xeen_read16(m, XEEN_DS_MINUTE_OF_DAY) < XEEN_NIGHT_ENDS);
    if (!m->ds[XEEN_DS_PARTY_MAP]) {
        g->inn_visited = 0;
        return inn_begin(g);
    } /* map 0: the 43E0B inn */
    m->ds[XEEN_DS_UI_MODE] = 0;
    m->ds[XEEN_DS_PORTRAIT_CLICKS] = 1;
    (void)xeen_sprite_free_from(&g->sprites,
                                (uint16_t)(xeen_read16(m, XEEN_DS_CHARPOW_SPRITES) + 1u));
    memset(m->ds + XEEN_DS_ROSTER_FACE_SPRITES, 0, XEEN_ROSTER_FACE_BYTES);
    m->ds[0x8d28] = 0;
    xeen_window_close(m, XEEN_WINDOW_VIEW);
    return outer_world(g);
}
/* The exploration loop's state at a world's entry: no treasure pending, combat mode 1, the loop's exit 0, no step
 * sound, no fall scene, member selector 1, the monsters disabled, no defeat, no sign, the step parity and the flips
 * 0, no door opening, Clairvoyance still, no portrait highlighted. FAFA and F9A2 are written as the original writes
 * them (it never reads either back). Original: 1791F's prologue. */
static void exploration_loop_reset(xeen_movement *m)
{
    m->ds[XEEN_DS_TREASURE_ITEMS_PENDING] = 0;
    xeen_write16(m, XEEN_DS_TREASURE_GEMS, 0);
    xeen_write16(m, XEEN_DS_TREASURE_GEMS + 2u, 0);
    xeen_write16(m, XEEN_DS_TREASURE_GOLD, 0);
    xeen_write16(m, XEEN_DS_TREASURE_GOLD + 2u, 0);
    m->ds[XEEN_DS_COMBAT_MODE] = 1;
    xeen_write16(m, XEEN_DS_LOOP_EXIT, 0);
    m->ds[XEEN_DS_STEP_SOUND] = 0;
    xeen_write16(m, 0xfafa, 0);
    m->ds[XEEN_DS_FALL_SCENE] = 0;
    m->ds[XEEN_DS_MEMBER_SELECTOR] = 1;
    xeen_write16(m, 0xf9a2, 1);
    m->ds[XEEN_DS_MONSTERS_ENABLED] = 0;
    m->ds[XEEN_DS_PARTY_DEFEATED] = 0;
    m->ds[XEEN_DS_SIGN_SHOWN] = 0;
    m->ds[XEEN_DS_STEP_PARITY] = 0;
    xeen_write16(m, XEEN_DS_SURFACE_FLIP, 0);
    xeen_write16(m, XEEN_DS_STEP_FLIP, 0);
    xeen_write16(m, XEEN_DS_TURN_FLIP, 0);
    m->ds[XEEN_DS_DOOR_OPENING] = 0;
    clairvoyance_still(m->ds);
    m->ds[XEEN_DS_HIGHLIGHTED_PORTRAIT] = XEEN_NO_PORTRAIT;
}
/* Enters the party's world: the exploration loop's state reset (exploration_loop_reset), the members' face sprites
 * loaded (with Darkside's dse.fac for a party of eight or fewer) and main.icn; the session then waits at the map
 * load for the host (session_ready). Stops when a sprite file cannot be loaded, and for a Clouds map
 * without Clouds installed (its relocation is not reconstructed). Also the inn's way out.
 * Original: 16169 -> 1791F's prologue, 2A040, 28D6E and 98D0, then 19E82 (after the inn: 44553). */
xeen_status outer_world(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    unsigned i;
    exploration_loop_reset(m);
    {
        unsigned count = m->ds[XEEN_DS_PARTY_COUNT];
        char name[16];
        for (i = 0; i < count; ++i) {
            (void)snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_FACE_FILE_FORMAT),
                           (int)((count > 8u ? i : m->ds[XEEN_DS_PARTY_MEMBERS + i]) + 1u));
            if (!menu_sprite(g, (uint16_t)(XEEN_DS_MEMBER_FACE_SPRITES + 2u * i), name)) {
                return menu_stop(g, ENTRY_STOP_FACE_SPRITES);
            }
        }
        if (count <= 8u && !menu_sprite(g, XEEN_DS_DARKSIDE_FACE_SPRITES, ds_text(g, XEEN_TEXT_DARKSIDE_FACES))) {
            return menu_stop(g, ENTRY_STOP_DARKSIDE_FACES);
        }
    }
    if (!menu_sprite(g, XEEN_DS_SPRITE_HANDLE, ds_text(g, XEEN_TEXT_MAIN_SPRITES))) {
        return menu_stop(g, ENTRY_STOP_MAIN_SPRITES);
    }
    if (!m->ds[XEEN_DS_CLOUDS_INSTALLED] && !m->ds[XEEN_DS_MAP_SIDE]) {
        return menu_stop(g, ENTRY_STOP_CLOUDS_RELOCATION);
    }
    xeen_write16(m, XEEN_DS_MAP_SPRITE_BASE, (uint16_t)(xeen_read16(m, XEEN_DS_SPRITE_HANDLE) + 1u));
    g->session_ready = 1;
    g->menu_stage = XEEN_MENU_SESSION;
    return service_wait(g, MENU_LOAD_BOUNDARY);
}
/* A digit KEY in a save dialog ('1'..'9', '0' slot 10): the selected slot again deselects it; another slot becomes
 * the only selection, its row in colour 15. The main menu's and the control panel's slot dialogs share it.
 * Original: 2AD6D. */
void dialog_select(xeen_game *g, uint16_t key)
{
    unsigned slot = key == '0' ? 10u : (unsigned)(key - '0');
    if (g->menu_selected == slot) {
        g->save_slots[slot - 1u].colour = 0;
        g->menu_selected = 0;
    } else {
        unsigned i;
        for (i = 0; i < 10u; ++i) {
            g->save_slots[i].colour = 0;
        }
        g->menu_selected = (uint8_t)slot;
        g->save_slots[slot - 1u].colour = 0x0f;
    }
}
/** Handles a key in the Load/Save dialog: selection, Enter or S to choose, Escape to leave. */
xeen_status dialog_key(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint16_t key = xeen_read16(m, XEEN_DS_INPUT_CODE);
    unsigned slot;
    if (key == '\r' || key == 'S') {
        if (g->menu_selected && g->dialog_title == XEEN_TEXT_DIALOG_START) {
            return newgame_slot(g); /* 2B29A: Start */
        }
        if (!g->menu_selected || !g->save_slots[g->menu_selected - 1u].header.name[0]) {
            return menu_set(g, XEEN_MENU_DIALOG, MENU_DIALOG_BOUNDARY);
        }
        xeen_write16(m, XEEN_DS_MAP_FIELD(ID), 0);
        slot = g->menu_selected;
        dialog_end(g);
        m->ds[XEEN_DS_GAME_SLOT] = (uint8_t)slot;
        return menu_loaded(g);
    }
    if (key == XEEN_KEY_ESCAPE) {
        g->menu_selected = 0;
        dialog_end(g);
        m->ds[XEEN_DS_GAME_SLOT] = 0;
        return menu_set(g, XEEN_MENU_FRAME, MENU_FRAME_BOUNDARY);
    }
    if (key >= '0' && key <= '9') {
        dialog_select(g, key);
        if (!dialog_text(g)) {
            return menu_stop(g, DIALOG_STOP_TEXT);
        }
        dialog_draw(g, 0);
        if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW_DIALOG) != XEEN_OK) {
            return menu_stop(g, DIALOG_STOP_FRAME);
        }
        ++g->scene_returns;
    }
    return menu_set(g, XEEN_MENU_DIALOG, MENU_DIALOG_BOUNDARY);
}
