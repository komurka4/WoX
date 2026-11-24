/*
 * The location cutscenes' frame loop (Phases 77-108; split by encounter in Phase 248): the services every
 * encounter's loop uses (the clock reads and polls with their return sites, the frames, the subtitles, the voice,
 * the stage and the stop, the 33D36 end), the poll test and cut_resume, which selects the encounter by its stage
 * range. The encounters are game_cut_reaper.c, game_cut_gate.c, game_cut_sphinx.c and game_cut_guard.c; their
 * private interface is game_cutscene.h.
 */
#include "game_cutscene.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* the closing scene, which the arrival tails reach (defined after them) */
static xeen_status cut_to_scene(xeen_game *g);
/* Where native play stops when a cutscene's shared steps cannot go on; each value is the original address of the
 * step (tools/xeen_replay/stops.json). */
enum {
    CUT_STOP_SUBTITLE_EMPTY = 0x1860c,       /* an empty subtitle: the original divides by its length (IDIV by 0) */
    CUT_STOP_BACKGROUND = 0x3606b,           /* back.raw */
    CUT_STOP_SCENE_STAGE = 0x1c1b3,          /* a scene ended outside the cutscene's scene stage */
    CUT_STOP_RESUME_STAGE = 0x34faa,          /* a resume in no cutscene's stage */
    /* variable stop sites (stops_variable.json) */
    CUT_STOP_GATE_MAP_RELOAD = 0x3604f,      /* the gate cutscene's destination map did not load */
    CUT_STOP_REAPER_MAP_RELOAD = 0x35921     /* the Reaper cutscene's destination map did not load */
};

/* ------------------------ the location cutscenes' shared loop
 * Location kind 8 (the Reaper) and kinds 10 and 13 (the town gatekeepers) run after the locations' shared setup
 * (location_begin). Each is a frame loop over its own sprites: every step ends at one original clock read (the frame,
 * scene or subtitle timer) or at one key poll whose key is discarded, and every read and poll is counted with its
 * return site (what the replay's clocks and keys answer). The talk frames draw the session's random numbers. A talk
 * lasts while the voice plays or the subtitle still runs (cut_talking). With the voice model on, the voice is the
 * audio model's (voice_busy); without it no voice is ever reported playing, as with the original's silent driver,
 * and a talk ends as soon as the subtitle is done. The subtitle is skipped while sound effects are on and the sound
 * card setting is 2 or above; otherwise it scrolls the special.bin line one character per subtitle tick above 1.
 * The end restores the screen, sets the song volume 5Fh, reloads the resident data, loads the destination's map (or
 * the current one), places the party, redraws the scene and closes the windows; the location returns 0.
 * Original: 34FAA (kind 8) and 35992 (kinds 10, 13) after 277E5's setup; the clock reads 4EA1C, 4EA44 and 185E3's
 * 4EA6C; the poll 189E3; the random numbers 504DA; the voice 18975 / 187E9 (187E9(1, 0) the query); 185E3 skipped
 * while ED9A is set and 8D38 >= 2, its position F9A4; the end 4AA3E, 187CB(5Fh), 33D36 (the DOS memory reloads),
 * 19E82, the position, 1B503(0), 50BF6(0), 4E06B(F460, 0), 18FFF and 27D66's 18FFF; 277E5 returns AX 0. */
/* Sets the song volume to LEVEL for the playback sink (only with native audio), after its "music NN" evidence mark
 * (every caller marked it; Phase 250 folded the mark in). Original: 187CB(level). */
void cut_volume(xeen_game *g, unsigned level)
{
    char event[12];
    (void)snprintf(event, sizeof event, "music %02x", level); /* the evidence mark ("music 30", "music 5f") */
    exit_event(g, event);
    if (g->native_audio_enabled) {
        xeen_native_audio_volume(&g->audio, level);
    }
}
/* Loads the voice NAME for a cutscene into its slot SLOT (0..2, the original's stack locals BP-0C, BP-0A, BP-8),
 * replacing the slot's earlier voice; a missing file leaves the slot empty. A later stage starts it.
 * Original: 18975 into the local; 187E9(slot, 0) starts it. */
void cut_voc_load(xeen_game *g, unsigned slot, const char *name)
{
    location_voc_load(g, name);
    free(g->cut_voc[slot].bytes);
    g->cut_voc[slot] = (xeen_owned_bytes){0};
    if (g->voc_kept && (g->cut_voc[slot].bytes = bytes_copy(g->voc_bytes, g->voc_size))) {
        g->cut_voc[slot].size = g->voc_size;
    }
}
/** Starts the cutscene's voice in SLOT at SITE, when it is loaded. */
void cut_voc_start(xeen_game *g, unsigned slot, uint32_t site)
{
    if (!g->cut_voc[slot].bytes) {
        return;
    }
    voice_clock(g);
    xeen_native_audio_voice_at(game_audio(g), &g->movement,
                               g->cut_voc[slot].bytes, g->cut_voc[slot].size, site);
}
int cut_poll_next(const xeen_game *g)
{
    switch (g->cut_stage) {
        case CUT_R_APPROACH_POLL:
        case CUT_R_SLIDE_POLL:
        case CUT_R_CSLIDE_POLL:
        case CUT_R_TALK_POLL:
        case CUT_R_TALK2_POLL:
        case CUT_R_RETREAT_POLL:
        case CUT_G_APPROACH_POLL:
        case CUT_G_WALK_POLL:
        case CUT_G_TALK_POLL:
        case CUT_S_APPROACH_POLL:
        case CUT_S_TALK_POLL:
        case CUT_S_RETREAT_POLL:
        case CUT_K_APPROACH_POLL:
        case CUT_K_SLIDE_POLL:
        case CUT_K_TALK_POLL:
        case CUT_K_TALK2_POLL:
        case CUT_K_RETREAT_POLL:
            return 1;
        default:
            return 0;
    }
}
/* One step ahead: the stage and its boundary (the call's return site). */
static xeen_status cut_set(xeen_game *g, int stage, uint32_t boundary)
{
    g->cut_stage = g->panel_stage = stage;
    return service_wait(g, boundary);
}
/*
 * The original wait site of each cutscene stage: the call whose return the stage resumes at, which the game reports
 * as its lifecycle boundary while the stage waits (traces and the original-byte harness name it). Every stage has
 * exactly one site (Phase 261: 59 stages, 83 entries). A stage not listed here is never entered by cut_step.
 */
static const uint32_t stage_site[CUT_UNLOAD - CUT_BASE + 1] = {
    [CUT_R_APPROACH - CUT_BASE] = 0x35272,
    [CUT_R_APPROACH_TICK - CUT_BASE] = 0x353b5,
    [CUT_R_APPROACH_POLL - CUT_BASE] = 0x353bf,
    [CUT_R_SLIDE - CUT_BASE] = 0x353d4,
    [CUT_R_SLIDE_TICK - CUT_BASE] = 0x35464,
    [CUT_R_SLIDE_POLL - CUT_BASE] = 0x3546e,
    [CUT_R_CSLIDE - CUT_BASE] = 0x3547f,
    [CUT_R_CSLIDE_POLL - CUT_BASE] = 0x354d1,
    [CUT_R_TALK - CUT_BASE] = 0x35545,
    [CUT_R_TALK_SUB - CUT_BASE] = CUT_SUBTITLE_SITE,
    [CUT_R_TALK_POLL - CUT_BASE] = 0x355d4,
    [CUT_R_TALK_TICK - CUT_BASE] = 0x355d9,
    [CUT_R_PAUSE_TICK - CUT_BASE] = 0x35643,
    [CUT_R_TALK2 - CUT_BASE] = 0x356ab,
    [CUT_R_TALK2_POLL - CUT_BASE] = 0x35735,
    [CUT_R_TALK2_TICK - CUT_BASE] = 0x3573a,
    [CUT_R_RETREAT - CUT_BASE] = 0x35797,
    [CUT_R_RETREAT_POLL - CUT_BASE] = 0x358e1,
    [CUT_G_APPROACH - CUT_BASE] = 0x35c2b,
    [CUT_G_APPROACH_POLL - CUT_BASE] = 0x35d96,
    [CUT_G_WALK - CUT_BASE] = 0x35dae,
    [CUT_G_WALK_POLL - CUT_BASE] = 0x35e35,
    [CUT_G_STEP - CUT_BASE] = 0x35e73,
    [CUT_G_STEP_SUB - CUT_BASE] = CUT_SUBTITLE_SITE,
    [CUT_G_STEP_TICK - CUT_BASE] = 0x35ec7,
    [CUT_G_TALK - CUT_BASE] = 0x35f41,
    [CUT_G_TALK_SUB - CUT_BASE] = CUT_SUBTITLE_SITE,
    [CUT_G_TALK_POLL - CUT_BASE] = 0x35f9a,
    [CUT_G_TALK_TICK - CUT_BASE] = 0x35f9f,
    [CUT_G_TALK_WAIT - CUT_BASE] = 0x35fd3,
    [CUT_SCENE - CUT_BASE] = XEEN_TIMER_SCENE_MARK,
    [CUT_S_APPROACH - CUT_BASE] = 0x34100,
    [CUT_S_APPROACH_POLL - CUT_BASE] = 0x34173,
    [CUT_S_STEP - CUT_BASE] = 0x34185,
    [CUT_S_TALK - CUT_BASE] = 0x341f3,
    [CUT_S_TALK_SUB - CUT_BASE] = CUT_SUBTITLE_SITE,
    [CUT_S_TALK_POLL - CUT_BASE] = 0x34256,
    [CUT_S_RETREAT - CUT_BASE] = 0x342d5,
    [CUT_S_RETREAT_POLL - CUT_BASE] = 0x34348,
    [CUT_K_APPROACH - CUT_BASE] = 0x346a7,
    [CUT_K_APPROACH_TICK - CUT_BASE] = 0x347b7,
    [CUT_K_APPROACH_POLL - CUT_BASE] = 0x347c1,
    [CUT_K_SLIDE - CUT_BASE] = 0x347ed,
    [CUT_K_SLIDE_POLL - CUT_BASE] = 0x348b9,
    [CUT_K_SLIDE_TICK - CUT_BASE] = 0x348e4,
    [CUT_K_TALK - CUT_BASE] = 0x349b5,
    [CUT_K_TALK_SUB - CUT_BASE] = CUT_SUBTITLE_SITE,
    [CUT_K_TALK_TICK - CUT_BASE] = 0x34a99,
    [CUT_K_TALK_POLL - CUT_BASE] = 0x34aa3,
    [CUT_K_PAUSE - CUT_BASE] = 0x34b7d,
    [CUT_K_TALK2 - CUT_BASE] = 0x34bea,
    [CUT_K_TALK2_TICK - CUT_BASE] = 0x34cc9,
    [CUT_K_TALK2_POLL - CUT_BASE] = 0x34cd3,
    [CUT_K_RETREAT - CUT_BASE] = 0x34da7,
    [CUT_K_RETREAT_POLL - CUT_BASE] = 0x34eb2,
    [CUT_K_WAIT1 - CUT_BASE] = 0x34999,
    [CUT_K_WAIT2 - CUT_BASE] = 0x34ba1,
    [CUT_K_WAIT3 - CUT_BASE] = 0x34d8c,
    [CUT_UNLOAD - CUT_BASE] = VOICE_END_WAIT_BOUNDARY,
};
/** One step ahead: cutscene STAGE, waiting at its original site (stage_site). */
xeen_status cut_step(xeen_game *g, int stage)
{
    return cut_set(g, stage, stage_site[stage - CUT_BASE]);
}
/** The original wait site of cutscene STAGE (stage_site): a poll's, a tick wait's or a voice wait's own call. */
static uint32_t cut_site(int stage)
{
    return stage_site[stage - CUT_BASE];
}
/** Stops a cutscene at unsupported BOUNDARY. */
xeen_status cut_stop(xeen_game *g, uint32_t boundary)
{
    g->cut_stage = 0;
    g->location_kind = 0;
    return panel_stop(g, boundary);
}
/** Records one cutscene return (RET, VALUE, whether it was a key poll) for the evidence log. */
static void cut_log(xeen_game *g, uint32_t ret, uint16_t value, int ui_poll)
{
    ++g->cut_events;
    g->cut_ret = ret;
    g->cut_value = value;
    g->cut_is_poll = (uint8_t)ui_poll;
}
/* Draws one sprite frame at (X, Y) with its SCALE and FLAGS; a draw the presentation refuses is counted as skipped.
 * Original: one 4AC92 with its scale (BP+0Eh). */
void cut_draw(xeen_game *g, uint16_t handle, uint16_t frame, int x, int y, int scale,
                     uint16_t flags)
{
    xeen_sprite_draw d = {handle, frame, (uint16_t)x, (uint16_t)y, (uint16_t)scale, flags, 0};
    if (xeen_presentation_draw_sprites(g->presentation, &d, 1) != XEEN_OK) {
        ++g->exit_skipped;
    }
}
/* A poll stage: the key is read and discarded (a cutscene cannot be skipped); the log names STAGE's site.
 * Original: 189E3 (F5FE). */
void cut_poll(xeen_game *g, int stage)
{
    uint16_t key = exit_poll(g);
    ++g->cut_polls;
    cut_log(g, cut_site(stage), key, 1);
}
/* One read of the clock ENTRY (the frame, scene or subtitle timer), logged at RET: 1 with *AX the ticks since its
 * mark, or 0 with *STATUS while the BIOS answer is pending. Original: 4EA1C, 4EA44 or 4EA6C. */
static int cut_read(xeen_game *g, uint32_t entry, uint32_t ret, xeen_timer_bios bios, void *user,
                    int16_t *ax, xeen_status *status)
{
    g->cut_elapsed = (xeen_timer_elapsed_state){0, entry};
    if ((*status = timer_ticks(g, &g->cut_elapsed, bios, user)) != XEEN_OK) {
        return 0;
    }
    *ax = (int16_t)g->timer_registers.ax;
    cut_log(g, ret, g->timer_registers.ax, 0);
    return 1;
}
/* A tick stage: the encounters' loop on one read of CLOCK (the frame, scene or subtitle timer) until MIN ticks have
 * passed since that clock was marked. 1 once they have; 0 with *STATUS the read's status (the BIOS is pending) or
 * XEEN_SERVICE_REQUIRED (fewer: the stage reads again). Each read is logged at STAGE's site. */
int cut_ticks(xeen_game *g, uint32_t clock, int stage, int16_t min, xeen_timer_bios bios, void *user,
              xeen_status *status)
{
    int16_t ax;
    if (!cut_read(g, clock, cut_site(stage), bios, user, &ax, status)) {
        return 0;
    }
    if (ax < min) {
        *status = XEEN_SERVICE_REQUIRED;
        return 0;
    }
    return 1;
}
/* A frame-pacing stage and nothing else: STAGE waits until MIN frame ticks have passed since the frame was marked,
 * then the cutscene goes on at NEXT. */
xeen_status cut_frame_stage(xeen_game *g, int stage, int16_t min, int next, xeen_timer_bios bios, void *user)
{
    xeen_status s;
    if (!cut_ticks(g, XEEN_TIMER_FRAME_ELAPSED, stage, min, bios, user, &s)) {
        return s;
    }
    return cut_step(g, next);
}
/* Whether the voice is still playing, asked at STAGE's site (a voice-wait stage stays while it is). */
int cut_voice_busy(xeen_game *g, int stage)
{
    return voice_busy(g, cut_site(stage));
}
/** Samples the BIOS timer at cutscene wait ENTRY. */
xeen_status cut_sample(xeen_game *g, uint32_t entry, xeen_timer_bios bios, void *user)
{
    return xeen_timer_sample(&g->timer, entry, &g->timer_registers, bios, user);
}
/* Lets the cutscene's DOS memory go: the segments of the map texts, the miss and pow voices and the file buffer are
 * zeroed (the native holds no DOS memory, so nothing is freed). Original: 33CD8: 18693 frees and zeroes 9C60, F386,
 * F388 and F42C, and the 9C6C / 9C68 copies. */
static void cut_unload(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_write16(m, XEEN_DS_MAP_TEXTS_SEGMENT, 0);
    xeen_write16(m, XEEN_DS_MISS_VOICE_SEGMENT, 0);
    xeen_write16(m, XEEN_DS_POW_VOICE_SEGMENT, 0);
    xeen_write16(m, XEEN_DS_FILE_BUFFER_SEGMENT, 0);
}
/* A cutscene's spoken line: entry K of special.bin (the text after its K-th NUL) copied into the text buffer, which is
 * zeroed to 200 bytes first; the file's DOS segment word is kept 0. Returns the line's length, or -1 when the file
 * is missing, has fewer entries, or the line reaches 200 bytes (the original's length scan would pass the buffer).
 * Original: 2B632(special.bin) outside mode 11h (97C2 / 97C0; the 50D4:165C offsets); 15616(A827, entry, 200);
 * 33FBA's length. */
int cut_line(xeen_game *g, unsigned k)
{
    xeen_movement *m = &g->movement;
    size_t n = 0, at = 0, len = 0;
    unsigned nul = 0;
    uint8_t *b;
    if (!(b = menu_load(g, ds_text(g, XEEN_TEXT_CUTSCENE_TEXTS_FILE), &n))) {
        return -1;
    }
    xeen_write16(m, XEEN_DS_TEXT_BLOCK_SEGMENT, 0);
    xeen_write16(m, XEEN_DS_TEXT_BLOCK, 0);
    while (nul < k && at < n) {
        if (!b[at]) {
            ++nul;
        }
        ++at;
    }
    if (nul < k) {
        free(b);
        return -1;
    }
    memset(m->ds + XEEN_DS_TEXT_BUFFER, 0, 200);
    while (len < 200u && at + len < n && b[at + len]) {
        m->ds[XEEN_DS_TEXT_BUFFER + len] = b[at + len];
        ++len;
    }
    free(b);
    if (len >= 200u) {
        return -1; /* 33FBA would read past A827+200 */
    }
    return (int)len;
}
/* The original's voice test (185F0, 34276, 35FC4): the panel's sound (ED9A) on and XEEN.CFG's sound driver 2 or above:
 * the voice speaks. Phase 286: [audio] speech = off answers 0, the original's no-voice branch (as XEEN.CFG's None:
 * the subtitles), without changing 8D38 (XEEN.CFG's image, which an ending writes back). */
int cut_voice_speaks(const xeen_game *g)
{
    const xeen_movement *m = &g->movement;
    return m->ds[XEEN_DS_SOUND_ON] && xeen_read16(m, XEEN_DS_CONFIG_SOUND_DRIVER) >= 2u && !g->audio.speech_off;
}
/* Whether a talk shows its subtitle: not when the voice speaks (cut_voice_speaks), which presents the screen and
 * answers 0; else 1. Original: 185E3(0, len) up to its 4EA6C (ED9A, 8D38). */
int cut_subtitles(xeen_game *g)
{
    if (cut_voice_speaks(g)) {
        (void)exit_present(g);
        return 0;
    }
    return 1;
}
/* One subtitle frame after TICKS of the subtitle clock: above 1 tick the line's window moves on one character
 * (the position counts modulo the line's length; the shown part is the 40 characters up to it), formatted into the
 * subtitle text and the clock marked again; then the box and the text are drawn and presented. With 1 tick or none
 * the last text is drawn again (the original redraws whatever its own stack frame still holds). Stops on an empty
 * line.
 * Original: 185FF onward: F9A4 = (F9A4 + 1) % len, A827[F9A4 - 40 .. F9A4) in the DS:4B6A format, 4EA5D; box.vga
 * (FAF6) frame 0 at (24h, BDh), 4E38C (window 0), 50BF6(0). */
static xeen_status cut_subtitle(xeen_game *g, int16_t ticks, xeen_timer_bios bios, void *user)
{
    xeen_movement *m = &g->movement;
    xeen_status status;
    if (ticks > 1) {
        char part[48];
        int16_t pos = (int16_t)(xeen_read16(m, XEEN_DS_SUBTITLE_POSITION) + 1u), dx;
        if (!g->cut_len) {
            return cut_stop(g, CUT_STOP_SUBTITLE_EMPTY);
        }
        pos = (int16_t)(pos % (int16_t)g->cut_len);
        xeen_write16(m, XEEN_DS_SUBTITLE_POSITION, (uint16_t)pos);
        dx = (int16_t)(pos - 40);
        if (dx < 1) {
            dx = 0;
        }
        if (pos - dx >= 0 && pos - dx < (int)sizeof part) {
            memcpy(part, m->ds + XEEN_DS_TEXT_BUFFER + dx, (size_t)(pos - dx));
            part[pos - dx] = 0;
        } else {
            part[0] = 0;
        }
        (void)snprintf(g->cut_text, sizeof g->cut_text, ds_text(g, XEEN_TEXT_SUBTITLE_LINE), part);
        if ((status = cut_sample(g, XEEN_TIMER_SUBTITLE_MARK, bios, user)) != XEEN_OK) {
            return status;
        }
    }
    cut_draw(g, xeen_read16(m, XEEN_DS_SUBTITLE_BOX), 0, 0x24, 0xbd, 0, 0);
    menu_window_text(g, XEEN_WINDOW_SCREEN, WINDOW_TEXT_ONLY, g->cut_text);
    (void)exit_present(g);
    return XEEN_OK;
}
/* Whether a cutscene's talk goes on: the voice still plays (asked at SITE) or the subtitle has not wrapped to its
 * start. Original: the talk loops' condition (34AA7, 355E2, 35FA8, 3425A): 187E9(1, 0), F9A4. */
int cut_talking(xeen_game *g, uint32_t site)
{
    return voice_busy(g, site) || xeen_read16(&g->movement, XEEN_DS_SUBTITLE_POSITION) != 0;
}
/* A talk frame's subtitle stage: the subtitle clock read, then its frame (cut_subtitle). XEEN_OK to continue the
 * talk. Original: 185FF inside 185E3 (its 4EA6C). */
xeen_status cut_subtitle_tick(xeen_game *g, xeen_timer_bios bios, void *user)
{
    int16_t ax;
    xeen_status s;
    if (!cut_read(g, XEEN_TIMER_SUBTITLE_ELAPSED, CUT_SUBTITLE_SITE, bios, user, &ax, &s)) {
        return s;
    }
    return cut_subtitle(g, ax, bios, user);
}
/* After a talk frame: while the subtitle runs, its step (stage SUB, cut_subtitle_stage) comes first, then the talk
 * goes on at NEXT. */
xeen_status cut_talk_next(xeen_game *g, int sub, int next)
{
    return cut_step(g, cut_subtitles(g) ? sub : next);
}
/* A subtitle stage: the subtitle clock's read and the subtitle's frame (cut_subtitle_tick), then NEXT. */
xeen_status cut_subtitle_stage(xeen_game *g, int next, xeen_timer_bios bios, void *user)
{
    xeen_status s;
    if ((s = cut_subtitle_tick(g, bios, user)) != XEEN_OK) {
        return s;
    }
    return cut_step(g, next);
}
/* The party bar's sprites (drawn, not yet shown) and the exploration icons, at the end of the gatekeeper's, the
 * Sphinx's and the guardians' cutscenes. Original: 2ABB8(0) and 4ADB1(0, DS:2A5C) (36075/36081, 343BB/343C7,
 * 34F3F/34F4B). */
static void cut_party_bar(xeen_game *g)
{
    if (!g->presentation) {
        return;
    }
    (void)xeen_world_entry_party_bar(&g->movement, g->presentation, 0);
    explore_icons(g);
}
static xeen_status cut_finish_tail(xeen_game *, int);
/** Waits, one stage per poll, until the cutscene's voice has finished, then ends the cutscene by its kind (the
 * Sphinx's, the guards', or the Reaper's and the gate's map reload). The memory the original frees around it is DOS
 * memory, not modelled. Original: 33D36 frees 97C2, loops on 187E9(1, 0) (33D4F, the boundary 33D58), frees F42C
 * and reloads the residents. */
static xeen_status cut_unload_wait(xeen_game *g)
{
    if (cut_voice_busy(g, CUT_UNLOAD)) {
        return cut_step(g, CUT_UNLOAD);
    }
    if (g->cut_end == CUT_END_SPHINX) {
        return sphinx_end_tail(g);
    }
    if (g->cut_end == CUT_END_GUARD) {
        return guard_end_tail(g);
    }
    return cut_finish_tail(g, g->cut_end);
}
/* Ends a talking cutscene (END): its sprites are freed, then the unload waits for the voice (cut_unload_wait) and
 * END's tail follows. The Reaper's and the gatekeeper's own end code restores the song volume here, between the free
 * and the unload; the Sphinx and the guards restore it earlier, before their last frames (so a retreat runs at the
 * talk's volume only in the Reaper's cutscene). Original: 358FC / 35FED (4AA3E, 187CB(5Fh), 33D36); the Sphinx's and
 * the guards' 4AA3E, 33D36. */
xeen_status cut_end(xeen_game *g, enum cut_end end)
{
    (void)xeen_sprite_free_from(&g->sprites, g->cut_sprites[0]);
    if (end == CUT_END_REAPER || end == CUT_END_GATE) {
        cut_volume(g, CUT_SONG_VOLUME_END);
    }
    g->cut_end = (uint8_t)end;
    return cut_unload_wait(g);
}
/* After an encounter's last talk: with the key (g->cut_key) the party passes and the cutscene ends (END); without it
 * the figure retreats first, its frames from the nearest (stage RETREAT with the counter 0). */
xeen_status cut_pass_or_retreat(xeen_game *g, enum cut_end end, int retreat)
{
    if (g->cut_key) {
        return cut_end(g, end);
    }
    g->cut_si = 0;
    return cut_step(g, retreat);
}
/* The retreat's last frame has been polled: the screen saved before the figure came is restored and shown, then the
 * cutscene ends (END). */
xeen_status cut_retreat_done(xeen_game *g, enum cut_end end)
{
    exit_restore(g);
    (void)exit_present(g);
    return cut_end(g, end);
}
/* The Sphinx's and the gatekeeper's talk has ended: without the voice (cut_voice_speaks) this talk is the last
 * (CUT_SILENT_LAST_TALK: the original sets its talk counter past every encounter's count, so the remaining talks and
 * their voices are skipped). Original: 34276 / 35FC4 (ED9A clear or 8D38 below 2: SI = 0Ah). */
void cut_talk_ended(xeen_game *g)
{
    if (!cut_voice_speaks(g)) {
        g->cut_si = CUT_SILENT_LAST_TALK;
    }
}
/* Every talking cutscene's entry (after its kind's own setup): the key and the first clock mark cleared, the mouse
 * off, the resident DOS memory let go (cut_unload: four segment words zeroed; the cutscene's sprites are loaded
 * later), the button set pushed and the subtitle position 0.
 * Original: 34FAA, 35992, the Sphinx's, the guards'; 4E231, 33CD8, 190ED/1919B, F9A4 = 0. */
void cut_enter(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    g->cut_key = 0;
    g->cut_sampled = 0;
    xeen_presentation_mouse_off(g->presentation); /* 4E231 (the mouse off) */
    cut_unload(g);
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    xeen_write16(m, XEEN_DS_SUBTITLE_POSITION, 0);
}
/* The gate's and the Reaper's start: cut_enter, then the destination set to MAP at the party's cell and facing (the
 * encounter may replace it). */
void cut_start(xeen_game *g, uint16_t map)
{
    cut_enter(g);
    cut_dest_here(g, map);
}
/* The keyed destination (see game_cutscene.h): the row of MAP is the first one on it (as the original's search by
 * map), and only its flag is read. */
int cut_keyed_dest(xeen_game *g, uint16_t map, const cut_keyed_destination *table, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; ++i) {
        if (table[i].map == map) {
            break;
        }
    }
    if (i < n && g->movement.ds[table[i].flag]) {
        cut_set_dest(g, &table[i].to);
        g->cut_key = 1;
        return 1;
    }
    return 0;
}
/* The end's destination (cut_dest: map, x, y, facing) set to TO. */
void cut_set_dest(xeen_game *g, const cut_destination *to)
{
    g->cut_dest[0] = to->map;
    g->cut_dest[1] = to->x;
    g->cut_dest[2] = to->y;
    g->cut_dest[3] = to->facing;
}
/* The end's destination (cut_dest: map, x, y, facing): MAP at the party's cell and facing (the cells sign-extended). */
void cut_dest_here(xeen_game *g, uint16_t map)
{
    const uint8_t *ds = g->movement.ds;
    g->cut_dest[0] = map;
    g->cut_dest[1] = (uint16_t)(int16_t)(int8_t)ds[XEEN_DS_PARTY_X];
    g->cut_dest[2] = (uint16_t)(int16_t)(int8_t)ds[XEEN_DS_PARTY_Y];
    g->cut_dest[3] = ds[XEEN_DS_PARTY_DIRECTION];
}
/* The end's position after the destination's 19E82 (cut_dest: map, x, y, facing): the party's cell and facing in
 * DS and the navigation state. */
static void cut_place_party(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    ds[XEEN_DS_PARTY_X] = (uint8_t)g->cut_dest[1];
    ds[XEEN_DS_PARTY_Y] = (uint8_t)g->cut_dest[2];
    ds[XEEN_DS_PARTY_DIRECTION] = (uint8_t)g->cut_dest[3];
    xeen_navigation_set(m, (xeen_navigation){(uint8_t)g->cut_dest[3], (uint8_t)g->cut_dest[1],
                                             (uint8_t)g->cut_dest[2], (uint8_t)g->cut_dest[0]});
}
/* The guards' and the Sphinx's end, after the destination's map load: the saved screen back, the party placed there,
 * the party bar and icons drawn, then the last scene (cut_to_scene). Original: 19E82, 4E99F(8C90), 1B503(0). */
xeen_status cut_arrive(xeen_game *g)
{
    exit_restore(g);
    cut_place_party(g);
    cut_party_bar(g);
    return cut_to_scene(g);
}
/* Every cutscene's end once the party is placed: the last scene (argument 0) and its stage (cut_after_scene
 * continues). Original: 1B503(0) at 4EA35. */
static xeen_status cut_to_scene(xeen_game *g)
{
    g->scene_argument = 0;
    xeen_presentation_trace_extra("cutend"); /* Evidence: before 1B503(0) */
    return cut_step(g, CUT_SCENE);
}
/** The end of the Reaper's and the gate's cutscenes (GATE: the gatekeeper's): the destination map is loaded and the
 * party placed in it; at the gate the whole screen is the frame's picture again (back.raw), with the party bar and
 * the icons over it (cut_party_bar: the guard's last frames had stayed outside the view and the icons); elsewhere
 * the screen saved before the scene comes back (exit_restore). Then the closing scene (cut_to_scene).
 * Original: 358FC / 35FED: 4AA3E(BP-6), 18693 of the voice (0), 187CB(5Fh), 33D36, 19E82(map), the position,
 * 1B503(0); the gatekeeper's 3606B 188F6(back.raw), 2ABB8(0), 4ADB1. */
static xeen_status cut_finish_tail(xeen_game *g, int gate)
{
    xeen_presentation_trace_extra("cutload"); /* Evidence: 19E82 */
    if (map_reload(g, g->cut_dest[0]) != XR_OK) {
        return cut_stop(g, gate ? CUT_STOP_GATE_MAP_RELOAD : CUT_STOP_REAPER_MAP_RELOAD);
    }
    if (!gate) {
        exit_restore(g);
    }
    cut_place_party(g);
    if (gate) {
        if (!menu_raw(g, ds_text(g, XEEN_TEXT_CUTSCENE_BACKGROUND))) {
            return cut_stop(g, CUT_STOP_BACKGROUND);
        }
        cut_party_bar(g);
    }
    return cut_to_scene(g);
}
/**
 * A talking cutscene's last scene has run: the whole screen presented, the mouse cursor back, the button set popped
 * twice (the cutscene's and the location's), the cutscene and location state cleared, and the location returns.
 * Any other stage is an explicit stop. Original: 1B503, then 50BF6(0), 4E06B(F460, 0), 18FFF and 27D66's 18FFF; 1C1B3.
 */
xeen_status cut_after_scene(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    if (g->cut_stage != CUT_SCENE) {
        return cut_stop(g, CUT_STOP_SCENE_STAGE);
    }
    (void)exit_present(g);
    (void)xeen_presentation_select_cursor(g->presentation, xeen_read16(m, XEEN_DS_MOUSE_SPRITES),
                                          0);
    xeen_button_stack(m->ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    xeen_button_stack(m->ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT); /* 27D66: 18FFF */
    g->cut_stage = g->panel_stage = 0;
    g->location_kind = 0;
    return location_return(g, 0, 1);
}
/* One draw LO..HI of a talk frame, its AX the world byte as at the original's calls (each follows a world test).
 * Original: 504DA (35582, 356E8, 35F67). */
uint16_t cut_random(xeen_game *g, uint16_t lo, uint16_t hi)
{
    uint16_t v = lo;
    if (g->event_random) {
        (void)xeen_entity_rng_range(g->event_random(g->event_random_user, g->movement.ds[XEEN_DS_WORLD]), lo,
                                    hi, &v);
    }
    return v;
}
/**
 * Draws a cutscene's still frame: the two 160-pixel halves (frames 0 and 1) of its first sprite.
 * The Sphinx's and the guard's cutscenes both use it.
 */
void cut_draw_still_frame(xeen_game *g)
{
    cut_draw(g, g->cut_sprites[0], 0, 0, 0, 0, 0);
    cut_draw(g, g->cut_sprites[0], 1, 0xa0, 0, 0, 0);
}
/* Resumes a talking cutscene at its stage: the unload's wait, else the subtitle clock marked once (its setup reads
 * no clock before the first frame), then the encounter's own resume by the stage's range (the Sphinx, the guards,
 * the gatekeeper, the Reaper); a stage in no range stops. Original: 4EA5D at 35190 / 35B8F / 34007 / 34477. */
xeen_status cut_resume(xeen_game *g, xeen_timer_bios bios, void *user)
{
    if (g->cut_stage == CUT_UNLOAD) {
        return cut_unload_wait(g);
    }
    if (!g->cut_sampled) {
        xeen_status s = cut_sample(g, XEEN_TIMER_SUBTITLE_MARK, bios, user);
        if (s != XEEN_OK) {
            return s;
        }
        g->cut_sampled = 1;
    }
    if (g->cut_stage > CUT_SCENE && g->cut_stage < CUT_S_END) {
        return sphinx_resume(g, bios, user);
    }
    if (g->cut_stage > CUT_S_END && g->cut_stage < CUT_K_END) {
        return guard_resume(g, bios, user);
    }
    if (g->cut_stage >= CUT_G_APPROACH && g->cut_stage < CUT_SCENE) {
        return gate_resume(g, bios, user);
    }
    if (g->cut_stage >= CUT_BASE && g->cut_stage < CUT_G_APPROACH) {
        return reaper_resume(g, bios, user);
    }
    return cut_stop(g, CUT_STOP_RESUME_STAGE);
}
