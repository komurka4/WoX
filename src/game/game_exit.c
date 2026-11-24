/*
 * The exit scenes and the game over (Phase 246, from game.c): the helpers every exit, death and cutscene
 * scene shares (button_defaults, the exit event and music records, exit_draw, exit_present, exit_poll,
 * sprite_load, the saved screen), 15647's book, 17F28's death scene (the laugh, the flames) and 1624F's
 * teardown to 1660E's title menu, driven by xeen_game_resume_exit.
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The original calls of the voice driver (187E9) and the CD status (47F3D) in this unit, each by the call's
 * return address: the audio model records which call asked (traces name them). Not stops, not waits. */
enum {
    DEATH_LAUGH_VOICE_QUERY_CALL = 0x15c7f,/* 15C7A 187E9(1, 0): the laugh runs while its voice plays */
    DEATH_LAUGH_VOICE_CALL = 0x15bd9/* 15BD4 187E9(F42C, 1): the laugh's voice starts after the screen is saved */
};
/*
 * Where native play stops when the game-over book or the party's death scene cannot go on; each value is the
 * original address of the step (tools/xeen_replay/stops.json). "Frame" stops mean the host could not present it.
 */
enum {
    BOOK_STOP_HAND_SPRITES = 0x1569a,        /* one of the book's hand00..15 sprites did not load */
    BOOK_STOP_MARBLE_SPRITES = 0x156d0,      /* one of its marb01..04 sprites did not load */
    BOOK_STOP_FRAME = 0x158e5,
    BOOK_STOP_END_FRAME = 0x15957,
    DEATH_STOP_LOOP_EXIT = 0x17f4b,          /* the exploration loop ended neither by death (1) nor quitting (2) */
    DEATH_STOP_NO_FRAMEBUFFER = 0x1596f,
    DEATH_STOP_SPRITES = 0x159b5,            /* one of the scene's sprite files */
    DEATH_STOP_FIRST_FRAME = 0x15a46,
    DEATH_STOP_FLAMES_FRAME = 0x15b47,
    DEATH_STOP_FLAMES_END_FRAME = 0x15b8c,
    DEATH_STOP_LAUGH_FRAME = 0x15c11,
    DEATH_STOP_LAUGH_STEP_SOUND = 0x15c31,   /* Clouds: the effect at steps 1 and 11 */
    DEATH_STOP_LAUGH_SOUND = 0x15c48,
    DEATH_STOP_LAUGH_END_FRAME = 0x15cac
};
/* ------------------------------------------------- 17F28 game over */
static xeen_status book_end(xeen_game *, int);
/* The screen starts without buttons: the live button set points at the empty tables (1919B). */
void button_defaults(xeen_movement *m)
{
    xeen_button_tables_clear(m->ds, XEEN_DS_SEGMENT);
}
void exit_event(xeen_game *g, const char *name)
{
    if (g->exit_event_count < 12u) {
        /* A bounded copy (the name may be DS text of the same session). */
        char *out = g->exit_events[g->exit_event_count];
        size_t k = 0;
        while (k + 1u < sizeof g->exit_events[0] && name[k]) {
            out[k] = name[k];
            ++k;
        }
        out[k] = 0;
        ++g->exit_event_count;
    }
}
/* The music is turned off: counted as stopped when the music driver is loaded (F430) and music is on, an evidence
 * mark, and the native sink silenced. Original: 187A5(0): 50AB1 when F430 and ED9B are set. */
void exit_music_off(xeen_game *g)
{
    if (xeen_read16(&g->movement, 0xf430) && g->movement.ds[XEEN_DS_MUSIC_ON]) {
        ++g->audio.music_stops;
        g->audio.music_playing = 0;
    }
    exit_event(g, "music off");
    if (g->native_audio_enabled) {
        xeen_native_audio_music(&g->audio, &g->movement, 0);
    }
}
/* 18975: the VOC (or song) is loaded into DOS memory and its segment
 * returned; the silent native backend loads nothing and the word is 0, as
 * a freed handle (18693) leaves it. */
/* The playback sink's copy of a named 18975 load from the selected
 * CC: a song into F42A, otherwise a VOC (each native exit_voc of F42C is
 * followed by its 187E9 start). Observation only; nothing is written. */
void keep_song_copy(xeen_game *g, const uint8_t *b, size_t n)
{
    uint8_t *copy = bytes_copy(b, n);
    if (!copy) {
        return;
    }
    free(g->kept_song);
    g->kept_song = copy;
    g->kept_song_size = n;
    xeen_native_audio_song_buffer(game_audio(g), b, n);
}
void sink_named(xeen_game *g, uint16_t word, const char *name)
{
    xeen_native_audio *a = game_audio(g);
    size_t n = 0;
    uint8_t *b;
    if (word != XEEN_DS_SONG_SEGMENT && (!a || !a->sink)) {
        return;
    }
    if (!(b = menu_extract(g, name, &n))) {
        return;
    }
    if (word == XEEN_DS_SONG_SEGMENT) {
        keep_song_copy(g, b, n);
    } else {
        xeen_native_audio_voice(a, &g->movement, b, n);
    }
    free(b);
}
/**
 * Frees the voice block in DS word WORD (F42C clears the known-song state) and records NAME's
 * event.
 */
static void exit_voc(xeen_game *g, uint16_t word, const char *name)
{
    cd_preload(g);
    if (word == XEEN_DS_FILE_BUFFER_SEGMENT) {
        g->f42c_known = 0; /* A new block; B418 keeps the freed one */
    }
    xeen_write16(&g->movement, word, 0);
    exit_event(g, name);
    sink_named(g, word, name);
    cd_postload(g);
}
/* The laugh's VOC into F42C without playing it (18975 at 15BB9): the original starts it later, at its own 187E9
 * (DEATH_LAUGH_VOICE_CALL), and its talk loop ends when that voice has finished. The bytes are kept for that start
 * (voc_keep), with or without a playback sink, so the voice model times the laugh as the driver would. */
static void exit_voc_kept(xeen_game *g, const char *name)
{
    size_t n = 0;
    uint8_t *b;
    cd_preload(g);
    g->f42c_known = 0; /* A new block; B418 keeps the freed one */
    xeen_write16(&g->movement, XEEN_DS_FILE_BUFFER_SEGMENT, 0);
    exit_event(g, name);
    g->voc_kept = 0;
    if ((b = menu_extract(g, name, &n)) != NULL) {
        voc_keep(g, b, n);
        free(b);
    }
    cd_postload(g);
}
/* Draws sprite FRAME of HANDLE at X, Y with FLAGS (a screen draw, no window). A draw the native raster cannot render
 * (colour mode 8, flag 800h, decoded by 4DAE4) is counted in exit_skipped and skipped: presentation only.
 * Original: 4AC92. */
void exit_draw(xeen_game *g, uint16_t handle, uint16_t frame, int16_t x, int16_t y,
                      uint16_t flags)
{
    xeen_sprite_draw d = {handle, frame, (uint16_t)x, (uint16_t)y, 0, flags, 0};
    if (xeen_presentation_draw_sprites(g->presentation, &d, 1) != XEEN_OK) {
        ++g->exit_skipped;
    }
}
/* Draws a sprite frame for a dialog's buttons in WINDOW (the coordinates stay absolute); a refused draw is counted.
 * Original: a 4AC92 whose last argument names a window (4798C's 1Fh, 3F9C4's 1Ah). */
void exit_draw_in(xeen_game *g, uint16_t handle, uint16_t frame, int16_t x, int16_t y,
                         uint16_t window)
{
    xeen_sprite_draw d = {handle, frame, (uint16_t)x, (uint16_t)y, 0, 0, window};
    if (xeen_presentation_draw_sprites(g->presentation, &d, 1) != XEEN_OK) {
        ++g->exit_skipped;
    }
}
/**
 * Presents the whole screen (50BF6(0)) for an exit, death, menu or cutscene frame and counts the frame. 50BF6 ends
 * every flip on a vertical retrace (4EA85's 4E90A at 4ECBE), so the window holds the frame for one retrace
 * (XEEN_DISPLAY_RETRACE): the clock-free frame loops (the Sphinx's, the gatekeepers' and the Reaper's approaches,
 * the talk's mouth, the death's flames) run at the original's 70 frames a second at most. Display only: no DS, RNG
 * or page state; headless there is no display hook.
 */
xeen_status exit_present(xeen_game *g)
{
    xeen_status status = xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
    ++g->exit_frames;
    ++g->scene_returns;
    if (status == XEEN_OK) {
        xeen_presentation_display(g->presentation, XEEN_DISPLAY_RETRACE); /* 50BF6's 4E90A retrace */
    }
    return status;
}
/* One input poll whose key the caller discards (the death and ending sequences): the shared poll, at the game's BIOS
 * clock for the CD status; returns the key (0 when the poll fails). Original: 189E3 (discarded by 1596F). */
uint16_t exit_poll(xeen_game *g)
{
    xeen_input_poll_result result;
    xeen_sprite_archives archives = sprite_archives(g, NULL);
    g->audio.cd_now = g->timer.clock; /* 18ED0's status */
    if (xeen_input_poll_run(&g->input_poll, &g->movement, &g->input_provider,
                            game_audio(g), &archives, g->presentation,
                            &result) != XEEN_OK) {
        return 0;
    }
    return xeen_read16(&g->movement, XEEN_DS_INPUT_CODE);
}
/** Restores the screen saved before the dream or exit scene. Original: 4E99F(8C90). */
void exit_restore(xeen_game *g)
{
    memcpy(g->presentation->framebuffer, g->dream_screen, XEEN_PRESENT_PIXELS);
}
void exit_save(xeen_game *g)
{
    memcpy(g->dream_screen, g->presentation->framebuffer, XEEN_PRESENT_PIXELS);
}
/* The book's sprites in exit_sprites (book_begin loads them): hand00..hand15, then marb01..marb04 (five frames each).
 * Its animation steps 0..22: the hands 0..13 one per step, then hands 14 and 15 moving together over eight positions
 * in colour mode 8 (exit_draw's flag 800h); the marble one frame per step below step 20. */
enum {
    BOOK_HANDS = 16,          /* hand00..hand15: exit_sprites[0..15] */
    BOOK_MOVING_HAND = 14,    /* hands 14 and 15 move together (hand14_x / hand15_x) */
    BOOK_MARBLE = 16,         /* marb01: exit_sprites[16..19] */
    BOOK_SPRITES = 20,
    BOOK_MARBLE_FRAMES = 5,
    BOOK_MARBLE_STEPS = 20,   /* the marble turns below this step */
    BOOK_STEPS = 22,          /* the animation's other end */
    BOOK_HAND_POSITIONS = 8   /* the moving hands' positions */
};
#define BOOK_COLOUR_MODE_8 0x800u /* exit_draw's flag: colour mode 8 (4DAE4) */
/* The book (Clouds only): the button tables saved, the screen saved, the sixteen hand and four marble sprites
 * loaded; then (argument 1) the animation backwards from step 22.
 * Original: 15647(1, 0) (DS 94 = 0): 190ED/1919B, hand00..15.vga and marb01..04.vga through 14BBA's name in F5E8,
 * SI = 16h. */
xeen_status book_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    char name[16];
    unsigned i;
    xeen_presentation_mouse_off(g->presentation);
    if (m->ds[XEEN_DS_WORLD]) {
        return book_end(g, 0);
    }
    xeen_button_stack(m->ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    exit_save(g);
    for (i = 0; i < BOOK_SPRITES; ++i) {
        if (i < BOOK_HANDS) {
            snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_BOOK_HAND_FRAMES), i);
        } else {
            snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_BOOK_MARBLE_FRAMES), i - (BOOK_MARBLE - 1u)); /* marb01.. */
        }
        ds_put_text(g, XEEN_DS_FILE_NAME, name);
        if (!sprite_load(g, name, &g->exit_sprites[i])) {
            return game_stop(g, i < 16u ? BOOK_STOP_HAND_SPRITES : BOOK_STOP_MARBLE_SPRITES);
        }
    }
    g->exit_stage = EXIT_BOOK;
    g->exit_step = BOOK_STEPS;
    g->exit_di = BOOK_HAND_POSITIONS - 1;
    g->exit_waiting = 0;
    if (g->book_from_inn) { /* 15647(0, 0): forward */
        g->exit_step = 0;
        g->exit_di = 0;
    }
    g->lifecycle_boundary = EXIT_BOOK_BOUNDARY;
    return XEEN_SERVICE_REQUIRED;
}
/* The game's way back to the title menu after the exploration loop has ended (its loop exit kept in F606; exit 1 is the
 * handoff's teardown instead): the voices' and the map texts' memory let go, a voice still playing waited for (the
 * session stops until it ends), the file block let go, the music stopped, newbrigh.m loaded from DARK.CC as the
 * menu's song, the words F644 and F642 cleared (F642 is overloaded: the temple's heal total, the ending's score);
 * then the title-screen main menu.
 * Original: 1624F after 1791F returned F64A to 16169: 18693 (F386, F388, 9C60), 187E9(1, 0), F42C and the 9C6C/9C68
 * copies, 187A5(0), F42A; 15F9C: 191DE(1), F42A = 18975 newbrigh.m, F644 = F642 = 0, 1660E. */
static xeen_status exit_teardown(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_write16(m, 0xf606, xeen_read16(m, XEEN_DS_LOOP_EXIT));
    if (xeen_read16(m, 0xf606) == 1u) {
        return handoff_teardown(g);
    }
    xeen_write16(m, XEEN_DS_MISS_VOICE_SEGMENT, 0);
    xeen_write16(m, XEEN_DS_POW_VOICE_SEGMENT, 0);
    xeen_write16(m, XEEN_DS_MAP_TEXTS_SEGMENT, 0);
    /* 16273 loops on 187E9(1, 0) until the voice has played. */
    if (voice_busy(g, EXIT_VOICE_WAIT_BOUNDARY)) {
        g->exit_stage = EXIT_VOICE_WAIT;
        g->phase = XG_STOPPED;
        g->lifecycle_boundary = EXIT_VOICE_WAIT_BOUNDARY;
        return XEEN_SERVICE_REQUIRED;
    }
    xeen_write16(m, XEEN_DS_FILE_BUFFER_SEGMENT, 0);
    exit_music_off(g);
    xeen_write16(m, XEEN_DS_SONG_SEGMENT, 0);
    xeen_select_archive(m, 1);
    exit_voc(g, XEEN_DS_SONG_SEGMENT, ds_text(g, XEEN_TEXT_STARTUP_SONG));
    xeen_write16(m, 0xf644, 0);
    xeen_write16(m, 0xf642, 0);
    g->exit_stage = EXIT_AT_MENU;
    return original_exit(g, MENU_ENTRY_BOUNDARY);
}
/* The book's end. Opened by the inn: the saved screen back and shown, its sprites freed and the button set popped,
 * then the inn goes on. Otherwise, after the animation (ANIMATED), the closed book (hand00 and marble frame 0) drawn
 * and shown, the sprites freed and the button set popped; then a Load from the panel (the loop's exit 2, no defeat)
 * restarts the game from the panel, and any other exit tears the session down. Stops when a frame cannot be shown.
 * Original: 15915 (argument 1 after the animation, 0 for the inn: the saved screen, 50BF6(0), 4AA3E from hand00,
 * 18FFF); 15647 returns to 17F57. */
static xeen_status book_end(xeen_game *g, int animated)
{
    if (g->book_from_inn) {
        /* 15915 with argument 0 (the inn): the saved screen, 50BF6(0),
         * 4AA3E from hand00, 18FFF. */
        exit_restore(g);
        if (exit_present(g) != XEEN_OK) {
            return game_stop(g, BOOK_STOP_END_FRAME);
        }
        (void)xeen_sprite_free_from(&g->sprites, g->exit_sprites[0]);
        xeen_button_stack(g->movement.ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
        return inn_after_book(g);
    }
    if (animated) {
        exit_draw(g, g->exit_sprites[0], 0, 0, 0, 0);
        exit_draw(g, g->exit_sprites[BOOK_MARBLE], 0, 0, 0, 0);
        if (exit_present(g) != XEEN_OK) {
            return game_stop(g, BOOK_STOP_END_FRAME);
        }
        (void)xeen_sprite_free_from(&g->sprites, g->exit_sprites[0]);
        xeen_button_stack(g->movement.ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    }
    if (!g->movement.ds[XEEN_DS_PARTY_DEFEATED] && xeen_read16(&g->movement, XEEN_DS_LOOP_EXIT) == 2u) {
        return panel_restart(g);
    }
    return exit_teardown(g);
}
/* The two moving hands (sprites 14 and 15) at position POSITION, in colour mode 8. */
static const int8_t book_hand14_x[BOOK_HAND_POSITIONS] = {29, 23, 15, -5, -11, -23, -49, -71};
static const int16_t book_hand15_x[BOOK_HAND_POSITIONS] = {165, 171, 198, 218, 228, 245, 264, 281};
static void book_hands(xeen_game *g, int position)
{
    exit_draw(g, g->exit_sprites[BOOK_MOVING_HAND], 0, book_hand14_x[position], 0, BOOK_COLOUR_MODE_8);
    exit_draw(g, g->exit_sprites[BOOK_MOVING_HAND + 1], 0, book_hand15_x[position], 0, BOOK_COLOUR_MODE_8);
}
/* The book opening forwards (the inn's): hand STEP below 14, then the moving hands at their next position (none
 * past the last); marble STEP / 5, frame STEP % 5, below step 20. Original: 156FA (DI the hands' position). */
static void book_draw_forward(xeen_game *g, int step)
{
    if (step < BOOK_MOVING_HAND) {
        exit_draw(g, g->exit_sprites[step], 0, 0, 0, 0);
    } else if (g->exit_di < BOOK_HAND_POSITIONS) {
        book_hands(g, g->exit_di);
        ++g->exit_di;
    }
    if (step < BOOK_MARBLE_STEPS) {
        exit_draw(g, g->exit_sprites[BOOK_MARBLE + step / BOOK_MARBLE_FRAMES], (uint16_t)(step % BOOK_MARBLE_FRAMES), 0,
                  0, 0);
    }
}
/* The book closing backwards (STEP counting down): hand STEP - 1 for steps 1..14, else the moving hands at their
 * position, which then steps back; the marble lags a step: (STEP - 1) / 5, frame (STEP - 1) % 5, up to step 20
 * (step 0 is never drawn: the animation ends first). Original: 1580C (SI the step; 50D2:0006 / 50D2:000E). */
static void book_draw_backward(xeen_game *g, int step)
{
    if (step <= BOOK_MOVING_HAND && step) {
        exit_draw(g, g->exit_sprites[step - 1], 0, 0, 0, 0);
    } else if (step) {
        book_hands(g, g->exit_di);
        --g->exit_di;
    }
    if (step <= BOOK_MARBLE_STEPS) {
        exit_draw(g, g->exit_sprites[BOOK_MARBLE + (step - 1) / BOOK_MARBLE_FRAMES],
                  (uint16_t)((step - 1) % BOOK_MARBLE_FRAMES), 0, 0, 0);
    }
}
/* One frame of the book per call: the frame mark and the saved screen; backwards (the step counting down), hand
 * step - 1 for steps 1..14, else hands 14 and 15 at their positions in colour mode 8 (steps 15..22), and marble
 * (step - 1) / 5, frame (step - 1) % 5, up to step 20; forwards, hand step below 14, then hands 14 and 15 at their
 * next position, and marble step / 5, frame step % 5, below 20. Then a one-tick wait, the screen shown and the key
 * poll: a key or a click ends the animation.
 * Original: backwards 1580C (SI the step; 50D2:0006 / 50D2:000E), forwards 156FA (DI the hands' position); 4EA0D,
 * 4EA1C, 50BF6(0), 189E3, the click F600. */
static xeen_status book_frame(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    int step = g->exit_step;
    if (!g->exit_waiting) {
        status = frame_base(g, bios, user);
        if (status != XEEN_OK) {
            return status;
        }
        exit_restore(g);
        if (g->book_from_inn) {
            book_draw_forward(g, step);
        } else {
            book_draw_backward(g, step);
        }
        g->exit_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
        g->exit_waiting = 1;
    }
    if ((status = timer_ticks(g, &g->exit_elapsed, bios, user)) != XEEN_OK) {
        return status;
    }
    if (!g->timer_registers.ax) {
        g->exit_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
        return XEEN_SERVICE_REQUIRED;
    }
    g->exit_waiting = 0;
    if (exit_present(g) != XEEN_OK) {
        return game_stop(g, BOOK_STOP_FRAME);
    }
    if (g->book_from_inn) {
        if (exit_poll(g) || xeen_read16(&g->movement, XEEN_DS_MOUSE_CLICK) || ++g->exit_step >= BOOK_STEPS) {
            return book_end(g, 1);
        }
    } else if (exit_poll(g) || xeen_read16(&g->movement, XEEN_DS_MOUSE_CLICK) || --g->exit_step <= 0) {
        return book_end(g, 1);
    }
    return XEEN_SERVICE_REQUIRED;
}
/* The laugh, one step per call (the step in exit_step, from 0): the frame mark, the saved screen, the laughing face's
 * frame step - 1 (none at step 0), the screen shown and the key poll; on Clouds sound 33 at steps 1 and 11 before,
 * and sound 34 at step 15 after, the two-tick wait. The face loops: at step 10 on Clouds (once) and step 9 on Darkside
 * (twice; exit_di counts the restarts, 0 at the laugh's start) the next step is 0 again. The laugh ends at step 23 on
 * Clouds and 10 on Darkside, or as soon as the voice has stopped playing (the step is set to 23: the next, 24, is past
 * both ends). The silent backend's voice never plays, so one pass shows the final picture. Then the saved screen, the
 * palette from the fire, the voice released, the resources reloaded and the book.
 * Original: 15BE3 (SI the step): 4EA0D, death1 frame SI - 1, 50BF6(0), 189E3; FX 21h at SI 1 and 11, 22h at 15;
 * SI = 10 - [94]; 187E9(1, 0) false: SI = 17h; then 4AA3E from fire1, 18693(F42C), 33D36, 15647(1, 0). */
static xeen_status death_laugh(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_movement *m = &g->movement;
    xeen_status status;
    int step = g->exit_step, limit = m->ds[XEEN_DS_WORLD] ? 10 : 23; /* the laugh's end, Darkside : Clouds */
    if (!g->exit_waiting) {
        status = frame_base(g, bios, user);
        if (status != XEEN_OK) {
            return status;
        }
        exit_restore(g);
        if (step) {
            exit_draw(g, g->exit_sprites[5], (uint16_t)(step - 1), 0, 0, 0);
        }
        if (exit_present(g) != XEEN_OK) {
            return game_stop(g, DEATH_STOP_LAUGH_FRAME);
        }
        (void)exit_poll(g);
        if (!m->ds[XEEN_DS_WORLD] && (step == 1 || step == 11) && sound_effect(g, 33u) != XEEN_OK) {
            return game_stop(g, DEATH_STOP_LAUGH_STEP_SOUND);
        }
        g->exit_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
        g->exit_waiting = 1;
    }
    if ((status = timer_ticks(g, &g->exit_elapsed, bios, user)) != XEEN_OK) {
        return status;
    }
    if ((int16_t)g->timer_registers.ax < 2) {
        g->exit_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
        return XEEN_SERVICE_REQUIRED;
    }
    g->exit_waiting = 0;
    if (!m->ds[XEEN_DS_WORLD] && step == 15 && sound_effect(g, 34u) != XEEN_OK) {
        return game_stop(g, DEATH_STOP_LAUGH_SOUND);
    }
    if (10 - (int)m->ds[XEEN_DS_WORLD] == step && (int)m->ds[XEEN_DS_WORLD] + 1 > g->exit_di) {
        step = -1;
        ++g->exit_di;
    }
    if (!voice_busy(g, DEATH_LAUGH_VOICE_QUERY_CALL)) {
        step = 0x17; /* 15C7A 187E9(1,0): the laugh ends with its voice (the voice model) */
    }
    g->exit_step = step + 1;
    if (g->exit_step < limit) {
        return XEEN_SERVICE_REQUIRED;
    }
    exit_restore(g);
    if (exit_present(g) != XEEN_OK) {
        return game_stop(g, DEATH_STOP_LAUGH_END_FRAME);
    }
    (void)xeen_sprite_free_from(&g->sprites, g->exit_sprites[0]);
    xeen_write16(m, XEEN_DS_FILE_BUFFER_SEGMENT, 0);
    /* 33D36 reloads the resident resources 33CD8 unloaded: platform memory. */
    return book_begin(g);
}
/* One frame of the flames per call, for steps 2, 4 .. 34: the saved screen, the fire sprite step / 10 with its
 * frames step % 10 and the next at x 0 and 160, the death picture's strips 0 .. step / 2 - 1 rising to their heights
 * (death_y), the screen shown and a key poll (the key is discarded); no timer wait. Then the death frames 34 and
 * 35, the voice released, the laugh's sample and the screen saved.
 * Original: 15A60 (SI the step, 2..22h): fire[SI / 10] frames SI % 10 and + 1 at x 0 and A0h, the death.vga strips at
 * 50D4:1644's heights, 50BF6(0), 189E3; death frames 22h/23h, 18693(&F42C), the laugh VOC, 186B2. */
static xeen_status death_flames(xeen_game *g)
{
    static const uint8_t death_y[17] = {196, 187, 179, 169, 159, 147, 138, 127, 113,
                                        101, 86,  73,  60,  48,  36,  23,  10};
    xeen_movement *m = &g->movement;
    int step = g->exit_step, k;
    if (step < 36) {
        exit_restore(g);
        exit_draw(g, g->exit_sprites[step / 10], (uint16_t)(step % 10), 0, 0, 0);
        exit_draw(g, g->exit_sprites[step / 10], (uint16_t)(step % 10 + 1), 160, 0, 0);
        for (k = 0; k < step / 2; ++k) {
            exit_draw(g, g->exit_sprites[4], (uint16_t)(2 * k), 0, death_y[k], 0);
            exit_draw(g, g->exit_sprites[4], (uint16_t)(2 * k + 1), 160, death_y[k], 0);
        }
        if (exit_present(g) != XEEN_OK) {
            return game_stop(g, DEATH_STOP_FLAMES_FRAME);
        }
        (void)exit_poll(g);
        g->exit_step += 2;
        return XEEN_SERVICE_REQUIRED;
    }
    exit_draw(g, g->exit_sprites[4], 0x22, 0, 0, 0);
    exit_draw(g, g->exit_sprites[4], 0x23, 160, 0, 0);
    if (exit_present(g) != XEEN_OK) {
        return game_stop(g, DEATH_STOP_FLAMES_END_FRAME);
    }
    xeen_write16(m, XEEN_DS_FILE_BUFFER_SEGMENT, 0);
    exit_voc_kept(g, m->ds[XEEN_DS_WORLD] ? ds_text(g, XEEN_TEXT_LAUGH_VOICE) : ds_text(g, XEEN_TEXT_DEATH_LAUGH_CLOUDS));
    exit_save(g);
    voc_start(g, DEATH_LAUGH_VOICE_CALL); /* 15BD4 187E9(F42C, 1): the laugh's voice */
    g->exit_stage = EXIT_LAUGH;
    g->exit_step = 0;
    g->exit_di = 0;
    g->exit_waiting = 0;
    return XEEN_SERVICE_REQUIRED;
}
/* The exploration loop has ended: the mouse reset. Without a defeat only the loop's exits 2 (the panel's Load) and 1
 * (an ending) go on, to the book's animation after every sprite above the mouse's is freed; any other value stops.
 * After a defeat the death sequence begins: the sprites freed, the music off and no song pending (the original's
 * unload of its resident resources is DOS memory, not modelled), fire.voc into the file block, the screen saved,
 * the fire and death sprites
 * loaded, the first fire frames drawn and the voice played. Stops without a framebuffer or a sprite.
 * Original: 17F28: 4E231 (INT 33h, platform), 4AA3E above F460, 1919B; F64A alone: 17F4B 15647(1,0), 16171; with F60A
 * 1596F: 187A5(0), the driver's pending byte 1658, 1919B, 33CD8 (33D36 reloads), F42C = fire.voc, 186B2(8C90),
 * fire1..4.vga, death.vga, death1.vga. */
static xeen_status death_begin(xeen_game *g)
{
    static const uint16_t names[6] = {XEEN_TEXT_DEATH_FIRE_1, XEEN_TEXT_DEATH_FIRE_2, XEEN_TEXT_DEATH_FIRE_3,
                                      XEEN_TEXT_DEATH_FIRE_4, XEEN_TEXT_DEATH_PICTURE, XEEN_TEXT_DEATH_PICTURE_2};
    xeen_movement *m = &g->movement;
    unsigned i;
    xeen_presentation_mouse_off(g->presentation); /* 17F28 4E231 */
    if (!m->ds[XEEN_DS_PARTY_DEFEATED]) {
        /* F64A alone: 17F4B 15647(1,0), then 16171. 2 (the panel's Load)
         * returns to 16159; 1 (an ending's 33518, AX 1) goes on
         * to 1617E and the XEEN.EXE hand-off; other values stay a stop. */
        if (xeen_read16(m, XEEN_DS_LOOP_EXIT) != 2u && xeen_read16(m, XEEN_DS_LOOP_EXIT) != 1u) {
            return game_stop(g, DEATH_STOP_LOOP_EXIT);
        }
        (void)xeen_sprite_free_from(&g->sprites, (uint16_t)(xeen_read16(m, XEEN_DS_MOUSE_SPRITES) + 1u));
        button_defaults(m);
        g->phase = XG_STOPPED;
        g->command_status = XGC_ORIGINAL_EXIT;
        return book_begin(g);
    }
    if (!g->presentation->framebuffer || g->presentation->framebuffer_size < XEEN_PRESENT_PIXELS) {
        return game_stop(g, DEATH_STOP_NO_FRAMEBUFFER);
    }
    (void)xeen_sprite_free_from(&g->sprites, (uint16_t)(xeen_read16(m, XEEN_DS_MOUSE_SPRITES) + 1u));
    button_defaults(m);
    exit_music_off(g);
    g->input_poll.pending_music = 0;
    button_defaults(m);
    exit_voc(g, XEEN_DS_FILE_BUFFER_SEGMENT, ds_text(g, XEEN_TEXT_DEATH_FIRE_VOICE));
    exit_save(g);
    for (i = 0; i < 6u; ++i) {
        if (!sprite_load(g, ds_text(g, names[i]), &g->exit_sprites[i])) {
            return game_stop(g, DEATH_STOP_SPRITES);
        }
    }
    exit_draw(g, g->exit_sprites[0], 0, 0, 0, 0);
    exit_draw(g, g->exit_sprites[0], 1, 160, 0, 0);
    if (exit_present(g) != XEEN_OK) {
        return game_stop(g, DEATH_STOP_FIRST_FRAME);
    }
    g->exit_stage = EXIT_FLAMES;
    g->exit_step = 2;
    return original_exit(g, EXIT_DEATH_BOUNDARY);
}
xeen_status xeen_game_resume_exit(xeen_game *g, xeen_timer_bios bios, void *user)
{
    if (!g || !g->continuous || g->phase != XG_STOPPED || !g->presentation || !bios) {
        return XEEN_BAD_INPUT;
    }
    if (g->lifecycle_boundary == XEEN_TICK_LOOP_EXIT && g->exit_stage == EXIT_NONE) {
        return death_begin(g);
    }
    if (g->lifecycle_boundary == EXIT_DEATH_BOUNDARY && g->exit_stage == EXIT_FLAMES) {
        return death_flames(g);
    }
    if (g->lifecycle_boundary == EXIT_DEATH_BOUNDARY && g->exit_stage == EXIT_LAUGH) {
        return death_laugh(g, bios, user);
    }
    if (g->lifecycle_boundary == EXIT_BOOK_BOUNDARY && g->exit_stage == EXIT_BOOK) {
        return book_frame(g, bios, user);
    }
    if (g->lifecycle_boundary == EXIT_VOICE_WAIT_BOUNDARY && g->exit_stage == EXIT_VOICE_WAIT) {
        return exit_teardown(g); /* The voice wait */
    }
    if (g->lifecycle_boundary == HANDOFF_VOICE_WAIT_BOUNDARY && g->exit_stage == EXIT_VOICE_WAIT) {
        return handoff_teardown(g);
    }
    return XEEN_BAD_INPUT;
}
