/*
 * The exploration scene's resumption (Phase 246, from game.c): xeen_game_resume_scene and its stages, the
 * outdoor renderer call 2538B with its 25390 epilogue, the outdoor stages, the scene suffix and its
 * presentation, and the minimap 1E467.
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Where native play stops when this unit cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json, stops_variable.json). */
enum {
    SCENE_STOP_DAMAGE_EVENT_CONTACT = 0x1c17e /* an event's damage would begin a contact fight: not modelled */
};
/**
 * The outdoor renderer's pass, exactly once: the draw list (list 1166h, window 3) collected and submitted (a refused
 * list changes nothing), then its epilogue and the restoration. Original: 2538B, its 25390 epilogue, 1BAA2.
 */
static xeen_status resume_outdoor_render(xeen_game *g)
{
    xeen_caller_result scene;
    /* Outdoor renderer call 2538B (list 1166, window 3), 25390 epilogue
     * and 1BAA2 restoration, each once; a rejected list changes nothing. */
    xeen_status status;
    if (!g->presentation) {
        return XEEN_SERVICE_REQUIRED;
    }
    status = xeen_scene_collect_draw_list(&g->movement, 3, 0x1166, &g->draw_list);
    if (status == XEEN_OK) {
        status = xeen_presentation_submit(g->presentation, &g->draw_list);
    }
    if (status != XEEN_OK) {
        return status;
    }
    scene = xeen_scene_construct_outdoor_finish(&g->movement);
    if (scene.boundary == XEEN_SCENE_OUTDOOR_RESTORE) {
        scene = xeen_scene_outdoor_restore(&g->movement, g->scene_selected_group);
    }
    g->lifecycle_boundary = scene.boundary;
    g->command_status = scene.status == XEEN_SERVICE_REQUIRED ? XGC_EXTERNAL : XGC_INTERNAL;
    return scene.status;
}
/**
 * The outdoor scene's stages: the effects, the monsters (the session's database; refused before they are prepared)
 * and the objects; a stage that is refused stays pending. Original: 1B7F1, 2126C, 219E4.
 */
static xeen_status resume_outdoor_stages(xeen_game *g)
{
    xeen_caller_result scene;
    /* Outdoor 1B7F1 effects, 2126C monsters (caller-owned database) and
     * 219E4 objects; each rejection leaves its stage pending. */
    xeen_status status = XEEN_OK;
    uint32_t current = g->lifecycle_boundary;
    if (current == XEEN_SCENE_OUTDOOR_EFFECTS) {
        scene = xeen_scene_outdoor_effects(&g->movement);
        status = scene.status == XEEN_SERVICE_REQUIRED ? XEEN_OK : scene.status;
    } else if (current == XEEN_SCENE_OUTDOOR_MONSTERS) {
        if (!g->monsters_prepared) {
            return XEEN_BAD_INPUT;
        }
        status = xeen_scene_outdoor_monsters(&g->movement, g->monster_database,
                                             g->monster_database_size);
    } else {
        if (!g->scene_resources_prepared) {
            return XEEN_SERVICE_REQUIRED;
        }
        status = xeen_scene_outdoor_objects(&g->movement);
    }
    if (status != XEEN_OK) {
        return status;
    }
    g->lifecycle_boundary = current == XEEN_SCENE_OUTDOOR_EFFECTS ? XEEN_SCENE_OUTDOOR_MONSTERS : current == XEEN_SCENE_OUTDOOR_MONSTERS ? XEEN_SCENE_OUTDOOR_OBJECTS : XEEN_SCENE_OUTDOOR_SHUFFLE;
    g->command_status = XGC_EXTERNAL;
    return XEEN_SERVICE_REQUIRED;
}
/* The scene's 3066 text (op 2/3/4/39's room 98DA..996F) is shown: 3066 set and no monster in view. */
static int suffix_has_text(const xeen_game *g)
{
    return g->movement.ds[XEEN_DS_SIGN_SHOWN] && g->movement.ds[XEEN_DS_VIEW_MONSTER(0)] == XEEN_VIEW_NO_MONSTER;
}
/*
 * Builds the interface border's draws on the movement all or nothing: every draw must name a resident sprite and a
 * frame below its count, and a shown sign text must end within its 150 bytes; otherwise the movement is put back
 * and the status returned (nothing is drawn). Original: 26306.
 */
static xeen_status suffix_border(xeen_game *g, xeen_border_ops *ops)
{
    xeen_movement *saved = malloc(sizeof *saved);
    xeen_status status;
    if (!saved) {
        return XEEN_BAD_INPUT;
    }
    *saved = g->movement;
    status = xeen_interface_border(&g->movement, ops);
    for (size_t i = 0; status == XEEN_OK && i < ops->count; i++) {
        const xeen_sprite *sp = xeen_sprite_get(&g->sprites, ops->draws[i].handle);
        if (!sp || ops->draws[i].frame >= sp->frames) {
            status = XEEN_BAD_INPUT;
        }
    }
    if (status == XEEN_OK && suffix_has_text(g)) {
        const char *t = (const char *)g->movement.ds + XEEN_DS_SIGN_TEXT;
        if (!memchr(t, 0, XEEN_SIGN_TEXT_BYTES)) {
            status = XEEN_BAD_INPUT; /* 98DA..996F (op 2/3/4/39's room) */
        }
    }
    if (status != XEEN_OK) {
        g->movement = *saved;
    }
    free(saved);
    return status;
}
/*
 * Writes the scene's status text (the sign or location-name text) in window 3 (0, 0, 230 x 149, no border) through
 * the text writer, carrying the text state the last window left (every form the same way; an earlier native drew
 * the location-name form by itself and gave the others a neutral state). Returns the presentation's status, or
 * XEEN_SERVICE_REQUIRED while the text font is missing. Original: 1C104 4E38C(3, 98DA).
 */
static xeen_status suffix_status_text(xeen_game *g)
{
    static const uint16_t window3[4] = {0, 0, 230, 149};
    const uint8_t *text = g->movement.ds + XEEN_DS_SIGN_TEXT;
    xeen_text_layout l = g->layout;
    int ready = g->layout_ready;
    if (!g->text_window.loaded && xeen_text_window_load(&g->text_window, &g->assets) != XR_OK) {
        return XEEN_SERVICE_REQUIRED;
    }
    xeen_text_layout_init(&g->layout, window3, 0);
    g->layout_ready = 1;
    if (ready) {
        layout_carry(g, &l, 1);
    }
    (void)xeen_text_layout_run(&g->layout, &g->text_window, g->presentation->framebuffer, text, 64, 0);
    return xeen_presentation_text(g->presentation, 3, (const char *)text);
}
/**
 * The scene's suffix: during a fall its animation (fall scene 1), or the screen saved for the landing (fall scene
 * 2); the interface border drawn all or nothing (suffix_border) with its text and frame; then the status text
 * (suffix_status_text) and the scene's end. Original: 1BF6E..1C112 (1BF78..1C0DB the fall; 1C0E5 186B2(8C90); 26306;
 * 1C0F6).
 */
static xeen_status resume_scene_suffix(xeen_game *g)
{
    xeen_border_ops ops;
    xeen_status status;
    if (!g->presentation || !g->world_entered) {
        return XEEN_SERVICE_REQUIRED;
    }
    if (g->movement.ds[XEEN_DS_FALL_SCENE] == 1) { /* 1BF78..1C0DB, the fall */
        status = fall_animation(g);
        if (status != XEEN_OK) {
            return status;
        }
    } else if (g->movement.ds[XEEN_DS_FALL_SCENE] == 2) { /* 1C0E5 186B2(8C90) */
        memcpy(g->fall_screen, g->presentation->framebuffer, XEEN_PRESENT_PIXELS);
    }
    status = suffix_border(g, &ops);
    if (status != XEEN_OK) {
        return status;
    }
    status = xeen_presentation_draw_sprites(g->presentation, ops.draws, ops.count);
    if (status == XEEN_OK && ops.has_text) {
        status = xeen_presentation_text(g->presentation, ops.text_window, ops.text);
        interface_text_cursor(g, ops.text);
    }
    if (status == XEEN_OK && ops.has_frame) {
        status = xeen_presentation_frame(g->presentation, ops.frame_window);
    }
    if (status == XEEN_OK && suffix_has_text(g)) {
        status = suffix_status_text(g);
    }
    if (status != XEEN_OK) {
        return status;
    }
    g->lifecycle_boundary = XEEN_SCENE_SUFFIX_PRESENT;
    g->command_status = XGC_EXTERNAL;
    return XEEN_SERVICE_REQUIRED;
}
/* Presents the scene pass's windows on a trial copy of the presentation, adopted only when every step succeeds: with
 * a scene argument, the minimap's and the view frame's windows updated, the page presented, then the speech window
 * when it is open; with none, nothing changes. XEEN_BAD_INPUT when the copy cannot be made, else the first failing
 * step's status. */
static xeen_status scene_present_trial(xeen_game *g)
{
    const uint8_t *ds = g->movement.ds;
    xeen_presentation *trial = malloc(sizeof *trial);
    xeen_status status;
    if (!trial) {
        return XEEN_BAD_INPUT;
    }
    *trial = *g->presentation;
    status = g->scene_argument ? xeen_presentation_update_window(trial, XEEN_WINDOW_MINIMAP) : XEEN_OK;
    if (status == XEEN_OK && g->scene_argument) {
        status = xeen_presentation_update_window(trial, XEEN_WINDOW_VIEW_FRAME);
    }
    if (status == XEEN_OK && g->scene_argument) {
        status = xeen_presentation_present(trial);
    }
    if (status == XEEN_OK && g->scene_argument && ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH)]) {
        status = xeen_presentation_update_window(trial, XEEN_WINDOW_SPEECH);
    }
    if (status == XEEN_OK) {
        *g->presentation = *trial;
    }
    free(trial);
    return status;
}
/**
 * xeen_game_resume_scene at 1C112 (scene argument 1, 17B01's push 1): the windows presented (scene_present_trial),
 * then a contact starts the encounter (unsupported for an event's own party damage); otherwise the party's acted
 * flag (3067) is cleared and the scene waits for its frame, or with UI mode 9 refreshes the event resources first.
 */
static xeen_status resume_scene_present(xeen_game *g)
{
    xeen_status status;
    uint8_t *ds = g->movement.ds;
    if (!g->presentation) {
        return XEEN_SERVICE_REQUIRED;
    }
    if ((status = scene_present_trial(g)) != XEEN_OK) {
        return status;
    }
    if (xeen_scene_contact_due(ds)) {
        /* A contact inside a surface/unlock 2E68D's 30E2D(2)
         * pass (damage_stage) is the generic encounter: 1C17E 38F5F, then
         * 38F5F's RETF into this pass (1C183, 1C18F), 30E3C, 2E8BB 2ABB8,
         * 2E8C5 2F869 and 2E68D's RETF (damage_after_scene; the unlock's
         * 1715A roll). Witnessed in original bytes (labelled synthetic,
         * xeen_dat_evidence/phase216/P216_UNLOCK_FIRST_POLL.md: the turn
         * at 30E37's own 1B56D with a one-pass Who-will). An event-owned
         * 2E68D (damage_event) runs with 9A1C 9: no contact
         * (xeen_dat_evidence/phase213, case D1). */
        if (g->damage_event) {
            g->lifecycle_boundary = SCENE_STOP_DAMAGE_EVENT_CONTACT;
            g->command_status = XGC_UNSUPPORTED;
            return XEEN_SERVICE_REQUIRED;
        }
        g->lifecycle_boundary = COMBAT_CONTACT_BOUNDARY;
        return XEEN_SERVICE_REQUIRED; /* encounter 38F5F */
    }
    ds[XEEN_DS_PARTY_ACTED] = 0;
    g->lifecycle_boundary = ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_EVENT ? XEEN_SCENE_EVENT_REFRESH : XEEN_SCENE_FRAME_WAIT;
    g->command_status = XGC_EXTERNAL;
    g->frame_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_SCENE_ELAPSED};
    return XEEN_SERVICE_REQUIRED;
}
/* Whether the first SHOWN minimap draws can be submitted: each one that draws (no FFFFh handle, frame or scale)
 * names a loaded sprite and one of its frames (the driver's checks before 4AC92 draws); XEEN_BAD_INPUT otherwise. */
static xeen_status minimap_draws_resolved(xeen_game *g, size_t shown)
{
    for (size_t i = 0; i < shown; i++) {
        const xeen_sprite_draw *d = &g->minimap.draws[i];
        const xeen_sprite *sp = xeen_sprite_get(&g->sprites, d->handle);
        if (d->handle == 0xffffu || d->frame == 0xffffu || d->scale == 0xffffu) {
            continue;
        }
        if (!sp || d->frame >= sp->frames) {
            return XEEN_BAD_INPUT;
        }
    }
    return XEEN_OK;
}
/**
 * xeen_game_resume_scene at the minimap 1E467 (called at 1BF5D). The minimap's draws are built
 * (xeen_scene_minimap_driver), checked (minimap_draws_resolved) and submitted (add-on wizard_eye_plus records the pass
 * around the submission), then add-on town_icons marks the
 * towns. A handle above the last (4ACF1) or a frame not below the loaded sprite's count (4AD07) is the driver's
 * fatal exit 4AD19 at that draw (the mirror "&": ED96 = 50h, so 1EA5F's party mark is global.icn frame 51h of 16):
 * the pass ends there, the draws before it reach the screen, and the session ends at the driver's exit. Any other
 * draw that cannot be resolved restores DS and submits nothing. Otherwise the scene goes on to its darkening.
 */
static xeen_status resume_minimap(xeen_game *g)
{
    xeen_movement *saved;
    xeen_status status;
    int fatal = 0;
    size_t shown;
    if (!g->presentation || !g->world_entered) {
        return XEEN_SERVICE_REQUIRED;
    }
    saved = malloc(sizeof *saved);
    if (!saved) {
        return XEEN_BAD_INPUT;
    }
    *saved = g->movement;
    status = xeen_scene_minimap_driver(&g->movement, &g->minimap, minimap_driver_test, &g->sprites,
                                       &fatal);
    shown = fatal ? g->minimap.count - 1u : g->minimap.count;
    if (status == XEEN_OK) {
        status = minimap_draws_resolved(g, shown);
    }
    if (xeen_wizard_eye_plus_on) {
        xeen_wizard_eye_plus_minimap_begin(&g->movement, g->presentation); /* add-on wizard_eye_plus */
    }
    if (status == XEEN_OK) {
        status = xeen_presentation_draw_sprites(g->presentation, g->minimap.draws, shown);
    }
    if (xeen_wizard_eye_plus_on) {
        xeen_wizard_eye_plus_minimap_end(&g->movement, g->presentation, status == XEEN_OK && !fatal && shown);
    }
    if (status == XEEN_OK && !fatal && xeen_town_icons_on) {
        xeen_town_icons_minimap(&g->movement, g->presentation->framebuffer,
                                g->presentation->framebuffer_size); /* add-on town_icons */
    }
    if (status != XEEN_OK) {
        g->movement = *saved;
    }
    free(saved);
    if (status != XEEN_OK) {
        return status;
    }
    if (fatal) {
        const xeen_sprite_draw *d = &g->minimap.draws[shown];
        xeen_presentation_trace_sprite(d); /* the call 4AC92 was entered with */
        driver_exit(g, d->window, d->handle, d->frame);
        return XEEN_SERVICE_REQUIRED;
    }
    g->lifecycle_boundary = XEEN_SCENE_DARKEN;
    g->command_status = XGC_EXTERNAL;
    return XEEN_SERVICE_REQUIRED;
}
/* The boundaries at which xeen_game_resume_scene continues a stopped scene (each case below). */
static int scene_boundary(uint32_t b)
{
    switch (b) {
    case XEEN_SCENE_PRESENT:
    case XEEN_SCENE_INDOOR_TERRAIN:
    case XEEN_SCENE_INDOOR_GEOMETRY:
    case XEEN_SCENE_INDOOR_RENDER:
    case XEEN_SCENE_ANIMATE:
    case XEEN_SCENE_DRAW:
    case XEEN_SCENE_DARKEN:
    case XEEN_SCENE_SUFFIX:
    case XEEN_SCENE_SUFFIX_PRESENT:
    case XEEN_SCENE_EVENT_REFRESH:
    case XEEN_SCENE_INDOOR_SHUFFLE:
    case XEEN_SCENE_INDOOR_OBJECTS:
    case XEEN_SCENE_INDOOR_WALLS:
    case XEEN_SCENE_INDOOR_MONSTERS:
    case XEEN_SCENE_PREPARE_RENDER:
    case XEEN_SCENE_VIEW_WALL_SCAN:
    case XEEN_SCENE_INDOOR_EFFECTS:
    case XEEN_SCENE_OUTDOOR_EFFECTS:
    case XEEN_SCENE_OUTDOOR_MONSTERS:
    case XEEN_SCENE_OUTDOOR_OBJECTS:
    case XEEN_SCENE_OUTDOOR_SHUFFLE:
    case XEEN_SCENE_OUTDOOR_TERRAIN:
    case XEEN_SCENE_OUTDOOR_RENDER:
        return 1;
    default:
        return 0;
    }
}
/* A scene during an event (UI mode 9) restores the map's event resource from its resident copy, then waits for its
 * frame (XEEN_SCENE_FRAME_WAIT) on the scene clock. Original: 1C19B (4A1B7), then 1C1B3. */
static xeen_status resume_event_resource(xeen_game *g)
{
    xeen_event_request request = {XEEN_REQUEST_RESTORE_EVENTS, 0, 0, 0, 0};
    uint16_t ax = 0;
    if (xeen_event_resource_service(&g->resources, &request, g->movement.ds, 65536, &ax) != 1) {
        return XEEN_SERVICE_REQUIRED;
    }
    g->lifecycle_boundary = XEEN_SCENE_FRAME_WAIT;
    g->frame_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_SCENE_ELAPSED};
    return XEEN_SERVICE_REQUIRED;
}
/* The shuffled frame is presented; the scene goes on with the terrain (indoors) or the outdoor terrain slots.
 * Original: 4E33F; indoor 1BECF returns to 1BED4 (22034), outdoor 1BA98 to 1BA9D (246CD). */
static xeen_status resume_present(xeen_game *g)
{
    xeen_status status;
    if (!g->presentation) {
        return XEEN_SERVICE_REQUIRED;
    }
    status = xeen_presentation_present(g->presentation);
    if (status == XEEN_OK) {
        g->lifecycle_boundary = xeen_read16(&g->movement, XEEN_DS_OUTDOORS) ? XEEN_SCENE_OUTDOOR_TERRAIN : XEEN_SCENE_INDOOR_TERRAIN;
    }
    return status == XEEN_OK ? XEEN_SERVICE_REQUIRED : status;
}
/* The indoor renderer's pass, exactly once: the draw list is collected and submitted (a refused list changes
 * nothing), then the construction finishes (restoring the saved group when it asks to) and the scene goes on.
 * Original: 2469A, its 22034 epilogue, the return to 1BED4 and the restoration 1BED9..1BF59. */
static xeen_status resume_indoor_render(xeen_game *g)
{
    xeen_caller_result scene;
    xeen_status status;
    if (!g->presentation) {
        return XEEN_SERVICE_REQUIRED;
    }
    status = xeen_scene_collect_draw_list(&g->movement, 3, 0x962, &g->draw_list);
    if (status == XEEN_OK) {
        status = xeen_presentation_submit(g->presentation, &g->draw_list);
    }
    if (status != XEEN_OK) {
        return status;
    }
    scene = xeen_scene_construct_finish(&g->movement);
    if (scene.boundary == XEEN_SCENE_INDOOR_RESTORE) {
        scene = xeen_scene_restore_group(&g->movement, g->scene_selected_group);
    }
    g->lifecycle_boundary = scene.boundary;
    g->command_status = scene.status == XEEN_SERVICE_REQUIRED ? XGC_EXTERNAL : XGC_INTERNAL;
    return scene.status;
}
/* The view darkens by the light level (only the viewport), then the scene's suffix follows.
 * Original: 1BF62 pushes W(F60C); 1BF66 the driver's 4A68A; then 1BF6E. */
static xeen_status resume_darken(xeen_game *g)
{
    xeen_status status;
    if (!g->presentation) {
        return XEEN_SERVICE_REQUIRED;
    }
    status = xeen_presentation_darken(g->presentation,
                                      xeen_read16(&g->movement, XEEN_DS_VIEW_LIGHT_LEVEL));
    if (status != XEEN_OK) {
        return status;
    }
    g->lifecycle_boundary = XEEN_SCENE_SUFFIX;
    g->command_status = XGC_EXTERNAL;
    return XEEN_SERVICE_REQUIRED;
}
/* The scene's common animation, over the session's monster database (refused before the monsters are prepared;
 * every refusal before any write); a step that needs the host continues at its boundary. Original: 1B0F6 at
 * 1BF59. */
static xeen_status resume_animation(xeen_game *g)
{
    xeen_caller_result scene;
    if (!g->monsters_prepared) {
        return XEEN_BAD_INPUT;
    }
    scene = xeen_scene_animate(&g->movement, g->monster_database, g->monster_database_size);
    if (scene.boundary == XEEN_SCENE_ANIMATE || scene.boundary == XEEN_SCENE_ANIMATE_NO_FRAMES) {
        return scene.status;
    }
    g->lifecycle_boundary = scene.boundary;
    g->command_status = XGC_EXTERNAL;
    return scene.status;
}
/* The indoor view's helper stages, one per step: the monsters, then the objects, then the walls, then the
 * attacking group's shuffle. */
static xeen_status resume_view_helpers(xeen_game *g)
{
    xeen_status status;
    uint32_t current = g->lifecycle_boundary;
    if (current == XEEN_SCENE_INDOOR_MONSTERS) {
        if (!g->monsters_prepared) {
            return XEEN_BAD_INPUT;
        }
        status = xeen_scene_monsters(&g->movement, g->monster_database, g->monster_database_size);
        if (status == XEEN_OK) {
            g->lifecycle_boundary = XEEN_SCENE_INDOOR_OBJECTS;
        }
        return status == XEEN_OK ? XEEN_SERVICE_REQUIRED : status;
    }
    if (!g->scene_resources_prepared) {
        return XEEN_SERVICE_REQUIRED;
    }
    status = current == XEEN_SCENE_INDOOR_OBJECTS ? xeen_scene_objects(&g->movement)
                                : xeen_scene_walls(&g->movement);
    if (status == XEEN_OK) {
        g->lifecycle_boundary = current == XEEN_SCENE_INDOOR_OBJECTS ? XEEN_SCENE_INDOOR_WALLS : XEEN_SCENE_INDOOR_SHUFFLE;
    }
    return status == XEEN_OK ? XEEN_SERVICE_REQUIRED : status;
}
/* The scene construction's steps, each continuing at the boundary the last returned: the outdoor construction and
 * its shuffle, the indoor geometry, terrain and shuffle, the render records, the view's wall scan, and the effect
 * records. Original: 246CD, 1B9B6, 22B25, 22034, 1BDE4, 1B70F, 1C9D4, 1BBDF. */
static xeen_status resume_construct(xeen_game *g)
{
    xeen_caller_result scene;
    scene = g->lifecycle_boundary == XEEN_SCENE_OUTDOOR_TERRAIN ? xeen_scene_construct_outdoor(&g->movement)
            : g->lifecycle_boundary == XEEN_SCENE_OUTDOOR_SHUFFLE
                    ? xeen_scene_outdoor_shuffle(&g->movement, &g->scene_selected_group)
            : g->lifecycle_boundary == XEEN_SCENE_INDOOR_GEOMETRY ? xeen_scene_construct_geometry(&g->movement)
            : g->lifecycle_boundary == XEEN_SCENE_INDOOR_TERRAIN ? xeen_scene_construct_terrain(&g->movement)
            : g->lifecycle_boundary == XEEN_SCENE_INDOOR_SHUFFLE
                    ? xeen_scene_shuffle(&g->movement, &g->scene_selected_group)
            : g->lifecycle_boundary == XEEN_SCENE_PREPARE_RENDER ? xeen_scene_prepare_render(&g->movement)
            : g->lifecycle_boundary == XEEN_SCENE_VIEW_WALL_SCAN ? xeen_scene_scan_view_walls(&g->movement)
                                               : xeen_scene_prepare_effects(&g->movement);
    g->lifecycle_boundary = scene.boundary;
    g->command_status = scene.status == XEEN_SERVICE_REQUIRED ? XGC_EXTERNAL : XGC_INTERNAL;
    return scene.status;
}
/*
 * Continues a scene stopped at one of its boundaries (scene_boundary); first, in a continuous session,
 * a join's treasure dialog (2EA8E) and a fall (173A0, no fall running) that wait at the scene's place.
 */
xeen_status xeen_game_resume_scene(xeen_game *g)
{
    if (g && g->restored && g->phase == XG_STOPPED && g->continuous &&
        g->lifecycle_boundary == XEEN_TREASURE_FOUND) {
        return join_treasure(g);
    }
    if (g && g->restored && g->phase == XG_STOPPED && g->continuous &&
        g->lifecycle_boundary == XEEN_LOOP_FALL && !g->fall_stage) {
        return fall_begin(g);
    }
    if (!g || !g->restored || g->phase != XG_STOPPED || !scene_boundary(g->lifecycle_boundary)) {
        return XEEN_BAD_INPUT;
    }
    switch (g->lifecycle_boundary) {
    case XEEN_SCENE_EVENT_REFRESH:
        return resume_event_resource(g);
    case XEEN_SCENE_PRESENT:
        return resume_present(g);
    case XEEN_SCENE_OUTDOOR_RENDER:
        return resume_outdoor_render(g);
    case XEEN_SCENE_OUTDOOR_EFFECTS:
    case XEEN_SCENE_OUTDOOR_MONSTERS:
    case XEEN_SCENE_OUTDOOR_OBJECTS:
        return resume_outdoor_stages(g);
    case XEEN_SCENE_INDOOR_RENDER:
        return resume_indoor_render(g);
    case XEEN_SCENE_SUFFIX:
        return resume_scene_suffix(g);
    case XEEN_SCENE_SUFFIX_PRESENT:
        return resume_scene_present(g);
    case XEEN_SCENE_DARKEN:
        return resume_darken(g);
    case XEEN_SCENE_DRAW:
        return resume_minimap(g);
    case XEEN_SCENE_ANIMATE:
        return resume_animation(g);
    case XEEN_SCENE_INDOOR_MONSTERS:
    case XEEN_SCENE_INDOOR_OBJECTS:
    case XEEN_SCENE_INDOOR_WALLS:
        return resume_view_helpers(g);
    default:
        return resume_construct(g);
    }
}
