/*
 * The cutscenes' private interface (Phase 248): game_cutscene.c's frame-loop services and the four encounters'
 * units (game_cut_reaper.c, game_cut_gate.c, game_cut_sphinx.c, game_cut_guard.c). Only these five units include
 * it; the session sees cut_resume, cut_after_scene, cut_poll_next and the encounters' starts (game_internal.h).
 */
#ifndef XEEN_GAME_CUTSCENE_H
#define XEEN_GAME_CUTSCENE_H
#include "game_internal.h"

/* The cutscenes' stages (the Reaper 34FAA, the gatekeeper 35992, the Sphinx and the guards): one sequence, whose
 * ranges select each encounter's resume (cut_resume). Each stage is one original resume point, entered by cut_step
 * (which also reports the stage's wait site, cut_site), and is of one kind:
 *  - a frame stage marks the frame clock (cut_sample), draws and presents one frame;
 *  - a tick stage (_TICK, _PAUSE, the gate's _TALK_WAIT) waits until enough ticks have passed since the mark (cut_ticks; cut_frame_stage
 *    when nothing else follows);
 *  - a poll stage (_POLL) reads and discards a key (cut_poll), then counts the loop (cut_si) or leaves it;
 *  - a subtitle stage (_SUB) runs the subtitle's own tick (cut_subtitle_stage);
 *  - a voice wait (the guards' CUT_K_WAIT1..3, CUT_UNLOAD) stays while the voice plays (cut_voice_busy).
 * A stage that falls through into the next one continues in the same call, as the original's code does. */
enum {
    CUT_R_APPROACH = CUT_BASE,
    CUT_R_APPROACH_TICK,
    CUT_R_APPROACH_POLL,
    CUT_R_SLIDE,
    CUT_R_SLIDE_TICK,
    CUT_R_SLIDE_POLL,
    CUT_R_CSLIDE,
    CUT_R_CSLIDE_POLL,
    CUT_R_TALK,
    CUT_R_TALK_SUB,
    CUT_R_TALK_POLL,
    CUT_R_TALK_TICK,
    CUT_R_PAUSE_TICK,
    CUT_R_TALK2,
    CUT_R_TALK2_POLL,
    CUT_R_TALK2_TICK,
    CUT_R_RETREAT,
    CUT_R_RETREAT_POLL,
    CUT_G_APPROACH,
    CUT_G_APPROACH_POLL,
    CUT_G_WALK,
    CUT_G_WALK_POLL,
    CUT_G_STEP,
    CUT_G_STEP_SUB,
    CUT_G_STEP_TICK,
    CUT_G_TALK,
    CUT_G_TALK_SUB,
    CUT_G_TALK_POLL,
    CUT_G_TALK_TICK,
    CUT_G_TALK_WAIT,
    CUT_SCENE,
    CUT_S_APPROACH,
    CUT_S_APPROACH_POLL,
    CUT_S_STEP,
    CUT_S_TALK,
    CUT_S_TALK_SUB,
    CUT_S_TALK_POLL,
    CUT_S_RETREAT,
    CUT_S_RETREAT_POLL,
    CUT_S_END,
    CUT_K_APPROACH,
    CUT_K_APPROACH_TICK,
    CUT_K_APPROACH_POLL,
    CUT_K_SLIDE,
    CUT_K_SLIDE_POLL,
    CUT_K_SLIDE_TICK,
    CUT_K_TALK,
    CUT_K_TALK_SUB,
    CUT_K_TALK_TICK,
    CUT_K_TALK_POLL,
    CUT_K_PAUSE,
    CUT_K_TALK2,
    CUT_K_TALK2_TICK,
    CUT_K_TALK2_POLL,
    CUT_K_RETREAT,
    CUT_K_RETREAT_POLL,
    CUT_K_WAIT1,
    CUT_K_WAIT2,
    CUT_K_WAIT3,
    CUT_K_END,
    CUT_UNLOAD /* 33D36's 187E9(1, 0) loop (33D4F) at a cutscene's end */
};

/* game_cutscene.c: the frame loop's services (the clock reads and polls, the frames, the subtitles, the voice,
 * the stage and the stop, the end) */
void cut_draw(xeen_game *g, uint16_t handle, uint16_t frame, int x, int y, int scale, uint16_t flags);
/* Which cutscene's end continues after the 33D36 unload wait (g->cut_end, cut_end): the Reaper's and the gate's map
 * reload (cut_finish_tail's gate flag: the Reaper 0, the gate 1) or the Sphinx's and the guards' own tails. */
enum cut_end { CUT_END_REAPER = 0, CUT_END_GATE = 1, CUT_END_SPHINX = 2, CUT_END_GUARD = 3 };
/* The song volumes a cutscene sets (cut_volume, 187CB): lowered while the figures speak, restored for the end. */
enum { CUT_SONG_VOLUME_TALK = 0x30, CUT_SONG_VOLUME_END = 0x5f };
/* Without the voice a talk's end sets the step counter to this value (the original's SI = 0Ah), past every
 * encounter's last talk, so that talk is the last (cut_talk_ended). */
enum { CUT_SILENT_LAST_TALK = 10 };
xeen_status cut_end(xeen_game *g, enum cut_end end);
xeen_status cut_pass_or_retreat(xeen_game *g, enum cut_end end, int retreat);
xeen_status cut_retreat_done(xeen_game *g, enum cut_end end);
void cut_talk_ended(xeen_game *g);
/* Where a cutscene's end takes the party: a map, the cell (x, y) and the facing (enum xeen_direction); cut_dest's
 * four words in this order. */
typedef struct {
    uint16_t map, x, y, facing;
} cut_destination;
/* A destination a key opens: on MAP, with the game flag byte FLAG set (the key or pass the party holds there), the
 * end takes the party TO it. The original's tables keep the three as parallel arrays by map (the gate's CS:23E0, the
 * Reaper's CS:1CAE and CS:1C9A); a row here is one index of them. */
typedef struct {
    uint16_t map, flag;
    cut_destination to;
} cut_keyed_destination;
void cut_dest_here(xeen_game *g, uint16_t map);
void cut_set_dest(xeen_game *g, const cut_destination *to);
/* The keyed destination: the first row of TABLE (N rows) on MAP whose flag is set takes its destination and the key
 * (cut_key = 1): 1. Else (no row on MAP, or its flag clear) 0: the destination stays. */
int cut_keyed_dest(xeen_game *g, uint16_t map, const cut_keyed_destination *table, unsigned n);
xeen_status cut_arrive(xeen_game *g);
int cut_line(xeen_game *g, unsigned k);
void cut_poll(xeen_game *g, int stage);
uint16_t cut_random(xeen_game *g, uint16_t lo, uint16_t hi);
int cut_ticks(xeen_game *g, uint32_t clock, int stage, int16_t min, xeen_timer_bios bios, void *user,
              xeen_status *status);
xeen_status cut_frame_stage(xeen_game *g, int stage, int16_t min, int next, xeen_timer_bios bios, void *user);
int cut_voice_busy(xeen_game *g, int stage);
xeen_status cut_sample(xeen_game *g, uint32_t entry, xeen_timer_bios bios, void *user);
xeen_status cut_step(xeen_game *g, int stage);
xeen_status cut_stop(xeen_game *g, uint32_t boundary);
xeen_status cut_subtitle_tick(xeen_game *g, xeen_timer_bios bios, void *user);
xeen_status cut_subtitle_stage(xeen_game *g, int next, xeen_timer_bios bios, void *user);
xeen_status cut_talk_next(xeen_game *g, int sub, int next);
int cut_talking(xeen_game *g, uint32_t site);
int cut_subtitles(xeen_game *g);
int cut_voice_speaks(const xeen_game *g);
void cut_volume(xeen_game *g, unsigned level);
void cut_draw_still_frame(xeen_game *g);
void cut_enter(xeen_game *g);
void cut_start(xeen_game *g, uint16_t map);
void cut_voc_load(xeen_game *g, unsigned slot, const char *name);
void cut_voc_start(xeen_game *g, unsigned slot, uint32_t site);

/* the encounters' frame loops (cut_resume selects one by its stage range) and their end tails (cut_unload_wait) */
xeen_status reaper_resume(xeen_game *g, xeen_timer_bios bios, void *user);
xeen_status gate_resume(xeen_game *g, xeen_timer_bios bios, void *user);
xeen_status sphinx_end_tail(xeen_game *g);
xeen_status sphinx_resume(xeen_game *g, xeen_timer_bios bios, void *user);
xeen_status guard_end_tail(xeen_game *g);
xeen_status guard_resume(xeen_game *g, xeen_timer_bios bios, void *user);

#endif
