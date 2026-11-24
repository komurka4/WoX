/*
 * The 28876 line entry (Phase 248, from game_panel.c, whose Save first needed it): the 2894F name entry of a game
 * (A827) or a created character, operation 21's word (2E8E5: its 2E93B mirror and compare), their key and their
 * end, and the entry's poll loop (288BF's 4EA0D and 189E3, 2890C's ticks). Its owners are name_owner's (an
 * amount's keys, 28ABA, are game_bank.c's).
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when a name or a word cannot be entered; each value is the original address of the step
 * (tools/xeen_replay/stops.json).
 */
enum {
    NAME_ENTRY_STOP_FRAME = 0x2898d,
    WORD_STOP_OPERATION = 0x32898,           /* a word entry from neither operation 21 nor 55 */
    WORD_STOP_TITLE = 0x328de,               /* the prompt's title has no end */
    WORD_STOP_TEXT = 0x2e90c,                /* the prompt does not fit */
    MIRROR_STOP_NO_MEMORY = 0x2ea04,         /* the mirror's destinations could not be read */
    MIRROR_STOP_TABLE = 0x2ea2a,             /* a destination past the loaded table */
    MIRROR_STOP_SOUND = 0x2ea3e              /* the transport's effect */
};
/* TEXT's first 1000 bytes in hex, the observers' evidence form, in a static buffer that the next call overwrites. */
static const char *hex_text(const char *text)
{
    static char hex[2001];
    size_t k;
    for (k = 0; k < 1000u && text[k]; ++k) {
        snprintf(hex + 2u * k, 3, "%02x", (unsigned char)text[k]);
    }
    hex[2u * k] = 0;
    return hex;
}
/* The buffer the entry types into, by its owner: operation 21's word (its own), a new character's name (its own), or
 * the game's name (the event text buffer A827). The amount entry has none (amount_key keeps its digits). */
static uint8_t *entry_buffer(xeen_game *g)
{
    return g->name_owner == NAME_WORD ? (uint8_t *)g->word
           : g->name_owner            ? (uint8_t *)g->create_name
                                      : g->movement.ds + XEEN_DS_TEXT_BUFFER;
}
/* The most characters the entry takes: 30 for the word, 10 for a character's name, 20 for the game's name (the
 * lengths 2E8E5, 3FF1C and 16332 pass to 2894F). */
static unsigned entry_limit(const xeen_game *g)
{
    return g->name_owner == NAME_WORD ? 30u : g->name_owner ? 10u : 20u;
}
/**
 * Draws the name entry line (a character's name, operation 21's word or the game's name) with its cursor; EMPTY
 * draws it without text. The game's name: 16332(A827): 190ED, 1919B, 1871B(6), "Enter a Name for this Game"; then
 * 2894F(A827, 20, C8h, 6): 8C52 = 1, SI = 0, 190ED, 1919B, the cursor line. Window 6 (presentation): the 4772
 * heading, then at its text cursor the input line (5871 empty, 587F with the name; width C8h).
 */
static void name_text(xeen_game *g, int empty)
{
    char line[96], text[192];
    /* A character's name (3FF1C: heading 7418, its own buffer).
     * operation 21's word (2E8E5: AC0F, the title line). */
    const char *name = (const char *)entry_buffer(g);
    if (empty) {
        (void)snprintf(line, sizeof line, ds_text(g, XEEN_TEXT_ENTRY_FIELD), 0xc8);
    } else {
        (void)snprintf(line, sizeof line, ds_text(g, XEEN_TEXT_ENTRY_FIELD_TEXT), 0xc8, name);
    }
    (void)snprintf(text, sizeof text, "%s%s",
                   ds_text(g, g->name_owner == NAME_WORD ? XEEN_DS_TEXT_BUFFER_2
                              : g->name_owner    ? XEEN_TEXT_ENTRY_CHARACTER_NAME
                                                 : XEEN_TEXT_ENTRY_GAME_NAME),
                   line);
    if (g->name_owner == NAME_WORD) {
        xeen_presentation_trace_extra("wordtext 6 %s",
                                      hex_text(text)); /* Evidence: 2E8E5's 4E38C(6) */
    }
    menu_window_text(g, XEEN_WINDOW_SCROLL, WINDOW_FRAMED, text);
    input_line(g, 6, line);
}
/* Routes the keys to the entry (the button tables pushed, the defaults installed) and shows its prompt with the
 * empty line in the scroll; 0 when the scroll could not be presented. */
static int entry_prompt_shown(xeen_game *g)
{
    xeen_button_stack(g->movement.ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    button_defaults(&g->movement);
    name_text(g, 1);
    return xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCROLL) == XEEN_OK;
}
/** Starts the name entry (2894F): routes the keys to the menu and draws the empty line. */
xeen_status name_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    button_defaults(m);
    panel_window(g, XEEN_WINDOW_SCROLL, 1);
    ds[XEEN_DS_TEXT_ENTRY] = 1;
    g->panel_name_length = 0;
    if (!entry_prompt_shown(g)) {
        return panel_stop(g, NAME_ENTRY_STOP_FRAME);
    }
    return name_poll(g);
}
/* Starts the line entry's key loop: the sign flag and the monsters' countdown saved for the next key and cleared
 * meanwhile; each pass owes a scene unless the main menu runs, the view's dialog is open, or the UI mode is FFh or a
 * town location's; the frame clock is marked anew.
 * Original: 28876 entry: 3066 and 96C0 saved and cleared; DI = the scene flag (8C77, 9885 clear, 9A1C neither FFh
 * nor 11h). */
xeen_status name_poll(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    g->panel_name_sign_shown = ds[XEEN_DS_SIGN_SHOWN];
    g->panel_name_monster_countdown = ds[XEEN_DS_MONSTER_COUNTDOWN];
    ds[XEEN_DS_MONSTER_COUNTDOWN] = 0;
    ds[XEEN_DS_SIGN_SHOWN] = 0;
    g->panel_name_scene = !ds[XEEN_DS_IN_MENU] && !ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW_DIALOG)] && ds[XEEN_DS_UI_MODE] != 0xffu &&
                          ds[XEEN_DS_UI_MODE] != XEEN_UI_MODE_LOCATION;
    g->panel_sampled = 0;
    return panel_set(g, PANEL_NAME_POLL, PANEL_NAME_POLL_BOUNDARY);
}
/* 2894F after a 28876 key (AL): 08h/F0h delete, 20h..7Fh append below 20
 * (no leading space), each word capitalised (11690 toupper, 11664
 * tolower), the 587F line redrawn; Esc empties; Enter or Esc end it with
 * F5FE = 0, 8C52 = 0, 18FFF; then 16332: 18765(6), 18FFF. */
static xeen_status name_done(xeen_game *g);
static xeen_status word_done(xeen_game *g);
/* Applies key CH to the LENGTH characters in BUF: Backspace (08h) or Left deletes the last one; a character 20h..7Fh
 * is appended below LIMIT, but a space not as the first. Returns 1 when the text changed. */
static int entry_edit(uint8_t *buf, unsigned *length, unsigned limit, uint8_t ch)
{
    if ((ch == XEEN_KEY_BACKSPACE || ch == XEEN_KEY_LEFT) && *length) {
        buf[--*length] = 0;
        return 1;
    }
    if (ch >= 0x20u && ch <= 0x7fu && *length < limit && !(!*length && ch == ' ')) {
        buf[(*length)++] = ch;
        buf[*length] = 0;
        return 1;
    }
    return 0;
}
/* Capitalises each word of the LENGTH characters in BUF: its first character upper case, the rest lower case (ASCII
 * letters only; words are separated by spaces). Original: 11690 toupper, 11664 tolower. */
static void entry_capitalise(uint8_t *buf, unsigned length)
{
    int start = 1;
    for (unsigned i = 0; i < length; ++i) {
        if (buf[i] == ' ') {
            start = 1;
        } else if (start) {
            if (buf[i] >= 'a' && buf[i] <= 'z') {
                buf[i] = (uint8_t)(buf[i] - 0x20);
            }
            start = 0;
        } else if (buf[i] >= 'A' && buf[i] <= 'Z') {
            buf[i] = (uint8_t)(buf[i] + 0x20);
        }
    }
}
/**
 * Handles a key of the name entry (the amount entry's go to amount_key). The exploration state the poll put aside
 * (the monsters' countdown, the sign) comes back first. An edit (entry_edit) capitalises the words and redraws the
 * line. Enter ends the entry with the text, Escape with none; either clears the key code and the entry flag, pops
 * the entry's button set and goes on to its owner (word_done, create_named or name_done). Any other key keeps
 * polling.
 */
static xeen_status name_key(xeen_game *g, uint8_t ch)
{
    if (g->name_owner == NAME_AMOUNT) {
        return amount_key(g, ch);
    }
    uint8_t *ds = g->movement.ds, *buf = entry_buffer(g);
    unsigned length = g->panel_name_length;
    ds[XEEN_DS_MONSTER_COUNTDOWN] = g->panel_name_monster_countdown;
    ds[XEEN_DS_SIGN_SHOWN] = g->panel_name_sign_shown;
    if (entry_edit(buf, &length, entry_limit(g), ch)) {
        entry_capitalise(buf, length);
        name_text(g, 0);
        (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCROLL);
        ++g->scene_returns;
    }
    g->panel_name_length = length;
    if (ch != XEEN_KEY_ESCAPE && ch != XEEN_KEY_ENTER) {
        return name_poll(g);
    }
    if (ch == XEEN_KEY_ESCAPE) {
        g->panel_name_length = length = 0;
    }
    buf[length] = 0;
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
    ds[XEEN_DS_TEXT_ENTRY] = 0;
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    return g->name_owner == NAME_WORD ? word_done(g) : g->name_owner ? create_named(g) : name_done(g);
}
/* Asks the party for a word, for the script's operation 21 (a password, or the mirror's destination when the record's
 * type is 0) or 55: the speech window closed first when open; the title (the record's text, "What's the Password?"
 * without one, none for operation 55) above an empty entry line in the scroll window; then the line entry with the
 * word as its owner (up to 30 characters; word_done answers it). Stops for another operation, a title that does
 * not fit, or a prompt that cannot be shown.
 * Original: 2E8E5(type, A43F, the title, 21) from 32855: with 9894 set 18765(28h); AC0F = DS:613E with the title
 * (record text p9, else DS:637A; 328AD zeroes operation 55's); 1871B(6), 4E38C(6, AC0F), 50BF6(6); then 2894F(the
 * word, 1Eh, C8h, 6): 8C52 = 1, 190ED, 1919B, the 5871 line, the 28876 polls. */
xeen_status word_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    const uint8_t *p = g->resident + (uint16_t)g->execution.state.offset;
    uint8_t title[1000];
    int k;
    if (p[5] != 21u && p[5] != 55u) {
        return panel_stop(g, WORD_STOP_OPERATION);
    }
    xeen_presentation_trace_extra("wordmark"); /* Evidence: 2E8E5's entry */
    if (p[5] == 55u) {
        title[0] = 0; /* 328AD, operation 55's zeroed title */
    } else if (p[9]) {
        if (xeen_event_text_copy(&g->resources.texts, p[9], title, sizeof title, 0, sizeof title) !=
                    XE_OK ||
            !memchr(title, 0, sizeof title)) {
            return panel_stop(g, WORD_STOP_TITLE);
        }
    } else {
        (void)snprintf((char *)title, sizeof title, "%s", ds_text(g, XEEN_TEXT_PASSWORD_PROMPT));
    }
    /* evidence (XEEN_DRAW_TRACE_EXTRA): 2E8E5's 18765(28h) and
     * 1871B(6), as the original's 'events' observer records them. */
    if (ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH)]) { /* an open window is closed and its pixels restored */
        xeen_presentation_trace_extra("evclose 40");
        xeen_presentation_trace_extra("evrestore 40");
        panel_window(g, XEEN_WINDOW_SPEECH, 0);
    }
    k = snprintf((char *)ds + XEEN_DS_TEXT_BUFFER_2, 1024, ds_text(g, XEEN_TEXT_ENTRY_TITLE), (const char *)title);
    if (k < 0 || k >= 1024) {
        return panel_stop(g, WORD_STOP_TEXT);
    }
    xeen_presentation_trace_extra("evopen 6");
    panel_window(g, XEEN_WINDOW_SCROLL, 1);
    ds[XEEN_DS_TEXT_ENTRY] = 1;
    g->panel_name_length = 0;
    g->word[0] = 0;
    g->name_owner = NAME_WORD;
    if (!entry_prompt_shown(g)) {
        return panel_stop(g, NAME_ENTRY_STOP_FRAME);
    }
    return name_poll(g);
}
/* The comparison's case fold: a..z to A..Z, any other byte as it is. Original: 14CDF's. */
static uint8_t fold_upper(uint8_t c)
{
    return c >= 'a' && c <= 'z' ? (uint8_t)(c - 0x20) : c;
}
/* How the word compared with a text (word_match). */
enum word_match {
    WORD_DIFFERS,
    WORD_MATCHES,
    WORD_PAST_KNOWN /* the comparison reached a byte past the known part */
};
/* Compares WORD with TEXT byte by byte, a..z folded to A..Z on a mismatch (14CDF): they match when both end
 * together. Only the first KNOWN bytes of TEXT may be read; reaching a further one (before the comparison is
 * decided) is WORD_PAST_KNOWN. */
static enum word_match word_match(const char *word, const uint8_t *text, size_t known)
{
    for (size_t at = 0;; ++at) {
        uint8_t x = (uint8_t)word[at], y;
        if (at >= known) {
            return WORD_PAST_KNOWN;
        }
        y = text[at];
        if (!x) {
            return y ? WORD_DIFFERS : WORD_MATCHES;
        }
        if (x != y && fold_upper(x) != fold_upper(y)) {
            return WORD_DIFFERS;
        }
    }
}
/* The mirror's word: the world's destination table (xeenmirr.txt or darkmirr.txt, both in DARK.CC, loaded into the
 * file block) is searched row by row, 32 bytes a row, from row 26h on Clouds above map 32 (else from 0): a row whose
 * name matches the word (case folded) answers the row + 1, with the transport sound, and ends the search. A
 * preserved quirk: the loop's test lets it also compare the row at its limit (3Eh on Clouds, 24h on Darkside),
 * past both files' ends: the block's older bytes (on Clouds the voice whereto.voc left there, whose "&" matches).
 * Returns 1 with *AX (0: no match), or 0 with *ST the stop: no memory for the table, a comparison past the block's
 * known bytes, the sound.
 * Original: 2E9AC (SI = 26h when DS 94 = 0 and ED99 > 20h); F5E8 = DS:614C with DS:9E / DS:9A; 191DE(1),
 * 18975(F5E8) into F42C (8582 the size), 191DE(94); 14CDF; DI, SI = 3Eh and 1880E(33h); the test 2EA53. */
static int word_mirror(xeen_game *g, uint16_t *ax, xeen_status *st)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint8_t world = ds[XEEN_DS_WORLD];
    /* the rows: Clouds 62 (3Eh; from row 26h above map 20h), Darkside 36 (24h): the files' 1984 and 1152 bytes */
    unsigned mirror_row = !world && ds[XEEN_DS_PARTY_MAP] > 32u ? 0x26u : 0u,
             limit = world ? 0x24u : 0x3eu, dx;
    char file[16];
    size_t n = 0;
    uint8_t *b;
    (void)snprintf(file, sizeof file, ds_text(g, XEEN_TEXT_MIRROR_FILE),
                   ds_text(g, world ? XEEN_TEXT_DARKSIDE_PREFIX : XEEN_TEXT_CLOUDS_PREFIX));
    ds_put_text(g, XEEN_DS_FILE_NAME, file);
    xeen_select_archive(m, 1);
    b = menu_load(g, file, &n);
    xeen_select_archive(m, world);
    if (!b) {
        *st = panel_stop(g, MIRROR_STOP_NO_MEMORY);
        return 0;
    }
    f42c_load(g, b, n);
    free(b);
    *ax = 0;
    do {
        /* 14CDF over the block: the word against the row's name; a byte outside the known part is a stop. */
        uint32_t at = MIRROR_ROW_BYTES * mirror_row;
        enum word_match match = at < g->f42c_known
                                        ? word_match(g->word, g->f42c + at, g->f42c_known - at)
                                        : WORD_PAST_KNOWN;
        if (match == WORD_PAST_KNOWN) {
            *st = panel_stop(g, MIRROR_STOP_TABLE);
            return 0;
        }
        if (match == WORD_MATCHES) {
            *ax = (uint16_t)(mirror_row + 1u);
            mirror_row = 0x3e;
            if (sound_effect(g, XEEN_CUE_TRANSPORTED) !=
                XEEN_OK) {
                *st = panel_stop(g, MIRROR_STOP_SOUND);
                return 0;
            }
        }
        dx = mirror_row++;
    } while (limit > dx);
    return 1;
}
/* The word was entered (Enter or Escape): an empty word answers 0; the mirror's (type 0) answers its row
 * (word_mirror); else 1 when it matches the first expected text (case folded), and when it matches the second, FFFFh
 * for operation 55 or 1, else 0. The scroll window closes and the script resumes with the answer.
 * Original: 2E95C..2EA67 (A827, then A43F; 2E99E: BP+10h 37h); 18765(6); the interpreter's phase 15. */
static xeen_status word_done(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    uint16_t ax = 0;
    const uint8_t *p = g->resident + (uint16_t)g->execution.state.offset;
    if (g->panel_name_length && !p[6]) {
        xeen_status st;
        if (!word_mirror(g, &ax, &st)) {
            return st;
        }
    } else if (g->panel_name_length) {
        if (word_match(g->word, ds + XEEN_DS_TEXT_BUFFER, SIZE_MAX) == WORD_MATCHES) {
            ax = 1;
        } else if (word_match(g->word, ds + XEEN_DS_TEXT_BUFFER_0, SIZE_MAX) == WORD_MATCHES) {
            ax = p[5] == 55u ? 0xffffu : 1u;
        }
    }
    xeen_presentation_trace_extra("evclose 6"); /* Evidence: 18765(6) and its 4F57B */
    if (ds[XEEN_DS_WINDOW_OPEN + 6u]) {
        xeen_presentation_trace_extra("evrestore 6");
    }
    panel_window(g, XEEN_WINDOW_SCROLL, 0);
    xeen_presentation_trace_extra("wordend"); /* Evidence: 2E8E5's RETF */
    g->name_owner = NAME_GAME;
    g->panel_stage = 0;
    g->execution.word_ax = ax;
    g->dispatcher.execution.word_ax = ax;
    g->phase = XG_STOPPED;
    drive(g);
    return XEEN_SERVICE_REQUIRED;
}
/* The game's name was entered in the save dialog: an empty name goes back to the dialog (the menu's or the panel's).
 * Otherwise the slot's Darkside file name goes into the file list's first entry, the slot becomes the game's, its
 * name and its "? 1" line are the dialog's, and the dialog is redrawn; then the main menu asks for the new game's
 * preference, while the panel takes the name as the game's and saves (panel_save). Stops when the dialog cannot be
 * drawn.
 * Original: 2B2E9: 8C96 = "dark%02d.wox", 95A6, the 5F45 line, the 2AEB2 redraw; 2B348: 96C5 = A827, 29502(0). */
static xeen_status name_done(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned slot = g->menu_selected;
    char out[16];
    panel_window(g, XEEN_WINDOW_SCROLL, 0);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    if (!g->panel_name_length) {
        if (ds[XEEN_DS_IN_MENU]) {
            g->panel_stage = 0;
            return menu_set(g, XEEN_MENU_DIALOG, MENU_DIALOG_BOUNDARY);
        } /* the menu's 2B117 */
        return panel_scene(g, PANEL_DIALOG_SCENE);
    }
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_DARK_SLOT_FORMAT), (int)slot);
    ds_put_text(g, XEEN_DS_FILE_LIST, out);
    ds[XEEN_DS_GAME_SLOT] = (uint8_t)slot;
    memset(g->save_slots[slot - 1u].header.name, 0, sizeof g->save_slots[slot - 1u].header.name);
    memcpy(g->save_slots[slot - 1u].header.name, ds + XEEN_DS_TEXT_BUFFER, g->panel_name_length + 1u);
    {
        const uint8_t *line = ds + XEEN_TEXT_DIALOG_SLOT_LINE;
        const uint8_t *end = memchr(line, 0, sizeof g->save_slots[0].line - 1u);
        size_t k = end ? (size_t)(end - line) : sizeof g->save_slots[0].line - 1u;
        memcpy(g->save_slots[slot - 1u].line, line, k);
        g->save_slots[slot - 1u].line[k] = 0;
    }
    xeen_window_open(m, XEEN_WINDOW_VIEW_DIALOG);
    if (!dialog_text(g)) {
        return panel_stop(g, DIALOG_STOP_TEXT);
    }
    dialog_draw(g, 0);
    if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW_DIALOG) != XEEN_OK) {
        return panel_stop(g, DIALOG_STOP_FRAME);
    }
    ++g->scene_returns;
    if (ds[XEEN_DS_IN_MENU]) {
        return newgame_preference(g);
    }
    memcpy(ds + XEEN_DS_GAME_NAME, ds + XEEN_DS_TEXT_BUFFER, g->panel_name_length + 1u);
    return panel_save(g, SAVE_BACK_TO_DIALOG);
}
/* A key ends the line entry's wait: a shown cursor is erased (the empty text redrawn in the entry's window and
 * presented), then the key is edited in (name_key).
 * Original: 28921..2893B: 4E38C(DS:5855) and 50BF6; then 2894F or 28ABA. */
static xeen_status input_key(xeen_game *g, uint8_t key)
{
    if (g->input_line[0] && g->text_cursor_shown) {
        menu_window_text_on(g, g->input_window, "");
        (void)xeen_presentation_update_window(g->presentation, g->input_window);
    }
    return name_key(g, key);
}
/*
 * The line entry's poll stage: the frame clock marked once a pass; a key goes to input_key; without one the line's
 * scene when one is owed, else the cursor's step and the tick wait. Original: 28876's loop, 288BF: 4EA0D, 189E3,
 * then 4EA1C.
 */
xeen_status name_entry_poll(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    uint16_t key;
    if (!g->panel_sampled) {
        status = frame_base(g, bios, user);
        if (status != XEEN_OK) {
            return status;
        }
        g->panel_sampled = 1;
    }
    key = panel_poll(g);
    if (key) {
        return input_key(g, (uint8_t)key);
    }
    if (g->panel_name_scene) {
        return panel_scene(g, PANEL_NAME_SCENE);
    }
    input_cursor_step(g);
    g->dialog_loop_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
    return panel_set(g, PANEL_NAME_TICK, PANEL_NAME_TICK_BOUNDARY);
}
/*
 * The line entry's tick stage: a key goes to input_key; without one the frame clock is read, and a tick or more
 * starts the next pass (PANEL_NAME_POLL). Original: 28876's loop, 2890C..2891F: 189E3, 4EA1C, then 288C4.
 */
xeen_status name_entry_tick(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    uint16_t key;
    key = panel_poll(g);
    if (key) {
        return input_key(g, (uint8_t)key);
    }
    if ((status = timer_ticks(g, &g->dialog_loop_elapsed, bios, user)) != XEEN_OK) {
        return status;
    }
    g->dialog_loop_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
    if (!g->timer_registers.ax) {
        return XEEN_SERVICE_REQUIRED;
    }
    g->panel_sampled = 0;
    g->panel_stage = PANEL_NAME_POLL;
    g->lifecycle_boundary = PANEL_NAME_POLL_BOUNDARY;
    return XEEN_SERVICE_REQUIRED;
}
