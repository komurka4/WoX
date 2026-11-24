/*
 * The map loads (Phase 246, from game.c): 19E82 completed on a trial session (map_load_commit, the staged
 * archives), the edge steps to the same or another map (xeen_game_resume_transition) and map_reload, the
 * in-place load of the location exits and the other world's maps.
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* 19EDA..1B0B8 of 19E82 on the staged copy next (*g with clone, the CUR
 * image, as its state): the destination identity and the cleared map-owned
 * DAT/entity regions, the maze cache, the event resources, objects, the
 * monsters (the session RNG) and the map sprites with the 2A0FB sky; then
 * only the map-owned state is committed to g, which owns clone on success
 * (clone left empty). The transition and the location exit (28056) share it. */
/* The map load's end: the "Please Wait..." window, when open, closes; the pixels it saved come back (shown by the next
 * update), or without them the window is presented once more. A loaded session runs it at the end of its 19E82
 * (create_seeded), before a presentation is bound: there the window closes in the DS only, as the host has already
 * put back the pixels under the box (xeen_game_show_session_wait). Original: 1B0CE, the end of 19E82: 18765(9)
 * (4F57B); before 1791F's first view. */
void map_load_tail(xeen_game *g)
{
    if (!g->movement.ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_WAIT)]) {
        return;
    }
    xeen_presentation_trace_extra("evclose 9");
    xeen_presentation_trace_extra("evrestore 9"); /* Evidence: 18765(9), 4F57B */
    /* 4F57B refreshes the restored window itself; no
     * second update (the original's 18765(9) makes no 50BF6(9) call). */
    if (g->wait_under) {
        panel_under(g, XEEN_WINDOW_WAIT, 1);
        g->wait_under = 0;
    } else if (g->presentation) {
        (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_WAIT);
    }
    xeen_window_close(&g->movement, XEEN_WINDOW_WAIT);
}
/*
 * The map's head on the trial game NEXT: the cloned state archive opened, the navigation cache prepared
 * for map TARGET with the live party's flag 19h (1A151; see create_seeded), 19EDA..19F2E's destination
 * identity and clears of the map-owned DAT/entity regions before the new members are expanded, the
 * cache's commit and the party's position.
 */
static xeen_resource_status map_load_head(xeen_game *next, const xeen_owned_bytes *clone, uint16_t target)
{
    xeen_navigation_cache cache;
    xeen_resource_status status;
    status = xeen_archive_open(&next->state, clone->bytes, clone->size);
    if (status != XR_OK) {
        return status;
    }
    /* The live party's flag 19h (1A151; see create_seeded). */
    status = xeen_cache_prepare(&next->state, XR_BANK_STATE, target,
                                (xeen_patch_inputs){next->world, clouds_flag19(next->movement.ds)},
                                &cache);
    if (status != XR_OK) {
        return status;
    }
    /* 19EDA..19F2E: destination identity, then clear map-owned DAT/entity
     * regions before the new members are expanded. */
    next->map_id = target;
    memset(next->movement.ds + XEEN_DS_MAP_DATA, 0, 9u * XEEN_MAP_RECORD_BYTES);
    memset(next->movement.ds + XEEN_DS_MAP_MONSTERS, 0, 0x85cu);
    memset(next->movement.ds + XEEN_DS_MAP_OBJECTS, 0,
           XEEN_MAP_OBJECTS_MAX * XEEN_MAP_OBJECT_BYTES);
    memset(next->movement.ds + XEEN_DS_WALL_ITEMS, 0, XEEN_WALL_ITEMS_MAX * XEEN_WALL_ITEM_BYTES);
    next->movement.ds[XEEN_DS_EVENT_OBJECT] = 0;
    next->movement.ds[XEEN_DS_MAP_MONSTER_COUNT] = 0;
    status = xeen_cache_commit(&cache, &next->movement);
    if (status != XR_OK) {
        return status;
    }
    xeen_navigation_set(&next->movement,
                        (xeen_navigation){next->movement.ds[XEEN_DS_PARTY_DIRECTION],
                                          next->movement.ds[XEEN_DS_PARTY_X],
                                          next->movement.ds[XEEN_DS_PARTY_Y],
                                          (uint8_t)next->map_id});
    return XR_OK;
}
/* The map's event resources loaded and prepared on NEXT (the session's event_service their fallback), and
 * NEXT's per-map preparation flags cleared for the entity steps. */
static xeen_resource_status map_load_events(xeen_game *next)
{
    xeen_resource_status status;
    status = xeen_event_resources_load(&next->resources, &next->state, &next->assets, next->map_id,
                                       next->backing, sizeof next->backing, next->resident,
                                       sizeof next->resident, next->texts, sizeof next->texts,
                                       &next->index);
    if (status != XR_OK) {
        return status;
    }
    if (xeen_event_resources_prepare(&next->resources, next->movement.ds,
                                     sizeof next->movement.ds) != XE_OK) {
        return XR_BAD_INPUT;
    }
    next->resources.fallback = event_service;
    next->resources.fallback_user = next;
    next->acted = 0;
    next->objects_prepared = 0;
    next->monsters_prepared = 0;
    next->scene_resources_prepared = 0;
    next->map_sprites_prepared = 0;
    next->phase = XG_NAVIGATION;
    return XR_OK;
}
/*
 * The map's entities on NEXT: its objects, then every destination entity and sprite dependency validated
 * without consuming the live RNG, on a scratch copy. Only after that complete preflight can the
 * authenticated entity RNG (G's) advance, in the original order: the 1A3F0 monster draws, then the map
 * sprites (Darkside's music choice, 504DA(0,6) at 1ABF5 outdoors and 1ADD1 indoors, draws there; Clouds
 * has no random branch).
 */
static xeen_resource_status map_load_entities(xeen_game *g, xeen_game *next)
{
    xeen_archive runtime = next->world ? next->assets : next->other;
    xeen_resource_status status;
    status = xeen_game_prepare_map_objects(next, runtime.bytes, runtime.size);
    if (status != XR_OK) {
        return status;
    }
    {
        xeen_game *probe = malloc(sizeof *probe);
        if (!probe) {
            return XR_CAPACITY;
        }
        *probe = *next;
        probe->movement.user = probe;
        status = xeen_game_prepare_monsters(probe, runtime.bytes, runtime.size,
                                            transition_probe_random, NULL, NULL);
        if (status == XR_OK) {
            status = xeen_game_prepare_map_sprites(probe, runtime.bytes, runtime.size,
                                                   transition_probe_random, NULL);
        }
        free(probe);
        if (status != XR_OK) {
            return status;
        }
    }
    status = xeen_game_prepare_monsters(next, runtime.bytes, runtime.size, g->event_random,
                                        g->event_random_user, NULL);
    if (status != XR_OK) {
        return status;
    }
    status = xeen_game_prepare_map_sprites(next, runtime.bytes, runtime.size, g->event_random,
                                           g->event_random_user);
    if (status != XR_OK) {
        return status;
    }
    next->event_random = g->event_random;
    next->event_random_user = g->event_random_user;
    return XR_OK;
}
/* Commit only map-owned state into the live G: the staged state archive (CLONE, taken: left empty), the movement, sprites, monsters, event resources and music requests. Persistent party/time,
 * platform, input and presentation owners stay in the same live xeen_game object. */
static void map_load_take(xeen_game *g, xeen_game *next, xeen_owned_bytes *clone)
{
    state_adopt(g, clone->bytes, clone->size, next->state);
    *clone = (xeen_owned_bytes){0};
    g->movement = next->movement;
    g->movement.user = g;
    g->sprites = next->sprites;
    g->map_id = next->map_id;
    g->hed_valid = 0; /* 1A244 loads the new map's .hed (hed_cell reads it at its first use) */
    memcpy(g->monster_database, next->monster_database, sizeof g->monster_database);
    g->monster_database_size = next->monster_database_size;
    memcpy(g->backing, next->backing, sizeof g->backing);
    memcpy(g->resident, next->resident, sizeof g->resident);
    memcpy(g->texts, next->texts, sizeof g->texts);
    g->index = next->index;
    g->resources = next->resources;
    g->resources.backing = g->backing;
    g->resources.resident = g->resident;
    g->resources.texts.bytes = g->texts;
    g->resources.fallback = event_service;
    g->resources.fallback_user = g;
    g->objects_prepared = next->objects_prepared;
    g->monsters_prepared = next->monsters_prepared;
    g->scene_resources_prepared = next->scene_resources_prepared;
    g->map_sprites_prepared = next->map_sprites_prepared;
    g->music_requests = next->music_requests;
    memcpy(g->music_name, next->music_name, sizeof g->music_name);
    g->music_flag = next->music_flag;
    g->input_poll.pending_music = next->input_poll.pending_music;
}
/**
 * Completes a map load on the trial game NEXT: opens the cloned state archive, prepares the
 * navigation cache for map TARGET and loads its sprites; returns the first failing resource status.
 * Only a load whose every step succeeded is taken into G (map_load_take).
 */
static xeen_resource_status map_load_commit(xeen_game *g, xeen_game *next, xeen_owned_bytes *clone,
                                            uint16_t target)
{
    xeen_resource_status status;
    status = map_load_head(next, clone, target);
    if (status == XR_OK) {
        status = map_load_events(next);
    }
    if (status == XR_OK) {
        status = map_load_entities(g, next);
    }
    if (status == XR_OK) {
        map_load_take(g, next, clone);
    }
    return status;
}
/*
 * The two buffers a 19E82 map load stages: CUR, a copy of the live state archive (the old map is saved into
 * it by 30F34, then the new map is read from it), and NEXT, a trial copy of the session the new map is loaded
 * into. map_load_commit takes CUR on success (it leaves it empty); map_stage_free releases whatever the stage
 * still owns, and is the only cleanup.
 */
typedef struct {
    xeen_game *next;
    xeen_owned_bytes cur;
} map_stage;
/** Allocates G's stage buffers and copies the live state archive into CUR (NEXT is not filled yet). */
static xeen_resource_status map_stage_open(xeen_game *g, map_stage *s)
{
    s->cur = (xeen_owned_bytes){bytes_copy(g->state.bytes, g->state.size), g->state.size};
    s->next = malloc(sizeof *s->next);
    return s->cur.bytes && s->next ? XR_OK : XR_CAPACITY;
}
/** Releases what stage S still owns. */
static void map_stage_free(map_stage *s)
{
    free(s->cur.bytes);
    free(s->next);
    s->cur = (xeen_owned_bytes){0};
    s->next = NULL;
}
/*
 * The tail of an edge step begun from Shoot's or a spell's surface: the destination cell's damage scene (the step
 * parity flipped) when it has damage, else the defeat scene when the party is defeated, else the command's tail;
 * never a step's own tail.
 * Original: 19D94 returned into 17264 (17339): with its SI (EDGE_DAMAGE) 1733D (F5DF), with F60A 17380 (owner 4),
 * else 17EF4's 17F07; not 17D50.
 */
static xeen_resource_status edge_step_tail(xeen_game *g, uint16_t edge_damage)
{
    xeen_movement *m = &g->movement;
    if (edge_damage) {
        m->ds[XEEN_DS_STEP_PARITY] ^= 1;
        (void)surface_scene(g, 1, SURFACE_SHOOT);
        return XR_OK;
    } /* 1733D, then 17F07 */
    if (m->ds[XEEN_DS_PARTY_DEFEATED]) {
        (void)surface_scene(g, 2, SURFACE_SHOOT);
        return XR_OK;
    } /* 17380, then 17F07 */
    (void)command_tail(g);
    return XR_OK;
}
/*
 * The edge step's trial session: the stage's NEXT is G with the transition armed, and the original boundary
 * helper wraps the byte coordinates and obtains the neighbor through the loaded source DAT. Its LOAD_MAP
 * service is captured here (NEXT's transition_target); the owner replacement follows in the caller.
 */
static xeen_resource_status edge_step_stage(xeen_game *g, map_stage *s)
{
    xeen_resource_status status = map_stage_open(g, s);
    if (status != XR_OK) {
        return status;
    }
    *s->next = *g;
    s->next->movement.user = s->next;
    s->next->transition_active = 1;
    s->next->transition_target = 0;
    return xeen_boundary_commit(&s->next->movement) == XEEN_OK ? XR_OK : XR_BAD_INPUT;
}
/*
 * An edge transition past the world's edge (no map linked there: only Teleport, Etherealize or Jump reach it, from
 * Shoot's or a spell's tail) wraps the coordinate by 16 on the same map and loads nothing; the surface goes on as
 * after a load, with G taking NEXT's movement. A step never gets here (the edge cells refuse it): without the tail
 * flag it is refused.
 * Original: 19D94 past a missing link (DX 0), skipping 19E82 (19E73); 17264 goes on at 17339; 17EF4.
 */
static xeen_resource_status edge_step_same_map(xeen_game *g, xeen_game *next)
{
    xeen_movement *m = &g->movement;
    if (!g->transition_tail) {
        return XR_BAD_INPUT;
    }
    next->transition_active = 0;
    *m = next->movement;
    m->user = g;
    g->navigation = XSN_MOVED;
    g->phase = XG_STOPPED;
    g->transition_tail = 0;
    return XR_OK;
}
/*
 * The map load of an edge step's target: the load's wait window ("Oops..." for a fall, else "Please Wait..." with a
 * UI mode), the party marked as having acted and the key cleared; the current map's state saved into the state file
 * (where Clouds map 15's monster check can set party flag 56), the target loaded and committed, the wait window
 * closed; then the session's map state reset (no event running, the navigation moved).
 * Original: 19E82's head 19E8A..19ED0 (8C85, 9A1C, 28850, 3067 = 1, 189D7), 19ED5's 30F34 (30E49, 2F8F3).
 */
static xeen_resource_status edge_step_load(xeen_game *g, map_stage *s)
{
    xeen_game *next = s->next;
    char error[160];
    xeen_resource_status status;
    next->transition_active = 0;
    reload_wait_open(g, &next->movement);
    next->movement.ds[XEEN_DS_PARTY_ACTED] = 1;
    xeen_write16(&next->movement, XEEN_DS_INPUT_CODE, 0);
    if (!xeen_save_map_live(s->cur.bytes, s->cur.size, next->movement.ds, sizeof next->movement.ds,
                            g->backing, g->resources.record_bytes, error, sizeof error)) {
        return XR_BAD_INPUT;
    }
    status = map_load_commit(g, next, &s->cur, next->transition_target);
    if (status != XR_OK) {
        return status;
    }
    map_load_tail(g);
    g->scene_selected_group = 0;
    g->execution = (xeen_event_execution){0};
    g->event_status = XEE_NO_MATCH;
    g->navigation = XSN_MOVED;
    g->phase = XG_STOPPED;
    return XR_OK;
}
xeen_resource_status xeen_game_resume_transition(xeen_game *g)
{
    map_stage stage = {0};
    xeen_resource_status status;
    xeen_caller_result continuation;
    uint16_t edge_damage;
    /* 17334 -> 19D94 has no world test (both worlds take the same
     * edge step; 19E82's 19F31 selects the Darkside special maps). */
    if (!g || !g->continuous || !g->entry_complete || !xeen_read16(&g->movement, XEEN_DS_OUTDOORS) ||
        g->phase != XG_STOPPED || g->lifecycle_boundary != XEEN_SURFACE_EDGE || !g->event_random ||
        g->ui.pending || g->modal.active || g->event_origin || g->event_scene) {
        return XR_BAD_INPUT;
    }
    /* 17264's SI (the destination cell's damage, read by 17277's
     * 1C1C1(2) before the edge step) survives 19D94; 17339 then runs the
     * 1733D damage scene for the caller (movement 1, Shoot/Cast 4). */
    edge_damage = g->movement.edge_damage;
    g->movement.edge_damage = 0;
    status = edge_step_stage(g, &stage);
    if (status == XR_OK && !stage.next->transition_target) {
        status = edge_step_same_map(g, stage.next);
        map_stage_free(&stage);
        return status == XR_OK ? edge_step_tail(g, edge_damage) : status;
    }
    if (status == XR_OK) {
        status = edge_step_load(g, &stage);
    }
    if (status != XR_OK || g->transition_tail) {
        map_stage_free(&stage);
        if (status != XR_OK) {
            return status;
        }
        g->transition_tail = 0;
        return edge_step_tail(g, edge_damage);
    }
    g->movement.edge_damage = edge_damage;
    continuation = xeen_post_action_after_transition_native(
            &g->movement, game_audio(g));
    map_stage_free(&stage);
    ++g->accepted_commands;
    if (continuation.boundary == XEEN_SURFACE_DAMAGE_SCENE) {
        /* 17339 -> 1733D (F5DF flipped by the post-action), the
         * movement's damage scene; then RETF to 17D56, the 17D50 tail. */
        (void)surface_scene(g, 1, SURFACE_MOVEMENT);
        return XR_OK;
    }
    g->lifecycle_boundary = continuation.boundary;
    g->command_status = exit_or_external(continuation.boundary);
    return XR_OK;
}
/*
 * Loads TARGET in the other world (the map side differs from the session's world, Clouds installed): the old map was
 * saved into the old world's state file (the stage's), which becomes the inactive copy; the map comes from a copy of
 * the other world's state file; the next session's world and its two asset archives swap. Refused without the other
 * archive or that world's state file; XR_CAPACITY without memory.
 * Original: 19E82's load with 96E2 and 95 set: 19F6A's 1A071 case (clouds.dat / xeen.mon / xeenpic.dat, or the
 * Darkside set), then 191DE(96E2); its 30F34 into the old CUR; the 33EB1 pyramid.
 */
static xeen_resource_status map_reload_other_world(xeen_game *g, map_stage *s, uint16_t target)
{
    unsigned w = g->movement.ds[XEEN_DS_MAP_SIDE];
    xeen_owned_bytes old = s->cur; /* the old world's state, with its map saved: the inactive CUR image */
    uint8_t *copy;
    xeen_archive swap;
    xeen_resource_status status;
    if (w > 1u || !g->other_bound || !g->cur[w].bytes) {
        return XR_BAD_INPUT;
    }
    if (!(copy = bytes_copy(g->cur[w].bytes, g->cur[w].size))) {
        return XR_CAPACITY; /* the stage keeps the old state */
    }
    s->cur = (xeen_owned_bytes){copy, g->cur[w].size};
    s->next->world = (uint8_t)w;
    swap = s->next->assets;
    s->next->assets = s->next->other;
    s->next->other = swap;
    status = map_load_commit(g, s->next, &s->cur, target);
    if (status != XR_OK) {
        free(old.bytes);
        return status;
    }
    cur_replace(g, !w, old.bytes, old.size);
    g->world = (uint8_t)w;
    g->assets = s->next->assets;
    g->other = s->next->other;
    map_load_tail(g);
    return XR_OK;
}
/* Reloads the map in place as TARGET, for a location's exit: the wait window, the party marked as having acted, the
 * key cleared; the current map's state saved into the state file (not without a map, as at a shop's exit, nor for
 * Clouds map 85), then TARGET loaded and committed (through the other world's archive when the side differs). The
 * dispatcher, the interpreter and the running location stay as they are. Refused (XR_BAD_INPUT) outside continuous
 * play, without a random source or for a target out of 1..255.
 * Original: 19E82(map) from the 28056 location exit: 19E8A, 3067 = 1, 189D7, 30F34 (C0CC 0, map 85), ED99. */
xeen_resource_status map_reload(xeen_game *g, uint16_t target)
{
    map_stage stage = {0};
    xeen_resource_status status;
    char error[160];
    uint16_t maze;
    if (!g || !g->continuous || !g->event_random || !target || target > 255u) {
        return XR_BAD_INPUT;
    }
    maze = xeen_read16(&g->movement, XEEN_DS_MAP_FIELD(ID));
    reload_wait_open(g, &g->movement);
    g->movement.ds[XEEN_DS_PARTY_ACTED] = 1;
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
    status = map_stage_open(g, &stage);
    if (status == XR_OK && !xeen_map_save_skipped(maze, g->movement.ds[XEEN_DS_WORLD]) &&
        !xeen_save_map_live(stage.cur.bytes, stage.cur.size, g->movement.ds, sizeof g->movement.ds,
                            g->backing, g->resources.record_bytes, error, sizeof error)) {
        status = XR_BAD_INPUT;
    }
    if (status == XR_OK) {
        g->movement.ds[XEEN_DS_PARTY_MAP] = (uint8_t)target;
        *stage.next = *g;
        stage.next->movement.user = stage.next;
        if (g->movement.ds[XEEN_DS_CLOUDS_INSTALLED] && g->movement.ds[XEEN_DS_MAP_SIDE] != g->world) {
            status = map_reload_other_world(g, &stage, target);
        } else {
            status = map_load_commit(g, stage.next, &stage.cur, target);
            if (status == XR_OK) {
                map_load_tail(g);
            }
        }
    }
    map_stage_free(&stage);
    return status;
}
