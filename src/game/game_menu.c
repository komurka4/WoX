#include "game_menu.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/*
 * Where native play stops when the title menu, its credits or its options cannot go on; each value is the original
 * address of the step (tools/xeen_replay/stops.json). "Frame" stops mean the host could not present the screen.
 */
enum {
    MENU_STOP_NO_HOST = 0x1660e,           /* no framebuffer or no save files */
    MENU_STOP_CLOUDS_TITLE = 0x1660e,      /* the Clouds-only title (title2*.int) is not reconstructed */
    MENU_STOP_SPECIAL_SPRITES = 0x1664a,   /* special.icn */
    MENU_STOP_START_SPRITES = 0x16662,     /* start.icn */
    MENU_STOP_MENU_WINDOW = 0x1670e,       /* window 28, the menu's */
    MENU_STOP_PALETTE = 0x16729,           /* dark.pal */
    MENU_STOP_BACKGROUND = 0x16753,        /* world.raw */
    MENU_STOP_HEAD_FRAME = 0x167dc,
    MENU_STOP_TITLE_FRAME = 0x168ad,
    MENU_STOP_MENU_FRAME = 0x1646a,
    MENU_STOP_DIALOG_FRAME = 0x2b1bf,
    MENU_STOP_OPTIONS_FRAME = 0x16e58,
    MENU_STOP_EXIT_FRAME = 0x17009,
    MENU_STOP_EXIT_SONG = 0x17095,         /* twnnitea.m */
    CREDITS_STOP_CLOUDS = 0x164aa,         /* the Clouds credits book is not reconstructed */
    CREDITS_STOP_BACKGROUND = 0x164cb,     /* marb.raw */
    CREDITS_STOP_TEXT = 0x164e1,           /* credits.bin */
    CREDITS_STOP_FRAME = 0x16522,
    OPTIONS_STOP_WINDOW = 0x16b73,         /* window 28 at the options' size */
    OPTIONS_STOP_FRAME = 0x16d5c,
    OPTIONS_STOP_COMMAND = 0x16f27,        /* the hand-off command does not fit */
    OPTIONS_STOP_DECLINE_WINDOW = 0x16f4a  /* window 28 again after a declined option */
};

/* ------------------------------------------- 1660E main menu
 * The World of Xeen title (DS 95 set: both CCs present), Load Saved Game,
 * and the outer loop up to the 19E82 map load of the loaded game, where
 * the native session object is handed to the startup owners
 * (xeen_game_take_session / xeen_game_create_loaded). */
/** Loads sprite NAME for the menu and stores its handle in DS word WORD; returns 0 on failure. */
int menu_sprite(xeen_game *g, uint16_t word, const char *name)
{
    uint16_t h = 0;
    if (!sprite_load(g, name, &h)) {
        return 0;
    }
    if (word) {
        xeen_write16(&g->movement, word, h);
    } else {
        g->menu_special = h;
    }
    return 1;
}
/** Stops the menu at unsupported BOUNDARY. */
xeen_status menu_stop(xeen_game *g, uint32_t boundary)
{
    g->menu_stop = boundary;
    g->menu_stage = 0;
    return game_stop(g, boundary);
}
/* Owners shared with game_panel.c and game_newgame.c. */
/** Stores a far pointer to OFFSET in the menu's segment at DS word WORD. */
void menu_far(xeen_movement *m, uint16_t word, uint16_t offset)
{
    xeen_write16(m, word, offset);
    xeen_write16(m, (uint16_t)(word + 2u), XEEN_DS_SEGMENT);
}
/** Points the live button set's six tables (ds_globals.h: x words, then the y, width, height, key and enabled
 * bytes) at those offsets in the menu's segment, in that order. */
void button_tables(xeen_movement *m, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t key,
                   uint16_t enabled)
{
    menu_far(m, XEEN_DS_BUTTON_X_TABLE, x);
    menu_far(m, XEEN_DS_BUTTON_Y_TABLE, y);
    menu_far(m, XEEN_DS_BUTTON_WIDTH_TABLE, width);
    menu_far(m, XEEN_DS_BUTTON_HEIGHT_TABLE, height);
    menu_far(m, XEEN_DS_BUTTON_KEY_TABLE, key);
    menu_far(m, XEEN_DS_BUTTON_ENABLED_TABLE, enabled);
}
/* The far pointer B41A (offset word; the segment at B41C) the roster and save reads read through (game_menu.h). */
#define ROSTER_READ_BUFFER 0xb41au
/* Points the roster buffer (contract in game_menu.h) at F42C:0 or the fixed 50D4:1C7E. Original: 19A0D. */
void roster_buffer(xeen_movement *m)
{
    if (xeen_read16(m, XEEN_DS_FILE_BUFFER_SEGMENT)) {
        xeen_write16(m, ROSTER_READ_BUFFER + 2u, xeen_read16(m, XEEN_DS_FILE_BUFFER_SEGMENT));
        xeen_write16(m, ROSTER_READ_BUFFER, 0);
    } else {
        xeen_write16(m, ROSTER_READ_BUFFER + 2u, 0x50d4);
        xeen_write16(m, ROSTER_READ_BUFFER, 0x1c7e);
    }
}
/* Reads the header of save file NAME into H (FULL: the whole header, else its first part) through the host's save
 * files. The roster buffer pointer is set first, as the original's read does (roster_buffer). Returns -1 when the
 * file is missing, 0 when it is shorter than the reads, 1 when read.
 * Original: 199B7 (F42C nonzero: B41C:B41A = F42C:0, else 50D4:1C7E at 19A0D; 19AEB the missing file). */
int menu_header(xeen_game *g, const char *name, int full, xeen_save_header *h)
{
    size_t n = 0;
    uint8_t *f;
    int ok;
    xeen_movement *m = &g->movement;
    if (!g->save_files || !(f = g->save_files->read(g->save_files->user, name, &n))) {
        return -1;
    }
    roster_buffer(m);
    ok = xeen_save_header_read(f, n, full, h);
    free(f);
    return ok;
}
/* Writes a file name such as "xeen01.wox" (FORMAT with PREFIX and NUMBER) into the DS file-name buffer; the formats
 * are the original's DS texts, whose conversions the callers' arguments match. Original: 14BBA into F5E8. */
void menu_name(xeen_game *g, uint16_t format, const char *prefix, unsigned number)
{
    char out[32];
    (void)snprintf(out, sizeof out, ds_text(g, format), prefix, number);
    ds_put_text(g, XEEN_DS_FILE_NAME, out);
}
/* The free DOS memory word: the largest free block in paragraphs / 64, which the state-archive routines cap at 63.
 * The native platform reports 0FC0h paragraphs (63 KB: the cap, the canonical harness's labelled DOS answer), so it
 * is always 63. Gameplay reads it only as the spell points of the Cast dialog's caster-less record (the "member -1"
 * quirk in cast_open). Its two neighbours stay unwritten.
 * Original: 18886's B414 = 11C6A (INT 21h 48h with FFFFh) SAR 6; capped at 3Fh by 19314/195C5/1978C (1944F..19459,
 * 19684..1968E, 1984B..19855), read by 29502 at 2956A; B2BC + 158h at 3D9CB; B410/B412 (11C6A, 50C9C). */
void dos_free_memory(xeen_movement *m)
{
    xeen_write16(m, 0xb414, 63u);
}
/* The program's own exit (the save pair's "Exit to DOS!" and the panel's Quit): the drivers are released, the text
 * mode set, the farewell printed with "World" (Clouds installed) or "Darkside", and the program ends with its exit
 * code. The session ends here, its DS as at the entry (the text is only the host's message).
 * Original: 2C80D (from 184DE and 29B4D): 483B4, 4FD02, 187A5(0), 18827, 4E231, 4AA3E(1), 4A4F8(0), 1307A(3), 11C8B(7),
 * 11CA0(0), AC0F = DS:607E (DS 95), 11E25, 1061F(F606): INT 21h 4Ch. */
xeen_status program_exit(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    char out[96];
    size_t n;
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_THANK_YOU), ds_text(g, m->ds[XEEN_DS_CLOUDS_INSTALLED] ? XEEN_TEXT_THANKS_CLOUDS_TITLE : XEEN_TEXT_THANKS_DARKSIDE_TITLE));
    n = strcspn(out, "\n\r");
    if (n >= sizeof g->program_exit) {
        n = sizeof g->program_exit - 1u;
    }
    memcpy(g->program_exit, out, n);
    g->program_exit[n] = 0;
    g->menu_stage = 0;
    return original_exit(g, PROGRAM_EXIT_BOUNDARY);
}
/* One frame of the title's animation: the frame timer marked, the step counter (DS F47C) advanced modulo 13, the
 * saved title screen restored, and unless the counter is 0 frame (counter mod 5) of animation sheet counter / 5
 * (world0..2.int, handles at F99C) drawn over it. Original: 16827 / 16388 (4EA0D; 4E99F(8C90)). */
static xeen_status menu_animate(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_movement *m = &g->movement;
    int16_t frame;
    xeen_status status;
    status = frame_base(g, bios, user);
    if (status != XEEN_OK) {
        return status;
    }
    frame = (int16_t)(((int16_t)xeen_read16(m, 0xf47c) + 1) % 13);
    xeen_write16(m, 0xf47c, (uint16_t)frame);
    exit_restore(g);
    if (frame) {
        exit_draw(g, xeen_read16(m, (uint16_t)(0xf99cu + 2u * (unsigned)(frame / 5))),
                  (uint16_t)(frame % 5), 0, 0, 0);
    }
    return XEEN_OK;
}
/* The menu panel over the animation: the menu window (28) framed with its title text, then each button whose byte in
 * enabled byte is set, drawn at its MAIN_MENU_BUTTON_X/_Y position with start.icn frame 2i (at most eight;
 * the x table ends at FFFFh). Original: 16380's body (window 1Ch, DS 4793; the 18D2/18E6/1902 tables). */
static void menu_panel(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    unsigned i;
    menu_window_text(g, XEEN_WINDOW_DEFINED, WINDOW_FRAMED, ds_text(g, XEEN_TEXT_MENU_TITLE_WORLD));
    for (i = 0; xeen_read16(m, (uint16_t)(XEEN_DS_MAIN_MENU_BUTTON_X + 2u * i)) != 0xffffu && i < 8u; ++i) {
        if (m->ds[XEEN_DS_MAIN_MENU_BUTTON_ENABLED + i]) {
            exit_draw(g, xeen_read16(m, 0xf45a), (uint16_t)(2u * i),
                      (int16_t)xeen_read16(m, (uint16_t)(XEEN_DS_MAIN_MENU_BUTTON_X + 2u * i)),
                      m->ds[XEEN_DS_MAIN_MENU_BUTTON_Y + i], 0);
        }
    }
}
/** Starts the menu's wait on the frame clock. Original: 4EA1C. */
static void menu_wait(xeen_game *g)
{
    g->menu_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
}
/* The timer ticks since the frame's mark into *TICKS (signed: the menu compares them signed), then a new wait
 * starts. Original: 4EA1C after 4EA0D (its JL/JGE tests). */
static xeen_status menu_ticks(xeen_game *g, xeen_timer_bios bios, void *user, int16_t *ticks)
{
    xeen_status status;
    if ((status = timer_ticks(g, &g->menu_elapsed, bios, user)) != XEEN_OK) {
        return status;
    }
    *ticks = (int16_t)g->timer_registers.ax;
    menu_wait(g);
    return XEEN_OK;
}
/** Stops the menu at stage STAGE awaiting input at BOUNDARY. */
xeen_status menu_set(xeen_game *g, int stage, uint32_t boundary)
{
    g->menu_stage = stage;
    g->phase = XG_STOPPED;
    g->lifecycle_boundary = boundary;
    g->command_status = XGC_PENDING_INPUT;
    return XEEN_SERVICE_REQUIRED;
}
static xeen_status menu_head(xeen_game *, int);
/*
 * The title menu's start (Darkside installed with the Clouds): the host checked (a page and save files), DS 8D28
 * set to 1 (read only by the inn, before its music: not named) and the menu music started; the sprites loaded (special.icn, the three title
 * animation sheets, start.icn), the menu window defined (72, 25, 150 x 176), the screen faded out and dark.pal
 * loaded; then the title's head (menu_head). The Clouds-only title is not reconstructed and stops.
 * Original: 1660E's head with DS 95 set, through 16822.
 */
static xeen_status menu_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_presentation *p = g->presentation;
    const xeen_archive *a;
    if (!p || !p->framebuffer || p->framebuffer_size < XEEN_PRESENT_PIXELS || !g->save_files) {
        return menu_stop(g, MENU_STOP_NO_HOST);
    }
    if (!m->ds[XEEN_DS_CLOUDS_INSTALLED] || !m->ds[XEEN_DS_WORLD]) {
        return menu_stop(g, MENU_STOP_CLOUDS_TITLE);
    }
    m->ds[0x8d28] = 1;
    g->menu_first = 0;
    exit_event(g, "music F42A");
    if (g->native_audio_enabled) {
        xeen_native_audio_start_buffer(&g->audio, m);
    }
    if (!menu_sprite(g, 0, ds_text(g, XEEN_TEXT_MENU_SPECIAL_SPRITES))) {
        return menu_stop(g, MENU_STOP_SPECIAL_SPRITES); /* its handle is g->menu_special. Original: [BP-8] */
    }
    if (!menu_sprite(g, 0xf99c, ds_text(g, XEEN_TEXT_MENU_ANIMATION_0)) ||
        !menu_sprite(g, 0xf99e, ds_text(g, XEEN_TEXT_MENU_ANIMATION_1)) ||
        !menu_sprite(g, 0xf9a0, ds_text(g, XEEN_TEXT_MENU_ANIMATION_2)) ||
        !menu_sprite(g, 0xf45a, ds_text(g, XEEN_TEXT_MENU_START_SPRITES))) {
        return menu_stop(g, MENU_STOP_START_SPRITES);
    }
    if (xeen_presentation_define_window(p, XEEN_WINDOW_DEFINED, 72, 25, 150, 176) != XEEN_OK) {
        return menu_stop(g, MENU_STOP_MENU_WINDOW); /* 50B45 */
    }
    xeen_presentation_fade(p, 0, 4); /* 1671F 4A75A(4) (DS 95 set); was a counter only */
    if (!(a = selected_cc(g)) || xeen_raster_palette(p, a, ds_text(g, XEEN_TEXT_MENU_PALETTE)) != XR_OK) {
        return menu_stop(g, MENU_STOP_PALETTE);
    }
    xeen_presentation_trace("palload %s",
                            ds_text(g, XEEN_TEXT_MENU_PALETTE)); /* Evidence: 16729 188BB (CS:0C3A only) */
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    return menu_head(g, 0);
}
/* The title menu's head: the menu background, the mouse cursor hidden, and for the program's first title (SI 0)
 * the frame presented and faded in; the game slot cleared and the world remembered. The first time with the Clouds
 * installed, the save pairs are checked first (pair_scan); then the title loop. SI is 1 after the credits.
 * Original: 16748..16822 with DS 95 set (world.raw, 186B2(8C90), 15647(0,0), at once with DS 94 set: 1565F;
 * 50BF6(0) and the 4A74F(4) fade; 4E06B(F460,0); 95A6 = 0, [BP-4] = DS 94; the first time ([BP-6] 0) 18363;
 * 191DE([BP-4]), 1919B, 16827's title loop); SI 1 from the credits at 16B1D. */
static xeen_status menu_head(xeen_game *g, int si)
{
    xeen_movement *m = &g->movement;
    xeen_presentation *p = g->presentation;
    uint32_t stop;
    if (!menu_raw(g, ds_text(g, XEEN_TEXT_MENU_BACKGROUND))) {
        return menu_stop(g, MENU_STOP_BACKGROUND);
    }
    exit_save(g);
    xeen_presentation_mouse_off(p); /* Original: 15647(0,0)'s 1564D 4E231 */
    if (!si) {
        if (exit_present(g) != XEEN_OK) {
            return menu_stop(g, MENU_STOP_HEAD_FRAME);
        }
        xeen_presentation_fade(p, 1, 4); /* the fade-in (Original: 167E4 4A74F(4) after 167DC 50BF6(0)) */
    }
    (void)xeen_presentation_select_cursor(p, xeen_read16(m, XEEN_DS_MOUSE_SPRITES), 0);
    m->ds[XEEN_DS_GAME_SLOT] = 0;
    g->menu_world = m->ds[XEEN_DS_WORLD];
    if (!g->menu_first) {
        g->menu_first = 1;
        if (m->ds[XEEN_DS_CLOUDS_INSTALLED]) {
            if ((stop = pair_scan(g)) != 0) {
                return menu_stop(g, stop);
            }
            return pair_continue(g);
        }
    }
    return menu_title_entry(g);
}
/* The title loop's entry: the remembered world's archive, the default buttons, then the title's frame.
 * Original: 1680F..16822 after 18363 (191DE([BP-4]), 1919B, 16827's title loop). */
xeen_status menu_title_entry(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_select_archive(m, g->menu_world);
    button_defaults(m);
    return menu_set(g, XEEN_MENU_TITLE, MENU_TITLE_BOUNDARY);
}
/* The credits (the title menu's C or V): credits.bin's four pages over marb.raw, each until a key or a click, then
 * the title's head again (as after its first showing). Page k is the text after the file's k-th NUL (page 0 the
 * file's start: the original never writes its offset, as for tavern.bin and qnotes.bin).
 * Original: 16B11 (18765(1Ch), 164A2, SI = 1, 16748); 164A2: 15647(1,0), 9A1C saved and 11h, 188F6(marb.raw),
 * 186B2(8C90), 2B632(credits.bin) into 50D4:1C7E (97C2:97C0, the size 8582, the offsets 50D4:165C); per page
 * 4E38C(0, 97C0 + entry), 50BF6(0), 189E3, 4E99F(8C90); then 9A1C back, 15647(1,0). */
static const char *credits_entry(const xeen_game *g, unsigned k)
{
    return nul_entry(g->credits, g->credits_size, k);
}
/* Shows the current credits page in the screen window (the first page also hides the cursor and installs the
 * default buttons) and waits for its key; the previous key is kept (no key flush). Stops when the page's text is
 * missing or the screen cannot be presented. Original: 164A2's four pages. */
static xeen_status credits_page(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    const char *text = credits_entry(g, g->credits_page);
    static const uint32_t text_site[4] = {0x164feu, 0x1655cu, 0x1659bu, 0x165dau};
    if (!text) {
        return menu_stop(g, text_site[g->credits_page]);
    }
    menu_window_text(g, XEEN_WINDOW_SCREEN, WINDOW_TEXT_ONLY, text);
    if (!g->credits_page) {
        xeen_presentation_mouse_off(g->presentation);
        (void)xeen_presentation_select_cursor(g->presentation, xeen_read16(m, XEEN_DS_MOUSE_SPRITES), 0);
    } /* 15647(0,0) (its 4E231), 4E06B */
    if (exit_present(g) != XEEN_OK) {
        return menu_stop(g, CREDITS_STOP_FRAME);
    }
    if (!g->credits_page) {
        button_defaults(m);
    }
    /* F5FE keeps the previous key (C or V, then the page's): no 189D7. */
    return menu_set(g, (int)(XEEN_MENU_CREDITS_POLL + g->credits_page),
                    MENU_CREDITS_POLL_BOUNDARY + MENU_CREDITS_POLL_STRIDE * g->credits_page);
}
/* Opens the credits: the menu window closed, the cursor hidden, the UI mode kept and set to the location mode, the
 * marb.raw background and the saved screen, credits.bin loaded, then the first page. Stops on the Clouds side (the
 * original's book with DS 94 clear is not reconstructed), or without the background or the text.
 * Original: 164A2 (stop 164AA), 16B13/164B4's 18765(1Ch), 1564D's 4E231. */
static xeen_status credits_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    size_t n = 0;
    if (!m->ds[XEEN_DS_WORLD]) {
        return menu_stop(g, CREDITS_STOP_CLOUDS); /* 15647(1, 0)'s book with DS 94 clear */
    }
    xeen_window_close(m, XEEN_WINDOW_DEFINED);                     /* the menu's window (Original: 16B13, 164B4 18765(1Ch)) */
    xeen_presentation_mouse_off(g->presentation); /* the cursor hidden (Original: 164A2 15647(1,0): 1564D 4E231) */
    g->credits_ui_mode = m->ds[XEEN_DS_UI_MODE];
    m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_LOCATION;
    if (!menu_raw(g, ds_text(g, XEEN_TEXT_CREDITS_BACKGROUND))) {
        return menu_stop(g, CREDITS_STOP_BACKGROUND);
    }
    exit_save(g);
    free(g->credits);
    g->credits = menu_load(g, ds_text(g, XEEN_TEXT_CREDITS_TEXT), &n);
    g->credits_size = n;
    if (!g->credits) {
        return menu_stop(g, CREDITS_STOP_TEXT);
    }
    xeen_write16(m, XEEN_DS_TEXT_BLOCK_SEGMENT, 0x50d4);
    xeen_write16(m, XEEN_DS_TEXT_BLOCK, 0x1c7e);
    g->credits_page = 0;
    return credits_page(g);
}
/* A key in the credits: the screen restored, then the next page, or after the fourth the UI mode back, the text freed
 * and the title's head as after the credits. Original: 164A2's page loop; 16B1D (SI = 1), 16748. */
static xeen_status credits_key(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    exit_restore(g);
    if (++g->credits_page < 4u) {
        return credits_page(g);
    }
    m->ds[XEEN_DS_UI_MODE] = g->credits_ui_mode;
    xeen_presentation_mouse_off(g->presentation); /* its 1564D 4E231 */
    free(g->credits);
    g->credits = NULL;
    g->credits_size = 0;
    return menu_head(g, 1); /* 16B1D SI = 1, 16748 */
}
/* The title menu opened: its four buttons installed (start.icn's sprites), the menu window open and the in-menu flag
 * set, then the title's frames. Original: 16996 (the button set main0, window 1Ch, 8C77 = 1), then 16380(1). */
static xeen_status menu_open(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    button_tables(m, XEEN_BUTTONS_MAIN_MENU);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, xeen_read16(m, 0xf45a));
    xeen_window_open(m, XEEN_WINDOW_DEFINED);
    m->ds[XEEN_DS_IN_MENU] = 1;
    return menu_set(g, XEEN_MENU_FRAME, MENU_FRAME_BOUNDARY);
}
/* The main menu's exit, after a load, a new game or an Other Options choice: the Darkside archive, the menu music
 * stopped, the buttons and windows reset, the title frame presented once more, the menu's sprites freed, the menu
 * flag cleared, the cursor hidden; returns 0, or the stop when the frame cannot be presented.
 * Original: 16FAB..1709D (191DE(1), 187A5(CFh), 1919B, 186C0, F47C = 7 and its title frame, 50BF6(0),
 * 4AA3E([BP-8]), 18693(F42C), 8C77 = 0, 189D7, 15647(1,0) (DS 94 set: 1565F at once), 187A5(0), 18693(F42A), the
 * 17EC driver byte (platform), F42A = 18975(twnnitea.m)). */
uint32_t menu_exit(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_select_archive(m, 1);
    exit_event(g, "music CFh");
    if (g->native_audio_enabled) {
        xeen_native_audio_music(&g->audio, m, 0xcfu);
    }
    button_defaults(m);
    xeen_window_close_all(m);
    xeen_write16(m, 0xf47c, 7);
    exit_restore(g);
    exit_draw(g, xeen_read16(m, 0xf99e), 2, 0, 0, 0);
    if (exit_present(g) != XEEN_OK) {
        return MENU_STOP_EXIT_FRAME;
    }
    (void)xeen_sprite_free_from(&g->sprites, g->menu_special);
    xeen_write16(m, XEEN_DS_FILE_BUFFER_SEGMENT, 0);
    m->ds[XEEN_DS_IN_MENU] = 0;
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    /* The cursor stays hidden from here to the world entry, so the first scene's present shows no cursor (an
     * owner report: the Load fade's black squares). Original: 15647(1,0)'s 1564D 4E231, until 17AA6's 4E06B; the
     * 1B503(0) present at 1BECF (4E33F). */
    xeen_presentation_mouse_off(g->presentation);
    exit_music_off(g);
    xeen_write16(m, XEEN_DS_SONG_SEGMENT, 0);
    if (!menu_load_word(g, XEEN_DS_SONG_SEGMENT, ds_text(g, XEEN_TEXT_EXIT_SONG))) {
        return MENU_STOP_EXIT_SONG;
    }
    return 0;
}
/* Other Options (16B23, a case of 1660E; PREFLIGHT.md 2.4). The
 * live tables point into 1660E's own frame (BP-38..BP-1, enter 38h): the
 * native keeps that frame's image in the input poll (xeen_input_poll_frame)
 * under the marker OPTIONS_SS:OPTIONS_BP (the MZ header's SS in file values,
 * as for 2F5B1, and a native frame pointer), so 18AC5/18C0C read
 * the buttons as the original does without any stack address. Image
 * offsets are BP-relative + 38h: x words at BP-38 (FFFFh at BP-2E), y at
 * BP-28, heights at BP-22, widths at BP-1C, enabled at BP-16, keys at
 * BP-10, [BP-8] the special.icn handle. */
#define OPTIONS_SS 0x57f5u
#define OPTIONS_BP 0xfff0u
#define OPT(bp)    (0x38u - (bp))
/** Stores a far pointer to the Other Options frame's BP-relative offset BP at DS word WORD. */
static void options_far(xeen_movement *m, uint16_t word, unsigned bp)
{
    xeen_write16(m, word, (uint16_t)(OPTIONS_BP - bp));
    xeen_write16(m, (uint16_t)(word + 2u), OPTIONS_SS);
}
/* The Other Options window: its title ("World" with the Clouds installed, else "Darkside"; the copyright line moved
 * down by options_si + 2Ah) in the menu window, FRAMED or text only, then each enabled button (special.icn frame 2i
 * at its x and y) until the x table's end. Original: 16D62..16E58 (4F6F8(1Ch), 14BBA/4E38C of DS:48C9, 4AC92). */
static void options_panel(xeen_game *g, int framed)
{
    xeen_movement *m = &g->movement;
    const uint8_t *f = g->options_frame;
    char text[160];
    unsigned i;
    (void)snprintf(text, sizeof text, ds_text(g, XEEN_TEXT_MENU_TITLE_FORMAT),
                   ds_text(g, m->ds[XEEN_DS_CLOUDS_INSTALLED] ? XEEN_TEXT_OPTIONS_CLOUDS_TITLE : XEEN_TEXT_OPTIONS_DARKSIDE_TITLE), g->options_si + 0x2a);
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, text);
    menu_window_text(g, XEEN_WINDOW_DEFINED, framed, text);
    for (i = 0; xeen_get_le16(f + OPT(0x38) + 2u * i) != 0xffffu && i < 5u; ++i) {
        if (f[OPT(0x16) + i]) {
            exit_draw(g, g->menu_special, (uint16_t)(2u * i),
                      (int16_t)xeen_get_le16(f + OPT(56) + 2u * i), f[OPT(40) + i], 0);
        }
    }
}
/* Which of the five Other Options buttons D C E V W are offered: D always; C with the Clouds installed; E once the
 * Darkside ending was seen (8D4E); V with the Clouds installed while 8C80 is clear; W after the World ending (8D4F)
 * with the Clouds installed. The window grows 19h for each offered button after D (16B2B..16B61's SI). */
static void options_offered(const uint8_t *ds, int on[5])
{
    on[0] = 1;
    on[1] = ds[XEEN_DS_CLOUDS_INSTALLED] != 0;
    on[2] = ds[XEEN_DS_CONFIG_DARKSIDE_ENDING] != 0;
    on[3] = !ds[XEEN_DS_CLOUDS_ENDING_UNMARKED] && ds[XEEN_DS_CLOUDS_INSTALLED];
    on[4] = ds[XEEN_DS_CONFIG_WORLD_ENDING] && ds[XEEN_DS_CLOUDS_INSTALLED];
}
/* The options frame image's button table (the original's own frame, BP-38..BP-1): special.icn's handle at BP-8; each
 * button 86h x 14h at x 5Dh; the x table's end FFFFh at BP-2E; D at y 35h, enabled, key 'D'; then C E V W in turn,
 * each offered one 19h below the last enabled one with its key, a button not offered at y 1, disabled, without a
 * key. Original: 16BC7..16CBB. */
static void options_buttons(xeen_game *g, const int on[5])
{
    static const uint8_t keys[5] = {'D', 'C', 'E', 'V', 'W'};
    uint8_t *f = g->options_frame;
    unsigned i, y = 0x35;
    memset(f, 0, sizeof g->options_frame);
    f[OPT(0x8)] = (uint8_t)g->menu_special;
    f[OPT(0x8) + 1u] = (uint8_t)(g->menu_special >> 8);
    for (i = 0; i < 5u; ++i) {
        f[OPT(0x1c) + i] = 0x86;
        f[OPT(0x22) + i] = 0x14;
        f[OPT(0x38) + 2u * i] = 0x5d;
    }
    f[OPT(0x2e)] = 0xff;
    f[OPT(0x2e) + 1u] = 0xff;
    f[OPT(0x28)] = (uint8_t)y;
    f[OPT(0x16)] = 1;
    f[OPT(0x10)] = keys[0];
    for (i = 1; i < 5u; ++i) {
        if (on[i]) {
            y += 0x19;
            f[OPT(0x28) + i] = (uint8_t)y;
            f[OPT(0x16) + i] = 1;
            f[OPT(0x10) + i] = keys[i];
        } else {
            f[OPT(0x28) + i] = 1;
            f[OPT(0x16) + i] = 0;
            f[OPT(0x10) + i] = 0;
        }
    }
}
/* Opens Other Options: the menu window closed and redefined 19h taller for each offered button after D
 * (options_offered), the button set pushed and the live tables pointed into the options' frame image (the original's
 * own stack frame; the input poll reads it there), whose button table options_buttons writes. With the Clouds
 * installed the title's frames go on under the window; without, it is drawn once and polled. Stops when the window
 * cannot be defined or presented.
 * Original: 16B23 (18765(1Ch), 50B45(48h, 19h, B0h, SI + 4Bh, 1Ch), 1871B(1Ch), 190ED, the tables 16B88..16BC7,
 * 13FE7); DS 95 clear: 16CC9..16D5F and the poll 16E76; set: the 16D62 frames and the poll 16E5E. */
static xeen_status options_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned i;
    static const uint16_t live[6] = {XEEN_DS_BUTTON_X_TABLE,     XEEN_DS_BUTTON_Y_TABLE,
                                     XEEN_DS_BUTTON_WIDTH_TABLE, XEEN_DS_BUTTON_HEIGHT_TABLE,
                                     XEEN_DS_BUTTON_KEY_TABLE,   XEEN_DS_BUTTON_ENABLED_TABLE};
    static const unsigned bp[6] = {0x38, 0x28, 0x1c, 0x22, 0x10, 0x16};
    int on[5];
    options_offered(ds, on);
    xeen_window_close(m, XEEN_WINDOW_DEFINED);
    g->options_si = 0;
    for (i = 1; i < 5u; ++i) {
        if (on[i]) {
            g->options_si += 0x19;
        }
    }
    if (xeen_presentation_define_window(g->presentation, XEEN_WINDOW_DEFINED, 72, 25,
                                        (uint16_t)(g->options_si + 75), 176) != XEEN_OK) {
        return menu_stop(g, OPTIONS_STOP_WINDOW);
    }
    xeen_window_open(m, XEEN_WINDOW_DEFINED);
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    for (i = 0; i < 6u; ++i) {
        options_far(m, live[i], bp[i]);
    }
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, g->menu_special); /* the buttons use special.icn's sprites */
    options_buttons(g, on);
    xeen_input_poll_frame(&g->input_poll, OPTIONS_SS, (uint16_t)(OPTIONS_BP - 0x38u), g->options_frame,
                          sizeof g->options_frame);
    if (!ds[XEEN_DS_CLOUDS_INSTALLED]) {
        options_panel(g, 0);
        if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_DEFINED) != XEEN_OK) {
            return menu_stop(g, OPTIONS_STOP_FRAME);
        }
        return menu_set(g, XEEN_MENU_OPTIONS_POLL, MENU_OPTIONS_STILL_POLL_BOUNDARY);
    }
    return menu_set(g, XEEN_MENU_OPTIONS, MENU_OPTIONS_BOUNDARY);
}
/* Other Options declined (a disabled button, or a key the options do not take): the options window closes and
 * window 28 is redefined as (72, 25, 150, 176), the menu's buttons come back, and with the Clouds installed window 28
 * opens again before the main menu's frames.
 * Original: 16F36 (18765(1Ch), 50B45(48h, 19h, B0h, 96h, 1Ch), 18FFF), then 16A19 (1871B(1Ch) with DS 95,
 * 8C77 = 1, the 16380(1) frames); the keys are CS:0D78's. */
static xeen_status options_decline(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_window_close(m, XEEN_WINDOW_DEFINED);
    if (xeen_presentation_define_window(g->presentation, XEEN_WINDOW_DEFINED, 72, 25, 150, 176) != XEEN_OK) {
        return menu_stop(g, OPTIONS_STOP_DECLINE_WINDOW);
    }
    xeen_button_stack(m->ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    xeen_input_poll_frame(&g->input_poll, 0, 0, NULL, 0);
    if (m->ds[XEEN_DS_CLOUDS_INSTALLED]) {
        xeen_window_open(m, XEEN_WINDOW_DEFINED);
    }
    m->ds[XEEN_DS_IN_MENU] = 1;
    return menu_set(g, XEEN_MENU_FRAME, MENU_FRAME_BOUNDARY);
}
/* An Other Options key (C, D, E, V or W): an enabled option sets its XEENCOMMAND for XEEN.EXE (D also clears 8D52
 * and writes xeen.cfg's 30 bytes from 8D38, its result untested); then the menu exits and the program's teardown
 * hands off to XEEN.EXE.
 * Original: 16E85 ([BP-2C] = F5FE, the keys CS:0D78; [BP-2] = 1, 16FAB's exit, RETF AX 1; 15FE6, 1617E's teardown,
 * the hand-off 123A1). */
static xeen_status options_key(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t key = xeen_read16(m, XEEN_DS_INPUT_CODE), command = 0;
    uint32_t stop;
    g->options_frame[OPT(0x2c)] = (uint8_t)key;
    g->options_frame[OPT(0x2c) + 1u] = (uint8_t)(key >> 8);
    if (key == 'D') {
        command = XEEN_TEXT_COMMAND_DARKSIDE_INTRO;
        xeen_write16(m, 0x8d52, 0);
        g->handoff_cfg = g->save_files && g->save_files->write &&
                         g->save_files->write(g->save_files->user, "XEEN.CFG", ds + XEEN_DS_CONFIG, XEEN_CONFIG_BYTES);
    } else if (key == 'C' && ds[XEEN_DS_CLOUDS_INSTALLED]) {
        command = XEEN_TEXT_COMMAND_CLOUDS_INTRO;
    } else if (key == 'E' && ds[XEEN_DS_CONFIG_DARKSIDE_ENDING]) {
        command = XEEN_TEXT_COMMAND_DARKSIDE_ENDING;
    } else if (key == 'V' && !ds[XEEN_DS_CLOUDS_ENDING_UNMARKED] && ds[XEEN_DS_CLOUDS_INSTALLED]) {
        command = XEEN_TEXT_COMMAND_CLOUDS_ENDING;
    } else if (key == 'W' && ds[XEEN_DS_CONFIG_WORLD_ENDING] && ds[XEEN_DS_CLOUDS_INSTALLED]) {
        command = XEEN_TEXT_COMMAND_WORLD_ENDING;
    }
    if (!command) {
        return options_decline(g);
    }
    {
        const char *text = ds_text(g, command);
        size_t k = strlen(text);
        if (k >= sizeof g->handoff_command) {
            return menu_stop(g, OPTIONS_STOP_COMMAND);
        }
        memcpy(g->handoff_command, text, k + 1u);
    }
    g->options_frame[OPT(0x2)] = 1;
    xeen_input_poll_frame(&g->input_poll, 0, 0, NULL, 0);
    if ((stop = menu_exit(g)) != 0) {
        return menu_stop(g, stop);
    }
    g->menu_stage = 0;
    return handoff_teardown(g);
}
/* Polls the keyboard for the main menu (counted in menu_polls): the key, also left in the key code, or 0.
 * Original: 189E3 (AX = F5FE). */
static uint16_t menu_poll(xeen_game *g)
{
    ++g->menu_polls;
    return exit_poll(g);
}
/** Dispatches the main menu's key (F5FE): Load, New Game, credits or the others. */
static xeen_status menu_dispatch(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    switch (xeen_read16(m, XEEN_DS_INPUT_CODE)) {
        case XEEN_KEY_ESCAPE: /* 16F5A: 18765(1Ch), 16822 */
            xeen_window_close(m, XEEN_WINDOW_DEFINED);
            button_defaults(m);
            return menu_set(g, XEEN_MENU_TITLE, MENU_TITLE_BOUNDARY);
        case 'C':
        case 'V':
            return credits_begin(g); /* 16B11: 18765(1Ch), credits 164A2 */
        case 'O':
            return options_begin(g);
        case 'S':
            return menu_new(g); /* 16A53: 2AD6D(0) Start Game Menu */
        case 'L':
            /* Load Saved Game: the Darkside saves into the file list (the original's 18E77 at 16AAE); with none,
             * the "no saved games" message instead of the slot dialog. */
            return panel_list(g, XEEN_TEXT_DARK_SAVES_PATTERN) ? dialog_begin(g) : menu_no_saves(g);
        default:
            return menu_set(g, XEEN_MENU_FRAME, MENU_FRAME_BOUNDARY);
    }
}
/* The title's and the load dialog's 189E3 wait (168B3, 2B1C5): a key ends it at once; else it ends once
 * the frame took 4 ticks. XEEN_OK when the wait ended (the caller then reads F5FE), else the clock's
 * status or XEEN_SERVICE_REQUIRED. */
static xeen_status menu_key_or_ticks(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    int16_t ticks;
    if (!menu_poll(g)) {
        if ((status = menu_ticks(g, bios, user, &ticks)) != XEEN_OK) {
            return status;
        }
        if (ticks < 4) {
            return XEEN_SERVICE_REQUIRED;
        }
    }
    return XEEN_OK;
}
/* The main menu's and the options panel's wait after their poll (16470, 16E5E): the clock is read every
 * time; the wait ends at 3 ticks or with a key. XEEN_OK when it ended, as menu_key_or_ticks. */
static xeen_status menu_ticks_or_key(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    int16_t ticks;
    if ((status = menu_ticks(g, bios, user, &ticks)) != XEEN_OK) {
        return status;
    }
    if (ticks < 3 && !xeen_read16(&g->movement, XEEN_DS_INPUT_CODE)) {
        return XEEN_SERVICE_REQUIRED;
    }
    return XEEN_OK;
}
/*
 * The title menu's state machine (enum xeen_menu_stage). Each screen is a pair of stages: a frame stage draws one
 * frame (the title's animation, then the screen's own panels over it), which the host presents, and starts the
 * frame wait; its poll stage then waits for a key and either draws the next frame (no key) or acts on the key. The
 * scroll and the credits' pages have no frame stage of their own (their openers drew them).
 */
/* Draws frame stage STAGE's frame: the title's next animation step, then over it the main menu's panel (the main
 * menu and the slot dialog), the slot dialog and Other Options' panel, each framed. Returns the frame clock's
 * status. Original: 16827 (the title), 16380(1) (the main menu), 2B117 (the dialog), 16D62 (Other Options). */
static xeen_status menu_frame(xeen_game *g, int stage, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    if ((status = menu_animate(g, bios, user)) != XEEN_OK) {
        return status;
    }
    switch (stage) {
        case XEEN_MENU_FRAME:
            menu_panel(g);
            break;
        case XEEN_MENU_DIALOG:
            menu_panel(g);
            dialog_draw(g, 1);
            break;
        case XEEN_MENU_OPTIONS:
            options_panel(g, 1);
            break;
        default: /* XEEN_MENU_TITLE: the animation alone */
            break;
    }
    return XEEN_OK;
}
/* After a presented frame: the frame wait starts and the menu waits in POLL (at BOUNDARY). */
static xeen_status menu_await(xeen_game *g, int poll, uint32_t boundary)
{
    menu_wait(g);
    return menu_set(g, poll, boundary);
}
/* The title's poll: any key opens the main menu (menu_open); with none after 4 ticks the title's next frame.
 * Original: 168B3..168C6. */
static xeen_status title_poll(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    if ((status = menu_key_or_ticks(g, bios, user)) != XEEN_OK) {
        return status;
    }
    if (!xeen_read16(&g->movement, XEEN_DS_INPUT_CODE)) {
        return menu_set(g, XEEN_MENU_TITLE, MENU_TITLE_BOUNDARY);
    }
    return menu_open(g);
}
/* The main menu's poll: polled once, then the wait (3 ticks or a key); a key is dispatched (menu_dispatch), else
 * the menu's next frame. Original: 16470..16491. */
static xeen_status main_menu_poll(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    (void)menu_poll(g);
    if ((status = menu_ticks_or_key(g, bios, user)) != XEEN_OK) {
        return status;
    }
    if (!xeen_read16(&g->movement, XEEN_DS_INPUT_CODE)) {
        return menu_set(g, XEEN_MENU_FRAME, MENU_FRAME_BOUNDARY);
    }
    return menu_dispatch(g);
}
/* The slot dialog's poll: a key goes to the dialog (dialog_key); with none after 4 ticks its next frame.
 * Original: 2B1C5..2B1EF. */
static xeen_status dialog_poll(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    if ((status = menu_key_or_ticks(g, bios, user)) != XEEN_OK) {
        return status;
    }
    if (!xeen_read16(&g->movement, XEEN_DS_INPUT_CODE)) {
        return menu_set(g, XEEN_MENU_DIALOG, MENU_DIALOG_BOUNDARY);
    }
    return dialog_key(g);
}
/* The "no saved games" scroll's poll: it waits without a clock; any key closes the scroll, clears the key word, pops
 * the button set, restores the UI mode the scroll saved, and the main menu's frame follows. Original: 18F97. */
static xeen_status scroll_poll(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    if (!menu_poll(g)) {
        return XEEN_SERVICE_REQUIRED;
    }
    xeen_window_close(m, XEEN_WINDOW_SCROLL);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    xeen_button_stack(m->ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    m->ds[XEEN_DS_UI_MODE] = g->scroll_mode;
    return menu_set(g, XEEN_MENU_FRAME, MENU_FRAME_BOUNDARY);
}
/* Other Options' poll: polled once; with the Clouds installed the panel is animated (the wait, then its next frame
 * without a key), else it waits for a key without a clock. A key goes to options_key. Original: 16E5E..16E74
 * (animated) or 16E76. */
static xeen_status options_poll(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_movement *m = &g->movement;
    xeen_status status;
    (void)menu_poll(g);
    if (m->ds[XEEN_DS_CLOUDS_INSTALLED]) {
        if ((status = menu_ticks_or_key(g, bios, user)) != XEEN_OK) {
            return status;
        }
        if (!xeen_read16(m, XEEN_DS_INPUT_CODE)) {
            return menu_set(g, XEEN_MENU_OPTIONS, MENU_OPTIONS_BOUNDARY);
        }
    } else if (!xeen_read16(m, XEEN_DS_INPUT_CODE)) {
        return XEEN_SERVICE_REQUIRED;
    }
    return options_key(g);
}
/* A credits page's poll: it waits for a key without a clock (a click is never seen headless), which credits_key
 * answers. Original: 1652D.. (189E3; F600). */
static xeen_status credits_poll(xeen_game *g)
{
    if (!menu_poll(g)) {
        return XEEN_SERVICE_REQUIRED;
    }
    return credits_key(g);
}
/*
 * Runs the title menu's current stage (enum xeen_menu_stage) once. Stage 0 is the menu's entry (the exit's hand-over
 * at MENU_ENTRY_BOUNDARY: menu_begin). A frame stage draws its frame (menu_frame), stops when the host cannot present
 * it (each screen its own stop), then waits in its poll stage (menu_await); a poll stage runs its poll. The stages the
 * other menu units own (the session, the pair poll) are not run here.
 */
xeen_status xeen_game_resume_menu(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    if (!g || !bios || g->phase != XG_STOPPED || !g->presentation) {
        return XEEN_BAD_INPUT;
    }
    if (!g->menu_stage) {
        if (g->lifecycle_boundary != MENU_ENTRY_BOUNDARY || (g->exit_stage != EXIT_AT_MENU && g->exit_stage != EXIT_FINISHED)) {
            return XEEN_BAD_INPUT;
        }
        g->exit_stage = EXIT_FINISHED;
        return menu_begin(g);
    }
    switch (g->menu_stage) {
        case XEEN_MENU_TITLE:
            if ((status = menu_frame(g, XEEN_MENU_TITLE, bios, user)) != XEEN_OK) {
                return status;
            }
            if (exit_present(g) != XEEN_OK) {
                return menu_stop(g, MENU_STOP_TITLE_FRAME);
            }
            return menu_await(g, XEEN_MENU_TITLE_POLL, MENU_TITLE_POLL_BOUNDARY);
        case XEEN_MENU_FRAME:
            if ((status = menu_frame(g, XEEN_MENU_FRAME, bios, user)) != XEEN_OK) {
                return status;
            }
            if (exit_present(g) != XEEN_OK) {
                return menu_stop(g, MENU_STOP_MENU_FRAME);
            }
            return menu_await(g, XEEN_MENU_POLL, MENU_POLL_BOUNDARY);
        case XEEN_MENU_DIALOG:
            if ((status = menu_frame(g, XEEN_MENU_DIALOG, bios, user)) != XEEN_OK) {
                return status;
            }
            if (exit_present(g) != XEEN_OK) {
                return menu_stop(g, MENU_STOP_DIALOG_FRAME);
            }
            return menu_await(g, XEEN_MENU_DIALOG_POLL, MENU_DIALOG_POLL_BOUNDARY);
        case XEEN_MENU_OPTIONS:
            if ((status = menu_frame(g, XEEN_MENU_OPTIONS, bios, user)) != XEEN_OK) {
                return status;
            }
            if (exit_present(g) != XEEN_OK) {
                return menu_stop(g, MENU_STOP_OPTIONS_FRAME);
            }
            return menu_await(g, XEEN_MENU_OPTIONS_POLL, MENU_OPTIONS_POLL_BOUNDARY);
        case XEEN_MENU_TITLE_POLL:
            return title_poll(g, bios, user);
        case XEEN_MENU_POLL:
            return main_menu_poll(g, bios, user);
        case XEEN_MENU_DIALOG_POLL:
            return dialog_poll(g, bios, user);
        case XEEN_MENU_SCROLL_POLL:
            return scroll_poll(g);
        case XEEN_MENU_OPTIONS_POLL:
            return options_poll(g, bios, user);
        case XEEN_MENU_CREDITS_POLL:
        case XEEN_MENU_CREDITS_POLL + 1:
        case XEEN_MENU_CREDITS_POLL + 2:
        case XEEN_MENU_CREDITS_POLL + 3:
            return credits_poll(g);
        default:
            return XEEN_BAD_INPUT;
    }
}
