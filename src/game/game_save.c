/*
 * The save (Phase 246, from game_panel.c): the control panel's Save (29502) and 19278: the worlds' CUR
 * images (the live state archive and the 1978C copy), the DOS clock and the debug time line, the slot
 * files, the save buffers, Darkside's state, the window name, the roster and the state written and
 * committed, the events reloaded, and panel_save, the control panel's entry.
 */
#include "game_menu.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where a save stops when a step cannot be performed; each value is the original address of that step
 * (tools/xeen_replay/stops.json). The save's stages run in the original's order and the first failure ends it.
 */
enum {
    SAVE_STOP_NO_MEMORY = 0x19278,        /* no memory for the Darkside state archive's copy */
    SAVE_STOP_NO_CUR_FILES = 0x19278,     /* a start without the two worlds' state archives (a legacy start) */
    SAVE_STOP_CLOCK = 0x19c3d,            /* the host offers no DOS clock to add the play time */
    SAVE_STOP_CLOCK_RESTART = 0x19c29,    /* the host offers no DOS clock to restart the play time */
    SAVE_STOP_DARK_MEMBERS = 0x50776,     /* maze.nam, maze.chr and maze.pty into the Darkside state archive */
    SAVE_STOP_SLOT_WRITE = 0x19629,       /* a state archive into its slot file (or no file services) */
    SAVE_STOP_PAIR_COPY = 0x197f5,        /* a new game's copy of both worlds' state */
    SAVE_STOP_READ_DARK = 0x19987,        /* the party files from the Darkside state archive */
    SAVE_STOP_NAME = 0x295b5,             /* the save's name has no end within the text buffer */
    SAVE_STOP_ROSTER_INDEX = 0x29622,     /* a member's roster index past the 30 records */
    SAVE_STOP_MAP_STATE = 0x30f34,        /* the map's live state into the active world's archive */
    SAVE_STOP_EVENTS_LOAD = 0x2c8bb,      /* the map's event script and texts from the saved archive */
    SAVE_STOP_EVENTS_PREPARE = 0x2c903,   /* the reloaded event script prepared */
    SAVE_STOP_REFUSAL_SCROLL = 0x29525,   /* "no saving in this maze" could not be shown */
    SAVE_STOP_SAVING_WINDOW = 0x29564,    /* "Saving your game" could not be drawn */
    SAVE_STOP_SAVED_WINDOW = 0x296ae      /* "Your Game ... has been saved" could not be drawn */
};
/* --------------------------------------------- 29502 Save, 19278 */
/* The CUR image of a world: the active world's is the live state archive
 * (map transitions persist into it), the other world's the 1978C copy. */
/* On Darkside the saved DARK.CUR is the session's state. The original has one DARK.CUR, written in place, so the
 * archive-1 copy that 19953 (menu_party: Sign In's 16159 restart, 43DA5) and the session handoff read must be the
 * saved one too: otherwise a Sign In at Castleview would restart into exploration on the old map (ED99) instead of
 * at the inn. */
uint32_t cur_owned_sync(xeen_game *g)
{
    uint8_t *c;
    if (g->world != 1u || !g->state.bytes || !g->state.size) {
        return 0;
    }
    if (!(c = malloc(g->state.size))) {
        return SAVE_STOP_NO_MEMORY;
    }
    memcpy(c, g->state.bytes, g->state.size);
    cur_replace(g, 1, c, g->state.size);
    return 0;
}
/** Returns a copy (owned by the caller) of WORLD's .CUR state archive and its size in *N. */
uint8_t *cur_copy(const xeen_game *g, unsigned world, size_t *n)
{
    const uint8_t *src = world == g->world ? g->state.bytes : g->cur[world].bytes;
    size_t size = world == g->world ? g->state.size : g->cur[world].size;
    uint8_t *b;
    if (!src || !size || !(b = malloc(size))) {
        return NULL;
    }
    memcpy(b, src, size);
    *n = size;
    return b;
}
/* Writes the state file CUR (N bytes) of the selected world as the current slot's file of that world through the
 * host's save files (the file-name buffer names the CUR, then the slot file, as the original's copy does). Returns
 * the host's answer (0: refused). Original: 195C5 (F5E8 = "%s.cur", then "%s%02d.wox" of 95A6). */
int cur_to_slot(xeen_game *g, const uint8_t *cur, size_t n)
{
    xeen_movement *m = &g->movement;
    char out[32];
    const char *prefix = ds_text(g, m->ds[XEEN_DS_WORLD] ? XEEN_TEXT_DARKSIDE_PREFIX : XEEN_TEXT_CLOUDS_PREFIX);
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_CUR_FILE_FORMAT), prefix);
    ds_put_text(g, XEEN_DS_FILE_NAME, out);
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_SLOT_FILE_FORMAT), prefix, (int)(int8_t)m->ds[XEEN_DS_GAME_SLOT]);
    ds_put_text(g, XEEN_DS_FILE_NAME, out);
    dos_free_memory(m);
    return g->save_files->write(g->save_files->user, out, cur, n);
}
/** Reads the host's DOS clock as the original's packed time into *NOW; returns 0 without one. */
static int dos_clock(xeen_game *g, uint32_t *now)
{
    xeen_dos_clock c;
    if (!g->save_files->clock || !g->save_files->clock(g->save_files->user, &c)) {
        return 0;
    }
    *now = xeen_dos_time(&g->movement, &c);
    return 1;
}
/* The C runtime's ctime text in the text buffer A827 ("Www Mmm dd hh:mm:ss yyyy"): the hour at +11 (A832), the
 * colon after it at +13 (A834), and +19 (A83A), after the seconds, where the time's 'a' or 'p' goes. */
#define CTIME_HOUR          11u
#define CTIME_HOUR_COLON    13u
#define CTIME_AFTER_SECONDS 19u
/* The control panel's debug time line (turned on by typing G, O, B, R): the play time as hours:minutes:seconds,
 * then the host's wall clock as the 12-hour time with "am" or "pm" (a leading 0 shown as a space), into the panel's
 * text buffer, which the panel's frames redraw. A preserved quirk: the hours' product is 16 bits, so from 65,536
 * seconds the minutes absorb the lost 65,536 (a 20-hour game shows 20:1092:16). The wall clock is the host's
 * (deterministic in replays). Returns 0, or the stop when the clock cannot be read.
 * Original: 19C65 (97FA set at 29C10, read only at 2991E); time(&t) 1160C; 19C3D: time(NULL) - 98B0 + F01A; 19BC6
 * "%02u:%02u:%02u" into A43F (unsigned 10752/10762; the IMUL at 19BDD); 19B0E: 4E06B(F460, 0), ctime(&t) into A827,
 * A834 = 0, 130C5 atoi(A832), 'a'/'p' at A83A, A834 = ':'; sprintf(DS:4BE3, A832, A43F), strcpy to A827. */
uint32_t debug_time_line(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint32_t t, now, e, hours;
    char text[160];
    int n, hour;
    if (!dos_clock(g, &t) || !dos_clock(g, &now)) {
        return SAVE_STOP_CLOCK; /* the time line's clock read (19C6E, 1160C) */
    }
    e = now - xeen_ds_read32(m, XEEN_DS_PLAY_CLOCK_START) + xeen_ds_read32(m, XEEN_DS_PLAY_SECONDS);
    hours = e / 3600u;
    e -= (uint16_t)((uint16_t)hours * 0xe10u); /* 19BDD IMUL (16 bits), SUB/SBB 0 */
    (void)snprintf(text, sizeof text, ds_text(g, XEEN_TEXT_SAVE_PLAY_TIME), (unsigned)(uint16_t)hours,
                   (unsigned)(uint16_t)(e / 60u), (unsigned)(uint16_t)(e % 60u));
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_0, text);
    if (g->presentation) {
        (void)xeen_presentation_select_cursor(g->presentation, xeen_read16(m, XEEN_DS_MOUSE_SPRITES), 0);
    }
    {
        const char *c = ds_text(g, xeen_dos_ctime(m, t));
        memmove(ds + XEEN_DS_TEXT_BUFFER, c, strlen(c) + 1u);
    } /* 13472, 19B37 "%s" */
    ds[XEEN_DS_TEXT_BUFFER + CTIME_HOUR_COLON] = 0;
    hour = atoi((const char *)ds + XEEN_DS_TEXT_BUFFER + CTIME_HOUR);
    if (hour < 12) {
        ds_put_text(g, XEEN_DS_TEXT_BUFFER + CTIME_AFTER_SECONDS, ds_text(g, XEEN_TEXT_SAVE_TIME_AM));
        if (!hour) {
            ds_put_text(g, XEEN_DS_TEXT_BUFFER + CTIME_HOUR, ds_text(g, XEEN_TEXT_SAVE_TIME_TWELVE));
        }
    } else {
        hour -= 12;
        if (!hour) {
            ds_put_text(g, XEEN_DS_TEXT_BUFFER + CTIME_HOUR, ds_text(g, XEEN_TEXT_SAVE_TIME_TWELVE));
        } else {
            char h[16];
            (void)snprintf(h, sizeof h, ds_text(g, XEEN_TEXT_SAVE_TIME_HOUR), hour);
            ds_put_text(g, XEEN_DS_TEXT_BUFFER + CTIME_HOUR, h);
        }
        ds_put_text(g, XEEN_DS_TEXT_BUFFER + CTIME_AFTER_SECONDS, ds_text(g, XEEN_TEXT_SAVE_TIME_PM));
    }
    ds[XEEN_DS_TEXT_BUFFER + CTIME_HOUR_COLON] = ':';
    if (ds[XEEN_DS_TEXT_BUFFER + CTIME_HOUR] == '0') {
        ds[XEEN_DS_TEXT_BUFFER + CTIME_HOUR] = ' ';
    }
    n = snprintf(text, sizeof text, ds_text(g, XEEN_TEXT_SAVE_TIME_LINE), ds_text(g, XEEN_DS_TEXT_BUFFER + CTIME_HOUR),
                 ds_text(g, XEEN_DS_TEXT_BUFFER_0));                       /* 19CA3: < 80 bytes */
    memcpy(ds + XEEN_DS_TEXT_BUFFER, text, n > 0 ? (size_t)n + 1u : 1u); /* 19CB4 strcpy */
    return 0;
}
/* Writes the current slot's files: the map side set to the world, the play time brought up to date from the DOS
 * clock, Darkside's state file given the name record, the roster and the party file and copied to its slot file;
 * with Clouds installed, a new game's save (the new-game flag) checks and repairs the slot pair and copies both
 * worlds' state files, any other copies the Clouds state file (the active one in Clouds); then the map side's archive
 * selected, the play clock restarted and the add-ons' slot file written. Returns 0, or the stop of the failed step.
 * Original: 2956A..296B4 after the window text (19278; 192D1). */
uint32_t save_slot_files(xeen_game *g, uint8_t *active, size_t an, uint8_t *dark, size_t dn,
                                const uint8_t *roster)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint32_t now;
    int addon_slot = (int)(int8_t)ds[XEEN_DS_GAME_SLOT];
    /* 19278 */
    ds[XEEN_DS_MAP_SIDE] = ds[XEEN_DS_WORLD];
    xeen_select_archive(m, 1);
    if (!dos_clock(g, &now)) {
        return SAVE_STOP_CLOCK;
    }
    xeen_ds_write32(m, XEEN_DS_PLAY_SECONDS,
                    now - xeen_ds_read32(m, XEEN_DS_PLAY_CLOCK_START) + xeen_ds_read32(m, XEEN_DS_PLAY_SECONDS));
    if (!xeen_save_member_replace(dark, dn, ds_text(g, XEEN_TEXT_STATE_NAME_FILE), ds + XEEN_DS_GAME_NAME,
                                  XEEN_GAME_NAME_BYTES) ||
        !xeen_save_member_replace(dark, dn, ds_text(g, XEEN_TEXT_STATE_ROSTER_FILE), roster, XEEN_ROSTER_BYTES) ||
        !xeen_save_member_replace(dark, dn, ds_text(g, XEEN_TEXT_STATE_PARTY_FILE),
                                  ds + XEEN_DS_PARTY_FILE, XEEN_DARK_PARTY_BYTES)) {
        return SAVE_STOP_DARK_MEMBERS;
    }
    if (!cur_to_slot(g, dark, dn)) {
        return SAVE_STOP_SLOT_WRITE;
    }
    if (ds[XEEN_DS_CLOUDS_INSTALLED]) {
        if (xeen_read16(m, XEEN_DS_NEW_GAME_SAVE)) {
            /* A new game's save (dark is then cur[1].bytes itself): the pair check and its repair (which writes the
             * Clouds slot file), then both worlds' copies.
             * Original: 192D8: 18363 (its repair 18441), then 1978C for both worlds. */
            uint32_t stop = menu_pair_check(g);
            if (stop) {
                return stop;
            }
            if (!menu_copy(g)) {
                return SAVE_STOP_PAIR_COPY;
            }
            xeen_select_archive(m, !ds[XEEN_DS_WORLD]);
            if (!menu_copy(g)) {
                return SAVE_STOP_PAIR_COPY;
            }
        } else {
            xeen_select_archive(m, 0);
            if (!cur_to_slot(g, g->world == 0u ? active : g->cur[0].bytes,
                             g->world == 0u ? an : g->cur[0].size)) {
                return SAVE_STOP_SLOT_WRITE;
            }
        }
    }
    xeen_select_archive(m, ds[XEEN_DS_MAP_SIDE]);
    if (!dos_clock(g, &now)) {
        return SAVE_STOP_CLOCK_RESTART;
    }
    xeen_ds_write32(m, XEEN_DS_PLAY_CLOCK_START, now);
    addons_slot_save(g, addon_slot); /* add-on identify */
    return 0;
}
/* The save's working copies: the Darkside CUR (DARK), the active world's CUR (ACTIVE; the same buffer on
 * Darkside) and the 30-record roster, owned here until save_commit_state hands the CURs to the game. */
typedef struct save_buffers {
    uint8_t *active, *dark, *roster;
    size_t an, dn;
} save_buffers;
/* Frees what B still owns (the shared Darkside buffer once). */
static void save_buffers_free(save_buffers *b)
{
    if (b->dark != b->active) {
        free(b->dark);
    }
    free(b->active);
    free(b->roster);
}
/* Reads the save's Darkside state: a copy of DARK.CUR (Darkside's archive selected, the roster buffer bound), a
 * roster buffer, and its name record, roster and party file (into DS, the Darkside size exactly); then the loaded
 * file's size, and the archive the map side names as the read left it (the name record covers the side byte, as in
 * the new game's read) before the caller's world byte and archive come back. Returns 0, or the party-file stop.
 * Original: 19953 from DARK.CUR (B41C/B41A from F42C or 50D4:1C7E; 96C5, 31 bytes; ED8C, 5FAh bytes); 8582, 96E2;
 * 19987. */
static uint32_t save_read_dark_state(xeen_game *g, save_buffers *b)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint8_t saved_world = ds[XEEN_DS_MAP_SIDE];
    size_t nn = 0, rn = 0, pn = 0;
    xeen_archive cur;
    xeen_select_archive(m, 1);
    roster_buffer(m);
    if (!(b->dark = cur_copy(g, 1, &b->dn)) || !(b->roster = malloc(XEEN_ROSTER_BYTES)) ||
        xeen_archive_open(&cur, b->dark, b->dn) != XR_OK ||
        !xeen_archive_extract_state_member(&cur, ds_text(g, XEEN_TEXT_STATE_NAME_FILE),
                                           ds + XEEN_DS_GAME_NAME, XEEN_GAME_NAME_BYTES, &nn) ||
        nn != XEEN_GAME_NAME_BYTES ||
        !xeen_archive_extract_state_member(&cur, ds_text(g, XEEN_TEXT_STATE_ROSTER_FILE),
                                           b->roster, XEEN_ROSTER_BYTES, &rn) ||
        rn != XEEN_ROSTER_BYTES ||
        !xeen_archive_extract_state_member(&cur, ds_text(g, XEEN_TEXT_STATE_PARTY_FILE), ds + XEEN_DS_PARTY_FILE,
                                           XEEN_DARK_PARTY_BYTES, &pn) ||
        pn != XEEN_DARK_PARTY_BYTES) {
        return SAVE_STOP_READ_DARK;
    }
    xeen_write16(m, XEEN_DS_LOADED_FILE_SIZE, (uint16_t)pn);
    xeen_select_archive(m, ds[XEEN_DS_MAP_SIDE]);
    ds[XEEN_DS_MAP_SIDE] = saved_world;
    xeen_select_archive(m, saved_world);
    return 0;
}
/* With the save dialog's window open (XEEN_WINDOW_VIEW_DIALOG, 2AD6D's 19h), the name typed into the text buffer (its
 * NUL within the buffer) becomes the game's name. Returns 0, or the stop when the name has no end. Original: DS A827
 * into 96C5; 295B5. */
static uint32_t save_window_name(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    const uint8_t *end;
    if (!ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW_DIALOG)]) {
        return 0;
    }
    if (!(end = memchr(ds + XEEN_DS_TEXT_BUFFER, 0, XEEN_TEXT_BUFFER_BYTES))) {
        return SAVE_STOP_NAME;
    }
    memcpy(ds + XEEN_DS_GAME_NAME, ds + XEEN_DS_TEXT_BUFFER, (size_t)(end - (ds + XEEN_DS_TEXT_BUFFER)) + 1u);
    return 0;
}
/* The party block back from 9C6F, then each of its ED8D members' records into ROSTER at its roster
 * index (ED8E+i); a member past the live party count is gone: its B572 word and B430 byte take the
 * world (DS 94 + 1Ch, DS 94) and its index becomes FFh. ED8D becomes the party count. 0, or 29622
 * for an index past the 30 roster records. */
static uint32_t save_roster(xeen_game *g, uint8_t *roster)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned i;
    memcpy(ds + XEEN_DS_PARTY_FILE, ds + XEEN_DS_SCRATCH, XEEN_DARK_PARTY_BYTES);
    for (i = 0; i < ds[XEEN_DS_SAVED_PARTY_COUNT]; ++i) {
        unsigned who = ds[XEEN_DS_PARTY_MEMBERS + i];
        int gone = !(ds[XEEN_DS_PARTY_COUNT] > i);
        if (gone) {
            xeen_write16(m, (uint16_t)(0xb572u + XEEN_CHARACTER_BYTES * i),
                         (uint16_t)(ds[XEEN_DS_WORLD] + 0x1cu));
            ds[0xb430u + XEEN_CHARACTER_BYTES * i] = ds[XEEN_DS_WORLD];
        }
        if (who >= 30u) {
            return SAVE_STOP_ROSTER_INDEX;
        }
        memcpy(roster + XEEN_CHARACTER_BYTES * who,
               ds + XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * i, XEEN_CHARACTER_BYTES);
        if (gone) {
            ds[XEEN_DS_PARTY_MEMBERS + i] = XEEN_NO_MEMBER;
        }
    }
    ds[XEEN_DS_SAVED_PARTY_COUNT] = ds[XEEN_DS_PARTY_COUNT];
    return 0;
}
/* Saves the current map's state into the active world's state file (Darkside's copy itself on Darkside, else a copy
 * of the live Clouds one), then writes the slot's files (save_slot_files). Returns 0, or the stop of the failing
 * step. Original: 30F34, then 2956A.. */
static uint32_t save_write_state(xeen_game *g, save_buffers *b)
{
    xeen_movement *m = &g->movement;
    char error[160];
    if (g->world == 1u) {
        b->active = b->dark;
        b->an = b->dn;
    } else if (!(b->active = cur_copy(g, g->world, &b->an))) {
        return SAVE_STOP_MAP_STATE;
    }
    if (!xeen_save_map_live(b->active, b->an, m->ds, sizeof m->ds, g->backing,
                            g->resources.record_bytes, error, sizeof error)) {
        return SAVE_STOP_MAP_STATE;
    }
    return save_slot_files(g, b->active, b->an, b->dark, b->dn, b->roster);
}
/* The CURs now hold the save (50776 wrote them in place): the active one becomes the game's state
 * archive and the Darkside one its archive-1 copy (on Darkside, cur_owned_sync copies the state).
 * B keeps only the roster afterwards. 0, or 30F34 / cur_owned_sync's boundary. */
static uint32_t save_commit_state(xeen_game *g, save_buffers *b)
{
    xeen_archive cur;
    uint32_t stop = 0;
    if (xeen_archive_open(&cur, b->active, b->an) != XR_OK) {
        return SAVE_STOP_MAP_STATE;
    }
    state_adopt(g, b->active, b->an, cur);
    if (b->dark != b->active) {
        cur_replace(g, 1, b->dark, b->dn);
    } else {
        stop = cur_owned_sync(g);
    }
    b->active = b->dark = NULL;
    return stop;
}
/* On a map (not the inn), the map's event scripts and texts reloaded from the state file and prepared (the file-name
 * buffer names the script's and then the texts' file, x for a map above 99, compared signed); the event service
 * bound as the resources' fallback. Returns 0, or the stop of the load or of the preparation.
 * Original: 2C88D(ED99) (F5E8; 8582 the texts' size; 2C8BB / 2C903). */
static uint32_t save_reload_events(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char name[32];
    int signed_map;
    if (!ds[XEEN_DS_PARTY_MAP]) {
        return 0;
    }
    signed_map = (int)(int16_t)ds[XEEN_DS_PARTY_MAP];
    (void)snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_RELOAD_EVENTS_FILE), signed_map > 99 ? 'x' : '0',
                   (unsigned)ds[XEEN_DS_PARTY_MAP]);
    ds_put_text(g, XEEN_DS_FILE_NAME, name);
    if (xeen_event_resources_load(&g->resources, &g->state, &g->assets, ds[XEEN_DS_PARTY_MAP],
                                  g->backing, sizeof g->backing, g->resident, sizeof g->resident,
                                  g->texts, sizeof g->texts, &g->index) != XR_OK) {
        return SAVE_STOP_EVENTS_LOAD;
    }
    g->resources.fallback = event_service;
    g->resources.fallback_user = g;
    (void)snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_RELOAD_MAP_TEXTS_FILE), signed_map > 99 ? 'x' : '0',
                   (unsigned)ds[XEEN_DS_PARTY_MAP]);
    ds_put_text(g, XEEN_DS_FILE_NAME, name);
    if (xeen_event_resources_prepare(&g->resources, ds, sizeof m->ds) != XE_OK) {
        return SAVE_STOP_EVENTS_PREPARE;
    }
    xeen_write16(m, XEEN_DS_LOADED_FILE_SIZE, (uint16_t)g->resources.texts.size);
    return 0;
}
/**
 * Writes the current game to its save slot (29502): the state archives of both worlds with the
 * party and map state; returns 0, or the boundary of a failing step. The stages run in the
 * original's order and the first failure ends the save (what it changed stays changed).
 */
uint32_t save_game(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    save_buffers b = {NULL, NULL, NULL, 0, 0};
    uint32_t stop;
    if (!g->cur[0].bytes || !g->cur[1].bytes) {
        return SAVE_STOP_NO_CUR_FILES;
    }
    if (!g->save_files->write || !g->save_files->clock) {
        return SAVE_STOP_SLOT_WRITE;
    }
    dos_free_memory(m); /* 2956A: 18886 (B414 only) */
    memcpy(m->ds + XEEN_DS_SCRATCH, m->ds + XEEN_DS_PARTY_FILE, XEEN_DARK_PARTY_BYTES);
    if (!(stop = save_read_dark_state(g, &b)) && !(stop = save_window_name(g)) &&
        !(stop = save_roster(g, b.roster)) && !(stop = save_write_state(g, &b)) &&
        !(stop = save_commit_state(g, &b))) {
        stop = save_reload_events(g);
    }
    save_buffers_free(&b);
    return stop;
}
/* Saves the game from the control panel (WHERE, enum save_return: its S key, or the save dialog after the name). A map that forbids
 * saving closes the panel's window and shows "The Gods of Game Restoration deem this area off limits" instead, with
 * nothing written; its close goes back to WHERE as after a save. Otherwise "Saving your game" in the message box, the
 * slot's files written (save_game), and "Your Game '%s' has been saved." until a key (no scene). Stops when a window
 * cannot be presented, the refusal cannot open, or the save stops.
 * Original: 29502(0): C0D6 bit 15 (29517): 18765(17h), 18F2F(DS:59C0, 1), 1871B(17h), 296DA; the S key back to
 * 2987D, the dialog from 2B348 back to 2B368 -> 2B268; window 15h; 189E3. */
xeen_status panel_save(xeen_game *g, enum save_return where)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char out[160];
    uint32_t stop;
    if (xeen_read16(m, XEEN_DS_MAP_RESTRICTIONS) & XEEN_MAP_NO_SAVE) {
        char text[0x98];
        (void)snprintf(text, sizeof text, ds_text(g, XEEN_DS_MESSAGE_FORMAT), ds_text(g, XEEN_TEXT_SAVE_REFUSED));
        xeen_presentation_trace_extra("evmark save-refused"); /* Evidence: 29517 */
        xeen_presentation_trace_extra("evclose 23");
        if (ds[XEEN_DS_WINDOW_OPEN + 0x17u]) {
            xeen_presentation_trace_extra("evrestore 23");
        }
        panel_window(g, XEEN_WINDOW_CONTROL_PANEL, 0);
        g->panel_stage = 0;
        xeen_presentation_trace_extra("evopen 6");
        trace_event_text(6, (const uint8_t *)text, strlen(text));
        if (!open_scroll_message(g, SCROLL_NO_SAVE, text)) {
            panel_window(g, XEEN_WINDOW_CONTROL_PANEL, 1);
            return panel_stop(g, SAVE_STOP_REFUSAL_SCROLL);
        }
        ++g->save_refusals;
        g->panel_return = where;
        return XEEN_SERVICE_REQUIRED;
    }
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_SAVING), ds_text(g, XEEN_DS_GAME_NAME));
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_0, out);
    panel_window(g, XEEN_WINDOW_MESSAGE_BOX, 1);
    menu_window_text(g, XEEN_WINDOW_MESSAGE_BOX, WINDOW_FRAMED, ds_text(g, XEEN_DS_TEXT_BUFFER_0));
    if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_MESSAGE_BOX) != XEEN_OK) {
        return panel_stop(g, SAVE_STOP_SAVING_WINDOW);
    }
    if ((stop = save_game(g)) != 0) {
        return panel_stop(g, stop);
    }
    ++g->saves;
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_SAVED), ds_text(g, XEEN_DS_GAME_NAME));
    ds_put_text(g, XEEN_DS_TEXT_BUFFER, out);
    menu_window_text(g, XEEN_WINDOW_MESSAGE_BOX, WINDOW_FRAMED, ds_text(g, XEEN_DS_TEXT_BUFFER));
    if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_MESSAGE_BOX) != XEEN_OK) {
        return panel_stop(g, SAVE_STOP_SAVED_WINDOW);
    }
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    ++g->scene_returns;
    g->panel_return = where;
    return panel_set(g, PANEL_SAVED, PANEL_SAVED_BOUNDARY);
}
