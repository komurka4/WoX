/*
 * The session's lifecycle (Phase 246, from game.c): its construction (xeen_game_create, create_seeded at a
 * map, xeen_game_create_saved), the bounded lifecycle checks (the restored party, the timer at 4EA0D /
 * 4EA35), the host bindings (the presentation, the random carry, the effect RNG, the mouse reset),
 * xeen_game_destroy, the audio, CD, voice and clock settings, and the read-only inspection view. A map's
 * resources are game_resources.c's.
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Whether Clouds flag 19h is set (the map load's query, most significant bit first). Original: 2F847(19h, F01F)
 * in 19E82's maze loop (1A151). */
int clouds_flag19(const uint8_t *ds)
{
    return xeen_ds_get_bit(ds, XEEN_DS_GAME_FLAGS, 0x19u);
}
xeen_game *xeen_game_create(const uint8_t *state, size_t sn, const uint8_t *assets, size_t an,
                            xeen_game_start start, xeen_ui_present_fn fn, void *user, char *error,
                            size_t en)
{
    return create_seeded(state, sn, assets, an, start, fn, user, NULL, -1, error, en);
}
/* A start's position and its storage. Indoor positions address the 3x3 maze cache (16..31: the east
 * and north neighbours; 17CFD..17D47 step without a bound); an outdoor one past 15 is a pending
 * transition, not an entry position (seeded_map refuses it once the map's F608 is known). */
static int seeded_start_valid(const uint8_t *state, const uint8_t *assets, xeen_game_start start)
{
    return state && assets && start.world <= 1 && start.x <= 31u && start.y <= 31u &&
           start.direction <= 3 && start.map != 0 && start.map <= 255;
}
/* A loaded session's map load head, from SEED's DS: the wait window ("Oops..." during a fall, else "Please Wait..."
 * when a UI mode is set: the inn leaves 1; the load's tail closes it, create_seeded), the party marked as having acted (the
 * first scene marks its cell) and the key cleared, then the previous map's data, monsters, objects and wall items
 * cleared, as a transition does. Original: 19E82's head: 19E8A, 19ECB (3067 = 1; 1C952), 189D7, 19EDA..19F2E. */
static void seeded_head(xeen_game *g, const uint8_t *seed)
{
    memcpy(g->movement.ds, seed, sizeof g->movement.ds);
    reload_wait_open(g, &g->movement);
    g->movement.ds[XEEN_DS_PARTY_ACTED] = 1;
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
    memset(g->movement.ds + XEEN_DS_MAP_DATA, 0, 9u * XEEN_MAP_RECORD_BYTES);
    memset(g->movement.ds + XEEN_DS_MAP_MONSTERS, 0, 0x85cu);
    memset(g->movement.ds + XEEN_DS_MAP_OBJECTS, 0, XEEN_MAP_OBJECTS_MAX * XEEN_MAP_OBJECT_BYTES);
    memset(g->movement.ds + XEEN_DS_WALL_ITEMS, 0, XEEN_WALL_ITEMS_MAX * XEEN_WALL_ITEM_BYTES);
    g->movement.ds[XEEN_DS_EVENT_OBJECT] = 0;
    g->movement.ds[XEEN_DS_MAP_MONSTER_COUNT] = 0;
}
/* The start map: 19E82's maze loop (1A14B..1A199) on Clouds clears the surface bits (AND F8h) of
 * every cached map 2Ah..2Ch while the party's flag 19h is set (a loaded session's DS has the
 * party's F01F; a start whose party is unknown, no seed and no flag, is refused on those maps);
 * the cache, the movement services, a seed's head, DS 94, the commit and the position. Returns
 * NULL, or the reason it failed. */
static const char *seeded_map(xeen_game *g, xeen_game_start start, const uint8_t *seed, int flag19)
{
    xeen_navigation_cache cache;
    if (seed) {
        flag19 = clouds_flag19(seed);
    }
    if (start.world == 0 && start.map >= 42 && start.map <= 44 && flag19 < 0) {
        return "map requires unavailable flag19";
    }
    if (xeen_cache_prepare(&g->state, XR_BANK_STATE, start.map,
                           (xeen_patch_inputs){start.world, flag19 > 0}, &cache) != XR_OK) {
        return "original map/cache resource unavailable";
    }
    xeen_movement_init(&g->movement, movement_service, g);
    if (seed) {
        seeded_head(g, seed);
    }
    g->movement.ds[XEEN_DS_WORLD] = start.world;
    if (xeen_cache_commit(&cache, &g->movement) != XR_OK) {
        return "map commit failed";
    }
    if (xeen_read16(&g->movement, XEEN_DS_OUTDOORS) && (start.x > 15u || start.y > 15u)) {
        return "outdoor coordinates require unsupported map relocation before entry";
    }
    xeen_navigation_set(&g->movement,
                        (xeen_navigation){start.direction, start.x, start.y, (uint8_t)start.map});
    return NULL;
}
/* The start map's events and texts, and their DS preparation. Returns NULL, or the reason it
 * failed. */
static const char *seeded_events(xeen_game *g, uint16_t map)
{
    if (xeen_event_resources_load(&g->resources, &g->state, &g->assets, map, g->backing,
                                  sizeof g->backing, g->resident, sizeof g->resident, g->texts,
                                  sizeof g->texts, &g->index) != XR_OK) {
        return "original event/text resource unavailable";
    }
    if (xeen_event_resources_prepare(&g->resources, g->movement.ds, sizeof g->movement.ds) !=
        XE_OK) {
        return "event DS preparation failed";
    }
    return NULL;
}
/* seed: a whole DS the map startup writes over (a loaded session's DS at
 * the 19E82 map load); NULL for the zeroed BSS of program start. flag19:
 * without a seed, the party's flag 19h when the caller knows it (a saved
 * game read before its map load), else -1. A failure names its reason in
 * ERROR and frees the session (it holds nothing to destroy yet). */
xeen_game *create_seeded(const uint8_t *state, size_t sn, const uint8_t *assets, size_t an,
                         xeen_game_start start, xeen_ui_present_fn fn, void *user,
                         const uint8_t *seed, int flag19, char *error, size_t en)
{
    xeen_game *g = NULL;
    const char *why;
    if (!seeded_start_valid(state, assets, start)) {
        why = "invalid start or archive storage";
    } else if ((g = calloc(1, sizeof(*g))) == NULL) {
        why = "allocation failed";
    } else if (xeen_archive_open(&g->state, state, sn) != XR_OK ||
               xeen_archive_open(&g->assets, assets, an) != XR_OK) {
        why = "invalid archive";
    } else {
        why = seeded_map(g, start, seed, flag19);
        if (!why) {
            why = seeded_events(g, start.map);
        }
        if (!why && seed) {
            map_load_tail(g); /* 1B0CE: 19E82's end closes the wait window (DS only: no presentation bound yet) */
        }
    }
    if (why) {
        if (error && en) {
            (void)snprintf(error, en, "%s", why);
        }
        free(g);
        return NULL;
    }
    xeen_input_poll_init(&g->input_poll, XEEN_DS_SEGMENT);
    g->scene_argument = 1;
    g->world = start.world;
    g->map_id = start.map;
    g->ui_present = fn;
    g->present_user = user;
    xeen_event_ui_init(&g->ui, ui_present, ui_poll, g);
    g->resources.fallback = event_service;
    g->resources.fallback_user = g;
    g->movement.ds[XEEN_DS_VIEW_MONSTER(0)] = 255; /* explicit no selected scene entity */
    g->phase = XG_NAVIGATION;
    g->navigation = XSN_INVALID;
    g->event_status = XEE_NO_MATCH;
    if (error && en) {
        error[0] = 0;
    }
    return g;
}
/* A failed xeen_game_create_saved: the snapshot and the game (either may be NULL) freed, ERROR set to WHY unless WHY is
 * NULL (a read or restore that wrote its own message). Returns NULL. */
static xeen_game *saved_failure(xeen_save_snapshot *snapshot, xeen_game *g, const char *why, char *error,
                                size_t error_size)
{
    free(snapshot);
    xeen_game_destroy(g);
    if (why && error && error_size) {
        (void)snprintf(error, error_size, "%s", why);
    }
    return NULL;
}
/* The saved party's Clouds flag 19h, which the map load tests (the party is read before the map in the original:
 * 19953 before 19E82), from a restore of SNAPSHOT on scratch; -1 when the scratch cannot be had or the restore fails.
 */
static int saved_flag19(const xeen_save_snapshot *snapshot)
{
    uint8_t *scratch = calloc(1, 65536u);
    int flag19 = -1;
    if (scratch && xeen_save_restore(snapshot, scratch, 65536u, NULL, 0)) {
        flag19 = clouds_flag19(scratch);
    }
    free(scratch);
    return flag19;
}
/* The runtime state a loaded game enters with, outside the saved party block (the party, calendar and flags as
 * restored): dawn pending before the night's end, the exploration UI mode, the portrait clicks, combat mode 1, member
 * selector 1, F9A2 = 1, the Clairvoyance faces still and no highlighted portrait. Original: 43DE5..43E01 (the dawn and
 * entry state), 1791F..17984 (the command loop's temporaries). */
static void enter_saved_game(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    ds[XEEN_DS_DAWN_PENDING] = (uint8_t)(xeen_read16(&g->movement, XEEN_DS_MINUTE_OF_DAY) < XEEN_NIGHT_ENDS);
    ds[XEEN_DS_UI_MODE] = 0;
    ds[XEEN_DS_PORTRAIT_CLICKS] = 1;
    ds[XEEN_DS_COMBAT_MODE] = 1;
    ds[XEEN_DS_MEMBER_SELECTOR] = 1;
    xeen_write16(&g->movement, 0xf9a2, 1);
    clairvoyance_still(ds);
    ds[XEEN_DS_HIGHLIGHTED_PORTRAIT] = XEEN_NO_PORTRAIT;
}
/* The game of a save pair (game.h): the snapshot read from the Darkside state (or, with the Clouds alone, from its
 * own), a position the native can enter (x and y 0..15, a compass facing), the session seeded from the saved world's
 * state and CC with the party's flag 19h, the program data loaded and checked, the other world's CC bound when given,
 * the party restored into DS, the Clouds' presence recorded, the saved world's archive selected and the entry state
 * set (enter_saved_game). Every failure frees what was made; ERROR says why (a read's or restore's own message). */
xeen_game *xeen_game_create_saved(const uint8_t *cloud_state, size_t cloud_size,
                                  const uint8_t *dark_state, size_t dark_size,
                                  const uint8_t *cloud_cc, size_t cloud_cc_size,
                                  const uint8_t *dark_cc, size_t dark_cc_size, char *error,
                                  size_t error_size)
{
    xeen_save_snapshot *snapshot = malloc(sizeof *snapshot);
    xeen_game *g;
    unsigned world;
    if (!snapshot) {
        return saved_failure(NULL, NULL, "save snapshot allocation failed", error, error_size);
    }
    /* 19953 loads the party from archive 1 before selecting header[1Dh]; a Clouds-only installation has only its
     * own state archive. */
    if (!xeen_save_read(snapshot, dark_state ? dark_state : cloud_state, dark_state ? dark_size : cloud_size, error,
                        error_size)) {
        return saved_failure(snapshot, NULL, NULL, error, error_size);
    }
    if (snapshot->x > 15 || snapshot->y > 15 || snapshot->direction > 3) {
        return saved_failure(snapshot, NULL, "saved coordinates require unsupported map relocation before entry",
                             error, error_size);
    }
    world = snapshot->world;
    g = create_seeded(world ? dark_state : cloud_state, world ? dark_size : cloud_size, world ? dark_cc : cloud_cc,
                      world ? dark_cc_size : cloud_cc_size,
                      (xeen_game_start){snapshot->world, snapshot->x, snapshot->y, snapshot->direction, snapshot->map},
                      NULL, NULL, NULL, saved_flag19(snapshot), error, error_size);
    if (!g) {
        return saved_failure(snapshot, NULL, NULL, error, error_size);
    }
    if (!xeen_game_load_program_data(g)) {
        return saved_failure(snapshot, g, "program data disagrees with the native startup state", error, error_size);
    }
    if ((world ? cloud_cc : dark_cc) != NULL &&
        !xeen_game_bind_other_archive(g, world ? cloud_cc : dark_cc, world ? cloud_cc_size : dark_cc_size)) {
        return saved_failure(snapshot, g, "other-world resources unavailable", error, error_size);
    }
    if (!xeen_save_restore(snapshot, g->movement.ds, sizeof g->movement.ds, error, error_size)) {
        return saved_failure(snapshot, g, NULL, error, error_size);
    }
    g->movement.ds[XEEN_DS_CLOUDS_INSTALLED] = (uint8_t)(cloud_cc != NULL);
    xeen_select_archive(&g->movement, snapshot->world);
    enter_saved_game(g);
    g->lifecycle = 1;
    g->restored = 1;
    free(snapshot);
    if (error && error_size) {
        error[0] = 0;
    }
    return g;
}
int xeen_game_enable_lifecycle(xeen_game *g)
{
    if (!g || g->acted || g->phase != XG_NAVIGATION) {
        return 0;
    }
    g->lifecycle = 1;
    return 1;
}
/* The restored party's place in the session: Clouds counts as installed when this is the Clouds session or the other
 * world's archive is bound; the name record NAM goes to DS with Darkside's archive selected, as the original's load
 * reads it, then the map side's archive is selected; the party stands at the session's start POSITION (the fixture's
 * explicit placement). The restored calendar, condition and opaque bytes are kept as they are (F60A and the sky
 * state are never normalized).
 * Original: 15D15 precedes the load (B95: xeen.cc available); 19953: select(1), maze.nam/chr/pty, select(96E2). */
static void place_restored_party(xeen_game *g, const uint8_t nam[XEEN_GAME_NAME_BYTES], xeen_game_start position)
{
    if (!g->world || g->other_bound) {
        g->movement.ds[XEEN_DS_CLOUDS_INSTALLED] = 1;
    }
    xeen_select_archive(&g->movement, 1);
    memcpy(g->movement.ds + XEEN_DS_GAME_NAME, nam, XEEN_GAME_NAME_BYTES);
    xeen_select_archive(&g->movement, g->movement.ds[XEEN_DS_MAP_SIDE]);
    xeen_navigation_set(&g->movement, (xeen_navigation){position.direction, position.x, position.y,
                                                        (uint8_t)position.map});
}
/* Restores the party of the session's state archive (maze.chr, maze.pty and maze.nam, 31 bytes) into G. *CHR gets
 * the roster buffer it allocates, for the caller to free. Returns NULL, or why the restoration is refused, in the
 * order tested: the session is not an unused navigation one; a member is missing; a member's size is not this
 * world's (the party is 5FAh bytes on Darkside, 32Ch on Clouds); no memory; an extraction fails; the name record's
 * side is not the session's world; the party library refuses the membership or payload; before all of them, the
 * session must hold the program data, whose texts name the members (XEEN_TEXT_STATE_*_FILE).
 * Original: 19953's loads: maze.nam (19980), maze.chr (19993), maze.pty (199A2). */
static const char *restore_party(xeen_game *g, uint8_t **chr)
{
    uint8_t pty[XEEN_DARK_PARTY_BYTES], nam[XEEN_GAME_NAME_BYTES];
    xeen_resource_info ci, pi, ni;
    size_t cn = 0, pn = 0, nn = 0;
    xeen_game_start position;
    if (!g || g->acted || g->restored || g->phase != XG_NAVIGATION) {
        return "party restoration requires an unused navigation session";
    }
    if (!g->program_data) {
        return "party restoration requires the program data";
    }
    if (xeen_archive_find_name(&g->state, ds_text(g, XEEN_TEXT_STATE_ROSTER_FILE), &ci) != XR_OK ||
        xeen_archive_find_name(&g->state, ds_text(g, XEEN_TEXT_STATE_PARTY_FILE), &pi) != XR_OK ||
        xeen_archive_find_name(&g->state, ds_text(g, XEEN_TEXT_STATE_NAME_FILE), &ni) != XR_OK) {
        return "original maze.chr or maze.pty is unavailable";
    }
    if (ni.size != sizeof nam || ci.size != XEEN_ROSTER_BYTES ||
        pi.size != (g->world ? XEEN_DARK_PARTY_BYTES : XEEN_CLOUDS_PARTY_BYTES)) {
        return "unsupported original party resource sizes";
    }
    if (!(*chr = malloc(XEEN_ROSTER_BYTES))) {
        return "party resource allocation failed";
    }
    if (xeen_archive_extract(&g->state, XR_BANK_STATE, &ci, *chr, XEEN_ROSTER_BYTES, &cn) != XR_OK ||
        xeen_archive_extract(&g->state, XR_BANK_STATE, &pi, pty, sizeof pty, &pn) != XR_OK ||
        xeen_archive_extract(&g->state, XR_BANK_STATE, &ni, nam, sizeof nam, &nn) != XR_OK) {
        return "original party resource extraction failed";
    }
    if (nam[XEEN_GAME_NAME_SIDE] != g->world) {
        return "maze.nam world flag disagrees with the selected world";
    }
    position = xeen_game_inspect(g).position;
    if (xeen_party_restore(*chr, cn, pty, pn, g->world, g->movement.ds, sizeof g->movement.ds) != XP_OK) {
        return "unsupported original party membership or payload";
    }
    place_restored_party(g, nam, position);
    g->lifecycle = 1;
    g->restored = 1;
    return NULL;
}
/* Restores the session's original party before any command (restore_party) and enables the lifecycle checks. Returns
 * 1 (ERROR then empty), or 0 with the reason in ERROR (cut to EN bytes; ERROR may be NULL). */
int xeen_game_restore_party(xeen_game *g, char *error, size_t en)
{
    uint8_t *chr = NULL;
    const char *why = restore_party(g, &chr);
    free(chr);
    if (error && en) {
        if (why) {
            (void)snprintf(error, en, "%s", why);
        } else {
            error[0] = 0;
        }
    }
    return why == NULL;
}
/* The scene of a dialog inside combat: the Cast dialog opened in combat (its own scene, its refusal scroll or "Spell
 * Failed!"), or a combat interface stage. Its scene is the combat side's (no countdown; the effects, the attacker
 * selection and the party scan), not exploration's, and it is not traced as an exploration scene call.
 * Original: a 3914E dialog's 1B503 inside combat (9A1C 3 or FFh), then its 1C1B3 frame wait. */
static int combat_dialog_scene(const xeen_game *g)
{
    return (g->cast_active && g->cast_mode == XEEN_UI_MODE_COMBAT &&
            (g->panel_stage || g->scroll_owner == SCROLL_CAST_REFUSED || g->scroll_owner == SCROLL_SPELL_FAILED)) ||
           (g->combat_ui_site && g->panel_stage);
}
/* A stage of the session that drew this scene itself and continues when it ends: a panel, Rest, a surface, a door,
 * Bash, the unlock's scene, damage, an event's scene, a modal, a scroll, a page or a Who-will wait. With the view
 * window open the scene's command poll is then that stage's frame wait (timing only).
 * Original: 1B517 with window 11 open returns at once (1C1BD) to its caller, which resumes through 1C1B3. */
static int stage_owns_scene(const xeen_game *g)
{
    return g->panel_stage || g->rest_stage || g->surface_stage || g->door_stage || g->bash_stage ||
           g->unlock_stage == UNLOCK_SCENE || g->damage_stage || g->event_scene == EVENT_SCENE_RUNNING ||
           g->modal.active || g->scroll_owner || g->page_wait || g->who_wait;
}
/* A timer sample that did not complete: no BIOS clock to read yet (the timer stays pending), invalid input, or the
 * clock read failed. */
static xeen_status timer_pending(xeen_game *g, xeen_status status)
{
    g->command_status = status == XEEN_SERVICE_REQUIRED ? XGC_PENDING_TIMER
                        : status == XEEN_BAD_INPUT      ? XGC_INPUT_ERROR
                                                        : XGC_INTERNAL;
    return status;
}
/* The loop's timer marks, then the scene they precede. The game stopped at the exploration loop's frame mark (the
 * 4EA0D sampler) or its scene mark (4EA35); the host answers with the timer registers. The frame mark is followed by
 * the scene mark at once (no scene logic precedes the scene's sampler), and the scene mark by the scene itself: the
 * combat side's for a dialog inside combat (combat_dialog_scene), else exploration's, whose continuation becomes the
 * frame wait of a stage that owns the scene (stage_owns_scene), a monster update's pending service, or the end of a
 * continuous entry.
 * Original: 17AFC's 4EA0D, 17B01's 1B503(1), whose first effect is 4EA35. */
xeen_status xeen_game_resume_timer(xeen_game *g, xeen_timer_registers *r, xeen_timer_bios bios,
                                   void *user)
{
    xeen_status status;
    xeen_caller_result scene;
    int in_combat_dialog;
    if (!g || !r || !g->restored || g->phase != XG_STOPPED ||
        (g->lifecycle_boundary != XEEN_TIMER_FRAME_MARK && g->lifecycle_boundary != XEEN_TIMER_SCENE_MARK)) {
        return XEEN_BAD_INPUT;
    }
    if (g->lifecycle_boundary == XEEN_TIMER_FRAME_MARK) {
        status = xeen_timer_sample(&g->timer, XEEN_TIMER_FRAME_MARK, r, bios, user);
        if (status != XEEN_OK) {
            return timer_pending(g, status);
        }
        g->timer_registers = *r;
        /* Evidence: the mark is the exploration loop's own scene only (not a dialog's own 4EA0D, a modal's or an
         * event's scene: 2F9BA's wait, 2F517, 316EB's page waits, an operation's 1B503(1)). */
        if (!g->panel_stage && !g->modal.active && !g->who_wait && !g->scroll_owner &&
            !g->page_wait && g->event_scene == EVENT_SCENE_IDLE && !g->damage_stage) {
            xeen_presentation_trace_extra("mark loop-scene");
        }
        g->lifecycle_boundary = XEEN_TIMER_SCENE_MARK;
    }
    status = xeen_timer_sample(&g->timer, XEEN_TIMER_SCENE_MARK, r, bios, user);
    if (status != XEEN_OK) {
        return timer_pending(g, status);
    }
    g->timer_registers = *r;
    g->service_boundary = 0;
    in_combat_dialog = combat_dialog_scene(g);
    /* Evidence: a 1B503 entry (the combat side traces its own). */
    if (!in_combat_dialog) {
        if (g->scene_site) {
            xeen_presentation_trace_extra("scenecall %05X", (unsigned)g->scene_site);
        } else {
            xeen_presentation_trace_extra("scenecall");
        }
    }
    g->scene_site = 0;
    if (in_combat_dialog) {
        uint32_t boundary = 0;
        xeen_status scene_status;
        g->combat.quiet_scene = !g->scene_argument; /* 1C112's argument */
        scene_status = xeen_combat_scene(&g->combat, &g->movement, g->monster_database,
                                         g->monster_database_size, &boundary);
        g->combat.quiet_scene = 0;
        if (scene_status != XEEN_OK) {
            g->lifecycle_boundary = boundary ? boundary : SCENE_CALL_BOUNDARY;
            g->command_status = XGC_UNSUPPORTED;
            return XEEN_SERVICE_REQUIRED;
        }
        g->lifecycle_boundary = XEEN_SCENE_FRAME_WAIT;
        g->command_status = XGC_EXTERNAL;
        return XEEN_SERVICE_REQUIRED;
    }
    scene = xeen_scene_after_timer(&g->movement);
    if (scene.boundary == XEEN_LOOP_COMMAND_POLL && g->movement.ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)] &&
        stage_owns_scene(g)) {
        scene.boundary = XEEN_SCENE_FRAME_WAIT;
    }
    if (scene.boundary == XEEN_TICK_MONSTER_UPDATE && g->service_boundary) {
        scene.boundary = g->service_boundary;
    }
    if (scene.boundary == XEEN_LOOP_COMMAND_POLL && g->continuous && !g->entry_complete && !g->scene_argument) {
        scene.boundary = XEEN_ENTRY_FINISH;
    }
    g->lifecycle_boundary = scene.boundary;
    g->command_status = scene.status == XEEN_SERVICE_REQUIRED ? XGC_EXTERNAL : XGC_INTERNAL;
    return scene.status;
}
/* The random draws a sprite draw's colour effect takes (modes 2 and 5: two per frame cell, the second cell only
 * when the frame table's second word is nonzero), from the game's generator: each second draw's AX is the draw before
 * it, the others' the effect's AX, and each draw's carry flag is recorded. CHECK only tells whether the draw can take
 * them. A draw whose cells or generator are unknown stops before anything is taken.
 * Original: 4ADB1/4AC92 -> 4AEB8 per cell; 50525. */
static xeen_status effect_rng(void *u, uint16_t handle, uint16_t frame, uint16_t scale,
                              uint16_t flags, int check, xeen_effect_rng *e)
{
    xeen_game *g = u;
    uint16_t ax = 0;
    unsigned per = xeen_raster_effect_rng(scale, flags, &ax), cells, i;
    if (!check) {
        memset(e, 0, sizeof *e);
    }
    if (!per) {
        return XEEN_OK;
    }
    cells = xeen_sprite_cells(&g->sprites, handle, frame);
    if (!cells || !g->event_random) {
        return XEEN_SERVICE_REQUIRED;
    }
    if (check) {
        return XEEN_OK;
    }
    for (i = 0; i < cells * per; i++) {
        e->raw[i] = g->event_random(g->event_random_user, i & 1u ? e->raw[i - 1u] : ax);
        e->carry[i] =
                (uint8_t)(g->event_random_carry && g->event_random_carry(g->event_random_user));
    }
    e->count = (uint8_t)(cells * per);
    return XEEN_OK;
}
int xeen_game_bind_random_carry(xeen_game *g, int (*carry)(void *))
{
    if (!g) {
        return 0;
    }
    g->event_random_carry = carry;
    return 1;
}
/* Turning the mouse on empties its press queue: the host's queued presses are read and dropped (at most 64).
 * Original: 4E06B (5C40 = 0). */
static void mouse_reset(void *u)
{
    xeen_game *g = u;
    xeen_mouse_sample s;
    unsigned guard = 0;
    if (!g || !g->input_provider.mouse) {
        return;
    }
    do {
        memset(&s, 0, sizeof s);
        if (!g->input_provider.mouse(g->input_provider.user, &s)) {
            break;
        }
    } while (s.click && ++guard < 64u);
}
int xeen_game_bind_presentation(xeen_game *g, xeen_presentation *p)
{
    if (!g || !p || !g->restored || g->presentation ||
        (g->acted && !(g->phase == XG_STOPPED && g->lifecycle_boundary == XEEN_SCENE_PRESENT))) {
        return 0;
    }
    g->presentation = p;
    p->effect_rng = effect_rng;
    p->effect_rng_user = g;
    p->mouse_reset = mouse_reset;
    p->mouse_reset_user = g;
    if (xeen_presentation_tracing()) {
        xeen_presentation_trace_bind(g->movement.ds); /* Evidence */
    }
    return 1;
}
void xeen_game_destroy(xeen_game *g)
{
    if (g) {
        xeen_presentation_trace_unbind(g->movement.ds);
        xeen_input_poll_destroy(&g->input_poll);
        free(g->guild_desc);
        free(g->spells);
        free(g->state_owned);
        free(g->cur[0].bytes);
        free(g->cur[1].bytes);
        free(g->roster);
        free(g->mae);
        free(g->quests_notes);
        free(g->credits);
        free(g->awards_bin);
        free(g->large);
        free(g->kept_song);
        free(g->voc_bytes);
        free(g->cut_voc[0].bytes);
        free(g->cut_voc[1].bytes);
        free(g->cut_voc[2].bytes);
    }
    free(g);
}
int xeen_game_enable_silent_audio(xeen_game *g)
{
    xeen_caller_result r;
    if (!g || g->native_audio_enabled || !g->restored ||
        (g->acted && !(g->phase == XG_STOPPED && g->lifecycle_boundary == XEEN_AUDIO_EFFECT_CALL))) {
        return 0;
    }
    xeen_native_audio_init(&g->audio);
    g->native_audio_enabled = 1;
    /*15E43 installs the music-driver handle. Native silent backend capability
     * token1 replaces that handle; no DOS driver segment is fabricated. */
    xeen_write16(&g->movement, 0xf430, 1);
    if (g->acted) {
        r = xeen_post_action_resume_audio(&g->movement, &g->audio);
        g->lifecycle_boundary = r.boundary;
        g->command_status = exit_or_external(r.boundary);
    }
    return 1;
}
void xeen_game_set_audio_sink(xeen_game *g, const xeen_audio_sink *sink)
{
    if (!g) {
        return;
    }
    g->audio.sink = sink;
    /* The F42A song loaded before the sink was attached (the program start's). */
    if (g->kept_song) {
        xeen_native_audio_song_buffer(&g->audio, g->kept_song, g->kept_song_size);
    }
}
void xeen_game_set_cd_status(xeen_game *g, int model,
                             void (*observer)(void *, const xeen_cd_status_query *), void *context)
{
    if (!g) {
        return;
    }
    xeen_native_audio_cd_model(&g->audio, model);
    g->audio.cd_status_observer = observer;
    g->audio.cd_status_context = context;
}
/* The driver samples of a VOC named in the world's CC (the combat
 * voices the sink reads by name). */
/**
 * Voice-length hook: returns the playing length in samples of voice NAME in archive CC (0 when it
 * is missing).
 */
static uint32_t voice_lookup(void *u, const char *name, unsigned cc)
{
    xeen_game *g = u;
    size_t n = 0;
    uint8_t *b = cc_extract(g, cc, name, &n);
    uint32_t k = b ? xeen_voc_driver_count(b, n, g->audio.voice_model, g->audio.voice_trim) : 0u;
    free(b);
    return k;
}
void xeen_game_set_voice_status(xeen_game *g, int model, unsigned trim,
                                void (*observer)(void *, const xeen_voice_request *), void *context)
{
    if (!g) {
        return;
    }
    xeen_native_audio_voice_model(&g->audio, model, trim);
    g->audio.voice_observer = observer;
    g->audio.voice_context = context;
    g->audio.voice_lookup = voice_lookup;
    g->audio.voice_lookup_context = g;
}
void xeen_game_set_speech_off(xeen_game *g, int off)
{
    if (g) {
        g->audio.speech_off = (uint8_t)(off != 0);
    }
}
void xeen_game_set_voice_block(xeen_game *g, void (*hold)(void *, uint32_t irqs), void *user)
{
    if (g) {
        g->audio.voice_block = hold;
        g->audio.voice_block_context = user;
    }
}
void xeen_game_set_clock_service(xeen_game *g, xeen_timer_bios bios, void *user)
{
    if (g) {
        g->clock_bios = bios;
        g->clock_user = user;
    }
}
const xeen_native_audio *xeen_game_audio(const xeen_game *g)
{
    return g && g->native_audio_enabled ? &g->audio : NULL;
}
/* The dialogs' key polls (and a few of their ticks) the control panel's resume continues, each a dialog's wait site
 * (game_internal.h). */
static const uint32_t dialog_polls[] = {
    PANEL_POLL_BOUNDARY, PANEL_SAVED_BOUNDARY, PANEL_DIALOG_POLL_BOUNDARY, PANEL_NAME_POLL_BOUNDARY,
    PANEL_NAME_TICK_BOUNDARY, PANEL_PREFERENCE_BOUNDARY,                      /* the control panel and its dialogs */
    LOCATION_POLL_BOUNDARY, LOCATION_TICK_BOUNDARY,                           /* a location's loop */
    INN_POLL_BOUNDARY, INN_SELECT_BOUNDARY,                                   /* the inn, its member selection */
    CREATE_POLL_BOUNDARY, CREATE_TICK_BOUNDARY,                               /* character creation */
    ITEMS_POLL_BOUNDARY, OPTION_WHICH_BOUNDARY, SCROLL0_BOUNDARY, EQUIP_MESSAGE_BOUNDARY, /* items, a message */
    VIEW_TICK_BOUNDARY, VIEW_POLL_BOUNDARY,                                   /* the character view */
    GUILD_POLL_BOUNDARY,                                                      /* the guild */
    CAST_POLL_BOUNDARY, CAST_WHOM_BOUNDARY, CAST_LACK_BOUNDARY,               /* Cast, On Whom?, the lack */
    CAST_ENGAGED_BOUNDARY, CAST_ELEMENT_BOUNDARY,                             /* engaged, Which Element? */
    LLOYD_BOUNDARY, DETECT_BOUNDARY, IDENTIFY_BOUNDARY,                       /* Lloyd's Beacon, Detect, Identify */
    INFO_POLL_BOUNDARY, QREF_POLL_BOUNDARY, QFO_POLL_BOUNDARY,                /* Info, Quick Reference, Options */
    IDENTIFY_ITEM_BOUNDARY, QUESTS_POLL_BOUNDARY,                             /* Identify Item, the Quests viewer */
    EXCHANGE_POLL_BOUNDARY, DETAILS_POLL_BOUNDARY, AWARDS_POLL_BOUNDARY,      /* the view's lists */
    CREATE_XCHG_BOUNDARY,                                                     /* creation's exchange */
    MAP_POLL_BOUNDARY, MAP_FIRST_BOUNDARY, MAP_PASS_BOUNDARY,                 /* the map (its poll, its passes) */
    DISMISS_POLL_BOUNDARY                                                     /* Dismiss */
};
static int is_dialog_poll(uint32_t b)
{
    size_t i;
    for (i = 0; i < sizeof dialog_polls / sizeof dialog_polls[0]; ++i) {
        if (dialog_polls[i] == b) {
            return 1;
        }
    }
    return 0;
}
static int is_menu_poll(uint32_t b)
{
    uint32_t page;
    for (page = 0; page < MENU_CREDITS_PAGES; ++page) {
        if (b == MENU_CREDITS_POLL_BOUNDARY + MENU_CREDITS_POLL_STRIDE * page) {
            return 1;
        }
    }
    return b == MENU_TITLE_POLL_BOUNDARY || b == MENU_POLL_BOUNDARY || b == MENU_DIALOG_POLL_BOUNDARY ||
           b == MENU_OPTIONS_POLL_BOUNDARY || b == MENU_OPTIONS_STILL_POLL_BOUNDARY;
}
xeen_site_kind xeen_game_site_kind(uint32_t b)
{
    switch (b) {
        case MENU_ENTRY_BOUNDARY:
            return XEEN_SITE_MENU_ENTRY;
        case LOCATION_LEAVE_WAIT_BOUNDARY:
        case LOCATION_COME_WAIT_BOUNDARY:
        case LOCATION_END_WAIT_BOUNDARY:
            return XEEN_SITE_LOCATION_VOICE;
        case XEEN_LOOP_COMMAND_POLL:
        case XEEN_LOOP_IDLE_WAIT:
        case XEEN_POLL_MOUSE_RELEASE:
        case XEEN_COMBAT_COMMAND_POLL:
            return XEEN_SITE_INPUT_POLL;
        case XEEN_LOOP_STEP_EVENT:
        case EVENT_TABLE_COMMAND_BOUNDARY:
        case EVENT_SPACE_WAIT_BOUNDARY:
            return XEEN_SITE_EVENT;
        case XEEN_ENTRY_FINISH:
            return XEEN_SITE_WORLD_ENTRY;
        case XEEN_TIMER_FRAME_MARK:
        case XEEN_TIMER_SCENE_MARK:
            return XEEN_SITE_TIMER_MARK;
        case XEEN_SCENE_FRAME_WAIT:
            return XEEN_SITE_FRAME_WAIT;
        case REST_DREAM_BOUNDARY:
        case REST_DREAM_VOICE_BOUNDARY:
        case REST_LAUGH_VOICE_BOUNDARY:
        case REST_DREAM_END_BOUNDARY:
            return XEEN_SITE_REST_WAIT;
        case ITEM_WAIT_BOUNDARY:
        case ITEM_WAIT_POLL_BOUNDARY:
        case ITEM_WAIT_TICK_BOUNDARY:
            return XEEN_SITE_ITEM_WAIT;
        case XEEN_TICK_LOOP_EXIT:
            return XEEN_SITE_LOOP_EXIT;
        case EXIT_DEATH_BOUNDARY:
        case EXIT_BOOK_BOUNDARY:
        case EXIT_VOICE_WAIT_BOUNDARY:
        case HANDOFF_VOICE_WAIT_BOUNDARY:
            return XEEN_SITE_EXIT_STAGE;
        case CUT_SUBTITLE_SITE:
        case VOICE_END_WAIT_BOUNDARY:
            return XEEN_SITE_CUTSCENE;
        case XEEN_SURFACE_EDGE:
            return XEEN_SITE_MAP_EDGE;
        case COMBAT_CONTACT_BOUNDARY:
            return XEEN_SITE_COMBAT_CONTACT;
        case COMBAT_CLEANUP_TAIL_BOUNDARY:
            return XEEN_SITE_COMBAT_CLEANUP;
        case COMBAT_MONSTER_TURN_BOUNDARY:
            return XEEN_SITE_COMBAT_MONSTER_TURN;
        case DRIVER_EXIT_BOUNDARY:
            return XEEN_SITE_DRIVER_EXIT;
        case PROGRAM_EXIT_BOUNDARY:
            return XEEN_SITE_PROGRAM_EXIT;
        case XEEN_EXE_HANDOFF_BOUNDARY:
            return XEEN_SITE_XEEN_EXE_HANDOFF;
        default:
            break;
    }
    if (is_menu_poll(b)) {
        return XEEN_SITE_MENU_POLL;
    }
    if (is_dialog_poll(b)) {
        return XEEN_SITE_DIALOG_POLL;
    }
    if (b >= CUT_FIRST_SITE && b < CUT_SITES_END) {
        return XEEN_SITE_CUTSCENE;
    }
    return XEEN_SITE_COMPUTE;
}
/* The view copies the combat's arrays whole: their sizes must agree (game.h, combat.h). */
_Static_assert(sizeof(((xeen_game_view *)0)->combat_attackers) == sizeof(((xeen_combat *)0)->attackers), "attackers");
_Static_assert(sizeof(((xeen_game_view *)0)->combat_monster_targets) == sizeof(((xeen_combat *)0)->monster_targets),
               "monster targets");
_Static_assert(sizeof(((xeen_game_view *)0)->combat_monster_hits) == sizeof(((xeen_combat *)0)->monster_hits),
               "monster hits");
xeen_game_view xeen_game_inspect(const xeen_game *g)
{
    xeen_game_view v;
    memset(&v, 0, sizeof v);
    if (!g) {
        return v;
    }
    v.position = (xeen_game_start){g->world, g->movement.ds[XEEN_DS_PARTY_X],
                                   g->movement.ds[XEEN_DS_PARTY_Y],
                                   g->movement.ds[XEEN_DS_PARTY_DIRECTION], g->map_id};
    v.command_status = g->command_status;
    v.lifecycle_boundary = g->lifecycle_boundary;
    v.dispatcher_ax = g->dispatcher.return_ax;
    v.clock = xeen_read16(&g->movement, XEEN_DS_MINUTE_OF_DAY);
    v.safe_to_continue = g->phase == XG_NAVIGATION ||
                         (g->continuous && g->entry_complete && g->lifecycle_boundary == XEEN_LOOP_COMMAND_POLL);
    v.phase = g->phase;
    v.navigation = g->navigation;
    v.event_status = g->event_status;
    v.boundary = g->execution.state.boundary;
    v.sequence = g->execution.state.sequence;
    v.offset = g->execution.state.offset;
    v.operation = g->execution.state.operation;
    v.steps = g->execution.state.steps;
    v.presentations = g->ui.presentations;
    v.completions = g->ui.completions;
    v.input = xeen_read16(&g->movement, XEEN_DS_INPUT_CODE);
    v.tile = xeen_game_tile(g, v.position.x, v.position.y);
    v.metadata = xeen_game_metadata(g, v.position.x, v.position.y);
    v.mode = xeen_read16(&g->movement, XEEN_DS_OUTDOORS);
    v.threshold = g->movement.ds[XEEN_DS_MAP_FIELD(THRESHOLD)];
    v.accepted_commands = g->accepted_commands;
    v.scene_returns = g->scene_returns;
    v.idle_scenes = g->idle_scenes;
    v.world_entry_complete = g->entry_complete;
    v.event_eligible = (v.metadata & 0x10) != 0; /* the metadata's bit 10h (bit 4), the console grid's 'e' */
    v.scene_group = g->scene_selected_group;
    v.combat_phase = g->combat.phase;
    memcpy(v.combat_attackers, g->combat.attackers, sizeof v.combat_attackers);
    v.combat_monster_id = g->combat.monster_id;
    v.combat_active_actor = g->combat.active_actor;
    v.combat_commands = g->combat.commands;
    v.combat_actions = g->combat.actions;
    v.combat_last_damage = g->combat.last_damage;
    v.combat_last_hit = g->combat.last_hit;
    v.combat_last_player_actor = g->combat.last_player_actor;
    v.combat_player_attacks = g->combat.player_attacks;
    v.combat_last_attack_score = g->combat.last_attack_score;
    v.combat_last_attack_threshold = g->combat.last_attack_threshold;
    v.combat_monster_actions = g->combat.monster_actions;
    memcpy(v.combat_monster_targets, g->combat.monster_targets, sizeof v.combat_monster_targets);
    memcpy(v.combat_monster_hits, g->combat.monster_hits, sizeof v.combat_monster_hits);
    memcpy(v.combat_monster_attack_scores, g->combat.monster_attack_scores,
           sizeof v.combat_monster_attack_scores);
    memcpy(v.combat_monster_defenses, g->combat.monster_defenses, sizeof v.combat_monster_defenses);
    memcpy(v.combat_monster_damage, g->combat.monster_damage, sizeof v.combat_monster_damage);
    v.combat_monster_deaths = g->combat.monster_deaths;
    v.combat_rewards_applied = g->combat.rewards_applied;
    v.combat_cleanup_complete = g->combat.cleanup_complete;
    v.combat_rounds = g->combat.rounds;
    v.combat_scenes = g->combat.scenes;
    v.combat_blocks = g->combat.blocks;
    v.shots = g->shots;
    v.volley_targets = g->combat.volley_targets;
    v.volley_hits = g->combat.volley_hits;
    v.volley_misses = g->combat.volley_misses;
    v.volley_saves = g->combat.volley_saves;
    v.volley_end = g->combat.volley_end;
    v.combat_rng_draws = g->combat.rng_draws;
    v.combat_rng_trace_complete = g->combat.rng_trace_complete;
    memcpy(v.combat_rng_raw, g->combat.rng_raw, sizeof v.combat_rng_raw);
    memcpy(v.combat_rng_low, g->combat.rng_low, sizeof v.combat_rng_low);
    memcpy(v.combat_rng_high, g->combat.rng_high, sizeof v.combat_rng_high);
    memcpy(v.combat_rng_result, g->combat.rng_result, sizeof v.combat_rng_result);
    v.confirm_ignored_keys = g->confirm_ignored;
    v.choice = (uint8_t)(g->modal.active ? g->choice : 0u);
    v.modal_polls = g->modal_polls;
    v.modal_poll_key = g->modal_poll_key;
    v.item_passes = g->treasure.item_passes;
    v.item_ticks = g->treasure.item_ticks;
    v.loc_tick_reads = g->location_tick_reads;
    v.loc_ticks = g->location_ticks;
    v.cut_events = g->cut_events;
    v.cut_polls = g->cut_polls;
    v.cut_ret = g->cut_ret;
    v.cut_value = g->cut_value;
    v.cut_is_poll = g->cut_is_poll;
    v.cut_poll_next = (uint8_t)cut_poll_next(g);
    v.cut_kind = g->cut_stage ? g->cut_kind : 0;
    v.game_over_stage = (uint8_t)g->exit_stage;
    v.game_over_frames = g->exit_frames;
    v.game_over_skipped_draws = g->exit_skipped;
    v.game_over_event_count = g->exit_event_count;
    memcpy(v.game_over_events, g->exit_events, sizeof v.game_over_events);
    v.menu_stage = g->menu_stage;
    v.menu_polls = g->menu_polls;
    v.menu_stop = g->menu_stop;
    v.session_ready = g->session_ready;
    v.panel_stage = g->panel_stage;
    v.panel_sessions = g->panel_sessions;
    v.panel_polls = g->panel_polls;
    v.panel_stop = g->panel_stop;
    v.saves = g->saves;
    v.new_games = g->new_games;
    v.menu_slot = g->menu_selected;
    return v;
}
