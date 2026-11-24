/*
 * The save pair files (Phase 246, from game_menu.c): the two worlds' slot files of each save. 19314 creates
 * the CURs again; 18363 scans both worlds' ids and 18441 repairs a slot whose pair is missing (18037 builds
 * the Darkside half of a Clouds file, 18200 writes both ids); 184DE checks the pairs at the menu, then
 * continues or asks (Delete / Exit to DOS) and takes the answer.
 */
#include "game_menu.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when the save pairs (each slot's Clouds and Darkside files) cannot be checked or
 * repaired; each value is the original address of the step (tools/xeen_replay/stops.json).
 */
enum {
    PAIR_STOP_SLOT_IDS = 0x199b7,            /* the slots' ids could not be read */
    PAIR_STOP_NO_RANDOM = 0x18441,           /* no random source for the repair */
    PAIR_STOP_CREATE = 0x19379,              /* a fresh state archive could not be made */
    PAIR_STOP_SLOT_WRITE = 0x19629,          /* a state archive into its slot file */
    PAIR_STOP_UNREPAIRED = 0x184de,          /* a pair is left that this check does not repair */
    PAIR_STOP_CONTINUE_IDS = 0x18514,
    PAIR_STOP_CONFIRM = 0x18542,             /* the repair's confirmation could not be shown */
    /* Writing both ids into a repaired pair (pair_ids) */
    PAIR_IDS_STOP_READ_CLOUDS = 0x1823c,     /* the Clouds slot file could not be read */
    PAIR_IDS_STOP_READ_DARK = 0x18297,       /* the Darkside slot file could not be read */
    PAIR_IDS_STOP_SHORT = 0x182ce,           /* a slot file ends before its game name */
    PAIR_IDS_STOP_WRITE = 0x18306,           /* a slot file could not be written back */
    /* Carrying a Clouds save's roster and party into its Darkside file (pair_carry_clouds) */
    PAIR_CARRY_STOP_READ_CLOUDS = 0x18097,   /* the Clouds slot file could not be read */
    PAIR_CARRY_STOP_READ_DARK = 0x18189,     /* the Darkside slot file could not be read */
    PAIR_CARRY_STOP_SHORT_CLOUDS = 0x180c3,  /* the Clouds file is shorter than name, roster and party */
    PAIR_CARRY_STOP_SHORT_DARK = 0x181b5,    /* the Darkside file is shorter than name, roster and party */
    PAIR_CARRY_STOP_ROSTER = 0x180da,        /* no buffer for the roster */
    PAIR_CARRY_STOP_WRITE_CLOUDS = 0x18152,  /* the Clouds file could not be written back */
    PAIR_CARRY_STOP_WRITE_DARK = 0x181e6     /* the Darkside file could not be written */
};
/* Creates the selected world's initial state file from its CC, kept in memory (the host's files are never written):
 * the CC's first members form it (five for Clouds, six for Darkside): their sizes from the CC's decoded directory
 * (xeen_directory_decode: each byte rotated left twice plus a key that starts at ACh and grows by 67h) summed, and that many bytes after
 * the directory copied XOR 35h. The file-name buffer ends with "%s.cur" and the world's archive is selected. The
 * original's memory metrics and copy buffer are DOS memory. Returns 0 when the CC is too short.
 * Original: 19314 for the world of DS 94 (193AE's decoding; entries 0..94+4, +5; count*8+2); F5E8 = "%s.cur";
 * 191DE(94); 18886's B410..B415 and the buffer 50581 or 50D4:1C7E. */
int cur_create(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    unsigned w = m->ds[XEEN_DS_WORLD] ? 1u : 0u, i;
    uint8_t head[0x3c], *cur; /* the directory's first 60 bytes, as 193AE decodes: entries 0..6 cover w + 5 */
    const xeen_archive *a = selected_cc(g);
    uint32_t total = 0;
    size_t start, k;
    char out[16];
    if (!a || a->size < 2u + sizeof head) {
        return 0;
    }
    for (i = 0; i < sizeof head; ++i) {
        head[i] = xeen_directory_decode(a->bytes[2u + i], i);
    }
    for (i = 0; i < w + 5u; ++i) {
        total += xeen_get_le16(head + 8u * i + 5u); /* entry i's size word (+5) */
    }
    start = (size_t)xeen_get_le16(a->bytes) * 8u + 2u; /* past the directory: a count word, 8-byte entries */
    if (start > a->size || total > a->size - start || !(cur = malloc(total ? total : 1u))) {
        return 0;
    }
    dos_free_memory(m);
    for (k = 0; k < total; ++k) {
        cur[k] = (uint8_t)(a->bytes[start + k] ^ 0x35u);
    }
    cur_replace(g, w, cur, total);
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_CUR_FILE_FORMAT), ds_text(g, w ? XEEN_TEXT_DARKSIDE_PREFIX : XEEN_TEXT_CLOUDS_PREFIX));
    ds_put_text(g, XEEN_DS_FILE_NAME, out);
    xeen_select_archive(m, m->ds[XEEN_DS_WORLD]);
    return 1;
}
/*
 * Reads world side W's file of the current save slot (DS 95A6) into *F, its size into *N and its name into NAME
 * (16 bytes); *AT becomes where the save's game name starts: past the count word and its count x 8 directory bytes.
 * Returns 0 when the file is missing or too short for the count (*F may then hold it, for the caller to free).
 */
static int slot_file_read(xeen_game *g, unsigned w, char name[16], uint8_t **f, size_t *n, size_t *at)
{
    xeen_movement *m = &g->movement;
    menu_name(g, XEEN_TEXT_SAVE_FILE_FORMAT, ds_text(g, w ? XEEN_TEXT_DARKSIDE_PREFIX : XEEN_TEXT_CLOUDS_PREFIX), (unsigned)(int)(int8_t)m->ds[XEEN_DS_GAME_SLOT]);
    memcpy(name, m->ds + XEEN_DS_FILE_NAME, 16);
    name[15] = 0;
    if (!(*f = g->save_files->read(g->save_files->user, name, n)) || *n < 2u) {
        return 0;
    }
    *at = (size_t)((*f)[0] | (*f)[1] << 8) * 8u + 2u;
    return 1;
}
/* Marks both slot files of the current slot as one pair: the Darkside file's name record, with its side byte set to
 * the current world (during a new game's save: the DS name record's byte 20, as the original reads it) and its
 * pair-id byte to the pair's id, is written over the name record of both files (Clouds first, then Darkside; the
 * file-name buffer keeps the Darkside name). Returns 0, or the stop of a file that cannot be read, is too short or
 * cannot be written.
 * Original: 18200 (byte 1Dh = FA90 ? 96D9 : 94, byte 1Eh = F61C; the files "%s%02u.wox" of 95A6, F5E8). */
static uint32_t pair_ids(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *f[2] = {NULL, NULL}, nam[XEEN_GAME_NAME_BYTES];
    size_t n[2] = {0, 0}, at[2] = {0, 0};
    char names[2][16];
    unsigned w;
    uint32_t stop = 0;
    for (w = 0; w < 2u; ++w) {
        if (!slot_file_read(g, w, names[w], &f[w], &n[w], &at[w])) {
            stop = w ? PAIR_IDS_STOP_READ_DARK : PAIR_IDS_STOP_READ_CLOUDS;
            goto done;
        }
        if (at[w] + sizeof nam > n[w]) {
            stop = PAIR_IDS_STOP_SHORT;
            goto done;
        }
    }
    memcpy(nam, f[1] + at[1], sizeof nam);
    nam[XEEN_GAME_NAME_SIDE] = xeen_read16(m, XEEN_DS_NEW_GAME_SAVE) ? m->ds[XEEN_DS_GAME_NAME + 20u]
                                                                     : m->ds[XEEN_DS_WORLD];
    nam[XEEN_GAME_NAME_PAIR_ID] = m->ds[XEEN_DS_PAIR_ID];
    for (w = 0; w < 2u; ++w) {
        memcpy(f[w] + at[w], nam, sizeof nam);
        if (!g->save_files->write ||
            !g->save_files->write(g->save_files->user, names[w], f[w], n[w])) {
            stop = PAIR_IDS_STOP_WRITE;
            goto done;
        }
    }
done:
    free(f[0]);
    free(f[1]);
    return stop;
}
/* The carried roster's levels: a member above level 24 comes to Darkside at level 25. Original: 18037's cap of +23h
 * (above 18h set to 19h). */
static void cap_carried_levels(uint8_t *roster)
{
    for (unsigned k = 0; k < XEEN_ROSTER_MEMBERS; ++k) {
        if (roster[k * XEEN_CHARACTER_BYTES + XEEN_CHARACTER_LEVEL] > 24u) {
            roster[k * XEEN_CHARACTER_BYTES + XEEN_CHARACTER_LEVEL] = 0x19u;
        }
    }
}
/* Carries the Clouds game into its Darkside slot file (the save pair's repair, after the Darkside state file was
 * copied to it): from the Clouds slot file its name record (the pair id set to the pair's), its roster with the
 * levels capped (cap_carried_levels) and its party file (into DS, the rest of Darkside's size zeroed); the name
 * record goes back over the Clouds file's, which is written. The Darkside file then gets, in file order, the name
 * record, the capped roster and the party file, the wares regenerated (into the DS party) first; the file-name
 * buffer keeps the Darkside name. Returns 0, or the stop of a file that cannot be read, is too short, or cannot be
 * written, or of no memory.
 * Original: 18037 (18441 on the xeen side, DS 94 = 1, after 195C5 copied dark.cur): B41C:B41A = F42C:0 or
 * 50D4:1C7E; maze.nam at count*8+2 (byte 1Eh = F61C), 297Ch roster bytes, 32Ch party bytes into ED8C after 5FAh
 * zeros (13FE7); 41D20's wares. */
static uint32_t pair_carry_clouds(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *f[2] = {NULL, NULL}, nam[XEEN_GAME_NAME_BYTES], *roster = NULL;
    size_t n[2] = {0, 0}, at[2] = {0, 0};
    static const size_t need[2] = {XEEN_GAME_NAME_BYTES + XEEN_ROSTER_BYTES + XEEN_CLOUDS_PARTY_BYTES,
                                   XEEN_GAME_NAME_BYTES + XEEN_ROSTER_BYTES + XEEN_DARK_PARTY_BYTES};
    char names[2][16];
    unsigned w;
    uint32_t stop = 0;
    roster_buffer(m);
    for (w = 0; w < 2u; ++w) {
        if (!slot_file_read(g, w, names[w], &f[w], &n[w], &at[w])) {
            stop = w ? PAIR_CARRY_STOP_READ_DARK : PAIR_CARRY_STOP_READ_CLOUDS;
            goto done;
        }
        if (at[w] > n[w] || need[w] > n[w] - at[w]) {
            stop = w ? PAIR_CARRY_STOP_SHORT_DARK : PAIR_CARRY_STOP_SHORT_CLOUDS;
            goto done;
        } /* a short read */
        if (w) {
            continue;
        }
        if (!(roster = malloc(XEEN_ROSTER_BYTES))) {
            stop = PAIR_CARRY_STOP_ROSTER;
            goto done;
        }
        memcpy(nam, f[0] + at[0], sizeof nam);
        nam[XEEN_GAME_NAME_PAIR_ID] = m->ds[XEEN_DS_PAIR_ID];
        memcpy(roster, f[0] + at[0] + XEEN_GAME_NAME_BYTES, XEEN_ROSTER_BYTES);
        cap_carried_levels(roster);
        memset(m->ds + XEEN_DS_PARTY_FILE, 0, XEEN_DARK_PARTY_BYTES);
        memcpy(m->ds + XEEN_DS_PARTY_FILE, f[0] + at[0] + XEEN_GAME_NAME_BYTES + XEEN_ROSTER_BYTES,
               XEEN_CLOUDS_PARTY_BYTES);
        memcpy(f[0] + at[0], nam, sizeof nam);
        if (!g->save_files->write ||
            !g->save_files->write(g->save_files->user, names[0], f[0], n[0])) {
            stop = PAIR_CARRY_STOP_WRITE_CLOUDS;
            goto done;
        }
    }
    memcpy(f[1] + at[1], nam, sizeof nam);
    memcpy(f[1] + at[1] + XEEN_GAME_NAME_BYTES, roster, XEEN_ROSTER_BYTES);
    xeen_item_regenerate_wares(m, newgame_draw, g);
    memcpy(f[1] + at[1] + XEEN_GAME_NAME_BYTES + XEEN_ROSTER_BYTES, m->ds + XEEN_DS_PARTY_FILE,
           XEEN_DARK_PARTY_BYTES);
    if (!g->save_files->write(g->save_files->user, names[1], f[1], n[1])) {
        stop = PAIR_CARRY_STOP_WRITE_DARK;
    }
done:
    free(f[0]);
    free(f[1]);
    free(roster);
    return stop;
}
/* The ids of both worlds' slot files (xeen side first): the save header's id byte (+1Eh) of every file
 * found, the last name kept in pair_nam. Returns 0, or 199B7 for a file whose header cannot be read. */
static uint32_t pair_read_ids(xeen_game *g)
{
    xeen_save_header h;
    for (unsigned w = 0; w < 2u; ++w) {
        for (unsigned slot = 1; slot <= 10u; ++slot) {
            int r;
            menu_name(g, XEEN_TEXT_SAVE_FILE_FORMAT, ds_text(g, w ? XEEN_TEXT_DARKSIDE_PREFIX : XEEN_TEXT_CLOUDS_PREFIX), slot);
            r = menu_header(g, ds_text(g, XEEN_DS_FILE_NAME), 0, &h);
            if (r < 0) {
                continue;
            }
            if (!r) {
                return PAIR_STOP_SLOT_IDS;
            }
            memcpy(g->pair_nam, h.name, XEEN_GAME_NAME_BYTES);
            g->pair_ids[w][slot - 1u] = h.name[XEEN_GAME_NAME_PAIR_ID];
        }
    }
    return 0;
}
/*
 * Repairs slot SLOT (0-based) of world side W, whose file has no id: the first repair shows "Creating files" and
 * makes both worlds' state files again; then the slot becomes the game's, a new pair id is drawn (1..FEh), the
 * world's state file is copied to the slot's file; on the Clouds side the Darkside half is carried over too
 * (pair_carry_clouds); both files get the pair's name record and both ids the new one. Returns 0, or the stop: no
 * random source, a state file not made, a refused copy, or the carry's or the name record's.
 * Original: 18330's window; 95A6 = the slot, F61C = 504DA(1, FEh) with AL = the slot, 195C5; 18037; 18200 (the stops
 * 18441, 19379, 19629).
 */
static uint32_t pair_repair_slot(xeen_game *g, unsigned w, unsigned slot, int *repaired)
{
    xeen_movement *m = &g->movement;
    uint16_t id = 0;
    unsigned cur;
    uint32_t stop;
    if (!g->event_random) {
        return PAIR_STOP_NO_RANDOM;
    }
    if (!*repaired) {
        panel_window(g, XEEN_WINDOW_MESSAGE_BOX, 1);
        menu_window_text(g, XEEN_WINDOW_MESSAGE_BOX, WINDOW_FRAMED, ds_text(g, XEEN_TEXT_CREATING_FILES));
        (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
        ++g->scene_returns;
        m->ds[XEEN_DS_WORLD] ^= 1u;
        if (!cur_create(g)) {
            return PAIR_STOP_CREATE;
        }
        m->ds[XEEN_DS_WORLD] ^= 1u;
        if (!cur_create(g)) {
            return PAIR_STOP_CREATE;
        }
        *repaired = 1;
    }
    m->ds[XEEN_DS_GAME_SLOT] = (uint8_t)(slot + 1u);
    (void)xeen_entity_rng_range(g->event_random(g->event_random_user, (uint16_t)(slot + 1u)), 1u, 0xfeu, &id);
    m->ds[XEEN_DS_PAIR_ID] = (uint8_t)id;
    addons_slot_remove(g, (int)slot + 1); /* add-on identify: a repaired pair */
    cur = m->ds[XEEN_DS_WORLD] ? 1u : 0u;
    if (!cur_to_slot(g, g->cur[cur].bytes, g->cur[cur].size)) {
        return PAIR_STOP_SLOT_WRITE;
    }
    if (!w && (stop = pair_carry_clouds(g)) != 0) {
        return stop;
    }
    m->ds[XEEN_DS_WORLD] ^= 1u;
    if ((stop = pair_ids(g)) != 0) {
        return stop;
    }
    m->ds[XEEN_DS_WORLD] ^= 1u;
    g->pair_ids[1][slot] = g->pair_ids[0][slot] = m->ds[XEEN_DS_PAIR_ID];
    return 0;
}
/* Scans the save pairs and repairs a broken one. Original: 18363 (DS 95 set) up to 184BA: byte 1Eh of every xeen/dark01..10 maze.nam
 * (199B7's id mode; FFh when 19AEB finds no file; the last nam read stays in
 * pair_nam, [BP-28h]). Genuine pairs carry the same nonzero id. A zero id
 * (18441), the xeen side first (DS 94 = 1), then the dark side (DS 94 = 0):
 * once 18330 ("Creating World of Xeen files" in window 15h, both CURs
 * created again by 19314); 95A6 = the slot, F61C = 504DA(1,FEh) with AL =
 * the slot, 195C5 copies the DS 94 world's CUR to its slot file; on the xeen
 * side 18037 builds the Darkside half of a Clouds file (a new game's pair
 * is the dark side); 18200 writes the pair's maze.nam into both,
 * both ids = F61C; 18765(15h) after the scan. A missing RNG is an explicit
 * boundary. */
uint32_t pair_scan(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    int repaired = 0;
    uint32_t stop;
    g->pair_world = m->ds[XEEN_DS_WORLD];
    g->pair_slot = 0;
    memset(g->pair_ids, 0xff, sizeof g->pair_ids);
    memset(g->pair_nam, 0, sizeof g->pair_nam);
    if ((stop = pair_read_ids(g)) != 0) {
        return stop;
    }
    for (unsigned w = 0; w < 2u; ++w) {
        m->ds[XEEN_DS_WORLD] = (uint8_t)(w ^ 1u);
        for (unsigned slot = 0; slot < 10u; ++slot) {
            if (!g->pair_ids[w][slot] && (stop = pair_repair_slot(g, w, slot, &repaired)) != 0) {
                return stop;
            }
        }
    }
    if (repaired) {
        panel_window(g, XEEN_WINDOW_MESSAGE_BOX, 0);
    }
    return 0;
}
/* The next slot from pair_slot on whose two files' ids differ (a missing file included), or -1. Original:
 * 184C3..184D9. */
static int pair_next(const xeen_game *g)
{
    unsigned slot;
    for (slot = g->pair_slot; slot < 10u; ++slot) {
        if (g->pair_ids[0][slot] != g->pair_ids[1][slot]) {
            return (int)slot;
        }
    }
    return -1;
}
/* The save pair's check inside a new game's save (nothing without Clouds): the slots scanned and repaired; a pair
 * still broken (possible only after a refused Delete) is an explicit stop; the world byte is put back.
 * Original: 18363 inside a new game's 19278 (FA90 = 1); 185DA. */
uint32_t menu_pair_check(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint32_t stop;
    if (!m->ds[XEEN_DS_CLOUDS_INSTALLED]) {
        return 0;
    }
    if ((stop = pair_scan(g)) != 0) {
        return stop;
    }
    if (pair_next(g) >= 0) {
        return PAIR_STOP_UNREPAIRED;
    }
    m->ds[XEEN_DS_WORLD] = g->pair_world;
    return 0;
}
/* Asks about the next broken save pair: none left puts the world byte back and enters the title. Otherwise the world
 * byte says which file is missing (Darkside when the Clouds one is), the file the scan named last is read again (not
 * the broken slot's: the scan's dark10.wox, or after a repair the Darkside name), its name record and id taken when
 * it exists, and "World of Xeen File: <name> (xeenNN.wox) is invaild!" (the original's spelling) with Delete and Exit
 * to DOS is shown in the yes/no. Stops when that header is short or the yes/no cannot open.
 * Original: 184DE for the next slot, else 185DA and the title; 199B7 re-reads F5E8, the nam into [BP-28h], its id
 * over the slot's; AC0F = 14BBA(DS:4B1C, [BP-28h], slot + 1); 50BF6(0); 2937A(AC0F, 0). */
xeen_status pair_continue(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_save_header h;
    char name[32], out[160];
    int slot = pair_next(g), r;
    if (slot < 0) {
        m->ds[XEEN_DS_WORLD] = g->pair_world;
        return menu_title_entry(g);
    }
    g->pair_slot = (uint8_t)slot;
    m->ds[XEEN_DS_WORLD] = g->pair_ids[0][slot] == 0xffu;
    r = menu_header(g, (const char *)m->ds + XEEN_DS_FILE_NAME, 0, &h);
    if (!r) {
        return menu_stop(g, PAIR_STOP_CONTINUE_IDS);
    }
    if (r > 0) {
        memcpy(g->pair_nam, h.name, XEEN_GAME_NAME_BYTES);
        g->pair_ids[0][slot] = h.name[XEEN_GAME_NAME_PAIR_ID];
    }
    memcpy(name, g->pair_nam, XEEN_GAME_NAME_BYTES);
    name[XEEN_GAME_NAME_BYTES] = 0;
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_FILE_INVALID), name, (unsigned)slot + 1u);
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, out);
    if (g->presentation) {
        (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
    }
    g->menu_stage = 0;
    if (!open_confirm(g, CONFIRM_PAIR, XEEN_DS_TEXT_BUFFER_2)) {
        return menu_stop(g, PAIR_STOP_CONFIRM);
    }
    g->menu_stage = XEEN_MENU_PAIR_POLL;
    return XEEN_SERVICE_REQUIRED;
}
/* The broken save pair's question answered: no (N, Escape) exits the program; yes deletes the slot's Clouds and then
 * Darkside file (each when it exists; a refused delete is ignored, as the original's only sets errno) and the slot's
 * add-on file, and the check goes on with the next slot.
 * Original: 2937A answered: 0 -> 185C9 2C80D; 1 -> 18553: 19AEB, 10A42 (INT 21h 41h; errno 7F/8852 unread). */
xeen_status pair_answer(xeen_game *g, int answer)
{
    unsigned w;
    size_t n = 0;
    uint8_t *f;
    g->menu_stage = 0;
    if (!answer) {
        return program_exit(g);
    }
    for (w = 0; w < 2u; ++w) {
        menu_name(g, XEEN_TEXT_SAVE_FILE_FORMAT, ds_text(g, w ? XEEN_TEXT_DARKSIDE_PREFIX : XEEN_TEXT_CLOUDS_PREFIX), (unsigned)g->pair_slot + 1u);
        if ((f = g->save_files->read(g->save_files->user, (const char *)g->movement.ds + XEEN_DS_FILE_NAME,
                                     &n)) != NULL) {
            free(f);
            if (g->save_files->remove) {
                (void)g->save_files->remove(g->save_files->user,
                                            (const char *)g->movement.ds + XEEN_DS_FILE_NAME);
            }
        }
    }
    addons_slot_remove(g, (int)g->pair_slot + 1); /* add-on identify: the slot's file */
    ++g->pair_slot;
    return pair_continue(g);
}
