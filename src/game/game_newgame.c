#include "game_menu.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/*
 * Where native play stops when a new game cannot be started; each value is the original address of the step
 * (tools/xeen_replay/stops.json).
 */
enum {
    NEWGAME_STOP_SAVE_OVER_CONFIRM = 0x2b2d1,
    NEWGAME_STOP_SPRITES = 0x2b396,          /* choice.icn */
    NEWGAME_STOP_FRAME = 0x2b447,
    NEWGAME_STOP_PARTY_FILE = 0x19987,       /* maze.pty from the state archive */
    NEWGAME_STOP_CREATE = 0x19379,           /* a fresh state archive could not be made */
    NEWGAME_STOP_SLOT_FILES = 0x19629,       /* the host cannot write save files or read the clock */
    NEWGAME_STOP_ROSTER = 0x2b4d5,           /* no buffer for the roster */
    NEWGAME_STOP_NO_RANDOM = 0x2b52b,        /* no random source for the wares */
    NEWGAME_STOP_NAME = 0x2b562              /* the typed game name has no terminator in its buffer */
};

/* ----------------------------------- 16A53 Start a New Game
 * Its own translation unit (Phase 244), over game_internal.h. The menu's 'S': 2AD6D(0) with
 * 8C77 set, the "Start Game Menu" (the slot, "Ok to save over", the 16332
 * name entry: the panel owners, with no scene under them), the 2B3E0 game
 * preference, the 19314 CURs from the CCs, the party of the CURs ("Default
 * Characters"), 41D20, the calendar, and the 19278 FA90 = 1 pair save;
 * then the 16FAB tail of a menu load. A new game has no party creation:
 * the CC's maze.pty already holds six members on map 28. */

/* The main menu's Start: the file block let go (its known bytes forgotten), the Darkside slots listed and the save
 * and load dialog opened with the title "Start"; then the dialog's poll. Stops when the dialog cannot open.
 * Original: 16A53: 18693(F42C) (F42C = 0; B418 keeps the block), 18E77("dark??.wox"), 2AD6D(0) with 5F20 (8C77 set). */
xeen_status menu_new(xeen_game *g)
{
    uint32_t stop;
    xeen_write16(&g->movement, XEEN_DS_FILE_BUFFER_SEGMENT, 0);
    g->f42c_known = 0; /* B418 keeps the freed block */
    (void)panel_list(g, XEEN_TEXT_DARK_SAVES_PATTERN);
    g->dialog_title = XEEN_TEXT_DIALOG_START;
    if ((stop = dialog_open(g)) != 0) {
        return menu_stop(g, stop);
    }
    return menu_set(g, XEEN_MENU_DIALOG, MENU_DIALOG_BOUNDARY);
}
/* Enter or S on the Start dialog's selected slot: a slot with a game asks "Ok to save over %s?" with its name (no goes
 * back to the dialog's loop), an empty one goes on to the game's name at once. Stops when the yes/no cannot open.
 * Original: 2B26E/2B29A; 2937A. */
xeen_status newgame_slot(xeen_game *g)
{
    unsigned slot = g->menu_selected;
    char out[64];
    g->menu_stage = 0;
    if (g->save_slots[slot - 1u].header.name[0]) {
        (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_SAVE_OVER),
                       (const char *)g->save_slots[slot - 1u].header.name);
        ds_put_text(g, XEEN_DS_TEXT_BUFFER, out);
        return open_confirm(g, CONFIRM_NEWGAME_SAVE_OVER, XEEN_DS_TEXT_BUFFER) ? XEEN_SERVICE_REQUIRED : menu_stop(g, NEWGAME_STOP_SAVE_OVER_CONFIRM);
    }
    return name_begin(g);
}
/* Opens the new game's preference prompt: the button set pushed, the prompt's sprites (choice.icn) pushed as its
 * buttons' with their table (A at 68, 167 and W at 166, 167), "Select Game Preference" in the scroll window with the
 * two buttons drawn and shown; then the key poll until Escape, A or W (no scene, no timer). The original's free-space
 * test (600,000 bytes) is not made: the host's save directory always has room. Stops when the sprites cannot load or
 * the window cannot be shown.
 * Original: 2B36B (8C77 set): 419B3 (2B37C's "There is not enough free space" not reached); 190ED; 190A3; the
 * 1E00..1E0E tables (90 x 20); 9896 = 98D4; 1871B(6), DS:5FAE, frames 0 and 2, 50BF6(6); 189E3. */
xeen_status newgame_preference(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint16_t h = 0;
    xeen_button_stack(m->ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (!handle_push(g, ds_text(g, XEEN_TEXT_PREFERENCE_SPRITES), &h)) {
        return panel_stop(g, NEWGAME_STOP_SPRITES);
    }
    button_tables(m, XEEN_BUTTONS_PREFERENCE);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, h);
    panel_window(g, XEEN_WINDOW_SCROLL, 1);
    menu_window_text(g, XEEN_WINDOW_SCROLL, WINDOW_FRAMED, ds_text(g, XEEN_TEXT_PREFERENCE_TITLE));
    buttons_draw(g, XEEN_BUTTON_X(XEEN_BUTTONS_PREFERENCE, 0), XEEN_BUTTON_Y(XEEN_BUTTONS_PREFERENCE, 0),
                 XEEN_BUTTON_ENABLED(XEEN_BUTTONS_PREFERENCE, 0), h, 2u);
    if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCROLL) != XEEN_OK) {
        return panel_stop(g, NEWGAME_STOP_FRAME);
    }
    ++g->scene_returns;
    return panel_set(g, PANEL_PREFERENCE, PANEL_PREFERENCE_BOUNDARY);
}
/* One draw LOW..HIGH from the event random source, with AL (its low byte) as the call's AX: the draws of the wares'
 * regeneration and of Enchant's item. Original: 504DA(low, high) for 41D20's 305E8 draws, with the caller's AL. */
uint16_t newgame_draw(void *user, uint16_t low, uint16_t high, uint16_t al)
{
    xeen_game *g = user;
    uint16_t v = 0;
    (void)xeen_entity_rng_range(g->event_random(g->event_random_user, (uint16_t)(al & 255u)), low,
                                high, &v);
    return v;
}
/* Reads world W's party files from its state file: the name record into DS, the roster into ROSTER and the party
 * file into DS (a Clouds party is 32Ch bytes and leaves the rest as it was), the loaded file's size the party's.
 * Returns 0, or the party-file stop when one is missing or of another size.
 * Original: 18975 of maze.nam (96C5), maze.chr (B41C:B41A) and maze.pty (ED8C); 8582. */
static uint32_t newgame_party(xeen_game *g, unsigned w, uint8_t *roster)
{
    xeen_movement *m = &g->movement;
    xeen_archive cur;
    size_t nn = 0, rn = 0, pn = 0;
    uint8_t party[XEEN_DARK_PARTY_BYTES];
    if (!g->cur[w].bytes ||
        xeen_archive_open(&cur, g->cur[w].bytes, g->cur[w].size) != XR_OK ||
        !xeen_archive_extract_state_member(&cur, ds_text(g, XEEN_TEXT_NEWGAME_NAME_FILE),
                                           m->ds + XEEN_DS_GAME_NAME, XEEN_GAME_NAME_BYTES, &nn) ||
        nn != XEEN_GAME_NAME_BYTES ||
        !xeen_archive_extract_state_member(&cur, ds_text(g, XEEN_TEXT_NEWGAME_ROSTER_FILE),
                                           roster, XEEN_ROSTER_BYTES, &rn) ||
        rn != XEEN_ROSTER_BYTES ||
        !xeen_archive_extract_state_member(&cur, ds_text(g, XEEN_TEXT_NEWGAME_PARTY_FILE),
                                           party, sizeof party, &pn) || !pn) {
        return NEWGAME_STOP_PARTY_FILE;
    }
    memcpy(m->ds + XEEN_DS_PARTY_FILE, party, pn);
    xeen_write16(m, XEEN_DS_LOADED_FILE_SIZE, (uint16_t)pn);
    return 0;
}
/* "Please Wait..." in its window, drawn while the input context (UI mode) is FFh (wait_box_present: shown for the
 * present's retrace; not with add-on please_wait_skip); the mode is then 0.
 * Original: 2B4AE: 9A1C = FFh; 28850 (window 9); 9A1C = 0. */
static void newgame_please_wait(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    ds[XEEN_DS_UI_MODE] = 0xff;
    xeen_window_open(&g->movement, XEEN_WINDOW_WAIT);
    wait_box_present(g, XEEN_TEXT_PLEASE_WAIT);
    ++g->scene_returns;
    ds[XEEN_DS_UI_MODE] = 0;
}
/* The new party's files into DS and ROSTER: Darkside's name record, roster and party from DARK.CUR (the roster
 * buffer bound first), then, with Clouds installed, Clouds' from XEEN.CUR over them, so the Clouds party is the one
 * kept. Each is read with its world's archive selected; the selection then returns to the map's side, and after
 * Clouds to the world that side selected. That side is DARK.CUR's: the name record just read covers DS 96C5..96E3,
 * so its side byte is the map side's byte (XEEN_GAME_NAME_SIDE). Returns 0, or newgame_party's stop.
 * Original: 19953 from DARK.CUR; with DS 95 2B4E1: 191DE(0), maze.nam/chr/pty from XEEN.CUR, 191DE(94). */
static uint32_t newgame_parties(xeen_game *g, uint8_t *roster)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, world;
    uint32_t stop;
    xeen_select_archive(m, 1);
    roster_buffer(m);
    if ((stop = newgame_party(g, 1, roster)) != 0) {
        return stop;
    }
    xeen_select_archive(m, ds[XEEN_DS_MAP_SIDE]);
    if (ds[XEEN_DS_CLOUDS_INSTALLED]) {
        world = ds[XEEN_DS_WORLD];
        xeen_select_archive(m, 0);
        if ((stop = newgame_party(g, 0, roster)) != 0) {
            return stop;
        }
        xeen_select_archive(m, world);
    }
    return 0;
}
/* The year a new game starts in: World of Xeen (Clouds installed), or Darkside alone. */
enum { NEWGAME_YEAR_WORLD = 610, NEWGAME_YEAR_DARKSIDE = 850 };
/* A new game's world state: the shops' wares regenerated (each roll from the event random source), day 1 of the
 * starting year, no play time (the play-time word lies inside the party file just read, F01A in ED8C's, and is
 * zeroed all the same), and the preference chosen at the prompt.
 * Original: 41D20; EFF0 = 1, EFF2 = 610 / 850, F01C:F01A = 0, EDA7. */
static void newgame_fresh_state(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    xeen_item_regenerate_wares(m, newgame_draw, g);
    xeen_write16(m, XEEN_DS_DAY, 1);
    xeen_write16(m, XEEN_DS_YEAR, ds[XEEN_DS_CLOUDS_INSTALLED] ? NEWGAME_YEAR_WORLD : NEWGAME_YEAR_DARKSIDE);
    xeen_write16(m, XEEN_DS_PLAY_SECONDS + 2u, 0);
    xeen_write16(m, XEEN_DS_PLAY_SECONDS, 0);
    ds[XEEN_DS_GAME_PREFERENCE] = (uint8_t)g->newgame_preference;
}
/* The typed name (the text buffer's first LENGTH bytes and its NUL) becomes the game's name; the game starts in
 * Clouds when it is installed, else in Darkside; the add-ons' per-party knowledge is forgotten (identify: a new party
 * knows nothing; spell_list_sort: every list A-Z).
 * Original: strcpy(96C5, A827); 94 = 95 ^ 1. */
static void newgame_name_party(xeen_game *g, size_t length)
{
    uint8_t *ds = g->movement.ds;
    memcpy(ds + XEEN_DS_GAME_NAME, ds + XEEN_DS_TEXT_BUFFER, length + 1u);
    ds[XEEN_DS_WORLD] = (uint8_t)(ds[XEEN_DS_CLOUDS_INSTALLED] ^ 1u);
    if (addons_items(g)) {
        xeen_addons_clear_items(g->addons);
    }
    if (addons_sort(g)) {
        xeen_sls_clear(&g->addons->sort);
    }
}
/* Writes the new game's slot files from DARK.CUR and ROSTER with the new-game flag up, so the save pairs both
 * worlds' files. Returns 0, or the save's stop.
 * Original: 2B572 FA90 = 1; 19278; 2B57D FA90 = 0. */
static uint32_t newgame_save(xeen_game *g, const uint8_t *roster)
{
    xeen_movement *m = &g->movement;
    uint32_t stop;
    xeen_write16(m, XEEN_DS_NEW_GAME_SAVE, 1);
    stop = save_slot_files(g, NULL, 0, g->cur[1].bytes, g->cur[1].size, roster);
    xeen_write16(m, XEEN_DS_NEW_GAME_SAVE, 0);
    return stop;
}
/*
 * Creates a new game in the chosen slot (menu_selected), once its preference was answered and the state files exist:
 * "Please Wait..." is shown; the new party's files are read (newgame_parties); the wares, calendar, play time and
 * preference are reset (newgame_fresh_state); the typed name becomes the game's, with the starting world and the
 * add-ons' party state (newgame_name_party); the slot's files are written (newgame_save). Then the save dialog closes,
 * the slot is the game's and the main menu's tail goes on.
 * Stops, in the order tested: no save-file writer or clock, no memory for the roster, a party file, no event random
 * source, a typed name with no NUL in the text buffer, and the save's own stops. Each is a stop statement here, so
 * every failure goes to the one exit that frees the roster.
 * Original: 2B4AE..2B583; 2B268 -> 2B58E, 16A73 -> 16FAB.
 */
static xeen_status newgame_create(xeen_game *g)
{
    uint8_t *ds = g->movement.ds, *roster = NULL;
    unsigned slot = g->menu_selected;
    uint32_t stop = 0;
    const uint8_t *end;
    newgame_please_wait(g);
    if (!g->save_files->write || !g->save_files->clock) {
        stop = NEWGAME_STOP_SLOT_FILES;
        goto done;
    }
    if (!(roster = malloc(XEEN_ROSTER_BYTES))) {
        stop = NEWGAME_STOP_ROSTER;
        goto done;
    }
    if ((stop = newgame_parties(g, roster)) != 0) {
        goto done;
    }
    if (!g->event_random) {
        stop = NEWGAME_STOP_NO_RANDOM;
        goto done;
    }
    newgame_fresh_state(g);
    if (!(end = memchr(ds + XEEN_DS_TEXT_BUFFER, 0, XEEN_TEXT_BUFFER_BYTES))) {
        stop = NEWGAME_STOP_NAME;
        goto done;
    }
    newgame_name_party(g, (size_t)(end - (ds + XEEN_DS_TEXT_BUFFER)));
    stop = newgame_save(g, roster);
done:
    free(roster);
    if (stop) {
        return menu_stop(g, stop);
    }
    ++g->new_games;
    dialog_end(g);
    ds[XEEN_DS_GAME_SLOT] = (uint8_t)slot;
    return menu_tail(g);
}
/* The new game's preference answered (KEY: A Adventurer, W Warrior, Esc back; any other keeps the preference): the
 * prompt closed, the state files (CURs) created for both worlds when Clouds is installed, else for Darkside, the
 * prompt's button set and sprites popped. Esc goes back to the slots, read again (the CURs stay created); otherwise
 * the game is created (newgame_create). Stops when a state file cannot be created.
 * Original: 2B479 ([BP-8] 0 or 1): 18765(6); 19314 for both worlds (DS 95: 94 = 0, then 1); 18FFF; 190CF; Esc to
 * 2ADDB. */
xeen_status newgame_preferred(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    uint32_t stop;
    g->panel_stage = 0;
    if (key == 'W') {
        g->newgame_preference = XEEN_PREFERENCE_WARRIOR;
    } else if (key == 'A') {
        g->newgame_preference = XEEN_PREFERENCE_ADVENTURER;
    }
    panel_window(g, XEEN_WINDOW_SCROLL, 0);
    if (m->ds[XEEN_DS_CLOUDS_INSTALLED]) {
        m->ds[XEEN_DS_WORLD] = 0;
        if (!cur_create(g)) {
            return menu_stop(g, NEWGAME_STOP_CREATE);
        }
        m->ds[XEEN_DS_WORLD] = 1;
    }
    if (!cur_create(g)) {
        return menu_stop(g, NEWGAME_STOP_CREATE);
    }
    xeen_button_stack(m->ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    handle_pop(g);
    if (key == XEEN_KEY_ESCAPE) {
        if ((stop = dialog_slots(g)) != 0) {
            return menu_stop(g, stop);
        }
        return menu_set(g, XEEN_MENU_DIALOG, MENU_DIALOG_BOUNDARY);
    }
    return newgame_create(g);
}
