#include "game_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/*
 * Where native play stops when the control panel or its save/load dialog cannot go on; each value is the original
 * address of the step (tools/xeen_replay/stops.json). "Frame" stops mean the host could not present the screen.
 */
enum {
    PANEL_STOP_NO_HOST = 0x297d9,            /* no presentation or no save files */
    PANEL_STOP_SPRITES = 0x29800,            /* cpanel.icn */
    PANEL_STOP_TEXT = 0x29892,               /* the panel's text could not be formed */
    PANEL_STOP_FRAME = 0x298c6,
    PANEL_STOP_SOUND = 0x299b3,              /* the sound toggle's effect */
    PANEL_STOP_CONFIRM = 0x2937a,            /* the confirmation could not be shown */
    PANEL_STOP_SCENE_VIEW_FRAME = 0x29936,
    PANEL_STOP_SCENE_PANEL_FRAME = 0x29940,
    PANEL_STOP_SCENE_STAGE = 0x1c1b3,        /* a scene ended in no panel stage */
    PANEL_STOP_PAIR_COPY = 0x197f5,          /* a loaded game's copy of both worlds' state */
    PANEL_STOP_SAVE_OVER_CONFIRM = 0x2b2d1,  /* "save over ...?" could not be shown */
    PANEL_STOP_DIALOG_VIEW_FRAME = 0x2b1dc,
    PANEL_STOP_DIALOG_PANEL_FRAME = 0x2b1e4,
    WIZARD_STOP_SOUND = 0x1880e              /* Mr Wizard's move: its effect */
};

/* ------------------------------------- 297D9 control panel
 * Tab in exploration (17C30): the panel, Save (29502), Save As (299E8: the
 * 2AD6D(0) Save Game Menu and the 16332 name entry), Load (29A8D: 2AD6D(1), the 1978C copies) and the
 * F64A = 2 restart (17C3F -> 17F28 -> 15647(1,0) -> 16159 43C37 1791F),
 * which ends at the 19E82 map load like a main-menu load. Each panel,
 * slot-dialog and name-entry poll follows the original loop: a 4EA0D /
 * 1B503(0) scene (96C4 = 0: no countdown, no contact) before it. */
/** Stops at control-panel stage STAGE, awaiting input at BOUNDARY. */
xeen_status panel_set(xeen_game *g, int stage, uint32_t boundary)
{
    g->panel_stage = stage;
    g->phase = XG_STOPPED;
    g->lifecycle_boundary = boundary;
    g->command_status = XGC_PENDING_INPUT;
    ++g->panel_sessions;
    return XEEN_SERVICE_REQUIRED;
}
/** Stops the control panel at unsupported BOUNDARY. */
xeen_status panel_stop(xeen_game *g, uint32_t boundary)
{
    g->panel_stop = boundary;
    g->panel_stage = 0;
    return game_stop(g, boundary);
}
/* Ends the control panel's pass at STAGE with a frame mark and a quiet scene pass (argument 0); the frame wait goes on
 * in panel_after_scene. Original: 298D1 / 2B117 / 288D4: 4EA0D, then 1B503(0). */
xeen_status panel_scene(xeen_game *g, int stage)
{
    g->panel_stage = stage;
    return scene_wait(g, 0, XEEN_TIMER_FRAME_MARK);
}
/* Window N's slot in the saved-pixel buffers (the original's at CS:1686, one per window, that 4F2A1 saves into and
 * 4F57B restores from): every window 0..XEEN_WINDOW_SPEECH, else -1. */
static int panel_window_slot(unsigned n)
{
    return n <= XEEN_WINDOW_SPEECH ? (int)n : -1;
}
/* The pixels under a window (presentation only): saved when the window opens, put back when it closes. The original
 * keeps a buffer for every window, so every window 0..40 has a slot here; a window never saved is not restored. (An
 * earlier native gave only 17 windows a slot: the others' close restored nothing; e.g. the blacksmith's Identify
 * window left its last lines over the button labels after Esc, and the items dialog's, the character view's, the
 * monster spells', the map's and Dismiss's windows stayed on screen.)
 * Original: 1871B opens (4F2A1 saves), 18765 closes (4F57B restores), the buffers at CS:1686; the windows 1Dh/1Eh,
 * 0Dh/18h, 13h, 11h, 5 (1DA2D) and 1Fh (2B75D).
 * Saves the pixels under window N (RESTORE clear) or puts them back (RESTORE set). */
void panel_under(xeen_game *g, unsigned n, int restore)
{
    uint16_t r[4];
    unsigned y;
    int slot = panel_window_slot(n);
    xeen_presentation *p = g->presentation;
    if (slot < 0 || !p || !p->framebuffer || p->framebuffer_size < XEEN_PRESENT_PIXELS ||
        !xeen_presentation_window(p, (uint16_t)n, r) || r[0] + r[2] > 320u || r[1] + r[3] > 200u) {
        return;
    }
    if (restore && !g->window_saves[slot].saved) {
        return;
    }
    if (!restore) {
        g->window_saves[slot].saved = 1;
    }
    g->window_saves[slot].shown = (uint8_t)!restore;
    for (y = r[1]; y < (unsigned)r[1] + r[3]; ++y) {
        uint8_t *screen = p->framebuffer + y * 320u + r[0],
                *under = g->window_saves[slot].pixels + y * 320u + r[0];
        if (restore) {
            memcpy(screen, under, r[2]);
        } else {
            memcpy(under, screen, r[2]);
        }
    }
    /* The restore writes the draw page without showing it; the next update's pending-window copy shows it. No window of
     * the table updates itself after its restore: the scroll, the wait box, the message box, the panel, the save and
     * load dialog, the right column (a location's exit, the Cast list's close), the map, the character view, the items
     * dialog, Dismiss, the rounds' window, the item prompt, "On whom?", the monster spells' windows, Info and the
     * details (compared without the update).
     * Original: 4F57B writes CS:1672's page with no 50BF6; the windows 6 (18FDF), 9 (19E82), 15h (29502), 17h
     * (29C30), 19h (2B58E), 0Ah (28056), 5, 18h, 1Dh/1Eh, 1Fh, 2, 0Dh, 10h, 11h, 13h and 1Ch. */
    if (restore) {
        xeen_presentation_restore_rect(p, r[0], r[1], r[2], r[3]); /* Both display pages */
    }
    if (restore) {
        xeen_presentation_trace_extra("restore %u", n); /* Evidence: 4F57B */
    }
}
void window_pop(xeen_game *g, unsigned n)
{
    panel_window(g, n, 0);
    handle_pop(g);
    xeen_button_stack(g->movement.ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
}
void panel_window(xeen_game *g, unsigned n, int open)
{
    xeen_movement *m = &g->movement;
    if (open) {
        if (!m->ds[XEEN_DS_WINDOW_OPEN + n]) {
            panel_under(g, n, 0);
            text_frame_reset(g);
        }
        xeen_window_open(m, n);
    } /* 1871B's 4F2A1 */
    else {
        if (m->ds[XEEN_DS_WINDOW_OPEN + n]) {
            if (g->location_kind || g->quests_trace || g->list_caster_spells || g->map_active ||
                g->panel_trace) {
                xeen_presentation_trace_extra(
                        "lwrestore %u",
                        n); /* Evidence: 18765's 4F57B (the Cast list's too, the map's) */
            }
            panel_under(g, n, 1);
        }
        xeen_window_close(m, n);
    }
}
/* Lists the save files matching the DS text PATTERN through the host's save files into the DS file list: the count
 * cleared, up to ten names (each cut to 12 characters) and then their count. Returns the host's count, which may
 * exceed the ten listed. Shared by the main menu's Load, Start a New Game and the control panel's Save and Load.
 * The program-data extractor counts a call's constant PATTERN as a native read of that text (its TEXT_WRAPPERS
 * entry, which checks that PATTERN still reaches ds_text unchanged).
 * Original: 18E77 (the names at 8C96 + 14n, the count 8C94). */
unsigned panel_list(xeen_game *g, uint16_t pattern)
{
    xeen_movement *m = &g->movement;
    char names[XEEN_FILE_LIST_ENTRIES][13];
    unsigned n, i;
    xeen_write16(m, XEEN_DS_FILE_LIST_COUNT, 0);
    n = g->save_files->list(g->save_files->user, ds_text(g, pattern), names, XEEN_FILE_LIST_ENTRIES);
    for (i = 0; i < n && i < XEEN_FILE_LIST_ENTRIES; ++i) {
        names[i][12] = 0;
        memcpy(m->ds + XEEN_DS_FILE_LIST + XEEN_FILE_LIST_ENTRY_BYTES * i, names[i], strlen(names[i]) + 1u);
    }
    xeen_write16(m, XEEN_DS_FILE_LIST_COUNT, (uint16_t)(n < XEEN_FILE_LIST_ENTRIES ? n : XEEN_FILE_LIST_ENTRIES));
    return n;
}
/** Copies the far pointer at DS POINTER to DS WORD. */
static void panel_far(xeen_movement *m, uint16_t word, uint16_t pointer)
{
    xeen_write16(m, word, xeen_read16(m, pointer));
    xeen_write16(m, (uint16_t)(word + 2u), xeen_read16(m, (uint16_t)(pointer + 2u)));
}
/* The control panel's text: for each switch (sound effects, music, speech, then text) its "on" or "off" and its
 * button's frame (1 on, 0 off), speech off also clearing Text off (the text is shown again); the switches' text in
 * A43F and the panel text (its page) in A827 with the game's name. Returns 0 when a text does not fit.
 * Original: 296DE: the far pointers 56B4 "on" / 56B8 "off" for ED9A, ED9B, F385 (off clears F384) and F384 into
 * F9AA..F9B8, the frames 2B2A/2B36/2B72/2B7E, the 5A88 text; 29882: A827 = 5AE9 with A43F and 96C5. */
static int panel_text(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char out[1024];
    int n;
    if (ds[XEEN_DS_SOUND_ON]) {
        panel_far(m, XEEN_DS_PANEL_SOUND_STATE, XEEN_DS_PANEL_ON_TEXT);
        xeen_write16(m, XEEN_VIEW_LIST_RECORD(XEEN_DS_PANEL_DRAW_LIST, 0u) + XEEN_VIEW_RECORD_FRAME, 1);
    } else {
        panel_far(m, XEEN_DS_PANEL_SOUND_STATE, XEEN_DS_PANEL_OFF_TEXT);
        xeen_write16(m, XEEN_VIEW_LIST_RECORD(XEEN_DS_PANEL_DRAW_LIST, 0u) + XEEN_VIEW_RECORD_FRAME, 0);
    }
    if (ds[XEEN_DS_MUSIC_ON]) {
        panel_far(m, XEEN_DS_PANEL_MUSIC_STATE, XEEN_DS_PANEL_ON_TEXT);
        xeen_write16(m, XEEN_VIEW_LIST_RECORD(XEEN_DS_PANEL_DRAW_LIST, 1u) + XEEN_VIEW_RECORD_FRAME, 1);
    } else {
        panel_far(m, XEEN_DS_PANEL_MUSIC_STATE, XEEN_DS_PANEL_OFF_TEXT);
        xeen_write16(m, XEEN_VIEW_LIST_RECORD(XEEN_DS_PANEL_DRAW_LIST, 1u) + XEEN_VIEW_RECORD_FRAME, 0);
    }
    if (ds[XEEN_DS_SPEECH_ON]) {
        panel_far(m, XEEN_DS_PANEL_SPEECH_STATE, XEEN_DS_PANEL_ON_TEXT);
        xeen_write16(m, XEEN_VIEW_LIST_RECORD(XEEN_DS_PANEL_DRAW_LIST, 7u) + XEEN_VIEW_RECORD_FRAME, 1);
    } else {
        panel_far(m, XEEN_DS_PANEL_SPEECH_STATE, XEEN_DS_PANEL_OFF_TEXT);
        ds[XEEN_DS_TEXT_OFF] = 0;
        xeen_write16(m, XEEN_VIEW_LIST_RECORD(XEEN_DS_PANEL_DRAW_LIST, 7u) + XEEN_VIEW_RECORD_FRAME, 0);
    }
    if (!ds[XEEN_DS_TEXT_OFF]) {
        panel_far(m, XEEN_DS_PANEL_TEXT_STATE, XEEN_DS_PANEL_ON_TEXT);
        xeen_write16(m, XEEN_VIEW_LIST_RECORD(XEEN_DS_PANEL_DRAW_LIST, 6u) + XEEN_VIEW_RECORD_FRAME, 1);
    } else {
        panel_far(m, XEEN_DS_PANEL_TEXT_STATE, XEEN_DS_PANEL_OFF_TEXT);
        xeen_write16(m, XEEN_VIEW_LIST_RECORD(XEEN_DS_PANEL_DRAW_LIST, 6u) + XEEN_VIEW_RECORD_FRAME, 0);
    }
    n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_PANEL_SETTINGS), ds_text(g, xeen_read16(m, XEEN_DS_PANEL_SOUND_STATE)),
                 ds_text(g, xeen_read16(m, XEEN_DS_PANEL_MUSIC_STATE)), ds_text(g, xeen_read16(m, XEEN_DS_PANEL_TEXT_STATE)),
                 ds_text(g, xeen_read16(m, XEEN_DS_PANEL_SPEECH_STATE)));
    if (n < 0 || (size_t)n >= sizeof out || n >= 1000) {
        return 0;
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER_0, out, (size_t)n + 1u);
    n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_PANEL_TITLE), ds_text(g, XEEN_DS_TEXT_BUFFER_0), ds_text(g, XEEN_DS_GAME_NAME));
    if (n < 0 || (size_t)n >= sizeof out || n >= 1000) {
        return 0;
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER, out, (size_t)n + 1u);
    return 1;
}
/* The panel's picture: its window's frame (the first time) or the window cleared (later frames), the nine buttons
 * (each a cpanel.icn frame showing its switch's state, at its place in the window), the panel's text, and the
 * cursor home. Original: 4ADB1(2B28, 17h) (the frame word 2B2A + 12k; E at (135, 56) as the 1BCE mouse table has
 * it); the first frame (2989A) has no "\r" text (the native draws the window's frame there), the loop's (298DE)
 * begins with 4E38C("\r"); both end with 4E38C(DS:5BBC "\v000\t000\x01"). */
static void panel_draw(xeen_game *g, int first)
{
    xeen_movement *m = &g->movement;
    xeen_draw_list list;
    xeen_presentation *p = g->presentation;
    xeen_native_audio_speech_hold(&g->audio, g->movement.ds); /* Phase 286: speech = off holds the panel's Speech off */
    uint16_t r[4];
    if (first) {
        if (p && p->framebuffer && xeen_presentation_window(p, XEEN_WINDOW_CONTROL_PANEL, r)) {
            (void)xeen_text_frame(p->framebuffer, p->framebuffer_size, r);
        }
    } else {
        menu_window_text(g, XEEN_WINDOW_CONTROL_PANEL, WINDOW_TEXT_ONLY, ds_text(g, XEEN_TEXT_PANEL_CLEAR));
    }
    /* The buttons' list, at the window's origin. */
    if (xeen_scene_collect_draw_list(m, XEEN_WINDOW_CONTROL_PANEL, XEEN_DS_PANEL_DRAW_LIST, &list) == XEEN_OK) {
        (void)xeen_presentation_submit(g->presentation, &list);
    }
    menu_window_text(g, XEEN_WINDOW_CONTROL_PANEL, WINDOW_TEXT_ONLY, ds_text(g, XEEN_DS_TEXT_BUFFER));
    menu_window_text(g, XEEN_WINDOW_CONTROL_PANEL, WINDOW_TEXT_ONLY, ds_text(g, XEEN_TEXT_PANEL_HOME));
}
/* Redraws the control panel: its text formed, its icons and texts drawn, its window shown, the input code cleared,
 * then the panel's scene pass and key loop. Original: 2987E: 296DE, the A827 text, 4ADB1, 4E38C, 4E38C(5BBC),
 * 50BF6(17h), 189BC (F5FE = 0), the loop 298D1. */
xeen_status panel_redraw(xeen_game *g)
{
    if (!panel_text(g)) {
        return panel_stop(g, PANEL_STOP_TEXT);
    }
    panel_draw(g, 1);
    if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_CONTROL_PANEL) != XEEN_OK) {
        return panel_stop(g, PANEL_STOP_FRAME);
    }
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
    ++g->scene_returns;
    return panel_scene(g, PANEL_SCENE);
}
/** Opens the control panel's window and draws it. */
static xeen_status panel_open(xeen_game *g)
{
    panel_window(g, XEEN_WINDOW_CONTROL_PANEL, 1);
    return panel_redraw(g);
}
/*
 * The control panel (the exploration command, and combat's): needs the host's presentation and save files; the
 * monsters are stopped while it shows (exploring only: combat calls the panel itself), the cursor selected, the
 * button tables saved, the panel's sprites and buttons loaded, portrait clicks off, its nine icons given the
 * panel's sprites; then it opens.
 * Original: 17C30 + 297D9..29876 (combat's 392C9 calls 297D9 alone).
 */
xeen_status panel_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t h = 0;
    unsigned i;
    if (!g->presentation || !g->save_files) {
        return game_stop(g, PANEL_STOP_NO_HOST);
    }
    xeen_presentation_trace_extra("mark dialog-panel"); /* Evidence: 297D9 */
    xeen_presentation_trace_extra("panelmark %x", (unsigned)xeen_read16(m, XEEN_DS_INPUT_CODE));
    g->panel_trace = 1; /* Evidence: 297D9 */
    if (!g->panel_combat) {
        ds[XEEN_DS_MONSTERS_ENABLED] = 0;
    }
    (void)xeen_presentation_select_cursor(g->presentation, xeen_read16(m, XEEN_DS_MOUSE_SPRITES),
                                          0);
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    g->panel_loaded = g->panel_speech = 0;
    g->panel_stop = 0;
    if (!sprite_load(g, ds_text(g, XEEN_TEXT_PANEL_SPRITES), &h)) {
        return panel_stop(g, PANEL_STOP_SPRITES);
    }
    g->panel_handle = h;
    button_tables(m, XEEN_BUTTONS_CONTROL_PANEL);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, h);
    ds[XEEN_DS_PORTRAIT_CLICKS] = 0;
    memset(g->panel_debug, 0, sizeof g->panel_debug);
    for (i = 0; i < 9u; ++i) {
        xeen_write16(m, (uint16_t)(XEEN_DS_PANEL_DRAW_LIST + XEEN_VIEW_RECORD_BYTES * i), h);
    }
    return panel_open(g);
}
/* The CD side's check, with speech on: the disc the map needs is its world's, except Clouds' for the Darkside maps
 * that play from the Clouds disc (xeen_cd_side); when that differs from the disc in the drive, the original asks for
 * a disc swap. The swap is the CD platform's: both worlds' data are present, so the native never asks; the request
 * is counted, nothing is written. The audio sink is told the drive's side either way. (On a real CD the swap's end
 * also sets the drive's side from the volume label and clears 84E0..84E5 and FC0F.)
 * Original: 19CC3 (callers: the speech toggle's close 29C5B and the map load's 1B0B2); F385 clear: 19CD2 returns;
 * 96E2, but 0 for the maps of CS:0090/009A/00A2 and the compares 35h..38h, 49h..4Dh, 53h..58h, 7Dh..7Fh, 81h (19D26
 * saves 96E2, zeroes it, 19D4A restores it); 19D36 compares its CBW with the drive's disc F61A (XEEN_DS_CD_DISC);
 * 19D3C calls 47F7F (MSCDEX's swap request; the harness substitutes a RETF). */
static void cd_side_check(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, world;
    /* speech = off holds Speech off first, so the check returns as 19CD2 does with F385 clear */
    xeen_native_audio_speech_hold(&g->audio, g->movement.ds);
    world = ds[XEEN_DS_MAP_SIDE];
    if (!ds[XEEN_DS_SPEECH_ON]) {
        return;
    }
    if (world && !xeen_cd_side(world, ds[XEEN_DS_PARTY_MAP])) {
        world = 0;
    }
    if ((uint16_t)(int16_t)(int8_t)world != xeen_read16(m, XEEN_DS_CD_DISC)) {
        ++g->cd_side_requests;
        g->cd_side_world = world;
    }
    xeen_native_audio_cd_side(&g->audio,
                              xeen_cd_side(world, ds[XEEN_DS_PARTY_MAP])); /* The sink's drive */
}
/* After the panel toggled speech (even twice: the toggle only sets the flag), the CD side is checked and the map
 * side's archive selected. Original: 29C55..29C6A ([BP-8], set by P at 29999): 19CC3, 191DE(96E2). */
static void speech_close(xeen_game *g)
{
    cd_side_check(g);
    xeen_select_archive(&g->movement, g->movement.ds[XEEN_DS_MAP_SIDE]);
}
/* The panel's shared end (panel_close, and Mr Wizard's jump): the input cleared, the buttons popped, the debug time
 * line off, portrait clicks back on, the panel no longer running, and a speech toggle's close. Original: 29C41
 * (panel_close past its 4AA3E): 189D7, 18FFF, 97FA = 0, 98D6 = 1; [BP-8]. */
static void panel_release(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    ds[XEEN_DS_PANEL_DEBUG_LINE] = 0;
    ds[XEEN_DS_PORTRAIT_CLICKS] = 1;
    g->panel_stage = 0;
    if (g->panel_speech) {
        speech_close(g);
    }
}
/* The panel closes: its window (unless a dialog closed it already), its sprites freed, the shared end
 * (panel_release). After a Load the exploration loop restarts with the loaded game; from combat the panel returns to
 * combat's command; else the monsters move again and the command's tail follows. Original: 29C30/29C37: 18765(17h),
 * 4AA3E; AX = [BP-6]: 1 (a Load) -> 17C3F F64A = 2, 17F28; else 17C48 96C4 = 1, 17F07. */
static xeen_status panel_close(xeen_game *g, int window_open)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    if (window_open) {
        panel_window(g, XEEN_WINDOW_CONTROL_PANEL, 0);
    }
    (void)xeen_sprite_free_from(&g->sprites, g->panel_handle);
    panel_release(g);
    xeen_presentation_trace_extra("panelend");
    g->panel_trace = 0; /* Evidence: 29C6B */
    if (g->panel_loaded) {
        xeen_write16(m, XEEN_DS_LOOP_EXIT, 2);
        return original_exit(g, XEEN_TICK_LOOP_EXIT);
    }
    if (g->panel_combat) {
        g->panel_combat = 0;
        g->combat_ui_site = 0;
        return combat_modal_return(g, XEEN_COMBAT_MODAL_CONTROL_PANEL, 0);
    }
    ds[XEEN_DS_MONSTERS_ENABLED] = 1;
    return command_tail(g);
}
/* --------------------------------------------- the save and load dialog in the game */
/* Opens the control panel's slot dialog for MODE (0 Save As, 1 Load), titled "Save" or "Load", with the pixels
 * under its window saved; then its scene wait. Stops when the dialog cannot open. Original: 2AD6D in the game
 * (8C77 = 0). */
static xeen_status panel_dialog(xeen_game *g, int mode)
{
    uint32_t stop;
    g->panel_mode = mode;
    g->dialog_title = (uint16_t)(mode ? XEEN_TEXT_DIALOG_LOAD : XEEN_TEXT_DIALOG_SAVE); /* "Load" / "Save" (8C77 = 0) */
    panel_under(g, XEEN_WINDOW_VIEW_DIALOG, 0);
    if ((stop = dialog_open(g)) != 0) {
        return panel_stop(g, stop);
    }
    return panel_scene(g, PANEL_DIALOG_SCENE);
}
/* The panel's slot dialog has ended with SLOT (0 none): after Save As or a cancelled Load the panel reopens; a Load
 * copies both worlds' slot files and leaves the panel to restart the game. Original: 2B58E with AL = the selection;
 * Save As 299E8 (29A7D), the panel 29876; a Load 29ACA (1978C), AX = 1. */
xeen_status panel_dialog_done(xeen_game *g, unsigned slot)
{
    xeen_movement *m = &g->movement;
    if (m->ds[XEEN_DS_WINDOW_OPEN + XEEN_WINDOW_VIEW_DIALOG]) {
        panel_under(g, XEEN_WINDOW_VIEW_DIALOG, 1);
    }
    dialog_end(g);
    if (!slot) {
        return panel_open(g);
    }
    m->ds[XEEN_DS_GAME_SLOT] = (uint8_t)slot;
    if (!g->panel_mode) {
        return panel_open(g);
    }
    g->panel_loaded = 1;
    if (!menu_copy(g)) {
        return panel_stop(g, PANEL_STOP_PAIR_COPY);
    }
    if (m->ds[XEEN_DS_CLOUDS_INSTALLED]) {
        xeen_select_archive(m, !m->ds[XEEN_DS_WORLD]);
        if (!menu_copy(g)) {
            return panel_stop(g, PANEL_STOP_PAIR_COPY);
        }
        xeen_select_archive(m, !m->ds[XEEN_DS_WORLD]);
    }
    addons_slot_load(g, (int)(int8_t)m->ds[XEEN_DS_GAME_SLOT]); /* add-on identify */
    return panel_close(g, 0);
}
/* A key in the control panel's save or load dialog: a digit selects or deselects a slot; Enter or S on a selection
 * loads a named slot (Load) or, for Save, asks "Ok to save over %s?" for a named slot and then the name; Escape
 * cancels; any other key polls again. Original: 2AD6D's game mode (2B1F9; C0CC = 0 for a Load; 2937A). */
static xeen_status panel_dialog_key(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint16_t key = xeen_read16(m, XEEN_DS_INPUT_CODE);
    unsigned slot = g->menu_selected;
    char out[64];
    if (key == '\r' || key == 'S') {
        if (!slot) {
            return panel_scene(g, PANEL_DIALOG_SCENE);
        }
        if (g->panel_mode) {
            if (!g->save_slots[slot - 1u].header.name[0]) {
                return panel_scene(g, PANEL_DIALOG_SCENE);
            }
            xeen_write16(m, XEEN_DS_MAP_FIELD(ID), 0);
            return panel_dialog_done(g, slot);
        }
        if (g->save_slots[slot - 1u].header.name[0]) {
            (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_SAVE_OVER),
                           (const char *)g->save_slots[slot - 1u].header.name);
            ds_put_text(g, XEEN_DS_TEXT_BUFFER, out);
            g->panel_stage = PANEL_CONFIRM;
            return open_confirm(g, CONFIRM_SAVE_OVER, XEEN_DS_TEXT_BUFFER) ? XEEN_SERVICE_REQUIRED : panel_stop(g, PANEL_STOP_SAVE_OVER_CONFIRM);
        }
        return name_begin(g);
    }
    if (key == XEEN_KEY_ESCAPE) {
        g->menu_selected = 0;
        return panel_dialog_done(g, 0);
    }
    if (key >= '0' && key <= '9') {
        dialog_select(g, key);
        if (!dialog_text(g)) {
            return panel_stop(g, DIALOG_STOP_TEXT);
        }
        dialog_draw(g, 0);
        if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW_DIALOG) != XEEN_OK) {
            return panel_stop(g, DIALOG_STOP_FRAME);
        }
        ++g->scene_returns;
    }
    return panel_scene(g, PANEL_DIALOG_SCENE);
}
/*
 * Mr Wizard's move (the panel's W outside combat, or the combat cleanup after it was chosen in a fight): the sound;
 * the map side from the installation; map 28 with both games installed (Clouds' Vertigo, from either world; else map
 * 29) loaded through map_reload (across the worlds as the pyramid's); the party faces east (south without Clouds);
 * the party map, and the new map's run position. The reference installation has both games: the other branch is not
 * exercised. SITE is the map load's call (the stop on a failed load). Returns 0, or the stopping address.
 * Original: 29B87..29BC5 (without window 2) and 39580..395BE (after 392D3's DI = 1): 1880E(33h); 96E2 = !95;
 * 19E82(1Ch with DS 95 set, else 1Dh); ED96 = 1 (2 without DS 95); 2125F; C0FB, C104.
 */
uint32_t wizard_move(xeen_game *g, uint32_t site)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, both = ds[XEEN_DS_CLOUDS_INSTALLED];
    if (sound_effect(g, XEEN_CUE_TRANSPORTED) != XEEN_OK) {
        return WIZARD_STOP_SOUND;
    }
    ds[XEEN_DS_MAP_SIDE] = (uint8_t)!both;
    if (map_reload(g, both ? 28u : 29u) != XR_OK) {
        return site;
    }
    m = &g->movement;
    ds = m->ds;
    ds[XEEN_DS_PARTY_DIRECTION] = both ? 1u : 2u;
    ds[XEEN_DS_PARTY_X] = ds[XEEN_DS_MAP_FIELD(RUN_X)];
    ds[XEEN_DS_PARTY_Y] = ds[XEEN_DS_MAP_FIELD(RUN_Y)];
    xeen_navigation_set(m, (xeen_navigation){ds[XEEN_DS_PARTY_DIRECTION], ds[XEEN_DS_PARTY_X],
                                             ds[XEEN_DS_PARTY_Y], ds[XEEN_DS_PARTY_MAP]});
    return 0;
}
/* Mr Wizard confirmed: the panel's window closed; outside combat the party moves to the map's start (wizard_move);
 * then the party's gems are taken (all of them) and the panel released without freeing its sprites (the original
 * jumps past that free). From combat, combat's control-panel return with answer 2 (its cleanup); else the monsters
 * move again and the command's tail follows. Stops when the move fails.
 * Original: 29B76: 18765(17h); without window 2 the map move; F010 = F00E = 0, AX 2 to 29C41 past 29C38's 4AA3E;
 * 189D7, 18FFF, 97FA = 0, 98D6 = 1, the [BP-8] speech test; exploration 17C3A (96C4 = 1, 17F07); combat 392CE (DI =
 * 1, 39430, 3957C). */
static xeen_status panel_wizard(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds;
    uint32_t stop;
    panel_window(g, XEEN_WINDOW_CONTROL_PANEL, 0);
    if (!m->ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_COMBAT)] && (stop = wizard_move(g, WIZARD_PANEL_LOAD_SITE)) != 0) {
        return panel_stop(g, stop);
    }
    m = &g->movement;
    ds = m->ds;
    xeen_ds_write32(m, XEEN_DS_PARTY_GEMS, 0);
    panel_release(g);
    if (g->panel_combat) {
        g->panel_combat = 0;
        g->combat_ui_site = 0;
        return combat_modal_return(g, XEEN_COMBAT_MODAL_CONTROL_PANEL, 2);
    }
    ds[XEEN_DS_MONSTERS_ENABLED] = 1;
    return command_tail(g);
}
/* A yes/no answered for a control-panel or menu owner: Save over goes on to the name (yes) or back to its dialog;
 * the new game's save over likewise, back to the menu's dialog; the inn's Delete, a Buy, the guild and the save pair
 * get their answers; Quit's yes exits the program at once (the yes/no closed its window; the panel's window, speech
 * and debug line are left as they are, from exploration's or combat's panel alike); Mr Wizard's yes moves the party
 * (panel_wizard); any other no puts the debug line back and returns to the panel.
 * Original: 2937A's callers: Save over 2B2D7, Quit 29B46 (yes: 29B4D's 2C80D, no 18765(17h), speech close or 97FA
 * restore), Mr Wizard 29B72, the inn 442BE, Buy 3C2E5, the guild 3F604, the pair 184DE; no: 97FA restored. */
xeen_status panel_confirmed(xeen_game *g, int owner, int answer)
{
    if (owner == CONFIRM_SAVE_OVER) {
        return answer ? name_begin(g) : panel_scene(g, PANEL_DIALOG_SCENE);
    }
    if (owner == CONFIRM_NEWGAME_SAVE_OVER) {
        return answer ? name_begin(g) : menu_set(g, XEEN_MENU_DIALOG, MENU_DIALOG_BOUNDARY); /* the menu's Start */
    }
    if (owner == CONFIRM_INN_DELETE) {
        return inn_delete_answer(g, answer); /* The inn's Delete (442BE) */
    }
    if (owner == CONFIRM_BUY) {
        return option_bought(g, answer); /* Buy (3C2E5) */
    }
    if (owner == CONFIRM_GUILD) {
        return guild_confirmed(g, answer); /* The guild's 3F604 */
    }
    if (owner == CONFIRM_PAIR) {
        return pair_answer(g, answer); /* 184DE's Delete / Exit to DOS */
    }
    if (answer && owner == CONFIRM_QUIT) {
        g->panel_stage = 0;
        g->panel_combat = 0;
        g->combat_ui_site = 0;
        return program_exit(g);
    }
    if (answer) {
        return panel_wizard(g);
    }
    g->movement.ds[XEEN_DS_PANEL_DEBUG_LINE] = g->panel_debug_line;
    return panel_scene(g, PANEL_SCENE);
}
/* The panel's M: music on restarts the song (Original: 1658 = 0, 187A5(F42A)); music off stops the driver directly,
 * with the music flag already clear (exit_music_off would test the flag and do nothing; Original: 299DE's 50AB1(0);
 * 187A5 tests ED9B). */
static void panel_toggle_music(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    ds[XEEN_DS_MUSIC_ON] ^= 1u;
    if (ds[XEEN_DS_MUSIC_ON]) {
        g->input_poll.pending_music = 0;
        exit_event(g, "music F42A");
        if (g->native_audio_enabled) {
            xeen_native_audio_start_buffer(&g->audio, &g->movement);
        }
    } else {
        ++g->audio.music_stops;
        g->audio.music_playing = 0;
        exit_event(g, "music off");
        if (g->native_audio_enabled) {
            xeen_native_audio_music_driver(&g->audio, 0);
        }
    }
}
/* The hidden debug line's keys G, O, B, R, typed in that order (panel_debug: G, O and B seen). G always starts the
 * sequence; O and B continue it only after their predecessors (out of order they close the panel: 0); R turns the
 * debug line on after the whole G-O-B and is always taken. 1: the key is taken (the panel's scene follows). */
static int panel_debug_key(xeen_game *g, uint16_t key)
{
    int *got_g = &g->panel_debug[0], *got_o = &g->panel_debug[1], *got_b = &g->panel_debug[2];
    switch (key) {
        case 'G':
            *got_g = 1;
            return 1;
        case 'O':
            return *got_g ? (*got_o = 1) : 0;
        case 'B':
            return *got_g && *got_o ? (*got_b = 1) : 0;
        default: /* 'R' */
            if (*got_g && *got_o && *got_b) {
                g->movement.ds[XEEN_DS_PANEL_DEBUG_LINE] = 1;
            }
            return 1;
    }
}
/* The control panel's keys: T text, P speech, E sound effects, M music, A and L the save lists, S save, Q quit, W Mr
 * Wizard, and G-O-B-R in order the debug line; any other key closes the panel. Original: 2994B..29C26 (the table
 * CS:1B42, indexed by F5FE - 41h). */
static xeen_status panel_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    switch (key) {
        case 'T':
            ds[XEEN_DS_TEXT_OFF] ^= 1u;
            return panel_redraw(g);
        case 'P':
            ds[XEEN_DS_SPEECH_ON] ^= 1u;
            g->panel_speech = 1;
            /* Phase 286: with speech = off the toggle is undone (the panel keeps showing Speech off). */
            xeen_native_audio_speech_hold(&g->audio, ds);
            return panel_redraw(g);
        case 'E':
            ds[XEEN_DS_SOUND_ON] ^= 1u;
            if (sound_effect(g, ds[XEEN_DS_SOUND_ON] ? 20u : 0u) != XEEN_OK) {
                return panel_stop(g, PANEL_STOP_SOUND);
            }
            return panel_redraw(g);
        case 'M':
            panel_toggle_music(g);
            return panel_redraw(g);
        case 'A':
            /* The original compares the state files' lengths with the disk's free space; the host's save directory
             * always has room. Original: 299E8 (dark.cur, xeen.cur against 419B3), 29A50. */
            (void)panel_list(g, XEEN_TEXT_PANEL_DARK_SAVES);
            panel_window(g, XEEN_WINDOW_CONTROL_PANEL, 0);
            return panel_dialog(g, 0);
        case 'L':
            /* In combat: "No Loading Allowed in Combat!", then the panel's loop. Original: 29A8D (9A1C 2),
             * 18F2F(DS:5C2D, 0), 29C26 -> 298D1. */
            if (ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_COMBAT) {
                return scroll0_open(g, MESSAGE_PANEL_LOAD, ds_text(g, XEEN_TEXT_PANEL_NO_LOADING));
            }
            (void)panel_list(g, XEEN_TEXT_PANEL_CLOUDS_SAVES);
            panel_window(g, XEEN_WINDOW_CONTROL_PANEL, 0);
            return panel_dialog(g, 1);
        case 'S':
            /* In combat (its rounds' window open): "No Saving Allowed in Combat!", then the panel's redraw. Original:
             * 29B0B, 18F2F(DS:5C56, 0), 2987E. */
            if (ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_COMBAT)]) {
                return scroll0_open(g, MESSAGE_PANEL_SAVE, ds_text(g, XEEN_TEXT_PANEL_NO_SAVING));
            }
            return panel_save(g, SAVE_BACK_TO_PANEL);
        case 'Q':
        case 'W':
            g->panel_debug_line = ds[XEEN_DS_PANEL_DEBUG_LINE];
            ds[XEEN_DS_PANEL_DEBUG_LINE] = 0;
            g->panel_stage = PANEL_CONFIRM;
            return open_confirm(g, key == 'Q' ? CONFIRM_QUIT : CONFIRM_WIZARD, key == 'Q' ? XEEN_TEXT_PANEL_QUIT_CONFIRM : XEEN_TEXT_PANEL_WIZARD_CONFIRM)
                           ? XEEN_SERVICE_REQUIRED
                           : panel_stop(g, PANEL_STOP_CONFIRM);
        case 'G':
        case 'O':
        case 'B':
        case 'R':
            if (panel_debug_key(g, key)) {
                return panel_scene(g, PANEL_SCENE);
            }
            break;
        default:
            break;
    }
    /* any other key closes the panel (Original: 29C17) */
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0x1b);
    g->panel_debug[0] = g->panel_debug[1] = g->panel_debug[2] = 0;
    return panel_close(g, 1);
}
/* The frame wait after a scene pass the control panel's stage owns (its own, its save/load dialog's, the name
 * entry's), and its poll and saved stages. */
xeen_status panel_after_scene(xeen_game *g)
{
    switch (g->panel_stage) {
        case PANEL_SCENE:
            /* After the panel's scene: its icons and texts again, the window's border, the debug line when it is on,
             * then the view (the scene drew without showing it) and the panel shown; then the key poll.
             * Original: 298DE..29945: "\r", 4ADB1, the texts, 4F6F8(17h), 2991E's line (29927 19C65, 2992E
             * 4E38C(.., 17h)), 50BF6(3), 50BF6(17h). */
            panel_draw(g, 0);
            {
                uint16_t r[4];
                xeen_presentation *p = g->presentation;
                if (p && p->framebuffer && xeen_presentation_window(p, XEEN_WINDOW_CONTROL_PANEL, r)) {
                    (void)xeen_text_border(p->framebuffer, p->framebuffer_size, r);
                }
                xeen_presentation_trace("frame %u", (unsigned)XEEN_WINDOW_CONTROL_PANEL);
            }
            if (g->movement.ds[XEEN_DS_PANEL_DEBUG_LINE]) { /* the debug time line */
                uint32_t stop = debug_time_line(g);
                if (stop) {
                    return panel_stop(g, stop);
                }
                menu_window_text(g, XEEN_WINDOW_CONTROL_PANEL, WINDOW_TEXT_ONLY, ds_text(g, XEEN_DS_TEXT_BUFFER));
            }
            if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW_FRAME) != XEEN_OK) {
                return panel_stop(g, PANEL_STOP_SCENE_VIEW_FRAME);
            }
            if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_CONTROL_PANEL) != XEEN_OK) {
                return panel_stop(g, PANEL_STOP_SCENE_PANEL_FRAME);
            }
            return panel_set(g, PANEL_POLL, PANEL_POLL_BOUNDARY);
        case PANEL_DIALOG_SCENE:
            dialog_draw(g, 1);
            /* 2B1DA, the game's dialog (8C77 = 0): 50BF6(3), then 19h */
            if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW_FRAME) != XEEN_OK) {
                return panel_stop(g, PANEL_STOP_DIALOG_VIEW_FRAME);
            }
            if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW_DIALOG) != XEEN_OK) {
                return panel_stop(g, PANEL_STOP_DIALOG_PANEL_FRAME);
            }
            return panel_set(g, PANEL_DIALOG_POLL, PANEL_DIALOG_POLL_BOUNDARY);
        case PANEL_NAME_SCENE:
            input_cursor_step(g);
            g->dialog_loop_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
            return panel_set(g, PANEL_NAME_TICK, PANEL_NAME_TICK_BOUNDARY);
        default:
            /* The other owners' stages are ranges above the panel's own, tested from the highest base down
             * (COMBAT_UI_BASE > CUT_BASE > CAST_SCENE, as xeen_game_resume_panel tests them). */
            return g->panel_stage >= COMBAT_UI_BASE ? combat_ui_after_scene(g)
                   : g->panel_stage >= CUT_BASE     ? cut_after_scene(g)
                   : g->panel_stage >= CAST_SCENE   ? cast_after_scene(g)
                                                    : panel_stop(g, PANEL_STOP_SCENE_STAGE);
    }
}
/** Polls a key for the control panel (counted). */
uint16_t panel_poll(xeen_game *g)
{
    ++g->panel_polls;
    return exit_poll(g);
}
xeen_status xeen_game_resume_panel(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_movement *m;
    uint16_t key;
    if (!g || !bios || g->phase != XG_STOPPED || !g->presentation || !g->panel_stage) {
        return XEEN_BAD_INPUT;
    }
    m = &g->movement;
    if (g->panel_stage >= COMBAT_UI_BASE) {
        return combat_ui_resume(g, bios, user);
    }
    if (g->panel_stage >= CUT_BASE) {
        return cut_resume(g, bios, user);
    }
    switch (g->panel_stage) {
        case PANEL_POLL: /* 29946 */
            key = panel_poll(g);
            if (!key && !xeen_read16(m, XEEN_DS_MOUSE_CLICK)) {
                return panel_scene(g, PANEL_SCENE);
            }
            return panel_key(g, key);
        case PANEL_SAVED: /* 296B9 */
            if (!panel_poll(g) && !xeen_read16(m, XEEN_DS_MOUSE_CLICK)) {
                return XEEN_SERVICE_REQUIRED;
            }
            panel_window(g, XEEN_WINDOW_MESSAGE_BOX, 0);
            xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
            if (g->panel_return == SAVE_BACK_TO_PANEL) {
                return panel_redraw(g);
            }
            return panel_dialog_done(g, g->menu_selected);
        case PANEL_DIALOG_POLL:                            /* 2B1EA */
            if (!panel_poll(g)) {
                return panel_scene(g, PANEL_DIALOG_SCENE);
            }
            return panel_dialog_key(g);
        case PANEL_NAME_POLL: /* 288BF */
            return name_entry_poll(g, bios, user);
        case PANEL_NAME_TICK: /* 2890C..2891F */
            return name_entry_tick(g, bios, user);
        case PANEL_PREFERENCE: /* 2B44D..2B46B */
            key = panel_poll(g);
            if (!key) {
                return XEEN_SERVICE_REQUIRED;
            }
            if (key != XEEN_KEY_ESCAPE && key != 'A' && key != 'W') {
                return panel_set(g, PANEL_PREFERENCE, PANEL_PREFERENCE_BOUNDARY);
            }
            return newgame_preferred(g, key);
        default:
            return location_resume(g, bios, user); /* Stages */
    }
}
/* A game loaded from inside the game (the character book answered with the loop exit 2): F606 takes the loop exit,
 * the panel and exit stages are cleared, and the outer loop restarts the world as after a main-menu load
 * (outer_restart). Original: 16159 after 15647(1, 0) returned F64A = 2 (1616E: F606 = 2); 191DE(96E2), 43C37,
 * 1791F, the 19E82 map load. */
xeen_status panel_restart(xeen_game *g)
{
    xeen_write16(&g->movement, 0xf606, xeen_read16(&g->movement, XEEN_DS_LOOP_EXIT));
    g->panel_stage = 0;
    g->exit_stage = EXIT_NONE;
    return outer_restart(g);
}
